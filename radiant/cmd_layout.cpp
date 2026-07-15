/**
 * lambda layout - HTML Layout Command with Lambda CSS
 *
 * Computes layout for HTML documents using Lambda-parsed HTML/CSS and Radiant's layout engine.
 * This is separate from the Lexbor-based CSS system.
 *
 * Usage:
 *   lambda layout input.html [-o output.json] [-c styles.css] [-vw 1200] [-vh 800]
 *
 * Options:
 *   -o, --output FILE              Output file for layout results (default: stdout)
 *   -c, --css FILE                 External CSS file to apply
 *   -vw, --viewport-width WIDTH    Viewport width in pixels (default: 1200)
 *   -vh, --viewport-height HEIGHT  Viewport height in pixels (default: 800)
 *   --format FORMAT                Output format: json, text (default: text)
 *   --debug                        Enable debug output
 *   --event-log                    Emit per-document JSONL event/state log under ./temp/
 *   --state-dump                   Emit per-cascade Mark state-store dump under ./temp/state/
 *   --flavor FLAVOR                LaTeX rendering pipeline: latex-js (default), tex-proper
 */

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
static inline int backtrace(void** arr, int max) { (void)arr; (void)max; return 0; }
static inline void backtrace_symbols_fd(void** arr, int n, int fd) { (void)arr; (void)n; (void)fd; }
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
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/input/html5/html5_parser.h"
#include "../lambda/format/format.h"
#include "../lambda/transpiler.hpp"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_downloader.h"
#include "../lambda/network/network_integration.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/mark_builder.hpp"
#include "../radiant/view.hpp"
#include "render.hpp"
#include "../radiant/layout.hpp"
#include "resource_resolver.hpp"
#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "../radiant/radiant.hpp"
#include "../lib/tagged.hpp"
#include "../lambda/render_map.h"
#include "../lambda/template_state.h"

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
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine, int depth = 0);
void apply_stylesheet_to_dom_tree_fast(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
static void apply_rule_to_dom_element(DomElement* elem, CssRule* rule, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count, int* linked_count_out = nullptr);
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                CssStylesheet*** stylesheets, int* count, int* capacity,
                                int depth = 0, bool recurse = true);
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                   CssStylesheet*** stylesheets, int* count, int* capacity,
                                   int depth = 0, bool recurse = true);
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool);
static EnhancedFileCache* layout_prepare_network_resources(UiContext* ui_context,
                                                           DomDocument* doc);
static const int MAX_CSS_TREE_DEPTH = 512;

// Current document charset for CSS fallback encoding (set before collect_linked_stylesheets)
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

static bool resolve_wpt_root_resource_path(const char* href, char* out_path, size_t out_size) {
    // WPT server-root fixtures such as /fonts/... live outside test/layout/data/support.
    if (strlen("ref/wpt") + strlen(href) + 1 > out_size) return false;
    snprintf(out_path, out_size, "ref/wpt%s", href);
    return access(out_path, R_OK) == 0;
}

static bool resolve_layout_support_resource_path(const char* href, const char* base_path,
                                                 char* out_path, size_t out_size) {
    if (!href || href[0] != '/' || href[1] == '/' || !base_path || !out_path || out_size == 0) {
        return false;
    }

    const char* base_local = base_path;
    if (strncmp(base_local, "file:", 5) == 0) {
        base_local += 5;
        if (base_local[0] == '/' && base_local[1] == '/') base_local += 2;
    }

    const char* data_marker = strstr(base_local, "/data/");
    size_t marker_prefix_len = 1;
    if (!data_marker && strncmp(base_local, "data/", 5) == 0) {
        data_marker = base_local;
        marker_prefix_len = 0;
    }
    if (!data_marker) {
        data_marker = strstr(base_local, "test/layout/data/");
        marker_prefix_len = 0;
    }
    if (!data_marker) {
        if (strlen("test/layout/data/support") + strlen(href) + 1 > out_size) return false;
        snprintf(out_path, out_size, "test/layout/data/support%s", href);
        if (access(out_path, R_OK) == 0) return true;
        return resolve_wpt_root_resource_path(href, out_path, out_size);
    }

    size_t data_root_len = data_marker - base_local + marker_prefix_len + strlen("data");
    if (data_root_len + strlen("/support") + strlen(href) + 1 > out_size) return false;

    memcpy(out_path, base_local, data_root_len);
    out_path[data_root_len] = '\0';
    strncat(out_path, "/support", out_size - strlen(out_path) - 1);
    strncat(out_path, href, out_size - strlen(out_path) - 1);
    if (access(out_path, R_OK) == 0) return true;
    return resolve_wpt_root_resource_path(href, out_path, out_size);
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

// Forward declaration for charset conversion.
char* convert_charset_to_utf8(const char* content, size_t content_len, const char* from_charset);
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool, int depth = 0);
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
    int viewport_width, int viewport_height, float pixel_ratio);

// Element-to-DOM map functions (from dom_element.cpp, Phase 12)
HashMap* element_dom_map_create(void);
void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem);
DomElement* element_dom_map_lookup(HashMap* map, Element* elem);
bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child);

// Function to determine HTML version from Lambda CSS document DOCTYPE
// This function examines the original Element tree to find DOCTYPE information
// before it gets filtered out during DomElement tree construction
HtmlVersion detect_html_version_from_lambda_element(Element* html_root, Input* input) {
    if (!input || !input->root.item) {
        log_debug("No input or root available for DOCTYPE detection");
        return HTML5;
    }
    log_debug("Detecting HTML version from Lambda Element tree");
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

                        log_debug("Found #doctype: name=%s publicId=%s",
                                  name ? name : "null", public_id ? public_id : "null");

                        // HTML5: <!DOCTYPE html> — name="html", no publicId
                        if (!public_id || public_id[0] == '\0') {
                            log_debug("Detected HTML5 DOCTYPE (no publicId)");
                            return HTML5;
                        }

                        // Check known public identifiers for HTML version
                        if (strstr(public_id, "-//W3C//DTD HTML 4.01//EN") ||
                            strstr(public_id, "-//W3C//DTD HTML 4.0//EN")) {
                            log_debug("Detected HTML 4.0/4.01 Strict DOCTYPE");
                            return HTML4_01_STRICT;
                        }
                        if (strstr(public_id, "Transitional")) {
                            const char* system_id = extract_element_attribute(child_elem, "systemId", nullptr);
                            // WHATWG §13.2.6.4.1: Transitional WITHOUT system identifier → quirks
                            // Transitional WITH system identifier → limited quirks (standards-like)
                            if (system_id && system_id[0] != '\0') {
                                log_debug("Detected Transitional with system ID (limited quirks / standards-like)");
                                return HTML4_01_STRICT;
                            }
                            log_debug("Detected Transitional without system ID (quirks mode)");
                            return HTML4_01_TRANSITIONAL;
                        }
                        if (strstr(public_id, "Frameset")) {
                            log_debug("Detected Frameset DOCTYPE");
                            return HTML4_01_FRAMESET;
                        }
                        if (strstr(public_id, "-//W3C//DTD XHTML 1.0")) {
                            if (strstr(public_id, "Strict")) return HTML4_01_STRICT;
                            return HTML4_01_TRANSITIONAL;
                        }
                        // WHATWG §13.2.6.4.1: Unrecognized publicId → quirks mode
                        // Legacy doctypes (e.g., -//IETF//DTD HTML 2.0, -//SoftQuad//, etc.)
                        // do not match any known standards-mode doctype, so they trigger quirks.
                        log_debug("Unrecognized publicId, using quirks mode: %s", public_id);
                        return HTML_QUIRKS;
                    }
                }
            }
            // #document without #doctype → quirks mode
            log_debug("No #doctype found in #document, using quirks mode");
            return HTML4_01_TRANSITIONAL;
        }
    }
    if (root_type == LMD_TYPE_ARRAY) {
        List* root_list = input->root.array;
        log_debug("Examining root list with %lld items", root_list->length);

        // Search through the list for DOCTYPE element
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Check for DOCTYPE element (case-insensitive)
                if (type && str_ieq_const(type->name.str, strlen(type->name.str), "!DOCTYPE")) {
                    log_debug("Found DOCTYPE element");

                    // Extract DOCTYPE content from the element's children
                    if (elem->length > 0) {
                        Item first_child = elem->items[0];
                        if (get_type_id(first_child) == LMD_TYPE_STRING) {
                            String* doctype_content = (String*)first_child.string_ptr;
                            const char* content = doctype_content->chars;

                            log_debug("DOCTYPE content: '%s'", content);

                            // Parse DOCTYPE content to determine version
                            // Check for HTML 4.01 patterns first (more specific)
                            if (strstr(content, "-//W3C//DTD HTML 4.01//EN")) {
                                log_debug("Detected HTML 4.01 Strict DOCTYPE");
                                return HTML4_01_STRICT;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.01 Transitional//EN")) {
                                log_debug("Detected HTML 4.01 Transitional DOCTYPE");
                                return HTML4_01_TRANSITIONAL;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.01 Frameset//EN")) {
                                log_debug("Detected HTML 4.01 Frameset DOCTYPE");
                                return HTML4_01_FRAMESET;
                            }

                            // Check for HTML 4.0 patterns
                            if (strstr(content, "-//W3C//DTD HTML 4.0//EN")) {
                                log_debug("Detected HTML 4.0 Strict DOCTYPE");
                                return HTML4_01_STRICT;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.0 Transitional//EN")) {
                                log_debug("Detected HTML 4.0 Transitional DOCTYPE");
                                return HTML4_01_TRANSITIONAL;
                            }

                            if (strstr(content, "-//W3C//DTD HTML 4.0 Frameset//EN")) {
                                log_debug("Detected HTML 4.0 Frameset DOCTYPE");
                                return HTML4_01_FRAMESET;
                            }

                            // Check for XHTML patterns
                            if (strstr(content, "-//W3C//DTD XHTML 1.0")) {
                                if (strstr(content, "Strict")) {
                                    log_debug("Detected XHTML 1.0 Strict DOCTYPE");
                                    return HTML4_01_STRICT;
                                }
                                if (strstr(content, "Transitional")) {
                                    log_debug("Detected XHTML 1.0 Transitional DOCTYPE");
                                    return HTML4_01_TRANSITIONAL;
                                }
                                if (strstr(content, "Frameset")) {
                                    log_debug("Detected XHTML 1.0 Frameset DOCTYPE");
                                    return HTML4_01_FRAMESET;
                                }
                                log_debug("Detected XHTML 1.0 DOCTYPE (default to Transitional)");
                                return HTML4_01_TRANSITIONAL; // Default XHTML 1.0
                            }

                            if (strstr(content, "-//W3C//DTD XHTML 1.1//EN")) {
                                log_debug("Detected XHTML 1.1 DOCTYPE");
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
                                    log_debug("Detected HTML5 DOCTYPE");
                                    return HTML5;
                                }
                            }

                            // If we found a DOCTYPE but don't recognize it, assume HTML5
                            log_debug("Found unrecognized DOCTYPE '%s', defaulting to HTML5", content);
                            return HTML5;
                        }
                    }

                    // Empty DOCTYPE content - assume HTML5
                    log_debug("Found empty DOCTYPE, assuming HTML5");
                    return HTML5;
                }
            }
        }
    }

    // No DOCTYPE found - use quirks mode (legacy HTML)
    // Per HTML spec, missing DOCTYPE triggers quirks mode, which uses serif fonts
    log_debug("No DOCTYPE found in Lambda Element tree, using quirks mode (HTML4_01_TRANSITIONAL)");
    return HTML4_01_TRANSITIONAL;
}

/**
 * Apply inline style attribute to a single DOM element
 * Inline styles have highest specificity (1,0,0,0)
 */
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool) {
    if (!dom_elem || !html_elem || !pool) return;

    // Extract 'style' attribute from html_elem
    const char* style_text = extract_element_attribute(html_elem, "style", nullptr);

    if (style_text && strlen(style_text) > 0) {
        log_debug("[CSS] Applying inline style to <%s>: %s",
                dom_elem->tag_name, style_text);

        // Apply the inline style using the DOM element API
        int decl_count = dom_element_apply_inline_style(dom_elem, style_text);

        if (decl_count > 0) {
            log_debug("[CSS] Applied %d inline declarations to <%s>",
                    decl_count, dom_elem->tag_name);
        } else {
            log_debug("[CSS] Warning: Failed to parse inline style for <%s>",
                    dom_elem->tag_name);
        }
    }
}

/**
 * Recursively apply inline style attributes to entire DOM tree
 */
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool, int depth) {
    if (!dom_elem || !html_elem || !pool) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    // Apply inline style to current element
    apply_inline_style_attributes(dom_elem, html_elem, pool);

    // Process children - need to match DOM children with HTML children
    // NOTE: Both HTML and DOM trees contain text nodes, so we need to skip them in parallel
    DomNode* dom_child = dom_elem->first_child;

    // Iterate through HTML children to find matching elements
    for (int64_t i = 0; i < html_elem->length; i++) {
        Item child_item = html_elem->items[i];
        TypeId child_type_id = get_type_id(child_item);

        if (child_type_id == LMD_TYPE_ELEMENT) {
            Element* html_child = child_item.element;

            // Skip HTML elements that are absent from the DOM tree.
            // build_dom_tree_from_element returns nullptr (and does NOT create a
            // DomElement sibling) for:
            //   - comments ("!--") and DOCTYPE → stored as DomComment, not DomElement
            //   - <script> elements WITHOUT an inline display override → skipped
            //     (display:none, no layout role); but a <script style="display:block">
            //     IS kept so its text contributes to Selection.toString().
            //   - XML declarations (tag starting with "?") → not HTML elements
            // For all such cases we must NOT advance dom_child, because there is
            // no corresponding DomElement in the DOM sibling chain to consume.
            TypeElmt* child_type = (TypeElmt*)html_child->type;
            if (child_type) {
                const char* ctn = child_type->name.str;
                size_t ctn_len = strlen(ctn);
                bool is_dom_absent =
                    strcmp(ctn, "!--") == 0 ||
                    strcmp(ctn, "#comment") == 0 ||
                    str_ieq_const(ctn, ctn_len, "!DOCTYPE") ||
                    (ctn_len > 0 && ctn[0] == '?');  // XML declarations
                if (!is_dom_absent && str_ieq_const(ctn, ctn_len, "script")) {
                    // Match dom_element.cpp's conditional skip.
                    const char* sty = extract_element_attribute(html_child, "style", nullptr);
                    bool has_display_override = false;
                    if (sty) {
                        const char* d = strstr(sty, "display");
                        if (d) {
                            const char* colon = strchr(d, ':');
                            if (colon) {
                                const char* v = colon + 1;
                                while (*v == ' ' || *v == '\t') v++;
                                if (strncmp(v, "none", 4) != 0) has_display_override = true;
                            }
                        }
                    }
                    if (!has_display_override) is_dom_absent = true;
                }
                if (is_dom_absent) {
                    continue;  // No corresponding DomElement — do NOT advance dom_child
                }
            }

            if (!dom_child) {
                // HTML has more element children than DOM - shouldn't happen
                break;
            }

            // Skip non-element DOM nodes (text, comments) until we find an element
            while (dom_child && !dom_child->is_element()) {
                dom_child = dom_child->next_sibling;
            }

            if (!dom_child) {
                // Ran out of DOM children
                break;
            }

            DomElement* dom_child_elem = lam::dom_require_element(dom_child);

            // Recursively apply to this child
            apply_inline_styles_to_tree(dom_child_elem, html_child, pool, depth + 1);

            // Move to next DOM sibling
            dom_child = dom_child_elem->next_sibling;
        } else if (child_type_id == LMD_TYPE_STRING) {
            // HTML text node - skip corresponding DOM text node
            if (dom_child && dom_child->is_text()) {
                dom_child = dom_child->next_sibling;
            }
        }
        // Skip other non-element HTML children (comments, etc.)
    }
}

/**
 * Extract the root HTML element from parsed input
 * Skips DOCTYPE, comments, and other non-element nodes
 * Handles both old parser (list root) and HTML5 parser (#document root)
 */
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

        log_debug("Root element type: '%.*s'", (int)root_type_elmt->name.length, root_type_elmt->name.str);

        // HTML5 parser: root is #document, find html child
        if (strview_equal(&root_type_elmt->name, "#document")) {
            log_debug("HTML5 parser detected: root is #document, searching for html element");

            // Search all children of #document for the html element
            // NOTE: HTML5 parser stores children as "attributes" not content
            List* doc_list = (List*)root_elem;

            log_debug("#document: list->length=%lld, content_length=%lld",
                      (long long)doc_list->length,
                      (long long)root_type_elmt->content_length);

            // Iterate through all items (HTML5 parser doesn't use content_length correctly)
            for (int64_t i = 0; i < doc_list->length; i++) {
                Item child = doc_list->items[i];
                TypeId child_type = get_type_id(child);

                log_debug("Child %lld: type_id=%d", (long long)i, child_type);

                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    TypeElmt* child_type_elmt = (TypeElmt*)child_elem->type;

                    log_debug("  Element name: '%.*s'",
                             (int)child_type_elmt->name.length, child_type_elmt->name.str);

                    // Return the html element (skip #doctype, comments, etc.)
                    if (strview_equal(&child_type_elmt->name, "html")) {
                        log_debug("Found html element inside #document");
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
    else {
        log_debug("Unexpected root type_id=%d in input", root_type);
    }
    return nullptr;
}

/**
 * Parse viewport meta tag content string
 * Format: "width=device-width, initial-scale=1.0, maximum-scale=2.0, ..."
 * @param content The content attribute value from <meta name="viewport">
 * @param doc The DomDocument to store parsed values in
 */
void parse_viewport_content(const char* content, DomDocument* doc) {
    if (!content || !doc) return;

    log_debug("[viewport] Parsing viewport content: '%s'", content);

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

            log_debug("[viewport] Key='%s' Value='%s'", key, value);

            if (str_ieq_const(key, strlen(key), "initial-scale")) {
                doc->viewport_initial_scale = (float)str_to_double_default(value, strlen(value), 0.0);
                log_info("[viewport] initial-scale=%.2f", doc->viewport_initial_scale);
            }
            else if (str_ieq_const(key, strlen(key), "minimum-scale")) {
                doc->viewport_min_scale = (float)str_to_double_default(value, strlen(value), 0.0);
                log_debug("[viewport] minimum-scale=%.2f", doc->viewport_min_scale);
            }
            else if (str_ieq_const(key, strlen(key), "maximum-scale")) {
                doc->viewport_max_scale = (float)str_to_double_default(value, strlen(value), 0.0);
                log_debug("[viewport] maximum-scale=%.2f", doc->viewport_max_scale);
            }
            else if (str_ieq_const(key, strlen(key), "width")) {
                if (str_ieq_const(value, strlen(value), "device-width")) {
                    doc->viewport_width = 0;  // 0 means device-width
                    log_debug("[viewport] width=device-width");
                } else {
                    doc->viewport_width = (int)str_to_int64_default(value, strlen(value), 0);
                    log_debug("[viewport] width=%d", doc->viewport_width);
                }
            }
            else if (str_ieq_const(key, strlen(key), "height")) {
                if (str_ieq_const(value, strlen(value), "device-height")) {
                    doc->viewport_height = 0;  // 0 means device-height
                    log_debug("[viewport] height=device-height");
                } else {
                    doc->viewport_height = (int)str_to_int64_default(value, strlen(value), 0);
                    log_debug("[viewport] height=%d", doc->viewport_height);
                }
            }
        }
    }
}

