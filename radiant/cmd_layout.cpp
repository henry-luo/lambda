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
 *   --flavor FLAVOR                LaTeX rendering pipeline: latex-js (default), tex-proper
 */

#include <stdio.h>
#include <string.h>
#include "../lib/mem.h"
#include <chrono>       // timing - acceptable for profiling
#include <limits.h>
#include <signal.h>
#include <setjmp.h>
#include <execinfo.h>
#include <unistd.h>
#include <sys/resource.h>  // getrusage for memory diagnostics
#ifdef __APPLE__
#include <mach/mach.h>     // mach_task_basic_info for current RSS
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
#include "../lambda/mark_builder.hpp"
#include "../radiant/view.hpp"
#include "../radiant/rdt_vector.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/font_face.h"
#include "../radiant/state_store.hpp"
#include "../radiant/form_control.hpp"
#include "../radiant/pdf/pdf_to_view.hpp"
#include "../radiant/script_runner.h"
#include "../lambda/render_map.h"
#include "../lambda/template_state.h"

// External C++ function declarations from Radiant
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);

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
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine, int depth = 0);
void apply_stylesheet_to_dom_tree_fast(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
static void apply_rule_to_dom_element(DomElement* elem, CssRule* rule, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count, int* linked_count_out = nullptr);
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool, CssStylesheet*** stylesheets, int* count);
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count);
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool);
static const int MAX_CSS_TREE_DEPTH = 512;

// Current document charset for CSS fallback encoding (set before collect_linked_stylesheets)
const char* g_css_document_charset = nullptr;

// Forward declaration for charset conversion (defined after convert_latin1_to_utf8)
char* convert_charset_to_utf8(const char* content, size_t content_len, const char* from_charset);
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool, int depth = 0);
void log_root_item(Item item, char* indent="  ");
DomDocument* load_latex_doc(Url* latex_url, int viewport_width, int viewport_height, Pool* pool);

DomDocument* load_lambda_script_doc(Url* script_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_xml_doc(Url* xml_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_pdf_doc(Url* pdf_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio = 1.0f);
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio = 1.0f);
DomDocument* load_image_doc(Url* img_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio = 1.0f);
DomDocument* load_text_doc(Url* text_url, int viewport_width, int viewport_height, Pool* pool);
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
void parse_pdf(Input* input, const char* pdf_data, size_t pdf_length);  // From input-pdf.cpp
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena);
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);

// Element-to-DOM map functions (from dom_element.cpp, Phase 12)
HashMap* element_dom_map_create(void);
void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem);
DomElement* element_dom_map_lookup(HashMap* map, Element* elem);
bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child);

// View pool management (from view_pool.cpp)
void view_pool_destroy(ViewTree* tree);

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
        List* root_list = input->root.list;
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
                                while (*after_html && isspace(*after_html)) {
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

            DomElement* dom_child_elem = (DomElement*)dom_child;

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
        List* root_list = input->root.list;
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
                    float scale_y = scale_x;  // uniform scale
                    if (func->arg_count >= 2 && func->args[1] && func->args[1]->type == CSS_VALUE_TYPE_NUMBER) {
                        scale_y = (float)func->args[1]->data.number.value;
                    }
                    // Return uniform scale (use x as primary)
                    log_info("[transform] Found scale(%.3f, %.3f)", scale_x, scale_y);
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
                DomElement* child_elem = static_cast<DomElement*>(child);
                if (child_elem->tag_name && str_ieq_const(child_elem->tag_name, strlen(child_elem->tag_name), "body")) {
                    body_elem = child_elem;
                    break;
                }
                // Also check one level deeper (html > head, body)
                for (DomNode* grandchild = child_elem->first_child; grandchild; grandchild = grandchild->next_sibling) {
                    if (grandchild->node_type == DOM_NODE_ELEMENT) {
                        DomElement* grandchild_elem = static_cast<DomElement*>(grandchild);
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
static void resolve_stylesheet_imports(CssStylesheet* stylesheet, const char* stylesheet_path,
                                        CssEngine* engine, Pool* pool,
                                        CssStylesheet*** stylesheets, int* count, int depth) {
    if (!stylesheet || !engine || !pool || !stylesheets || !count) return;
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

        // Resolve import path relative to the stylesheet
        char import_path[1024];
        if (strstr(import_url, "://") != nullptr) {
            // Absolute URL
            strncpy(import_path, import_url, sizeof(import_path) - 1);
            import_path[sizeof(import_path) - 1] = '\0';
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
            css_content = download_http_content(import_path, &content_size, nullptr);
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

        CssStylesheet* imported = css_parse_stylesheet(engine, css_pool_copy, import_path);
        if (imported && imported->rule_count > 0) {
            log_debug("[CSS @import] Parsed imported stylesheet '%s': %zu rules", import_path, imported->rule_count);

            *stylesheets = (CssStylesheet**)pool_realloc(pool, *stylesheets,
                                                          (*count + 1) * sizeof(CssStylesheet*));
            (*stylesheets)[*count] = imported;
            (*count)++;

            // Recursively resolve @imports within the imported stylesheet
            resolve_stylesheet_imports(imported, import_path, engine, pool, stylesheets, count, depth + 1);
        } else {
            log_warn("[CSS @import] Failed to parse imported stylesheet: %s", import_path);
        }
    }
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
static char* sanitize_utf8_css(const char* data, size_t len) {
    if (!data || len == 0) return nullptr;

    // first pass: check if sanitization is needed and compute output size
    bool needs_sanitize = false;
    size_t out_size = 0;
    for (size_t i = 0; i < len; ) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0x00) {
            // CSS §3.3: replace U+0000 NULL with U+FFFD
            needs_sanitize = true;
            out_size += 3;
            i++;
        } else if (c < 0x80) {
            out_size++;
            i++;
        } else if (c < 0xC0) {
            // unexpected continuation byte
            needs_sanitize = true;
            out_size += 3; // U+FFFD = EF BF BD
            i++;
        } else if (c < 0xE0) {
            if (i + 1 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80) {
                out_size += 2;
                i += 2;
            } else {
                needs_sanitize = true;
                out_size += 3;
                i++;
            }
        } else if (c < 0xF0) {
            if (i + 2 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+2] & 0xC0) == 0x80) {
                out_size += 3;
                i += 3;
            } else {
                needs_sanitize = true;
                out_size += 3;
                i++;
            }
        } else if (c < 0xF8) {
            if (i + 3 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+2] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+3] & 0xC0) == 0x80) {
                out_size += 4;
                i += 4;
            } else {
                needs_sanitize = true;
                out_size += 3;
                i++;
            }
        } else {
            needs_sanitize = true;
            out_size += 3;
            i++;
        }
    }

    if (!needs_sanitize) return nullptr;

    // second pass: build sanitized output
    char* out = (char*)mem_alloc(out_size + 1, MEM_CAT_LAYOUT);
    if (!out) return nullptr;
    size_t o = 0;
    for (size_t i = 0; i < len; ) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0x00) {
            out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
            i++;
        } else if (c < 0x80) {
            out[o++] = data[i++];
        } else if (c < 0xC0) {
            out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
            i++;
        } else if (c < 0xE0) {
            if (i + 1 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80) {
                out[o++] = data[i++]; out[o++] = data[i++];
            } else {
                out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
                i++;
            }
        } else if (c < 0xF0) {
            if (i + 2 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+2] & 0xC0) == 0x80) {
                out[o++] = data[i++]; out[o++] = data[i++]; out[o++] = data[i++];
            } else {
                out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
                i++;
            }
        } else if (c < 0xF8) {
            if (i + 3 < len && ((unsigned char)data[i+1] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+2] & 0xC0) == 0x80 &&
                ((unsigned char)data[i+3] & 0xC0) == 0x80) {
                out[o++] = data[i++]; out[o++] = data[i++]; out[o++] = data[i++]; out[o++] = data[i++];
            } else {
                out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
                i++;
            }
        } else {
            out[o++] = (char)0xEF; out[o++] = (char)0xBF; out[o++] = (char)0xBD;
            i++;
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
 * Recursively collect <link rel="stylesheet"> references from HTML
 * Loads and parses external CSS files
 */
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool, CssStylesheet*** stylesheets, int* count, int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;
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
                // Absolute local path - use as-is (only when base is not HTTP)
                strncpy(css_path, href, sizeof(css_path) - 1);
                css_path[sizeof(css_path) - 1] = '\0';
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

            log_debug("[CSS] Loading stylesheet from: %s", css_path);

            // Load and parse CSS file (or download from HTTP URL)
            char* css_content = nullptr;
            size_t css_file_size = 0;
            if (is_http_css) {
                // Download CSS from HTTP URL
                size_t content_size = 0;
                css_content = download_http_content(css_path, &content_size, nullptr);
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
            if (css_content) {
                // Use binary size when available (handles null bytes); fallback to strlen
                size_t css_len = css_file_size > 0 ? css_file_size : strlen(css_content);

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

                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_pool_copy, css_path);
                    if (stylesheet && stylesheet->rule_count > 0) {
                        log_debug("[CSS] Parsed linked stylesheet '%s': %zu rules", css_path, stylesheet->rule_count);

                        // Add to list
                        *stylesheets = (CssStylesheet**)pool_realloc(pool, *stylesheets,
                                                                      (*count + 1) * sizeof(CssStylesheet*));
                        (*stylesheets)[*count] = stylesheet;
                        (*count)++;

                        // Resolve @import rules within this stylesheet
                        resolve_stylesheet_imports(stylesheet, css_path, engine, pool, stylesheets, count, 0);
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
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_linked_stylesheets(child_item.element, engine, base_path, pool, stylesheets, count, depth + 1);
        }
    }
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and returns list of stylesheets
 */
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count, int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;
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
        // Extract text content from style element
        for (int64_t i = 0; i < elem->length; i++) {
            Item child_item = elem->items[i];
            int type_id = get_type_id(child_item);
            log_debug("[CSS] <style> child[%lld] type_id=%d", i, type_id);

            if (type_id == LMD_TYPE_STRING) {
                String* css_text = (String*)child_item.string_ptr;
                log_debug("[CSS] Found STRING child: ptr=%p, len=%d", (void*)css_text, css_text ? css_text->len : -1);
                if (css_text && css_text->len > 0) {
                    // Log first non-whitespace content
                    const char* content = css_text->chars;
                    while (*content && (*content == ' ' || *content == '\n' || *content == '\t' || *content == '\r')) {
                        content++;
                    }
                    // log_debug("[CSS CONTENT] After whitespace: %.200s", content);

                    // Parse the inline CSS
                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_text->chars, "<inline-style>");
                    if (stylesheet && stylesheet->rule_count > 0) {
                        log_debug("[CSS] Parsed inline <style>: %zu rules", stylesheet->rule_count);

                        // Add to list
                        *stylesheets = (CssStylesheet**)pool_realloc(pool, *stylesheets,
                                                                      (*count + 1) * sizeof(CssStylesheet*));
                        (*stylesheets)[*count] = stylesheet;
                        (*count)++;
                    }
                }
            }
        }
        } // end media check else
    }

    // Recursively process children
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_inline_styles_to_list(child_item.element, engine, pool, stylesheets, count, depth + 1);
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
void collect_inline_styles_from_dom(DomElement* elem, CssEngine* engine, Pool* pool,
                                     CssStylesheet*** stylesheets, int* count, int depth = 0) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;
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
                        DomText* text_node = (DomText*)child;
                        if (text_node->text && text_node->length > 0) {
                            CssStylesheet* stylesheet = css_parse_stylesheet(engine, text_node->text, "<inline-style>");
                            if (stylesheet && stylesheet->rule_count > 0) {
                                log_debug("[CSS] Re-scan: parsed <style> from DOM: %zu rules", stylesheet->rule_count);
                                *stylesheets = (CssStylesheet**)pool_realloc(pool, *stylesheets,
                                                                              (*count + 1) * sizeof(CssStylesheet*));
                                (*stylesheets)[*count] = stylesheet;
                                (*count)++;
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
            collect_inline_styles_from_dom((DomElement*)child, engine, pool, stylesheets, count, depth + 1);
        }
        child = child->next_sibling;
    }
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and adds to engine's stylesheet list
 */
