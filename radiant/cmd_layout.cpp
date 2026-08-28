// lambda layout command: parse, cascade, lay out, and emit documents.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../lib/mem.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/uv_loop.h"
#include "../lib/escape.h"
#include <chrono>       // timing - acceptable for profiling
#include <limits.h>
#include <signal.h>
#include <setjmp.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <psapi.h>
#undef ERROR  // windows.h defines ERROR as a macro; conflicts with ParseErrorSeverity::ERROR
#define STDERR_FILENO 2
#else
#include <execinfo.h>
#include <unistd.h>
#include <sys/resource.h>  // getrusage for memory diagnostics
#ifdef __APPLE__
#include <mach/mach.h>     // mach_task_basic_info for current RSS
#endif
#endif

extern "C" {
#include "../lib/mempool.h"
#include "../lib/file.h"
#include "../lib/string.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include "../lib/url.h"
#include "../lib/log.h"
#include "../lib/image.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/font/font.h"
#include "../lib/shell.h"
void log_mem_stage(const char* stage);  // defined in radiant/window.cpp
}

#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/input/html5/html5_parser.h"
#include "../lambda/format/format.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_downloader.h"
#include "network_integration.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/io/mark_builder.hpp"
#include "../radiant/view.hpp"
#include "render.hpp"
#include "../radiant/layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "../radiant/radiant.hpp"
#include "../lib/tagged.hpp"
#include "../lambda/runtime/render_map.h"
#include "../lambda/runtime/template_state.h"

// JS runtime batch reset functions (from lambda/js/)
extern "C" void js_batch_reset(void);
extern "C" void js_dom_batch_reset(void);
extern "C" void js_globals_batch_reset(void);
extern "C" void script_runner_cleanup_heap(void);

// Thread-local eval context (set by runner during JIT execution, stale after return)
extern __thread EvalContext* context;
extern __thread Context* input_context;
// print_view_tree is declared in layout.hpp
// print_item is declared in lambda/ast.hpp

// Forward declarations
Element* get_html_root_element(Input* input);
extern void fontface_cleanup(UiContext* uicon);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count, int* linked_count_out = nullptr);
static void populate_layout_document(DomDocument* doc, DomElement* root,
                                     Element* html_root, HtmlVersion version,
                                     Url* url, Runtime* runtime);
static CssStylesheet* load_pool_backed_stylesheet(CssEngine* css_engine, Pool* pool,
                                                  const char* css_filename,
                                                  const char* log_prefix,
                                                  const char* label, bool warn_missing);
static EnhancedFileCache* layout_prepare_network_resources(UiContext* ui_context,
                                                           DomDocument* doc);

// Current document charset for CSS fallback encoding (set before stylesheet collection)
const char* g_css_document_charset = nullptr;

static void annotate_css_rule_source_file(CssRule* rule, const char* source_file) {
    if (!rule || !source_file) return;

    if (rule->type == CSS_RULE_STYLE) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl && !decl->source_file) {
                decl->source_file = source_file;
            }
        }
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            annotate_css_rule_source_file(rule->data.style_rule.nested_rules[i], source_file);
        }
    } else if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
               rule->type == CSS_RULE_CONTAINER || rule->type == CSS_RULE_SCOPE) {
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            annotate_css_rule_source_file(rule->data.conditional_rule.rules[i], source_file);
        }
    }
}

static void annotate_css_stylesheet_source_file(CssStylesheet* stylesheet, const char* source_file) {
    if (!stylesheet) return;
    const char* stable_source_file = stylesheet->origin_url ? stylesheet->origin_url : source_file;
    if (!stable_source_file) return;
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        annotate_css_rule_source_file(stylesheet->rules[i], stable_source_file);
    }
}

static bool css_file_url_to_local_path(const char* href, char* out_path, size_t out_size) {
    if (!href || !out_path || out_size == 0 || strncmp(href, "file:", 5) != 0) return false;
    const char* path = href + 5;
    if (path[0] == '/' && path[1] == '/') path += 2;
    if (path[0] != '/') return false;

    size_t len = strlen(path);
    if (len + 1 > out_size) return false;
    str_copy(out_path, out_size, path, len);
    return true;
}

static bool css_path_is_http(const char* path) {
    return path && (strncmp(path, "http://", 7) == 0 ||
                    strncmp(path, "https://", 8) == 0);
}

static void css_copy_path(char* out_path, size_t out_size, const char* path) {
    if (!out_path || out_size == 0) return;
    if (!path) {
        out_path[0] = '\0';
        return;
    }
    str_copy(out_path, out_size, path, strlen(path));
}

// CSS references share URL resolution, but only link elements use the layout
// support-root fallback for WPT-style absolute paths.
static bool css_resolve_reference_path(const char* href, const char* base_path,
                                       bool allow_support_root, char* out_path,
                                       size_t out_size, bool* out_is_http) {
    if (!href || !*href || !out_path || out_size == 0) return false;
    out_path[0] = '\0';
    if (out_is_http) *out_is_http = false;

    bool base_is_http = css_path_is_http(base_path);
    if (href[0] == '/' && href[1] != '/' && (!base_path || !base_is_http)) {
        if (!allow_support_root ||
            !radiant_resolve_layout_support_resource_path(href, base_path,
                                                          out_path, out_size)) {
            css_copy_path(out_path, out_size, href);
        }
    } else if (strstr(href, "://") != nullptr) {
        if (strncmp(href, "file:", 5) == 0 &&
            !css_file_url_to_local_path(href, out_path, out_size)) {
            css_copy_path(out_path, out_size, href);
        } else if (strncmp(href, "file:", 5) != 0) {
            css_copy_path(out_path, out_size, href);
        }
    } else if (base_path) {
        Url* base_url = url_parse(base_path);
        Url* resolved_url = base_url && base_url->is_valid
            ? url_parse_with_base(href, base_url) : nullptr;
        if (resolved_url && resolved_url->is_valid) {
            if (resolved_url->scheme == URL_SCHEME_HTTP ||
                resolved_url->scheme == URL_SCHEME_HTTPS) {
                css_copy_path(out_path, out_size, url_get_href(resolved_url));
            } else if (resolved_url->scheme == URL_SCHEME_FILE) {
                char* local_path = url_to_local_path(resolved_url);
                if (local_path) {
                    css_copy_path(out_path, out_size, local_path);
                    mem_free(local_path);
                } else {
                    css_copy_path(out_path, out_size, href);
                }
            } else {
                const char* resolved_href = url_get_href(resolved_url);
                css_copy_path(out_path, out_size, resolved_href ? resolved_href : href);
            }
        } else {
            const char* last_slash = strrchr(base_path, '/');
            if (last_slash) {
                size_t dir_len = (size_t)(last_slash - base_path) + 1;
                if (dir_len + strlen(href) + 1 <= out_size) {
                    memcpy(out_path, base_path, dir_len);
                    str_copy(out_path + dir_len, out_size - dir_len,
                             href, strlen(href));
                } else {
                    css_copy_path(out_path, out_size, href);
                }
            } else {
                css_copy_path(out_path, out_size, href);
            }
        }
        if (resolved_url) url_destroy(resolved_url);
        if (base_url) url_destroy(base_url);
    } else {
        css_copy_path(out_path, out_size, href);
    }

    if (out_path[0] == '\0') css_copy_path(out_path, out_size, href);
    if (out_is_http) *out_is_http = css_path_is_http(out_path);
    return out_path[0] != '\0';
}

struct CssSourceBuffer {
    char* data;
    size_t length;
};

static bool css_load_source(const char* path, bool is_http, bool binary,
                            CssSourceBuffer* source) {
    if (!path || !*path || !source) return false;
    source->data = nullptr;
    source->length = 0;
    if (is_http) {
        source->data = download_http_content_cached(path, &source->length, "./temp/cache");
    } else if (binary) {
        source->data = read_binary_file(path, &source->length);
    } else {
        source->data = read_text_file(path);
        if (source->data) source->length = strlen(source->data);
    }
    return source->data != nullptr;
}

// Forward declaration for charset conversion.
char* convert_charset_to_utf8(const char* content, size_t content_len, const char* from_charset);
void apply_inline_styles_to_tree(DomElement* dom_elem, Pool* pool, int depth = 0);
void log_root_item(Item item, const char* indent="  ");
DomDocument* load_latex_doc(Url* latex_url, int viewport_width, int viewport_height, Pool* pool);

DomDocument* load_lambda_script_doc(Url* script_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_lambda_script_source_doc(Url* script_url, const char* script_source,
                                           int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_xml_doc(Url* xml_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio = 1.0f);
DomDocument* load_image_doc(Url* img_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio = 1.0f);
DomDocument* load_text_doc(Url* text_url, int viewport_width, int viewport_height, Pool* pool);
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena);
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);
static DomDocument* load_html_doc_no_redirect(Url *base, char* doc_url,
    int viewport_width, int viewport_height,
    const DocumentJsHostConfig* js_host_config);

// Element-to-DOM map functions (from dom_element.cpp, Phase 12)
HashMap* element_dom_map_create(void);
void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem);
DomElement* element_dom_map_lookup(HashMap* map, Element* elem);
bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child);

static HtmlVersion classify_html_doctype_identifiers(const char* name,
                                                     const char* public_id,
                                                     const char* system_id) {
    // WHATWG: a doctype without a public identifier is the HTML5 doctype.
    if (!public_id || public_id[0] == '\0') return HTML5;

    int quirks_mode = html5_determine_quirks_mode(
        name, public_id, system_id, false);
    if (quirks_mode == 1) return HTML_QUIRKS;
    if (quirks_mode == 2) return HTML4_01_STRICT;

    if (strstr(public_id, "-//W3C//DTD HTML 4.01//EN") ||
        strstr(public_id, "-//W3C//DTD HTML 4.0//EN")) {
        return HTML4_01_STRICT;
    }
    if (strstr(public_id, "Transitional")) return HTML4_01_TRANSITIONAL;
    if (strstr(public_id, "Frameset")) return HTML4_01_FRAMESET;
    if (strstr(public_id, "-//W3C//DTD XHTML 1.0")) {
        return HTML4_01_STRICT;
    }
    return HTML4_01_STRICT;
}

// Function to determine HTML version from Lambda CSS document DOCTYPE
// This function examines the original Element tree to find DOCTYPE information
// before it gets filtered out during DomElement tree construction
HtmlVersion detect_html_version_from_lambda_element(Element* html_root, Input* input) {
    if (!input || !input->root.item) {
        return HTML5;
    }
    // The input->root contains the full parsed tree including DOCTYPE
    // It's typically a List containing multiple items (DOCTYPE, html element, etc.)
    // HTML5 parser: root is #document element with children including #doctype
    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* root_elem = input->root.element;
        TypeElmt* root_type_elmt = (TypeElmt*)root_elem->type;
        if (root_type_elmt && strview_equal(&root_type_elmt->name, "#document")) {
            List* doc_list = (List*)root_elem;
            for (int64_t i = 0; i < doc_list->length; i++) {
                Item child = doc_list->items[i];
                if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    TypeElmt* child_type = (TypeElmt*)child_elem->type;
                    if (child_type && strview_equal(&child_type->name, "#doctype")) {
                        // Found #doctype — examine attributes per WHATWG spec
                        const char* name = extract_element_attribute(child_elem, "name", nullptr);
                        const char* public_id = extract_element_attribute(child_elem, "publicId", nullptr);
                        const char* system_id = extract_element_attribute(child_elem, "systemId", nullptr);


                        return classify_html_doctype_identifiers(
                            name, public_id, system_id);
                    }
                }
            }
            // #document without #doctype → quirks mode
            return HTML4_01_TRANSITIONAL;
        }
    }
    if (root_type == LMD_TYPE_ARRAY) {
        List* root_list = input->root.array;

        // Search through the list for DOCTYPE element
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                bool is_attribute_doctype = type && str_ieq_const(
                    type->name.str, strlen(type->name.str), "#doctype");
                bool is_content_doctype = type && str_ieq_const(
                    type->name.str, strlen(type->name.str), "!DOCTYPE");

                // The HTML5 parser represents the doctype as #doctype with
                // name/publicId/systemId attributes, while the legacy parser
                // emits !DOCTYPE with a content child.
                if (is_attribute_doctype) {
                    const char* name = extract_element_attribute(elem, "name", nullptr);
                    const char* public_id = extract_element_attribute(elem, "publicId", nullptr);
                    const char* system_id = extract_element_attribute(elem, "systemId", nullptr);
                    return classify_html_doctype_identifiers(name, public_id, system_id);
                }
                if (is_content_doctype) {

                    // Extract DOCTYPE content from the element's children
                    if (elem->length > 0) {
                        Item first_child = elem->items[0];
                        if (get_type_id(first_child) == LMD_TYPE_STRING) {
                            String* doctype_content = (String*)first_child.string_ptr;
                            const char* content = doctype_content->chars;


                            // Parse DOCTYPE content to determine version
                            // Check for HTML 4.01 patterns first (more specific)
                            if (strstr(content, "-//W3C//DTD HTML 4.01//EN")) {
                                return HTML4_01_STRICT;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.01 Transitional//EN")) {
                                return HTML4_01_TRANSITIONAL;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.01 Frameset//EN")) {
                                return HTML4_01_FRAMESET;
                            }

                            // Check for HTML 4.0 patterns
                            if (strstr(content, "-//W3C//DTD HTML 4.0//EN")) {
                                return HTML4_01_STRICT;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.0 Transitional//EN")) {
                                return HTML4_01_TRANSITIONAL;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.0 Frameset//EN")) {
                                return HTML4_01_FRAMESET;
                            }

                            // Check for XHTML patterns
                            if (strstr(content, "-//W3C//DTD XHTML 1.0")) {
                                if (strstr(content, "Strict")) {
                                    return HTML4_01_STRICT;
                                }
                                if (strstr(content, "Transitional")) {
                                    return HTML4_01_TRANSITIONAL;
                                }
                                if (strstr(content, "Frameset")) {
                                    return HTML4_01_FRAMESET;
                                }
                                return HTML4_01_TRANSITIONAL; // Default XHTML 1.0
                            }

                            if (strstr(content, "-//W3C//DTD XHTML 1.1//EN")) {
                                return HTML4_01_TRANSITIONAL;
                            }

                            // HTML5 DOCTYPE: "html" with no public/system identifiers
                            // Must check this AFTER other patterns to avoid false matches
                            if (str_istarts_with_const(content, strlen(content), "html")) {
                                // Skip whitespace after "html"
                                const char* after_html = content + 4;
                                while (*after_html && str_char_is_ascii_space(*after_html)) {
                                    after_html++;
                                }
                                // HTML5 should have nothing after "html" (or only whitespace)
                                if (*after_html == '\0') {
                                    return HTML5;
                                }
                            }

                            // If we found a DOCTYPE but don't recognize it, assume HTML5
                            return HTML5;
                        }
                    }

                    // Empty DOCTYPE content - assume HTML5
                    return HTML5;
                }
            }
        }
    }

    // No DOCTYPE found - use quirks mode (legacy HTML)
    // Per HTML spec, missing DOCTYPE triggers quirks mode, which uses serif fonts
    return HTML4_01_TRANSITIONAL;
}

// Apply live inline attributes directly from the DOM tree. The old parallel
// Element/DOM walk duplicated build_dom_tree's filtering rules and drifted
// whenever scripts inserted or removed nodes.
void apply_inline_styles_to_tree(DomElement* dom_elem, Pool* pool, int depth) {
    if (!dom_elem || !pool) return;
    if (depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    // CSSOM writes live on DomElement; reading that attribute preserves
    // script-created inline styles during later subtree recascades.
    const char* style_text = dom_elem->get_attribute("style");
    if (style_text && *style_text) dom_element_apply_inline_style(dom_elem, style_text);
    for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            apply_inline_styles_to_tree(
                lam::dom_require_element(child), pool, depth + 1);
        }
    }
}

// extract the first renderable HTML root from either parser representation.
Element* get_html_root_element(Input* input) {
    if (!input) return nullptr;
    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_ARRAY) {
        // Old parser: root is a list, search for HTML element
        List* root_list = input->root.array;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments (case-insensitive for DOCTYPE)
                if (!str_ieq_const(type->name.str, strlen(type->name.str), "!DOCTYPE") &&
                    strcmp(type->name.str, "!--") != 0) {
                    return elem;
                }
            }
        }
    }
    else if (root_type == LMD_TYPE_ELEMENT) {
        Element* root_elem = input->root.element;
        TypeElmt* root_type_elmt = (TypeElmt*)root_elem->type;


        // HTML5 parser: root is #document, find html child
        if (strview_equal(&root_type_elmt->name, "#document")) {

            // Search all children of #document for the html element
            // NOTE: HTML5 parser stores children as "attributes" not content
            List* doc_list = (List*)root_elem;


            // Iterate through all items (HTML5 parser doesn't use content_length correctly)
            for (int64_t i = 0; i < doc_list->length; i++) {
                Item child = doc_list->items[i];
                TypeId child_type = get_type_id(child);


                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    TypeElmt* child_type_elmt = (TypeElmt*)child_elem->type;


                    // Return the html element (skip #doctype, comments, etc.)
                    if (strview_equal(&child_type_elmt->name, "html")) {
                        return child_elem;
                    }
                }
            }

            log_warn("No html element found inside #document");
            return nullptr;
        }

        // Old parser or direct html element
        return root_elem;
    }
    return nullptr;
}

// parse a viewport meta content value into document viewport state.
void parse_viewport_content(const char* content, DomDocument* doc) {
    if (!content || !doc) return;


    // Parse comma or semicolon separated key=value pairs
    const char* p = content;
    while (*p) {
        // skip whitespace and separators
        while (*p && (*p == ' ' || *p == ',' || *p == ';' || *p == '\t' || *p == '\n')) p++;
        if (!*p) break;

        // find the key
        const char* key_start = p;
        while (*p && *p != '=' && *p != ',' && *p != ';' && *p != ' ') p++;
        size_t key_len = p - key_start;

        // skip to '='
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (*p != '=') continue;
        p++; // skip '='

        // skip whitespace after '='
        while (*p && (*p == ' ' || *p == '\t')) p++;

        // find the value
        const char* value_start = p;
        while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') p++;
        size_t value_len = p - value_start;

        // parse key-value pairs
        if (key_len > 0 && value_len > 0) {
            char key[64], value[64];
            size_t copy_len = (key_len < 63) ? key_len : 63;
            strncpy(key, key_start, copy_len);
            key[copy_len] = '\0';
            copy_len = (value_len < 63) ? value_len : 63;
            strncpy(value, value_start, copy_len);
            value[copy_len] = '\0';


            if (str_ieq_const(key, strlen(key), "initial-scale")) {
                doc->viewport.initial_scale = (float)str_to_double_default(value, strlen(value), 0.0);
                log_info("[viewport] initial-scale=%.2f", doc->viewport.initial_scale);
            }
            else if (str_ieq_const(key, strlen(key), "minimum-scale")) {
                doc->viewport.min_scale = (float)str_to_double_default(value, strlen(value), 0.0);
            }
            else if (str_ieq_const(key, strlen(key), "maximum-scale")) {
                doc->viewport.max_scale = (float)str_to_double_default(value, strlen(value), 0.0);
            }
            else if (str_ieq_const(key, strlen(key), "width")) {
                if (str_ieq_const(value, strlen(value), "device-width")) {
                    doc->viewport.width = 0;  // 0 means device-width
                } else {
                    doc->viewport.width = (int)str_to_int64_default(value, strlen(value), 0);
                }
            }
            else if (str_ieq_const(key, strlen(key), "height")) {
                if (str_ieq_const(value, strlen(value), "device-height")) {
                    doc->viewport.height = 0;  // 0 means device-height
                } else {
                    doc->viewport.height = (int)str_to_int64_default(value, strlen(value), 0);
                }
            }
        }
    }
}

// find and parse the document viewport meta element.
void extract_viewport_meta(Element* elem, DomDocument* doc) {
    if (!elem || !doc) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <meta> element with name="viewport"
    if (str_ieq_const(type->name.str, strlen(type->name.str), "meta")) {
        const char* name = extract_element_attribute(elem, "name", nullptr);
        if (name && str_ieq_const(name, strlen(name), "viewport")) {
            const char* content = extract_element_attribute(elem, "content", nullptr);
            if (content) {
                parse_viewport_content(content, doc);
            }
        }
        return;  // meta elements have no children to search
    }

    // Stop searching after <body> - viewport meta should be in <head>
    if (str_ieq_const(type->name.str, strlen(type->name.str), "body")) {
        return;
    }

    // Recursively process children
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            extract_viewport_meta(child_item.element, doc);
        }
    }
}

static char* find_refresh_url_in_content(const char* content) {
    if (!content) return nullptr;

    const char* p = content;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ';')) p++;
        if (strncasecmp(p, "url", 3) == 0) {
            const char* q = p + 3;
            while (*q && (*q == ' ' || *q == '\t')) q++;
            if (*q == '=') {
                q++;
                while (*q && (*q == ' ' || *q == '\t')) q++;
                char quote = 0;
                if (*q == '\'' || *q == '"') {
                    quote = *q;
                    q++;
                }
                const char* end = q;
                while (*end && ((quote && *end != quote) || (!quote && *end != ';'))) end++;
                while (end > q && (end[-1] == ' ' || end[-1] == '\t')) end--;
                size_t len = (size_t)(end - q);
                if (len == 0) return nullptr;
                char* out = (char*)mem_alloc(len + 1, MEM_CAT_DOM);
                if (!out) return nullptr;
                memcpy(out, q, len);
                out[len] = '\0';
                return out;
            }
        }
        while (*p && *p != ';') p++;
    }
    return nullptr;
}

static char* find_meta_refresh_url(Element* elem) {
    if (!elem) return nullptr;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    if (str_ieq_const(type->name.str, strlen(type->name.str), "meta")) {
        const char* http_equiv = extract_element_attribute(elem, "http-equiv", nullptr);
        if (!http_equiv) http_equiv = extract_element_attribute(elem, "http_equiv", nullptr);
        if (!http_equiv) http_equiv = extract_element_attribute(elem, "httpEquiv", nullptr);
        if (http_equiv && str_ieq_const(http_equiv, strlen(http_equiv), "refresh")) {
            const char* content = extract_element_attribute(elem, "content", nullptr);
            char* refresh_url = find_refresh_url_in_content(content);
            if (refresh_url && refresh_url[0]) {
                return refresh_url;
            }
        }
        return nullptr;
    }

    if (str_ieq_const(type->name.str, strlen(type->name.str), "body")) {
        return nullptr;
    }

    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            char* result = find_meta_refresh_url(child_item.element);
            if (result) return result;
        }
    }
    return nullptr;
}

// resolve the document's <base href> URL.
const char* extract_base_href(Element* elem) {
    if (!elem) return nullptr;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    // Check if this is a <base> element with href attribute
    if (str_ieq_const(type->name.str, strlen(type->name.str), "base")) {
        const char* href = extract_element_attribute(elem, "href", nullptr);
        if (href && strlen(href) > 0) {
            return href;
        }
        return nullptr;
    }

    // Stop searching after <body> - base should be in <head>
    if (str_ieq_const(type->name.str, strlen(type->name.str), "body")) {
        return nullptr;
    }

    // Recursively process children
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            const char* result = extract_base_href(child_item.element);
            if (result) return result;
        }
    }
    return nullptr;
}

// extract a uniform scale from a transform declaration.
float extract_transform_scale(CssDeclaration* transform_decl) {
    if (!transform_decl || !transform_decl->value) return 1.0f;

    CssValue* value = transform_decl->value;

    // Transform can be a single function or a list of functions
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.values && value->data.list.count > 0) {
        // Iterate through the transform function list
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item || item->type != CSS_VALUE_TYPE_FUNCTION || !item->data.function) continue;

            CssFunction* func = item->data.function;
            if (!func->name) continue;

            // Check for scale functions
            if (str_ieq_const(func->name, strlen(func->name), "scale") && func->arg_count >= 1 && func->args && func->args[0]) {
                CssValue* arg = func->args[0];
                if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                    float scale_x = (float)arg->data.number.value;
#ifndef NDEBUG
                    float scale_y = scale_x;  // uniform scale
                    if (func->arg_count >= 2 && func->args[1] && func->args[1]->type == CSS_VALUE_TYPE_NUMBER) {
                        scale_y = (float)func->args[1]->data.number.value;
                    }
                    log_info("[transform] Found scale(%.3f, %.3f)", scale_x, scale_y);
#endif
                    // Return uniform scale (use x as primary)
                    return scale_x;
                }
            }
            else if (str_ieq_const(func->name, strlen(func->name), "scaleX") && func->arg_count >= 1 && func->args && func->args[0]) {
                if (func->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
                    float scale = (float)func->args[0]->data.number.value;
                    log_info("[transform] Found scaleX(%.3f)", scale);
                    return scale;  // X scale only
                }
            }
            else if (str_ieq_const(func->name, strlen(func->name), "scaleY") && func->arg_count >= 1 && func->args && func->args[0]) {
                if (func->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
                    float scale = (float)func->args[0]->data.number.value;
                    log_info("[transform] Found scaleY(%.3f)", scale);
                    return scale;  // Y scale only
                }
            }
        }
    }
    else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        // Single transform function
        CssFunction* func = value->data.function;
        if (func->name && str_ieq_const(func->name, strlen(func->name), "scale") && func->arg_count >= 1 && func->args && func->args[0]) {
            CssValue* arg = func->args[0];
            if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                float scale = (float)arg->data.number.value;
                log_info("[transform] Found scale(%.3f)", scale);
                return scale;
            }
        }
    }

    return 1.0f;  // no scale transform found
}