/**
 * Recursively find and parse <meta name="viewport"> tag from HTML tree
 * @param elem The current element to search (start with html_root)
 * @param doc The DomDocument to store parsed viewport values
 */
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

/**
 * Extract <base href="..."> from the HTML document and return the URL
 * This is used to properly resolve relative URLs when loading remote HTML
 * @param elem The root element to search
 * @return The base href URL or nullptr if not found
 */
const char* extract_base_href(Element* elem) {
    if (!elem) return nullptr;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    // Check if this is a <base> element with href attribute
    if (str_ieq_const(type->name.str, strlen(type->name.str), "base")) {
        const char* href = extract_element_attribute(elem, "href", nullptr);
        if (href && strlen(href) > 0) {
            log_debug("[base] Found <base href=\"%s\">", href);
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

/**
 * Extract scale value from a CSS transform declaration
 * Parses transform functions like scale(0.9), scale(0.9, 0.9), scaleX(0.9), scaleY(0.9)
 * Returns the uniform scale factor if found, 1.0 otherwise
 */
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

/**
 * Find body element in DOM tree and extract transform scale from its CSS
 * @param root The root DomElement to search from
 * @param doc The DomDocument to store the body_transform_scale in
 */
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
        log_debug("[transform] Body element not found");
        return;
    }

    // Get transform property from body's specified styles
    CssDeclaration* transform_decl = dom_element_get_specified_value(body_elem, CSS_PROPERTY_TRANSFORM);
    if (transform_decl) {
        float scale = extract_transform_scale(transform_decl);
        if (scale != 1.0f) {
            doc->body_transform_scale = scale;
            log_info("[transform] Body transform scale=%.3f", scale);
        }
    }
}

/**
 * Resolve @import rules within a stylesheet: load and parse imported CSS files,
 * add them to the stylesheet list. This handles CSS like @import url('font-awesome.css');
 */
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

    // Get directory of the stylesheet for resolving relative import URLs
    char dir_buf[1024];
    const char* import_base_dir = nullptr;
    if (stylesheet_path) {
        const char* last_slash = strrchr(stylesheet_path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - stylesheet_path + 1;
            if (dir_len < sizeof(dir_buf)) {
                memcpy(dir_buf, stylesheet_path, dir_len);
                dir_buf[dir_len] = '\0';
                import_base_dir = dir_buf;
            }
        }
    }

    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        CssRule* rule = stylesheet->rules[i];
        if (!rule || rule->type != CSS_RULE_IMPORT) continue;

        const char* import_url = rule->data.import_rule.url;
        if (!import_url || import_url[0] == '\0') continue;

        log_debug("[CSS @import] Processing: @import '%s' (base: %s)", import_url,
                  import_base_dir ? import_base_dir : "(none)");

        // Resolve import path relative to the stylesheet or document URL.
        char import_path[1024];
        import_path[0] = '\0';
        if (strstr(import_url, "://") != nullptr) {
            Url* import_abs_url = url_parse(import_url);
            if (import_abs_url && import_abs_url->scheme == URL_SCHEME_FILE) {
                char* local_path = url_to_local_path(import_abs_url);
                if (local_path) {
                    strncpy(import_path, local_path, sizeof(import_path) - 1);
                    import_path[sizeof(import_path) - 1] = '\0';
                    mem_free(local_path);
                }
                if (import_path[0] == '\0') {
                    const char* import_href = url_get_href(import_abs_url);
                    css_file_url_to_local_path(import_href, import_path, sizeof(import_path));
                }
            }
            if (import_path[0] == '\0') {
                if (!css_file_url_to_local_path(import_url, import_path, sizeof(import_path))) {
                    strncpy(import_path, import_url, sizeof(import_path) - 1);
                    import_path[sizeof(import_path) - 1] = '\0';
                }
            }
            if (import_abs_url) url_destroy(import_abs_url);
        } else if (stylesheet_path &&
                   (strncmp(stylesheet_path, "file:", 5) == 0 ||
                    strncmp(stylesheet_path, "http://", 7) == 0 ||
                    strncmp(stylesheet_path, "https://", 8) == 0)) {
            Url* base_url = url_parse(stylesheet_path);
            Url* resolved_url = base_url ? url_parse_with_base(import_url, base_url) : nullptr;
            if (resolved_url && resolved_url->scheme == URL_SCHEME_FILE) {
                char* local_path = url_to_local_path(resolved_url);
                if (local_path) {
                    strncpy(import_path, local_path, sizeof(import_path) - 1);
                    import_path[sizeof(import_path) - 1] = '\0';
                    mem_free(local_path);
                }
                if (import_path[0] == '\0') {
                    const char* resolved_href = url_get_href(resolved_url);
                    css_file_url_to_local_path(resolved_href, import_path, sizeof(import_path));
                }
            } else if (resolved_url) {
                const char* resolved_href = url_get_href(resolved_url);
                if (resolved_href) {
                    if (!css_file_url_to_local_path(resolved_href, import_path, sizeof(import_path))) {
                        strncpy(import_path, resolved_href, sizeof(import_path) - 1);
                        import_path[sizeof(import_path) - 1] = '\0';
                    }
                }
            }
            if (resolved_url) url_destroy(resolved_url);
            if (base_url) url_destroy(base_url);
        } else if (import_base_dir) {
            snprintf(import_path, sizeof(import_path), "%s%s", import_base_dir, import_url);
        } else {
            strncpy(import_path, import_url, sizeof(import_path) - 1);
            import_path[sizeof(import_path) - 1] = '\0';
        }

        // Load and parse the imported CSS file
        char* css_content = nullptr;
        bool is_http = (strncmp(import_path, "http://", 7) == 0 || strncmp(import_path, "https://", 8) == 0);
        if (is_http) {
            size_t content_size = 0;
            css_content = download_http_content_cached(import_path, &content_size, "./temp/cache");
        } else {
            css_content = read_text_file(import_path);
        }

        if (!css_content) {
            log_warn("[CSS @import] Failed to load imported stylesheet: %s", import_path);
            continue;
        }

        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (!css_pool_copy) {
            mem_free(css_content);
            continue;
        }
        str_copy(css_pool_copy, css_len + 1, css_content, css_len);
        mem_free(css_content);

        CssStylesheet* imported = parse_and_collect_stylesheet(
            engine, css_pool_copy, import_path, import_path, pool,
            stylesheets, count, capacity, depth + 1);
        if (imported && imported->rule_count > 0) {
            log_debug("[CSS @import] Parsed imported stylesheet '%s': %zu rules", import_path, imported->rule_count);
        } else {
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
            log_debug("[CSS charset] Ignoring UTF-16-pattern @charset prelude (%zu bytes)", end);
            return end;
        }
    }
    return 0;
}

/**
 * Detect the encoding of a CSS file per CSS Syntax §3.2.
 * Resolution order: BOM → HTTP charset → link charset → @charset → document fallback → UTF-8.
 * Returns the charset name or nullptr if UTF-8.
 */
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
            log_debug("[CSS charset] HTTP charset '%s'", http_charset);
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
                        log_debug("[CSS charset] @charset declares '%s'", cs_buf);
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
            log_debug("[CSS charset] <link charset='%s'>", link_charset);
            return link_charset;
        }
    }

    // 5. Fallback to referring document's encoding
    if (document_charset) {
        log_debug("[CSS charset] Falling back to document charset '%s'", document_charset);
        return document_charset;
    }

    // 6. Default to UTF-8
    return nullptr;
}

/**
 * Sanitize UTF-8 content per CSS Syntax §3.3: replace invalid byte sequences with U+FFFD.
 * Returns newly allocated sanitized content, or nullptr if no changes needed.
 * Caller must free with mem_free().
 */
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
    log_debug("[CSS charset] Sanitized UTF-8: replaced invalid bytes (%zu → %zu bytes)", len, o);
    return out;
}

/**
 * Convert CSS file content from detected encoding to UTF-8.
 * Returns newly allocated UTF-8 content, or nullptr if no conversion needed.
 * Caller must free with mem_free().
 */
static char* convert_css_to_utf8(const char* data, size_t len, const char* css_charset) {
    if (!data || !css_charset) return nullptr;
    return convert_charset_to_utf8(data, len, css_charset);
}

/**
 * Resolve a possibly-relative href against a base URL/path. Returns mem_alloc'd
 * absolute URL string (caller mem_free's), or nullptr if not HTTP/HTTPS.
 */
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

/**
 * Recursively collect HTTP(S) URLs from <link rel="stylesheet" href="...">
 * and <script src="..."> elements. Appends absolute URL strings (mem_alloc'd)
 * to *out_urls (realloc'd) and increments *out_count.
 */
static void collect_external_resource_urls(Element* elem, const char* base_path,
                                            char*** out_urls, int* out_count, int* out_capacity,
                                            int depth) {
    if (!elem || depth > MAX_CSS_TREE_DEPTH) return;
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

/**
 * Pre-fetch all external HTTP(S) sub-resources (CSS links, script sources)
 * referenced by the document in parallel. Populates the on-disk cache so the
 * subsequent serial loaders find them already cached. No-op for local docs.
 */
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

/**
 * Recursively collect <link rel="stylesheet"> references from HTML
 * Loads and parses external CSS files
 */
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                CssStylesheet*** stylesheets, int* count, int* capacity,
                                int depth, bool recurse) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <link> element
    if (str_ieq_const(type->name.str, strlen(type->name.str), "link")) {
        // Extract 'rel' and 'href' attributes
        const char* rel = extract_element_attribute(elem, "rel", nullptr);
        const char* href = extract_element_attribute(elem, "href", nullptr);

        if (rel && href && str_ieq_const(rel, strlen(rel), "stylesheet")) {
            // Extract optional charset attribute from <link>
            const char* link_charset = extract_element_attribute(elem, "charset", nullptr);

            // Check media attribute - skip stylesheets that don't apply to screen
            const char* media = extract_element_attribute(elem, "media", nullptr);
            if (media && !css_evaluate_media_query(engine, media)) {
                log_debug("[CSS] Skipping <link> stylesheet '%s' - media '%s' does not match screen", href, media);
            } else {
            log_debug("[CSS] Found <link rel='stylesheet' href='%s'>", href);

            // Resolve relative path using base_path (supports file:// and http:// URLs)
            char css_path[1024];
            bool is_http_css = false;

            if (href[0] == '/' && href[1] != '/'
                && (!base_path || (strncmp(base_path, "http://", 7) != 0 && strncmp(base_path, "https://", 8) != 0))) {
                // WPT-style absolute support URLs (for example /fonts/ahem.css)
                // are rooted at the test server, not the local filesystem root.
                if (!resolve_layout_support_resource_path(href, base_path, css_path, sizeof(css_path))) {
                    log_debug("[CSS] Support resource fallback miss: href=%s base=%s",
                              href, base_path ? base_path : "(none)");
                    // Absolute local path - use as-is (only when base is not HTTP)
                    strncpy(css_path, href, sizeof(css_path) - 1);
                    css_path[sizeof(css_path) - 1] = '\0';
                }
            } else if (strstr(href, "://") != nullptr) {
                // Full URL - use as-is
                strncpy(css_path, href, sizeof(css_path) - 1);
                css_path[sizeof(css_path) - 1] = '\0';
                is_http_css = (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0);
            } else if (base_path) {
                // Relative path - resolve against base_path using URL resolution
                Url* base_url = url_parse(base_path);
                if (base_url && base_url->is_valid) {
                    Url* resolved_url = parse_url(base_url, href);
                    if (resolved_url && resolved_url->is_valid) {
                        if (resolved_url->scheme == URL_SCHEME_HTTP || resolved_url->scheme == URL_SCHEME_HTTPS) {
                            // HTTP URL - use the full href
                            const char* url_str = url_get_href(resolved_url);
                            strncpy(css_path, url_str, sizeof(css_path) - 1);
                            css_path[sizeof(css_path) - 1] = '\0';
                            is_http_css = true;
                        } else {
                            // File URL - convert to local path
                            char* local_path = url_to_local_path(resolved_url);
                            if (local_path) {
                                strncpy(css_path, local_path, sizeof(css_path) - 1);
                                css_path[sizeof(css_path) - 1] = '\0';
                                mem_free(local_path);
                            } else {
                                strncpy(css_path, href, sizeof(css_path) - 1);
                                css_path[sizeof(css_path) - 1] = '\0';
                            }
                        }
                        url_destroy(resolved_url);
                    } else {
                        strncpy(css_path, href, sizeof(css_path) - 1);
                        css_path[sizeof(css_path) - 1] = '\0';
                    }
                    url_destroy(base_url);
                } else {
                    // Fallback: simple string concatenation for non-URL base paths
                    const char* last_slash = strrchr(base_path, '/');
                    if (last_slash) {
                        size_t dir_len = last_slash - base_path + 1;
                        if (dir_len < sizeof(css_path) - strlen(href) - 1) {
                            strncpy(css_path, base_path, dir_len);
                            css_path[dir_len] = '\0';
                            strncat(css_path, href, sizeof(css_path) - dir_len - 1);
                        } else {
                            strncpy(css_path, href, sizeof(css_path) - 1);
                            css_path[sizeof(css_path) - 1] = '\0';
                        }
                    } else {
                        strncpy(css_path, href, sizeof(css_path) - 1);
                        css_path[sizeof(css_path) - 1] = '\0';
                    }
                }
            } else {
                strncpy(css_path, href, sizeof(css_path) - 1);
                css_path[sizeof(css_path) - 1] = '\0';
            }

            if (!is_http_css && access(css_path, R_OK) != 0) {
                char shared_path[1024];
                if (radiant_resolve_shared_data_resource_path(href, base_path,
                                                              shared_path, sizeof(shared_path))) {
                    str_copy(css_path, sizeof(css_path), shared_path, strlen(shared_path));
                }
            }

            log_debug("[CSS] Loading stylesheet from: %s", css_path);

            // Load and parse CSS file (or download from HTTP URL)
            char* css_content = nullptr;
            size_t css_file_size = 0;
            if (is_http_css) {
                // Download CSS from HTTP URL (uses on-disk cache populated by prefetch)
                size_t content_size = 0;
                css_content = download_http_content_cached(css_path, &content_size, "./temp/cache");
                if (css_content) {
                    css_file_size = content_size;
                    log_debug("[CSS] Downloaded stylesheet from URL: %s (%zu bytes)", css_path, content_size);
                }
            } else {
                size_t content_size = 0;
                css_content = read_binary_file(css_path, &content_size);
                if (css_content) {
                    css_file_size = content_size;
                }
            }
            bool css_content_empty = css_content && css_file_size == 0 && css_content[0] == '\0';
            if (css_content_empty) {
                // Empty linked stylesheets are valid CSS; skip them before
                // parser setup so zero-byte resources do not look failed.
                log_debug("[CSS] Skipping empty linked stylesheet: %s", css_path);
                mem_free(css_content);
            } else if (css_content) {

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
                                log_debug("[CSS charset] Found .headers file charset='%s' for %s", http_charset, css_path);
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
                    if (stylesheet && stylesheet->rule_count > 0) {
                        log_debug("[CSS] Parsed linked stylesheet '%s': %zu rules", css_path, stylesheet->rule_count);
                    } else {
                        log_warn("[CSS] Failed to parse stylesheet or empty: %s", css_path);
                    }
                } else {
                    mem_free(css_content);
                    log_error("[CSS] Failed to allocate memory for CSS content");
                }
            } else {
                log_warn("[CSS] Failed to load stylesheet: %s", css_path);
            }
            } // end media check else
        }
    }

    // Recursively process children
    if (recurse) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child_item = elem->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                collect_linked_stylesheets(child_item.element, engine, base_path, pool,
                                           stylesheets, count, capacity, depth + 1, true);
            }
        }
    }
}

static void parse_inline_style_children(Element* elem, CssEngine* engine,
                                        const char* base_path, Pool* pool,
                                        CssStylesheet*** stylesheets, int* count,
                                        int* capacity) {
    bool collect_to_list = stylesheets && count && capacity;
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        int type_id = get_type_id(child_item);
        if (collect_to_list) {
            log_debug("[CSS] <style> child[%lld] type_id=%d", i, type_id);
        }
        if (type_id != LMD_TYPE_STRING) continue;

        String* css_text = (String*)child_item.string_ptr;
        if (!css_text || css_text->len <= 0) continue;
        CssStylesheet* stylesheet = nullptr;
        if (collect_to_list) {
            log_debug("[CSS] Found STRING child: ptr=%p, len=%d",
                      (void*)css_text, css_text->len);
            stylesheet = parse_and_collect_stylesheet(
                engine, css_text->chars, "<inline-style>", base_path,
                pool, stylesheets, count, capacity, 0);
        } else {
            log_debug("[CSS] Found <style> element with %d bytes of CSS", css_text->len);
            stylesheet = css_parse_stylesheet(engine, css_text->chars, "<inline-style>");
            annotate_css_stylesheet_source_file(stylesheet, "<inline-style>");
        }
        if (stylesheet && stylesheet->rule_count > 0) {
            log_debug("[CSS] Parsed inline <style>: %zu rules", stylesheet->rule_count);
        }
    }
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and returns list of stylesheets
 */
static void collect_inline_styles_impl(Element* elem, CssEngine* engine,
                                       const char* base_path, Pool* pool,
                                       CssStylesheet*** stylesheets, int* count,
                                       int* capacity, int depth, bool recurse) {
    if (!elem || !engine || !pool ||
        (stylesheets && (!count || !capacity)) ||
        (!stylesheets && (count || capacity))) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <style> element
    if (str_ieq_const(type->name.str, strlen(type->name.str), "style")) {
        // Check media attribute - skip styles that don't apply to screen
        const char* media = extract_element_attribute(elem, "media", nullptr);
        if (media && !css_evaluate_media_query(engine, media)) {
            log_debug("[CSS] Skipping <style> element - media '%s' does not match screen", media);
        } else {
        parse_inline_style_children(
            elem, engine, base_path, pool, stylesheets, count, capacity);
        } // end media check else
    }

    // Recursively process children
    if (recurse) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child_item = elem->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                collect_inline_styles_impl(child_item.element, engine, base_path, pool,
                                           stylesheets, count, capacity, depth + 1, true);
            }
        }
    }
}

void collect_inline_styles_to_list(Element* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                   CssStylesheet*** stylesheets, int* count, int* capacity,
                                   int depth, bool recurse) {
    collect_inline_styles_impl(elem, engine, base_path, pool,
                               stylesheets, count, capacity, depth, recurse);
}