void collect_inline_styles(Element* elem, CssEngine* engine, Pool* pool, int depth = 0) {
    if (!elem || !engine || !pool) return;
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
        // Extract text content from style element
        // Text content should be in the element's children
        for (int64_t i = 0; i < elem->length; i++) {
            Item child_item = elem->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* css_text = (String*)child_item.string_ptr;
                if (css_text && css_text->len > 0) {
                    log_debug("[CSS] Found <style> element with %d bytes of CSS", css_text->len);

                    // Parse the inline CSS
                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_text->chars, "<inline-style>");
                    if (stylesheet) {
                        log_debug("[CSS] Parsed inline <style>: %zu rules", stylesheet->rule_count);
                        // Note: stylesheet is already added to engine's internal list
                    }
                }
            }
        }
        } // end media check else
    }

    // Recursively process children
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_inline_styles(child_item.element, engine, pool, depth + 1);
        }
    }
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

    // Step 1: Collect and parse <link rel="stylesheet"> references
    log_debug("[CSS] Step 1: Collecting linked stylesheets...");
    collect_linked_stylesheets(html_root, engine, base_path, pool, &stylesheets, stylesheet_count, 0);

    int linked_count = *stylesheet_count;
    if (linked_count_out) *linked_count_out = linked_count;

    // Step 2: Collect and parse <style> inline CSS
    log_debug("[CSS] Step 2: Collecting inline <style> elements...");
    collect_inline_styles_to_list(html_root, engine, pool, &stylesheets, stylesheet_count, 0);

    log_debug("[CSS] Collected %d stylesheet(s) from HTML (%d linked, %d inline)",
              *stylesheet_count, linked_count, *stylesheet_count - linked_count);
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

// Hash and compare functions for string-keyed hashmap
static uint64_t string_key_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const char* key = *(const char**)item;
    if (!key) return 0;
    return hashmap_xxhash3(key, strlen(key), seed0, seed1);
}