// cache the body transform scale used by viewport mapping.
void extract_body_transform_scale(DomElement* root, DomDocument* doc) {
    if (!root || !doc) return;

    // Find body element
    DomElement* body_elem = nullptr;

    // Traverse to find body - typically root is <html>, body is a child
    if (root->tag_name && str_ieq_const(root->tag_name, strlen(root->tag_name), "body")) {
        body_elem = root;
    } else {
        // Search in children
        for (DomNode* child = root->first_child; child; child = child->next_sibling) {
            if (child->node_type == DOM_NODE_ELEMENT) {
                DomElement* child_elem = lam::dom_require_element(child);
                if (child_elem->tag_name && str_ieq_const(child_elem->tag_name, strlen(child_elem->tag_name), "body")) {
                    body_elem = child_elem;
                    break;
                }
                // Also check one level deeper (html > head, body)
                for (DomNode* grandchild = child_elem->first_child; grandchild; grandchild = grandchild->next_sibling) {
                    if (grandchild->node_type == DOM_NODE_ELEMENT) {
                        DomElement* grandchild_elem = lam::dom_require_element(grandchild);
                        if (grandchild_elem->tag_name && str_ieq_const(grandchild_elem->tag_name, strlen(grandchild_elem->tag_name), "body")) {
                            body_elem = grandchild_elem;
                            break;
                        }
                    }
                }
                if (body_elem) break;
            }
        }
    }

    if (!body_elem) {
        return;
    }

    // Get transform property from body's specified styles
    CssDeclaration* transform_decl = dom_element_get_specified_value(body_elem, CSS_PROPERTY_TRANSFORM);
    if (transform_decl) {
        float scale = extract_transform_scale(transform_decl);
        if (scale != 1.0f) {
            doc->viewport.body_transform_scale = scale;
            log_info("[transform] Body transform scale=%.3f", scale);
        }
    }
}

// load nested @import rules in source order.
static CssStylesheet* parse_and_collect_stylesheet(
    CssEngine* engine, const char* css, const char* source_path,
    const char* import_base, Pool* pool, CssStylesheet*** stylesheets,
    int* count, int* capacity, int import_depth);

static void resolve_stylesheet_imports(CssStylesheet* stylesheet, const char* stylesheet_path,
                                        CssEngine* engine, Pool* pool,
                                        CssStylesheet*** stylesheets, int* count,
                                        int* capacity, int depth) {
    if (!stylesheet || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > 5) {
        log_warn("[CSS @import] Maximum @import nesting depth reached (5), skipping");
        return;
    }

    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        CssRule* rule = stylesheet->rules[i];
        if (!rule || rule->type != CSS_RULE_IMPORT) continue;

        const char* import_url = rule->data.import_rule.url;
        if (!import_url || import_url[0] == '\0') continue;


        // Resolve import path relative to the stylesheet or document URL.
        char import_path[1024];
        if (!css_resolve_reference_path(import_url, stylesheet_path, false,
                                        import_path, sizeof(import_path), nullptr)) {
            continue;
        }

        // Load and parse the imported CSS file
        CssSourceBuffer source;
        if (!css_load_source(import_path, css_path_is_http(import_path), false, &source)) {
            log_warn("[CSS @import] Failed to load imported stylesheet: %s", import_path);
            continue;
        }

        char* css_pool_copy = (char*)pool_alloc(pool, source.length + 1);
        if (!css_pool_copy) {
            mem_free(source.data);
            continue;
        }
        str_copy(css_pool_copy, source.length + 1, source.data, source.length);
        mem_free(source.data);

        CssStylesheet* imported = parse_and_collect_stylesheet(
            engine, css_pool_copy, import_path, import_path, pool,
            stylesheets, count, capacity, depth + 1);
        if (!imported || imported->rule_count == 0) {
            log_warn("[CSS @import] Failed to parse imported stylesheet: %s", import_path);
        }
    }
}

static CssStylesheet* parse_and_collect_stylesheet(
    CssEngine* engine, const char* css, const char* source_path,
    const char* import_base, Pool* pool, CssStylesheet*** stylesheets,
    int* count, int* capacity, int import_depth) {
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, source_path);
    annotate_css_stylesheet_source_file(stylesheet, source_path);
    if (!stylesheet || stylesheet->rule_count == 0) return stylesheet;

    if (!lam::pool_grow_array(pool, stylesheets, capacity, *count + 1, 4)) {
        // stylesheet arrays are dereferenced immediately after append; skip this sheet if the pool cannot grow.
        log_error("[CSS] Failed to grow stylesheet array to %d entries", *count + 1);
        return stylesheet;
    }
    (*stylesheets)[*count] = stylesheet;
    (*count)++;
    resolve_stylesheet_imports(stylesheet, import_base, engine, pool,
                               stylesheets, count, capacity, import_depth);
    return stylesheet;
}

static size_t css_utf16_encoded_at_charset_prelude_len(const char* data, size_t len) {
    if (!data || len < 20) return 0;

    bool be = (unsigned char)data[0] == 0x00 && data[1] == '@';
    bool le = data[0] == '@' && (unsigned char)data[1] == 0x00;
    if (!be && !le) return 0;

    const char* prefix = "@charset \"";
    size_t prefix_len = strlen(prefix);
    if (len < prefix_len * 2) return 0;

    char decoded_prefix[11];
    for (size_t i = 0; i < prefix_len; i++) {
        size_t pos = i * 2;
        unsigned char high = (unsigned char)data[pos + (be ? 0 : 1)];
        unsigned char low = (unsigned char)data[pos + (be ? 1 : 0)];
        if (high != 0x00) return 0;
        decoded_prefix[i] = (char)low;
    }
    decoded_prefix[prefix_len] = '\0';
    if (strcmp(decoded_prefix, prefix) != 0) return 0;

    bool saw_close_quote = false;
    for (size_t pos = prefix_len * 2; pos + 1 < len; pos += 2) {
        unsigned char high = (unsigned char)data[pos + (be ? 0 : 1)];
        unsigned char low = (unsigned char)data[pos + (be ? 1 : 0)];
        if (high != 0x00) return 0;
        if (low == '"') {
            saw_close_quote = true;
            continue;
        }
        if (saw_close_quote && low == ';') {
            size_t end = pos + 2;
            if (end < len && data[end] == '\r') end++;
            if (end < len && data[end] == '\n') end++;
            return end;
        }
    }
    return 0;
}

// detect a CSS source encoding using the CSS Syntax precedence order.
const char* detect_css_encoding(const char* data, size_t len, const char* document_charset,
                                      const char* http_charset = nullptr, const char* link_charset = nullptr) {
    if (!data || len == 0) return nullptr;

    // 1. BOM detection (takes absolute precedence)
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        return nullptr; // UTF-8 BOM → UTF-8
    }
    // UTF-16 LE BOM
    if (len >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        return "utf-16le";
    }
    // UTF-16 BE BOM
    if (len >= 2 && (unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF) {
        return "utf-16be";
    }

    // 2. HTTP Content-Type charset (highest after BOM) — must be a recognized charset
    if (http_charset) {
        if (str_ieq_const(http_charset, strlen(http_charset), "utf-8")) return nullptr;
        // validate: only use if it's a recognized charset we can convert
        if (strncasecmp(http_charset, "windows-", 8) == 0 ||
            strncasecmp(http_charset, "iso-8859", 8) == 0 ||
            strncasecmp(http_charset, "utf-16", 6) == 0) {
            return http_charset;
        }
        // bogus/unrecognized HTTP charset → ignore, fall through
    }

    // 3. @charset rule: must be the very first bytes, exactly: @charset "...";
    //    Per spec, @charset "utf-16" and "utf-16be" are NOT valid (the file must
    //    already be readable to parse @charset, meaning it's really UTF-8 or single-byte).
    if (len >= 10 && strncmp(data, "@charset \"", 10) == 0) {
        const char* start = data + 10;
        const char* end = (const char*)memchr(start, '"', len - 10);
        if (end && end > start) {
            size_t clen = end - start;
            if (clen < 32) {
                static char cs_buf[32];
                for (size_t i = 0; i < clen; i++) {
                    cs_buf[i] = (start[i] >= 'A' && start[i] <= 'Z') ? start[i] + 32 : start[i];
                }
                cs_buf[clen] = '\0';
                // @charset "utf-8" → no conversion needed
                if (strcmp(cs_buf, "utf-8") == 0) return nullptr;
                // @charset "utf-16" / "utf-16le" / "utf-16be" → treat as UTF-8 per spec
                if (strncmp(cs_buf, "utf-16", 6) == 0) return nullptr;
                // @charset "bogus" or unrecognized → fall through to document fallback
                // valid recognized @charset declaration → use it
                if (strcmp(cs_buf, "bogus") != 0) {
                    // check if it's a charset we can convert
                    if (strncmp(cs_buf, "windows-", 8) == 0 ||
                        strncmp(cs_buf, "iso-8859", 8) == 0 ||
                        strncmp(cs_buf, "cp12", 4) == 0 ||
                        strcmp(cs_buf, "latin1") == 0 ||
                        strcmp(cs_buf, "latin-1") == 0) {
                        return cs_buf;
                    }
                }
            }
        }
    }

    // 4. Fallback: <link charset=...> attribute overrides document charset
    if (link_charset) {
        if (str_ieq_const(link_charset, strlen(link_charset), "utf-8")) return nullptr;
        // validate: only use if recognized
        if (strncasecmp(link_charset, "windows-", 8) == 0 ||
            strncasecmp(link_charset, "iso-8859", 8) == 0) {
            return link_charset;
        }
    }

    // 5. Fallback to referring document's encoding
    if (document_charset) {
        return document_charset;
    }

    // 6. Default to UTF-8
    return nullptr;
}

// replace invalid UTF-8 and embedded NULs for CSS parsing.
static size_t css_utf8_sequence_length(const char* data, size_t len, size_t offset) {
    unsigned char c = (unsigned char)data[offset];
    if (c == 0x00) return 0;
    if (c < 0x80) return 1;
    if (c < 0xC0) return 0;
    size_t sequence_len = c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF8 ? 4 : 0;
    if (sequence_len == 0 || offset + sequence_len > len) return 0;
    for (size_t i = 1; i < sequence_len; i++) {
        if (((unsigned char)data[offset + i] & 0xC0) != 0x80) return 0;
    }
    return sequence_len;
}

static char* sanitize_utf8_css(const char* data, size_t len) {
    if (!data || len == 0) return nullptr;

    // first pass: check if sanitization is needed and compute output size
    bool needs_sanitize = false;
    size_t out_size = 0;
    for (size_t i = 0; i < len; ) {
        size_t sequence_len = css_utf8_sequence_length(data, len, i);
        if (sequence_len == 0) {
            needs_sanitize = true;
            out_size += 3;
            i++;
        } else {
            out_size += sequence_len;
            i += sequence_len;
        }
    }

    if (!needs_sanitize) return nullptr;

    // second pass: build sanitized output
    char* out = (char*)mem_alloc(out_size + 1, MEM_CAT_LAYOUT);
    if (!out) return nullptr;
    size_t o = 0;
    for (size_t i = 0; i < len; ) {
        size_t sequence_len = css_utf8_sequence_length(data, len, i);
        if (sequence_len == 0) {
            out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
            i++;
        } else {
            memcpy(out + o, data + i, sequence_len);
            o += sequence_len;
            i += sequence_len;
        }
    }
    out[o] = '\0';
    return out;
}

// convert a non-UTF-8 CSS source to UTF-8.
static char* convert_css_to_utf8(const char* data, size_t len, const char* css_charset) {
    if (!data || !css_charset) return nullptr;
    return convert_charset_to_utf8(data, len, css_charset);
}

// resolve an HTTP href against its base URL.
static char* resolve_http_href(const char* href, const char* base_path) {
    if (!href || !*href) return nullptr;

    // already absolute http(s) URL
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        return mem_strdup(href, MEM_CAT_TEMP);
    }
    // protocol-relative //host/path
    if (href[0] == '/' && href[1] == '/' && base_path) {
        const char* scheme_end = strstr(base_path, "://");
        if (scheme_end) {
            size_t scheme_len = scheme_end - base_path;
            size_t out_len = scheme_len + 1 /*':'*/ + strlen(href) + 1;
            char* out = (char*)mem_alloc(out_len, MEM_CAT_TEMP);
            snprintf(out, out_len, "%.*s:%s", (int)scheme_len, base_path, href);
            return out;
        }
        return nullptr;
    }
    // relative — resolve against base_path if base is HTTP
    if (!base_path) return nullptr;
    if (strncmp(base_path, "http://", 7) != 0 && strncmp(base_path, "https://", 8) != 0) {
        return nullptr;
    }
    Url* base_url = url_parse(base_path);
    if (!base_url || !base_url->is_valid) {
        if (base_url) url_destroy(base_url);
        return nullptr;
    }
    Url* resolved = parse_url(base_url, href);
    char* out = nullptr;
    if (resolved && resolved->is_valid &&
        (resolved->scheme == URL_SCHEME_HTTP || resolved->scheme == URL_SCHEME_HTTPS)) {
        const char* s = url_get_href(resolved);
        if (s) out = mem_strdup(s, MEM_CAT_TEMP);
    }
    if (resolved) url_destroy(resolved);
    url_destroy(base_url);
    return out;
}

static void append_external_resource_url(char* url, char*** out_urls,
                                         int* out_count, int* out_capacity) {
    if (!url || !out_urls || !out_count || !out_capacity) return;
    if (*out_count >= *out_capacity) {
        if (!lam::mem_grow_array(out_urls, out_capacity, *out_count + 1, 16,
                                 MEM_CAT_TEMP)) {
            // URL collection appends immediately after growth; skip this URL if the temp list cannot grow.
            log_error("[CSS] Failed to grow external resource URL list to %d entries",
                      *out_count + 1);
            return;
        }
    }
    (*out_urls)[(*out_count)++] = url;
}

// collect external CSS and script URLs for prefetch.
static void collect_external_resource_urls(Element* elem, const char* base_path,
                                            char*** out_urls, int* out_count, int* out_capacity,
                                            int depth) {
    if (!elem || depth > MAX_RADIANT_CSS_TREE_DEPTH) return;
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type || !type->name.str) goto recurse;

    if (str_ieq_const(type->name.str, strlen(type->name.str), "link")) {
        const char* rel = extract_element_attribute(elem, "rel", nullptr);
        const char* href = extract_element_attribute(elem, "href", nullptr);
        if (rel && href && str_ieq_const(rel, strlen(rel), "stylesheet")) {
            char* abs = resolve_http_href(href, base_path);
            append_external_resource_url(abs, out_urls, out_count, out_capacity);
        }
    } else if (str_ieq_const(type->name.str, strlen(type->name.str), "script")) {
        const char* src = extract_element_attribute(elem, "src", nullptr);
        if (src) {
            char* abs = resolve_http_href(src, base_path);
            append_external_resource_url(abs, out_urls, out_count, out_capacity);
        }
    }

recurse:
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_external_resource_urls(child_item.element, base_path,
                                            out_urls, out_count, out_capacity, depth + 1);
        }
    }
}

// prefetch external CSS and script resources for remote documents.
static void prefetch_document_subresources(Element* html_root, const char* base_path) {
    if (!html_root || !base_path) return;
    if (strncmp(base_path, "http://", 7) != 0 && strncmp(base_path, "https://", 8) != 0) return;

    char** urls = nullptr;
    int count = 0;
    int capacity = 0;
    collect_external_resource_urls(html_root, base_path, &urls, &count, &capacity, 0);

    if (count > 0) {
        log_info("[PREFETCH] downloading %d sub-resources in parallel", count);
        double t0 = (double)clock() / CLOCKS_PER_SEC;
        http_prefetch_urls_parallel((const char* const*)urls, count, "./temp/cache", 8);
        double elapsed = (double)clock() / CLOCKS_PER_SEC - t0;
        log_info("[PREFETCH] completed in %.3fs (cpu)", elapsed);
    }

    for (int i = 0; i < count; i++) mem_free(urls[i]);
    if (urls) mem_free(urls);
}

// load one linked stylesheet; document traversal owns source ordering.
static void load_linked_stylesheet(Element* elem, CssEngine* engine, const char* base_path,
                                   Pool* pool, CssStylesheet*** stylesheets,
                                   int* count, int* capacity) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    if (!str_ieq_const(type->name.str, strlen(type->name.str), "link")) return;
    const char* rel = extract_element_attribute(elem, "rel", nullptr);
    const char* href = extract_element_attribute(elem, "href", nullptr);
    if (!rel || !href || !str_ieq_const(rel, strlen(rel), "stylesheet")) return;

    const char* link_charset = extract_element_attribute(elem, "charset", nullptr);
    const char* media = extract_element_attribute(elem, "media", nullptr);
    if (media && !css_evaluate_media_query(engine, media)) return;

    char css_path[1024];
    bool is_http_css = false;
    if (!css_resolve_reference_path(href, base_path, true,
                                    css_path, sizeof(css_path), &is_http_css)) {
        log_warn("[CSS] Failed to resolve stylesheet: %s", href);
        return;
    }
            if (!is_http_css && access(css_path, R_OK) != 0) {
                char shared_path[1024];
                if (radiant_resolve_shared_data_resource_path(href, base_path,
                                                              shared_path, sizeof(shared_path))) {
                    str_copy(css_path, sizeof(css_path), shared_path, strlen(shared_path));
                }
            }


            CssSourceBuffer source;
            if (!css_load_source(css_path, is_http_css, true, &source)) {
                log_warn("[CSS] Failed to load stylesheet: %s", css_path);
                return;
            }
            if (source.length == 0 && source.data[0] == '\0') {
                mem_free(source.data);
                return;
            }
            {
                char* css_content = source.data;
                size_t css_file_size = source.length;

                // Use binary size when available (handles null bytes); fallback to strlen
                size_t css_len = css_file_size > 0 ? css_file_size : strlen(css_content);

                size_t utf16_at_charset_prelude_len = css_utf16_encoded_at_charset_prelude_len(css_content, css_len);
                if (utf16_at_charset_prelude_len > 0 && utf16_at_charset_prelude_len < css_len) {
                    size_t stripped_len = css_len - utf16_at_charset_prelude_len;
                    char* stripped_css = (char*)mem_alloc(stripped_len + 1, MEM_CAT_LAYOUT);
                    if (stripped_css) {
                        memcpy(stripped_css, css_content + utf16_at_charset_prelude_len, stripped_len);
                        stripped_css[stripped_len] = '\0';
                        mem_free(css_content);
                        css_content = stripped_css;
                        css_len = stripped_len;
                        css_file_size = stripped_len;
                    }
                }

                // Check for .headers companion file (WPT HTTP charset simulation)
                const char* http_charset = nullptr;
                static char http_cs_buf[64];
                if (!is_http_css) {
                    char headers_path[1040];
                    snprintf(headers_path, sizeof(headers_path), "%s.headers", css_path);
                    if (access(headers_path, R_OK) == 0) {
                        char* headers = read_text_file(headers_path);
                        if (headers) {
                        // parse: Content-Type: text/css; charset=XXX
                        const char* cs = strstr(headers, "charset=");
                        if (!cs) cs = strstr(headers, "Charset=");
                        if (cs) {
                            cs += 8; // skip "charset="
                            size_t i = 0;
                            while (cs[i] && cs[i] != '\n' && cs[i] != '\r' && cs[i] != ' ' && cs[i] != ';' && i < sizeof(http_cs_buf) - 1) {
                                http_cs_buf[i] = cs[i];
                                i++;
                            }
                            http_cs_buf[i] = '\0';
                            if (i > 0) {
                                http_charset = http_cs_buf;
                            }
                        }
                        mem_free(headers);
                    }
                    } // access check
                }

                // CSS Syntax §3.2: determine encoding and convert to UTF-8 if needed
                const char* css_charset = detect_css_encoding(css_content, css_len, g_css_document_charset,
                                                              http_charset, link_charset);
                if (css_charset) {
                    char* utf8_css = convert_css_to_utf8(css_content, css_len, css_charset);
                    if (utf8_css) {
                        mem_free(css_content);
                        css_content = utf8_css;
                        // Note: can't use strlen here if conversion output contains NUL bytes
                        // (e.g., from UTF-16 encoded files). Sanitize first to replace NULs.
                    }
                }

                // CSS Syntax §3.3: sanitize UTF-8 (replace invalid bytes + NUL bytes with U+FFFD)
                char* sanitized = sanitize_utf8_css(css_content, css_len);
                if (sanitized) {
                    mem_free(css_content);
                    css_content = sanitized;
                }
                // After sanitization, no NUL bytes remain — strlen is safe
                css_len = strlen(css_content);

                // CSS Syntax §3.3: strip UTF-8 BOM (U+FEFF) if present
                char* css_data = css_content;  // track original alloc for free
                if (css_len >= 3 && (unsigned char)css_data[0] == 0xEF &&
                    (unsigned char)css_data[1] == 0xBB && (unsigned char)css_data[2] == 0xBF) {
                    css_data += 3;
                    css_len -= 3;
                }

                char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
                if (css_pool_copy) {
                    str_copy(css_pool_copy, css_len + 1, css_data, css_len);
                    mem_free(css_content);

                    CssStylesheet* stylesheet = parse_and_collect_stylesheet(
                        engine, css_pool_copy, css_path, css_path, pool,
                        stylesheets, count, capacity, 0);
                    if (!stylesheet || stylesheet->rule_count == 0) {
                        log_warn("[CSS] Failed to parse stylesheet or empty: %s", css_path);
                    }
                } else {
                    mem_free(css_content);
                    log_error("[CSS] Failed to allocate memory for CSS content");
                }
            }
}

static void collect_stylesheets_in_document_order(Element* elem, CssEngine* engine,
                                                  const char* base_path, Pool* pool,
                                                  CssStylesheet*** stylesheets,
                                                  int* count, int* capacity,
                                                  int* linked_count,
                                                  int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (type) {
        if (str_ieq_const(type->name.str, strlen(type->name.str), "link")) {
            int before = *count;
            load_linked_stylesheet(elem, engine, base_path, pool,
                                   stylesheets, count, capacity);
            if (linked_count && *count > before) {
                *linked_count += *count - before;
            }
        } else if (str_ieq_const(type->name.str, strlen(type->name.str), "style")) {
            const char* media = extract_element_attribute(elem, "media", nullptr);
            if (!media || css_evaluate_media_query(engine, media)) {
                for (int64_t i = 0; i < elem->length; i++) {
                    Item child_item = elem->items[i];
                    if (get_type_id(child_item) != LMD_TYPE_STRING) continue;
                    String* css_text = (String*)child_item.string_ptr;
                    if (css_text && css_text->len > 0) {
                        parse_and_collect_stylesheet(
                            engine, css_text->chars, "<inline-style>", base_path,
                            pool, stylesheets, count, capacity, 0);
                    }
                }
            }
        }
    }

    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_stylesheets_in_document_order(child_item.element, engine,
                                                  base_path, pool, stylesheets,
                                                  count, capacity, linked_count, depth + 1);
        }
    }
}

// collect live DOM <style> elements after script mutations.
void collect_inline_styles_from_dom(DomElement* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                    CssStylesheet*** stylesheets, int* count,
                                    int* capacity, int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    // Check if this is a <style> element
    if (elem->tag_name && strcasecmp(elem->tag_name, "style") == 0) {
        // Check disabled attribute — skip disabled stylesheets
        if (!elem->has_attribute("disabled")) {
            // Check media attribute
            const char* media = elem->get_attribute("media");
            if (!media || css_evaluate_media_query(engine, media)) {
                // Extract text content from DomText children
                DomNode* child = elem->first_child;
                while (child) {
                    if (child->node_type == DOM_NODE_TEXT) {
                        DomText* text_node = lam::dom_require_text(child);
                        if (text_node->text && text_node->length > 0) {
                            parse_and_collect_stylesheet(
                                engine, text_node->text, "<inline-style>", base_path,
                                pool, stylesheets, count, capacity, 0);
                        }
                    }
                    child = child->next_sibling;
                }
        }
    }
    }

    // Recursively process children
    DomNode* child = elem->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            collect_inline_styles_from_dom(lam::dom_require_element(child), engine, base_path,
                                           pool, stylesheets, count, capacity, depth + 1);
        }
        child = child->next_sibling;
    }
}