static void collect_stylesheets_in_document_order(Element* elem, CssEngine* engine,
                                                  const char* base_path, Pool* pool,
                                                  CssStylesheet*** stylesheets,
                                                  int* count, int* capacity,
                                                  int* linked_count,
                                                  int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (type) {
        if (str_ieq_const(type->name.str, strlen(type->name.str), "link")) {
            int before = *count;
            collect_linked_stylesheets(elem, engine, base_path, pool,
                                       stylesheets, count, capacity, depth, false);
            if (linked_count && *count > before) {
                *linked_count += *count - before;
            }
        } else if (str_ieq_const(type->name.str, strlen(type->name.str), "style")) {
            collect_inline_styles_to_list(elem, engine, base_path, pool,
                                          stylesheets, count, capacity, depth, false);
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

/**
 * Recursively collect <style> inline CSS from DomElement* tree (post-JS mutations).
 * Unlike collect_inline_styles_to_list() which walks Element* (pre-JS), this walks
 * the DomElement tree which reflects JS DOM mutations (createElement, appendChild,
 * textContent writes, disabled attribute changes).
 *
 * Handles:
 * - Dynamically-added <style> elements (e.g. first-letter-dynamic-001)
 * - <style disabled> elements — skipped (e.g. table-anonymous-objects-015)
 * - Media query filtering
 */
void collect_inline_styles_from_dom(DomElement* elem, CssEngine* engine, const char* base_path, Pool* pool,
                                    CssStylesheet*** stylesheets, int* count,
                                    int* capacity, int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count || !capacity) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    // Check if this is a <style> element
    if (elem->tag_name && strcasecmp(elem->tag_name, "style") == 0) {
        // Check disabled attribute — skip disabled stylesheets
        if (dom_element_has_attribute(elem, "disabled")) {
            log_debug("[CSS] Skipping <style> element with disabled attribute");
        } else {
            // Check media attribute
            const char* media = dom_element_get_attribute(elem, "media");
            if (media && !css_evaluate_media_query(engine, media)) {
                log_debug("[CSS] Skipping <style> element - media '%s' does not match screen", media);
            } else {
                // Extract text content from DomText children
                DomNode* child = elem->first_child;
                while (child) {
                    if (child->node_type == DOM_NODE_TEXT) {
                        DomText* text_node = lam::dom_require_text(child);
                        if (text_node->text && text_node->length > 0) {
                            CssStylesheet* stylesheet = parse_and_collect_stylesheet(
                                engine, text_node->text, "<inline-style>", base_path,
                                pool, stylesheets, count, capacity, 0);
                            if (stylesheet && stylesheet->rule_count > 0) {
                                log_debug("[CSS] Re-scan: parsed <style> from DOM: %zu rules", stylesheet->rule_count);
                            }
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
    if (doc->js_mutation_record_overflow > 0) return true;

    for (int i = 0; i < doc->js_mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js_mutation_records[i];
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
            default:
                break;
        }
    }
    return false;
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and adds to engine's stylesheet list
 */
void collect_inline_styles(Element* elem, CssEngine* engine, Pool* pool, int depth = 0) {
    collect_inline_styles_impl(elem, engine, nullptr, pool,
                               nullptr, nullptr, nullptr, depth, true);
}

/**
 * Master function to extract and apply all CSS from HTML document
 * Handles linked stylesheets, <style> elements, and inline style attributes
 * Returns array of collected stylesheets
 */
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count, int* linked_count_out) {
    if (!html_root || !engine || !pool || !stylesheet_count) return nullptr;

    log_debug("[CSS] Extracting CSS from HTML document...");

    *stylesheet_count = 0;
    CssStylesheet** stylesheets = nullptr;
    int stylesheet_capacity = 0;

    // Step 0: Pre-fetch external HTTP sub-resources (CSS, scripts) in parallel
    // so subsequent serial loaders find them already cached on disk.
    prefetch_document_subresources(html_root, base_path);

    // CSS Cascade §6.4: stylesheet source order follows document order across
    // both <link rel=stylesheet> and <style>. A later external sheet must win
    // ties against earlier inline rules, and vice versa.
    log_debug("[CSS] Collecting stylesheets in document order...");
    int linked_count = 0;
    collect_stylesheets_in_document_order(html_root, engine, base_path, pool,
                                          &stylesheets, stylesheet_count,
                                          &stylesheet_capacity, &linked_count, 0);
    if (linked_count_out) *linked_count_out = linked_count;

    int inline_count = *stylesheet_count - linked_count;

    log_debug("[CSS] Collected %d stylesheet(s) from HTML (%d linked, %d inline)",
              *stylesheet_count, linked_count, inline_count);
    return stylesheets;
}

/**
 * Extract inline CSS from <style> tags in the HTML document
 * Returns concatenated CSS text or nullptr if none found
 * DEPRECATED: Use extract_and_apply_all_css instead
 */
const char* extract_inline_css(Element* root) {
    if (!root) return nullptr;

    // TODO: Implement style tag extraction
    // For now, return nullptr - external CSS via -c flag will work
    return nullptr;
}

/**
 * Apply a single CSS rule to a DOM element (and recursively to its children)
 * Handles style rules directly, and recursively processes media rules
 */

// ============================================================================
// Selector Index for O(1) Rule Lookup
// ============================================================================

// Maximum classes per selector for fixed array
#define MAX_SELECTOR_CLASSES 16

// A rule entry in the index
struct IndexedRule {
    CssRule* rule;
    CssSelector* selector;  // The specific selector that matched the index key
    int rule_index;         // Original position for source order
};

// Rule list entry for hashmap: key string + ArrayList of IndexedRule
struct RuleListEntry {
    const char* key;        // hash key (tag/class/id name)
    ArrayList* rules;       // ArrayList of IndexedRule*
};

static uint64_t rule_list_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RuleListEntry* entry = (const RuleListEntry*)item;
    if (!entry || !entry->key) return 0;
    return hashmap_xxhash3(entry->key, strlen(entry->key), seed0, seed1);
}

static int rule_list_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const RuleListEntry* ea = (const RuleListEntry*)a;
    const RuleListEntry* eb = (const RuleListEntry*)b;
    if (!ea->key && !eb->key) return 0;
    if (!ea->key) return -1;
    if (!eb->key) return 1;
    return strcmp(ea->key, eb->key);
}

// Hash function for CssRule pointers (for seen-set)
static uint64_t rule_ptr_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const CssRule* ptr = *(const CssRule**)item;
    return hashmap_xxhash3(&ptr, sizeof(ptr), seed0, seed1);
}

static int rule_ptr_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const CssRule* pa = *(const CssRule**)a;
    const CssRule* pb = *(const CssRule**)b;
    if (pa == pb) return 0;
    return (pa < pb) ? -1 : 1;
}

// Selector index for fast rule lookup
struct SelectorIndex {
    HashMap* by_tag;        // RuleListEntry: rules indexed by tag name
    HashMap* by_class;      // RuleListEntry: rules indexed by class
    HashMap* by_id;         // RuleListEntry: rules indexed by id
    ArrayList* universal;   // IndexedRule*: rules that match any element (*, etc.)
    ArrayList* media_rules; // CssRule*: @media rules requiring runtime evaluation
    Pool* pool;
};

// Helper to add a rule to a hashmap bucket
static void add_rule_to_map(HashMap* map, const char* key, IndexedRule* entry, Pool* pool) {
    RuleListEntry search = { .key = key, .rules = nullptr };
    RuleListEntry* existing = (RuleListEntry*)hashmap_get(map, &search);

    if (existing) {
        // Add to existing list
        arraylist_append(existing->rules, entry);
    } else {
        // Create new list
        ArrayList* list = arraylist_new(8);
        arraylist_append(list, entry);
        RuleListEntry new_entry = { .key = key, .rules = list };
        hashmap_set(map, &new_entry);
    }
}

// Helper to get rules from a hashmap bucket
static ArrayList* get_rules_from_map(HashMap* map, const char* key) {
    if (!map || !key) return nullptr;
    RuleListEntry search = { .key = key, .rules = nullptr };
    RuleListEntry* entry = (RuleListEntry*)hashmap_get(map, &search);
    return entry ? entry->rules : nullptr;
}

// Get the key selector (rightmost compound selector) from a selector
static CssCompoundSelector* get_key_selector(CssSelector* selector) {
    if (!selector || selector->compound_selector_count == 0) return nullptr;
    return selector->compound_selectors[selector->compound_selector_count - 1];
}

// Extract indexing keys from a compound selector (using fixed array for classes)
static void extract_index_keys(CssCompoundSelector* compound,
                               const char** out_tag,
                               const char** out_classes,
                               int* out_class_count,
                               const char** out_id) {
    *out_tag = nullptr;
    *out_id = nullptr;
    *out_class_count = 0;

    if (!compound) return;

    for (size_t i = 0; i < compound->simple_selector_count; i++) {
        CssSimpleSelector* simple = compound->simple_selectors[i];
        if (!simple) continue;

        switch (simple->type) {
            case CSS_SELECTOR_TYPE_ELEMENT:
                if (simple->value && strcmp(simple->value, "*") != 0) {
                    *out_tag = simple->value;
                }
                break;
            case CSS_SELECTOR_TYPE_CLASS:
                if (simple->value && *out_class_count < MAX_SELECTOR_CLASSES) {
                    out_classes[(*out_class_count)++] = simple->value;
                }
                break;
            case CSS_SELECTOR_TYPE_ID:
                if (simple->value) {
                    *out_id = simple->value;
                }
                break;
            default:
                break;
        }
    }
}

// Build selector index from a stylesheet
static SelectorIndex* build_selector_index(CssStylesheet* stylesheet, Pool* pool) {
    SelectorIndex* index = (SelectorIndex*)mem_calloc(1, sizeof(SelectorIndex), MEM_CAT_LAYOUT);
    if (!index) return nullptr;

    index->pool = pool;
    index->by_tag = hashmap_new(sizeof(RuleListEntry), 64, 0, 0, rule_list_hash, rule_list_compare, nullptr, nullptr);
    index->by_class = hashmap_new(sizeof(RuleListEntry), 64, 0, 0, rule_list_hash, rule_list_compare, nullptr, nullptr);
    index->by_id = hashmap_new(sizeof(RuleListEntry), 32, 0, 0, rule_list_hash, rule_list_compare, nullptr, nullptr);
    index->universal = arraylist_new(16);
    index->media_rules = arraylist_new(8);

    for (size_t rule_idx = 0; rule_idx < stylesheet->rule_count; rule_idx++) {
        CssRule* rule = stylesheet->rules[rule_idx];
        if (!rule) continue;

        // Keep media rules in the same source-ordered candidate stream as
        // normal rules. CSS cascade order is based on the @media rule's
        // position in the stylesheet; applying media rules after all normal
        // candidates reverses equal-specificity ties.
        if (rule->type == CSS_RULE_MEDIA) {
            IndexedRule* entry = (IndexedRule*)pool_calloc(pool, sizeof(IndexedRule));
            if (entry) {
                entry->rule = rule;
                entry->selector = nullptr;
                entry->rule_index = (int)rule_idx;
                arraylist_append(index->universal, entry);
            }
            continue;
        }

        if (rule->type != CSS_RULE_STYLE) continue;

        // Handle selector groups (comma-separated)
        CssSelectorGroup* group = rule->data.style_rule.selector_group;
        CssSelector* single = rule->data.style_rule.selector;

        // Collect selectors to index
        // CSS reset stylesheets often have 80+ comma-separated selectors
        CssSelector* selectors_buf[128];
        CssSelector** selectors_to_index = selectors_buf;
        int selector_count = 0;
        int selector_capacity = 128;

        if (group && group->selector_count > 0) {
            if (group->selector_count > 128) {
                selector_capacity = (int)group->selector_count;
                selectors_to_index = (CssSelector**)pool_calloc(pool, selector_capacity * sizeof(CssSelector*));
                if (!selectors_to_index) {
                    selectors_to_index = selectors_buf;
                    selector_capacity = 128;
                }
            }
            for (size_t i = 0; i < group->selector_count && selector_count < selector_capacity; i++) {
                if (group->selectors[i]) {
                    selectors_to_index[selector_count++] = group->selectors[i];
                }
            }
        } else if (single) {
            selectors_to_index[selector_count++] = single;
        }

        for (int si = 0; si < selector_count; si++) {
            CssSelector* selector = selectors_to_index[si];
            CssCompoundSelector* key_compound = get_key_selector(selector);
            if (!key_compound) continue;

            const char* tag = nullptr;
            const char* id = nullptr;
            const char* classes[MAX_SELECTOR_CLASSES];
            int class_count = 0;
            extract_index_keys(key_compound, &tag, classes, &class_count, &id);

            // Allocate IndexedRule in pool
            IndexedRule* entry = (IndexedRule*)pool_calloc(pool, sizeof(IndexedRule));
            entry->rule = rule;
            entry->selector = selector;
            entry->rule_index = (int)rule_idx;

            // Index by most specific key first (ID > class > tag)
            bool indexed = false;

            if (id) {
                add_rule_to_map(index->by_id, id, entry, pool);
                indexed = true;
            }

            if (class_count > 0) {
                // Index by first class (could index by all, but one is usually enough)
                add_rule_to_map(index->by_class, classes[0], entry, pool);
                indexed = true;
            }

            if (tag) {
                add_rule_to_map(index->by_tag, tag, entry, pool);
                indexed = true;
            }

            // If no specific key, it's a universal rule
            if (!indexed) {
                arraylist_append(index->universal, entry);
            }
        }
    }

    return index;
}

// Callback for freeing rule lists in hashmap
static bool free_rule_list_cb(const void* item, void* udata) {
    (void)udata;
    const RuleListEntry* entry = (const RuleListEntry*)item;
    if (entry && entry->rules) {
        arraylist_free(entry->rules);
    }
    return true;
}

// Free selector index
static void free_selector_index(SelectorIndex* index) {
    if (!index) return;

    if (index->by_tag) {
        hashmap_scan(index->by_tag, free_rule_list_cb, nullptr);
        hashmap_free(index->by_tag);
    }
    if (index->by_class) {
        hashmap_scan(index->by_class, free_rule_list_cb, nullptr);
        hashmap_free(index->by_class);
    }
    if (index->by_id) {
        hashmap_scan(index->by_id, free_rule_list_cb, nullptr);
        hashmap_free(index->by_id);
    }
    if (index->universal) arraylist_free(index->universal);
    if (index->media_rules) arraylist_free(index->media_rules);

    mem_free(index);
}

// Comparison function for qsort on IndexedRule pointers (by rule_index)
static int compare_indexed_rules(const void* a, const void* b) {
    const IndexedRule* ra = *(const IndexedRule**)a;
    const IndexedRule* rb = *(const IndexedRule**)b;
    return ra->rule_index - rb->rule_index;
}

// Get candidate rules for an element from the index
static void get_candidate_rules(SelectorIndex* index, DomElement* elem,
                                ArrayList* candidates) {
    // Clear candidates
    candidates->length = 0;

    // Use hashmap for seen rules (deduplication)
    HashMap* seen = hashmap_new(sizeof(CssRule*), 64, 0, 0, rule_ptr_hash, rule_ptr_compare, nullptr, nullptr);

    // Add universal rules
    for (int i = 0; i < index->universal->length; i++) {
        IndexedRule* entry = (IndexedRule*)index->universal->data[i];
        if (!hashmap_get(seen, &entry->rule)) {
            arraylist_append(candidates, entry);
            hashmap_set(seen, &entry->rule);
        }
    }

    // Add rules by tag
    if (elem->tag_name) {
        ArrayList* tag_rules = get_rules_from_map(index->by_tag, elem->tag_name);
        if (tag_rules) {
            for (int i = 0; i < tag_rules->length; i++) {
                IndexedRule* entry = (IndexedRule*)tag_rules->data[i];
                if (!hashmap_get(seen, &entry->rule)) {
                    arraylist_append(candidates, entry);
                    hashmap_set(seen, &entry->rule);
                }
            }
        }
    }

    // Add rules by ID
    if (elem->id) {
        ArrayList* id_rules = get_rules_from_map(index->by_id, elem->id);
        if (id_rules) {
            for (int i = 0; i < id_rules->length; i++) {
                IndexedRule* entry = (IndexedRule*)id_rules->data[i];
                if (!hashmap_get(seen, &entry->rule)) {
                    arraylist_append(candidates, entry);
                    hashmap_set(seen, &entry->rule);
                }
            }
        }
    }

    // Add rules by class
    if (elem->class_count > 0 && elem->class_names) {
        for (int i = 0; i < elem->class_count; i++) {
            if (elem->class_names[i]) {
                ArrayList* class_rules = get_rules_from_map(index->by_class, elem->class_names[i]);
                if (class_rules) {
                    for (int j = 0; j < class_rules->length; j++) {
                        IndexedRule* entry = (IndexedRule*)class_rules->data[j];
                        if (!hashmap_get(seen, &entry->rule)) {
                            arraylist_append(candidates, entry);
                            hashmap_set(seen, &entry->rule);
                        }
                    }
                }
            }
        }
    }

    hashmap_free(seen);

    // Sort by rule_index to maintain source order
    if (candidates->length > 1) {
        qsort(candidates->data, candidates->length, sizeof(void*), compare_indexed_rules);
    }
}

// ============================================================================
// Timing accumulators for CSS cascade analysis
// ============================================================================

static thread_local int64_t g_selector_match_count = 0;
static thread_local int64_t g_selector_match_success = 0;
static thread_local int64_t g_property_apply_count = 0;
static thread_local int64_t g_element_count = 0;
static thread_local int64_t g_candidate_rule_count = 0;

static void reset_cascade_timing() {
    g_selector_match_count = 0;
    g_selector_match_success = 0;
    g_property_apply_count = 0;
    g_element_count = 0;
    g_candidate_rule_count = 0;
}

static void log_cascade_timing_summary() {
    log_info("[TIMING] cascade: elements: %lld, candidates: %lld, selectors: %lld matches (%lld hits, %.1f%%), properties: %lld",
        g_element_count, g_candidate_rule_count, g_selector_match_count, g_selector_match_success,
        g_selector_match_count > 0 ? (100.0 * g_selector_match_success / g_selector_match_count) : 0.0,
        g_property_apply_count);
}

static void apply_rule_to_dom_element(DomElement* elem, CssRule* rule, SelectorMatcher* matcher, Pool* pool, CssEngine* engine) {
    if (!elem || !rule || !matcher || !pool) return;

    // Handle media rules by evaluating the condition
    if (rule->type == CSS_RULE_MEDIA) {
        const char* media_condition = rule->data.conditional_rule.condition;
#ifdef RADIANT_TRACE_MEDIA_QUERY
        // Media query evaluation happens for every candidate rule/element pair;
        // keep per-element trace opt-in so large tables do not stall on logging.
        log_debug("[MediaQuery] Evaluating condition: '%s' for element <%s>",
                  media_condition ? media_condition : "(null)", elem->tag_name ? elem->tag_name : "?");
#endif
        bool matches = css_evaluate_media_query(engine, media_condition);
#ifdef RADIANT_TRACE_MEDIA_QUERY
        log_debug("[MediaQuery] Result: %s", matches ? "MATCHES" : "does not match");
#endif
        if (matches) {
            for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
                CssRule* nested_rule = rule->data.conditional_rule.rules[i];
                if (nested_rule) {
                    apply_rule_to_dom_element(elem, nested_rule, matcher, pool, engine);
                }
            }
        }
        return;
    }

    // Handle @supports rules - skip for now
    if (rule->type == CSS_RULE_SUPPORTS) return;

    // Skip other at-rules (@import, @charset, @namespace, etc.)
    if (rule->type != CSS_RULE_STYLE) return;

    // Process style rule
    CssSelector* selector = rule->data.style_rule.selector;
    CssSelectorGroup* selector_group = rule->data.style_rule.selector_group;

    // Handle selector groups (comma-separated selectors like "th, td")
    if (selector_group && selector_group->selector_count > 0) {
        // Try matching each selector in the group.
        // For pseudo-element selectors (e.g., ".scope::before, .scope::after"),
        // each matching selector may target a different pseudo-element, so we
        // must apply the rule for each match individually rather than breaking
        // on the first match.
        bool any_non_pseudo_match = false;
        CssSpecificity best_specificity = {0, 0, 0, 0, false};

        for (size_t sel_idx = 0; sel_idx < selector_group->selector_count; sel_idx++) {
            CssSelector* group_sel = selector_group->selectors[sel_idx];
            if (!group_sel) continue;

            // Calculate and cache specificity if not already done
            if (group_sel->specificity.inline_style == 0 &&
                group_sel->specificity.ids == 0 &&
                group_sel->specificity.classes == 0 &&
                group_sel->specificity.elements == 0) {
                group_sel->specificity = selector_matcher_calculate_specificity(matcher, group_sel);
            }

            MatchResult match_result;
            bool matched = selector_matcher_matches(matcher, group_sel, elem, &match_result);
            g_selector_match_count++;

            if (matched) {
                g_selector_match_success++;
                if (match_result.pseudo_element != PSEUDO_ELEMENT_NONE) {
                    // Apply pseudo-element rule immediately — different selectors
                    // in the group may target different pseudo-elements
                    if (rule->data.style_rule.declaration_count > 0) {
                        dom_element_apply_pseudo_element_rule(elem, rule,
                            match_result.specificity, (int)match_result.pseudo_element);
                        g_property_apply_count++;
                    }
                } else {
                    // For non-pseudo matches, track best specificity and apply once
                    if (!any_non_pseudo_match) {
                        best_specificity = match_result.specificity;
                    }
                    any_non_pseudo_match = true;
                }
            }
        }

        if (any_non_pseudo_match && rule->data.style_rule.declaration_count > 0) {
            dom_element_apply_rule(elem, rule, best_specificity);
            g_property_apply_count++;
        }
        return;
    }

    // Handle single selector
    if (!selector) return;

    // Calculate and cache specificity
    if (selector->specificity.inline_style == 0 &&
        selector->specificity.ids == 0 &&
        selector->specificity.classes == 0 &&
        selector->specificity.elements == 0) {
        selector->specificity = selector_matcher_calculate_specificity(matcher, selector);
    }

    // Check if selector matches
    MatchResult match_result;
    bool matched = selector_matcher_matches(matcher, selector, elem, &match_result);
    g_selector_match_count++;

    // DEBUG: Log .fa class selector matching on <i> elements
    bool is_i_element = (elem->tag_name && strcmp(elem->tag_name, "i") == 0 && elem->class_count > 0);
    if (is_i_element) {
        bool is_fa_selector = false;
        // Check if this is a simple .fa class selector (single compound selector with one simple selector)
        if (selector->compound_selector_count == 1 && selector->compound_selectors[0] &&
            selector->compound_selectors[0]->simple_selector_count == 1) {
            CssSimpleSelector* simple = selector->compound_selectors[0]->simple_selectors[0];
            if (simple && simple->type == CSS_SELECTOR_TYPE_CLASS && simple->value &&
                strcmp(simple->value, "fa") == 0) {
                is_fa_selector = true;
            }
        }
        // Log .fa selector matching attempts
        if (is_fa_selector) {
            log_debug("[FA MATCH] Selector '.fa' vs <i class='%s %s'> -> matched=%d",
                      elem->class_names && elem->class_count > 0 ? elem->class_names[0] : "none",
                      elem->class_names && elem->class_count > 1 ? elem->class_names[1] : "",
                      matched);
            if (matched && rule->data.style_rule.declaration_count > 0) {
                log_debug("[FA MATCH] Applying %d declarations from .fa rule",
                          rule->data.style_rule.declaration_count);
            }
        }
        // Also log ALL selectors tested on <i> elements with "fa" class
        if (elem->class_names && elem->class_count > 0 && strcmp(elem->class_names[0], "fa") == 0) {
            // Log first simple selector if available
            if (selector->compound_selector_count > 0 && selector->compound_selectors[0] &&
                selector->compound_selectors[0]->simple_selector_count > 0) {
                CssSimpleSelector* simple = selector->compound_selectors[0]->simple_selectors[0];
                if (simple) {
                    const char* type_names[] = {"ELEMENT", "CLASS", "ID", "UNIVERSAL", "ATTR", "..."};
                    int type_idx = (simple->type <= CSS_SELECTOR_TYPE_UNIVERSAL) ? simple->type : 4;
                    log_debug("[I SELECTOR CHECK] Testing selector type=%s value='%s' on <i class='fa ...'> -> matched=%d",
                              type_names[type_idx], simple->value ? simple->value : "(null)", matched);
                }
            }
        }
    }

    if (matched) {
        g_selector_match_success++;
        if (rule->data.style_rule.declaration_count > 0) {
            if (match_result.pseudo_element != PSEUDO_ELEMENT_NONE) {
                dom_element_apply_pseudo_element_rule(elem, rule, match_result.specificity,
                                                      (int)match_result.pseudo_element);
            } else {
                dom_element_apply_rule(elem, rule, match_result.specificity);
            }
            g_property_apply_count++;
        }
    }
}

/**
 * Apply CSS stylesheet rules to DOM tree (original O(n×m) version)
 * Walks the tree recursively and matches selectors to elements
 */
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine, int depth) {
    if (!root || !stylesheet || !matcher || !pool) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    g_element_count++;

    // Iterate through all rules in the stylesheet
    for (int rule_idx = 0; rule_idx < (int)stylesheet->rule_count; rule_idx++) {
        CssRule* rule = stylesheet->rules[rule_idx];
        if (!rule) continue;
        apply_rule_to_dom_element(root, rule, matcher, pool, engine);
    }

    // Recursively apply to children (only element children)
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require_element(child);
            apply_stylesheet_to_dom_tree(child_elem, stylesheet, matcher, pool, engine, depth + 1);
        }
        child = child->next_sibling;
    }
}

