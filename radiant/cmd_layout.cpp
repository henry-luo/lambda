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
#include <stdlib.h>
#include <chrono>       // timing - acceptable for profiling
#include <limits.h>
extern "C" {
#include "../lib/mempool.h"
#include "../lib/file.h"
#include "../lib/string.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include "../lib/url.h"
#include "../lib/log.h"
#include "../lib/image.h"
#include "../lib/memtrack.h"
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
#include "../lambda/input/html5/html5_parser.h"
#include "../lambda/format/format.h"
#include "../lambda/transpiler.hpp"
#include "../lambda/mark_builder.hpp"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/font_face.h"
#include "../radiant/pdf/pdf_to_view.hpp"
#include "../radiant/script_runner.h"
#include "../lambda/render_map.h"
#include "../lambda/template_state.h"

// External C++ function declarations from Radiant
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// print_view_tree is declared in layout.hpp
// print_item is declared in lambda/ast.hpp

// Forward declarations
Element* get_html_root_element(Input* input);
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
void apply_stylesheet_to_dom_tree_fast(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
static void apply_rule_to_dom_element(DomElement* elem, CssRule* rule, SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count);
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool, CssStylesheet*** stylesheets, int* count);
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count);
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool);
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool);
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
                        // Unrecognized publicId — assume HTML5
                        log_debug("Unrecognized publicId, defaulting to HTML5");
                        return HTML5;
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
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool) {
    if (!dom_elem || !html_elem || !pool) return;

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
            //   - <script> elements → explicitly skipped (display:none, no layout role)
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
                    str_ieq_const(ctn, ctn_len, "script") ||
                    (ctn_len > 0 && ctn[0] == '?');  // XML declarations
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
            apply_inline_styles_to_tree(dom_child_elem, html_child, pool);

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
            free(css_content);
            continue;
        }
        str_copy(css_pool_copy, css_len + 1, css_content, css_len);
        free(css_content);

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
 * Recursively collect <link rel="stylesheet"> references from HTML
 * Loads and parses external CSS files
 */
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool, CssStylesheet*** stylesheets, int* count) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <link> element
    if (str_ieq_const(type->name.str, strlen(type->name.str), "link")) {
        // Extract 'rel' and 'href' attributes
        const char* rel = extract_element_attribute(elem, "rel", nullptr);
        const char* href = extract_element_attribute(elem, "href", nullptr);

        if (rel && href && str_ieq_const(rel, strlen(rel), "stylesheet")) {
            // Check media attribute - skip stylesheets that don't apply to screen
            const char* media = extract_element_attribute(elem, "media", nullptr);
            if (media && !css_evaluate_media_query(engine, media)) {
                log_debug("[CSS] Skipping <link> stylesheet '%s' - media '%s' does not match screen", href, media);
            } else {
            log_debug("[CSS] Found <link rel='stylesheet' href='%s'>", href);

            // Resolve relative path using base_path (supports file:// and http:// URLs)
            char css_path[1024];
            bool is_http_css = false;

            if (href[0] == '/' && href[1] != '/') {
                // Absolute local path - use as-is
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
                                free(local_path);
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
            if (is_http_css) {
                // Download CSS from HTTP URL
                size_t content_size = 0;
                css_content = download_http_content(css_path, &content_size, nullptr);
                if (css_content) {
                    log_debug("[CSS] Downloaded stylesheet from URL: %s (%zu bytes)", css_path, content_size);
                }
            } else {
                css_content = read_text_file(css_path);
            }
            if (css_content) {
                size_t css_len = strlen(css_content);
                char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
                if (css_pool_copy) {
                    str_copy(css_pool_copy, css_len + 1, css_content, css_len);
                    free(css_content);

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
                    free(css_content);
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
            collect_linked_stylesheets(child_item.element, engine, base_path, pool, stylesheets, count);
        }
    }
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and returns list of stylesheets
 */
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;

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
            collect_inline_styles_to_list(child_item.element, engine, pool, stylesheets, count);
        }
    }
}

/**
 * Recursively collect <style> inline CSS from HTML
 * Parses and adds to engine's stylesheet list
 */
void collect_inline_styles(Element* elem, CssEngine* engine, Pool* pool) {
    if (!elem || !engine || !pool) return;

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
            collect_inline_styles(child_item.element, engine, pool);
        }
    }
}