static bool dom_node_subtree_has_tag(DomNode* node, const char* tag_name) {
    if (!node || !tag_name || !node->is_element()) return false;
    DomElement* elem = lam::dom_require_element(node);
    if (!elem) return false;
    if (elem->tag_name && strcasecmp(elem->tag_name, tag_name) == 0) return true;

    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (dom_node_subtree_has_tag(child, tag_name)) return true;
    }
    return false;
}

static bool dom_node_or_parent_is_tag(DomJsMutationRecord* record, const char* tag_name) {
    return record &&
           (dom_node_subtree_has_tag(record->target, tag_name) ||
            dom_node_subtree_has_tag(record->parent, tag_name));
}

static bool dom_js_mutation_requires_inline_stylesheet_rescan(DomDocument* doc) {
    if (!doc) return true;
    if (doc->js.mutation_record_overflow > 0) return true;

    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        switch (record->kind) {
            case DOM_JS_MUTATION_CHILD_INSERT:
            case DOM_JS_MUTATION_CHILD_REMOVE:
            case DOM_JS_MUTATION_TREE_REPLACE:
                if (dom_node_or_parent_is_tag(record, "style")) return true;
                break;
            case DOM_JS_MUTATION_TEXT:
            case DOM_JS_MUTATION_ATTRIBUTE:
                if (dom_node_or_parent_is_tag(record, "style")) return true;
                break;
            case DOM_JS_MUTATION_UNKNOWN:
                return true;
            case DOM_JS_MUTATION_STYLE:
            case DOM_JS_MUTATION_STYLE_REPAINT:
            case DOM_JS_MUTATION_CONTROL_VALUE:
            default:
                break;
        }
    }
    return false;
}

// collect linked and inline document stylesheets in source order.
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count, int* linked_count_out) {
    if (!html_root || !engine || !pool || !stylesheet_count) return nullptr;


    *stylesheet_count = 0;
    CssStylesheet** stylesheets = nullptr;
    int stylesheet_capacity = 0;

    // Step 0: Pre-fetch external HTTP sub-resources (CSS, scripts) in parallel
    // so subsequent serial loaders find them already cached on disk.
    prefetch_document_subresources(html_root, base_path);

    // CSS Cascade §6.4: stylesheet source order follows document order across
    // both <link rel=stylesheet> and <style>. A later external sheet must win
    // ties against earlier inline rules, and vice versa.
    int linked_count = 0;
    collect_stylesheets_in_document_order(html_root, engine, base_path, pool,
                                          &stylesheets, stylesheet_count,
                                          &stylesheet_capacity, &linked_count, 0);
    if (linked_count_out) *linked_count_out = linked_count;

    return stylesheets;
}

static void clear_load_stylesheet_cascade_recursive(DomNode* node) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        if (!layout_element_is_anonymous_table_fixup(elem)) {
            dom_element_clear_cascaded_styles(elem);
            // Keep pseudo declarations in the same cascade epoch as element styles.
            dom_element_clear_pseudo_styles(elem);
            elem->set_styles_resolved(false);
        }

        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            clear_load_stylesheet_cascade_recursive(child);
        }
    }
}

static CssStylesheet** layout_merge_css_sources(Pool* pool,
                                                CssStylesheet** first, int first_count,
                                                CssStylesheet** second, int second_count,
                                                int* out_count) {
    if (out_count) *out_count = 0;
    if (!pool || !out_count) return nullptr;

    int capacity = first_count + second_count;
    if (capacity <= 0) return nullptr;
    CssStylesheet** merged = (CssStylesheet**)pool_alloc(
        pool, (size_t)capacity * sizeof(CssStylesheet*));
    if (!merged) return nullptr;
    for (int pass = 0; pass < 2; pass++) {
        CssStylesheet** source = pass == 0 ? first : second;
        int count = pass == 0 ? first_count : second_count;
        for (int i = 0; source && i < count; i++) {
            if (source[i]) merged[(*out_count)++] = source[i];
        }
    }
    return merged;
}

static void store_document_stylesheets(DomDocument* dom_doc,
                                       CssStylesheet** first, int first_count,
                                       CssStylesheet** second, int second_count,
                                       Pool* pool) {
    if (!dom_doc || !pool) return;
    int count = 0;
    CssStylesheet** merged = layout_merge_css_sources(
        pool, first, first_count, second, second_count, &count);
    bool stylesheet_set_changed = count != dom_doc->stylesheet_count;
    for (int i = 0; !stylesheet_set_changed && i < count; i++) {
        stylesheet_set_changed = dom_doc->stylesheets[i] != merged[i];
    }
    dom_doc->stylesheets = merged;
    dom_doc->stylesheet_count = count;
    dom_doc->stylesheet_capacity = count;
    // A new parsed sheet set invalidates the matching document-font registry.
    if (stylesheet_set_changed) dom_doc->font_faces_processed = false;
}

static void layout_apply_css_stylesheets(DomDocument* doc, DomElement* root,
                                         CssStylesheet** stylesheets, int count,
                                         Pool* pool, CssEngine* engine) {
    if (!doc || !root || !pool || !engine || count <= 0) return;
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return;
    state_configure_selector_matcher((DocState*)doc->state, matcher);
    radiant_apply_css_stylesheets_to_tree(
        doc, root, stylesheets, count, pool, engine, matcher);
}

static void apply_load_css_cascade(DomDocument* dom_doc,
                                   DomElement* dom_root,
                                   CssStylesheet* external_stylesheet,
                                   CssStylesheet** inline_stylesheets,
                                   int inline_stylesheet_count,
                                   CssEngine* css_engine,
                                   Pool* pool,
                                   const char* phase) {
    if (!dom_doc || !dom_root || !pool || !css_engine) return;
    using namespace std::chrono;
    auto t_cascade_start = high_resolution_clock::now();
    CssStylesheet* external[] = {external_stylesheet};
    int stylesheet_count = 0;
    CssStylesheet** stylesheets = layout_merge_css_sources(
        pool, external_stylesheet ? external : nullptr, external_stylesheet ? 1 : 0,
        inline_stylesheets, inline_stylesheet_count, &stylesheet_count);
    layout_apply_css_stylesheets(
        dom_doc, dom_root, stylesheets, stylesheet_count, pool, css_engine);
    log_info("[TIMING] load: CSS cascade (%s): %.1fms",
             phase ? phase : "load",
             duration<double, std::milli>(high_resolution_clock::now() - t_cascade_start).count());
}

// check for a UTF-8 BOM.
static bool has_utf8_bom(const char* html, size_t len) {
    return len >= 3 &&
           (unsigned char)html[0] == 0xEF &&
           (unsigned char)html[1] == 0xBB &&
           (unsigned char)html[2] == 0xBF;
}

// heuristically detect valid non-ASCII UTF-8 content.
static bool content_looks_utf8(const char* html, size_t len) {
    size_t scan_len = len < 4096 ? len : 4096;
    int multi_byte_count = 0;
    for (size_t i = 0; i < scan_len; ) {
        unsigned char c = (unsigned char)html[i];
        if (c < 0x80) { i++; continue; }
        // check for valid UTF-8 multi-byte sequence
        int seq_len = 0;
        if ((c & 0xE0) == 0xC0) seq_len = 2;
        else if ((c & 0xF0) == 0xE0) seq_len = 3;
        else if ((c & 0xF8) == 0xF0) seq_len = 4;
        else return false; // invalid UTF-8 lead byte
        if (i + seq_len > scan_len) break; // truncated at scan boundary, don't fail
        for (int j = 1; j < seq_len; j++) {
            if (((unsigned char)html[i + j] & 0xC0) != 0x80) return false; // invalid continuation
        }
        multi_byte_count++;
        i += seq_len;
    }
    return multi_byte_count > 0;
}

// detect HTML charset metadata after applying BOM and UTF-8 precedence.
const char* detect_html_charset(const char* html, size_t len) {
    if (!html || len == 0) return nullptr;

    // Per HTML spec: BOM takes precedence over all other charset declarations
    if (has_utf8_bom(html, len)) return nullptr;

    // only scan the first 1024 bytes (charset must appear early per HTML spec)
    size_t scan_len = len < 1024 ? len : 1024;
    char buf[1025];
    memcpy(buf, html, scan_len);
    buf[scan_len] = '\0';

    // look for <meta charset="...">
    const char* p = buf;
    while ((p = strstr(p, "charset")) != nullptr) {
        p += 7; // skip "charset"
        // skip optional whitespace and '='
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        // skip optional quote
        if (*p == '"' || *p == '\'') p++;

        // extract charset value
        const char* start = p;
        while (*p && *p != '"' && *p != '\'' && *p != ';' && *p != '>' && *p != ' ') p++;
        size_t vlen = p - start;
        if (vlen == 0 || vlen > 30) continue;

        // copy to static buffer (lowercase)
        static char charset_buf[32];
        for (size_t i = 0; i < vlen; i++) {
            charset_buf[i] = (start[i] >= 'A' && start[i] <= 'Z') ? start[i] + 32 : start[i];
        }
        charset_buf[vlen] = '\0';

        // skip if already UTF-8
        if (strcmp(charset_buf, "utf-8") == 0 || strcmp(charset_buf, "utf8") == 0) {
            return nullptr;
        }

        log_info("[charset] Detected non-UTF-8 charset: %s", charset_buf);

        // Safety check: if the content already contains valid UTF-8 multi-byte
        // sequences, the meta charset is likely stale/wrong (file was re-saved as
        // UTF-8 without updating the meta tag). Converting would double-encode.
        if (content_looks_utf8(html, len)) {
            log_info("[charset] Content already appears to be valid UTF-8, ignoring meta charset '%s'", charset_buf);
            return nullptr;
        }

        return charset_buf;
    }

    return nullptr;
}

// convert the common Latin-1/Windows-1252 HTML encodings.
// convert a single-byte encoding using its codepoint table.
static char* convert_single_byte_to_utf8(const char* content, size_t content_len, const uint16_t* table, const char* charset_name) {
    if (!content) return nullptr;
    // Win-1252 maps the Latin-1 C1 control range to the glyphs commonly found
    // in legacy web content; a null table selects that compatibility mapping.
    static const uint16_t win1252_map[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    size_t out_size = content_len * 3 + 1;
    char* out_buf = (char*)mem_alloc(out_size, MEM_CAT_LAYOUT);
    if (!out_buf) return nullptr;

    char* out = out_buf;
    for (size_t i = 0; i < content_len; i++) {
        unsigned char c = (unsigned char)content[i];
        if (c == 0x00) {
            // CSS §3.3: replace NUL with U+FFFD
            *out++ = (char)0xEF; *out++ = (char)0xBF; *out++ = (char)0xBD;
        } else if (c < 0x80) {
            *out++ = (char)c;
        } else {
            uint16_t cp = table ? table[c - 0x80]
                : (c <= 0x9F ? win1252_map[c - 0x80] : c);
            if (cp < 0x80) {
                *out++ = (char)cp;
            } else if (cp < 0x800) {
                *out++ = (char)(0xC0 | (cp >> 6));
                *out++ = (char)(0x80 | (cp & 0x3F));
            } else {
                *out++ = (char)(0xE0 | (cp >> 12));
                *out++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *out++ = (char)(0x80 | (cp & 0x3F));
            }
        }
    }
    *out = '\0';
    log_info("[charset] Converted %zu bytes %s to %zu bytes UTF-8", content_len, charset_name, (size_t)(out - out_buf));
    return out_buf;
}

static char* convert_latin1_to_utf8(const char* content, size_t content_len) {
    return convert_single_byte_to_utf8(
        content, content_len, nullptr, "Latin-1/Win-1252");
}

// Windows-1251 (Cyrillic) to Unicode mapping for 0x80-0xFF
static const uint16_t win1251_table[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, // 80-87
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, // 88-8F
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90-97
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, // 98-9F
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, // A0-A7
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, // A8-AF
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, // B0-B7
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, // B8-BF
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, // C0-C7
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, // C8-CF
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, // D0-D7
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, // D8-DF
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, // E0-E7
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, // E8-EF
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, // F0-F7
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, // F8-FF
};

// Windows-1250 (Central European) to Unicode mapping for 0x80-0xFF
static const uint16_t win1250_table[128] = {
    0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021, // 80-87
    0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179, // 88-8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90-97
    0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, // 98-9F
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, // A0-A7
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, // A8-AF
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, // B0-B7
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, // B8-BF
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, // C0-C7
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, // C8-CF
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, // D0-D7
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, // D8-DF
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, // E0-E7
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, // E8-EF
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, // F0-F7
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9, // F8-FF
};

// Windows-1253 (Greek) to Unicode mapping for 0x80-0xFF
static const uint16_t win1253_table[128] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 80-87
    0x0088, 0x2030, 0x008A, 0x2039, 0x008C, 0x008D, 0x008E, 0x008F, // 88-8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90-97
    0x0098, 0x2122, 0x009A, 0x203A, 0x009C, 0x009D, 0x009E, 0x009F, // 98-9F
    0x00A0, 0x0385, 0x0386, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, // A0-A7
    0x00A8, 0x00A9, 0xFFFD, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x2015, // A8-AF
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x0384, 0x00B5, 0x00B6, 0x00B7, // B0-B7
    0x0388, 0x0389, 0x038A, 0x00BB, 0x038C, 0x00BD, 0x038E, 0x038F, // B8-BF
    0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397, // C0-C7
    0x0398, 0x0399, 0x039A, 0x039B, 0x039C, 0x039D, 0x039E, 0x039F, // C8-CF
    0x03A0, 0x03A1, 0xFFFD, 0x03A3, 0x03A4, 0x03A5, 0x03A6, 0x03A7, // D0-D7
    0x03A8, 0x03A9, 0x03AA, 0x03AB, 0x03AC, 0x03AD, 0x03AE, 0x03AF, // D8-DF
    0x03B0, 0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7, // E0-E7
    0x03B8, 0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BE, 0x03BF, // E8-EF
    0x03C0, 0x03C1, 0x03C2, 0x03C3, 0x03C4, 0x03C5, 0x03C6, 0x03C7, // F0-F7
    0x03C8, 0x03C9, 0x03CA, 0x03CB, 0x03CC, 0x03CD, 0x03CE, 0xFFFD, // F8-FF
};

// convert supported non-UTF-8 HTML sources.
char* convert_charset_to_utf8(const char* content, size_t content_len, const char* from_charset) {
    if (!content || !from_charset) return nullptr;

    // ISO-8859-1 and Windows-1252 use the same converter
    if (strcmp(from_charset, "iso-8859-1") == 0 ||
        strcmp(from_charset, "latin1") == 0 ||
        strcmp(from_charset, "latin-1") == 0 ||
        strcmp(from_charset, "windows-1252") == 0 ||
        strcmp(from_charset, "cp1252") == 0) {
        return convert_latin1_to_utf8(content, content_len);
    }

    if (strcmp(from_charset, "windows-1251") == 0 || strcmp(from_charset, "cp1251") == 0) {
        return convert_single_byte_to_utf8(content, content_len, win1251_table, "windows-1251");
    }

    if (strcmp(from_charset, "windows-1250") == 0 || strcmp(from_charset, "cp1250") == 0) {
        return convert_single_byte_to_utf8(content, content_len, win1250_table, "windows-1250");
    }

    if (strcmp(from_charset, "windows-1253") == 0 || strcmp(from_charset, "cp1253") == 0) {
        return convert_single_byte_to_utf8(content, content_len, win1253_table, "windows-1253");
    }

    log_warn("[charset] Unsupported charset '%s', proceeding with raw content", from_charset);
    return nullptr;
}

// generate a self-contained load-error document.
static char* generate_error_page_html(const char* url, const char* error_title, const char* error_detail) {
    char buf[4096];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<title>%s</title>"
        "<style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        "max-width: 600px; margin: 80px auto; padding: 20px; color: #333; }"
        "h1 { color: #c00; font-size: 24px; }"
        "p { line-height: 1.6; }"
        ".url { word-break: break-all; color: #666; font-size: 14px; "
        "background: #f5f5f5; padding: 8px 12px; border-radius: 4px; }"
        "</style></head><body>"
        "<h1>%s</h1>"
        "<p>%s</p>"
        "<p class=\"url\">%s</p>"
        "</body></html>",
        error_title, error_title, error_detail, url ? url : "");
    return mem_strdup(buf, MEM_CAT_LAYOUT);
}

// load, style, and build an HTML document.
struct HtmlLoadPhaseTiming {
    double loader_total_ms;
    double read_ms;
    double html_parse_ms;
    double dom_build_ms;
    double css_parse_ms;
    double stylesheet_setup_ms;
    double inline_style_ms;
    double initial_cascade_ms;
    double script_exec_ms;
    double post_script_ms;
    double final_cascade_ms;
    double finalize_ms;
};