/**
 * Apply CSS stylesheet rules to DOM tree using selector index
 * This is an optimized O(n) version that uses pre-built index
 */
static void apply_stylesheet_to_dom_tree_indexed(DomElement* root, SelectorIndex* index,
                                                  SelectorMatcher* matcher, Pool* pool, CssEngine* engine, int depth) {
    if (!root || !index || !matcher || !pool) return;
    if (depth > MAX_CSS_TREE_DEPTH) return;

    g_element_count++;

    // Get candidate rules for this element from the index
    ArrayList* candidates = arraylist_new(32);
    get_candidate_rules(index, root, candidates);
    g_candidate_rule_count += candidates->length;

    // Only check candidate rules (much fewer than all rules)
    for (int i = 0; i < candidates->length; i++) {
        IndexedRule* entry = (IndexedRule*)candidates->data[i];
        apply_rule_to_dom_element(root, entry->rule, matcher, pool, engine);
    }
    arraylist_free(candidates);

    // Recursively apply to children (only element children)
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require_element(child);
            apply_stylesheet_to_dom_tree_indexed(child_elem, index, matcher, pool, engine, depth + 1);
        }
        child = child->next_sibling;
    }
}

/**
 * Apply CSS stylesheet to DOM tree with index optimization
 * Builds index first, then applies using indexed lookup
 */
void apply_stylesheet_to_dom_tree_fast(DomElement* root, CssStylesheet* stylesheet,
                                        SelectorMatcher* matcher, Pool* pool, CssEngine* engine) {
    if (!root || !stylesheet || !matcher || !pool) return;

    // Build selector index
    SelectorIndex* index = build_selector_index(stylesheet, pool);

    // Apply using index
    apply_stylesheet_to_dom_tree_indexed(root, index, matcher, pool, engine, 0);

    // Free index
    free_selector_index(index);
}

static bool load_scripts_before_cascade_legacy() {
    const char* env = getenv("RADIANT_SCRIPT_BEFORE_CASCADE");
    return env && env[0] && env[0] != '0';
}

static void clear_load_stylesheet_cascade_recursive(DomNode* node) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        bool changed = false;
        if (elem->specified_style &&
            style_tree_remove_non_inline_declarations(elem->specified_style)) {
            changed = true;
        }
        if (elem->before_styles) { style_tree_clear(elem->before_styles); changed = true; }
        if (elem->after_styles) { style_tree_clear(elem->after_styles); changed = true; }
        if (elem->first_letter_styles) { style_tree_clear(elem->first_letter_styles); changed = true; }
        if (elem->marker_styles) { style_tree_clear(elem->marker_styles); changed = true; }
        if (elem->placeholder_styles) { style_tree_clear(elem->placeholder_styles); changed = true; }
        if (changed) {
            // Load-time scripts now run after an initial cascade; the recascade
            // must drop old selector matches without erasing JS inline styles.
            elem->style_version++;
            elem->needs_style_recompute = true;
        }
        elem->styles_resolved = false;

        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            clear_load_stylesheet_cascade_recursive(child);
        }
    }
}

static void store_document_stylesheet_bundle(DomDocument* dom_doc,
                                             CssStylesheet* first_stylesheet,
                                             CssStylesheet* second_stylesheet,
                                             CssStylesheet* third_stylesheet,
                                             CssStylesheet** inline_stylesheets,
                                             int inline_stylesheet_count,
                                             Pool* pool,
                                             const char* log_prefix,
                                             const char* reason) {
    if (!dom_doc || !pool) return;

    int total_stylesheets = inline_stylesheet_count;
    if (first_stylesheet) total_stylesheets++;
    if (second_stylesheet) total_stylesheets++;
    if (third_stylesheet) total_stylesheets++;
    dom_doc->stylesheet_capacity = total_stylesheets;
    dom_doc->stylesheet_count = 0;
    dom_doc->stylesheets = total_stylesheets > 0
        ? (CssStylesheet**)pool_alloc(pool, total_stylesheets * sizeof(CssStylesheet*))
        : nullptr;

    if (first_stylesheet && dom_doc->stylesheets) {
        dom_doc->stylesheets[dom_doc->stylesheet_count++] = first_stylesheet;
    }
    if (second_stylesheet && dom_doc->stylesheets) {
        dom_doc->stylesheets[dom_doc->stylesheet_count++] = second_stylesheet;
    }
    if (third_stylesheet && dom_doc->stylesheets) {
        dom_doc->stylesheets[dom_doc->stylesheet_count++] = third_stylesheet;
    }
    for (int i = 0; i < inline_stylesheet_count; i++) {
        if (inline_stylesheets && inline_stylesheets[i] && dom_doc->stylesheets) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = inline_stylesheets[i];
        }
    }
    log_debug("[%s] Stored %d stylesheets in DomDocument for %s",
              log_prefix ? log_prefix : "Lambda CSS",
              dom_doc->stylesheet_count, reason ? reason : "load");
}

static void store_document_stylesheets(DomDocument* dom_doc,
                                       CssStylesheet* external_stylesheet,
                                       CssStylesheet** inline_stylesheets,
                                       int inline_stylesheet_count,
                                       Pool* pool,
                                       const char* reason) {
    store_document_stylesheet_bundle(dom_doc, external_stylesheet, nullptr, nullptr,
                                     inline_stylesheets, inline_stylesheet_count,
                                     pool, "Lambda CSS", reason);
}

static void apply_stylesheet_to_dom_tree_if_nonempty(DomElement* dom_root,
                                                     CssStylesheet* stylesheet,
                                                     SelectorMatcher* matcher,
                                                     Pool* pool,
                                                     CssEngine* css_engine) {
    if (!dom_root || !stylesheet || stylesheet->rule_count == 0 ||
        !matcher || !pool || !css_engine) {
        return;
    }
    // Loader cascades share the same matcher across sheets; centralize the
    // non-empty guard without changing each loader's stylesheet ownership.
    apply_stylesheet_to_dom_tree_fast(dom_root, stylesheet, matcher, pool, css_engine);
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

    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)dom_doc->state, matcher);

    auto t_cascade_start = high_resolution_clock::now();
    reset_cascade_timing();
    reset_dom_element_timing();

    if (external_stylesheet && external_stylesheet->rule_count > 0) {
        log_debug("[Lambda CSS] Applying external stylesheet with %d rules", external_stylesheet->rule_count);
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, external_stylesheet, matcher, pool, css_engine);
    }

    auto t_external = high_resolution_clock::now();
    log_info("[TIMING] cascade(%s): external stylesheet: %.1fms",
             phase ? phase : "load",
             duration<double, std::milli>(t_external - t_cascade_start).count());

    for (int i = 0; i < inline_stylesheet_count; i++) {
        log_debug("[Lambda CSS] Inline stylesheet %d: ptr=%p, rule_count=%zu",
            i, inline_stylesheets ? inline_stylesheets[i] : nullptr,
            (inline_stylesheets && inline_stylesheets[i]) ? inline_stylesheets[i]->rule_count : 0);
        if (inline_stylesheets && inline_stylesheets[i]) {
            log_debug("[Lambda CSS] Stylesheet %d is not null, rule_count=%zu",
                i, inline_stylesheets[i]->rule_count);
            if (inline_stylesheets[i]->rule_count > 0) {
                log_debug("[Lambda CSS] Applying inline stylesheet %d with %zu rules",
                    i, inline_stylesheets[i]->rule_count);
                auto t_inline_start = high_resolution_clock::now();
                apply_stylesheet_to_dom_tree_if_nonempty(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
                auto t_inline_end = high_resolution_clock::now();
                log_info("[TIMING] cascade(%s): inline stylesheet %d (%zu rules): %.1fms",
                    phase ? phase : "load", i, inline_stylesheets[i]->rule_count,
                    duration<double, std::milli>(t_inline_end - t_inline_start).count());
                log_debug("[Lambda CSS] Finished applying inline stylesheet %d", i);
            } else {
                log_debug("[Lambda CSS] Skipping stylesheet %d: rule_count=%zu is not > 0",
                    i, inline_stylesheets[i]->rule_count);
            }
        } else {
            log_debug("[Lambda CSS] Stylesheet %d is NULL", i);
        }
    }

    auto t_cascade = high_resolution_clock::now();
    log_debug("[Lambda CSS] CSS cascade complete (%s)", phase ? phase : "load");
    log_cascade_timing_summary();
    log_dom_element_timing();
    log_info("[TIMING] load: CSS cascade (%s): %.1fms",
             phase ? phase : "load",
             duration<double, std::milli>(t_cascade - t_cascade_start).count());
}

/**
 * Check if content starts with a UTF-8 BOM (EF BB BF).
 */
static bool has_utf8_bom(const char* html, size_t len) {
    return len >= 3 &&
           (unsigned char)html[0] == 0xEF &&
           (unsigned char)html[1] == 0xBB &&
           (unsigned char)html[2] == 0xBF;
}

/**
 * Quick heuristic: check if content with high bytes appears to be valid UTF-8.
 * Scans up to 4096 bytes. If we find multi-byte UTF-8 sequences and no invalid
 * sequences, the content is likely UTF-8 regardless of what the meta tag says.
 * Returns true if content appears to be valid UTF-8 (has multi-byte sequences).
 * Returns false if content is all-ASCII (ambiguous) or has invalid UTF-8 sequences.
 */
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

/**
 * Detect charset from HTML content by scanning for <meta charset="..."> or
 * <meta http-equiv="Content-Type" content="...; charset=..."> within the first 1024 bytes.
 * Returns the charset name (e.g., "iso-8859-1") or nullptr if UTF-8 or not specified.
 * Per HTML spec, UTF-8 BOM takes precedence over meta charset declarations.
 * Also skips conversion if content is already valid UTF-8 (e.g., file saved as UTF-8
 * but with a legacy meta charset tag).
 */
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

/**
 * Convert HTML content from ISO-8859-1 or Windows-1252 to UTF-8.
 * These two encodings cover the vast majority of non-UTF-8 web pages.
 * Each byte 0x80-0xFF maps to a 2-byte or 3-byte UTF-8 sequence.
 * Returns a newly allocated UTF-8 string or nullptr on failure.
 * Caller must free the returned string with mem_free().
 */
/**
 * Convert content from a single-byte encoding to UTF-8 using a 128-entry mapping table.
 * The table maps bytes 0x80-0xFF to Unicode code points.
 * Bytes 0x00-0x7F are passed through as ASCII.
 */
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

/**
 * Convert content from a non-UTF-8 charset to UTF-8.
 * Currently supports ISO-8859-1 and Windows-1252 (covers ~95% of non-UTF-8 pages).
 * Returns a newly allocated UTF-8 string or nullptr if charset is unsupported.
 * Caller must free the returned string with mem_free().
 */
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

/**
 * Generate an HTML error page for network/loading errors.
 * Returns a heap-allocated HTML string that the caller must free with mem_free().
 * @param url The URL that failed to load
 * @param error_title Short error title (e.g., "Network Error", "404 Not Found")
 * @param error_detail Detailed error message
 */
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