/**
 * Master function to extract and apply all CSS from HTML document
 * Handles linked stylesheets, <style> elements, and inline style attributes
 * Returns array of collected stylesheets
 */
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count) {
    if (!html_root || !engine || !pool || !stylesheet_count) return nullptr;

    log_debug("[CSS] Extracting CSS from HTML document...");

    *stylesheet_count = 0;
    CssStylesheet** stylesheets = nullptr;

    // Step 1: Collect and parse <link rel="stylesheet"> references
    log_debug("[CSS] Step 1: Collecting linked stylesheets...");
    collect_linked_stylesheets(html_root, engine, base_path, pool, &stylesheets, stylesheet_count);

    // Step 2: Collect and parse <style> inline CSS
    log_debug("[CSS] Step 2: Collecting inline <style> elements...");
    collect_inline_styles_to_list(html_root, engine, pool, &stylesheets, stylesheet_count);

    log_debug("[CSS] Collected %d stylesheet(s) from HTML", *stylesheet_count);
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
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool, CssEngine* engine) {
    if (!root || !stylesheet || !matcher || !pool) return;

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
            apply_stylesheet_to_dom_tree(child_elem, stylesheet, matcher, pool, engine);
        }
        child = child->next_sibling;
    }
}

/**
 * Apply CSS stylesheet rules to DOM tree using selector index
 * This is an optimized O(n) version that uses pre-built index
 */