static DomDocument* load_lambda_html_doc_profiled(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source,
    bool track_source_lines, bool execute_scripts, HtmlLoadPhaseTiming* timing,
    DocumentScriptPhaseTiming* script_timing, const DocumentJsHostConfig* js_host_config) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    log_mem_stage("load_html: enter");

    if (!html_url || !pool) {
        log_error("load_lambda_html_doc: invalid parameters");
        return nullptr;
    }

    Url* superseded_html_urls[4] = {nullptr};
    int superseded_html_url_count = 0;

    char* html_filepath = url_to_local_path(html_url);

    // Step 1: Parse HTML with Lambda parser
    // If html_source is provided, use it directly; otherwise read from file or download
    char* html_content = nullptr;
    bool html_content_owned = false;
    if (html_source) {
        html_content = const_cast<char*>(html_source);
    } else if (html_url->scheme == URL_SCHEME_HTTP || html_url->scheme == URL_SCHEME_HTTPS) {
        const char* url_str = url_get_href(html_url);
        size_t content_size = 0;
        char* eff_url = nullptr;
        html_content = download_http_content(url_str, &content_size, nullptr, &eff_url);
        // Update document URL if redirected (e.g. google.com → www.google.com)
        if (eff_url) {
            Url* redirected_url = url_parse(eff_url);
            if (redirected_url && redirected_url->is_valid) {
                log_info("[redirect] Updating document URL: %s → %s", url_str, eff_url);
                // the returned document owns the final URL, so keep replaced
                // URLs until success instead of leaving them unreachable.
                if (html_url && html_url != redirected_url && superseded_html_url_count < 4) {
                    superseded_html_urls[superseded_html_url_count++] = html_url;
                }
                html_url = redirected_url;
            } else if (redirected_url) {
                url_destroy(redirected_url);
            }
            mem_free(eff_url);
        }
        if (!html_content) {
            log_error("Failed to download HTML from URL: %s", url_str);
            // generate error page instead of returning nullptr
            html_content = generate_error_page_html(url_str,
                "Page Could Not Be Loaded",
                "The requested page could not be loaded. The server may be unreachable, "
                "the URL may be incorrect, or there may be a network connectivity issue.");
            if (!html_content) return nullptr;
        }
        html_content_owned = true;
    } else {
        html_content = read_text_file(html_filepath);
        if (!html_content) {
            log_error("Failed to read HTML file: %s", html_filepath);
            mem_free(html_filepath);
            return nullptr;
        }
        html_content_owned = true;
    }

    // Detect non-UTF-8 charset and convert if needed
    const char* detected_charset = nullptr;
    if (html_content && html_content_owned) {
        size_t html_len = strlen(html_content);
        detected_charset = detect_html_charset(html_content, html_len);
        if (detected_charset) {
            char* utf8_content = convert_charset_to_utf8(html_content, html_len, detected_charset);
            if (utf8_content) {
                mem_free(html_content);
                html_content = utf8_content;
            }
            // if conversion fails, proceed with original content (best effort)
        }
    }

    auto t_read = high_resolution_clock::now();
    log_info("[TIMING] load: read file: %.1fms", duration<double, std::milli>(t_read - t_start).count());

    // Create type string for HTML
    String* type_str = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_LAYOUT);
    type_str->len = 4;
    str_copy(type_str->chars, type_str->len + 1, "html", 4);

    Input* input = nullptr;
    if (track_source_lines) {
        // Use extended parser to record source line numbers on elements
        input = Input::create(pool, html_url);
        if (input) {
            input->ui_mode = true;
            Html5ParseOptions parse_opts = { .track_source_lines = true };
            Element* doc = html5_parse_ex(input, html_content, &parse_opts);
            if (doc) {
                input->root = (Item){.element = doc};
            }
        }
    } else {
        // Create Input first so we can set ui_mode before parsing
        input = Input::create(pool, html_url);
        if (input) {
            input->ui_mode = true;
            Element* doc = html5_parse(input, html_content);
            if (doc) {
                input->root = (Item){.element = doc};
            }
        }
    }
    mem_free(type_str);
    if (html_content_owned) mem_free(html_content);  // only free what we allocated

    auto t_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse HTML: %.1fms", duration<double, std::milli>(t_parse - t_read).count());
    log_mem_stage("load_html: html_parsed");

    if (!input) {
        log_error("Failed to create input for file: %s", html_filepath);
        mem_free(html_filepath);
        return nullptr;
    }
    mem_free(html_filepath);
    html_filepath = nullptr;

    auto t_debug = high_resolution_clock::now();
    log_info("[TIMING] load: debug output: %.1fms", duration<double, std::milli>(t_debug - t_parse).count());

    Element* html_root = get_html_root_element(input);
    if (!html_root) {
        log_error("Failed to get HTML root element");
        return nullptr;
    }

    // Detect HTML version from the original input tree (contains DOCTYPE)
    int detected_version = HTML4_01_TRANSITIONAL;  // Default to quirks mode
    if (input) {
        detected_version = detect_html_version_from_lambda_element(nullptr, input);
    }
    log_root_item((Item){.element = html_root});

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }
    // parsed HTML: the only page kind that may host a JS DOM script realm.
    dom_doc->page_kind = DOM_PAGE_KIND_HTML;
    // Scripts may call getClientRects() during load. Preserve the parsed mode
    // before that first layout so transient measurements use the same initial
    // values as the eventual post-script layout.
    dom_doc->html_version = (HtmlVersion)detected_version;
    log_debug("[page-kind] html document -> %s",
              dom_page_kind_name(dom_doc->page_kind));
    if (js_host_config) {
        // The document Runtime is not bound yet.  Keep these settings on the
        // document until script_runner binds its owner context.
        dom_doc->js.host_ui_context = js_host_config->ui_context;
        dom_doc->js.host_driven_loop = js_host_config->host_driven_loop;
        dom_doc->js.auto_close_event_loop = js_host_config->auto_close_event_loop;
        dom_doc->js.virtual_clock_enabled = js_host_config->virtual_clock_enabled;
        dom_doc->js.virtual_clock_ms = js_host_config->virtual_clock_ms;
    }
    dom_doc->document_charset = detected_charset;
    // HTML parsing always runs with scripting enabled in the layout loader;
    // retain that mode so noscript can suppress only its rendered contents.
    dom_doc->html_scripting_enabled = true;

    // Extract viewport meta tag values before building DOM tree
    extract_viewport_meta(html_root, dom_doc);
    // If viewport initial-scale is set and given_scale is default (1.0), apply it
    if (dom_doc->viewport.initial_scale != 1.0f && dom_doc->viewport.given_scale == 1.0f) {
        dom_doc->viewport.given_scale = dom_doc->viewport.initial_scale;
        log_info("[viewport] Applied initial-scale=%.2f to given_scale", dom_doc->viewport.given_scale);
    }

    // Extract <base href="..."> and update document URL if found
    // This ensures relative URLs resolve correctly when loading remote HTML
    const char* base_href = extract_base_href(html_root);
    if (base_href) {
        Url* base_url = url_parse(base_href);
        if (base_url && base_url->is_valid) {
            log_info("[base] Overriding document URL with base href: %s", base_href);
            // Input and DomDocument must agree on the same owned Url; otherwise
            // replacing html_url for <base> leaves the initial parse URL leaked.
            if (input && input->url == html_url) {
                input->url = base_url;
            }
            if (html_url && html_url != base_url && superseded_html_url_count < 4) {
                superseded_html_urls[superseded_html_url_count++] = html_url;
            }
            html_url = base_url;
        } else if (base_url) {
            url_destroy(base_url);
        }
    }

    // Page scripts execute before final document bookkeeping below, but their
    // URL-dependent globals and relative fetches must already observe the
    // redirected/<base>-adjusted document URL.
    dom_doc->url = html_url;

    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree");
        dom_document_destroy(dom_doc);
        return nullptr;
    }
    log_mem_stage("load_html: dom_built");

    auto t_dom = high_resolution_clock::now();

    // Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        return nullptr;
    }
    // Cache for runtime re-cascade (e.g. on pseudo-state changes like :hover)
    dom_doc->services.cached_css_engine = css_engine;
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Load external CSS if provided
    CssStylesheet* external_stylesheet = nullptr;
    if (css_filename) {
        external_stylesheet = load_pool_backed_stylesheet(
            css_engine, pool, css_filename, "CSS", "stylesheet", true);
    }

    // Extract and parse <link rel="stylesheet"> and <style> elements
    int inline_stylesheet_count = 0;
    int linked_stylesheet_count = 0;
    const char* css_base_path = url_get_href(html_url);
    g_css_document_charset = dom_doc->document_charset; // set fallback encoding for CSS files
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, css_base_path, pool, &inline_stylesheet_count, &linked_stylesheet_count);
    g_css_document_charset = nullptr; // reset after CSS collection

    auto t_css_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse CSS: %.1fms", duration<double, std::milli>(t_css_parse - t_dom).count());
    log_mem_stage("load_html: css_parsed");

    // Store stylesheets before scripts so getComputedStyle and @font-face share
    // the same sheet list that the pre-script cascade uses.
    CssStylesheet* external_sources[] = {external_stylesheet};
    store_document_stylesheets(dom_doc,
                               external_stylesheet ? external_sources : nullptr,
                               external_stylesheet ? 1 : 0,
                               inline_stylesheets, inline_stylesheet_count, pool);
    auto t_stylesheet_setup = timing ? high_resolution_clock::now() : t_css_parse;

    // Step 2c: Apply inline style="" attributes BEFORE scripts
    // Inline style="" attributes from HTML are applied first as the baseline.
    // JS style modifications (element.style.xxx = 'value') are applied after
    // via dom_element_apply_inline_style with a later source_order, so they
    // correctly override the original HTML inline styles in the cascade.
    // This also ensures the HTML→DOM tree mapping is pristine (no JS mutations yet).
    log_mem_stage("load_html: before_inline_attrs");
    apply_inline_styles_to_tree(dom_root, pool);
    log_mem_stage("load_html: after_inline_attrs");
    auto t_inline_style = timing ? high_resolution_clock::now() : t_stylesheet_setup;

    dom_doc->root = dom_root;  // set root for CSSOM and JS DOM API access

    // Scripts read computed styles during load, so the initial cascade is the
    // single ordering invariant; the retired pre-cascade mode made that state
    // dependent on an environment variable.
    log_mem_stage("load_html: before_pre_script_cascade");
    apply_load_css_cascade(dom_doc, dom_root, external_stylesheet,
                           inline_stylesheets, inline_stylesheet_count,
                           css_engine, pool, "pre-script");
    log_mem_stage("load_html: pre_script_cascade_done");
    auto t_initial_cascade = timing ? high_resolution_clock::now() : t_inline_style;
    auto t_post_script = t_initial_cascade;

    if (execute_scripts) {
        // Step 2d: Execute <script> elements (inline + external) and body onload handlers
        // P17: scripts run after the initial cascade so load-time CSSOM reads see
        // resolved styles; if scripts mutate DOM/classes/stylesheets we recascade
        // below while preserving JS inline style writes.
        log_mem_stage("load_html: before_scripts");
        execute_document_scripts_profiled(html_root, dom_doc, pool, html_url, script_timing);
        log_mem_stage("load_html: after_scripts");
        auto t_script_exec = timing ? high_resolution_clock::now() : t_initial_cascade;

        if (dom_doc->root != dom_root) {
            // DOM scripts may replace documentElement; use the committed DOM
            // roots for the post-script cascade instead of resurrecting HTML.
            dom_root = dom_doc->root;
            html_root = dom_doc->html_root;
        }

        if (!dom_doc->pending_navigation_url) {
            char* refresh_url = find_meta_refresh_url(html_root);
            if (refresh_url && refresh_url[0]) {
                dom_doc->pending_navigation_url = refresh_url;
                log_info("meta_refresh_navigation: pending navigation to %s", refresh_url);
            } else if (refresh_url) {
                mem_free(refresh_url);
            }
        }

        if (dom_doc->js.mutation_count > 0) {
            log_info("execute_document_scripts: %d DOM mutations from JS, CSS cascade will re-resolve after scripts",
                     dom_doc->js.mutation_count);

            if (dom_js_mutation_requires_inline_stylesheet_rescan(dom_doc)) {
                // CSSOM edits mutate parsed CssStylesheet objects; only reparse when a <style> subtree changed.
                int rescan_inline_count = 0;
                int rescan_inline_capacity = 0;
                CssStylesheet** rescan_inline_sheets = nullptr;
                collect_inline_styles_from_dom(dom_root, css_engine, css_base_path, pool,
                                               &rescan_inline_sheets, &rescan_inline_count,
                                               &rescan_inline_capacity);

                int old_inline_only = inline_stylesheet_count - linked_stylesheet_count;
                if (rescan_inline_count != old_inline_only) {
                    log_info("[CSS] Re-scan found %d inline <style> stylesheets (was %d before JS)",
                             rescan_inline_count, old_inline_only);
                }

                int merged_count = linked_stylesheet_count + rescan_inline_count;
                CssStylesheet** merged_sheets = (CssStylesheet**)pool_alloc(pool, merged_count * sizeof(CssStylesheet*));

                for (int i = 0; i < linked_stylesheet_count; i++) {
                    merged_sheets[i] = inline_stylesheets[i];
                }
                for (int i = 0; i < rescan_inline_count; i++) {
                    merged_sheets[linked_stylesheet_count + i] = rescan_inline_sheets[i];
                }

                inline_stylesheets = merged_sheets;
                inline_stylesheet_count = merged_count;
            }

            store_document_stylesheets(dom_doc,
                                       external_stylesheet ? external_sources : nullptr,
                                       external_stylesheet ? 1 : 0,
                                       inline_stylesheets, inline_stylesheet_count, pool);

            clear_load_stylesheet_cascade_recursive(static_cast<DomNode*>(dom_root));
            apply_load_css_cascade(dom_doc, dom_root, external_stylesheet,
                                   inline_stylesheets, inline_stylesheet_count,
                                   css_engine, pool, "post-script");
            log_mem_stage("load_html: post_script_cascade_done");
        }

        // Step 2e: Install inline event handler attributes into EventTarget slots.
        // Must happen after execute_document_scripts so function definitions are available.
        collect_and_compile_event_handlers(dom_doc);
        t_post_script = timing ? high_resolution_clock::now() : t_script_exec;

        if (timing) {
            timing->script_exec_ms += duration<double, std::milli>(
                t_script_exec - t_initial_cascade).count();
            timing->post_script_ms += duration<double, std::milli>(
                t_post_script - t_script_exec).count();
        }
    }

    log_mem_stage("load_html: cascade_done");
    auto t_final_cascade = timing ? high_resolution_clock::now() : t_post_script;

    // Dump CSS computed values for testing/comparison (includes inheritance, before layout).
    // Skip the (potentially expensive) tree walk entirely when debug logs are disabled \u2014
    // for large pages (e.g. cnn_lite) this dump dominated peak RSS.
    if (log_level_enabled(NULL, LOG_LEVEL_DEBUG)) {
        StrBuf* str_buf = strbuf_new();
        dom_root->print(str_buf, 0);
        strbuf_free(str_buf);
    }

    // Step 8: Publish the document roots after scripts and cascade are complete.
    populate_layout_document(dom_doc, dom_root, html_root,
                             (HtmlVersion)detected_version, html_url, nullptr);
    for (int i = 0; i < superseded_html_url_count; i++) {
        if (superseded_html_urls[i] && superseded_html_urls[i] != html_url) {
            url_destroy(superseded_html_urls[i]);
        }
    }
    // Parsing can execute scripts that synchronously create geometry and state;
    // preserve their shared owners through the document's committed layout.

    // Set scale fields for HTML documents
    // HTML layout is in CSS logical pixels, scale is set later based on display context
    dom_doc->viewport.given_scale = 1.0f;
    dom_doc->viewport.scale = 1.0f;  // Will be updated by caller (window or render) with pixel_ratio

    // Step 9: Extract body transform scale from CSS (after cascade is complete)
    extract_body_transform_scale(dom_root, dom_doc);
    // If body has transform: scale(), apply it to the document's body_transform_scale
    // This can be used by the renderer to apply additional scaling

    auto t_end = high_resolution_clock::now();
    if (timing) {
        timing->loader_total_ms += duration<double, std::milli>(t_end - t_start).count();
        timing->read_ms += duration<double, std::milli>(t_read - t_start).count();
        timing->html_parse_ms += duration<double, std::milli>(t_parse - t_read).count();
        timing->dom_build_ms += duration<double, std::milli>(t_dom - t_parse).count();
        timing->css_parse_ms += duration<double, std::milli>(t_css_parse - t_dom).count();
        timing->stylesheet_setup_ms += duration<double, std::milli>(
            t_stylesheet_setup - t_css_parse).count();
        timing->inline_style_ms += duration<double, std::milli>(
            t_inline_style - t_stylesheet_setup).count();
        timing->initial_cascade_ms += duration<double, std::milli>(
            t_initial_cascade - t_inline_style).count();
        timing->final_cascade_ms += duration<double, std::milli>(
            t_final_cascade - t_post_script).count();
        timing->finalize_ms += duration<double, std::milli>(t_end - t_final_cascade).count();
    }
    log_info("[TIMING] load: total: %.1fms", duration<double, std::milli>(t_end - t_start).count());

    return dom_doc;
}

DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source = nullptr,
    bool track_source_lines = false, bool execute_scripts = true) {
    return load_lambda_html_doc_profiled(html_url, css_filename, viewport_width, viewport_height,
                                         pool, html_source, track_source_lines, execute_scripts,
                                         nullptr, nullptr, nullptr);
}

static DomDocument* load_lambda_html_doc_with_host_config(
    Url* html_url, const char* css_filename, int viewport_width, int viewport_height,
    Pool* pool, const DocumentJsHostConfig* js_host_config) {
    return load_lambda_html_doc_profiled(html_url, css_filename, viewport_width, viewport_height,
                                         pool, nullptr, false, true, nullptr, nullptr,
                                         js_host_config);
}

static char* escape_pdf_bridge_lambda_string(const char* value) {
    if (!value) return nullptr;
    size_t out_len = 0;
    for (const char* cursor = value; *cursor; cursor++) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') {
            out_len += 2;
        } else {
            out_len++;
        }
    }
    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_LAYOUT);
    if (!out) return nullptr;
    size_t pos = 0;
    for (const char* cursor = value; *cursor; cursor++) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == '\\') {
            out[pos++] = '\\'; out[pos++] = '\\';
        } else if (ch == '"') {
            out[pos++] = '\\'; out[pos++] = '"';
        } else if (ch == '\n') {
            out[pos++] = '\\'; out[pos++] = 'n';
        } else if (ch == '\r') {
            out[pos++] = '\\'; out[pos++] = 'r';
        } else if (ch == '\t') {
            out[pos++] = '\\'; out[pos++] = 't';
        } else {
            out[pos++] = (char)ch;
        }
    }
    out[pos] = '\0';
    return out;
}

static char* build_pdf_view_bridge_script(const char* pdf_file, const char* opts_expr) {
    char* escaped_pdf = escape_pdf_bridge_lambda_string(pdf_file);
    if (!escaped_pdf) {
        log_error("[load_html_doc] PDF package: failed to escape input path");
        return nullptr;
    }

    const char* opts = opts_expr ? opts_expr : "null";
    int needed = snprintf(nullptr, 0,
        "import pdf: lambda.package.pdf.pdf\n"
        "let doc = input(\"%s\", 'pdf') ^ { null }\n"
        "pdf.pdf_to_html(doc, %s)\n",
        escaped_pdf, opts);
    if (needed <= 0) {
        mem_free(escaped_pdf);
        log_error("[load_html_doc] PDF package: failed to size bridge script");
        return nullptr;
    }

    char* script_buf = (char*)mem_alloc((size_t)needed + 1, MEM_CAT_LAYOUT);
    if (!script_buf) {
        mem_free(escaped_pdf);
        log_error("[load_html_doc] PDF package: failed to allocate bridge script");
        return nullptr;
    }

    snprintf(script_buf, (size_t)needed + 1,
        "import pdf: lambda.package.pdf.pdf\n"
        "let doc = input(\"%s\", 'pdf') ^ { null }\n"
        "pdf.pdf_to_html(doc, %s)\n",
        escaped_pdf, opts);
    mem_free(escaped_pdf);
    return script_buf;
}

static DomDocument* load_pdf_bridge_doc(Url* pdf_url, int viewport_width,
                                        int viewport_height, Pool* pool) {
    if (!pdf_url) return nullptr;
    char* pdf_path = url_to_local_path(pdf_url);
    const char* pdf_source = pdf_path ? pdf_path : url_get_href(pdf_url);
    if (!pdf_source || !pdf_source[0]) {
        log_error("[load_html_doc] PDF package: failed to resolve input path");
        if (pdf_path) mem_free(pdf_path);
        return nullptr;
    }

    char* bridge_source = build_pdf_view_bridge_script(pdf_source, "{max_pages: 48}");
    if (!bridge_source) {
        if (pdf_path) mem_free(pdf_path);
        return nullptr;
    }

    DomDocument* doc = load_lambda_script_source_doc(pdf_url, bridge_source,
                                                     viewport_width, viewport_height, pool);
    mem_free(bridge_source);
    if (pdf_path) mem_free(pdf_path);
    return doc;
}

static DomDocument* load_graph_bridge_doc(Url* graph_url, int viewport_width,
                                          int viewport_height, Pool* pool) {
    if (!graph_url) return nullptr;
    char* graph_path = url_to_local_path(graph_url);
    const char* graph_source = graph_path ? graph_path : url_get_href(graph_url);
    if (!graph_source || !graph_source[0]) {
        log_error("[load_html_doc] GRAPH_BRIDGE_PATH: failed to resolve input path");
        if (graph_path) mem_free(graph_path);
        return nullptr;
    }

    char* bridge_source = build_graph_to_html_bridge_script(
        graph_source, nullptr, nullptr, "load_html_doc");
    if (!bridge_source) {
        if (graph_path) mem_free(graph_path);
        return nullptr;
    }

    DomDocument* doc = load_lambda_script_source_doc(graph_url, bridge_source,
                                                     viewport_width, viewport_height, pool);
    mem_free(bridge_source);
    if (graph_path) mem_free(graph_path);
    return doc;
}

typedef DomDocument* (*LayoutFormatLoader)(Url*, int, int, Pool*);

DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width,
                               int viewport_height, Pool* pool);
DomDocument* load_wiki_doc(Url* wiki_url, int viewport_width,
                           int viewport_height, Pool* pool);
static DomDocument* load_svg_layout_file(Url* url, int width, int height, Pool* pool);
static DomDocument* load_image_layout_file(Url* url, int width, int height, Pool* pool);

struct LayoutFormatRoute {
    const char* extension;
    LayoutFormatLoader loader;
};

static const LayoutFormatRoute layout_format_routes[] = {
    {".ls", load_lambda_script_doc},
    {".tex", load_latex_doc}, {".latex", load_latex_doc},
    {".md", load_markdown_doc}, {".markdown", load_markdown_doc},
    {".wiki", load_wiki_doc}, {".xml", load_xml_doc},
    {".svg", load_svg_layout_file}, {".png", load_image_layout_file},
    {".jpg", load_image_layout_file}, {".jpeg", load_image_layout_file},
    {".gif", load_image_layout_file},
    {".json", load_text_doc}, {".yaml", load_text_doc},
    {".yml", load_text_doc}, {".toml", load_text_doc},
    {".txt", load_text_doc}, {".csv", load_text_doc},
    {".ini", load_text_doc}, {".conf", load_text_doc}, {".cfg", load_text_doc},
    {".log", load_text_doc}
};

static const LayoutFormatRoute* layout_find_format_route(const char* extension) {
    if (!extension) return nullptr;
    for (size_t i = 0; i < sizeof(layout_format_routes) / sizeof(layout_format_routes[0]); i++) {
        if (strcmp(extension, layout_format_routes[i].extension) == 0) {
            return &layout_format_routes[i];
        }
    }
    return nullptr;
}

static bool layout_path_has_known_extension(const char* path) {
    if (!path) return false;
    if (graph_bridge_path_is_graph(path)) return true;
    const char* extension = strrchr(path, '.');
    return extension && (strcmp(extension, ".pdf") == 0 ||
                         layout_find_format_route(extension));
}

static DomDocument* load_svg_layout_file(Url* url, int width, int height, Pool* pool) {
    return load_svg_doc(url, width, height, pool, 1.0f);
}

static DomDocument* load_image_layout_file(Url* url, int width, int height, Pool* pool) {
    return load_image_doc(url, width, height, pool, 1.0f);
}

static DomDocument* load_layout_special_file(Url* url, const char* path,
                                              int width, int height, Pool* pool,
                                              bool bridge_pdf, bool include_text,
                                              bool* handled) {
    if (handled) *handled = false;
    if (!url || !path || !pool) return nullptr;

    if (graph_bridge_path_is_graph(path)) {
        if (handled) *handled = true;
        return load_graph_bridge_doc(url, width, height, pool);
    }

    const char* ext = strrchr(path, '.');
    if (!ext) return nullptr;
    if (strcmp(ext, ".pdf") == 0) {
        if (handled) *handled = true;
        return bridge_pdf ? load_pdf_bridge_doc(url, width, height, pool) : nullptr;
    }

    const LayoutFormatRoute* route = layout_find_format_route(ext);
    if (route) {
        bool text_route = route->loader == load_text_doc;
        if (text_route && !include_text) return nullptr;
        if (handled) *handled = true;
        log_info("[Layout] Detected %s file, using format loader", ext);
        return route->loader(url, width, height, pool);
    }
    return nullptr;
}

static DomDocument* load_html_doc_no_redirect(Url *base, char* doc_url, int viewport_width,
                                              int viewport_height,
                                              const DocumentJsHostConfig* js_host_config) {
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!pool) { log_error("Failed to create memory pool");  return NULL; }

    Url* full_url = parse_url(base, doc_url);
    if (!full_url) {
        log_error("Failed to parse URL: %s, with base: %p", doc_url, base);
        pool_destroy(pool);
        return NULL;
    }

    DomDocument* doc = nullptr;

    // For HTTP/HTTPS URLs, always route to HTML loader (it handles downloading)
    if (full_url->scheme == URL_SCHEME_HTTP || full_url->scheme == URL_SCHEME_HTTPS) {
        log_info("[load_html_doc] HTTP/HTTPS URL detected, using HTML pipeline: %s", doc_url);
        doc = load_lambda_html_doc_with_host_config(full_url, NULL, viewport_width,
                                                    viewport_height, pool, js_host_config);
    } else {
    bool handled = false;
    doc = load_layout_special_file(full_url, doc_url, viewport_width, viewport_height,
                                   pool, true, true, &handled);
    if (!handled) {
        doc = load_lambda_html_doc_with_host_config(full_url, NULL, viewport_width,
                                                    viewport_height, pool, js_host_config);
    }
    }

    if (!doc) {
        url_destroy(full_url);
        pool_destroy(pool);
    }

    return doc;
}

DomDocument* load_html_doc(Url *base, char* doc_url, int viewport_width, int viewport_height,
                           const DocumentJsHostConfig* js_host_config) {
    const int max_redirects = 8;
    Url* current_base = base;
    char* current_doc_url = doc_url;
    char* owned_doc_url = nullptr;

    for (int redirect_count = 0; redirect_count <= max_redirects; redirect_count++) {
        DomDocument* doc = load_html_doc_no_redirect(current_base, current_doc_url,
            viewport_width, viewport_height, js_host_config);
        if (!doc || !doc->pending_navigation_url || !doc->pending_navigation_url[0]) {
            if (owned_doc_url) mem_free(owned_doc_url);
            return doc;
        }

        Url* resolved = doc->url
            ? url_parse_with_base(doc->pending_navigation_url, doc->url)
            : url_parse(doc->pending_navigation_url);
        if (!resolved || !url_is_valid(resolved)) {
            log_error("load_html_doc: invalid pending navigation URL: %s", doc->pending_navigation_url);
            if (resolved) url_destroy(resolved);
            if (owned_doc_url) mem_free(owned_doc_url);
            return doc;
        }

        const char* href = url_get_href(resolved);
        char* next_doc_url = href ? mem_strdup(href, MEM_CAT_LAYOUT) : nullptr;
        url_destroy(resolved);
        if (!next_doc_url) {
            if (owned_doc_url) mem_free(owned_doc_url);
            return doc;
        }

        log_info("load_html_doc: following document navigation to %s", next_doc_url);
        free_document(doc);
        if (owned_doc_url) mem_free(owned_doc_url);
        owned_doc_url = next_doc_url;
        current_base = nullptr;
        current_doc_url = owned_doc_url;
    }

    log_error("load_html_doc: too many document redirects from %s", doc_url ? doc_url : "(null)");
    if (owned_doc_url) mem_free(owned_doc_url);
    return nullptr;
}

static char* escape_image_document_html_attr(const char* value) {
    if (!value) return mem_strdup("", MEM_CAT_LAYOUT);

    StrBuf* escaped = strbuf_new_cap(strlen(value) + 1);
    if (!escaped) return nullptr;
    escape_append(escaped, value, strlen(value), ESCAPE_RULES_HTML_ATTR,
                  ESCAPE_RULES_HTML_ATTR_COUNT, ESCAPE_CTRL_NONE);
    char* result = mem_strdup(escaped->str, MEM_CAT_LAYOUT);
    strbuf_free(escaped);
    return result;
}

static DomDocument* load_dom_backed_image_document(Url* image_url, int viewport_width,
                                                   int viewport_height, Pool* pool,
                                                   const char* log_prefix) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!image_url || !pool) {
        log_error("%s: invalid parameters", log_prefix ? log_prefix : "load_image_doc");
        return nullptr;
    }

    char* image_filepath = url_to_local_path(image_url);
    const char* image_href = url_get_href(image_url);
    const char* src_value = image_href && image_href[0] ? image_href : image_filepath;
    const char* filename = image_filepath ? strrchr(image_filepath, '/') : nullptr;
    filename = filename ? filename + 1 : (src_value ? src_value : "image");

    char* src_attr = escape_image_document_html_attr(src_value);
    char* title_attr = escape_image_document_html_attr(filename);
    if (!src_attr || !title_attr) {
        log_error("%s: failed to escape image URL", log_prefix ? log_prefix : "load_image_doc");
        if (src_attr) mem_free(src_attr);
        if (title_attr) mem_free(title_attr);
        if (image_filepath) mem_free(image_filepath);
        return nullptr;
    }

    const char* html_template =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>%s</title>\n"
        "  <style>\n"
        "    html, body { margin: 0; padding: 0; min-width: 100%%; min-height: 100%%; background: #fff; }\n"
        "    body { overflow: auto; }\n"
        "    img.rdt-image-document { display: block; max-width: none; height: auto; user-select: auto; }\n"
        "  </style>\n"
        "</head>\n"
        "<body data-rdt-document=\"image\">\n"
        "<img class=\"rdt-image-document\" src=\"%s\" alt=\"%s\">\n"
        "</body>\n"
        "</html>\n";

    size_t html_len = strlen(html_template) + strlen(title_attr) + strlen(src_attr) + strlen(title_attr) + 1;
    char* html_content = (char*)mem_alloc(html_len, MEM_CAT_LAYOUT);
    if (!html_content) {
        log_error("%s: failed to allocate wrapper HTML", log_prefix ? log_prefix : "load_image_doc");
        mem_free(src_attr);
        mem_free(title_attr);
        if (image_filepath) mem_free(image_filepath);
        return nullptr;
    }
    snprintf(html_content, html_len, html_template, title_attr, src_attr, title_attr);

    log_info("[TIMING] Loading DOM-backed image document: %s", src_value ? src_value : "(null)");
    DomDocument* doc = load_lambda_html_doc(image_url, nullptr, viewport_width, viewport_height,
                                            pool, html_content);

    mem_free(html_content);
    mem_free(src_attr);
    mem_free(title_attr);
    if (image_filepath) mem_free(image_filepath);

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] %s total: %.1fms",
             log_prefix ? log_prefix : "load_image_doc",
             std::chrono::duration<double, std::milli>(total_end - total_start).count());
    return doc;
}

// load SVG through the DOM-backed image path.
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    (void)pixel_ratio;
    return load_dom_backed_image_document(svg_url, viewport_width, viewport_height, pool, "load_svg_doc");
}

// load a raster image through the DOM-backed image path.
DomDocument* load_image_doc(Url* img_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    (void)pixel_ratio;
    return load_dom_backed_image_document(img_url, viewport_width, viewport_height, pool, "load_image_doc");
}

struct LayoutTempPathGuard {
    char* path;
    ~LayoutTempPathGuard() {
        if (path) mem_free(path);
    }
};

static Input* parse_layout_source_as(char* content, Url* source_url,
                                     const char* type_name, const char* log_prefix) {
    size_t type_len = strlen(type_name);
    String* type_str = (String*)mem_alloc(sizeof(String) + type_len + 1, MEM_CAT_LAYOUT);
    if (!type_str) {
        log_error("%s: failed to allocate type string", log_prefix);
        return nullptr;
    }
    type_str->len = type_len;
    str_copy(type_str->chars, type_str->len + 1, type_name, type_len);
    Input* input = input_from_source(content, source_url, type_str, nullptr);
    mem_free(type_str);
    return input;
}