static int string_key_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const char* ka = *(const char**)a;
    const char* kb = *(const char**)b;
    if (!ka && !kb) return 0;
    if (!ka) return -1;
    if (!kb) return 1;
    return strcmp(ka, kb);
}

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

        // Collect media rules separately for runtime evaluation
        if (rule->type == CSS_RULE_MEDIA) {
            arraylist_append(index->media_rules, rule);
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
    for (unsigned int i = 0; i < index->universal->length; i++) {
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
            for (unsigned int i = 0; i < tag_rules->length; i++) {
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
            for (unsigned int i = 0; i < id_rules->length; i++) {
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
                    for (unsigned int j = 0; j < class_rules->length; j++) {
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
        log_debug("[MediaQuery] Evaluating condition: '%s' for element <%s>",
                  media_condition ? media_condition : "(null)", elem->tag_name ? elem->tag_name : "?");
        bool matches = css_evaluate_media_query(engine, media_condition);
        log_debug("[MediaQuery] Result: %s", matches ? "MATCHES" : "does not match");
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
            DomElement* child_elem = (DomElement*)child;
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
    for (unsigned int i = 0; i < candidates->length; i++) {
        IndexedRule* entry = (IndexedRule*)candidates->data[i];
        apply_rule_to_dom_element(root, entry->rule, matcher, pool, engine);
    }
    arraylist_free(candidates);

    // Also process media rules (need runtime evaluation for each element)
    for (unsigned int i = 0; i < index->media_rules->length; i++) {
        CssRule* media_rule = (CssRule*)index->media_rules->data[i];
        apply_rule_to_dom_element(root, media_rule, matcher, pool, engine);
    }

    // Recursively apply to children (only element children)
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = (DomElement*)child;
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
        char quote = 0;
        if (*p == '"' || *p == '\'') { quote = *p; p++; }

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
static char* convert_latin1_to_utf8(const char* content, size_t content_len) {
    if (!content) return nullptr;

    // Windows-1252 to Unicode mapping for 0x80-0x9F (differs from Latin-1)
    // Latin-1 maps these as C1 control characters; Win-1252 maps them to useful glyphs
    static const uint16_t win1252_map[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };

    // worst case: each byte becomes 3 UTF-8 bytes
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
            uint16_t cp;
            if (c >= 0x80 && c <= 0x9F) {
                cp = win1252_map[c - 0x80];
            } else {
                cp = c;  // Latin-1: codepoint == byte value for 0xA0-0xFF
            }
            // encode as UTF-8
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
    size_t converted_len = out - out_buf;
    log_info("[charset] Converted %zu bytes Latin-1/Win-1252 to %zu bytes UTF-8", content_len, converted_len);
    return out_buf;
}

/**
 * Convert content from a single-byte encoding to UTF-8 using a 128-entry mapping table.
 * The table maps bytes 0x80-0xFF to Unicode code points.
 * Bytes 0x00-0x7F are passed through as ASCII.
 */
static char* convert_single_byte_to_utf8(const char* content, size_t content_len, const uint16_t* table, const char* charset_name) {
    if (!content || !table) return nullptr;
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
            uint16_t cp = table[c - 0x80];
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
DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source = nullptr,
    bool track_source_lines = false) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    if (!html_url || !pool) {
        log_error("load_lambda_html_doc: invalid parameters");
        return nullptr;
    }

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
                html_url = redirected_url;
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
            // Don't destroy old html_url as it may be referenced elsewhere
            html_url = base_url;
        }
    }

    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

    auto t_dom = high_resolution_clock::now();

    // Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        return nullptr;
    }
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

    // print internal stylesheets for debugging
    for (int i = 0; i < inline_stylesheet_count; i++) {
        const char* formatted_css = css_stylesheet_to_string_styled(
            inline_stylesheets[i], pool, CSS_FORMAT_EXPANDED);
        if (formatted_css) {
            log_debug("[Lambda CSS] Parsed inline stylesheet %d:\n%s", i, formatted_css);
        }
    }

    // Store stylesheets in DomDocument BEFORE scripts
    // This enables getComputedStyle to do on-demand selector matching
    int total_stylesheets = inline_stylesheet_count + (external_stylesheet ? 1 : 0);
    if (total_stylesheets > 0) {
        dom_doc->stylesheet_capacity = total_stylesheets;
        dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, total_stylesheets * sizeof(CssStylesheet*));
        dom_doc->stylesheet_count = 0;

        if (external_stylesheet) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = external_stylesheet;
        }
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i]) {
                dom_doc->stylesheets[dom_doc->stylesheet_count++] = inline_stylesheets[i];
            }
        }
        log_debug("[Lambda CSS] Stored %d stylesheets in DomDocument for getComputedStyle + @font-face",
                  dom_doc->stylesheet_count);
    }

    // Step 2c: Apply inline style="" attributes BEFORE scripts
    // Inline style="" attributes from HTML are applied first as the baseline.
    // JS style modifications (element.style.xxx = 'value') are applied after
    // via dom_element_apply_inline_style with a later source_order, so they
    // correctly override the original HTML inline styles in the cascade.
    // This also ensures the HTML→DOM tree mapping is pristine (no JS mutations yet).
    apply_inline_styles_to_tree(dom_root, html_root, pool);

    // Step 2d: Execute <script> elements (inline + external) and body onload handlers
    // Scripts run after inline style application so JS style changes override HTML attrs.
    // Scripts run before stylesheet cascade so JS DOM mutations (className changes,
    // appendChild, removeChild, etc.) take effect before styles are resolved.
    // getComputedStyle can query parsed stylesheets via on-demand matching.
    dom_doc->root = dom_root;  // set root for JS DOM API access
    execute_document_scripts(html_root, dom_doc, pool, html_url);

    auto t_scripts = high_resolution_clock::now();

    // Log JS DOM mutations — cascade will pick these up since it runs after scripts
    if (dom_doc->js_mutation_count > 0) {
        log_info("execute_document_scripts: %d DOM mutations from JS, CSS cascade will re-resolve",
                 dom_doc->js_mutation_count);

        // Re-collect inline <style> stylesheets from the DomElement* tree to pick up:
        // 1. Dynamically-added <style> elements (e.g. first-letter-dynamic-001)
        // 2. <style disabled> elements that should be skipped (e.g. table-anonymous-objects-015)
        // NOTE: Only <style> elements are rescanned. <link> stylesheets from the
        // initial collection are preserved — they don't change due to JS mutations.
        int rescan_inline_count = 0;
        CssStylesheet** rescan_inline_sheets = nullptr;
        collect_inline_styles_from_dom(dom_root, css_engine, pool, &rescan_inline_sheets, &rescan_inline_count);

        int old_inline_only = inline_stylesheet_count - linked_stylesheet_count;
        if (rescan_inline_count != old_inline_only) {
            log_info("[CSS] Re-scan found %d inline <style> stylesheets (was %d before JS)",
                     rescan_inline_count, old_inline_only);
        }

        // Merge: linked stylesheets (first N entries) + rescanned inline stylesheets
        int merged_count = linked_stylesheet_count + rescan_inline_count;
        CssStylesheet** merged_sheets = (CssStylesheet**)pool_alloc(pool, merged_count * sizeof(CssStylesheet*));

        // Copy linked stylesheets from original array (indices 0..linked_stylesheet_count-1)
        for (int i = 0; i < linked_stylesheet_count; i++) {
            merged_sheets[i] = inline_stylesheets[i];
        }
        // Copy rescanned inline <style> stylesheets
        for (int i = 0; i < rescan_inline_count; i++) {
            merged_sheets[linked_stylesheet_count + i] = rescan_inline_sheets[i];
        }

        inline_stylesheets = merged_sheets;
        inline_stylesheet_count = merged_count;

        // Update DomDocument stylesheet cache for getComputedStyle
        int new_total = merged_count + (external_stylesheet ? 1 : 0);
        dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, new_total * sizeof(CssStylesheet*));
        dom_doc->stylesheet_count = 0;
        dom_doc->stylesheet_capacity = new_total;
        if (external_stylesheet) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = external_stylesheet;
        }
        for (int i = 0; i < merged_count; i++) {
            if (merged_sheets[i]) {
                dom_doc->stylesheets[dom_doc->stylesheet_count++] = merged_sheets[i];
            }
        }
    }

    // Step 2e: Collect and compile inline event handlers (onclick, onmouseover, etc.)
    // Must happen after execute_document_scripts so function definitions are available.
    collect_and_compile_event_handlers(dom_doc);

    // Step 6: Apply CSS cascade (external + <style> elements)
    SelectorMatcher* matcher = selector_matcher_create(pool);

    auto t_cascade_start = high_resolution_clock::now();
    reset_cascade_timing();  // reset timing accumulators
    reset_dom_element_timing();  // reset declaration timing

    // Apply external stylesheet first (lower priority)
    if (external_stylesheet && external_stylesheet->rule_count > 0) {
        log_debug("[Lambda CSS] Applying external stylesheet with %d rules", external_stylesheet->rule_count);
        apply_stylesheet_to_dom_tree_fast(dom_root, external_stylesheet, matcher, pool, css_engine);
    }

    auto t_external = high_resolution_clock::now();
    log_info("[TIMING] cascade: external stylesheet: %.1fms", duration<double, std::milli>(t_external - t_cascade_start).count());

    // Apply inline stylesheets (higher priority)
    for (int i = 0; i < inline_stylesheet_count; i++) {
        log_debug("[Lambda CSS] Inline stylesheet %d: ptr=%p, rule_count=%zu",
            i, inline_stylesheets[i], inline_stylesheets[i] ? inline_stylesheets[i]->rule_count : 0);
        if (inline_stylesheets[i]) {
            log_debug("[Lambda CSS] Stylesheet %d is not null, rule_count=%zu", i, inline_stylesheets[i]->rule_count);
            if (inline_stylesheets[i]->rule_count > 0) {
                log_debug("[Lambda CSS] Applying inline stylesheet %d with %zu rules", i, inline_stylesheets[i]->rule_count);
                auto t_inline_start = high_resolution_clock::now();
                apply_stylesheet_to_dom_tree_fast(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
                auto t_inline_end = high_resolution_clock::now();
                log_info("[TIMING] cascade: inline stylesheet %d (%zu rules): %.1fms",
                    i, inline_stylesheets[i]->rule_count,
                    duration<double, std::milli>(t_inline_end - t_inline_start).count());
                log_debug("[Lambda CSS] Finished applying inline stylesheet %d", i);
            } else {
                log_debug("[Lambda CSS] Skipping stylesheet %d: rule_count=%zu is not > 0", i, inline_stylesheets[i]->rule_count);
            }
        } else {
            log_debug("[Lambda CSS] Stylesheet %d is NULL", i);
        }
    }

    auto t_inline_done = high_resolution_clock::now();
    log_debug("[Lambda CSS] CSS cascade complete");

    auto t_cascade = high_resolution_clock::now();
    log_info("[TIMING] cascade: inline style attrs: %.1fms", duration<double, std::milli>(t_cascade - t_inline_done).count());
    log_cascade_timing_summary();  // log selector matching and property application stats
    log_dom_element_timing();  // log detailed cascade timing
    log_info("[TIMING] load: CSS cascade: %.1fms", duration<double, std::milli>(t_cascade - t_css_parse).count());

    // Dump CSS computed values for testing/comparison (includes inheritance, before layout)
    StrBuf* str_buf = strbuf_new();
    dom_root->print(str_buf, 0);
    log_debug("Built DomElement tree with styles::\n%s", str_buf->str);
    strbuf_free(str_buf);

    // Step 8: Create DomDocument structure (already created by dom_document_create)
    // Just populate the additional fields
    dom_doc->root = dom_root;
    dom_doc->html_root = html_root;
    dom_doc->html_version = detected_version;
    dom_doc->url = html_url;
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
    log_info("[TIMING] load: total: %.1fms", duration<double, std::milli>(t_end - t_start).count());

    log_debug("[Lambda CSS] Document loaded and styled");
    return dom_doc;
}

DomDocument* load_html_doc(Url *base, char* doc_url, int viewport_width, int viewport_height, float pixel_ratio) {
    Pool* pool = pool_create();
    if (!pool) { log_error("Failed to create memory pool");  return NULL; }

    Url* full_url = parse_url(base, doc_url);
    if (!full_url) {
        log_error("Failed to parse URL: %s, with base: %p", doc_url, base);
        pool_destroy(pool);
        return NULL;
    }

    // For HTTP/HTTPS URLs, always route to HTML loader (it handles downloading)
    if (full_url->scheme == URL_SCHEME_HTTP || full_url->scheme == URL_SCHEME_HTTPS) {
        log_info("[load_html_doc] HTTP/HTTPS URL detected, using HTML pipeline: %s", doc_url);
        return load_lambda_html_doc(full_url, NULL, viewport_width, viewport_height, pool);
    }

    // Detect file type by extension (local files only)
    const char* ext = strrchr(doc_url, '.');
    DomDocument* doc = nullptr;

    if (ext && strcmp(ext, ".ls") == 0) {
        // Load Lambda script: evaluate script → wrap result → layout
        log_info("[load_html_doc] Detected Lambda script file, using script evaluation pipeline");
        doc = load_lambda_script_doc(full_url, viewport_width, viewport_height, pool);
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
        // Load PDF document: parse PDF → convert to ViewTree directly (no CSS layout needed)
        log_info("[load_html_doc] Detected PDF file, using PDF→ViewTree pipeline");
        doc = load_pdf_doc(full_url, viewport_width, viewport_height, pool, pixel_ratio);
    } else if (ext && strcmp(ext, ".svg") == 0) {
        // Load SVG document: render SVG → convert to ViewTree directly (no CSS layout needed)
        log_info("[load_html_doc] Detected SVG file, using SVG→ViewTree pipeline");
        doc = load_svg_doc(full_url, viewport_width, viewport_height, pool, pixel_ratio);
    } else if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                       strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0)) {
        // Load image document: load image → convert to ViewTree directly (no CSS layout needed)
        log_info("[load_html_doc] Detected image file, using Image→ViewTree pipeline");
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

    return doc;
}

/**
 * Load PDF document with direct ViewTree conversion
 * Parses PDF, converts to ViewTree directly (no CSS layout needed as PDF has absolute positions)
 *
 * @param pdf_url URL to PDF file
 * @param viewport_width Viewport width for scaling (currently unused, PDF uses original dimensions)
 * @param viewport_height Viewport height for scaling (currently unused)
 * @param pool Memory pool for allocations
 * @param pixel_ratio Display pixel ratio for high-DPI scaling (e.g., 2.0 for Retina)
 * @return DomDocument structure with view_tree pre-set, ready for rendering
 */