static void apply_stylesheet_to_dom_tree_indexed(DomElement* root, SelectorIndex* index,
                                                  SelectorMatcher* matcher, Pool* pool, CssEngine* engine) {
    if (!root || !index || !matcher || !pool) return;

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
            apply_stylesheet_to_dom_tree_indexed(child_elem, index, matcher, pool, engine);
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
    apply_stylesheet_to_dom_tree_indexed(root, index, matcher, pool, engine);

    // Free index
    free_selector_index(index);
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
        html_content = download_http_content(url_str, &content_size, nullptr);
        if (!html_content) {
            log_error("Failed to download HTML from URL: %s", url_str);
            return nullptr;
        }
        html_content_owned = true;
    } else {
        html_content = read_text_file(html_filepath);
        if (!html_content) {
            log_error("Failed to read HTML file: %s", html_filepath);
            return nullptr;
        }
        html_content_owned = true;
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
            Html5ParseOptions parse_opts = { .track_source_lines = true };
            Element* doc = html5_parse_ex(input, html_content, &parse_opts);
            if (doc) {
                input->root = (Item){.element = doc};
            }
        }
    } else {
        input = input_from_source(html_content, html_url, type_str, nullptr);
    }
    if (html_content_owned) free(html_content);  // only free what we allocated

    auto t_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse HTML: %.1fms", duration<double, std::milli>(t_parse - t_read).count());

    if (!input) {
        log_error("Failed to create input for file: %s", html_filepath);
        mem_free(type_str);
        return nullptr;
    }

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
                free(css_content);
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
                free(css_content);
            }
        } else {
            log_warn("Failed to load CSS file: %s", css_filename);
        }
    }

    // Extract and parse <style> elements
    int inline_stylesheet_count = 0;
    const char* css_base_path = url_get_href(html_url);
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, css_base_path, pool, &inline_stylesheet_count);

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

    // Detect file type by extension
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
        free(pdf_content);
        return nullptr;
    }

    // Parse PDF content with explicit size
    parse_pdf(input, pdf_content, pdf_size);
    free(pdf_content);  // Done with raw content

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

    // Load SVG using ThorVG (v1.0-pre34: Tvg_Paint is already a pointer type)
    Tvg_Paint pic = tvg_picture_new();
    Tvg_Result ret = tvg_picture_load(pic, svg_filepath);
    if (ret != TVG_RESULT_SUCCESS) {
        log_error("Failed to load SVG: %s (error: %d)", svg_filepath, ret);
        tvg_paint_unref(pic, true);
        pool_destroy(view_pool);
        return nullptr;
    }

    // Get SVG intrinsic size from viewBox
    float svg_width, svg_height;
    tvg_picture_get_size(pic, &svg_width, &svg_height);
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
        tvg_paint_unref(pic, true);
        pool_destroy(view_pool);
        return nullptr;
    }
    view_tree->pool = view_pool;
    view_tree->html_version = HTML5;

    // Create root ViewBlock to hold the SVG
    ViewBlock* root_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!root_view) {
        log_error("Failed to allocate root view for SVG");
        tvg_paint_unref(pic, true);
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
        free(text_content);  // from read_text_file, uses stdlib
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
    free(text_content);  // from read_text_file, uses stdlib

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
    free(markdown_content);  // from read_text_file, uses stdlib

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
                        MathInfo* mi = (MathInfo*)malloc(sizeof(MathInfo));
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
            free(math_list->data[i]);
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
            free(css_content);
            markdown_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (markdown_stylesheet) {
                log_debug("[Lambda Markdown] Loaded markdown stylesheet with %zu rules",
                        markdown_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse markdown.css");
            }
        } else {
            free(css_content);
        }
    } else {
        log_warn("Failed to load markdown.css file: %s", css_filename);
        log_warn("Continuing without stylesheet - markdown will use browser defaults");
    }
    free(css_filename);

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
                free(math_css_content);
                math_stylesheet = css_parse_stylesheet(css_engine, math_css_pool, math_css_filename);
                if (math_stylesheet) {
                    log_debug("[Lambda Markdown] Loaded math stylesheet with %zu rules",
                              math_stylesheet->rule_count);
                }
            } else {
                free(math_css_content);
            }
        }
        free(math_css_filename);

        char* katex_css_filename = lambda_home_path("input/latex/css/katex.css");
        char* katex_css_content = read_text_file(katex_css_filename);
        if (katex_css_content) {
            size_t katex_css_len = strlen(katex_css_content);
            char* katex_css_pool = (char*)pool_alloc(pool, katex_css_len + 1);
            if (katex_css_pool) {
                str_copy(katex_css_pool, katex_css_len + 1, katex_css_content, katex_css_len);
                free(katex_css_content);
                katex_stylesheet = css_parse_stylesheet(css_engine, katex_css_pool, katex_css_filename);
                if (katex_stylesheet) {
                    log_debug("[Lambda Markdown] Loaded KaTeX font stylesheet with %zu rules",
                              katex_stylesheet->rule_count);
                }
            } else {
                free(katex_css_content);
            }
        }
        free(katex_css_filename);
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
    free(wiki_content);  // from read_text_file, uses stdlib

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
            free(css_content);
            wiki_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (wiki_stylesheet) {
                log_debug("[Lambda Wiki] Loaded wiki stylesheet with %zu rules",
                        wiki_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse wiki.css");
            }
        } else {
            free(css_content);
        }
    } else {
        log_warn("Failed to load wiki.css file: %s", css_filename);
        log_warn("Continuing without stylesheet - wiki will use browser defaults");
    }
    free(css_filename);

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
            free(css_content);
            latex_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
            if (latex_stylesheet) {
                log_debug("[Lambda LaTeX] Loaded LaTeX stylesheet with %zu rules",
                        latex_stylesheet->rule_count);
            } else {
                log_warn("Failed to parse latex.css");
            }
        } else {
            free(css_content);
        }
    } else {
        log_debug("No latex.css file found, LaTeX HTML will use embedded and inline styles");
    }
    free(css_filename);

    // Load KaTeX font stylesheet for math rendering (@font-face declarations for KaTeX_Size1-4 etc.)
    char* katex_css_filename = lambda_home_path("input/latex/css/katex.css");
    CssStylesheet* katex_stylesheet = nullptr;
    char* katex_css_content = read_text_file(katex_css_filename);
    if (katex_css_content) {
        size_t katex_css_len = strlen(katex_css_content);
        char* katex_css_pool_copy = (char*)pool_alloc(pool, katex_css_len + 1);
        if (katex_css_pool_copy) {
            str_copy(katex_css_pool_copy, katex_css_len + 1, katex_css_content, katex_css_len);
            free(katex_css_content);
            katex_stylesheet = css_parse_stylesheet(css_engine, katex_css_pool_copy, katex_css_filename);
            if (katex_stylesheet) {
                log_debug("[Lambda LaTeX] Loaded KaTeX font stylesheet with %zu rules",
                        katex_stylesheet->rule_count);
            }
        } else {
            free(katex_css_content);
        }
    }
    free(katex_css_filename);

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

    // Step 2: Parse XML into Lambda elements using input_from_source
    String* type_str = (String*)mem_alloc(sizeof(String) + 4, MEM_CAT_LAYOUT);
    type_str->len = 3;
    str_copy(type_str->chars, type_str->len + 1, "xml", 3);

    Input* xml_input = input_from_source(xml_content, xml_url, type_str, nullptr);
    free(xml_content);  // from read_text_file, uses stdlib

    if (!xml_input || !xml_input->root.item || xml_input->root.item == ITEM_ERROR) {
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
            free(css_content);
            return nullptr;
        }
        free(css_content);
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

    Runtime* runtime = (Runtime*)calloc(1, sizeof(Runtime));
    runtime_init(runtime);

    log_debug("[Lambda Script] Evaluating script...");
    // Use MIR Direct JIT to evaluate the Lambda script (functional scripts don't need a main() entry point)
    Input* script_output = run_script_mir(runtime, nullptr, script_filepath, false);

    if (!script_output || !script_output->root.item) {
        log_error("[Lambda Script] Failed to evaluate script or script returned null");
        runtime_cleanup(runtime);
        free(runtime);
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
        free(runtime);
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
        free(runtime);
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
            free(runtime);
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
            free(runtime);
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

            // Use MarkBuilder to create the div element properly
            MarkBuilder builder(script_output);
            ElementBuilder div = builder.element("div");
            div.text(result_str->str);
            Item div_item = div.final();
            result_elem = div_item.element;

            strbuf_free(result_str);
        }

        auto step2_start = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 2 - Wrap result: %.1fms",
            std::chrono::duration<double, std::milli>(step2_start - step1_end).count());

        // Step 3: Create HTML wrapper structure using MarkBuilder
        log_debug("[Lambda Script] Building HTML wrapper structure");

        MarkBuilder builder(script_output);

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

        // Step 4: Update script_output root to point to html element
        script_output->root = html_item;
    } else {
        auto step2_start = std::chrono::high_resolution_clock::now();
        log_info("[TIMING] Step 2 - HTML document detected, skipping wrap: %.1fms",
            std::chrono::duration<double, std::milli>(step2_start - step1_end).count());
    }

    // Step 5: Create DomDocument and build DomElement tree
    auto step5_start = std::chrono::high_resolution_clock::now();
    DomDocument* dom_doc = dom_document_create(script_output);
    if (!dom_doc) {
        log_error("[Lambda Script] Failed to create DomDocument");
        runtime_cleanup(runtime);
        free(runtime);
        return nullptr;
    }

    // Ensure CSS property system is initialized before building DOM tree,
    // so that inline style declarations get correct property IDs.
    css_property_system_init(dom_doc->pool);

    DomElement* dom_root = build_dom_tree_from_element(html_elem, dom_doc, nullptr);
    if (!dom_root) {
        log_error("[Lambda Script] Failed to build DomElement tree");
        dom_document_destroy(dom_doc);
        runtime_cleanup(runtime);
        free(runtime);
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
        free(runtime);
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
                free(css_content);
                script_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
                if (script_stylesheet) {
                    log_debug("[Lambda Script] Loaded script stylesheet with %zu rules",
                            script_stylesheet->rule_count);
                } else {
                    log_warn("[Lambda Script] Failed to parse script.css");
                }
            } else {
                free(css_content);
            }
        } else {
            log_debug("[Lambda Script] No script.css file found, using browser defaults");
        }
        free(css_filename);
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
void rebuild_lambda_doc(UiContext* uicon) {
    if (!uicon || !uicon->document) {
        log_error("rebuild_lambda_doc: no document");
        return;
    }

    DomDocument* doc = uicon->document;
    Element* html_elem = doc->html_root;
    if (!html_elem) {
        log_error("rebuild_lambda_doc: no html_root in document");
        return;
    }

    log_info("rebuild_lambda_doc: rebuilding DOM from updated Lambda elements");

    // ensure CSS property system is initialized
    css_property_system_init(doc->pool);

    // rebuild DOM tree from Lambda elements
    DomElement* new_root = build_dom_tree_from_element(html_elem, doc, nullptr);
    if (!new_root) {
        log_error("rebuild_lambda_doc: failed to rebuild DOM tree");
        return;
    }

    // replace old DOM root
    doc->root = new_root;

    // re-extract and apply inline <style> elements
    CssEngine* css_engine = css_engine_create(doc->pool);
    if (css_engine) {
        int inline_count = 0;
        CssStylesheet** inline_sheets = extract_and_collect_css(
            html_elem, css_engine, nullptr, doc->pool, &inline_count);

        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        if (inline_sheets && inline_count > 0) {
            for (int i = 0; i < inline_count; i++) {
                if (inline_sheets[i] && inline_sheets[i]->rule_count > 0) {
                    apply_stylesheet_to_dom_tree_fast(new_root, inline_sheets[i],
                                                      matcher, doc->pool, css_engine);
                }
            }
        }
    }

    // apply inline style attributes
    apply_inline_styles_to_tree(new_root, html_elem, doc->pool);

    // mark view tree dirty for full relayout
    doc->view_tree = nullptr;  // force full layout rebuild

    // trigger relayout + repaint
    layout_html_doc(uicon, doc, false);
    if (doc->view_tree) {
        render_html_doc(uicon, doc->view_tree, NULL);
    }

    log_info("rebuild_lambda_doc: DOM rebuild and relayout complete");
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

    // Cleanup document and pool for this file
    // Note: free_document is handled by pool_destroy since doc is allocated from pool
    pool_destroy(pool);

    // Reset per-document font state to avoid cross-document cache pollution in batch mode.
    // Clears @font-face descriptors, face cache, and codepoint fallback cache.
    font_context_reset_document_fonts(ui_context->font_ctx);
    ui_context->font_face_count = 0;

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
int cmd_layout(int argc, char** argv) {
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

    // Set viewport dimensions
    ui_context.window_width = opts.viewport_width;
    ui_context.window_height = opts.viewport_height;

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