static Element* input_first_element_root(Input* input) {
    if (!input) return nullptr;

    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_ELEMENT) {
        return input->root.element;
    }
    if (root_type != LMD_TYPE_ARRAY) {
        return nullptr;
    }

    List* root_list = input->root.array;
    for (int64_t i = 0; i < root_list->length; i++) {
        Item item = root_list->items[i];
        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
            return item.element;
        }
    }
    return nullptr;
}

static Input* read_layout_input_file(Url* url, const char* filepath,
                                     const char* type_name, const char* log_prefix,
                                     const char* file_label) {
    char* content = read_text_file(filepath);
    if (!content) {
        log_error("Failed to read %s file: %s", file_label, filepath);
        return nullptr;
    }

    Input* input = parse_layout_source_as(content, url, type_name, log_prefix);
    mem_free(content);
    if (!input) {
        log_error("Failed to parse %s file: %s", file_label, filepath);
    }
    return input;
}

static void populate_layout_document(DomDocument* doc, DomElement* root,
                                      Element* html_root, HtmlVersion version,
                                      Url* url, Runtime* runtime) {
    if (!doc) return;
    doc->root = root;
    doc->html_root = html_root;
    doc->html_version = version;
    doc->url = url;
    doc->view_tree = nullptr;
    doc->state = nullptr;
    doc->lambda_runtime = runtime;
}

static CssStylesheet* load_pool_backed_stylesheet(CssEngine* css_engine, Pool* pool,
                                                  const char* css_filename,
                                                  const char* log_prefix,
                                                  const char* label,
                                                  bool warn_missing) {
    char* css_content = read_text_file(css_filename);
    if (!css_content) {
        if (warn_missing) {
            log_warn("[%s] Failed to load %s file: %s", log_prefix, label, css_filename);
        }
        return nullptr;
    }

    size_t css_len = strlen(css_content);
    char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
    if (!css_pool_copy) {
        mem_free(css_content);
        return nullptr;
    }

    str_copy(css_pool_copy, css_len + 1, css_content, css_len);
    mem_free(css_content);
    CssStylesheet* stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
    if (!stylesheet && warn_missing) {
        log_warn("[%s] Failed to parse %s", log_prefix, label);
    }
    return stylesheet;
}

static CssStylesheet* load_home_stylesheet(CssEngine* css_engine, Pool* pool,
                                           const char* relative_path,
                                           const char* log_prefix,
                                           const char* label,
                                           bool warn_missing) {
    char* css_filename = lambda_home_path(relative_path);
    CssStylesheet* stylesheet = load_pool_backed_stylesheet(css_engine, pool, css_filename,
                                                            log_prefix, label, warn_missing);
    mem_free(css_filename);
    return stylesheet;
}

// load a text file as an escaped, monospace HTML source view.
DomDocument* load_text_doc(Url* text_url, int viewport_width, int viewport_height, Pool* pool) {
    if (!text_url || !pool) {
        log_error("load_text_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard text_path_guard = { url_to_local_path(text_url) };
    char* text_filepath = text_path_guard.path;
    if (!text_filepath) {
        log_error("load_text_doc: failed to resolve text file URL");
        return nullptr;
    }
    log_info("[TIMING] Loading text document: %s", text_filepath);

    auto step1_start = std::chrono::high_resolution_clock::now();
    char* text_content = read_text_file(text_filepath);
    if (!text_content) {
        log_error("Failed to read text file: %s", text_filepath);
        return nullptr;
    }
    size_t content_len = strlen(text_content);
    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Read text file: %.1fms (%zu bytes)",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count(), content_len);

    auto step2_start = std::chrono::high_resolution_clock::now();

    StrBuf* escaped_buf = strbuf_new_cap(content_len + 1);
    if (!escaped_buf) {
        log_error("Failed to allocate escaped content buffer");
        mem_free(text_content);  // from read_text_file, uses stdlib
        return nullptr;
    }
    escape_append(escaped_buf, text_content, content_len, ESCAPE_RULES_HTML_TEXT,
                  ESCAPE_RULES_HTML_TEXT_COUNT, ESCAPE_CTRL_NONE);
    char* escaped_content = mem_strdup(escaped_buf->str, MEM_CAT_LAYOUT);
    strbuf_free(escaped_buf);
    mem_free(text_content);  // from read_text_file, uses stdlib
    if (!escaped_content) {
        log_error("Failed to copy escaped content buffer");
        return nullptr;
    }

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Escape HTML: %.1fms",
        std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    auto step3_start = std::chrono::high_resolution_clock::now();

    const char* filename = strrchr(text_filepath, '/');
    filename = filename ? filename + 1 : text_filepath;

    const char* html_template =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>%s</title>\n"
        "  <style>\n"
        "    body {\n"
        "      margin: 0;\n"
        "      padding: 16px;\n"
        "      background: #1e1e1e;\n"
        "      color: #d4d4d4;\n"
        "      font-family: 'SF Mono', 'Menlo', 'Monaco', 'Consolas', monospace;\n"
        "      font-size: 13px;\n"
        "      line-height: 1.5;\n"
        "    }\n"
        "    pre {\n"
        "      margin: 0;\n"
        "      white-space: pre-wrap;\n"
        "      word-wrap: break-word;\n"
        "    }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "<pre>%s</pre>\n"
        "</body>\n"
        "</html>\n";

    size_t html_len = strlen(html_template) + strlen(filename) + strlen(escaped_content) + 1;
    char* html_content = (char*)mem_alloc(html_len, MEM_CAT_LAYOUT);
    if (!html_content) {
        log_error("Failed to allocate HTML buffer");
        mem_free(escaped_content);
        return nullptr;
    }
    snprintf(html_content, html_len, html_template, filename, escaped_content);
    mem_free(escaped_content);

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - Build HTML: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    DomDocument* document = load_lambda_html_doc(
        text_url, nullptr, viewport_width, viewport_height, pool,
        html_content, false, false);
    mem_free(html_content);
    return document;
}

// load Markdown and its bundled stylesheets.
static void release_layout_runtime(Runtime* runtime) {
    if (!runtime) return;
    runtime_cleanup(runtime);
    mem_free(runtime);
}

static DomDocument* create_layout_dom(Input* input, Element* root,
                                      const char* document_kind,
                                      DomPageKind page_kind,
                                      Runtime* owned_runtime,
                                      DomElement** out_root) {
    if (out_root) *out_root = nullptr;
    DomDocument* document = dom_document_create(input);
    if (!document) {
        log_error("[LAYOUT DOC INIT] failed to create %s document", document_kind);
        release_layout_runtime(owned_runtime);
        return nullptr;
    }
    // record provenance at construction; routing must never re-derive it from
    // which runtime pointer a document happens to hold.
    document->page_kind = page_kind;
    log_debug("[page-kind] %s document -> %s", document_kind,
              dom_page_kind_name(page_kind));
    // Inline declarations are parsed while building DomElement nodes, so the
    // property table must exist before the tree is materialized.
    if (!css_property_system_init(document->document_pool)) {
        log_error("[LAYOUT DOC INIT] failed to initialize CSS properties for %s", document_kind);
        dom_document_destroy(document);
        release_layout_runtime(owned_runtime);
        return nullptr;
    }
    DomElement* dom_root = build_dom_tree_from_element(root, document, nullptr);
    if (!dom_root) {
        log_error("[LAYOUT DOC INIT] failed to build %s DOM tree", document_kind);
        dom_document_destroy(document);
        // initialization owns the optional runtime until the DOM adopts it.
        release_layout_runtime(owned_runtime);
        return nullptr;
    }
    if (out_root) *out_root = dom_root;
    return document;
}

static DomDocument* create_layout_css_document(
        Input* input, Element* root, const char* document_kind,
        DomPageKind page_kind,
        Runtime* owned_runtime, int viewport_width, int viewport_height,
        Pool* pool, DomElement** out_root, CssEngine** out_engine) {
    if (out_engine) *out_engine = nullptr;
    DomDocument* document = create_layout_dom(
        input, root, document_kind, page_kind, owned_runtime, out_root);
    if (!document) return nullptr;

    CssEngine* engine = css_engine_create(pool);
    if (!engine) {
        log_error("[LAYOUT DOC INIT] failed to create %s CSS engine", document_kind);
        dom_document_destroy(document);
        release_layout_runtime(owned_runtime);
        return nullptr;
    }
    css_engine_set_viewport(engine, viewport_width, viewport_height);
    if (out_engine) *out_engine = engine;
    return document;
}

static DomDocument* load_home_styled_source_doc(
        Url* url, int viewport_width, int viewport_height, Pool* pool,
        const char* type_name, const char* log_prefix, const char* stylesheet_path,
        const char* stylesheet_label) {
    if (!url || !pool) return nullptr;
    LayoutTempPathGuard path_guard = {url_to_local_path(url)};
    if (!path_guard.path) {
        log_error("[%s] failed to resolve source URL", log_prefix);
        return nullptr;
    }
    Input* input = read_layout_input_file(
        url, path_guard.path, type_name, log_prefix, type_name);
    Element* source_root = input_first_element_root(input);
    if (!source_root) {
        log_error("[%s] failed to get source root", log_prefix);
        return nullptr;
    }

    DomElement* dom_root = nullptr;
    CssEngine* css_engine = nullptr;
    DomDocument* document = create_layout_css_document(
        input, source_root, type_name, DOM_PAGE_KIND_GENERATED, nullptr,
        viewport_width, viewport_height, pool, &dom_root, &css_engine);
    if (!document) return nullptr;

    CssStylesheet* stylesheet = load_home_stylesheet(
        css_engine, pool, stylesheet_path, log_prefix, stylesheet_label, true);
    CssStylesheet* stylesheets[1] = {stylesheet};
    layout_apply_css_stylesheets(document, dom_root, stylesheets, 1, pool, css_engine);
    populate_layout_document(document, dom_root, source_root, HTML5, url, nullptr);
    log_info("[%s] loaded source document", log_prefix);
    return document;
}

DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!markdown_url || !pool) {
        log_error("load_markdown_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard markdown_path_guard = { url_to_local_path(markdown_url) };
    char* markdown_filepath = markdown_path_guard.path;
    log_info("[TIMING] Loading markdown document: %s", markdown_filepath);

    auto step1_start = std::chrono::high_resolution_clock::now();
    Input* input = read_layout_input_file(markdown_url, markdown_filepath,
                                          "markdown", "load_markdown_doc", "markdown");
    if (!input) {
        return nullptr;
    }

    Element* markdown_root = input_first_element_root(input);

    if (!markdown_root) {
        log_error("Failed to get markdown root element");
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Parse markdown: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    Runtime* markdown_math_runtime = nullptr;

    {
        auto math_start = std::chrono::high_resolution_clock::now();

        struct MathInfo {
            Element* parent;
            int64_t index;
            const char* source;
            size_t source_len;
            bool is_display;
        };
        ArrayList* math_list = arraylist_new(16);

        struct WalkFrame { Element* elem; };
        ArrayList* stack = arraylist_new(64);
        arraylist_append(stack, (ArrayListValue)markdown_root);

        while (stack->length > 0) {
            Element* elem = (Element*)stack->data[stack->length - 1];
            stack->length--;

            for (int64_t i = 0; i < elem->length; i++) {
                Item child = elem->items[i];
                if (get_type_id(child) != LMD_TYPE_ELEMENT) continue;

                Element* child_elem = child.element;
                TypeElmt* child_type = (TypeElmt*)child_elem->type;
                if (!child_type) continue;

                const char* tag = child_type->name.str;
                if (tag && strcmp(tag, "math") == 0) {
                    ConstItem type_attr = child_elem->get_attr("type");
                    String* type_str_val = type_attr.string();
                    bool is_display = type_str_val && strcmp(type_str_val->chars, "block") == 0;

                    const char* math_src = nullptr;
                    size_t math_src_len = 0;
                    for (int64_t j = 0; j < child_elem->length; j++) {
                        if (get_type_id(child_elem->items[j]) == LMD_TYPE_STRING) {
                            String* s = child_elem->items[j].get_string();
                            if (s && s->len > 0) {
                                math_src = s->chars;
                                math_src_len = s->len;
                                break;
                            }
                        }
                    }

                    if (math_src && math_src_len > 0) {
                        MathInfo* mi = (MathInfo*)mem_alloc(sizeof(MathInfo), MEM_CAT_LAYOUT);
                        mi->parent = elem;
                        mi->index = i;
                        mi->source = math_src;
                        mi->source_len = math_src_len;
                        mi->is_display = is_display;
                        arraylist_append(math_list, (ArrayListValue)mi);
                    }
                } else {
                    arraylist_append(stack, (ArrayListValue)child_elem);
                }
            }
        }
        arraylist_free(stack);

        if (math_list->length > 0) {
            log_info("[Lambda Markdown] Found %d math elements, rendering via math package",
                     math_list->length);

            // Build a Lambda script that renders all math at once
            // Use parse() instead of input() to parse raw strings (not files)
            StrBuf* script = strbuf_new_cap(4096);
            strbuf_append_str(script, "import math: lambda.package.math.math\n[\n");

            for (int i = 0; i < math_list->length; i++) {
                MathInfo* mi = (MathInfo*)math_list->data[i];

                // Escape the LaTeX source for use in a Lambda string literal
                strbuf_append_str(script, "  ");
                if (mi->is_display) {
                    strbuf_append_str(script, "<div class: \"math-display-container\"; math.render_display(parse(\"");
                } else {
                    strbuf_append_str(script, "math.render_inline(parse(\"");
                }

                // Escape: \ -> \\, " -> \", newline -> \n, tab -> \t
                for (size_t k = 0; k < mi->source_len; k++) {
                    char c = mi->source[k];
                    if (c == '\\') strbuf_append_str(script, "\\\\");
                    else if (c == '"') strbuf_append_str(script, "\\\"");
                    else if (c == '\n') strbuf_append_str(script, "\\n");
                    else if (c == '\t') strbuf_append_str(script, "\\t");
                    else if (c == '\r') { /* skip */ }
                    else strbuf_append_char(script, c);
                }

                strbuf_append_str(script, "\", {type: \"math\"}))");
                if (mi->is_display) {
                    strbuf_append_str(script, ">");
                }
                if (i < math_list->length - 1) strbuf_append_str(script, ",");
                strbuf_append_str(script, "\n");
            }

            strbuf_append_str(script, "]\n");

            // Run the script in-memory (no temp file needed). Generated math
            // nodes are allocated directly in the markdown document arena and
            // the runtime is retained if any of those nodes are spliced in.
            Runtime* math_runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
            if (!math_runtime) {
                log_error("[Lambda Markdown] Failed to allocate math runtime");
                strbuf_free(script);
                for (int i = 0; i < math_list->length; i++) {
                    mem_free(math_list->data[i]);
                }
                arraylist_free(math_list);
                return nullptr;
            }
            runtime_init(math_runtime);
            math_runtime->current_dir = const_cast<char*>("./");
            math_runtime->import_base_dir = "./";
            math_runtime->ui_mode = true;
            math_runtime->result_arena = input->arena;

            Input* math_result = run_script_mir(math_runtime, script->str, (char*)"<math_render>", false);
            input_context = nullptr;

            if (math_result && get_type_id(math_result->root) == LMD_TYPE_ARRAY) {
                Array* rendered_arr = math_result->root.array;
                int replace_count = 0;
                for (int i = 0; i < math_list->length && i < (int)rendered_arr->length; i++) {
                    Item rendered_item = rendered_arr->items[i];
                    if (get_type_id(rendered_item) == LMD_TYPE_ELEMENT) {
                        MathInfo* mi = (MathInfo*)math_list->data[i];
                        mi->parent->items[mi->index] = rendered_item;
                        replace_count++;
                    }
                }
                if (replace_count > 0) {
                    markdown_math_runtime = math_runtime;
                    math_runtime = nullptr;
                }
                log_info("[Lambda Markdown] Replaced %d/%d math elements with rendered HTML",
                         replace_count, math_list->length);
            } else {
                log_error("[Lambda Markdown] Math rendering script failed or returned unexpected type");
            }

            release_layout_runtime(math_runtime);
            strbuf_free(script);
        }

        // Free math_list entries
        for (int i = 0; i < math_list->length; i++) {
            mem_free(math_list->data[i]);
        }
        arraylist_free(math_list);

        auto math_end = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 1.5 - Math rendering: %.1fms",
            std::chrono::duration<double, std::milli>(math_end - math_start).count());
    }

    // Step 2: Create DomDocument, CSS engine, and build the DOM tree.
    auto step2_start = std::chrono::high_resolution_clock::now();
    DomElement* dom_root = nullptr;
    CssEngine* css_engine = nullptr;
    DomDocument* dom_doc = create_layout_css_document(
        input, markdown_root, "markdown", DOM_PAGE_KIND_GENERATED,
        markdown_math_runtime,
        viewport_width, viewport_height, pool, &dom_root, &css_engine);
    if (!dom_doc) return nullptr;

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Build DOM tree: %.1fms",
        std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Load the document stylesheet.
    auto step3_start = std::chrono::high_resolution_clock::now();
    CssStylesheet* markdown_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/markdown.css", "Lambda Markdown", "markdown stylesheet", true);
    if (!markdown_stylesheet) {
        log_warn("Continuing without stylesheet - markdown will use browser defaults");
    }

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 4.5: Load math CSS and KaTeX font CSS for math rendering
    CssStylesheet* math_stylesheet = nullptr;
    CssStylesheet* katex_stylesheet = nullptr;
    {
        math_stylesheet = load_home_stylesheet(
            css_engine, pool, "input/math.css", "Lambda Markdown", "math stylesheet", false);
        katex_stylesheet = load_home_stylesheet(
            css_engine, pool, "input/latex/css/katex.css", "Lambda Markdown", "KaTeX font stylesheet", false);
    }

    // Step 5: Apply CSS cascade to DOM tree
    auto step4_start = std::chrono::high_resolution_clock::now();
    CssStylesheet* markdown_stylesheets[3] = {
        markdown_stylesheet, math_stylesheet, katex_stylesheet};
    layout_apply_css_stylesheets(dom_doc, dom_root, markdown_stylesheets, 3, pool, css_engine);
    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 5.5: Apply inline style="" attributes (highest priority, after stylesheet cascade)
    apply_inline_styles_to_tree(dom_root, pool);

    // Step 6: Populate DomDocument structure
    populate_layout_document(dom_doc, dom_root, markdown_root, HTML5,
                             markdown_url, markdown_math_runtime);

    store_document_stylesheets(dom_doc, markdown_stylesheets, 3, nullptr, 0, pool);

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_markdown_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    return dom_doc;
}

// load MediaWiki markup and its bundled stylesheet.
DomDocument* load_wiki_doc(Url* wiki_url, int viewport_width, int viewport_height, Pool* pool) {
    return load_home_styled_source_doc(
        wiki_url, viewport_width, viewport_height, pool,
        "wiki", "Lambda Wiki", "input/wiki.css", "wiki stylesheet");
}

// convert LaTeX through the Lambda package, then style the generated DOM.
DomDocument* load_latex_doc(Url* latex_url, int viewport_width, int viewport_height, Pool* pool) {
    if (!latex_url || !pool) {
        log_error("load_latex_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard latex_path_guard = { url_to_local_path(latex_url) };
    char* latex_filepath = latex_path_guard.path;
    if (!latex_filepath) {
        log_error("load_latex_doc: failed to resolve LaTeX file URL");
        return nullptr;
    }
    log_info("[Lambda LaTeX] Loading LaTeX document via Lambda package pipeline: %s", latex_filepath);

    // Step 1: Use the Lambda LaTeX package to convert LaTeX → HTML
    // Build a Lambda script that imports the LaTeX package and renders to HTML
    char safe_path[1024];
    snprintf(safe_path, sizeof(safe_path), "%s", latex_filepath);
    for (char* p = safe_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    char script_buf[4096];
    snprintf(script_buf, sizeof(script_buf),
        "import latex: lambda.package.latex.latex\n"
        "let ast = input(\"%s\", {type: \"latex\"}) ^ { null }\n"
        "latex.render(ast, {standalone: true})\n",
        safe_path);

    Pool* result_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!result_pool) {
        log_error("[Lambda LaTeX] Failed to create result pool");
        return nullptr;
    }

    Input* result_input = Input::create(result_pool, latex_url);
    if (!result_input) {
        log_error("[Lambda LaTeX] Failed to create result input");
        pool_destroy(result_pool);
        return nullptr;
    }
    result_input->ui_mode = true;

    Runtime* latex_runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
    if (!latex_runtime) {
        log_error("[Lambda LaTeX] Failed to allocate runtime");
        pool_destroy(result_pool);
        return nullptr;
    }
    runtime_init(latex_runtime);
    latex_runtime->current_dir = const_cast<char*>("./");
    latex_runtime->import_base_dir = "./";  // resolve imports from project root
    latex_runtime->ui_mode = true;
    latex_runtime->result_arena = result_input->arena;
    Input* script_result = run_script_mir(latex_runtime, script_buf, (char*)"<latex_render>", false);

    if (!script_result || get_type_id(script_result->root) == LMD_TYPE_NULL
        || get_type_id(script_result->root) == LMD_TYPE_ERROR) {
        log_error("[Lambda LaTeX] Lambda LaTeX package - HTML rendering failed for: %s", latex_filepath);
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    Element* html_root = nullptr;
    TypeId result_type = get_type_id(script_result->root);
    if (result_type == LMD_TYPE_ELEMENT) {
        result_input->root = script_result->root;
        html_root = script_result->root.element;
    } else {
        log_error("[Lambda LaTeX] Lambda package returned non-element type: %d", result_type);
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    input_context = nullptr;

    if (!html_root) {
        log_error("[Lambda LaTeX] Failed to get HTML root element from LaTeX conversion");
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }


    DomElement* dom_root = nullptr;
    CssEngine* css_engine = nullptr;
    DomDocument* dom_doc = create_layout_css_document(
        result_input, html_root, "LaTeX", DOM_PAGE_KIND_GENERATED, latex_runtime,
        viewport_width, viewport_height, pool, &dom_root, &css_engine);
    if (!dom_doc) {
        pool_destroy(result_pool);
        return nullptr;
    }

    CssStylesheet* latex_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/latex/css/article.css", "Lambda LaTeX", "LaTeX stylesheet", false);
    CssStylesheet* katex_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/latex/css/katex.css", "Lambda LaTeX", "KaTeX font stylesheet", false);

    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, latex_filepath, pool, &inline_stylesheet_count);

    CssStylesheet* latex_stylesheets[2] = {latex_stylesheet, katex_stylesheet};
    int latex_sheet_count = 0;
    CssStylesheet** all_latex_stylesheets = layout_merge_css_sources(
        pool, latex_stylesheets, 2, inline_stylesheets, inline_stylesheet_count,
        &latex_sheet_count);
    layout_apply_css_stylesheets(dom_doc, dom_root, all_latex_stylesheets,
                                 latex_sheet_count, pool, css_engine);

    apply_inline_styles_to_tree(dom_root, pool);


    store_document_stylesheets(dom_doc, latex_stylesheets, 2,
                               inline_stylesheets, inline_stylesheet_count, pool);

    populate_layout_document(dom_doc, dom_root, html_root, HTML5,
                             latex_url, latex_runtime);

    return dom_doc;
}

DomDocument* load_xml_doc(Url* xml_url, int viewport_width, int viewport_height, Pool* pool) {
    using namespace std::chrono;
    auto total_start = high_resolution_clock::now();

    if (!xml_url || !pool) {
        log_error("load_xml_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard xml_path_guard = { url_to_local_path(xml_url) };
    char* xml_filepath = xml_path_guard.path;
    if (!xml_filepath) {
        log_error("[Lambda XML] Failed to resolve XML file URL");
        return nullptr;
    }
    log_info("[Lambda XML] Loading XML file: %s", xml_filepath);

    auto t_read = high_resolution_clock::now();
    char* xml_content = read_text_file(xml_filepath);
    if (!xml_content) {
        log_error("[Lambda XML] Failed to read XML file: %s", xml_filepath);
        return nullptr;
    }
    auto t_parse = high_resolution_clock::now();
    log_info("[TIMING] load: read XML: %.1fms",
             duration_cast<duration<double, std::milli>>(t_parse - t_read).count());

    Input* xml_input = Input::create(pool, xml_url);
    if (!xml_input) {
        log_error("[Lambda XML] Failed to create Input for XML");
        mem_free(xml_content);
        return nullptr;
    }
    xml_input->ui_mode = true;
    parse_xml(xml_input, xml_content);
    mem_free(xml_content);  // from read_text_file, uses stdlib

    if (!xml_input->root.item || xml_input->root.item == ITEM_ERROR) {
        log_error("[Lambda XML] Failed to parse XML");
        return nullptr;
    }

    if (!xml_input->xml_stylesheet_href) {
        log_info("[Lambda XML] No <?xml-stylesheet?> directive found, showing as source text");
        return load_text_doc(xml_url, viewport_width, viewport_height, pool);
    }

    log_info("[Lambda XML] Found stylesheet: %s", xml_input->xml_stylesheet_href);
    auto t_css_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse XML: %.1fms",
             duration_cast<duration<double, std::milli>>(t_css_parse - t_parse).count());

    Element* document_wrapper = (Element*)xml_input->root.item;
    Element* xml_root = nullptr;

    if (document_wrapper && document_wrapper->type) {
        TypeElmt* doc_type = (TypeElmt*)document_wrapper->type;

        if (strcmp(doc_type->name.str, "document") == 0) {
            for (int64_t i = 0; i < document_wrapper->length; i++) {
                Item child = document_wrapper->items[i];
                TypeId child_type_id = get_type_id(child);

                if (child.item && child_type_id == LMD_TYPE_ELEMENT) {
                    Element* child_elem = (Element*)child.item;
                    TypeElmt* child_type = (TypeElmt*)child_elem->type;

                    if (child_type->name.str[0] != '?' && child_type->name.str[0] != '!') {
                        xml_root = child_elem;
                        break;
                    }
                }
            }
        }
    }

    if (!xml_root) {
        log_error("[Lambda XML] Could not find XML root element");
        return nullptr;
    }

    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("[Lambda XML] Failed to create CSS engine");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    CssStylesheet* external_stylesheet = load_pool_backed_stylesheet(
        css_engine, pool, xml_input->xml_stylesheet_href,
        "Lambda XML", "XML stylesheet", true);
    if (!external_stylesheet) return nullptr;

    auto t_dom = high_resolution_clock::now();
    log_info("[TIMING] load: parse CSS: %.1fms",
             duration_cast<duration<double, std::milli>>(t_dom - t_css_parse).count());

    DomDocument* dom_doc = dom_document_create(xml_input);
    if (!dom_doc) {
        log_error("[Lambda XML] Failed to create DOM document");
        return nullptr;
    }
    dom_doc->page_kind = DOM_PAGE_KIND_GENERATED;

    dom_doc->document_pool = pool;
    dom_doc->url = xml_url;

    DomElement* html_elem = DomElement::create(dom_doc, "html", nullptr);
    if (!html_elem) {
        log_error("[Lambda XML] Failed to create html wrapper element");
        return nullptr;
    }
    html_elem->tag_id = MARKUP_NAME_HTML;

    DomElement* body_elem = DomElement::create(dom_doc, "body", nullptr);
    if (!body_elem) {
        log_error("[Lambda XML] Failed to create body wrapper element");
        return nullptr;
    }
    body_elem->tag_id = MARKUP_NAME_BODY;

    DomElement* xml_dom = build_dom_tree_from_element(xml_root, dom_doc, body_elem);
    if (!xml_dom) {
        log_error("[Lambda XML] Failed to build DOM tree from XML");
        return nullptr;
    }

    html_elem->first_child = static_cast<DomNode*>(body_elem);
    html_elem->last_child = static_cast<DomNode*>(body_elem);
    body_elem->parent = static_cast<DomNode*>(html_elem);

    body_elem->first_child = static_cast<DomNode*>(xml_dom);
    body_elem->last_child = static_cast<DomNode*>(xml_dom);
    xml_dom->parent = static_cast<DomNode*>(body_elem);


    dom_doc->root = html_elem;
    dom_doc->html_root = xml_root;

    auto t_cascade = high_resolution_clock::now();
    log_info("[TIMING] load: build DOM: %.1fms",
             duration_cast<duration<double, std::milli>>(t_cascade - t_dom).count());

    CssStylesheet* xml_stylesheets[1] = {external_stylesheet};
    store_document_stylesheets(dom_doc, xml_stylesheets, 1, nullptr, 0, pool);

    layout_apply_css_stylesheets(dom_doc, html_elem, xml_stylesheets, 1, pool, css_engine);

    apply_inline_styles_to_tree(html_elem, pool);

    auto t_complete = high_resolution_clock::now();
    log_info("[TIMING] load: apply cascade: %.1fms",
             duration_cast<duration<double, std::milli>>(t_complete - t_cascade).count());
    log_info("[TIMING] load: total: %.1fms",
             duration_cast<duration<double, std::milli>>(t_complete - total_start).count());

    return dom_doc;
}

// load an in-memory HTML string with a document-owned pool.
static DomDocument* load_html_string_doc(const char* html_source, int viewport_width, int viewport_height) {
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!pool) { log_error("load_html_string_doc: pool_create failed"); return nullptr; }
    Url* base_url = get_current_dir();
    if (!base_url) { log_error("load_html_string_doc: get_current_dir failed"); pool_destroy(pool); return nullptr; }
    return load_lambda_html_doc(base_url, nullptr, viewport_width, viewport_height, pool, html_source);
}

// evaluate a Lambda document and run it through the CSS/layout pipeline.
DomDocument* load_lambda_script_source_doc(Url* script_url, const char* script_source,
                                           int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!script_url || !pool) {
        log_error("load_lambda_script_doc: invalid parameters");
        return nullptr;
    }
    if (context) {
        // Starting a second document Runtime on an occupied eval thread would
        // require the forbidden save/switch/restore lifetime pattern.
        log_error("load_lambda_script_doc: eval thread already owns a Runtime");
        return nullptr;
    }

    LayoutTempPathGuard script_path_guard = { url_to_local_path(script_url) };
    char* script_filepath = script_path_guard.path;
    if (!script_filepath) {
        log_error("load_lambda_script_doc: failed to resolve Lambda script URL");
        return nullptr;
    }
    log_info("[Lambda Script] Loading Lambda script: %s", script_filepath);

    // Step 1: Initialize Runtime and evaluate the Lambda script
    auto step1_start = std::chrono::high_resolution_clock::now();

    Runtime* runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
    runtime_init(runtime);
    EvalContext* layout_context = runtime_get_eval_context(runtime);
    if (!layout_context) {
        mem_free(runtime);
        return nullptr;
    }
    if (!eval_context_init(layout_context)) {
        log_error("load_lambda_script_doc: failed to initialize eval thread");
        release_layout_runtime(runtime);
        return nullptr;
    }

    Pool* result_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    Input* result_input = Input::create(result_pool, script_url);
    result_input->ui_mode = true;
    runtime->ui_mode = true;
    runtime->result_arena = result_input->arena;

    source_pos_bridge_reset();
    render_map_init();
    render_map_set_path_recorder(&render_map_record_path);

    Input* script_output = run_script_mir(runtime, script_source, script_filepath, false);

    if (runtime->heap) {
        layout_context->heap = runtime->heap;
        layout_context->name_pool = runtime->name_pool;
        layout_context->pool = runtime->heap->pool;
        if (runtime->ui_mode && runtime->result_arena) {
            layout_context->ui_mode = true;
            layout_context->arena = runtime->result_arena;
        }
        if (!eval_context_matches(layout_context)) {
            log_error("load_lambda_script_doc: eval owner changed during execution");
            release_layout_runtime(runtime);
            pool_destroy(result_pool);
            return nullptr;
        }
        input_context = (Context*)layout_context;
    }

    if (!script_output || !script_output->root.item) {
        log_error("[Lambda Script] Failed to evaluate script or script returned null");
        release_layout_runtime(runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Evaluate script: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    TypeId result_type = get_type_id(script_output->root);

    if (result_type == LMD_TYPE_ERROR) {
        log_error("[Lambda Script] Script evaluation returned an error");
        release_layout_runtime(runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    auto write_svg_wrapped_html = [&](const char* svg_content) -> DomDocument* {
        StrBuf* html_buf = strbuf_new_cap(strlen(svg_content) + 256);
        strbuf_append_format(html_buf,
            "<!DOCTYPE html><html><head><style>"
            "html,body{margin:0;padding:0;background:#fff;}"
            "svg{display:block;}"
            "</style></head><body>%s</body></html>",
            svg_content);
        release_layout_runtime(runtime);
        pool_destroy(result_pool);
        url_destroy(script_url);
        log_info("[Lambda Script] Loading SVG-in-HTML from string (%zu bytes)", html_buf->length);
        DomDocument* doc = load_html_string_doc(html_buf->str, viewport_width, viewport_height);
        strbuf_free(html_buf);
        return doc;
    };

    if (result_type == LMD_TYPE_ELEMENT) {
        Element* check_elem = script_output->root.element;
        TypeElmt* check_type = (TypeElmt*)check_elem->type;
        if (check_type && check_type->name.str && str_ieq_const(check_type->name.str, strlen(check_type->name.str), "svg")) {
            log_info("[Lambda Script] Script returned SVG element, wrapping in HTML for rendering");
            String* svg_str = format_xml(script_output->pool, script_output->root);
            if (svg_str && svg_str->len > 0) {
                return write_svg_wrapped_html(svg_str->chars);
            }
            log_error("[Lambda Script] Failed to format SVG element");
            release_layout_runtime(runtime);
            pool_destroy(result_pool);
            return nullptr;
        }
    } else if (result_type == LMD_TYPE_STRING) {
        String* result_str = script_output->root.get_string();
        if (result_str && result_str->len >= 4 &&
                strncmp(result_str->chars, "<svg", 4) == 0) {
            log_info("[Lambda Script] Script returned SVG string, wrapping in HTML for rendering");
            return write_svg_wrapped_html(result_str->chars);
        }
        if (result_str && result_str->len >= 5 &&
                (strncmp(result_str->chars, "<html", 5) == 0 ||
                 (result_str->len >= 9 && strncmp(result_str->chars, "<!DOCTYPE", 9) == 0))) {
            log_info("[Lambda Script] Script returned HTML string, loading in-memory (%zu bytes)", (size_t)result_str->len);
            DomDocument* doc = load_html_string_doc(result_str->chars, viewport_width, viewport_height);
            release_layout_runtime(runtime);
            pool_destroy(result_pool);
            url_destroy(script_url);
            return doc;
        }
    }

    bool is_html_document = false;
    Element* html_elem = nullptr;

    if (result_type == LMD_TYPE_ELEMENT) {
        Element* result_elem = script_output->root.element;
        TypeElmt* elem_type = (TypeElmt*)result_elem->type;

        if (elem_type && str_ieq_const(elem_type->name.str, strlen(elem_type->name.str), "html")) {
            is_html_document = true;
            html_elem = result_elem;
        }
    }

    if (!is_html_document) {
        Element* result_elem = nullptr;

        if (result_type == LMD_TYPE_ELEMENT) {
            result_elem = script_output->root.element;
        } else {
            StrBuf* result_str = strbuf_new();
            print_item(result_str, script_output->root, 0);

            MarkBuilder builder(result_input);
            ElementBuilder div = builder.element("div");
            div.text(result_str->str);
            Item div_item = div.final();
            result_elem = div_item.element;

            strbuf_free(result_str);
        }

        auto step2_start = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 2 - Wrap result: %.1fms",
            std::chrono::duration<double, std::milli>(step2_start - step1_end).count());

        MarkBuilder builder(result_input);

        Item result_item = {.element = result_elem};

        ElementBuilder body = builder.element("body");
        body.child(result_item);
        Item body_item = body.final();

        ElementBuilder html = builder.element("html");
        html.child(body_item);
        Item html_item = html.final();

        html_elem = html_item.element;

        auto step3_end = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 3 - Build HTML structure: %.1fms",
            std::chrono::duration<double, std::milli>(step3_end - step2_start).count());

        result_input->root = html_item;
    } else {
        result_input->root = {.element = html_elem};

        auto step2_start = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 2 - HTML document detected, skipping wrap: %.1fms",
            std::chrono::duration<double, std::milli>(step2_start - step1_end).count());
    }

    auto step5_start = std::chrono::high_resolution_clock::now();
    DomElement* dom_root = nullptr;
    CssEngine* css_engine = nullptr;
    DomDocument* dom_doc = create_layout_css_document(
        result_input, html_elem, "Lambda Script", DOM_PAGE_KIND_LAMBDA_SCRIPT,
        runtime,
        viewport_width, viewport_height, pool, &dom_root, &css_engine);
    if (!dom_doc) {
        pool_destroy(result_pool);
        return nullptr;
    }

    auto step5_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 5 - Build DOM tree: %.1fms",
        std::chrono::duration<double, std::milli>(step5_end - step5_start).count());

    auto step6_start = std::chrono::high_resolution_clock::now();
    CssStylesheet* script_stylesheet = nullptr;
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = nullptr;

    if (!is_html_document) {
        char* css_filename = lambda_home_path("input/script.css");
        script_stylesheet = load_pool_backed_stylesheet(
            css_engine, pool, css_filename, "Lambda Script", "script.css", true);
        mem_free(css_filename);
    } else {
        inline_stylesheets = extract_and_collect_css(
            html_elem, css_engine, script_filepath, pool, &inline_stylesheet_count);
    }

    auto step6_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 6 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step6_end - step6_start).count());

    auto step7_start = std::chrono::high_resolution_clock::now();
    CssStylesheet* script_stylesheets[1] = {script_stylesheet};
    int script_sheet_count = 0;
    CssStylesheet** all_script_stylesheets = layout_merge_css_sources(
        pool, script_stylesheet ? script_stylesheets : nullptr,
        script_stylesheet ? 1 : 0, inline_stylesheets, inline_stylesheet_count,
        &script_sheet_count);
    layout_apply_css_stylesheets(dom_doc, dom_root, all_script_stylesheets,
                                 script_sheet_count, pool, css_engine);

    auto step7_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 7 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step7_end - step7_start).count());

    populate_layout_document(dom_doc, dom_root, html_elem, HTML5, script_url, nullptr);

    store_document_stylesheets(dom_doc,
                               script_stylesheet ? script_stylesheets : nullptr,
                               script_stylesheet ? 1 : 0,
                               inline_stylesheets, inline_stylesheet_count, pool);
    if (dom_doc->stylesheet_count > 0) {
        dom_doc->cached_inline_sheets = inline_stylesheets;
        dom_doc->cached_inline_sheet_count = inline_stylesheet_count;
        dom_doc->services.cached_css_engine = css_engine;
    }

    Item html_item_root = {.element = html_elem};
    render_map_set_doc_root(html_item_root);

    input_context = nullptr;

    dom_doc->lambda_runtime = runtime;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_lambda_script_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    log_notice("[Lambda Script] Script document loaded and styled");
    return dom_doc;
}