/**
 * Load HTML document with Lambda CSS system
 * Parses HTML, applies CSS cascade, builds DOM tree, returns DomDocument for layout
 *
 * @param html_filename Path to HTML file
 * @param css_filename Optional external CSS file (can be NULL)
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
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
    DocumentScriptPhaseTiming* script_timing) {
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
    log_debug("[Lambda CSS] Loading HTML document: %s", html_filepath);

    // Step 1: Parse HTML with Lambda parser
    // If html_source is provided, use it directly; otherwise read from file or download
    char* html_content = nullptr;
    bool html_content_owned = false;
    if (html_source) {
        html_content = const_cast<char*>(html_source);
        log_debug("[Lambda CSS] Using in-memory HTML source (%zu bytes)", strlen(html_source));
    } else if (html_url->scheme == URL_SCHEME_HTTP || html_url->scheme == URL_SCHEME_HTTPS) {
        const char* url_str = url_get_href(html_url);
        log_debug("[Lambda CSS] Downloading HTML from URL: %s", url_str);
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

    // [debug] write html tree to 'html_tree.txt' — disabled; enable locally for inspection
    // if (log_default_category && log_default_category->output &&
    //     log_default_category->output != stdout && log_default_category->output != stderr) {
    //     StrBuf* htm_buf = strbuf_new();
    //     print_item(htm_buf, input->root, 0);
    //     FILE* htm_file = fopen("html_tree.txt", "w");
    //     if (htm_file) { fwrite(htm_buf->str, 1, htm_buf->length, htm_file); fclose(htm_file); }
    //     strbuf_free(htm_buf);
    // }

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
    log_debug("Parsed HTML root element");
    log_root_item((Item){.element = html_root});

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }
    dom_doc->document_charset = detected_charset;

    // Extract viewport meta tag values before building DOM tree
    extract_viewport_meta(html_root, dom_doc);
    // If viewport initial-scale is set and given_scale is default (1.0), apply it
    if (dom_doc->viewport_initial_scale != 1.0f && dom_doc->given_scale == 1.0f) {
        dom_doc->given_scale = dom_doc->viewport_initial_scale;
        log_info("[viewport] Applied initial-scale=%.2f to given_scale", dom_doc->given_scale);
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
    dom_doc->cached_css_engine = css_engine;
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Load external CSS if provided
    CssStylesheet* external_stylesheet = nullptr;
    if (css_filename) {
        log_debug("[Lambda CSS] Loading external CSS: %s", css_filename);
        char* css_content = read_text_file(css_filename);
        if (css_content) {
            size_t css_len = strlen(css_content);
            char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
            if (css_pool_copy) {
                str_copy(css_pool_copy, css_len + 1, css_content, css_len);
                mem_free(css_content);
                external_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
                if (external_stylesheet) {
                    const char* formatted_css = css_stylesheet_to_string_styled(
                        external_stylesheet, pool, CSS_FORMAT_EXPANDED);
                    if (formatted_css) {
                        log_debug("[Lambda CSS] Parsed external stylesheet:\n%s", formatted_css);
                    }
                    log_debug("[Lambda CSS] Loaded external stylesheet with %zu rules",
                            external_stylesheet->rule_count);
                } else {
                    log_warn("Failed to parse CSS file: %s", css_filename);
                }
            } else {
                mem_free(css_content);
            }
        } else {
            log_warn("Failed to load CSS file: %s", css_filename);
        }
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

    // print internal stylesheets for debugging
    // Skip the (expensive) formatting entirely when debug logs are disabled —
    // for large pages (e.g. cnn_lite) this dump dominated peak memory.
    if (log_level_enabled(NULL, LOG_LEVEL_DEBUG)) {
        for (int i = 0; i < inline_stylesheet_count; i++) {
            const char* formatted_css = css_stylesheet_to_string_styled(
                inline_stylesheets[i], pool, CSS_FORMAT_EXPANDED);
            if (formatted_css) {
                log_debug("[Lambda CSS] Parsed inline stylesheet %d:\n%s", i, formatted_css);
            }
        }
    }

    // Store stylesheets before scripts so getComputedStyle and @font-face share
    // the same sheet list that the pre-script cascade uses.
    store_document_stylesheets(dom_doc, external_stylesheet, inline_stylesheets,
                               inline_stylesheet_count, pool,
                               "getComputedStyle + @font-face");
    auto t_stylesheet_setup = timing ? high_resolution_clock::now() : t_css_parse;

    // Step 2c: Apply inline style="" attributes BEFORE scripts
    // Inline style="" attributes from HTML are applied first as the baseline.
    // JS style modifications (element.style.xxx = 'value') are applied after
    // via dom_element_apply_inline_style with a later source_order, so they
    // correctly override the original HTML inline styles in the cascade.
    // This also ensures the HTML→DOM tree mapping is pristine (no JS mutations yet).
    log_mem_stage("load_html: before_inline_attrs");
    apply_inline_styles_to_tree(dom_root, html_root, pool);
    log_mem_stage("load_html: after_inline_attrs");
    auto t_inline_style = timing ? high_resolution_clock::now() : t_stylesheet_setup;

    dom_doc->root = dom_root;  // set root for CSSOM and JS DOM API access

    bool legacy_scripts_before_cascade = load_scripts_before_cascade_legacy();
    bool css_cascade_current = false;
    if (!legacy_scripts_before_cascade) {
        log_mem_stage("load_html: before_pre_script_cascade");
        apply_load_css_cascade(dom_doc, dom_root, external_stylesheet,
                               inline_stylesheets, inline_stylesheet_count,
                               css_engine, pool, "pre-script");
        css_cascade_current = true;
        log_mem_stage("load_html: pre_script_cascade_done");
    } else {
        log_warn("[P17] RADIANT_SCRIPT_BEFORE_CASCADE enabled; using legacy load order");
    }
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

        if (!dom_doc->pending_navigation_url) {
            char* refresh_url = find_meta_refresh_url(html_root);
            if (refresh_url && refresh_url[0]) {
                dom_doc->pending_navigation_url = refresh_url;
                log_info("meta_refresh_navigation: pending navigation to %s", refresh_url);
            } else if (refresh_url) {
                mem_free(refresh_url);
            }
        }

        if (dom_doc->js_mutation_count > 0) {
            log_info("execute_document_scripts: %d DOM mutations from JS, CSS cascade will re-resolve after scripts",
                     dom_doc->js_mutation_count);

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

            store_document_stylesheets(dom_doc, external_stylesheet, inline_stylesheets,
                                       inline_stylesheet_count, pool,
                                       "post-script getComputedStyle + @font-face");

            if (!legacy_scripts_before_cascade) {
                clear_load_stylesheet_cascade_recursive(static_cast<DomNode*>(dom_root));
                apply_load_css_cascade(dom_doc, dom_root, external_stylesheet,
                                       inline_stylesheets, inline_stylesheet_count,
                                       css_engine, pool, "post-script");
                css_cascade_current = true;
                log_mem_stage("load_html: post_script_cascade_done");
            }
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
    } else {
        log_debug("[Lambda CSS] Skipping document script execution for caller-managed JS");
    }

    if (!css_cascade_current) {
        apply_load_css_cascade(dom_doc, dom_root, external_stylesheet,
                               inline_stylesheets, inline_stylesheet_count,
                               css_engine, pool,
                               legacy_scripts_before_cascade ? "legacy-post-script" : "no-script");
        css_cascade_current = true;
    }
    log_mem_stage("load_html: cascade_done");
    auto t_final_cascade = timing ? high_resolution_clock::now() : t_post_script;

    // Dump CSS computed values for testing/comparison (includes inheritance, before layout).
    // Skip the (potentially expensive) tree walk entirely when debug logs are disabled \u2014
    // for large pages (e.g. cnn_lite) this dump dominated peak RSS.
    if (log_level_enabled(NULL, LOG_LEVEL_DEBUG)) {
        StrBuf* str_buf = strbuf_new();
        dom_root->print(str_buf, 0);
        log_debug("Built DomElement tree with styles::\n%s", str_buf->str);
        strbuf_free(str_buf);
    }

    // Step 8: Create DomDocument structure (already created by dom_document_create)
    // Just populate the additional fields
    dom_doc->root = dom_root;
    dom_doc->html_root = html_root;
    dom_doc->html_version = detected_version;
    dom_doc->url = html_url;
    for (int i = 0; i < superseded_html_url_count; i++) {
        if (superseded_html_urls[i] && superseded_html_urls[i] != html_url) {
            url_destroy(superseded_html_urls[i]);
        }
    }
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;

    // Set scale fields for HTML documents
    // HTML layout is in CSS logical pixels, scale is set later based on display context
    dom_doc->given_scale = 1.0f;
    dom_doc->scale = 1.0f;  // Will be updated by caller (window or render) with pixel_ratio

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

    log_debug("[Lambda CSS] Document loaded and styled");
    return dom_doc;
}

DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source = nullptr,
    bool track_source_lines = false, bool execute_scripts = true) {
    return load_lambda_html_doc_profiled(html_url, css_filename, viewport_width, viewport_height,
                                         pool, html_source, track_source_lines, execute_scripts,
                                         nullptr, nullptr);
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
        "let doc^err = input(\"%s\", 'pdf')\n"
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
        "let doc^err = input(\"%s\", 'pdf')\n"
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

    char* bridge_source = build_graph_to_html_bridge_script(graph_source, nullptr, "load_html_doc");
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

static DomDocument* load_html_doc_no_redirect(Url *base, char* doc_url, int viewport_width, int viewport_height, float pixel_ratio) {
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
        doc = load_lambda_html_doc(full_url, NULL, viewport_width, viewport_height, pool);
    } else {
    // Detect file type by extension (local files only)
    const char* ext = strrchr(doc_url, '.');

    if (ext && strcmp(ext, ".ls") == 0) {
        // Load Lambda script: evaluate script → wrap result → layout
        log_info("[load_html_doc] Detected Lambda script file, using script evaluation pipeline");
        doc = load_lambda_script_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".mmd") == 0 || strcmp(ext, ".d2") == 0 ||
                       strcmp(ext, ".dot") == 0 || strcmp(ext, ".gv") == 0)) {
        log_info("[load_html_doc] Detected graph file, using Lambda graph package pipeline");
        doc = load_graph_bridge_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0)) {
        // Load LaTeX document via LaTeX→HTML pipeline
        log_info("[load_html_doc] Detected LaTeX file, using LaTeX→HTML pipeline");
        doc = load_latex_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0)) {
        // Load Markdown document: parse Markdown → convert to HTML → layout
        log_info("[load_html_doc] Detected Markdown file, using Markdown→HTML pipeline");
        doc = load_markdown_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".wiki") == 0) {
        // Load Wiki document: parse Wiki → convert to HTML → layout
        log_info("[load_html_doc] Detected Wiki file, using Wiki→HTML pipeline");
        doc = load_wiki_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".xml") == 0) {
        // Load XML document: parse XML → treat as custom HTML elements → apply CSS → layout
        log_info("[load_html_doc] Detected XML file, using XML→DOM pipeline");
        doc = load_xml_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".pdf") == 0) {
        log_info("[load_html_doc] Detected PDF file, using Lambda PDF package in-memory element pipeline");
        doc = load_pdf_bridge_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".svg") == 0) {
        // Load SVG document as a normal DOM-backed image document.
        log_info("[load_html_doc] Detected SVG file, using SVG image document pipeline");
        doc = load_svg_doc(full_url, viewport_width, viewport_height, pool, pixel_ratio);
    } else if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                       strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0)) {
        // Load image document as html/body/img so view interactions use normal DOM paths.
        log_info("[load_html_doc] Detected image file, using DOM image document pipeline");
        doc = load_image_doc(full_url, viewport_width, viewport_height, pool, pixel_ratio);
    } else if (ext && (strcmp(ext, ".json") == 0 || strcmp(ext, ".yaml") == 0 ||
                       strcmp(ext, ".yml") == 0 || strcmp(ext, ".toml") == 0 ||
                       strcmp(ext, ".txt") == 0 || strcmp(ext, ".csv") == 0 ||
                       strcmp(ext, ".ini") == 0 || strcmp(ext, ".conf") == 0 ||
                       strcmp(ext, ".cfg") == 0 || strcmp(ext, ".log") == 0)) {
        // Load text document: read source → wrap in <pre> → render as HTML
        log_info("[load_html_doc] Detected text file, using Text→HTML pipeline");
        doc = load_text_doc(full_url, viewport_width, viewport_height, pool);
    } else {
        // Load HTML document with Lambda CSS system
        doc = load_lambda_html_doc(full_url, NULL, viewport_width, viewport_height, pool);
    }
    }

    if (!doc) {
        url_destroy(full_url);
        pool_destroy(pool);
    }

    return doc;
}

DomDocument* load_html_doc(Url *base, char* doc_url, int viewport_width, int viewport_height, float pixel_ratio) {
    const int max_redirects = 8;
    Url* current_base = base;
    char* current_doc_url = doc_url;
    char* owned_doc_url = nullptr;

    for (int redirect_count = 0; redirect_count <= max_redirects; redirect_count++) {
        DomDocument* doc = load_html_doc_no_redirect(current_base, current_doc_url,
            viewport_width, viewport_height, pixel_ratio);
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

/**
 * Load SVG document as a DOM-backed html/body/img document.
 */
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    (void)pixel_ratio;
    return load_dom_backed_image_document(svg_url, viewport_width, viewport_height, pool, "load_svg_doc");
}

/**
 * Load raster image document (PNG, JPG, JPEG, GIF) as a DOM-backed html/body/img document.
 */
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
    if (stylesheet) {
        log_debug("[%s] Loaded %s with %zu rules", log_prefix, label, stylesheet->rule_count);
    } else if (warn_missing) {
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
    log_debug("[%s] Loading stylesheet: %s", log_prefix, css_filename);
    CssStylesheet* stylesheet = load_pool_backed_stylesheet(css_engine, pool, css_filename,
                                                            log_prefix, label, warn_missing);
    mem_free(css_filename);
    return stylesheet;
}

/**
 * Load text document as source view
 * Reads text file content, wraps in HTML <pre> element, renders with monospace font
 *
 * @param text_url URL to text file
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure ready for layout
 */
DomDocument* load_text_doc(Url* text_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

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

    // Step 1: Read text file content
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

    // Step 3: Build HTML document wrapping text in <pre>
    auto step3_start = std::chrono::high_resolution_clock::now();

    // Get filename for title
    const char* filename = strrchr(text_filepath, '/');
    filename = filename ? filename + 1 : text_filepath;

    // Create HTML with minimal styling for source text view
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

    // Calculate buffer size and format HTML
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

    // Step 4: Parse HTML using Lambda parser
    auto step4_start = std::chrono::high_resolution_clock::now();

    Input* input = parse_layout_source_as(html_content, text_url, "html", "load_text_doc");
    mem_free(html_content);

    if (!input || !input->root.item || input->root.item == ITEM_ERROR) {
        log_error("Failed to parse HTML wrapper for text file");
        return nullptr;
    }

    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - Parse HTML: %.1fms",
        std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 5: Build DOM tree and apply CSS (reuse lambda_html_doc logic)
    auto step5_start = std::chrono::high_resolution_clock::now();

    Element* html_root = get_html_root_element(input);
    if (!html_root) {
        log_error("Failed to get HTML root from text file wrapper");
        return nullptr;
    }

    // Create DomDocument
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument for text file");
        return nullptr;
    }

    // Build DomElement tree
    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree from text file");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

    // Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine for text file");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Extract and apply inline styles from the HTML
    int stylesheet_count = 0;
    CssStylesheet** stylesheets = extract_and_collect_css(html_root, css_engine, text_filepath, pool, &stylesheet_count);

    // Create selector matcher
    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)dom_doc->state, matcher);

    // Apply stylesheets to DOM tree
    for (int i = 0; i < stylesheet_count; i++) {
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, stylesheets[i], matcher, pool, css_engine);
    }

    auto step5_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 5 - Build DOM & apply CSS: %.1fms",
        std::chrono::duration<double, std::milli>(step5_end - step5_start).count());

    // Set document properties
    dom_doc->root = dom_root;
    dom_doc->html_root = html_root;
    dom_doc->html_version = HTML5;
    dom_doc->url = text_url;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_text_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    return dom_doc;
}