DomDocument* load_pdf_doc(Url* pdf_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!pdf_url || !pool) {
        log_error("load_pdf_doc: invalid parameters");
        return nullptr;
    }

    char* pdf_filepath = url_to_local_path(pdf_url);
    log_info("[TIMING] Loading PDF document: %s", pdf_filepath);

    // Step 1: Read PDF file content (binary mode for proper handling)
    auto step1_start = std::chrono::high_resolution_clock::now();
    size_t pdf_size = 0;
    char* pdf_content = read_binary_file(pdf_filepath, &pdf_size);
    if (!pdf_content) {
        log_error("Failed to read PDF file: %s", pdf_filepath);
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Read PDF file: %.1fms (%zu bytes)",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count(), pdf_size);

    // Step 2: Create Input structure and parse PDF
    auto step2_start = std::chrono::high_resolution_clock::now();
    Input* input = InputManager::create_input(pdf_url);
    if (!input) {
        log_error("Failed to create Input structure");
        mem_free(pdf_content);
        return nullptr;
    }

    // Parse PDF content with explicit size
    parse_pdf(input, pdf_content, pdf_size);
    mem_free(pdf_content);  // Done with raw content

    // Check if parsing succeeded
    if (input->root.item == ITEM_ERROR || input->root.item == ITEM_NULL) {
        log_error("Failed to parse PDF file: %s", pdf_filepath);
        return nullptr;
    }

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Parse PDF: %.1fms",
        std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Get page count and convert first page to ViewTree
    auto step3_start = std::chrono::high_resolution_clock::now();
    int total_pages = pdf_get_page_count(input->root);
    if (total_pages <= 0) {
        log_error("PDF has no pages or page count failed");
        return nullptr;
    }

    log_info("PDF has %d page(s)", total_pages);

    // Convert first page to view tree (page 0)
    // Pass pixel_ratio for high-DPI display scaling
    ViewTree* view_tree = pdf_page_to_view_tree(input, input->root, 0, pixel_ratio);
    if (!view_tree || !view_tree->root) {
        log_error("Failed to convert PDF page to view tree");
        return nullptr;
    }

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - Convert to ViewTree: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 4: Create DomDocument and populate it
    // Note: We set html_root to nullptr since PDF doesn't use HTML/CSS layout
    // The view_tree is pre-created with absolute positions from PDF
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

    dom_doc->root = nullptr;           // No DomElement tree for PDF
    dom_doc->html_root = nullptr;      // No HTML tree for PDF (skips layout_html_doc)
    dom_doc->html_version = HTML5;     // Treat as HTML5 for compatibility
    dom_doc->url = pdf_url;
    dom_doc->view_tree = view_tree;    // Pre-created ViewTree from PDF
    dom_doc->state = nullptr;

    // Set scale fields for rendering
    // PDF view tree is pre-scaled by pixel_ratio, so given_scale = 1.0 and scale = pixel_ratio
    dom_doc->given_scale = 1.0f;
    dom_doc->scale = pixel_ratio;

    // Set content dimensions from ViewTree root
    if (view_tree->root) {
        ViewBlock* root = (ViewBlock*)view_tree->root;
        log_info("PDF view tree root dimensions: %.0fx%.0f", root->width, root->height);
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_pdf_doc total: %.1fms",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());

    return dom_doc;
}

/**
 * Load SVG document with direct ViewTree conversion
 * Loads SVG with ThorVG, creates ViewTree with SVG as embedded image
 *
 * @param svg_url URL to SVG file
 * @param viewport_width Viewport width (used for scaling if SVG has no intrinsic size)
 * @param viewport_height Viewport height
 * @param pool Memory pool for allocations
 * @param pixel_ratio Display pixel ratio for high-DPI scaling
 * @return DomDocument structure with view_tree pre-set, ready for rendering
 */
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!svg_url || !pool) {
        log_error("load_svg_doc: invalid parameters");
        return nullptr;
    }

    char* svg_filepath = url_to_local_path(svg_url);
    log_info("[TIMING] Loading SVG document: %s", svg_filepath);

    // Create Input structure (minimal, for DomDocument creation)
    Input* input = InputManager::create_input(svg_url);
    if (!input) {
        log_error("Failed to create Input structure for SVG");
        return nullptr;
    }

    // Create a dedicated pool for the view tree
    Pool* view_pool = pool_create();
    if (!view_pool) {
        log_error("Failed to create view pool for SVG");
        return nullptr;
    }

    // Load SVG through rdt_ vector API
    RdtPicture* pic = rdt_picture_load(svg_filepath);
    if (!pic) {
        log_error("Failed to load SVG: %s", svg_filepath);
        pool_destroy(view_pool);
        return nullptr;
    }

    // Get SVG intrinsic size from viewBox
    float svg_width, svg_height;
    rdt_picture_get_size(pic, &svg_width, &svg_height);
    log_info("SVG intrinsic size: %.1f x %.1f", svg_width, svg_height);

    // If SVG has no intrinsic size, use viewport
    if (svg_width <= 0) svg_width = (float)viewport_width;
    if (svg_height <= 0) svg_height = (float)viewport_height;

    // Apply pixel_ratio for high-DPI displays
    float scaled_width = svg_width * pixel_ratio;
    float scaled_height = svg_height * pixel_ratio;

    // Create ViewTree
    ViewTree* view_tree = (ViewTree*)pool_calloc(view_pool, sizeof(ViewTree));
    if (!view_tree) {
        log_error("Failed to allocate view tree for SVG");
        rdt_picture_free(pic);
        pool_destroy(view_pool);
        return nullptr;
    }
    view_tree->pool = view_pool;
    view_tree->html_version = HTML5;

    // Create root ViewBlock to hold the SVG
    ViewBlock* root_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!root_view) {
        log_error("Failed to allocate root view for SVG");
        rdt_picture_free(pic);
        pool_destroy(view_pool);
        return nullptr;
    }

    root_view->view_type = RDT_VIEW_BLOCK;
    root_view->x = 0;
    root_view->y = 0;
    root_view->width = scaled_width;
    root_view->height = scaled_height;
    root_view->content_width = scaled_width;
    root_view->content_height = scaled_height;

    // Create ImageSurface to hold the SVG
    ImageSurface* svg_surface = (ImageSurface*)pool_calloc(view_pool, sizeof(ImageSurface));
    if (svg_surface) {
        svg_surface->format = IMAGE_FORMAT_SVG;
        svg_surface->pic = pic;
        svg_surface->width = (int)svg_width;
        svg_surface->height = (int)svg_height;
        svg_surface->url = svg_url;
        // Set max_render_width so render_svg knows the target size for rasterization
        svg_surface->max_render_width = (int)scaled_width;

        // Create EmbedProp to hold the image
        EmbedProp* embed = (EmbedProp*)pool_calloc(view_pool, sizeof(EmbedProp));
        if (embed) {
            embed->img = svg_surface;
            root_view->embed = embed;
        }
    }

    view_tree->root = (View*)root_view;

    // Create DomDocument
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument for SVG");
        pool_destroy(view_pool);
        return nullptr;
    }

    dom_doc->root = nullptr;           // No DomElement tree for SVG
    dom_doc->html_root = nullptr;      // No HTML tree for SVG (skips layout_html_doc)
    dom_doc->html_version = HTML5;
    dom_doc->url = svg_url;
    dom_doc->view_tree = view_tree;
    dom_doc->state = nullptr;

    // Set scale fields for rendering
    // SVG view tree is pre-scaled by pixel_ratio, so given_scale = 1.0 and scale = pixel_ratio
    dom_doc->given_scale = 1.0f;
    dom_doc->scale = pixel_ratio;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_svg_doc total: %.1fms, size: %.0fx%.0f (scaled: %.0fx%.0f)",
        std::chrono::duration<double, std::milli>(total_end - total_start).count(),
        svg_width, svg_height, scaled_width, scaled_height);

    return dom_doc;
}

/**
 * Load raster image document (PNG, JPG, JPEG, GIF)
 * Loads image, creates ViewTree directly (no CSS layout needed)
 *
 * @param img_url URL to image file
 * @param viewport_width Viewport width for display
 * @param viewport_height Viewport height for display
 * @param pool Memory pool for allocations
 * @param pixel_ratio Display pixel ratio for high-DPI scaling (e.g., 2.0 for Retina)
 * @return DomDocument structure with view_tree pre-set, ready for rendering
 */