DomDocument* load_lambda_script_doc(Url* script_url, int viewport_width, int viewport_height, Pool* pool) {
    return load_lambda_script_source_doc(script_url, nullptr, viewport_width, viewport_height, pool);
}

static View* find_matching_input(View* root, const char* match_tag, const char* match_class) {
    if (!root) return nullptr;
    if (root->is_element()) {
        DomElement* elem = lam::dom_require_element(root);
        if (elem->form_control() &&
            elem->form->control_type == FORM_CONTROL_TEXT) {
            bool tag_ok = (!match_tag || (elem->tag_name && strcmp(elem->tag_name, match_tag) == 0));
            bool class_ok = true;
            if (match_class) {
                class_ok = false;
                for (int i = 0; i < elem->class_count; i++) {
                    if (elem->class_names[i] && strcmp(elem->class_names[i], match_class) == 0) {
                        class_ok = true;
                        break;
                    }
                }
            }
            if (tag_ok && class_ok) return root;
        }
        DomNode* child = elem->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_ELEMENT) {
                View* found = find_matching_input(static_cast<View*>(child), match_tag, match_class);
                if (found) return found;
            }
            child = child->next_sibling;
        }
    }
    return nullptr;
}

struct LambdaFocusRestore {
    bool valid;
    RenderMapLookup lookup;
    int path[64];
    int path_len;
    const char* fallback_tag;
    const char* fallback_class;
};

static bool find_child_element_index(DomElement* parent, DomElement* child,
                                     int* out_index) {
    if (!parent || !child || !out_index) return false;
    int index = 0;
    DomNode* node = parent->first_child;
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            if (node == static_cast<DomNode*>(child)) {
                *out_index = index;
                return true;
            }
            index++;
        }
        node = node->next_sibling;
    }
    return false;
}

static bool build_focus_path_from_template_root(DomElement* root,
                                                DomElement* focused,
                                                LambdaFocusRestore* out) {
    if (!root || !focused || !out) return false;

    DomElement* chain[64];
    int depth = 0;
    DomNode* node = static_cast<DomNode*>(focused);
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            if (depth >= 64) return false;
            chain[depth++] = lam::dom_require_element(node);
            if (node == static_cast<DomNode*>(root)) break;
        }
        node = node->parent;
    }
    if (depth == 0 || chain[depth - 1] != root) return false;

    out->path_len = 0;
    for (int i = depth - 1; i > 0; i--) {
        int child_index = 0;
        if (!find_child_element_index(chain[i], chain[i - 1], &child_index)) {
            return false;
        }
        out->path[out->path_len++] = child_index;
    }
    return true;
}

static bool capture_lambda_focus_restore(DocState* state,
                                         LambdaFocusRestore* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!state || !focus_has_current(state)) return false;

    View* focused = focus_get(state);
    if (!focused || !focused->is_element()) return false;
    DomElement* focused_elem = lam::dom_require_element(focused);
    if (focused_elem->role_kind() != DomElement::ROLE_FORM ||
        !focused_elem->form ||
        focused_elem->form->control_type != FORM_CONTROL_TEXT) {
        return true;
    }
    out->fallback_tag = focused_elem->tag_name;
    if (focused_elem->class_count > 0 && focused_elem->class_names) {
        out->fallback_class = focused_elem->class_names[0];
    }

    DomNode* node = static_cast<DomNode*>(focused);
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* elem = lam::dom_require_element(node);
            if (!elem->is_synthetic()) {
                Item item = {.element = dom_element_render_source(elem)};
                RenderMapLookup lookup;
                if (render_map_reverse_lookup(item, &lookup)) {
                    out->lookup = lookup;
                    out->valid = build_focus_path_from_template_root(
                        elem, focused_elem, out);
                    return true;
                }
            }
        }
        node = node->parent;
    }
    return true;
}

static void set_layout_dirty_subtree(DomNode* root, bool dirty) {
    if (!root) return;
    DomNode* stack[256];
    int top = 0;
    stack[top++] = root;
    while (top > 0) {
        DomNode* node = stack[--top];
        node->layout_dirty = dirty;
        if (!node->is_element()) continue;
        for (DomNode* child = lam::dom_require_element(node)->first_child;
             child && top < 255; child = child->next_sibling) {
            stack[top++] = child;
        }
    }
}

static View* resolve_lambda_focus_restore(DomDocument* doc,
                                          const LambdaFocusRestore* restore) {
    if (!doc || !restore || !restore->valid || !doc->element_dom_map) {
        return nullptr;
    }

    Item result = render_map_get_result(restore->lookup.source_item,
                                        restore->lookup.template_ref);
    if (get_type_id(result) != LMD_TYPE_ELEMENT) return nullptr;

    DomElement* elem = element_dom_map_lookup(doc->element_dom_map,
                                              result.element);
    for (int i = 0; elem && i < restore->path_len; i++) {
        int wanted = restore->path[i];
        int index = 0;
        DomElement* found = nullptr;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_ELEMENT) {
                if (index == wanted) {
                    found = lam::dom_require_element(child);
                    break;
                }
                index++;
            }
            child = child->next_sibling;
        }
        elem = found;
    }

    return elem ? static_cast<View*>(elem) : nullptr;
}

static View* restore_lambda_focus(DomDocument* doc, DocState* state, bool had_focus,
                                  const LambdaFocusRestore* restore) {
    if (!had_focus || !state || !doc || !doc->view_tree || !doc->view_tree->root) return nullptr;
    View* focused = resolve_lambda_focus_restore(doc, restore);
    if (focused && (!doc->root || !view_tree_contains_view(
                        static_cast<DomNode*>(doc->root), focused))) {
        // A retransform may retain a render-map entry for the retired DOM
        // wrapper; never restore focus to a node detached from the live tree.
        focused = nullptr;
    }
    if (focused && restore->fallback_tag) {
        DomElement* elem = focused->is_element()
            ? lam::dom_require_element(focused) : nullptr;
        bool is_matching_text_control = elem && elem->form_control() && elem->form &&
            elem->form->control_type == FORM_CONTROL_TEXT && elem->tag_name &&
            strcmp(elem->tag_name, restore->fallback_tag) == 0 &&
            (!restore->fallback_class || elem->has_class(restore->fallback_class));
        // The render-map path can resolve to the template root instead of the
        // focused descendant; only a matching control may retain text focus.
        if (!is_matching_text_control) focused = nullptr;
    }
    if (!focused && restore->fallback_tag) {
        focused = find_matching_input(
            doc->view_tree->root, restore->fallback_tag, restore->fallback_class);
    }
    if (focused) {
        focus_set(state, focused, false);
    } else if (focus_has_current(state)) {
        focus_clear(state);
    }
    return focused;
}

static Element* layout_current_html_root(DomDocument* doc) {
    if (!doc) return nullptr;
    Element* html_root = doc->html_root;
    Item current_root = render_map_get_doc_root();
    if (current_root.item && current_root.element != html_root) {
        html_root = current_root.element;
        doc->html_root = html_root;
    }
    return html_root;
}

void rebuild_lambda_doc(UiContext* uicon) {
    if (!uicon || !uicon->document) {
        log_error("rebuild_lambda_doc: no document");
        return;
    }

    DomDocument* doc = uicon->document;
    Element* html_elem = layout_current_html_root(doc);

    if (!html_elem) {
        log_error("rebuild_lambda_doc: no html_root in document");
        return;
    }


    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    DocState* state = (DocState*)doc->state;
    LambdaFocusRestore focus_restore;
    bool had_focus = capture_lambda_focus_restore(state, &focus_restore);

    css_property_system_init(doc->document_pool);

    if (!doc->element_dom_map) {
        doc->element_dom_map = element_dom_map_create();
    } else {
        hashmap_clear(doc->element_dom_map, false);
    }

    DomElement* new_root = build_dom_tree_from_element(html_elem, doc, nullptr);
    if (!new_root) {
        log_error("rebuild_lambda_doc: failed to rebuild DOM tree");
        return;
    }
    auto t_dom = high_resolution_clock::now();

    doc->root = new_root;

    CssStylesheet** inline_sheets = doc->cached_inline_sheets;
    int inline_count = doc->cached_inline_sheet_count;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;

    if (!inline_sheets) {
        css_engine = css_engine_create(doc->document_pool);
        if (css_engine) {
            inline_sheets = extract_and_collect_css(
                html_elem, css_engine, nullptr, doc->document_pool, &inline_count);
            doc->cached_inline_sheets = inline_sheets;
            doc->cached_inline_sheet_count = inline_count;
            doc->services.cached_css_engine = css_engine;
        }
    }

    layout_apply_css_stylesheets(
        doc, new_root, inline_sheets, inline_count, doc->document_pool, css_engine);

    apply_inline_styles_to_tree(new_root, doc->document_pool);
    auto t_css = high_resolution_clock::now();

    layout_html_doc(uicon, doc, false);
    auto t_layout = high_resolution_clock::now();

    restore_lambda_focus(doc, state, had_focus, &focus_restore);

    if (state && !focus_has_current(state) &&
        doc->view_tree && doc->view_tree->root) {
        View* af = find_matching_input(doc->view_tree->root, "input", nullptr);
        if (af && af->is_element()) {
            DomElement* af_elem = lam::dom_require_element(af);
            if (af_elem->has_attribute("autofocus")) {
                focus_set(state, af, false);
                state_store_caret_collapse_to_view_offset(state, af, 0);
            }
        }
    }

    if (state) {
        state->dirty_tracker.full_repaint = true;
        doc_state_mark_dirty(state);
        doc_state_clear_reflow(state);  // layout already done by rebuild
        reflow_clear(state);          // discard stale pending reflow requests
    }
    auto t_end = high_resolution_clock::now();

    log_info("[TIMING] rebuild: dom_build=%.2fms css_cascade=%.2fms layout=%.2fms total=%.2fms",
        duration<double, std::milli>(t_dom - t_start).count(),
        duration<double, std::milli>(t_css - t_dom).count(),
        duration<double, std::milli>(t_layout - t_css).count(),
        duration<double, std::milli>(t_end - t_start).count());
}

static void compute_absolute_bounds(DomNode* node, float* abs_x, float* abs_y, float* w, float* h) {
    *abs_x = node->x;
    *abs_y = node->y;
    *w = node->width;
    *h = node->height;
    DomNode* p = node->parent;
    while (p) {
        *abs_x += p->x;
        *abs_y += p->y;
        p = p->parent;
    }
}