/**
 * Load markdown document with Lambda CSS system
 * Parses markdown, applies GitHub-style CSS, builds DOM tree, returns DomDocument for layout
 *
 * @param markdown_url URL to markdown file
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
static void release_layout_runtime(Runtime* runtime) {
    if (!runtime) return;
    runtime_cleanup(runtime);
    mem_free(runtime);
}

static DomDocument* create_layout_dom(Input* input, Element* root,
                                      const char* document_kind,
                                      Runtime* owned_runtime,
                                      DomElement** out_root) {
    if (out_root) *out_root = nullptr;
    DomDocument* document = dom_document_create(input);
    if (!document) {
        log_error("[LAYOUT DOC INIT] failed to create %s document", document_kind);
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

DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!markdown_url || !pool) {
        log_error("load_markdown_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard markdown_path_guard = { url_to_local_path(markdown_url) };
    char* markdown_filepath = markdown_path_guard.path;
    if (!markdown_filepath) {
        // url_to_local_path failed - try using the URL's pathname directly as fallback
        const char* pathname = url_get_pathname(markdown_url);
        log_debug("load_markdown_doc: url_to_local_path returned NULL, scheme=%d, pathname=%s, href=%s",
                  markdown_url->scheme,
                  pathname ? pathname : "(null)",
                  markdown_url->href ? markdown_url->href->chars : "(null)");
    }
    log_info("[TIMING] Loading markdown document: %s", markdown_filepath);

    // Step 1: Parse markdown with Lambda parser
    auto step1_start = std::chrono::high_resolution_clock::now();
    Input* input = read_layout_input_file(markdown_url, markdown_filepath,
                                          "markdown", "load_markdown_doc", "markdown");
    if (!input) {
        return nullptr;
    }

    // Get root element from parsed markdown
    Element* markdown_root = input_first_element_root(input);

    if (!markdown_root) {
        log_error("Failed to get markdown root element");
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Parse markdown: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    Runtime* markdown_math_runtime = nullptr;

    // Step 1.5: Process <math> elements using Lambda math package
    // Walk the Element tree, find <math> elements, render them to HTML via the math package,
    // and replace them in the tree with the rendered HTML elements.
    {
        auto math_start = std::chrono::high_resolution_clock::now();

        // Collect all <math> elements: {parent, index_in_parent, source, is_display}
        struct MathInfo {
            Element* parent;
            int64_t index;
            const char* source;
            size_t source_len;
            bool is_display;
        };
        ArrayList* math_list = arraylist_new(16);

        // Recursive lambda to walk the tree (use a stack to avoid recursion depth issues)
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
                    // Found a <math> element - extract source and type
                    ConstItem type_attr = child_elem->get_attr("type");
                    String* type_str_val = type_attr.string();
                    bool is_display = type_str_val && strcmp(type_str_val->chars, "block") == 0;

                    // Get LaTeX source from first string child
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
                    // Not a <math> element - recurse into it
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
            context = nullptr;
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

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    auto step2_start = std::chrono::high_resolution_clock::now();
    DomElement* dom_root = nullptr;
    DomDocument* dom_doc = create_layout_dom(
        input, markdown_root, "markdown", markdown_math_runtime, &dom_root);
    if (!dom_doc) return nullptr;

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Build DOM tree: %.1fms",
        std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Initialize CSS engine
    auto step3_start = std::chrono::high_resolution_clock::now();
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        dom_document_destroy(dom_doc);
        release_layout_runtime(markdown_math_runtime);
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

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
    {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        state_configure_selector_matcher((DocState*)dom_doc->state, matcher);
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, markdown_stylesheet, matcher, pool, css_engine);
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, math_stylesheet, matcher, pool, css_engine);
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, katex_stylesheet, matcher, pool, css_engine);
    }
    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 5.5: Apply inline style="" attributes (highest priority, after stylesheet cascade)
    apply_inline_styles_to_tree(dom_root, markdown_root, pool);

    // Step 6: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = markdown_root;
    dom_doc->html_version = HTML5;  // Treat markdown as HTML5
    dom_doc->url = markdown_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;
    dom_doc->lambda_runtime = markdown_math_runtime;

    store_document_stylesheet_bundle(dom_doc, markdown_stylesheet, math_stylesheet, katex_stylesheet,
                                     nullptr, 0, pool, "Lambda Markdown", "@font-face processing");

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_markdown_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    return dom_doc;
}

/**
 * Load MediaWiki document with Lambda CSS system
 * Parses wiki markup, applies Wikipedia-style CSS, builds DOM tree, returns DomDocument for layout
 *
 * @param wiki_url URL to wiki file
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
DomDocument* load_wiki_doc(Url* wiki_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!wiki_url || !pool) {
        log_error("load_wiki_doc: invalid parameters");
        return nullptr;
    }

    LayoutTempPathGuard wiki_path_guard = { url_to_local_path(wiki_url) };
    char* wiki_filepath = wiki_path_guard.path;
    if (!wiki_filepath) {
        log_error("load_wiki_doc: failed to resolve wiki file URL");
        return nullptr;
    }
    log_info("[TIMING] Loading wiki document: %s", wiki_filepath);

    // Step 1: Parse wiki with Lambda parser
    auto step1_start = std::chrono::high_resolution_clock::now();
    Input* input = read_layout_input_file(wiki_url, wiki_filepath,
                                          "wiki", "load_wiki_doc", "wiki");
    if (!input) {
        return nullptr;
    }

    // Get root element from parsed wiki
    Element* wiki_root = input_first_element_root(input);

    if (!wiki_root) {
        log_error("Failed to get wiki root element");
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Parse wiki: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    auto step2_start = std::chrono::high_resolution_clock::now();
    DomElement* dom_root = nullptr;
    DomDocument* dom_doc = create_layout_dom(
        input, wiki_root, "wiki", nullptr, &dom_root);
    if (!dom_doc) return nullptr;

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Build DOM tree: %.1fms",
        std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Initialize CSS engine
    auto step3_start = std::chrono::high_resolution_clock::now();
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    CssStylesheet* wiki_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/wiki.css", "Lambda Wiki", "wiki stylesheet", true);
    if (!wiki_stylesheet) {
        log_warn("Continuing without stylesheet - wiki will use browser defaults");
    }

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 5: Apply CSS cascade to DOM tree
    auto step4_start = std::chrono::high_resolution_clock::now();
    if (wiki_stylesheet && wiki_stylesheet->rule_count > 0) {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        state_configure_selector_matcher((DocState*)dom_doc->state, matcher);
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, wiki_stylesheet, matcher, pool, css_engine);
    }
    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 6: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = wiki_root;
    dom_doc->html_version = HTML5;  // Treat wiki as HTML5
    dom_doc->url = wiki_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_wiki_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    return dom_doc;
}

/**
 * Load LaTeX document with Lambda CSS system
 * Parses LaTeX, converts to HTML, applies CSS, builds DOM tree, returns DomDocument for layout
 *
 * @param latex_url URL to LaTeX file
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
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
    // Normalize backslashes to forward slashes so the path is safe inside a Lambda string literal
    char safe_path[1024];
    snprintf(safe_path, sizeof(safe_path), "%s", latex_filepath);
    for (char* p = safe_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    char script_buf[4096];
    snprintf(script_buf, sizeof(script_buf),
        "import latex: lambda.package.latex.latex\n"
        "let ast^err = input(\"%s\", {type: \"latex\"})\n"
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

    // Run the script in-memory (no temp file needed). Match the normal .ls
    // view path: allocate generated nodes in the retained document arena and
    // keep the runtime alive for the document session.
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

    // The script returns a Lambda Element tree (not an HTML string).
    // Use it directly as input to build_dom_tree_from_element, skipping
    // the unnecessary serialize→reparse roundtrip.
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

    // run_script_mir leaves thread-local context pointing at a stack Runner.
    // The runtime itself is retained on the document; clear the transient TLS.
    context = nullptr;
    input_context = nullptr;

    if (!html_root) {
        log_error("[Lambda LaTeX] Failed to get HTML root element from LaTeX conversion");
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Converted LaTeX to HTML Element tree via Lambda package");

    // Step 3: Create DomDocument and build DomElement tree from HTML Element tree
    log_debug("[Lambda LaTeX] Building DomElement tree from HTML");
    DomDocument* dom_doc = dom_document_create(result_input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    // Ensure CSS property system is initialized before building DOM tree,
    // so that inline style declarations get correct property IDs.
    css_property_system_init(dom_doc->pool);

    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree from HTML");
        dom_document_destroy(dom_doc);
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Built DomElement tree: root=%p", (void*)dom_root);

    // Step 4: Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        dom_document_destroy(dom_doc);
        release_layout_runtime(latex_runtime);
        pool_destroy(result_pool);
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    CssStylesheet* latex_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/latex/css/article.css", "Lambda LaTeX", "LaTeX stylesheet", false);
    if (!latex_stylesheet) {
        log_debug("No latex.css file found, LaTeX HTML will use embedded and inline styles");
    }
    CssStylesheet* katex_stylesheet = load_home_stylesheet(
        css_engine, pool, "input/latex/css/katex.css", "Lambda LaTeX", "KaTeX font stylesheet", false);

    // Step 6: Extract and parse any inline <style> elements from HTML
    log_debug("[Lambda LaTeX] Extracting inline <style> elements from LaTeX-generated HTML...");
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, latex_filepath, pool, &inline_stylesheet_count);

    // Step 7: Apply CSS cascade to DOM tree
    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)dom_doc->state, matcher);

    // Apply LaTeX stylesheet first if available
    if (latex_stylesheet && latex_stylesheet->rule_count > 0) {
        log_debug("[Lambda LaTeX] Applying LaTeX stylesheet...");
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, latex_stylesheet, matcher, pool, css_engine);
    }

    // Apply inline stylesheets from LaTeX-generated HTML
    for (int i = 0; i < inline_stylesheet_count; i++) {
        if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
            log_debug("[Lambda LaTeX] Applying inline stylesheet %d with %zu rules",
                     i, inline_stylesheets[i]->rule_count);
            apply_stylesheet_to_dom_tree_if_nonempty(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
        }
    }

    // Apply inline style="" attributes (highest priority)
    apply_inline_styles_to_tree(dom_root, html_root, pool);

    log_debug("[Lambda LaTeX] CSS cascade complete");

    store_document_stylesheet_bundle(dom_doc, latex_stylesheet, katex_stylesheet, nullptr,
                                     inline_stylesheets, inline_stylesheet_count,
                                     pool, "Lambda LaTeX", "@font-face processing");

    // Step 8: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = html_root;
    dom_doc->html_version = HTML5;  // Treat LaTeX-generated HTML as HTML5
    dom_doc->url = latex_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;
    dom_doc->lambda_runtime = latex_runtime;

    log_debug("[Lambda LaTeX] LaTeX document loaded, converted to HTML, and styled");
    return dom_doc;
}

/**
 * Load XML document with Lambda CSS system
 * Parses XML, treats XML elements as custom HTML elements, applies external CSS, builds DOM tree
 *
 * @param xml_url URL to XML file
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
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

    // Step 1: Read XML file
    auto t_read = high_resolution_clock::now();
    char* xml_content = read_text_file(xml_filepath);
    if (!xml_content) {
        log_error("[Lambda XML] Failed to read XML file: %s", xml_filepath);
        return nullptr;
    }
    auto t_parse = high_resolution_clock::now();
    log_info("[TIMING] load: read XML: %.1fms",
             duration_cast<duration<double, std::milli>>(t_parse - t_read).count());

    // Step 2: Parse XML with ui_mode for unified DOM allocation
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

    // Check if XML has stylesheet - if not, fall back to source text view
    if (!xml_input->xml_stylesheet_href) {
        log_info("[Lambda XML] No <?xml-stylesheet?> directive found, showing as source text");
        return load_text_doc(xml_url, viewport_width, viewport_height, pool);
    }

    log_info("[Lambda XML] Found stylesheet: %s", xml_input->xml_stylesheet_href);
    auto t_css_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse XML: %.1fms",
             duration_cast<duration<double, std::milli>>(t_css_parse - t_parse).count());

    // Step 3: Get the actual XML root element (skip document wrapper)
    Element* document_wrapper = (Element*)xml_input->root.item;
    Element* xml_root = nullptr;

    // Find the first actual XML element (skip processing instructions, comments)
    if (document_wrapper && document_wrapper->type) {
        TypeElmt* doc_type = (TypeElmt*)document_wrapper->type;

        if (strcmp(doc_type->name.str, "document") == 0) {
            // Element extends List, so access children via items array
            for (int64_t i = 0; i < document_wrapper->length; i++) {
                Item child = document_wrapper->items[i];
                TypeId child_type_id = get_type_id(child);

                if (child.item && child_type_id == LMD_TYPE_ELEMENT) {
                    Element* child_elem = (Element*)child.item;
                    TypeElmt* child_type = (TypeElmt*)child_elem->type;

                    // Skip processing instructions and comments
                    if (child_type->name.str[0] != '?' && child_type->name.str[0] != '!') {
                        xml_root = child_elem;
                        log_debug("[Lambda XML] Found XML root element: <%s> with %lld children",
                                child_type->name.str, child_elem->length);
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

    // Step 4: Create CSS engine and set viewport
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("[Lambda XML] Failed to create CSS engine");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Step 5: Load external CSS stylesheet
    log_debug("[Lambda XML] Loading external stylesheet: %s", xml_input->xml_stylesheet_href);

    CssStylesheet* external_stylesheet = nullptr;
    char* css_content = read_text_file(xml_input->xml_stylesheet_href);
    if (css_content) {
        size_t css_len = strlen(css_content);
        external_stylesheet = css_parse_stylesheet(css_engine, css_content, xml_filepath);
        if (external_stylesheet) {
            log_info("[Lambda XML] Loaded external stylesheet: %s (%zu bytes)",
                     xml_input->xml_stylesheet_href, css_len);
        } else {
            log_error("[Lambda XML] Failed to parse CSS stylesheet");
            mem_free(css_content);
            return nullptr;
        }
        mem_free(css_content);
    } else {
        log_error("[Lambda XML] Failed to read CSS file: %s", xml_input->xml_stylesheet_href);
        return nullptr;
    }

    auto t_dom = high_resolution_clock::now();
    log_info("[TIMING] load: parse CSS: %.1fms",
             duration_cast<duration<double, std::milli>>(t_dom - t_css_parse).count());

    // Step 6: Create DOM document
    DomDocument* dom_doc = dom_document_create(xml_input);
    if (!dom_doc) {
        log_error("[Lambda XML] Failed to create DOM document");
        return nullptr;
    }

    // Assign pool to DOM document for future allocations
    dom_doc->pool = pool;
    dom_doc->url = xml_url;

    // Step 7: Build DOM tree from XML elements
    // XML needs to be wrapped in html > body structure for the layout engine
    log_debug("[Lambda XML] Building DOM tree from XML root with %lld children", xml_root->length);

    // Create <html> wrapper element
    DomElement* html_elem = dom_element_create(dom_doc, "html", nullptr);
    if (!html_elem) {
        log_error("[Lambda XML] Failed to create html wrapper element");
        return nullptr;
    }
    html_elem->tag_id = HTM_TAG_HTML;

    // Create <body> wrapper element
    DomElement* body_elem = dom_element_create(dom_doc, "body", nullptr);
    if (!body_elem) {
        log_error("[Lambda XML] Failed to create body wrapper element");
        return nullptr;
    }
    body_elem->tag_id = HTM_TAG_BODY;

    // Build DOM tree from XML content and add as child of body
    DomElement* xml_dom = build_dom_tree_from_element(xml_root, dom_doc, body_elem);
    if (!xml_dom) {
        log_error("[Lambda XML] Failed to build DOM tree from XML");
        return nullptr;
    }

    // Wire up the tree structure: html > body > xml_dom
    html_elem->first_child = static_cast<DomNode*>(body_elem);
    html_elem->last_child = static_cast<DomNode*>(body_elem);
    body_elem->parent = static_cast<DomNode*>(html_elem);

    body_elem->first_child = static_cast<DomNode*>(xml_dom);
    body_elem->last_child = static_cast<DomNode*>(xml_dom);
    xml_dom->parent = static_cast<DomNode*>(body_elem);

    log_debug("[Lambda XML] Built DOM tree: html > body > %s", xml_dom->tag_name);

    // Use html_elem as the root
    dom_doc->root = html_elem;
    dom_doc->html_root = xml_root;

    auto t_cascade = high_resolution_clock::now();
    log_info("[TIMING] load: build DOM: %.1fms",
             duration_cast<duration<double, std::milli>>(t_cascade - t_dom).count());

    // Step 8: Store stylesheet in DOM document
    dom_doc->stylesheet_capacity = 1;
    dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, sizeof(CssStylesheet*));
    dom_doc->stylesheet_count = 0;
    if (external_stylesheet) {
        dom_doc->stylesheets[dom_doc->stylesheet_count++] = external_stylesheet;
    }

    // Step 9: Apply CSS cascade to DOM tree
    log_debug("[Lambda XML] Applying CSS cascade to XML/DOM tree");

    // Create selector matcher
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) {
        log_error("[Lambda XML] Failed to create selector matcher");
        return nullptr;
    }
    state_configure_selector_matcher((DocState*)dom_doc->state, matcher);

    // Reset timing stats before cascade
    reset_cascade_timing();

    // Apply external stylesheet to the entire tree (starting from html)
    if (external_stylesheet && external_stylesheet->rule_count > 0) {
        log_debug("[Lambda XML] Applying external stylesheet with %zu rules", external_stylesheet->rule_count);
        apply_stylesheet_to_dom_tree_if_nonempty(html_elem, external_stylesheet, matcher, pool, css_engine);
    }

    log_cascade_timing_summary();  // Output cascade stats

    // Apply inline style="" attributes (if XML elements have them)
    apply_inline_styles_to_tree(html_elem, xml_root, pool);
    log_debug("[Lambda XML] CSS cascade complete");

    auto t_complete = high_resolution_clock::now();
    log_info("[TIMING] load: apply cascade: %.1fms",
             duration_cast<duration<double, std::milli>>(t_complete - t_cascade).count());
    log_info("[TIMING] load: total: %.1fms",
             duration_cast<duration<double, std::milli>>(t_complete - total_start).count());

    log_debug("[Lambda XML] Document loaded and styled");
    return dom_doc;
}

/**
 * Load HTML document from an in-memory string, using the Lambda CSS pipeline.
 * Avoids writing to a temp file. Creates its own pool (owned by the returned DomDocument).
 *
 * @param html_source  NUL-terminated HTML string (caller owns; not freed here)
 * @param viewport_width  Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @return DomDocument ready for layout, or nullptr on failure
 */
static DomDocument* load_html_string_doc(const char* html_source, int viewport_width, int viewport_height) {
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!pool) { log_error("load_html_string_doc: pool_create failed"); return nullptr; }
    Url* base_url = get_current_dir();
    if (!base_url) { log_error("load_html_string_doc: get_current_dir failed"); pool_destroy(pool); return nullptr; }
    return load_lambda_html_doc(base_url, nullptr, viewport_width, viewport_height, pool, html_source);
}

/**
 * Load Lambda script document with Lambda CSS system
 * Evaluates a Lambda script, wraps the result in HTML structure, applies CSS, builds DOM tree
 *
 * @param script_url URL to Lambda script file (.ls)
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return DomDocument structure with Lambda CSS DOM, ready for layout
 */