DomDocument* load_image_doc(Url* img_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!img_url || !pool) {
        log_error("load_image_doc: invalid parameters");
        return nullptr;
    }

    char* img_filepath = url_to_local_path(img_url);
    log_info("[TIMING] Loading image document: %s", img_filepath);

    // Create Input structure (minimal, for DomDocument creation)
    Input* input = InputManager::create_input(img_url);
    if (!input) {
        log_error("Failed to create Input structure for image");
        return nullptr;
    }

    // Create a dedicated pool for the view tree
    Pool* view_pool = pool_create();
    if (!view_pool) {
        log_error("Failed to create view pool for image");
        return nullptr;
    }

    // Determine image format from extension
    const char* ext = strrchr(img_filepath, '.');
    ImageFormat format = IMAGE_FORMAT_PNG;  // default
    if (ext) {
        if (str_ieq_const(ext, strlen(ext), ".jpg") || str_ieq_const(ext, strlen(ext), ".jpeg")) {
            format = IMAGE_FORMAT_JPEG;
        } else if (str_ieq_const(ext, strlen(ext), ".gif")) {
            format = IMAGE_FORMAT_GIF;
        } else if (str_ieq_const(ext, strlen(ext), ".png")) {
            format = IMAGE_FORMAT_PNG;
        }
    }

    // Load image using stb_image
    int img_width, img_height, channels;
    unsigned char* data = image_load(img_filepath, &img_width, &img_height, &channels, 4);
    if (!data) {
        log_error("Failed to load image: %s", img_filepath);
        pool_destroy(view_pool);
        return nullptr;
    }

    log_info("Image loaded: %dx%d, channels=%d", img_width, img_height, channels);

    // Create ImageSurface from loaded data
    ImageSurface* img_surface = image_surface_create_from(img_width, img_height, data);
    if (!img_surface) {
        log_error("Failed to create image surface");
        image_free(data);
        pool_destroy(view_pool);
        return nullptr;
    }
    img_surface->format = format;
    img_surface->url = img_url;

    // Apply pixel_ratio for high-DPI displays
    float scaled_width = (float)img_width * pixel_ratio;
    float scaled_height = (float)img_height * pixel_ratio;

    // Create ViewTree
    ViewTree* view_tree = (ViewTree*)pool_calloc(view_pool, sizeof(ViewTree));
    if (!view_tree) {
        log_error("Failed to allocate view tree for image");
        image_surface_destroy(img_surface);
        pool_destroy(view_pool);
        return nullptr;
    }
    view_tree->pool = view_pool;
    view_tree->html_version = HTML5;

    // Create root ViewBlock to hold the image
    ViewBlock* root_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!root_view) {
        log_error("Failed to allocate root view for image");
        image_surface_destroy(img_surface);
        pool_destroy(view_pool);
        return nullptr;
    }

    root_view->view_type = RDT_VIEW_BLOCK;
    root_view->x = 0;
    root_view->y = 0;
    root_view->width = scaled_width;
    root_view->height = scaled_height;
    root_view->content_width = scaled_width;
    root_view->content_height = scaled_height;

    // Create EmbedProp to hold the image
    EmbedProp* embed = (EmbedProp*)pool_calloc(view_pool, sizeof(EmbedProp));
    if (embed) {
        embed->img = img_surface;
        root_view->embed = embed;
    }

    view_tree->root = (View*)root_view;

    // Create DomDocument
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument for image");
        image_surface_destroy(img_surface);
        pool_destroy(view_pool);
        return nullptr;
    }

    dom_doc->root = nullptr;           // No DomElement tree for images
    dom_doc->html_root = nullptr;      // No HTML tree for images (skips layout_html_doc)
    dom_doc->html_version = HTML5;
    dom_doc->url = img_url;
    dom_doc->view_tree = view_tree;
    dom_doc->state = nullptr;

    // Set scale fields for rendering
    // Image view tree is pre-scaled by pixel_ratio, so given_scale = 1.0 and scale = pixel_ratio
    dom_doc->given_scale = 1.0f;
    dom_doc->scale = pixel_ratio;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_image_doc total: %.1fms, size: %dx%d (scaled: %.0fx%.0f)",
        std::chrono::duration<double, std::milli>(total_end - total_start).count(),
        img_width, img_height, scaled_width, scaled_height);

    return dom_doc;
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

    char* text_filepath = url_to_local_path(text_url);
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

    // Step 2: Escape HTML special characters for safe display
    auto step2_start = std::chrono::high_resolution_clock::now();

    // count how many chars need escaping
    size_t escaped_len = 0;
    for (size_t i = 0; i < content_len; i++) {
        char c = text_content[i];
        if (c == '<') escaped_len += 4;       // &lt;
        else if (c == '>') escaped_len += 4;  // &gt;
        else if (c == '&') escaped_len += 5;  // &amp;
        else escaped_len += 1;
    }

    // allocate escaped string
    char* escaped_content = (char*)mem_alloc(escaped_len + 1, MEM_CAT_LAYOUT);
    if (!escaped_content) {
        log_error("Failed to allocate escaped content buffer");
        mem_free(text_content);  // from read_text_file, uses stdlib
        return nullptr;
    }

    // escape content
    size_t j = 0;
    for (size_t i = 0; i < content_len; i++) {
        char c = text_content[i];
        if (c == '<') {
            memcpy(escaped_content + j, "&lt;", 4);
            j += 4;
        } else if (c == '>') {
            memcpy(escaped_content + j, "&gt;", 4);
            j += 4;
        } else if (c == '&') {
            memcpy(escaped_content + j, "&amp;", 5);
            j += 5;
        } else {
            escaped_content[j++] = c;
        }
    }
    escaped_content[j] = '\0';
    mem_free(text_content);  // from read_text_file, uses stdlib

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
    size_t html_len = strlen(html_template) + strlen(filename) + escaped_len + 1;
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

    String* type_str = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_LAYOUT);
    type_str->len = 4;
    str_copy(type_str->chars, type_str->len + 1, "html", 4);

    Input* input = input_from_source(html_content, text_url, type_str, nullptr);
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

    // Apply stylesheets to DOM tree
    for (int i = 0; i < stylesheet_count; i++) {
        if (stylesheets[i]) {
            apply_stylesheet_to_dom_tree_fast(dom_root, stylesheets[i], matcher, pool, css_engine);
        }
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
DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!markdown_url || !pool) {
        log_error("load_markdown_doc: invalid parameters");
        return nullptr;
    }

    char* markdown_filepath = url_to_local_path(markdown_url);
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
    char* markdown_content = read_text_file(markdown_filepath);
    if (!markdown_content) {
        log_error("Failed to read markdown file: %s", markdown_filepath);
        return nullptr;
    }

    // Create type string for markdown
    String* type_str = (String*)mem_alloc(sizeof(String) + 9, MEM_CAT_LAYOUT);
    type_str->len = 8;
    str_copy(type_str->chars, type_str->len + 1, "markdown", 8);

    // Parse markdown to Lambda Element tree
    Input* input = input_from_source(markdown_content, markdown_url, type_str, nullptr);
    mem_free(markdown_content);  // from read_text_file, uses stdlib

    if (!input) {
        log_error("Failed to parse markdown file: %s", markdown_filepath);
        return nullptr;
    }

    // Get root element from parsed markdown
    Element* markdown_root = nullptr;
    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_ELEMENT) {
        markdown_root = input->root.element;
    } else if (root_type == LMD_TYPE_ARRAY) {
        // Markdown parser may return list, find first element
        List* root_list = input->root.list;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            if (get_type_id(item) == LMD_TYPE_ELEMENT) {
                markdown_root = item.element;
                break;
            }
        }
    }

    if (!markdown_root) {
        log_error("Failed to get markdown root element");
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Parse markdown: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

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

            // Run the script in-memory (no temp file needed)
            Runtime math_runtime;
            runtime_init(&math_runtime);
            math_runtime.current_dir = const_cast<char*>("./");
            math_runtime.import_base_dir = "./";

            Input* math_result = run_script_mir(&math_runtime, script->str, (char*)"<math_render>", false);

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
                log_info("[Lambda Markdown] Replaced %d/%d math elements with rendered HTML",
                         replace_count, math_list->length);
            } else {
                log_error("[Lambda Markdown] Math rendering script failed or returned unexpected type");
            }

            runtime_cleanup(&math_runtime);
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
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

    DomElement* dom_root = build_dom_tree_from_element(markdown_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree from markdown");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

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

    // Step 4: Load markdown.css stylesheet
    // Determine the path to markdown.css (relative to lambda home)
    char* css_filename = lambda_home_path("input/markdown.css");
    log_debug("[Lambda Markdown] Loading default markdown stylesheet: %s", css_filename);

    CssStylesheet* markdown_stylesheet = nullptr;
    char* css_content = read_text_file(css_filename);
    if (css_content) {
        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (css_pool_copy) {
            str_copy(css_pool_copy, css_len + 1, css_content, css_len);
            mem_free(css_content);
            markdown_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (markdown_stylesheet) {
                log_debug("[Lambda Markdown] Loaded markdown stylesheet with %zu rules",
                        markdown_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse markdown.css");
            }
        } else {
            mem_free(css_content);
        }
    } else {
        log_warn("Failed to load markdown.css file: %s", css_filename);
        log_warn("Continuing without stylesheet - markdown will use browser defaults");
    }
    mem_free(css_filename);

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 4.5: Load math CSS and KaTeX font CSS for math rendering
    CssStylesheet* math_stylesheet = nullptr;
    CssStylesheet* katex_stylesheet = nullptr;
    {
        char* math_css_filename = lambda_home_path("input/math.css");
        char* math_css_content = read_text_file(math_css_filename);
        if (math_css_content) {
            size_t math_css_len = strlen(math_css_content);
            char* math_css_pool = (char*)pool_alloc(pool, math_css_len + 1);
            if (math_css_pool) {
                str_copy(math_css_pool, math_css_len + 1, math_css_content, math_css_len);
                mem_free(math_css_content);
                math_stylesheet = css_parse_stylesheet(css_engine, math_css_pool, math_css_filename);
                if (math_stylesheet) {
                    log_debug("[Lambda Markdown] Loaded math stylesheet with %zu rules",
                              math_stylesheet->rule_count);
                }
            } else {
                mem_free(math_css_content);
            }
        }
        mem_free(math_css_filename);

        char* katex_css_filename = lambda_home_path("input/latex/css/katex.css");
        char* katex_css_content = read_text_file(katex_css_filename);
        if (katex_css_content) {
            size_t katex_css_len = strlen(katex_css_content);
            char* katex_css_pool = (char*)pool_alloc(pool, katex_css_len + 1);
            if (katex_css_pool) {
                str_copy(katex_css_pool, katex_css_len + 1, katex_css_content, katex_css_len);
                mem_free(katex_css_content);
                katex_stylesheet = css_parse_stylesheet(css_engine, katex_css_pool, katex_css_filename);
                if (katex_stylesheet) {
                    log_debug("[Lambda Markdown] Loaded KaTeX font stylesheet with %zu rules",
                              katex_stylesheet->rule_count);
                }
            } else {
                mem_free(katex_css_content);
            }
        }
        mem_free(katex_css_filename);
    }

    // Step 5: Apply CSS cascade to DOM tree
    auto step4_start = std::chrono::high_resolution_clock::now();
    {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        if (markdown_stylesheet && markdown_stylesheet->rule_count > 0) {
            apply_stylesheet_to_dom_tree_fast(dom_root, markdown_stylesheet, matcher, pool, css_engine);
        }
        if (math_stylesheet && math_stylesheet->rule_count > 0) {
            apply_stylesheet_to_dom_tree_fast(dom_root, math_stylesheet, matcher, pool, css_engine);
        }
        if (katex_stylesheet && katex_stylesheet->rule_count > 0) {
            apply_stylesheet_to_dom_tree_fast(dom_root, katex_stylesheet, matcher, pool, css_engine);
        }
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

    // Store stylesheets in DomDocument for @font-face processing
    int total_stylesheets = (markdown_stylesheet ? 1 : 0) + (math_stylesheet ? 1 : 0) + (katex_stylesheet ? 1 : 0);
    if (total_stylesheets > 0) {
        dom_doc->stylesheet_capacity = total_stylesheets;
        dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, total_stylesheets * sizeof(CssStylesheet*));
        dom_doc->stylesheet_count = 0;
        if (markdown_stylesheet) dom_doc->stylesheets[dom_doc->stylesheet_count++] = markdown_stylesheet;
        if (math_stylesheet) dom_doc->stylesheets[dom_doc->stylesheet_count++] = math_stylesheet;
        if (katex_stylesheet) dom_doc->stylesheets[dom_doc->stylesheet_count++] = katex_stylesheet;
        log_debug("[Lambda Markdown] Stored %d stylesheets in DomDocument for @font-face processing",
                  dom_doc->stylesheet_count);
    }

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

    char* wiki_filepath = url_to_local_path(wiki_url);
    log_info("[TIMING] Loading wiki document: %s", wiki_filepath);

    // Step 1: Parse wiki with Lambda parser
    auto step1_start = std::chrono::high_resolution_clock::now();
    char* wiki_content = read_text_file(wiki_filepath);
    if (!wiki_content) {
        log_error("Failed to read wiki file: %s", wiki_filepath);
        return nullptr;
    }

    // Create type string for wiki
    String* type_str = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_LAYOUT);
    type_str->len = 4;
    str_copy(type_str->chars, type_str->len + 1, "wiki", 4);

    // Parse wiki to Lambda Element tree
    Input* input = input_from_source(wiki_content, wiki_url, type_str, nullptr);
    mem_free(wiki_content);  // from read_text_file, uses stdlib

    if (!input) {
        log_error("Failed to parse wiki file: %s", wiki_filepath);
        return nullptr;
    }

    // Get root element from parsed wiki
    Element* wiki_root = nullptr;
    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_ELEMENT) {
        wiki_root = input->root.element;
    } else if (root_type == LMD_TYPE_ARRAY) {
        // Wiki parser may return list, find first element
        List* root_list = input->root.list;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            if (get_type_id(item) == LMD_TYPE_ELEMENT) {
                wiki_root = item.element;
                break;
            }
        }
    }

    if (!wiki_root) {
        log_error("Failed to get wiki root element");
        return nullptr;
    }

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Parse wiki: %.1fms",
        std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    auto step2_start = std::chrono::high_resolution_clock::now();
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

    DomElement* dom_root = build_dom_tree_from_element(wiki_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree from wiki");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

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

    // Step 4: Load wiki.css stylesheet
    // Determine the path to wiki.css (relative to lambda home)
    char* css_filename = lambda_home_path("input/wiki.css");
    log_debug("[Lambda Wiki] Loading default wiki stylesheet: %s", css_filename);

    CssStylesheet* wiki_stylesheet = nullptr;
    char* css_content = read_text_file(css_filename);
    if (css_content) {
        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (css_pool_copy) {
            str_copy(css_pool_copy, css_len + 1, css_content, css_len);
            mem_free(css_content);
            wiki_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (wiki_stylesheet) {
                log_debug("[Lambda Wiki] Loaded wiki stylesheet with %zu rules",
                        wiki_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse wiki.css");
            }
        } else {
            mem_free(css_content);
        }
    } else {
        log_warn("Failed to load wiki.css file: %s", css_filename);
        log_warn("Continuing without stylesheet - wiki will use browser defaults");
    }
    mem_free(css_filename);

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 5: Apply CSS cascade to DOM tree
    auto step4_start = std::chrono::high_resolution_clock::now();
    if (wiki_stylesheet && wiki_stylesheet->rule_count > 0) {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        apply_stylesheet_to_dom_tree_fast(dom_root, wiki_stylesheet, matcher, pool, css_engine);
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

    char* latex_filepath = url_to_local_path(latex_url);
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

    // Run the script in-memory (no temp file needed)
    Runtime latex_runtime;
    runtime_init(&latex_runtime);
    latex_runtime.current_dir = const_cast<char*>("./");
    latex_runtime.import_base_dir = "./";  // resolve imports from project root
    Input* script_result = run_script_mir(&latex_runtime, script_buf, (char*)"<latex_render>", false);

    if (!script_result || get_type_id(script_result->root) == LMD_TYPE_NULL
        || get_type_id(script_result->root) == LMD_TYPE_ERROR) {
        log_error("[Lambda LaTeX] Lambda LaTeX package - HTML rendering failed for: %s", latex_filepath);
        runtime_cleanup(&latex_runtime);
        return nullptr;
    }

    // The script returns a Lambda Element tree (not an HTML string).
    // Use it directly as input to build_dom_tree_from_element, skipping
    // the unnecessary serialize→reparse roundtrip.
    Element* html_root = nullptr;
    TypeId result_type = get_type_id(script_result->root);
    if (result_type == LMD_TYPE_ELEMENT) {
        html_root = script_result->root.element;
    } else {
        // Fallback: extract HTML via print_root_item → input_from_source
        StrBuf* html_buf = strbuf_new_cap(8192);
        print_root_item(html_buf, script_result->root);
        const char* html_doc = html_buf->str;
        size_t html_len = html_buf->length;
        if (html_doc && html_len > 0) {
            log_info("[Lambda LaTeX] Lambda package generated HTML (%zu bytes)", html_len);
            String* html_type_str = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_LAYOUT);
            html_type_str->len = 4;
            str_copy(html_type_str->chars, html_type_str->len + 1, "html", 4);
            Input* html_input = input_from_source(html_doc, latex_url, html_type_str, nullptr);
            if (html_input) {
                html_root = get_html_root_element(html_input);
            }
        }
        strbuf_free(html_buf);
    }
    runtime_cleanup(&latex_runtime);

    if (!html_root) {
        log_error("[Lambda LaTeX] Failed to get HTML root element from LaTeX conversion");
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Converted LaTeX to HTML Element tree via Lambda package");

    // Step 3: Create DomDocument and build DomElement tree from HTML Element tree
    log_debug("[Lambda LaTeX] Building DomElement tree from HTML");
    DomDocument* dom_doc = dom_document_create(script_result);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

    // Ensure CSS property system is initialized before building DOM tree,
    // so that inline style declarations get correct property IDs.
    css_property_system_init(dom_doc->pool);

    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree from HTML");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Built DomElement tree: root=%p", (void*)dom_root);

    // Step 4: Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Step 5: Load LaTeX.css stylesheet (if it exists)
    // LaTeX to HTML conversion may embed styles, but we can provide default styling
    char* css_filename = lambda_home_path("input/latex/css/article.css");
    log_debug("[Lambda LaTeX] Loading default LaTeX stylesheet: %s", css_filename);

    CssStylesheet* latex_stylesheet = nullptr;
    char* css_content = read_text_file(css_filename);
    if (css_content) {
        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (css_pool_copy) {
            str_copy(css_pool_copy, css_len + 1, css_content, css_len);
            mem_free(css_content);
            latex_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (latex_stylesheet) {
                log_debug("[Lambda LaTeX] Loaded LaTeX stylesheet with %zu rules",
                        latex_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse latex.css");
            }
        } else {
            mem_free(css_content);
        }
    } else {
        log_debug("No latex.css file found, LaTeX HTML will use embedded and inline styles");
    }
    mem_free(css_filename);

    // Load KaTeX font stylesheet for math rendering (@font-face declarations for KaTeX_Size1-4 etc.)
    char* katex_css_filename = lambda_home_path("input/latex/css/katex.css");
    CssStylesheet* katex_stylesheet = nullptr;
    char* katex_css_content = read_text_file(katex_css_filename);
    if (katex_css_content) {
        size_t katex_css_len = strlen(katex_css_content);
        char* katex_css_pool_copy = (char*)pool_alloc(pool, katex_css_len + 1);
        if (katex_css_pool_copy) {
            str_copy(katex_css_pool_copy, katex_css_len + 1, katex_css_content, katex_css_len);
            mem_free(katex_css_content);
            katex_stylesheet = css_parse_stylesheet(css_engine, katex_css_pool_copy, katex_css_filename);
            if (katex_stylesheet) {
                log_debug("[Lambda LaTeX] Loaded KaTeX font stylesheet with %zu rules",
                        katex_stylesheet->rule_count);
            }
        } else {
            mem_free(katex_css_content);
        }
    }
    mem_free(katex_css_filename);

    // Step 6: Extract and parse any inline <style> elements from HTML
    log_debug("[Lambda LaTeX] Extracting inline <style> elements from LaTeX-generated HTML...");
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, latex_filepath, pool, &inline_stylesheet_count);

    // Step 7: Apply CSS cascade to DOM tree
    SelectorMatcher* matcher = selector_matcher_create(pool);

    // Apply LaTeX stylesheet first if available
    if (latex_stylesheet && latex_stylesheet->rule_count > 0) {
        log_debug("[Lambda LaTeX] Applying LaTeX stylesheet...");
        apply_stylesheet_to_dom_tree_fast(dom_root, latex_stylesheet, matcher, pool, css_engine);
    }

    // Apply inline stylesheets from LaTeX-generated HTML
    for (int i = 0; i < inline_stylesheet_count; i++) {
        if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
            log_debug("[Lambda LaTeX] Applying inline stylesheet %d with %zu rules",
                     i, inline_stylesheets[i]->rule_count);
            apply_stylesheet_to_dom_tree_fast(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
        }
    }

    // Apply inline style="" attributes (highest priority)
    apply_inline_styles_to_tree(dom_root, html_root, pool);

    log_debug("[Lambda LaTeX] CSS cascade complete");

    // Store stylesheets in DomDocument for @font-face processing later
    int total_stylesheets = inline_stylesheet_count + (latex_stylesheet ? 1 : 0) + (katex_stylesheet ? 1 : 0);
    if (total_stylesheets > 0) {
        dom_doc->stylesheet_capacity = total_stylesheets;
        dom_doc->stylesheets = (CssStylesheet**)pool_alloc(pool, total_stylesheets * sizeof(CssStylesheet*));
        dom_doc->stylesheet_count = 0;

        if (latex_stylesheet) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = latex_stylesheet;
        }
        if (katex_stylesheet) {
            dom_doc->stylesheets[dom_doc->stylesheet_count++] = katex_stylesheet;
        }
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i]) {
                dom_doc->stylesheets[dom_doc->stylesheet_count++] = inline_stylesheets[i];
            }
        }
        log_debug("[Lambda LaTeX] Stored %d stylesheets in DomDocument for @font-face processing",
                  dom_doc->stylesheet_count);
    }

    // Step 8: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = html_root;
    dom_doc->html_version = HTML5;  // Treat LaTeX-generated HTML as HTML5
    dom_doc->url = latex_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;

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

    char* xml_filepath = url_to_local_path(xml_url);
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
    html_elem->first_child = (DomNode*)body_elem;
    html_elem->last_child = (DomNode*)body_elem;
    body_elem->parent = (DomNode*)html_elem;

    body_elem->first_child = (DomNode*)xml_dom;
    body_elem->last_child = (DomNode*)xml_dom;
    xml_dom->parent = (DomNode*)body_elem;

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

    // Reset timing stats before cascade
    reset_cascade_timing();

    // Apply external stylesheet to the entire tree (starting from html)
    if (external_stylesheet && external_stylesheet->rule_count > 0) {
        log_debug("[Lambda XML] Applying external stylesheet with %zu rules", external_stylesheet->rule_count);
        apply_stylesheet_to_dom_tree_fast(html_elem, external_stylesheet, matcher, pool, css_engine);
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
    Pool* pool = pool_create();
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
DomDocument* load_lambda_script_doc(Url* script_url, int viewport_width, int viewport_height, Pool* pool) {
    auto total_start = std::chrono::high_resolution_clock::now();

    if (!script_url || !pool) {
        log_error("load_lambda_script_doc: invalid parameters");
        return nullptr;
    }

    char* script_filepath = url_to_local_path(script_url);
    log_info("[Lambda Script] Loading Lambda script: %s", script_filepath);

    // Step 1: Initialize Runtime and evaluate the Lambda script
    auto step1_start = std::chrono::high_resolution_clock::now();

    Runtime* runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
    runtime_init(runtime);

    // Phase 5: Create result Input with arena for unified DOM allocation.
    // Elements from JIT execution will be fat DomElements on this arena.
    Pool* result_pool = pool_create();
    Input* result_input = Input::create(result_pool, script_url);
    result_input->ui_mode = true;
    runtime->ui_mode = true;
    runtime->result_arena = result_input->arena;

    log_debug("[Lambda Script] Evaluating script (ui_mode=true, arena allocation)...");
    // Use MIR Direct JIT to evaluate the Lambda script (functional scripts don't need a main() entry point)
    Input* script_output = run_script_mir(runtime, nullptr, script_filepath, false);

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
        runtime_cleanup(runtime);
        mem_free(runtime);
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
        runtime_cleanup(runtime);
        mem_free(runtime);
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
        runtime_cleanup(runtime);
        mem_free(runtime);
        pool_destroy(result_pool);
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
            runtime_cleanup(runtime);
            mem_free(runtime);
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
            runtime_cleanup(runtime);
            mem_free(runtime);
            pool_destroy(result_pool);
            return load_html_string_doc(result_str->chars, viewport_width, viewport_height);
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
        runtime_cleanup(runtime);
        mem_free(runtime);
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
        runtime_cleanup(runtime);
        mem_free(runtime);
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
        runtime_cleanup(runtime);
        mem_free(runtime);
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

    // Apply script.css if loaded
    if (script_stylesheet && script_stylesheet->rule_count > 0) {
        apply_stylesheet_to_dom_tree_fast(dom_root, script_stylesheet, matcher, pool, css_engine);
    }

    // Apply inline stylesheets for HTML documents
    if (inline_stylesheets && inline_stylesheet_count > 0) {
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
                log_debug("[Lambda Script] Applying inline stylesheet %d with %zu rules",
                          i, inline_stylesheets[i]->rule_count);
                apply_stylesheet_to_dom_tree_fast(dom_root, inline_stylesheets[i], matcher, pool, css_engine);
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

    // Register doc root for render map parent fixup during retransform
    Item html_item_root = {.element = html_elem};
    render_map_set_doc_root(html_item_root);

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
        DomElement* elem = (DomElement*)root;
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
                View* found = find_matching_input((View*)child, match_tag, match_class);
                if (found) return found;
            }
            child = child->next_sibling;
        }
    }
    return nullptr;
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
    RadiantState* state = (RadiantState*)doc->state;
    const char* focus_tag = nullptr;
    const char* focus_class = nullptr;
    bool had_focus = false;
    if (state && state->focus && state->focus->current) {
        View* focused = state->focus->current;
        if (focused->is_element()) {
            DomElement* felem = (DomElement*)focused;
            focus_tag = felem->tag_name;
            if (felem->class_count > 0 && felem->class_names)
                focus_class = felem->class_names[0];
            had_focus = true;
        }
    }

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
        for (int i = 0; i < inline_count; i++) {
            if (inline_sheets[i] && inline_sheets[i]->rule_count > 0) {
                apply_stylesheet_to_dom_tree_fast(new_root, inline_sheets[i],
                                                  matcher, doc->pool, css_engine);
            }
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
    if (had_focus && state && doc->view_tree && doc->view_tree->root) {
        View* new_focused = find_matching_input(
            (View*)doc->view_tree->root, focus_tag, focus_class);
        if (new_focused) {
            state->focus->current = new_focused;
            log_debug("rebuild_lambda_doc: restored focus to new view %p (tag=%s class=%s)",
                     new_focused, focus_tag ? focus_tag : "", focus_class ? focus_class : "");
        } else if (state->focus) {
            // old focused element was removed; clear stale pointer so autofocus can fire
            state->focus->current = nullptr;
        }
    }

    // autofocus — if no focus was restored, scan the new tree for an autofocus input
    if (state && (!state->focus || !state->focus->current) &&
        doc->view_tree && doc->view_tree->root) {
        View* af = find_matching_input((View*)doc->view_tree->root, "input", nullptr);
        if (af && af->is_element()) {
            DomElement* af_elem = (DomElement*)af;
            if (af_elem->has_attribute("autofocus")) {
                focus_set(state, af, false);
                caret_set(state, af, 0);
                log_debug("rebuild_lambda_doc: autofocus set on new input");
            }
        }
    }

    // Skip render here — let the main loop handle it via render().
    // Full rebuild: mark dirty tracker for full repaint so the main loop does a full render.
    if (state) {
        state->dirty_tracker.full_repaint = true;
        state->is_dirty = true;
        state->needs_reflow = false;  // layout already done by rebuild
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
            DomElement* parent_dom = (DomElement*)old_dom->parent;
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

    // Phase 16: Helper to mark a subtree as layout_dirty (stack-based, handles arbitrary depth)
    auto mark_dirty_subtree = [](DomNode* root) {
        if (!root) return;
        DomNode* stack[256];
        int top = 0;
        stack[top++] = root;
        while (top > 0) {
            DomNode* n = stack[--top];
            n->layout_dirty = true;
            if (n->is_element()) {
                DomNode* c = ((DomElement*)n)->first_child;
                while (c && top < 255) {
                    stack[top++] = c;
                    c = c->next_sibling;
                }
            }
        }
    };

    // Phase 16: Helper to clear layout_dirty on all nodes
    auto clear_dirty_subtree = [](DomNode* root) {
        if (!root) return;
        DomNode* stack[256];
        int top = 0;
        stack[top++] = root;
        while (top > 0) {
            DomNode* n = stack[--top];
            n->layout_dirty = false;
            if (n->is_element()) {
                DomNode* c = ((DomElement*)n)->first_child;
                while (c && top < 255) {
                    stack[top++] = c;
                    c = c->next_sibling;
                }
            }
        }
    };

    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // Save focus info
    RadiantState* state = (RadiantState*)doc->state;
    const char* focus_tag = nullptr;
    const char* focus_class = nullptr;
    bool had_focus = false;
    if (state && state->focus && state->focus->current) {
        View* focused = state->focus->current;
        if (focused->is_element()) {
            DomElement* felem = (DomElement*)focused;
            focus_tag = felem->tag_name;
            if (felem->class_count > 0 && felem->class_names)
                focus_class = felem->class_names[0];
            had_focus = true;
        }
    }

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
            compute_absolute_bounds((DomNode*)old_dom,
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
        DomElement* parent_dom = (DomElement*)old_dom->parent;

        // Build new DOM subtree from the new Lambda element (not linked to parent yet)
        DomElement* new_dom = build_dom_tree_from_element(new_elem, doc, nullptr);

        if (!new_dom) {
            log_debug("rebuild_lambda_doc_incremental: failed to build subtree for entry %d", i);
            continue;
        }

        // Replace old DOM child with new subtree in parent's linked list
        bool replaced = dom_node_replace_in_parent(parent_dom, (DomNode*)old_dom, (DomNode*)new_dom);
        if (i < 16) new_doms[i] = new_dom;

        // Phase 16: Mark new subtree as layout_dirty
        mark_dirty_subtree((DomNode*)new_dom);

        // Phase 15: Invalidate ancestor styles only (new subtree nodes already have styles_resolved=false)
        // Phase 16: Mark ancestors as layout_dirty for incremental layout
        DomNode* ancestor = (DomNode*)parent_dom;
        while (ancestor) {
            if (ancestor->is_element()) {
                ((DomElement*)ancestor)->styles_resolved = false;
            }
            ancestor->layout_dirty = true;
            ancestor = ancestor->parent;
        }

        // Phase 12.2: Apply CSS cascade to new subtree only
        if (css_engine && inline_sheets && inline_count > 0) {
            SelectorMatcher* matcher = selector_matcher_create(doc->pool);
            for (int s = 0; s < inline_count; s++) {
                if (inline_sheets[s] && inline_sheets[s]->rule_count > 0) {
                    apply_stylesheet_to_dom_tree_fast(new_dom, inline_sheets[s],
                                                      matcher, doc->pool, css_engine);
                }
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
        if (doc->root) clear_dirty_subtree((DomNode*)doc->root);
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
    if (had_focus && state && doc->view_tree && doc->view_tree->root) {
        View* new_focused = find_matching_input(
            (View*)doc->view_tree->root, focus_tag, focus_class);
        if (new_focused) {
            state->focus->current = new_focused;
            log_debug("rebuild_lambda_doc_incremental: restored focus");
        } else if (state->focus) {
            // old focused element was removed; clear stale pointer so autofocus can fire
            state->focus->current = nullptr;
        }
    }

    // Phase 20: autofocus — if no focus was restored and new subtree contains an input,
    // check for autofocus attribute and set focus to it
    if (state && (!state->focus || !state->focus->current)) {
        for (int i = 0; i < result_count; i++) {
            if (new_doms[i]) {
                View* af = find_matching_input((View*)new_doms[i], "input", nullptr);
                if (af && af->is_element()) {
                    DomElement* af_elem = (DomElement*)af;
                    if (af_elem->has_attribute("autofocus")) {
                        focus_set(state, af, false);
                        caret_set(state, af, 0);
                        log_debug("rebuild_incr: autofocus set on new input");
                        break;
                    }
                }
            }
        }
    }

    // Skip render here — let the main loop handle it via render().
    if (state) {
        state->is_dirty = true;
        state->needs_reflow = false;  // layout already done by rebuild
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
    bool continue_on_error;                     // continue processing on errors in batch mode
    bool summary;                               // print summary statistics
};

bool parse_layout_args(int argc, char** argv, LayoutOptions* opts) {
    // Initialize defaults
    opts->input_file_count = 0;
    opts->output_file = nullptr;
    opts->output_dir = nullptr;
    opts->css_file = nullptr;
    opts->view_output_file = nullptr;  // Default to /tmp/view_tree.json
    opts->font_dir_count = 0;
    opts->viewport_width = 1200;  // Standard viewport width for layout tests (matches browser reference)
    opts->viewport_height = 800;  // Standard viewport height for layout tests (matches browser reference)
    opts->debug = false;
    opts->continue_on_error = false;
    opts->summary = false;

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
        else if (strcmp(argv[i], "--continue-on-error") == 0) {
            opts->continue_on_error = true;
        }
        else if (strcmp(argv[i], "--summary") == 0) {
            opts->summary = true;
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
    bool track_source_lines = false
) {
    log_debug("[Layout] Processing file: %s", input_file);

    // Create memory pool for this file
    Pool* pool = pool_create();
    if (!pool) {
        log_error("Failed to create memory pool for %s", input_file);
        return false;
    }

    Url* input_url = url_parse_with_base(input_file, cwd);

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
        log_info("[Layout] Detected PDF file, using PDF→ViewTree pipeline");
        doc = load_pdf_doc(input_url, viewport_width, viewport_height, pool, 1.0f);
    } else if (ext && strcmp(ext, ".svg") == 0) {
        log_info("[Layout] Detected SVG file, using SVG→ViewTree pipeline");
        doc = load_svg_doc(input_url, viewport_width, viewport_height, pool, 1.0f);
    } else {
        doc = load_lambda_html_doc(input_url, css_file, viewport_width, viewport_height, pool,
                                   nullptr, track_source_lines);
    }

    if (!doc) {
        log_error("Failed to load document: %s", input_file);
        pool_destroy(pool);
        return false;
    }

    // Process @font-face rules from stored stylesheets
    process_document_font_faces(ui_context, doc);

    // Perform layout computation
    ui_context->document = doc;

    if (doc->view_tree && doc->view_tree->root) {
        log_info("[Layout] Document already has view_tree (PDF/SVG/image), skipping CSS layout");
    } else {
        log_debug("[Layout] About to call layout_html_doc...");
        layout_html_doc(ui_context, doc, false);
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

        print_view_tree((ViewElement*)doc->view_tree->root, doc->url, output_path);
        log_debug("[Layout] Layout tree written to %s", output_path ? output_path : "/tmp/view_tree.json");

        if (is_pdf || is_svg) {
            set_combine_text_nodes(true);
        }
    }

    // Cleanup: destroy view tree, document, and per-file CSS pool.
    // DomDocument and ViewTree are malloc-allocated with their own internal pools.
    // Failing to free them leaks pools/arenas across batch files, which can cause
    // rpmalloc heap corruption that manifests as SIGTRAP in system malloc.

    if (doc) {
        // Clean up retained JS state (MIR context, event registry, runtime heap)
        // before destroying the document that owns the pointers.
        script_runner_cleanup_js_state(doc);

        if (doc->view_tree) {
            view_pool_destroy(doc->view_tree);
            mem_free(doc->view_tree);
            doc->view_tree = nullptr;
        }
        dom_document_destroy(doc);
    }

    // Free the input URL (dom_document_destroy doesn't own it)
    if (input_url) {
        url_destroy(input_url);
        input_url = nullptr;
    }

    pool_destroy(pool);

    // Reset JS runtime state to avoid cross-document leakage in batch mode.
    // Must happen BEFORE script_runner_cleanup_heap: js_batch_reset clears
    // global Items (js_input, js_exception_value, etc.) that reference the
    // JS heap. Freeing the heap first would leave dangling pointers.
    js_batch_reset();
    js_dom_batch_reset();
    js_globals_batch_reset();

    // Drain the mmap pool from JS execution (after js_batch_reset cleared globals).
    script_runner_cleanup_heap();

    // Reset per-document font state to avoid cross-document cache pollution in batch mode.
    font_context_reset_document_fonts(ui_context->font_ctx);
    font_context_reset_glyph_caches(ui_context->font_ctx);
    ui_context->font_face_count = 0;

    // [DIAG] Track memory usage per file for leak detection
    {
#ifdef __APPLE__
        // use mach_task_basic_info for CURRENT RSS (not peak)
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
        long rss_kb = (long)(info.resident_size / 1024);
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
    extern void image_cache_cleanup(UiContext* uicon);
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

/**
 * Main layout command implementation using Lambda CSS and Radiant layout.
 * Supports both single-file and batch modes.
 */
// Crash recovery for layout_single_file — catches SIGSEGV/SIGBUS from
// Apple framework code (CoreText/CoreGraphics font rasterisation, vImage lazy load, etc.)
// that cannot be fixed in our code.
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

// Legacy crash handler for SIGTRAP/SIGABRT (non-recoverable)
static void crash_signal_handler(int sig) {
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
    fprintf(stderr, "=== END BACKTRACE ===\n");
    _exit(128 + sig);
}

int cmd_layout(int argc, char** argv) {
    // Install crash signal handlers for diagnostics
    signal(SIGTRAP, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    // SIGSEGV/SIGBUS use sigaction for crash recovery (siglongjmp) support
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = layout_crash_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }

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

    // Initialize UI context once (shared across all files in batch mode)
    log_debug("[Layout] Initializing UI context (headless mode)...");
    UiContext ui_context;
    memset(&ui_context, 0, sizeof(UiContext));

    if (ui_context_init(&ui_context, true) != 0) {
        log_error("Failed to initialize UI context");
        return 1;
    }

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
                    opts.debug
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
    log_debug("[Cleanup] Starting cleanup...");
    log_debug("[Cleanup] Cleaning up UI context...");
    ui_context_cleanup(&ui_context);
    log_debug("[Cleanup] Complete");

    printf("Completed layout command: %d success, %d failed\n", success_count, failure_count);
    log_notice("Completed layout command: %d success, %d failed", success_count, failure_count);
    return failure_count > 0 ? 1 : 0;
}