void rebuild_lambda_doc_incremental(UiContext* uicon, RetransformResult* results, int result_count) {
    if (!uicon || !uicon->document) {
        log_error("rebuild_lambda_doc_incremental: no document");
        return;
    }

    DomDocument* doc = uicon->document;
    Element* html_elem = layout_current_html_root(doc);

    if (!html_elem) {
        log_error("rebuild_lambda_doc_incremental: no html_root in document");
        return;
    }

    bool can_incremental = (doc->element_dom_map != nullptr) &&
                           (doc->root != nullptr) &&
                           (result_count > 0);

    if (can_incremental) {
        for (int i = 0; i < result_count; i++) {
            if (get_type_id(results[i].old_result) != LMD_TYPE_ELEMENT) {
                can_incremental = false;
                break;
            }
            Element* old_elem = results[i].old_result.element;
            DomElement* old_dom = old_elem ? element_dom_map_lookup(doc->element_dom_map, old_elem) : nullptr;
            if (!old_dom || old_dom->node_type != DOM_NODE_ELEMENT || !old_dom->parent) {
                can_incremental = false;
                break;
            }
            DomElement* parent_dom = lam::dom_require_element(old_dom->parent);
            if (parent_dom->node_type != DOM_NODE_ELEMENT || !parent_dom->tag_name) {
                can_incremental = false;
                break;
            }
            if (get_type_id(results[i].new_result) != LMD_TYPE_ELEMENT) {
                can_incremental = false;
                break;
            }
        }
    }

    if (!can_incremental) {
        rebuild_lambda_doc(uicon);
        return;
    }

    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    DocState* state = (DocState*)doc->state;
    LambdaFocusRestore focus_restore;
    bool had_focus = capture_lambda_focus_restore(state, &focus_restore);

    css_property_system_init(doc->document_pool);

    CssStylesheet** inline_sheets = doc->cached_inline_sheets;
    int inline_count = doc->cached_inline_sheet_count;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;

    struct { float x, y, w, h; } old_bounds[16] = {};
    DomElement* new_doms[16] = {};
    for (int i = 0; i < result_count && i < 16; i++) {
        Element* old_elem = results[i].old_result.element;
        DomElement* old_dom = element_dom_map_lookup(doc->element_dom_map, old_elem);
        if (old_dom) {
            compute_absolute_bounds(static_cast<DomNode*>(old_dom),
                &old_bounds[i].x, &old_bounds[i].y, &old_bounds[i].w, &old_bounds[i].h);
        }
    }

    SelectorMatcher* incremental_matcher = nullptr;
    if (css_engine && inline_sheets && inline_count > 0) {
        incremental_matcher = selector_matcher_create(doc->document_pool);
        state_configure_selector_matcher((DocState*)doc->state, incremental_matcher);
    }

    for (int i = 0; i < result_count; i++) {
        Element* old_elem = results[i].old_result.element;
        Element* new_elem = results[i].new_result.element;

        DomElement* old_dom = element_dom_map_lookup(doc->element_dom_map, old_elem);
        if (!old_dom || !old_dom->parent) {
            continue;
        }
        DomElement* parent_dom = lam::dom_require_element(old_dom->parent);
        DomNode* old_previous = old_dom->prev_sibling;
        DomNode* old_next = old_dom->next_sibling;

        DomElement* new_dom = build_dom_tree_from_element(new_elem, doc, nullptr);

        if (!new_dom) {
            continue;
        }

        if (old_dom->is_popover_open() && new_dom->has_attribute("popover")) {
            // Reconciliation replaces the DOM wrapper, but popover openness is
            // live state and must survive an unrelated class/style mutation.
            new_dom->set_popover_open(true);
        }

        if (new_dom == old_dom) {
            new_dom->parent = parent_dom;
            new_dom->prev_sibling = old_previous;
            new_dom->next_sibling = old_next;
            if (old_previous) {
                old_previous->next_sibling = static_cast<DomNode*>(new_dom);
            } else {
                parent_dom->first_child = static_cast<DomNode*>(new_dom);
            }
            if (old_next) {
                old_next->prev_sibling = static_cast<DomNode*>(new_dom);
            } else {
                parent_dom->last_child = static_cast<DomNode*>(new_dom);
            }
        } else if (!dom_node_replace_in_parent(parent_dom, static_cast<DomNode*>(old_dom),
                                                static_cast<DomNode*>(new_dom))) {
            log_error("rebuild_lambda_doc_incremental: failed to replace entry %d", i);
            continue;
        }
        if (i < 16) new_doms[i] = new_dom;

        set_layout_dirty_subtree(static_cast<DomNode*>(new_dom), true);

        DomNode* ancestor = static_cast<DomNode*>(parent_dom);
        while (ancestor) {
            if (ancestor->is_element()) {
                (lam::dom_require_element(ancestor))->set_styles_resolved(false);
            }
            ancestor->layout_dirty = true;
            ancestor = ancestor->parent;
        }

        if (incremental_matcher) {
            radiant_apply_css_stylesheets_to_tree(
                doc, new_dom, inline_sheets, inline_count, doc->document_pool,
                css_engine, incremental_matcher);
        }

        apply_inline_styles_to_tree(new_dom, doc->document_pool);
    }
    auto t_dom_css = high_resolution_clock::now();

    if (doc->view_tree) {
        doc->incremental_layout = true;
        doc->skip_style_reset = true;
        layout_html_doc(uicon, doc, true);
        doc->skip_style_reset = false;
        doc->incremental_layout = false;
        if (doc->root) set_layout_dirty_subtree(static_cast<DomNode*>(doc->root), false);
    } else {
        layout_html_doc(uicon, doc, false);
    }
    auto t_layout = high_resolution_clock::now();

    if (state) {
        dirty_clear(&state->dirty_tracker);

        state->dirty_tracker.full_repaint = true;
    }

    restore_lambda_focus(doc, state, had_focus, &focus_restore);

    if (state && !focus_has_current(state)) {
        for (int i = 0; i < result_count; i++) {
            if (new_doms[i]) {
                View* af = find_matching_input(static_cast<View*>(new_doms[i]), "input", nullptr);
                if (af && af->is_element()) {
                    DomElement* af_elem = lam::dom_require_element(af);
                    if (af_elem->has_attribute("autofocus")) {
                        focus_set(state, af, false);
                        state_store_caret_collapse_to_view_offset(state, af, 0);
                        break;
                    }
                }
            }
        }
    }

    if (state) {
        doc_state_mark_dirty(state);
        doc_state_clear_reflow(state);  // layout already done by rebuild
        reflow_clear(state);          // discard stale pending reflow requests
    }
    bool has_selective = state && !state->dirty_tracker.full_repaint
                         && dirty_has_regions(&state->dirty_tracker);
    auto t_end = high_resolution_clock::now();

    log_info("[TIMING] rebuild_incr: dom_patch=%.2fms layout=%.2fms total=%.2fms (subtrees=%d, selective=%s)",
        duration<double, std::milli>(t_dom_css - t_start).count(),
        duration<double, std::milli>(t_layout - t_dom_css).count(),
        duration<double, std::milli>(t_end - t_start).count(),
        result_count,
        has_selective ? "yes" : "no");
}

#define MAX_INPUT_FILES 4096

struct LayoutOptions {
    const char* input_files[MAX_INPUT_FILES];  // array of input file paths
    int input_file_count;                       // number of input files
    const char* output_file;
    const char* output_dir;                     // output directory for batch mode
    const char* css_file;
    const char* view_output_file;  // Custom output path for view_tree.json (single file mode)
    const char* font_dirs[16];                  // additional font scan directories
    int font_dir_count;                         // number of font directories
    int viewport_width;
    int viewport_height;
    bool debug;
    bool event_log;
    bool state_dump;
    bool continue_on_error;                     // continue processing on errors in batch mode
    bool summary;                               // print summary statistics
    const char* timing_output_file;              // optional JSONL phase timing output
    const char* memory_profile_output_file;      // post-layout six-domain snapshot
    bool auto_close;                            // cancel async JS timers after load/onload
    bool disable_animations;                    // freeze CSS animation/transition effects for snapshots
    bool stream_layout_results;                 // write compact framed results to stdout
};

static bool layout_read_option_value(int argc, char** argv, int* index,
                                     const char* option, const char** value) {
    if (!index || !value || *index + 1 >= argc) {
        log_error("Error: %s requires an argument", option);
        return false;
    }
    *value = argv[++*index];
    return true;
}

enum LayoutOptionKind {
    LAYOUT_OPTION_STRING,
    LAYOUT_OPTION_INTEGER,
    LAYOUT_OPTION_FLAG
};

struct LayoutOption {
    const char* short_name;
    const char* long_name;
    const char* error_name;
    void* target;
    LayoutOptionKind kind;
};

bool parse_layout_args(int argc, char** argv, LayoutOptions* opts) {
    if (!opts) return false;
    *opts = {};
    opts->viewport_width = 1200;
    opts->viewport_height = 800;

    LayoutOption options[] = {
        {"-o", "--output", "-o", &opts->output_file, LAYOUT_OPTION_STRING},
        {nullptr, "--output-dir", "--output-dir", &opts->output_dir, LAYOUT_OPTION_STRING},
        {nullptr, "--view-output", "--view-output", &opts->view_output_file, LAYOUT_OPTION_STRING},
        {"-c", "--css", "-c", &opts->css_file, LAYOUT_OPTION_STRING},
        {nullptr, "--timing-output", "--timing-output", &opts->timing_output_file, LAYOUT_OPTION_STRING},
        {nullptr, "--view-memory-profile", "--view-memory-profile",
         &opts->memory_profile_output_file, LAYOUT_OPTION_STRING},
        {"-vw", "--viewport-width", "-vw/--viewport-width", &opts->viewport_width, LAYOUT_OPTION_INTEGER},
        {"-vh", "--viewport-height", "-vh/--viewport-height", &opts->viewport_height, LAYOUT_OPTION_INTEGER},
        {nullptr, "--debug", nullptr, &opts->debug, LAYOUT_OPTION_FLAG},
        {nullptr, "--event-log", nullptr, &opts->event_log, LAYOUT_OPTION_FLAG},
        {nullptr, "--state-dump", nullptr, &opts->state_dump, LAYOUT_OPTION_FLAG},
        {nullptr, "--continue-on-error", nullptr, &opts->continue_on_error, LAYOUT_OPTION_FLAG},
        {nullptr, "--summary", nullptr, &opts->summary, LAYOUT_OPTION_FLAG},
        {nullptr, "--auto-close", nullptr, &opts->auto_close, LAYOUT_OPTION_FLAG},
        {nullptr, "--disable-animations", nullptr, &opts->disable_animations, LAYOUT_OPTION_FLAG},
        {nullptr, "--stream-layout-results", nullptr,
         &opts->stream_layout_results, LAYOUT_OPTION_FLAG}
    };

    for (int i = 0; i < argc; i++) {
        bool matched = false;
        for (size_t option = 0; option < sizeof(options) / sizeof(options[0]); option++) {
            LayoutOption* spec = &options[option];
            if ((spec->short_name && strcmp(argv[i], spec->short_name) == 0) ||
                (spec->long_name && strcmp(argv[i], spec->long_name) == 0)) {
                if (spec->kind == LAYOUT_OPTION_FLAG) {
                    *(bool*)spec->target = true;
                } else {
                    const char* value = nullptr;
                    if (!layout_read_option_value(argc, argv, &i, spec->error_name, &value)) return false;
                    if (spec->kind == LAYOUT_OPTION_STRING) {
                        *(const char**)spec->target = value;
                    } else {
                        *(int*)spec->target = (int)str_to_int64_default(
                            value, strlen(value), 0); // INT_CAST_OK: CLI viewport option
                    }
                }
                matched = true;
                break;
            }
        }
        if (matched) continue;

        if (strcmp(argv[i], "--font-dir") == 0) {
            const char* value = nullptr;
            if (!layout_read_option_value(argc, argv, &i, "--font-dir", &value)) return false;
            if (opts->font_dir_count >= 16) {
                log_error("Error: too many --font-dir options (max 16)");
                return false;
            }
            opts->font_dirs[opts->font_dir_count++] = value;
            continue;
        }

        if (argv[i][0] != '-') {
            if (opts->input_file_count >= MAX_INPUT_FILES) {
                log_error("Error: too many input files (max %d)", MAX_INPUT_FILES);
                return false;
            }
            opts->input_files[opts->input_file_count++] = argv[i];
        }
    }

    if (opts->input_file_count == 0) {
        log_error("Error: at least one input file required");
        log_error("Usage: lambda layout <input.html> [input2.html ...] [options]");
        return false;
    }

    if (opts->input_file_count > 1 && !opts->output_dir && !opts->stream_layout_results) {
        log_error("Error: batch mode requires --output-dir or --stream-layout-results");
        return false;
    }
    if (opts->memory_profile_output_file && opts->input_file_count != 1) {
        // One fresh process per page is part of the R7 comparability contract.
        log_error("Error: --view-memory-profile requires exactly one input file");
        return false;
    }

    return true;
}

struct LayoutPhaseTiming {
    double total_ms;
    double load_ms;
    double document_parse_ms;
    double load_setup_ms;
    double load_read_ms;
    double load_html_parse_ms;
    double load_dom_build_ms;
    double load_css_parse_ms;
    double load_stylesheet_setup_ms;
    double load_inline_style_ms;
    double load_initial_cascade_ms;
    double load_script_exec_ms;
    double load_post_script_ms;
    double load_final_cascade_ms;
    double load_finalize_ms;
    double script_collect_ms;
    double script_runtime_setup_ms;
    double script_postdom_total_ms;
    double script_preamble_wall_ms;
    double script_scheduler_ms;
    double script_interactive_ms;
    double script_user_scripts_ms;
    double script_dom_content_loaded_ms;
    double script_async_scripts_ms;
    double script_load_blockers_ms;
    double script_complete_ms;
    double script_body_onload_ms;
    double script_window_load_ms;
    double script_event_loop_ms;
    double script_runtime_cleanup_ms;
    double script_source_cleanup_ms;
    uint64_t script_cache_lookups;
    uint64_t script_cache_hits;
    uint64_t script_cache_misses;
    uint64_t script_cache_compiles;
    uint64_t script_cache_instantiations;
    double js_parse_ms;
    double js_ast_ms;
    double js_transpile_ms;
    double js_link_ms;
    double js_exec_ms;
    double js_cleanup_ms;
    double js_total_ms;
    double js_preamble_ms;
    double layout_ms;
    double output_ms;
};

static double script_phase_us_to_ms(uint64_t us) {
    return (double)us / 1000.0;
}

static void set_detailed_script_timing(LayoutPhaseTiming* timing,
                                       const DocumentScriptPhaseTiming* script_timing) {
    if (!timing || !script_timing) return;
    timing->script_collect_ms = script_phase_us_to_ms(script_timing->collect_us);
    timing->script_runtime_setup_ms = script_phase_us_to_ms(script_timing->runtime_setup_us);
    timing->script_postdom_total_ms = script_phase_us_to_ms(script_timing->postdom_total_us);
    timing->script_preamble_wall_ms = script_phase_us_to_ms(script_timing->preamble_us);
    timing->script_scheduler_ms = script_phase_us_to_ms(script_timing->scheduler_us);
    timing->script_interactive_ms = script_phase_us_to_ms(script_timing->interactive_us);
    timing->script_user_scripts_ms = script_phase_us_to_ms(script_timing->user_scripts_us);
    timing->script_dom_content_loaded_ms = script_phase_us_to_ms(script_timing->dom_content_loaded_us);
    timing->script_async_scripts_ms = script_phase_us_to_ms(script_timing->async_scripts_us);
    timing->script_load_blockers_ms = script_phase_us_to_ms(script_timing->load_blockers_us);
    timing->script_complete_ms = script_phase_us_to_ms(script_timing->complete_us);
    timing->script_body_onload_ms = script_phase_us_to_ms(script_timing->body_onload_us);
    timing->script_window_load_ms = script_phase_us_to_ms(script_timing->window_load_us);
    timing->script_event_loop_ms = script_phase_us_to_ms(script_timing->event_loop_us);
    timing->script_runtime_cleanup_ms = script_phase_us_to_ms(script_timing->runtime_cleanup_us);
    timing->script_source_cleanup_ms = script_phase_us_to_ms(script_timing->source_cleanup_us);
    timing->script_cache_lookups = script_timing->cache_lookups;
    timing->script_cache_hits = script_timing->cache_hits;
    timing->script_cache_misses = script_timing->cache_misses;
    timing->script_cache_compiles = script_timing->cache_compiles;
    timing->script_cache_instantiations = script_timing->cache_instantiations;
}

static void set_detailed_load_timing(LayoutPhaseTiming* timing,
                                     const HtmlLoadPhaseTiming* html_timing) {
    if (!timing || !html_timing) return;
    timing->load_setup_ms = timing->load_ms - html_timing->loader_total_ms;
    if (timing->load_setup_ms < 0.0) timing->load_setup_ms = 0.0;
    timing->load_read_ms = html_timing->read_ms;
    timing->load_html_parse_ms = html_timing->html_parse_ms;
    timing->load_dom_build_ms = html_timing->dom_build_ms;
    timing->load_css_parse_ms = html_timing->css_parse_ms;
    timing->load_stylesheet_setup_ms = html_timing->stylesheet_setup_ms;
    timing->load_inline_style_ms = html_timing->inline_style_ms;
    timing->load_initial_cascade_ms = html_timing->initial_cascade_ms;
    timing->load_script_exec_ms = html_timing->script_exec_ms;
    timing->load_post_script_ms = html_timing->post_script_ms;
    timing->load_final_cascade_ms = html_timing->final_cascade_ms;
    timing->load_finalize_ms = html_timing->finalize_ms;
}

static double js_phase_us_to_ms(long us) {
    return (double)us / 1000.0;
}

static void layout_phase_timing_set_js(LayoutPhaseTiming* timing,
                                       const JsMirPhaseTiming* js) {
    if (!timing || !js) return;
    timing->js_parse_ms = js_phase_us_to_ms(js->parse_us);
    timing->js_ast_ms = js_phase_us_to_ms(js->ast_us);
    timing->js_transpile_ms = js_phase_us_to_ms(js->early_us + js->imports_us + js->mir_us);
    timing->js_link_ms = js_phase_us_to_ms(js->link_us);
    timing->js_exec_ms = js_phase_us_to_ms(js->execute_us);
    timing->js_cleanup_ms = js_phase_us_to_ms(js->cleanup_us);
    timing->js_total_ms = js_phase_us_to_ms(js->total_us);
    timing->js_preamble_ms = js_phase_us_to_ms(js->preamble_us);
}

static void layout_phase_timing_set_load(LayoutPhaseTiming* timing,
                                         double total_ms, double load_ms,
                                         const HtmlLoadPhaseTiming* html,
                                         const DocumentScriptPhaseTiming* script,
                                         const JsMirPhaseTiming* js) {
    timing->total_ms = total_ms;
    timing->load_ms = load_ms;
    set_detailed_load_timing(timing, html);
    set_detailed_script_timing(timing, script);
    layout_phase_timing_set_js(timing, js);
    timing->document_parse_ms = load_ms - timing->js_total_ms;
    if (timing->document_parse_ms < 0.0) timing->document_parse_ms = 0.0;
}

static void write_layout_phase_timing(FILE* timing_file, const char* input_file,
                                      bool success, const LayoutPhaseTiming* timing) {
    if (!timing_file || !timing) return;

    char buf[4096];
    JsonWriter w;
    double js_attributed_ms = timing->js_parse_ms + timing->js_ast_ms +
        timing->js_transpile_ms + timing->js_link_ms + timing->js_exec_ms +
        timing->js_cleanup_ms;
    double js_unattributed_ms = timing->js_total_ms - js_attributed_ms;
    if (js_unattributed_ms < 0.0) js_unattributed_ms = 0.0;
    double script_lifecycle_ms = timing->script_interactive_ms +
        timing->script_dom_content_loaded_ms + timing->script_complete_ms +
        timing->script_window_load_ms;
    jw_init(&w, buf, sizeof(buf));
    jw_obj_begin(&w);
        jw_kv_str(&w, "file", input_file ? input_file : "");
        jw_kv_bool(&w, "success", success);
        jw_kv_double(&w, "total_ms", timing->total_ms);
        jw_kv_double(&w, "load_ms", timing->load_ms);
        jw_kv_double(&w, "document_parse_ms", timing->document_parse_ms);
        jw_kv_double(&w, "load_setup_ms", timing->load_setup_ms);
        jw_kv_double(&w, "load_read_ms", timing->load_read_ms);
        jw_kv_double(&w, "load_html_parse_ms", timing->load_html_parse_ms);
        jw_kv_double(&w, "load_dom_build_ms", timing->load_dom_build_ms);
        jw_kv_double(&w, "load_css_parse_ms", timing->load_css_parse_ms);
        jw_kv_double(&w, "load_stylesheet_setup_ms", timing->load_stylesheet_setup_ms);
        jw_kv_double(&w, "load_inline_style_ms", timing->load_inline_style_ms);
        jw_kv_double(&w, "load_initial_cascade_ms", timing->load_initial_cascade_ms);
        jw_kv_double(&w, "load_script_exec_ms", timing->load_script_exec_ms);
        jw_kv_double(&w, "load_post_script_ms", timing->load_post_script_ms);
        jw_kv_double(&w, "load_final_cascade_ms", timing->load_final_cascade_ms);
        jw_kv_double(&w, "load_finalize_ms", timing->load_finalize_ms);
        jw_kv_double(&w, "script_collect_ms", timing->script_collect_ms);
        jw_kv_double(&w, "script_runtime_setup_ms", timing->script_runtime_setup_ms);
        jw_kv_double(&w, "script_postdom_total_ms", timing->script_postdom_total_ms);
        jw_kv_double(&w, "script_preamble_wall_ms", timing->script_preamble_wall_ms);
        jw_kv_double(&w, "script_scheduler_ms", timing->script_scheduler_ms);
        jw_kv_double(&w, "script_interactive_ms", timing->script_interactive_ms);
        jw_kv_double(&w, "script_user_scripts_ms", timing->script_user_scripts_ms);
        jw_kv_double(&w, "script_dom_content_loaded_ms", timing->script_dom_content_loaded_ms);
        jw_kv_double(&w, "script_async_scripts_ms", timing->script_async_scripts_ms);
        jw_kv_double(&w, "script_load_blockers_ms", timing->script_load_blockers_ms);
        jw_kv_double(&w, "script_complete_ms", timing->script_complete_ms);
        jw_kv_double(&w, "script_body_onload_ms", timing->script_body_onload_ms);
        jw_kv_double(&w, "script_window_load_ms", timing->script_window_load_ms);
        jw_kv_double(&w, "script_event_loop_ms", timing->script_event_loop_ms);
        jw_kv_double(&w, "script_runtime_cleanup_ms", timing->script_runtime_cleanup_ms);
        jw_kv_double(&w, "script_source_cleanup_ms", timing->script_source_cleanup_ms);
        jw_kv_uint(&w, "script_cache_lookups", timing->script_cache_lookups);
        jw_kv_uint(&w, "script_cache_hits", timing->script_cache_hits);
        jw_kv_uint(&w, "script_cache_misses", timing->script_cache_misses);
        jw_kv_uint(&w, "script_cache_compiles", timing->script_cache_compiles);
        jw_kv_uint(&w, "script_cache_instantiations", timing->script_cache_instantiations);
        jw_kv_double(&w, "script_lifecycle_ms", script_lifecycle_ms);
        jw_kv_double(&w, "js_parse_ms", timing->js_parse_ms);
        jw_kv_double(&w, "js_ast_ms", timing->js_ast_ms);
        jw_kv_double(&w, "js_transpile_ms", timing->js_transpile_ms);
        jw_kv_double(&w, "js_link_ms", timing->js_link_ms);
        jw_kv_double(&w, "js_exec_ms", timing->js_exec_ms);
        jw_kv_double(&w, "js_cleanup_ms", timing->js_cleanup_ms);
        jw_kv_double(&w, "js_total_ms", timing->js_total_ms);
        jw_kv_double(&w, "js_unattributed_ms", js_unattributed_ms);
        jw_kv_double(&w, "js_preamble_ms", timing->js_preamble_ms);
        jw_kv_double(&w, "layout_ms", timing->layout_ms);
        jw_kv_double(&w, "output_ms", timing->output_ms);
    jw_obj_end(&w);

    const char* json = jw_finish(&w);
    if (!json) {
        log_error("layout_timing_output: JSON buffer overflow for %s",
                  input_file ? input_file : "(unknown)");
        return;
    }
    size_t len = strlen(json);
    fwrite(json, 1, len, timing_file);
    fwrite("\n", 1, 1, timing_file);
    fflush(timing_file);
}

static void layout_close_failed_load_logs(EventStateLog** event_log,
                                          StateDumpLog** state_dump) {
    if (event_log && *event_log) {
        event_state_log_document(*event_log, "load_failed");
        event_state_log_close(*event_log);
        *event_log = nullptr;
    }
    if (state_dump && *state_dump) {
        radiant_state_dump_close(*state_dump);
        *state_dump = nullptr;
    }
}