DomDocument* load_lambda_script_source_doc(Url* script_url, const char* script_source,
                                           int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!script_url || !pool) {
        log_error("load_lambda_script_doc: invalid parameters");
        return nullptr;
    }

    struct ScriptTempPathGuard {
        char* path;
        ~ScriptTempPathGuard() {
            if (path) mem_free(path);
        }
    };
    ScriptTempPathGuard script_path_guard = { url_to_local_path(script_url) };
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

    // Phase 5: Create result Input with arena for unified DOM allocation.
    // Elements from JIT execution will be fat DomElements on this arena.
    Pool* result_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    Input* result_input = Input::create(result_pool, script_url);
    result_input->ui_mode = true;
    runtime->ui_mode = true;
    runtime->result_arena = result_input->arena;

    source_pos_bridge_reset();
    render_map_init();
    render_map_set_path_recorder(&render_map_record_path);

    log_debug("[Lambda Script] Evaluating script (ui_mode=true, arena allocation)...");
    // Use MIR Direct JIT to evaluate the Lambda script as a functional document result.
    Input* script_output = run_script_mir(runtime, script_source, script_filepath, false);

    // After run_script_mir returns, the thread-local context points to a
    // stack-local Runner that has been destroyed. Restore it from the retained
    // runtime so that GC operations (e.g. render_map_set_doc_root →
    // heap_register_gc_root) can access context->heap->gc safely.
    EvalContext retained_ctx;
    memset(&retained_ctx, 0, sizeof(retained_ctx));
    if (runtime->heap) {
        retained_ctx.heap = runtime->heap;
        retained_ctx.nursery = runtime->nursery;
        retained_ctx.name_pool = runtime->name_pool;
        retained_ctx.pool = runtime->heap->pool;
        if (runtime->ui_mode && runtime->result_arena) {
            retained_ctx.ui_mode = true;
            retained_ctx.arena = runtime->result_arena;
        }
        context = &retained_ctx;
        input_context = (Context*)&retained_ctx;
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

    // Step 2: Get the result from script execution
    TypeId result_type = get_type_id(script_output->root);
    log_debug("[Lambda Script] Script result type: %d", result_type);

    // Check if the script returned an error
    if (result_type == LMD_TYPE_ERROR) {
        log_error("[Lambda Script] Script evaluation returned an error");
        release_layout_runtime(runtime);
        pool_destroy(result_pool);
        return nullptr;
    }

    // Check if the script returned SVG content — embed it in an HTML page so the
    // inline SVG rendering pipeline (render_inline_svg) handles it, rather than
    // the external-SVG file path (which uses ThorVG's root-embed approach that
    // is not drawn by render_children at the root level).
    auto write_svg_wrapped_html = [&](const char* svg_content) -> DomDocument* {
        // Build a minimal HTML page that contains only the SVG
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
        // Handle HTML strings: script returned a full HTML document as a string
        if (result_str && result_str->len >= 5 &&
                (strncmp(result_str->chars, "<html", 5) == 0 ||
                 (result_str->len >= 9 && strncmp(result_str->chars, "<!DOCTYPE", 9) == 0))) {
            log_info("[Lambda Script] Script returned HTML string, loading in-memory (%zu bytes)", (size_t)result_str->len);
            // Parse the HTML into a DomDocument BEFORE tearing down the runtime:
            // result_str lives in the runtime's GC heap, so runtime_cleanup() frees
            // result_str->chars. load_html_string_doc() copies its input (see the
            // SVG helper above, which frees its buffer right after the call), so the
            // document is self-contained once it returns. (was heap-use-after-free)
            DomDocument* doc = load_html_string_doc(result_str->chars, viewport_width, viewport_height);
            release_layout_runtime(runtime);
            pool_destroy(result_pool);
            url_destroy(script_url);
            return doc;
        }
    }

    // Check if the script returned a complete HTML document
    bool is_html_document = false;
    Element* html_elem = nullptr;

    if (result_type == LMD_TYPE_ELEMENT) {
        Element* result_elem = script_output->root.element;
        TypeElmt* elem_type = (TypeElmt*)result_elem->type;

        // Check if this is an 'html' element
        if (elem_type && str_ieq_const(elem_type->name.str, strlen(elem_type->name.str), "html")) {
            log_debug("[Lambda Script] Script returned complete HTML document, using as-is");
            is_html_document = true;
            html_elem = result_elem;
        }
    }

    // If not a complete HTML document, wrap the result
    if (!is_html_document) {
        // Wrap the result in an HTML structure for rendering
        Element* result_elem = nullptr;

        if (result_type == LMD_TYPE_ELEMENT) {
            // If the script returns an element (but not html), use it directly
            result_elem = script_output->root.element;
            log_debug("[Lambda Script] Script returned element, wrapping in html>body");
        } else {
            // For other types, create a wrapper element with the result as text content
            log_debug("[Lambda Script] Script returned non-element, wrapping in div");

            // Convert result to string representation
            StrBuf* result_str = strbuf_new();
            print_item(result_str, script_output->root, 0);

            // Use MarkBuilder with result_input (ui_mode) so DomText goes to result arena
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

        // Step 3: Create HTML wrapper structure using MarkBuilder (on result arena in ui_mode)
        log_debug("[Lambda Script] Building HTML wrapper structure");

        MarkBuilder builder(result_input);

        // Build: html > body > result_elem
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

        // Step 4: Update root to point to html element
        result_input->root = html_item;
    } else {
        // In ui_mode, the html root element is already on result_input's arena
        result_input->root = {.element = html_elem};

        auto step2_start = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 2 - HTML document detected, skipping wrap: %.1fms",
            std::chrono::duration<double, std::milli>(step2_start - step1_end).count());
    }

    // Step 5: Create DomDocument and build DomElement tree
    // Use result_input so dom_doc->input->ui_mode enables fat allocation path
    auto step5_start = std::chrono::high_resolution_clock::now();
    DomDocument* dom_doc = dom_document_create(result_input);
    if (!dom_doc) {
        log_error("[Lambda Script] Failed to create DomDocument");
        pool_destroy(result_pool);
        release_layout_runtime(runtime);
        return nullptr;
    }

    // Ensure CSS property system is initialized before building DOM tree,
    // so that inline style declarations get correct property IDs.
    css_property_system_init(dom_doc->pool);

    DomElement* dom_root = build_dom_tree_from_element(html_elem, dom_doc, nullptr);
    if (!dom_root) {
        log_error("[Lambda Script] Failed to build DomElement tree");
        dom_document_destroy(dom_doc);
        pool_destroy(result_pool);
        release_layout_runtime(runtime);
        return nullptr;
    }

    auto step5_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 5 - Build DOM tree: %.1fms",
        std::chrono::duration<double, std::milli>(step5_end - step5_start).count());

    // Step 6: Initialize CSS engine
    auto step6_start = std::chrono::high_resolution_clock::now();
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("[Lambda Script] Failed to create CSS engine");
        release_layout_runtime(runtime);
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Step 7: Load stylesheets
    CssStylesheet* script_stylesheet = nullptr;
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = nullptr;

    if (!is_html_document) {
        // For non-HTML elements, load the default script.css
        char* css_filename = lambda_home_path("input/script.css");
        log_debug("[Lambda Script] Loading default script stylesheet: %s", css_filename);

        char* css_content = read_text_file(css_filename);
        if (css_content) {
            size_t css_len = strlen(css_content);
            char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
            if (css_pool_copy) {
                str_copy(css_pool_copy, css_len + 1, css_content, css_len);
                mem_free(css_content);
                script_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
                if (script_stylesheet) {
                    log_debug("[Lambda Script] Loaded script stylesheet with %zu rules",
                            script_stylesheet->rule_count);
                } else {
                    log_warn("[Lambda Script] Failed to parse script.css");
                }
            } else {
                mem_free(css_content);
            }
        } else {
            log_debug("[Lambda Script] No script.css file found, using browser defaults");
        }
        mem_free(css_filename);
    } else {
        // For complete HTML documents, extract inline <style> elements
        log_debug("[Lambda Script] Skipping script.css for complete HTML document");
        log_debug("[Lambda Script] Extracting inline <style> elements...");
        inline_stylesheets = extract_and_collect_css(
            html_elem, css_engine, script_filepath, pool, &inline_stylesheet_count);
        log_debug("[Lambda Script] Found %d inline stylesheet(s)", inline_stylesheet_count);
    }

    auto step6_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 6 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step6_end - step6_start).count());

    // Step 8: Apply CSS cascade to DOM tree
    auto step7_start = std::chrono::high_resolution_clock::now();
    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)dom_doc->state, matcher);

    // Apply script.css if loaded
    if (script_stylesheet && script_stylesheet->rule_count > 0) {
        apply_stylesheet_to_dom_tree_if_nonempty(dom_root, script_stylesheet, matcher, pool, css_engine);
    }

    // Apply inline stylesheets for HTML documents
    if (inline_stylesheets && inline_stylesheet_count > 0) {
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
                log_debug("[Lambda Script] Applying inline stylesheet %d with %zu rules",
                          i, inline_stylesheets[i]->rule_count);
                apply_stylesheet_to_dom_tree_if_nonempty(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
            }
        }
    }

    auto step7_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 7 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step7_end - step7_start).count());

    // Step 9: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = html_elem;
    dom_doc->html_version = HTML5;
    dom_doc->url = script_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;

    int total_stylesheets = inline_stylesheet_count + (script_stylesheet ? 1 : 0);
    if (total_stylesheets > 0) {
        dom_doc->stylesheet_capacity = total_stylesheets;
        dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, total_stylesheets * sizeof(CssStylesheet*));
        dom_doc->stylesheet_count = 0;
        if (script_stylesheet) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = script_stylesheet;
        }
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i]) {
                dom_doc->stylesheets[dom_doc->stylesheet_count++] = inline_stylesheets[i];
            }
        }
        dom_doc->cached_inline_sheets = inline_stylesheets;
        dom_doc->cached_inline_sheet_count = inline_stylesheet_count;
        dom_doc->cached_css_engine = css_engine;
        log_debug("[Lambda Script] Stored %d stylesheet(s) in DomDocument", dom_doc->stylesheet_count);
    }

    // Register doc root for render map parent fixup during retransform
    Item html_item_root = {.element = html_elem};
    render_map_set_doc_root(html_item_root);

    // The retained context above is stack-local and only exists to let the
    // post-evaluation setup register Lambda roots. Event execution restores a
    // fresh context from dom_doc->lambda_runtime, so clear the thread-local now.
    context = nullptr;
    input_context = nullptr;

    // Store the retained runtime on the document for event handler execution.
    // The GC heap, JIT code, and name pool remain live for the session.
    dom_doc->lambda_runtime = runtime;

    // dom_doc->input already set to result_input by dom_document_create
    // result_input has ui_mode=true and its arena holds the fat DomElement/DomText nodes

    // Note: Don't cleanup runtime — heap and JIT context still in use for reactive UI

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_lambda_script_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    log_notice("[Lambda Script] Script document loaded and styled");
    return dom_doc;
}

DomDocument* load_lambda_script_doc(Url* script_url, int viewport_width, int viewport_height, Pool* pool) {
    return load_lambda_script_source_doc(script_url, nullptr, viewport_width, viewport_height, pool);
}

// ============================================================================
// Reactive UI: rebuild DOM after template handler modifies state
// ============================================================================

/**
 * Rebuild the DOM tree from the Lambda html_root element after a template
 * handler has modified state and render_map_retransform() has updated the
 * Lambda element tree. Rebuilds DOM, re-applies CSS, and triggers relayout.
 */

// helper: walk view tree to find a form text input matching tag + class
static View* find_matching_input(View* root, const char* match_tag, const char* match_class) {
    if (!root) return nullptr;
    if (root->is_element()) {
        DomElement* elem = lam::dom_require_element(root);
        if (elem->item_prop_type == DomElement::ITEM_PROP_FORM &&
            elem->form &&
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
        // recurse into children
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
    if (focused_elem->item_prop_type != DomElement::ITEM_PROP_FORM ||
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
            if (elem->native_element) {
                Item item = {.element = elem->native_element};
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

void rebuild_lambda_doc(UiContext* uicon) {
    if (!uicon || !uicon->document) {
        log_error("rebuild_lambda_doc: no document");
        return;
    }

    DomDocument* doc = uicon->document;
    Element* html_elem = doc->html_root;

    // sync html_root with render_map's doc_root (may have been updated by retransform)
    Item current_doc_root = render_map_get_doc_root();
    if (current_doc_root.item && current_doc_root.element != html_elem) {
        html_elem = current_doc_root.element;
        doc->html_root = html_elem;
        log_debug("rebuild_lambda_doc: synced html_root from render_map doc_root");
    }

    if (!html_elem) {
        log_error("rebuild_lambda_doc: no html_root in document");
        return;
    }

    log_debug("rebuild_lambda_doc: rebuilding DOM from updated Lambda elements");

    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // Save focus info before rebuild so we can restore it on the new tree
    DocState* state = (DocState*)doc->state;
    LambdaFocusRestore focus_restore;
    bool had_focus = capture_lambda_focus_restore(state, &focus_restore);

    // ensure CSS property system is initialized
    css_property_system_init(doc->pool);

    // create/reset element-to-DOM map (enables incremental rebuild next time)
    if (!doc->element_dom_map) {
        doc->element_dom_map = element_dom_map_create();
    } else {
        hashmap_clear(doc->element_dom_map, false);
    }

    // rebuild DOM tree from Lambda elements
    DomElement* new_root = build_dom_tree_from_element(html_elem, doc, nullptr);
    if (!new_root) {
        log_error("rebuild_lambda_doc: failed to rebuild DOM tree");
        return;
    }
    auto t_dom = high_resolution_clock::now();

    // replace old DOM root
    doc->root = new_root;

    // apply cached CSS stylesheets (parse once, reuse on subsequent rebuilds)
    CssStylesheet** inline_sheets = doc->cached_inline_sheets;
    int inline_count = doc->cached_inline_sheet_count;
    CssEngine* css_engine = (CssEngine*)doc->cached_css_engine;

    if (!inline_sheets) {
        // first rebuild: parse and cache stylesheets
        css_engine = css_engine_create(doc->pool);
        if (css_engine) {
            inline_sheets = extract_and_collect_css(
                html_elem, css_engine, nullptr, doc->pool, &inline_count);
            doc->cached_inline_sheets = inline_sheets;
            doc->cached_inline_sheet_count = inline_count;
            doc->cached_css_engine = css_engine;
            log_debug("rebuild_lambda_doc: cached %d inline stylesheet(s)", inline_count);
        }
    }

    if (css_engine && inline_sheets && inline_count > 0) {
        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        state_configure_selector_matcher((DocState*)doc->state, matcher);
        for (int i = 0; i < inline_count; i++) {
            apply_stylesheet_to_dom_tree_if_nonempty(new_root, inline_sheets[i],
                                                     matcher, doc->pool, css_engine);
        }
    }

    // apply inline style attributes
    apply_inline_styles_to_tree(new_root, html_elem, doc->pool);
    auto t_css = high_resolution_clock::now();

    // mark view tree dirty for full relayout
    doc->view_tree = nullptr;  // force full layout rebuild

    // trigger relayout + repaint
    layout_html_doc(uicon, doc, false);
    auto t_layout = high_resolution_clock::now();

    // Restore focus to matching element in new view tree
    View* restored_focus = restore_lambda_focus(doc, state, had_focus, &focus_restore);
    if (restored_focus) {
        log_debug("rebuild_lambda_doc: restored focus to new view %p (tag=%s class=%s)",
                  restored_focus,
                  focus_restore.fallback_tag ? focus_restore.fallback_tag : "",
                  focus_restore.fallback_class ? focus_restore.fallback_class : "");
    }

    // autofocus — if no focus was restored, scan the new tree for an autofocus input
    if (state && !focus_has_current(state) &&
        doc->view_tree && doc->view_tree->root) {
        View* af = find_matching_input(doc->view_tree->root, "input", nullptr);
        if (af && af->is_element()) {
            DomElement* af_elem = lam::dom_require_element(af);
            if (af_elem->has_attribute("autofocus")) {
                focus_set(state, af, false);
                state_store_caret_collapse_to_view_offset(state, af, 0);
                log_debug("rebuild_lambda_doc: autofocus set on new input");
            }
        }
    }

    // Skip render here — let the main loop handle it via render().
    // Full rebuild: mark dirty tracker for full repaint so the main loop does a full render.
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

// ============================================================================
// Reactive UI: incremental DOM rebuild (Phase 12)
// Only rebuilds changed DOM subtrees instead of the entire tree.
// Falls back to full rebuild when incremental update is not possible.
// ============================================================================

// Helper: compute absolute bounds of a DOM node by walking parent chain
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
    Element* html_elem = doc->html_root;

    // sync html_root with render_map's doc_root (may have been updated by retransform)
    Item current_doc_root = render_map_get_doc_root();
    if (current_doc_root.item && current_doc_root.element != html_elem) {
        html_elem = current_doc_root.element;
        doc->html_root = html_elem;
        log_debug("rebuild_lambda_doc_incremental: synced html_root from render_map doc_root");
    }

    if (!html_elem) {
        log_error("rebuild_lambda_doc_incremental: no html_root in document");
        return;
    }

    // Determine if incremental update is feasible
    bool can_incremental = (doc->element_dom_map != nullptr) &&
                           (doc->root != nullptr) &&
                           (result_count > 0);

    // Verify all results have Element-typed old_result that exists in the map
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
            // Verify parent DOM is still valid (not GC-collected)
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
        // Fallback: full rebuild (rebuild_lambda_doc creates element_dom_map for next time)
        log_debug("rebuild_lambda_doc_incremental: falling back to full rebuild");
        rebuild_lambda_doc(uicon);
        return;
    }

    // --- Incremental path ---
    log_debug("rebuild_lambda_doc_incremental: patching %d subtree(s)", result_count);

    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // Save focus info
    DocState* state = (DocState*)doc->state;
    LambdaFocusRestore focus_restore;
    bool had_focus = capture_lambda_focus_restore(state, &focus_restore);

    // ensure CSS property system is initialized
    css_property_system_init(doc->pool);

    // Get cached CSS (must already exist from prior full rebuild)
    CssStylesheet** inline_sheets = doc->cached_inline_sheets;
    int inline_count = doc->cached_inline_sheet_count;
    CssEngine* css_engine = (CssEngine*)doc->cached_css_engine;

    // Phase 12.3: Record old bounds of changed subtrees for dirty tracking
    // (Views ARE DomNodes — x/y/w/h from previous layout pass)
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

    // Phase 12.1: Replace changed DOM subtrees
    for (int i = 0; i < result_count; i++) {
        Element* old_elem = results[i].old_result.element;
        Element* new_elem = results[i].new_result.element;

        DomElement* old_dom = element_dom_map_lookup(doc->element_dom_map, old_elem);
        if (!old_dom || !old_dom->parent) {
            log_debug("rebuild_lambda_doc_incremental: entry %d has no DOM parent, skipping", i);
            continue;
        }
        DomElement* parent_dom = lam::dom_require_element(old_dom->parent);

        // Build new DOM subtree from the new Lambda element (not linked to parent yet)
        DomElement* new_dom = build_dom_tree_from_element(new_elem, doc, nullptr);

        if (!new_dom) {
            log_debug("rebuild_lambda_doc_incremental: failed to build subtree for entry %d", i);
            continue;
        }

        // Replace old DOM child with new subtree in parent's linked list
        dom_node_replace_in_parent(parent_dom, static_cast<DomNode*>(old_dom), static_cast<DomNode*>(new_dom));
        if (i < 16) new_doms[i] = new_dom;

        // Phase 16: Mark new subtree as layout_dirty
        set_layout_dirty_subtree(static_cast<DomNode*>(new_dom), true);

        // Phase 15: Invalidate ancestor styles only (new subtree nodes already have styles_resolved=false)
        // Phase 16: Mark ancestors as layout_dirty for incremental layout
        DomNode* ancestor = static_cast<DomNode*>(parent_dom);
        while (ancestor) {
            if (ancestor->is_element()) {
                (lam::dom_require_element(ancestor))->styles_resolved = false;
            }
            ancestor->layout_dirty = true;
            ancestor = ancestor->parent;
        }

        // Phase 12.2: Apply CSS cascade to new subtree only
        if (css_engine && inline_sheets && inline_count > 0) {
            SelectorMatcher* matcher = selector_matcher_create(doc->pool);
            state_configure_selector_matcher((DocState*)doc->state, matcher);
            for (int s = 0; s < inline_count; s++) {
                apply_stylesheet_to_dom_tree_if_nonempty(new_dom, inline_sheets[s],
                                                         matcher, doc->pool, css_engine);
            }
        }

        // Apply inline styles to new subtree only
        apply_inline_styles_to_tree(new_dom, new_elem, doc->pool);
    }
    auto t_dom_css = high_resolution_clock::now();

    // Reuse ViewTree — Phase 16: incremental layout preserves pool for clean elements
    if (doc->view_tree) {
        // Phase 16: DON'T destroy pool — preserve BoundaryProp etc. for unchanged elements
        // view_pool_destroy(doc->view_tree);  // disabled for incremental layout
        doc->incremental_layout = true;
        // Phase 15: Skip blanket reset_styles_resolved — only ancestors + new nodes need re-resolution
        doc->skip_style_reset = true;
        layout_html_doc(uicon, doc, true);
        doc->skip_style_reset = false;
        doc->incremental_layout = false;
        // Phase 16: Clear layout_dirty flags for next pass
        if (doc->root) set_layout_dirty_subtree(static_cast<DomNode*>(doc->root), false);
    } else {
        layout_html_doc(uicon, doc, false);
    }
    auto t_layout = high_resolution_clock::now();

    // Phase 12.3: Compute dirty rects from old/new bounds
    if (state) {
        dirty_clear(&state->dirty_tracker);

        // Template retransform can affect any part of the page (counters, headers, etc.),
        // not just the replaced subtree.  Always do a full repaint for correctness.
        state->dirty_tracker.full_repaint = true;
        log_debug("rebuild_incr dirty: full repaint (template retransform)");
    }

    // Restore focus
    if (restore_lambda_focus(doc, state, had_focus, &focus_restore)) {
        log_debug("rebuild_lambda_doc_incremental: restored focus");
    }

    // Phase 20: autofocus — if no focus was restored and new subtree contains an input,
    // check for autofocus attribute and set focus to it
    if (state && !focus_has_current(state)) {
        for (int i = 0; i < result_count; i++) {
            if (new_doms[i]) {
                View* af = find_matching_input(static_cast<View*>(new_doms[i]), "input", nullptr);
                if (af && af->is_element()) {
                    DomElement* af_elem = lam::dom_require_element(af);
                    if (af_elem->has_attribute("autofocus")) {
                        focus_set(state, af, false);
                        state_store_caret_collapse_to_view_offset(state, af, 0);
                        log_debug("rebuild_incr: autofocus set on new input");
                        break;
                    }
                }
            }
        }
    }

    // Skip render here — let the main loop handle it via render().
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

/**
 * Parse command-line arguments
 */
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
    bool auto_close;                            // cancel async JS timers after load/onload
};

bool parse_layout_args(int argc, char** argv, LayoutOptions* opts) {
    // Initialize defaults
    opts->input_file_count = 0;
    opts->output_file = nullptr;
    opts->output_dir = nullptr;
    opts->css_file = nullptr;
    opts->view_output_file = nullptr;  // Default to ./temp/view_tree.json
    opts->font_dir_count = 0;
    opts->viewport_width = 1200;  // Standard viewport width for layout tests (matches browser reference)
    opts->viewport_height = 800;  // Standard viewport height for layout tests (matches browser reference)
    opts->debug = false;
    opts->event_log = false;
    opts->state_dump = false;
    opts->continue_on_error = false;
    opts->summary = false;
    opts->timing_output_file = nullptr;
    opts->auto_close = false;

    // Parse arguments
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                opts->output_file = argv[++i];
            } else {
                log_error("Error: -o requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 < argc) {
                opts->output_dir = argv[++i];
            } else {
                log_error("Error: --output-dir requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "--view-output") == 0) {
            if (i + 1 < argc) {
                opts->view_output_file = argv[++i];
            } else {
                log_error("Error: --view-output requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--css") == 0) {
            if (i + 1 < argc) {
                opts->css_file = argv[++i];
            } else {
                log_error("Error: -c requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "-vw") == 0 || strcmp(argv[i], "--viewport-width") == 0) {
            if (i + 1 < argc) {
                i++;
                opts->viewport_width = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
            } else {
                log_error("Error: -vw/--viewport-width requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "-vh") == 0 || strcmp(argv[i], "--viewport-height") == 0) {
            if (i + 1 < argc) {
                i++;
                opts->viewport_height = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
            } else {
                log_error("Error: -vh/--viewport-height requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            opts->debug = true;
        }
        else if (strcmp(argv[i], "--event-log") == 0) {
            opts->event_log = true;
        }
        else if (strcmp(argv[i], "--state-dump") == 0) {
            opts->state_dump = true;
        }
        else if (strcmp(argv[i], "--continue-on-error") == 0) {
            opts->continue_on_error = true;
        }
        else if (strcmp(argv[i], "--summary") == 0) {
            opts->summary = true;
        }
        else if (strcmp(argv[i], "--auto-close") == 0) {
            opts->auto_close = true;
        }
        else if (strcmp(argv[i], "--timing-output") == 0) {
            if (i + 1 < argc) {
                opts->timing_output_file = argv[++i];
            } else {
                log_error("Error: --timing-output requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "--font-dir") == 0) {
            if (i + 1 < argc) {
                if (opts->font_dir_count < 16) {
                    opts->font_dirs[opts->font_dir_count++] = argv[++i];
                } else {
                    log_error("Error: too many --font-dir options (max 16)");
                    return false;
                }
            } else {
                log_error("Error: --font-dir requires an argument");
                return false;
            }
        }
        else if (argv[i][0] != '-') {
            // Collect all non-option arguments as input files
            if (opts->input_file_count < MAX_INPUT_FILES) {
                opts->input_files[opts->input_file_count++] = argv[i];
            } else {
                log_error("Error: too many input files (max %d)", MAX_INPUT_FILES);
                return false;
            }
        }
    }

    if (opts->input_file_count == 0) {
        log_error("Error: at least one input file required");
        log_error("Usage: lambda layout <input.html> [input2.html ...] [options]");
        return false;
    }

    // Batch mode requires --output-dir
    if (opts->input_file_count > 1 && !opts->output_dir) {
        log_error("Error: batch mode (multiple input files) requires --output-dir");
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

/**
 * Load and layout a single document file.
 * Returns true on success, false on failure.
 * Uses shared UiContext for batch efficiency.
 */
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
    bool auto_close = false
) {
    log_debug("[Layout] Processing file: %s", input_file);
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

    // Create memory pool for this file
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "cmd_layout");
    if (!pool) {
        log_error("Failed to create memory pool for %s", input_file);
        return false;
    }
    js_mir_begin_document_phase_timing();

    bool previous_auto_close_mode = js_event_loop_auto_close_mode();
    js_event_loop_set_auto_close_mode(auto_close);

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

    // For HTTP URLs without clear extension, fetch and determine type from Content-Type
    const char* effective_ext = nullptr;
    bool is_http_url = (input_url->scheme == URL_SCHEME_HTTP || input_url->scheme == URL_SCHEME_HTTPS);

    // Detect file type by extension and load appropriate document
    DomDocument* doc = nullptr;
    const char* ext = strrchr(input_file, '.');

    // Check if extension looks valid (not just a domain TLD like .org, .com, etc.)
    bool has_valid_ext = ext && (
        strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0 ||
        strcmp(ext, ".ls") == 0 || strcmp(ext, ".tex") == 0 ||
        strcmp(ext, ".latex") == 0 || strcmp(ext, ".md") == 0 ||
        strcmp(ext, ".markdown") == 0 || strcmp(ext, ".xml") == 0 ||
        strcmp(ext, ".wiki") == 0 || strcmp(ext, ".pdf") == 0 ||
        strcmp(ext, ".mmd") == 0 || strcmp(ext, ".d2") == 0 ||
        strcmp(ext, ".dot") == 0 || strcmp(ext, ".gv") == 0 ||
        strcmp(ext, ".svg") == 0 || strcmp(ext, ".png") == 0 ||
        strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
        strcmp(ext, ".gif") == 0
    );

    // If HTTP URL with no valid extension, fetch to determine type
    if (is_http_url && !has_valid_ext) {
        const char* url_str = url_get_href(input_url);
        log_info("[Layout] HTTP URL without extension, fetching to determine type: %s", url_str);

        FetchResponse* response = http_fetch(url_str, nullptr);
        if (!response || !response->data || response->status_code >= 400) {
            log_error("Failed to fetch URL: %s (HTTP %ld)", url_str,
                      response ? response->status_code : 0);
            if (response) free_fetch_response(response);
            if (event_log) {
                event_state_log_document(event_log, "load_failed");
                event_state_log_close(event_log);
            }
            if (state_dump) {
                radiant_state_dump_close(state_dump);
                state_dump = nullptr;
            }
            js_event_loop_set_auto_close_mode(previous_auto_close_mode);
            pool_destroy(pool);
            return false;
        }

        // Get extension from Content-Type
        effective_ext = content_type_to_extension(response->content_type);
        log_info("[Layout] HTTP Content-Type: %s -> extension: %s",
                 response->content_type ? response->content_type : "(none)",
                 effective_ext ? effective_ext : ".html");

        // Use the effective extension for routing
        ext = effective_ext;

        free_fetch_response(response);
    }

    if (ext && strcmp(ext, ".ls") == 0) {
        log_info("[Layout] Detected Lambda script file, using script evaluation pipeline");
        doc = load_lambda_script_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".mmd") == 0 || strcmp(ext, ".d2") == 0 ||
                       strcmp(ext, ".dot") == 0 || strcmp(ext, ".gv") == 0)) {
        log_info("[Layout] Detected graph file, using Lambda graph package pipeline");
        doc = load_graph_bridge_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0)) {
        // Load LaTeX document via LaTeX→HTML pipeline
        log_info("[Layout] Detected LaTeX file, using LaTeX→HTML pipeline");
        doc = load_latex_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0)) {
        log_info("[Layout] Detected Markdown file, using Markdown→HTML pipeline");
        doc = load_markdown_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".wiki") == 0) {
        log_info("[Layout] Detected Wiki file, using Wiki→HTML pipeline");
        doc = load_wiki_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".xml") == 0) {
        log_info("[Layout] Detected XML file, using XML→DOM pipeline");
        doc = load_xml_doc(input_url, viewport_width, viewport_height, pool);
    } else if (ext && strcmp(ext, ".pdf") == 0) {
        log_error("[Layout] Direct PDF layout was retired; use lambda view PDF conversion");
        doc = nullptr;
    } else if (ext && strcmp(ext, ".svg") == 0) {
        log_info("[Layout] Detected SVG file, using SVG→ViewTree pipeline");
        doc = load_svg_doc(input_url, viewport_width, viewport_height, pool, 1.0f);
    } else {
        const int max_redirects = 8;
        for (int redirect_count = 0; redirect_count <= max_redirects; redirect_count++) {
            // layout CLI only needs load-time DOM mutations; event dispatch is not used
            script_runner_set_retain_js_state(false);
            script_runner_set_execute_external_scripts(true);
            doc = load_lambda_html_doc_profiled(input_url, css_file, viewport_width,
                                                viewport_height, pool, nullptr,
                                                track_source_lines, true,
                                                timing_file ? &html_load_timing : nullptr,
                                                timing_file ? &document_script_timing : nullptr);
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
                js_event_loop_set_auto_close_mode(previous_auto_close_mode);
                break;
            }
        }
    }
    js_event_loop_set_auto_close_mode(previous_auto_close_mode);

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
        if (event_log) {
            event_state_log_document(event_log, "load_failed");
            event_state_log_close(event_log);
        }
        if (state_dump) {
            radiant_state_dump_close(state_dump);
            state_dump = nullptr;
        }
        pool_destroy(pool);
        return false;
    }
    load_end = std::chrono::high_resolution_clock::now();
    js_mir_end_document_phase_timing(&document_js_timing);

    ui_context->document = doc;

    DocState* state = radiant_document_ensure_state(doc, "layout_single_file");
    if (!state) {
        log_error("Failed to create DocState for headless document: %s", input_file);
        if (event_log) {
            event_state_log_document(event_log, "load_failed");
            event_state_log_close(event_log);
        }
        if (state_dump) {
            radiant_state_dump_close(state_dump);
            state_dump = nullptr;
        }
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

    // Process @font-face rules from stored stylesheets
    process_document_font_faces(ui_context, doc);
    EnhancedFileCache* layout_file_cache = layout_prepare_network_resources(ui_context, doc);

    // Perform layout computation
    if (doc->view_tree && doc->view_tree->root) {
        log_info("[Layout] Document already has view_tree (PDF/SVG/image), skipping CSS layout");
    } else {
        log_debug("[Layout] About to call layout_html_doc...");
        auto event_layout_start = std::chrono::high_resolution_clock::now();
        layout_start = event_layout_start;
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
        log_debug("[Layout] layout_html_doc returned");
    }

    bool success = true;
    if (!doc->view_tree || !doc->view_tree->root) {
        log_warn("Layout computation did not produce view tree for %s", input_file);
        success = false;
    } else {
        log_info("[Layout] Layout computed successfully for %s", input_file);

        // For PDF/SVG documents, disable text node combination
        bool is_pdf = ext && strcmp(ext, ".pdf") == 0;
        bool is_svg = ext && strcmp(ext, ".svg") == 0;
        if (is_pdf || is_svg) {
            set_combine_text_nodes(false);
        }

        output_start = std::chrono::high_resolution_clock::now();
        print_view_tree(lam::unsafe_view_element_storage(doc->view_tree->root), doc->url, output_path);
        output_end = std::chrono::high_resolution_clock::now();
        output_phase_ran = true;
        log_debug("[Layout] Layout tree written to %s", output_path ? output_path : "./temp/view_tree.json");

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

    // Cleanup: destroy view tree, document, and per-file CSS pool.
    // DomDocument and ViewTree are malloc-allocated with their own internal pools.
    // Failing to free them leaks pools/arenas across batch files, which can cause
    // rpmalloc heap corruption that manifests as SIGTRAP in system malloc.

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
        // Clean up retained JS state (MIR context, event registry, runtime heap)
        // before destroying the document that owns the pointers.
        script_runner_cleanup_js_state(doc);

        radiant_document_destroy_state(doc);

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
    source_pos_bridge_reset();
    render_map_destroy();

    if (event_log) {
        event_state_log_document(event_log, "unload_complete");
        event_state_log_close(event_log);
        event_log = nullptr;
    }
    if (state_dump) {
        radiant_state_dump_close(state_dump);
        state_dump = nullptr;
    }

    // Free the input URL (dom_document_destroy doesn't own it). Some loaders
    // (markdown/latex/wiki/xml/script) pass input_url straight into
    // input_from_source, so a tracked Input ends up owning this exact pointer
    // (input->url == input_url). InputManager::destroy_global() below frees
    // every tracked input->url, so detach it from any owner first to avoid a
    // double-free. detach_url is doc-independent, so it also covers the case
    // where doc creation failed after the input was parsed.
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
        // Reset JS runtime state to avoid cross-document leakage in batch mode.
        // Must happen BEFORE script_runner_cleanup_heap: js_batch_reset clears
        // global Items (js_input, js_exception_value, etc.) that reference the
        // JS heap. Freeing the heap first would leave dangling pointers.
        js_batch_reset();
        js_dom_batch_reset();
        js_globals_batch_reset();

        // Drain the mmap pool from JS execution (after js_batch_reset cleared globals).
        script_runner_cleanup_heap();
    }
    lambda_uv_cleanup();

    // Reset per-document font state to avoid cross-document cache pollution in batch mode.
    fontface_cleanup(ui_context);
    font_context_reset_document_fonts(ui_context->font_ctx);
    font_context_reset_glyph_caches(ui_context->font_ctx);

    // [DIAG] Track memory usage per file for leak detection
    {
#ifdef __APPLE__
        // use mach_task_basic_info for CURRENT RSS (not peak)
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

    // Release decoded image cache to prevent batch accumulation.
    image_cache_cleanup(ui_context);

    // Release the InputManager's global pool which accumulates all Input parse trees.
    InputManager::destroy_global();

    // Reset ui_context document pointer to avoid dangling pointer in batch mode
    ui_context->document = nullptr;

    return success;
}

/**
 * Generate output path for batch mode.
 * Returns allocated string that caller must free.
 */
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

/**
 * Main layout command implementation using Lambda CSS and Radiant layout.
 * Supports both single-file and batch modes.
 */
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
    // Install crash signal handlers for diagnostics
#ifndef _WIN32
    signal(SIGTRAP, crash_signal_handler);
#endif
    signal(SIGABRT, crash_signal_handler);
#ifndef _WIN32
    // SIGSEGV/SIGBUS use sigaction for crash recovery (siglongjmp) support
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = layout_crash_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }
#endif

    // Initialize logging system (only write log.txt when log.conf exists, i.e. dev/debug mode)
    if (file_exists("log.conf")) {
        FILE *file = fopen("log.txt", "w");
        if (file) { fclose(file); }
    }
    log_parse_config_file("log.conf");

    // Parse command-line options
    LayoutOptions opts;
    if (!parse_layout_args(argc, argv, &opts)) {
        return 1;
    }

    bool batch_mode = (opts.input_file_count > 1) || (opts.output_dir != nullptr);
    bool auto_close = opts.auto_close || shell_getenv("LAMBDA_AUTO_CLOSE") != nullptr;

    // Disable all logging in batch mode for better performance
    if (batch_mode) {
        log_disable_all();
    }

    log_debug("Lambda Layout Command");
    log_debug("  Mode: %s", batch_mode ? "batch" : "single");
    log_debug("  Input files: %d", opts.input_file_count);
    if (opts.input_file_count > 0) {
        log_debug("  First input: %s", opts.input_files[0]);
    }
    log_debug("  Output dir: %s", opts.output_dir ? opts.output_dir : "(none)");
    log_debug("  CSS: %s", opts.css_file ? opts.css_file : "(inline only)");
    log_debug("  Viewport: %dx%d", opts.viewport_width, opts.viewport_height);
    log_debug("  Auto-close: %s", auto_close ? "yes" : "no");

    // Initialize UI context once (shared across all files in batch mode)
    log_debug("[Layout] Initializing UI context (headless mode)...");
    UiContext ui_context;
    memset(&ui_context, 0, sizeof(UiContext));

    if (ui_context_init(&ui_context, true) != 0) {
        log_error("Failed to initialize UI context");
        return 1;
    }
    // Batch files share immutable JS code, but every document instantiates it
    // into a fresh heap and DOM realm. The batch owns the cache lifetime.
    // The disable switch keeps an identical uncached path available for
    // differential verification and performance attribution.
    bool js_mir_cache_enabled = batch_mode &&
        shell_getenv("LAMBDA_DISABLE_JS_MIR_CACHE") == nullptr;
    JsMirCache* js_mir_cache = js_mir_cache_enabled ? js_mir_cache_create() : nullptr;
    if (js_mir_cache_enabled && !js_mir_cache) {
        log_error("layout_js_mir_cache: failed to create batch cache; continuing uncached");
    }
    script_runner_set_js_mir_cache(js_mir_cache);

    // Add custom font scan directories (must be done before any font resolution)
    for (int i = 0; i < opts.font_dir_count; i++) {
        font_context_add_scan_directory(ui_context.font_ctx, opts.font_dirs[i]);
        log_debug("[Layout] Added font directory: %s", opts.font_dirs[i]);
    }

    // Set viewport dimensions (both window and viewport must be set so layout_init
    // uses the correct initial containing block width via uicon->viewport_width)
    ui_context.window_width = opts.viewport_width;
    ui_context.window_height = opts.viewport_height;
    ui_context.viewport_width = opts.viewport_width;
    ui_context.viewport_height = opts.viewport_height;

    // Create surface for layout calculations
    log_debug("[Layout] Creating surface for layout calculations...");
    ui_context_create_surface(&ui_context, opts.viewport_width, opts.viewport_height);
    log_debug("[Layout] Surface created");

    // Get current working directory
    Url* cwd = get_current_dir();

    FILE* timing_file = nullptr;
    if (opts.timing_output_file) {
        timing_file = fopen(opts.timing_output_file, "w");
        if (!timing_file) {
            log_error("layout_timing_output: failed to open %s", opts.timing_output_file);
        }
    }

    // Track statistics for batch mode
    int success_count = 0;
    int failure_count = 0;
    auto batch_start = std::chrono::high_resolution_clock::now();

    // Process all input files
    for (int i = 0; i < opts.input_file_count; i++) {
        const char* input_file = opts.input_files[i];
        const char* output_path = nullptr;
        char* allocated_output = nullptr;

        if (batch_mode && opts.output_dir) {
            // Generate output path for batch mode
            allocated_output = generate_output_path(input_file, opts.output_dir);
            output_path = allocated_output;
        } else {
            // Single file mode: use view_output_file if specified
            output_path = opts.view_output_file;
        }

        bool success = false;
#ifdef _WIN32
        // Windows: no sigsetjmp/siglongjmp crash recovery — run directly
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
                auto_close
            );
        } catch (...) {
            log_error("batch layout: uncaught exception processing %s", input_file);
            success = false;
        }
#else
        // Guard layout with crash recovery (catches SIGSEGV/SIGBUS from Apple frameworks)
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
                    auto_close
                );
            } catch (...) {
                log_error("batch layout: uncaught exception processing %s", input_file);
                success = false;
            }
            layout_crash_guarded = 0;
        } else {
            // After SIGSEGV/SIGBUS recovery via siglongjmp, the process state
            // (heap, allocator, caches) is likely corrupted. It is NOT safe to
            // continue processing more files — exit immediately with code 1
            // (graceful failure) instead of 128+sig (crash).
            fprintf(stderr, "layout: recovered from signal %d processing %s — exiting\n",
                    crash_sig, input_file);
            _exit(1);
        }
#endif

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

    // Print summary if requested or in batch mode
    if (opts.summary || (batch_mode && opts.input_file_count > 1)) {
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

    // Cleanup
    if (timing_file) {
        fclose(timing_file);
        timing_file = nullptr;
    }
    script_runner_set_js_mir_cache(nullptr);
    js_mir_cache_destroy(js_mir_cache);
    log_debug("[Cleanup] Starting cleanup...");
    log_debug("[Cleanup] Cleaning up UI context...");
    ui_context_cleanup(&ui_context);
    // cwd is the base URL passed (by pointer, not owned) into each
    // layout_single_file call; cmd_layout owns it and must free it here.
    if (cwd) url_destroy(cwd);
    log_debug("[Cleanup] Complete");

    printf("Completed layout command: %d success, %d failed\n", success_count, failure_count);
    log_notice("Completed layout command: %d success, %d failed", success_count, failure_count);
    return failure_count > 0 ? 1 : 0;
}