// load and layout one document file.
static bool layout_single_file(
    const char* input_file,
    const char* output_path,
    const char* css_file,
    int viewport_width,
    int viewport_height,
    UiContext* ui_context,
    Url* cwd,
    bool track_source_lines = false,
    bool enable_event_log = false,
    bool enable_state_dump = false,
    FILE* timing_file = nullptr,
    const char* memory_profile_output_file = nullptr,
    bool auto_close = false,
    bool disable_animations = false,
    FILE* result_stream = nullptr
) {
    auto total_start = std::chrono::high_resolution_clock::now();
    auto load_start = total_start;
    auto load_end = load_start;
    auto layout_start = load_start;
    auto layout_end = layout_start;
    auto output_start = layout_start;
    auto output_end = output_start;
    bool layout_phase_ran = false;
    bool output_phase_ran = false;
    JsMirPhaseTiming document_js_timing = {};
    HtmlLoadPhaseTiming html_load_timing = {};
    DocumentScriptPhaseTiming document_script_timing = {};

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!pool) {
        log_error("Failed to create memory pool for %s", input_file);
        return false;
    }
    js_mir_begin_document_phase_timing();

    DocumentJsHostConfig js_host_config = {
        ui_context,
        false,
        auto_close,
        false,
        0.0
    };

    Url* input_url = url_parse_with_base(input_file, cwd);
    EventStateLog* event_log = nullptr;
    if (enable_event_log && input_url) {
        event_log = event_state_log_open(input_file, url_get_href(input_url));
        if (event_log) {
            event_state_log_session_start(event_log, viewport_width, viewport_height, 1.0);
            event_state_log_document(event_log, "load_start");
        }
    }
    StateDumpLog* state_dump = nullptr;
    if (enable_state_dump && input_url) {
        state_dump = radiant_state_dump_open(input_file);
    }

    const char* effective_ext = nullptr;
    bool is_http_url = (input_url->scheme == URL_SCHEME_HTTP || input_url->scheme == URL_SCHEME_HTTPS);

    DomDocument* doc = nullptr;
    const char* ext = strrchr(input_file, '.');

    bool has_valid_ext = layout_path_has_known_extension(input_file) ||
        (ext && (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0));

    if (is_http_url && !has_valid_ext) {
        const char* url_str = url_get_href(input_url);
        log_info("[Layout] HTTP URL without extension, fetching to determine type: %s", url_str);

        FetchResponse* response = http_fetch(url_str, nullptr);
        if (!response || !response->data || response->status_code >= 400) {
            log_error("Failed to fetch URL: %s (HTTP %ld)", url_str,
                      response ? response->status_code : 0);
            if (response) free_fetch_response(response);
            layout_close_failed_load_logs(&event_log, &state_dump);
            pool_destroy(pool);
            return false;
        }

        effective_ext = content_type_to_extension(response->content_type);
        log_info("[Layout] HTTP Content-Type: %s -> extension: %s",
                 response->content_type ? response->content_type : "(none)",
                 effective_ext ? effective_ext : ".html");

        ext = effective_ext;

        free_fetch_response(response);
    }

    bool special_handled = false;
    const char* route_path = effective_ext ? effective_ext : input_file;
    doc = load_layout_special_file(input_url, route_path, viewport_width, viewport_height,
                                   pool, false, false, &special_handled);
    if (!special_handled) {
        const int max_redirects = 8;
        for (int redirect_count = 0; redirect_count <= max_redirects; redirect_count++) {
            script_runner_set_retain_js_state(false);
            script_runner_set_execute_external_scripts(true);
            doc = load_lambda_html_doc_profiled(input_url, css_file, viewport_width,
                                                viewport_height, pool, nullptr,
                                                track_source_lines, true,
                                                timing_file ? &html_load_timing : nullptr,
                                                timing_file ? &document_script_timing : nullptr,
                                                &js_host_config);
            if (!doc || !doc->pending_navigation_url || !doc->pending_navigation_url[0]) {
                break;
            }

            Url* next_url = doc->url
                ? url_parse_with_base(doc->pending_navigation_url, doc->url)
                : url_parse(doc->pending_navigation_url);
            if (!next_url || !url_is_valid(next_url)) {
                log_error("[Layout] Invalid pending navigation URL: %s", doc->pending_navigation_url);
                if (next_url) url_destroy(next_url);
                break;
            }

            const char* next_href = url_get_href(next_url);
            log_info("[Layout] Following document navigation to %s", next_href ? next_href : "(null)");
            script_runner_cleanup_js_state(doc);
            dom_document_destroy(doc);
            doc = nullptr;
            if (input_url) url_destroy(input_url);
            pool_destroy(pool);

            input_url = next_url;
            pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
            if (!pool) {
                log_error("Failed to create memory pool for redirected document: %s",
                          next_href ? next_href : "(null)");
                break;
            }
        }
    }
    if (!doc) {
        load_end = std::chrono::high_resolution_clock::now();
        js_mir_end_document_phase_timing(&document_js_timing);
        LayoutPhaseTiming timing = {};
        layout_phase_timing_set_load(
            &timing,
            std::chrono::duration<double, std::milli>(load_end - total_start).count(),
            std::chrono::duration<double, std::milli>(load_end - load_start).count(),
            &html_load_timing, &document_script_timing, &document_js_timing);
        write_layout_phase_timing(timing_file, input_file, false, &timing);
        log_error("Failed to load document: %s", input_file);
        layout_close_failed_load_logs(&event_log, &state_dump);
        pool_destroy(pool);
        return false;
    }

    load_end = std::chrono::high_resolution_clock::now();
    js_mir_end_document_phase_timing(&document_js_timing);

    ui_context->document = doc;
    doc->disable_css_animations = disable_animations;

    DocState* state = radiant_document_ensure_state(doc, "layout_single_file");
    if (!state) {
        log_error("Failed to create DocState for headless document: %s", input_file);
        layout_close_failed_load_logs(&event_log, &state_dump);
        script_runner_cleanup_js_state(doc);
        dom_document_destroy(doc);
        ui_context->document = nullptr;
        if (input_url) {
            url_destroy(input_url);
        }
        pool_destroy(pool);
        return false;
    }

    if (event_log) {
        event_state_log_document(event_log, "load_complete");
    }
    radiant_state_set_dump_log(state, state_dump);

    uint64_t layout_cascade_id = state_begin_event_cascade(
        state,
        event_log,
        "layout");

    process_document_font_faces(ui_context, doc);
    EnhancedFileCache* layout_file_cache = layout_prepare_network_resources(ui_context, doc);

    if (!doc->root && doc->view_tree && doc->view_tree->root) {
        log_info("[Layout] Document already has non-DOM view_tree (PDF/SVG/image), skipping CSS layout");
    } else {
        auto event_layout_start = std::chrono::high_resolution_clock::now();
        layout_start = event_layout_start;
        // A script geometry read can create a provisional DOM view tree before
        // font-face loading and post-script cascade; DOM documents still need
        // this final commit to replace that stale layout-resource epoch.
        layout_html_doc(ui_context, doc, false);
        auto event_layout_end = std::chrono::high_resolution_clock::now();
        layout_end = event_layout_end;
        layout_phase_ran = true;
        if (event_log) {
            char event_buf[1024];
            JsonWriter event_writer;
            double duration_ms = std::chrono::duration<double, std::milli>(
                event_layout_end - event_layout_start).count();
            event_state_log_begin_record(event_log, &event_writer,
                event_buf, sizeof(event_buf), "layout.stats", layout_cascade_id);
            jw_key(&event_writer, "data");
            jw_obj_begin(&event_writer);
                jw_kv_double(&event_writer, "duration_ms", duration_ms);
                jw_kv_int(&event_writer, "viewport_width", viewport_width);
                jw_kv_int(&event_writer, "viewport_height", viewport_height);
                jw_kv_bool(&event_writer, "full", true);
                if (doc->view_tree && doc->view_tree->root) {
                    event_state_log_write_node_ref(&event_writer, "root", doc->view_tree->root);
                }
            jw_obj_end(&event_writer);
            event_state_log_finish_record(event_log, &event_writer);
        }
    }

    bool success = !memory_profile_output_file || view_memory_profile_write(
        doc, input_file, memory_profile_output_file);
    if (!doc->view_tree || !doc->view_tree->root) {
        log_warn("Layout computation did not produce view tree for %s", input_file);
        success = false;
    } else {
        log_info("[Layout] Layout computed successfully for %s", input_file);

        bool is_pdf = ext && strcmp(ext, ".pdf") == 0;
        bool is_svg = ext && strcmp(ext, ".svg") == 0;
        if (is_pdf || is_svg) {
            set_combine_text_nodes(false);
        }

        output_start = std::chrono::high_resolution_clock::now();
        ViewElement* root = lam::unsafe_view_element_storage(doc->view_tree->root);
        if (result_stream) {
            // The batch pipe carries only schema-v2 frames; never materialize a legacy file first.
            success = stream_view_tree_json(root, input_file, result_stream) && success;
        } else {
            print_view_tree(root, doc->url, output_path);
        }
        output_end = std::chrono::high_resolution_clock::now();
        output_phase_ran = true;

        if (is_pdf || is_svg) {
            set_combine_text_nodes(true);
        }
    }

    {
        auto total_end = std::chrono::high_resolution_clock::now();
        LayoutPhaseTiming timing = {};
        layout_phase_timing_set_load(
            &timing,
            std::chrono::duration<double, std::milli>(total_end - total_start).count(),
            std::chrono::duration<double, std::milli>(load_end - load_start).count(),
            &html_load_timing, &document_script_timing, &document_js_timing);
        timing.layout_ms = layout_phase_ran
            ? std::chrono::duration<double, std::milli>(layout_end - layout_start).count()
            : 0.0;
        timing.output_ms = output_phase_ran
            ? std::chrono::duration<double, std::milli>(output_end - output_start).count()
            : 0.0;
        write_layout_phase_timing(timing_file, input_file, success, &timing);
    }

    if (event_log || state_dump) {
        state_end_event_cascade(
            doc && doc->state ? (DocState*)doc->state : nullptr,
            event_log,
            layout_cascade_id);
    }

    if (event_log) {
        event_state_log_document(event_log, "unload_start");
    }

    if (doc) {
        if (doc->resource_manager) {
            radiant_cleanup_network_support(doc);
        }
        Runtime* render_runtime = dom_document_script_runtime(doc);
        if (render_runtime &&
                !eval_context_init(runtime_get_eval_context(render_runtime))) {
            log_error("[Layout] document cleanup reached a foreign eval thread");
        }
        source_pos_bridge_reset();
        radiant_document_destroy_state(doc);
        render_map_destroy();
        script_runner_cleanup_js_state(doc);

        if (doc->view_tree) {
            view_pool_destroy(doc->view_tree);
            mem_free(doc->view_tree);
            doc->view_tree = nullptr;
        }
        dom_document_destroy(doc);
        if (ui_context->document == doc) {
            ui_context->document = nullptr;
        }
    }
    if (event_log) {
        event_state_log_document(event_log, "unload_complete");
        event_state_log_close(event_log);
        event_log = nullptr;
    }
    if (state_dump) {
        radiant_state_dump_close(state_dump);
        state_dump = nullptr;
    }

    if (input_url) {
        InputManager::detach_url(input_url);
        url_destroy(input_url);
        input_url = nullptr;
    }

    if (layout_file_cache) {
        enhanced_cache_destroy(layout_file_cache);
        layout_file_cache = nullptr;
    }

    pool_destroy(pool);

    if (!script_runner_js_batch_cleanup_unsafe()) {
        js_event_loop_shutdown();
        js_batch_reset();
        js_dom_batch_reset();
        js_globals_batch_reset();

        script_runner_cleanup_heap();
    }
    lambda_uv_cleanup();

    fontface_cleanup(ui_context);
    font_context_reset_document_fonts(ui_context->font_ctx);
    font_context_reset_glyph_caches(ui_context->font_ctx);

    {
#ifdef __APPLE__
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
        long rss_kb = (long)(info.resident_size / 1024);
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc;
        long rss_kb = 0;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            rss_kb = (long)(pmc.WorkingSetSize / 1024);
#else
        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        long rss_kb = ru.ru_maxrss; // Linux: already in KB
#endif
        FontCacheStats stats = font_get_cache_stats(ui_context->font_ctx);
        static int file_num = 0;
        file_num++;
        if (file_num <= 10 || file_num % 50 == 0) {
            fprintf(stderr, "[MEMDIAG] file=%d rss=%ldMB main_arena=%zuKB glyph_arena=%zuKB faces=%d glyphs=%d loaded=%d\n",
                    file_num, rss_kb / 1024, stats.main_arena_bytes / 1024,
                    stats.glyph_arena_bytes / 1024, stats.face_count,
                    stats.glyph_cache_count, stats.loaded_glyph_count);
        }
    }

    image_cache_cleanup(ui_context);

    InputManager::destroy_global();

    ui_context->document = nullptr;

    return success;
}

// generate a batch-mode output path.
static char* generate_output_path(const char* input_file, const char* output_dir) {
    // Extract basename from input file
    const char* basename = strrchr(input_file, '/');
    if (!basename) {
        basename = strrchr(input_file, '\\');
    }
    const char* file_start = basename ? basename + 1 : input_file;

    // Extract parent directory name to disambiguate files with same basename
    // (e.g., web-tmpl/dreamy/index.html and web-tmpl/zenlike/index.html)
    const char* parent_name = NULL;
    size_t parent_len = 0;
    if (basename && basename > input_file) {
        // Find the start of the parent directory
        const char* parent_end = basename; // points to the last '/' before basename
        const char* p = parent_end - 1;
        while (p >= input_file && *p != '/' && *p != '\\') {
            p--;
        }
        parent_name = p + 1;
        parent_len = (size_t)(parent_end - parent_name);
    }

    // Find extension and replace with .json
    const char* ext = strrchr(file_start, '.');
    size_t name_len = ext ? (size_t)(ext - file_start) : strlen(file_start);

    // Build output path: output_dir/[parentdir__]basename.json
    size_t dir_len = strlen(output_dir);
    bool need_slash = (dir_len > 0 && output_dir[dir_len - 1] != '/' && output_dir[dir_len - 1] != '\\');
    // prefix = "parentdir__" if parent exists (parent_len + 2 for "__")
    size_t prefix_len = (parent_name && parent_len > 0) ? parent_len + 2 : 0;

    size_t path_len = dir_len + (need_slash ? 1 : 0) + prefix_len + name_len + 5 + 1; // ".json" + null
    char* output_path = (char*)mem_alloc(path_len, MEM_CAT_LAYOUT);

    if (prefix_len > 0) {
        if (need_slash) {
            snprintf(output_path, path_len, "%s/%.*s__%.*s.json", output_dir,
                     (int)parent_len, parent_name, (int)name_len, file_start);
        } else {
            snprintf(output_path, path_len, "%s%.*s__%.*s.json", output_dir,
                     (int)parent_len, parent_name, (int)name_len, file_start);
        }
    } else {
        if (need_slash) {
            snprintf(output_path, path_len, "%s/%.*s.json", output_dir, (int)name_len, file_start);
        } else {
            snprintf(output_path, path_len, "%s%.*s.json", output_dir, (int)name_len, file_start);
        }
    }

    return output_path;
}

static int layout_resource_wait_timeout_ms() {
    const char* env = getenv("RADIANT_LAYOUT_RESOURCE_TIMEOUT_MS");
    if (env && env[0]) {
        char* end = nullptr;
        long parsed = strtol(env, &end, 10);
        if (end != env && parsed >= 0) {
            if (parsed > 5000) return 5000;
            return (int)parsed; // INT_CAST_OK: bounded millisecond timeout.
        }
    }
    return 1000;
}

static bool layout_doc_has_remote_font_face(DomDocument* doc) {
    if (!doc || !doc->stylesheets || doc->stylesheet_count <= 0) return false;

    for (int s = 0; s < doc->stylesheet_count; s++) {
        CssStylesheet* sheet = doc->stylesheets[s];
        if (!sheet || !sheet->rules) continue;
        for (size_t r = 0; r < sheet->rule_count; r++) {
            CssRule* rule = sheet->rules[r];
            if (!rule || rule->type != CSS_RULE_FONT_FACE) continue;
            const char* content = rule->data.generic_rule.content;
            if (content && (strstr(content, "http://") || strstr(content, "https://"))) {
                return true;
            }
        }
    }
    return false;
}

static EnhancedFileCache* layout_prepare_network_resources(UiContext* ui_context,
                                                           DomDocument* doc) {
    if (!ui_context || !doc) return nullptr;
    if (!layout_doc_has_remote_font_face(doc)) return nullptr;

    // The resource manager changes image/media loading to async mode, so the
    // layout CLI only enables it for documents that need remote webfont metrics.
    network_downloader_init_shared();
    EnhancedFileCache* file_cache = enhanced_cache_create("./temp/cache",
        100 * 1024 * 1024, 10000);
    if (!file_cache) {
        log_warn("[Layout] Network cache unavailable; proceeding without async resources");
        return nullptr;
    }

    if (radiant_init_network_support(doc, NULL, file_cache) != 0) {
        log_warn("[Layout] Network support unavailable; proceeding without async resources");
        enhanced_cache_destroy(file_cache);
        return nullptr;
    }

    resource_manager_set_ui_context(doc->resource_manager, ui_context);
    radiant_discover_document_font_resources(doc);

    int waited_ms = 0;
    const int poll_ms = 10;
    int timeout_ms = layout_resource_wait_timeout_ms();
    while (timeout_ms > 0 && waited_ms < timeout_ms &&
           !resource_manager_is_fully_loaded(doc->resource_manager)) {
        resource_manager_flush_layout_updates(doc->resource_manager);
#ifndef _WIN32
        usleep((useconds_t)poll_ms * 1000);
#endif
        waited_ms += poll_ms;
    }
    resource_manager_flush_layout_updates(doc->resource_manager);

    int total_resources = 0;
    int completed_resources = 0;
    int failed_resources = 0;
    resource_manager_get_stats(doc->resource_manager, &total_resources,
                               &completed_resources, &failed_resources);
    log_info("[Layout] Network resources total=%d completed=%d failed=%d waited=%dms",
             total_resources, completed_resources, failed_resources, waited_ms);
    return file_cache;
}

// run the layout command in single-file or batch mode.
// Crash recovery for layout_single_file — catches SIGSEGV/SIGBUS from
// Apple framework code (CoreText/CoreGraphics font rasterisation, vImage lazy load, etc.)
// that cannot be fixed in our code.
#ifndef _WIN32
static sigjmp_buf layout_crash_jmpbuf;
static volatile sig_atomic_t layout_crash_guarded = 0;

static void layout_crash_handler(int sig, siginfo_t* info, void* ctx) {
    if (layout_crash_guarded) {
        const char* msg = (sig == SIGBUS)
            ? "\n=== RECOVERED: SIGBUS during layout (Apple framework bug) ===\n"
            : "\n=== RECOVERED: SIGSEGV during layout ===\n";
        write(STDERR_FILENO, msg, strlen(msg));
        // Print backtrace for diagnostics
        void* callstack[128];
        int frames = backtrace(callstack, 128);
        backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
        write(STDERR_FILENO, "=== END BACKTRACE ===\n", 22);
        layout_crash_guarded = 0;
        siglongjmp(layout_crash_jmpbuf, sig);
    }
    // not guarded — print backtrace and exit
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
    fprintf(stderr, "=== END BACKTRACE ===\n");
    _exit(128 + sig);
}
#endif // !_WIN32

// Legacy crash handler for SIGTRAP/SIGABRT (non-recoverable)
static void crash_signal_handler(int sig) {
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
#ifndef _WIN32
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
#endif
    fprintf(stderr, "=== END BACKTRACE ===\n");
    _exit(128 + sig);
}

int cmd_layout(int argc, char** argv) {
#ifndef _WIN32
    signal(SIGTRAP, crash_signal_handler);
#endif
    signal(SIGABRT, crash_signal_handler);
#ifndef _WIN32
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = layout_crash_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }
#endif

    if (!log_is_disabled() && file_exists("log.conf")) {
        FILE *file = fopen("log.txt", "w");
        if (file) { fclose(file); }
        log_parse_config_file("log.conf");
    }

    LayoutOptions opts;
    if (!parse_layout_args(argc, argv, &opts)) {
        return 1;
    }

    bool batch_mode = (opts.input_file_count > 1) || (opts.output_dir != nullptr) ||
        opts.stream_layout_results;
    bool auto_close = opts.auto_close || shell_getenv("LAMBDA_AUTO_CLOSE") != nullptr;

    if (batch_mode) {
        log_disable_all();
    }

    UiContext ui_context;
    memset(&ui_context, 0, sizeof(UiContext));

    if (ui_context_init(&ui_context, true, 1.0f) != 0) {
        log_error("Failed to initialize UI context");
        return 1;
    }
    bool js_mir_cache_enabled = batch_mode &&
        shell_getenv("LAMBDA_DISABLE_JS_MIR_CACHE") == nullptr;
    JsMirCache* js_mir_cache = js_mir_cache_enabled ? js_mir_cache_create() : nullptr;
    if (js_mir_cache_enabled && !js_mir_cache) {
        log_error("layout_js_mir_cache: failed to create batch cache; continuing uncached");
    }
    script_runner_set_js_mir_cache(js_mir_cache);

    for (int i = 0; i < opts.font_dir_count; i++) {
        font_context_add_scan_directory(ui_context.font_ctx, opts.font_dirs[i]);
    }

    ui_context.window_width = opts.viewport_width;
    ui_context.window_height = opts.viewport_height;
    ui_context.viewport_width = opts.viewport_width;
    ui_context.viewport_height = opts.viewport_height;

    ui_context_create_surface(&ui_context, opts.viewport_width, opts.viewport_height);

    Url* cwd = get_current_dir();

    FILE* timing_file = nullptr;
    if (opts.timing_output_file) {
        timing_file = fopen(opts.timing_output_file, "w");
        if (!timing_file) {
            log_error("layout_timing_output: failed to open %s", opts.timing_output_file);
        }
    }

    int success_count = 0;
    int failure_count = 0;
    auto batch_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < opts.input_file_count; i++) {
        const char* input_file = opts.input_files[i];
        const char* output_path = nullptr;
        char* allocated_output = nullptr;

        if (batch_mode && opts.output_dir) {
            allocated_output = generate_output_path(input_file, opts.output_dir);
            output_path = allocated_output;
        } else {
            output_path = opts.view_output_file;
        }

        bool success = false;
#ifdef _WIN32
        try {
            success = layout_single_file(
                input_file,
                output_path,
                opts.css_file,
                opts.viewport_width,
                opts.viewport_height,
                &ui_context,
                cwd,
                opts.debug,
                opts.event_log,
                opts.state_dump,
                timing_file,
                opts.memory_profile_output_file,
                auto_close,
                opts.disable_animations,
                opts.stream_layout_results ? stdout : nullptr
            );
        } catch (...) {
            log_error("batch layout: uncaught exception processing %s", input_file);
            success = false;
        }
#else
        layout_crash_guarded = 1;
        int crash_sig = sigsetjmp(layout_crash_jmpbuf, 1);
        if (crash_sig == 0) {
            try {
                success = layout_single_file(
                    input_file,
                    output_path,
                    opts.css_file,
                    opts.viewport_width,
                    opts.viewport_height,
                    &ui_context,
                    cwd,
                    opts.debug,
                    opts.event_log,
                    opts.state_dump,
                    timing_file,
                    opts.memory_profile_output_file,
                    auto_close,
                    opts.disable_animations,
                    opts.stream_layout_results ? stdout : nullptr
                );
            } catch (...) {
                log_error("batch layout: uncaught exception processing %s", input_file);
                success = false;
            }
            layout_crash_guarded = 0;
        } else {
            fprintf(stderr, "layout: recovered from signal %d processing %s — exiting\n",
                    crash_sig, input_file);
            _exit(1);
        }
#endif

        if (!success && opts.stream_layout_results) {
            // Every non-crashing input gets a terminal frame so the runner retries only true gaps.
            write_layout_result_frame(stdout, input_file, false);
        }

        if (success) {
            success_count++;
        } else {
            failure_count++;
            if (!opts.continue_on_error && opts.input_file_count > 1) {
                log_error("Stopping batch due to error (use --continue-on-error to continue)");
                if (allocated_output) mem_free(allocated_output);
                break;
            }
        }

        if (allocated_output) {
            mem_free(allocated_output);
        }
    }

    auto batch_end = std::chrono::high_resolution_clock::now();
    double total_time_ms = std::chrono::duration<double, std::milli>(batch_end - batch_start).count();

    if (!opts.stream_layout_results &&
            (opts.summary || (batch_mode && opts.input_file_count > 1))) {
        printf("\n=== Layout Summary ===\n");
        printf("Files processed: %d\n", success_count + failure_count);
        printf("Successful: %d\n", success_count);
        printf("Failed: %d\n", failure_count);
        printf("Total time: %.1f ms\n", total_time_ms);
        if (success_count + failure_count > 0) {
            printf("Avg time per file: %.1f ms\n", total_time_ms / (success_count + failure_count));
        }
        printf("======================\n");
    }

    if (timing_file) {
        fclose(timing_file);
        timing_file = nullptr;
    }
    script_runner_set_js_mir_cache(nullptr);
    js_mir_cache_destroy(js_mir_cache);
    ui_context_cleanup(&ui_context);
    if (cwd) url_destroy(cwd);

    if (!opts.stream_layout_results) {
        printf("Completed layout command: %d success, %d failed\n", success_count, failure_count);
    }
    log_notice("Completed layout command: %d success, %d failed", success_count, failure_count);
    return failure_count > 0 ? 1 : 0;
}
