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
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <chrono>
#include <string>
#include <unistd.h>
#include <limits.h>
extern "C" {
#include "../lib/mempool.h"
#include "../lib/file.h"
#include "../lib/string.h"
#include "../lib/strbuf.h"
#include "../lib/url.h"
#include "../lib/log.h"
}

#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/font_face.h"

// Forward declaration of format_latex_html_v2 in lambda namespace
namespace lambda {
    Item format_latex_html_v2(Input* input, bool text_mode);
}

// External C++ function declarations from Radiant
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// print_view_tree is declared in layout.hpp
void print_item(StrBuf *strbuf, Item item, int depth=0, char* indent="  ");

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
    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_LIST) {
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
                if (type && strcasecmp(type->name.str, "!DOCTYPE") == 0) {
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
                            if (strncasecmp(content, "html", 4) == 0) {
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

            // Skip HTML comments and DOCTYPE - they are stored as DomComment nodes
            // in the DOM tree, not DomElement, so we just skip them here.
            // The DOM iterator will skip DomComment nodes via !is_element() check below.
            TypeElmt* child_type = (TypeElmt*)html_child->type;
            if (child_type && (strcmp(child_type->name.str, "!--") == 0 ||
                               strcasecmp(child_type->name.str, "!DOCTYPE") == 0)) {
                continue;  // Skip comment/DOCTYPE - DOM has corresponding DomComment
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

    if (root_type == LMD_TYPE_LIST) {
        // Old parser: root is a list, search for HTML element
        List* root_list = input->root.list;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments (case-insensitive for DOCTYPE)
                if (strcasecmp(type->name.str, "!DOCTYPE") != 0 &&
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
 * Recursively collect <link rel="stylesheet"> references from HTML
 * Loads and parses external CSS files
 */
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool, CssStylesheet*** stylesheets, int* count) {
    if (!elem || !engine || !pool || !stylesheets || !count) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <link> element
    if (strcasecmp(type->name.str, "link") == 0) {
        // Extract 'rel' and 'href' attributes
        const char* rel = extract_element_attribute(elem, "rel", nullptr);
        const char* href = extract_element_attribute(elem, "href", nullptr);

        if (rel && href && strcasecmp(rel, "stylesheet") == 0) {
            log_debug("[CSS] Found <link rel='stylesheet' href='%s'>", href);

            // Resolve relative path using base_path
            char css_path[1024];
            if (href[0] == '/' || strstr(href, "://") != nullptr) {
                // Absolute path or URL - use as-is (URLs won't load locally)
                strncpy(css_path, href, sizeof(css_path) - 1);
                css_path[sizeof(css_path) - 1] = '\0';
            } else if (base_path) {
                // Relative path - resolve against base_path
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
            } else {
                strncpy(css_path, href, sizeof(css_path) - 1);
                css_path[sizeof(css_path) - 1] = '\0';
            }

            log_debug("[CSS] Loading stylesheet from: %s", css_path);

            // Load and parse CSS file
            char* css_content = read_text_file(css_path);
            if (css_content) {
                size_t css_len = strlen(css_content);
                char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
                if (css_pool_copy) {
                    strcpy(css_pool_copy, css_content);
                    free(css_content);

                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_pool_copy, css_path);
                    if (stylesheet && stylesheet->rule_count > 0) {
                        log_debug("[CSS] Parsed linked stylesheet '%s': %zu rules", css_path, stylesheet->rule_count);

                        // Add to list
                        *stylesheets = (CssStylesheet**)pool_realloc(pool, *stylesheets,
                                                                      (*count + 1) * sizeof(CssStylesheet*));
                        (*stylesheets)[*count] = stylesheet;
                        (*count)++;
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
    if (strcasecmp(type->name.str, "style") == 0) {
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
    if (strcasecmp(type->name.str, "style") == 0) {
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

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

// A rule entry in the index
struct IndexedRule {
    CssRule* rule;
    CssSelector* selector;  // The specific selector that matched the index key
    int rule_index;         // Original position for source order
};

// Selector index for fast rule lookup
struct SelectorIndex {
    std::unordered_map<std::string, std::vector<IndexedRule>> by_tag;      // rules indexed by tag name
    std::unordered_map<std::string, std::vector<IndexedRule>> by_class;    // rules indexed by class
    std::unordered_map<std::string, std::vector<IndexedRule>> by_id;       // rules indexed by id
    std::vector<IndexedRule> universal;                                     // rules that match any element (*, etc.)
    std::vector<CssRule*> media_rules;                                      // @media rules requiring runtime evaluation
    Pool* pool;
};

// Get the key selector (rightmost compound selector) from a selector
static CssCompoundSelector* get_key_selector(CssSelector* selector) {
    if (!selector || selector->compound_selector_count == 0) return nullptr;
    return selector->compound_selectors[selector->compound_selector_count - 1];
}

// Extract indexing keys from a compound selector
static void extract_index_keys(CssCompoundSelector* compound,
                               const char** out_tag,
                               std::vector<const char*>& out_classes,
                               const char** out_id) {
    *out_tag = nullptr;
    *out_id = nullptr;
    out_classes.clear();

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
                if (simple->value) {
                    out_classes.push_back(simple->value);
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
    SelectorIndex* index = new SelectorIndex();
    index->pool = pool;

    for (size_t rule_idx = 0; rule_idx < stylesheet->rule_count; rule_idx++) {
        CssRule* rule = stylesheet->rules[rule_idx];
        if (!rule) continue;

        // Collect media rules separately for runtime evaluation
        if (rule->type == CSS_RULE_MEDIA) {
            index->media_rules.push_back(rule);
            continue;
        }

        if (rule->type != CSS_RULE_STYLE) continue;

        // Handle selector groups (comma-separated)
        CssSelectorGroup* group = rule->data.style_rule.selector_group;
        CssSelector* single = rule->data.style_rule.selector;

        std::vector<CssSelector*> selectors_to_index;
        if (group && group->selector_count > 0) {
            for (size_t i = 0; i < group->selector_count; i++) {
                if (group->selectors[i]) {
                    selectors_to_index.push_back(group->selectors[i]);
                }
            }
        } else if (single) {
            selectors_to_index.push_back(single);
        }

        for (CssSelector* selector : selectors_to_index) {
            CssCompoundSelector* key_compound = get_key_selector(selector);
            if (!key_compound) continue;

            const char* tag = nullptr;
            const char* id = nullptr;
            std::vector<const char*> classes;
            extract_index_keys(key_compound, &tag, classes, &id);

            IndexedRule entry = { rule, selector, (int)rule_idx };

            // Index by most specific key first (ID > class > tag)
            bool indexed = false;

            if (id) {
                index->by_id[id].push_back(entry);
                indexed = true;
            }

            if (!classes.empty()) {
                // Index by first class (could index by all, but one is usually enough)
                index->by_class[classes[0]].push_back(entry);
                indexed = true;
            }

            if (tag) {
                index->by_tag[tag].push_back(entry);
                indexed = true;
            }

            // If no specific key, it's a universal rule
            if (!indexed) {
                index->universal.push_back(entry);
            }
        }
    }

    return index;
}

// Free selector index
static void free_selector_index(SelectorIndex* index) {
    delete index;
}

// Get candidate rules for an element from the index
static void get_candidate_rules(SelectorIndex* index, DomElement* elem,
                                std::vector<IndexedRule>& candidates) {
    candidates.clear();
    std::unordered_set<CssRule*> seen;  // Avoid duplicates

    // Add universal rules
    for (const auto& entry : index->universal) {
        if (seen.find(entry.rule) == seen.end()) {
            candidates.push_back(entry);
            seen.insert(entry.rule);
        }
    }

    // Add rules by tag
    if (elem->tag_name) {
        auto it = index->by_tag.find(elem->tag_name);
        if (it != index->by_tag.end()) {
            for (const auto& entry : it->second) {
                if (seen.find(entry.rule) == seen.end()) {
                    candidates.push_back(entry);
                    seen.insert(entry.rule);
                }
            }
        }
    }

    // Add rules by ID
    if (elem->id) {
        auto it = index->by_id.find(elem->id);
        if (it != index->by_id.end()) {
            for (const auto& entry : it->second) {
                if (seen.find(entry.rule) == seen.end()) {
                    candidates.push_back(entry);
                    seen.insert(entry.rule);
                }
            }
        }
    }

    // Add rules by class
    if (elem->class_count > 0 && elem->class_names) {
        for (int i = 0; i < elem->class_count; i++) {
            if (elem->class_names[i]) {
                auto it = index->by_class.find(elem->class_names[i]);
                if (it != index->by_class.end()) {
                    for (const auto& entry : it->second) {
                        if (seen.find(entry.rule) == seen.end()) {
                            candidates.push_back(entry);
                            seen.insert(entry.rule);
                        }
                    }
                }
            }
        }
    }

    // Sort by rule_index to maintain source order
    std::sort(candidates.begin(), candidates.end(),
              [](const IndexedRule& a, const IndexedRule& b) {
                  return a.rule_index < b.rule_index;
              });
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
        // Try matching each selector in the group
        bool any_match = false;
        CssSpecificity best_specificity = {0, 0, 0, 0, false};
        PseudoElementType pseudo_element = PSEUDO_ELEMENT_NONE;

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
                any_match = true;
                best_specificity = match_result.specificity;
                pseudo_element = match_result.pseudo_element;
                break;
            }
        }

        if (any_match && rule->data.style_rule.declaration_count > 0) {
            if (pseudo_element != PSEUDO_ELEMENT_NONE) {
                dom_element_apply_pseudo_element_rule(elem, rule, best_specificity, (int)pseudo_element);
            } else {
                dom_element_apply_rule(elem, rule, best_specificity);
            }
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
    std::vector<IndexedRule> candidates;
    get_candidate_rules(index, root, candidates);
    g_candidate_rule_count += candidates.size();

    // Only check candidate rules (much fewer than all rules)
    for (const auto& entry : candidates) {
        apply_rule_to_dom_element(root, entry.rule, matcher, pool, engine);
    }

    // Also process media rules (need runtime evaluation for each element)
    for (CssRule* media_rule : index->media_rules) {
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
    int viewport_width, int viewport_height, Pool* pool) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    if (!html_url || !pool) {
        log_error("load_lambda_html_doc: invalid parameters");
        return nullptr;
    }

    char* html_filepath = url_to_local_path(html_url);
    log_debug("[Lambda CSS] Loading HTML document: %s", html_filepath);
    // Step 1: Parse HTML with Lambda parser
    char* html_content = read_text_file(html_filepath);
    if (!html_content) {
        log_error("Failed to read HTML file: %s", html_filepath);
        return nullptr;
    }

    auto t_read = high_resolution_clock::now();
    log_info("[TIMING] load: read file: %.1fms", duration<double, std::milli>(t_read - t_start).count());

    // Create type string for HTML
    String* type_str = (String*)malloc(sizeof(String) + 5);
    type_str->len = 4;
    strcpy(type_str->chars, "html");

    Input* input = input_from_source(html_content, html_url, type_str, nullptr);
    free(html_content);

    auto t_parse = high_resolution_clock::now();
    log_info("[TIMING] load: parse HTML: %.1fms", duration<double, std::milli>(t_parse - t_read).count());

    StrBuf* htm_buf = strbuf_new();
    print_item(htm_buf, input->root, 0);
    // write html to 'html_tree.txt' for debugging
    FILE* htm_file = fopen("html_tree.txt", "w");
    if (htm_file) {
        fwrite(htm_buf->str, 1, htm_buf->length, htm_file);
        fclose(htm_file);
    }
    strbuf_free(htm_buf);

    auto t_debug = high_resolution_clock::now();
    log_info("[TIMING] load: debug output: %.1fms", duration<double, std::milli>(t_debug - t_parse).count());

    if (!input) {
        log_error("Failed to create input for file: %s", html_filepath);
        return nullptr;
    }

    Element* html_root = get_html_root_element(input);
    if (!html_root) {
        log_error("Failed to get HTML root element");
        return nullptr;
    }

    // Detect HTML version from the original input tree (contains DOCTYPE)
    int detected_version = HTML4_01_TRANSITIONAL;  // Default to quirks mode
    if (input) {
        detected_version = detect_html_version_from_lambda_element(nullptr, input);
        log_debug("[Lambda CSS] Detected HTML version: %d", detected_version);
    }
    log_debug("Parsed HTML root element");
    log_root_item((Item){.element = html_root});

    // Step 2: Create DomDocument and build DomElement tree from Lambda Element tree
    log_debug("Building DomElement tree with Lambda backing (input=%p)!!!", (void*)input);
    DomDocument* dom_doc = dom_document_create(input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

    DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree");
        dom_document_destroy(dom_doc);
        return nullptr;
    }

    auto t_dom = high_resolution_clock::now();
    log_info("[TIMING] load: build DOM tree: %.1fms", duration<double, std::milli>(t_dom - t_debug).count());

    log_debug("Built DomElement tree: root=%p, backed=%s",
              (void*)dom_root,
              (dom_root->native_element && dom_root->doc) ? "YES" : "NO");

    // Step 3: Initialize CSS engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        return nullptr;
    }
    css_engine_set_viewport(css_engine, viewport_width, viewport_height);

    // Step 4: Load external CSS if provided
    CssStylesheet* external_stylesheet = nullptr;
    if (css_filename) {
        log_debug("[Lambda CSS] Loading external CSS: %s", css_filename);
        char* css_content = read_text_file(css_filename);
        if (css_content) {
            size_t css_len = strlen(css_content);
            char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
            if (css_pool_copy) {
                strcpy(css_pool_copy, css_content);
                free(css_content);
                external_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
                if (external_stylesheet) {
                    // Print parsed stylesheet for debugging
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

    // Step 5: Extract and parse <style> elements
    log_debug("[Lambda CSS] Extracting inline <style> elements...");
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = extract_and_collect_css(
        html_root, css_engine, html_filepath, pool, &inline_stylesheet_count);

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

    // Store stylesheets in DomDocument for @font-face processing later
    // (after UiContext is initialized in cmd_layout_main)
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
        log_debug("[Lambda CSS] Stored %d stylesheets in DomDocument for @font-face processing",
                  dom_doc->stylesheet_count);
    }

    // Step 6: Apply CSS cascade (external + <style> elements)
    log_debug("[Lambda CSS] Applying CSS cascade...");
    log_debug("[Lambda CSS] inline_stylesheet_count = %d", inline_stylesheet_count);
    log_debug("[Lambda CSS] external_stylesheet = %p, rule_count = %d",
              external_stylesheet, external_stylesheet ? external_stylesheet->rule_count : -1);
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

    // Step 7: Apply inline style="" attributes (highest priority)
    log_debug("[Lambda CSS] Applying inline style attributes...");
    apply_inline_styles_to_tree(dom_root, html_root, pool);
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

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] load: total: %.1fms", duration<double, std::milli>(t_end - t_start).count());

    log_debug("[Lambda CSS] Document loaded and styled");
    return dom_doc;
}

DomDocument* load_html_doc(Url *base, char* doc_url, int viewport_width, int viewport_height) {
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

    if (ext && (strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0)) {
        // Load LaTeX document: parse LaTeX → convert to HTML → layout
        log_info("[load_html_doc] Detected LaTeX file, using LaTeX→HTML pipeline");
        doc = load_latex_doc(full_url, viewport_width, viewport_height, pool);
    } else if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0)) {
        // Load Markdown document: parse Markdown → convert to HTML → layout
        log_info("[load_html_doc] Detected Markdown file, using Markdown→HTML pipeline");
        doc = load_markdown_doc(full_url, viewport_width, viewport_height, pool);
    } else {
        // Load HTML document with Lambda CSS system
        doc = load_lambda_html_doc(full_url, NULL, viewport_width, viewport_height, pool);
    }

    return doc;
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
    log_info("[TIMING] Loading markdown document: %s", markdown_filepath);

    // Step 1: Parse markdown with Lambda parser
    auto step1_start = std::chrono::high_resolution_clock::now();
    char* markdown_content = read_text_file(markdown_filepath);
    if (!markdown_content) {
        log_error("Failed to read markdown file: %s", markdown_filepath);
        return nullptr;
    }

    // Create type string for markdown
    String* type_str = (String*)malloc(sizeof(String) + 9);
    type_str->len = 8;
    strcpy(type_str->chars, "markdown");

    // Parse markdown to Lambda Element tree
    Input* input = input_from_source(markdown_content, markdown_url, type_str, nullptr);
    free(markdown_content);

    if (!input) {
        log_error("Failed to parse markdown file: %s", markdown_filepath);
        return nullptr;
    }

    // Get root element from parsed markdown
    Element* markdown_root = nullptr;
    TypeId root_type = get_type_id(input->root);
    if (root_type == LMD_TYPE_ELEMENT) {
        markdown_root = input->root.element;
    } else if (root_type == LMD_TYPE_LIST) {
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
    // Determine the path to markdown.css (relative to executable)
    const char* css_filename = "lambda/input/markdown.css";
    log_debug("[Lambda Markdown] Loading default markdown stylesheet: %s", css_filename);

    CssStylesheet* markdown_stylesheet = nullptr;
    char* css_content = read_text_file(css_filename);
    if (css_content) {
        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (css_pool_copy) {
            strcpy(css_pool_copy, css_content);
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

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - CSS parse: %.1fms",
        std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 5: Apply CSS cascade to DOM tree
    auto step4_start = std::chrono::high_resolution_clock::now();
    if (markdown_stylesheet && markdown_stylesheet->rule_count > 0) {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        apply_stylesheet_to_dom_tree_fast(dom_root, markdown_stylesheet, matcher, pool, css_engine);
    }
    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - CSS cascade: %.1fms",
        std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 6: Populate DomDocument structure
    dom_doc->root = dom_root;
    dom_doc->html_root = markdown_root;
    dom_doc->html_version = HTML5;  // Treat markdown as HTML5
    dom_doc->url = markdown_url;
    dom_doc->view_tree = nullptr;  // Will be created during layout
    dom_doc->state = nullptr;

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] load_markdown_doc total: %.1fms",
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
    log_debug("[Lambda LaTeX] Loading LaTeX document: %s", latex_filepath);

    // Step 1: Parse LaTeX with Lambda parser (Tree-sitter based)
    char* latex_content = read_text_file(latex_filepath);
    if (!latex_content) {
        log_error("Failed to read LaTeX file: %s", latex_filepath);
        return nullptr;
    }

    // Create type string for LaTeX
    String* type_str = (String*)malloc(sizeof(String) + 6);
    type_str->len = 5;
    strcpy(type_str->chars, "latex");

    // Parse LaTeX to Lambda Element tree using input_from_source
    Input* latex_input = input_from_source(latex_content, latex_url, type_str, nullptr);
    free(latex_content);

    if (!latex_input || !latex_input->root.item) {
        log_error("Failed to parse LaTeX file: %s", latex_filepath);
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Parsed LaTeX document to Lambda Element tree");

    // DEBUG: Dump the LaTeX tree
    log_debug("[Lambda LaTeX] Dumping LaTeX input tree:");
    log_root_item(latex_input->root);

    // Step 2: Generate complete HTML document with external CSS links
    log_debug("[Lambda LaTeX] Converting LaTeX to HTML with external CSS...");

    // Generate HTML output path: <input_file>.html
    std::string html_output_path = std::string(latex_filepath) + ".html";

    // Compute relative path from HTML file location to conf/input/latex/
    // We need to find how many directories deep the HTML file is from the CWD
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        log_error("Failed to get current working directory");
        return nullptr;
    }
    std::string cwd_str(cwd);
    std::string html_abs_dir = html_output_path;
    size_t last_slash = html_abs_dir.rfind('/');
    if (last_slash != std::string::npos) {
        html_abs_dir = html_abs_dir.substr(0, last_slash);
    }

    // Get relative path from CWD to HTML directory
    std::string rel_html_dir;
    if (html_abs_dir.find(cwd_str) == 0) {
        // HTML is under CWD
        rel_html_dir = html_abs_dir.substr(cwd_str.length());
        if (!rel_html_dir.empty() && rel_html_dir[0] == '/') {
            rel_html_dir = rel_html_dir.substr(1);
        }
    } else {
        rel_html_dir = html_abs_dir;  // Fallback to absolute
    }

    // Count directory depth
    int depth = 0;
    if (!rel_html_dir.empty()) {
        depth = 1;  // At least one directory
        for (char c : rel_html_dir) {
            if (c == '/') depth++;
        }
    }

    // Build relative path: go up 'depth' levels, then into conf/input/latex/
    std::string asset_base_url_str;
    for (int i = 0; i < depth; i++) {
        asset_base_url_str += "../";
    }
    asset_base_url_str += "conf/input/latex/";

    log_debug("[Lambda LaTeX] CWD: %s, HTML dir: %s, rel: %s, depth: %d, asset URL: %s",
              cwd_str.c_str(), html_abs_dir.c_str(), rel_html_dir.c_str(), depth, asset_base_url_str.c_str());

    const char* doc_class = "article";  // Default document class

    // Generate complete HTML document with linked CSS
    const char* html_doc = format_latex_html_v2_document_c(latex_input, doc_class, asset_base_url_str.c_str(), 0);
    if (!html_doc) {
        log_error("Failed to generate HTML document from LaTeX");
        return nullptr;
    }

    // Save HTML to file
    FILE* html_file = fopen(html_output_path.c_str(), "w");
    if (html_file) {
        fprintf(html_file, "%s", html_doc);
        fclose(html_file);
        log_info("[Lambda LaTeX] Saved HTML to: %s (assets: %s)", html_output_path.c_str(), asset_base_url_str.c_str());
    } else {
        log_warn("[Lambda LaTeX] Could not save HTML to: %s", html_output_path.c_str());
    }

    // Step 3: Parse the generated HTML for DOM construction
    log_debug("[Lambda LaTeX] Parsing generated HTML for layout...");

    // Create new input for HTML parsing
    String* html_type_str = (String*)malloc(sizeof(String) + 5);
    html_type_str->len = 4;
    strcpy(html_type_str->chars, "html");

    Input* html_input = input_from_source(html_doc, latex_url, html_type_str, nullptr);
    if (!html_input) {
        log_error("Failed to parse generated HTML");
        return nullptr;
    }

    Element* html_root = get_html_root_element(html_input);
    // Use html_input for DOM construction
    latex_input = html_input;

    if (!html_root) {
        log_error("Failed to get HTML root element from LaTeX conversion");
        return nullptr;
    }

    log_debug("[Lambda LaTeX] Converted LaTeX to HTML Element tree");

    // Step 3: Create DomDocument and build DomElement tree from HTML Element tree
    log_debug("[Lambda LaTeX] Building DomElement tree from HTML");
    DomDocument* dom_doc = dom_document_create(latex_input);
    if (!dom_doc) {
        log_error("Failed to create DomDocument");
        return nullptr;
    }

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
    const char* css_filename = "lambda/format/latex.css";
    log_debug("[Lambda LaTeX] Loading default LaTeX stylesheet: %s", css_filename);

    CssStylesheet* latex_stylesheet = nullptr;
    char* css_content = read_text_file(css_filename);
    if (css_content) {
        size_t css_len = strlen(css_content);
        char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
        if (css_pool_copy) {
            strcpy(css_pool_copy, css_content);
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
 * Parse command-line arguments
 */
struct LayoutOptions {
    const char* input_file;
    const char* output_file;
    const char* css_file;
    const char* view_output_file;  // Custom output path for view_tree.json
    int viewport_width;
    int viewport_height;
    bool debug;
};

bool parse_layout_args(int argc, char** argv, LayoutOptions* opts) {
    // Initialize defaults
    opts->input_file = nullptr;
    opts->output_file = nullptr;
    opts->css_file = nullptr;
    opts->view_output_file = nullptr;  // Default to /tmp/view_tree.json
    opts->viewport_width = 1200;  // Standard viewport width for layout tests (matches browser reference)
    opts->viewport_height = 800;  // Standard viewport height for layout tests (matches browser reference)
    opts->debug = false;

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
                opts->viewport_width = atoi(argv[++i]);
            } else {
                log_error("Error: -vw/--viewport-width requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "-vh") == 0 || strcmp(argv[i], "--viewport-height") == 0) {
            if (i + 1 < argc) {
                opts->viewport_height = atoi(argv[++i]);
            } else {
                log_error("Error: -vh/--viewport-height requires an argument");
                return false;
            }
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            opts->debug = true;
        }
        else if (argv[i][0] != '-' && !opts->input_file) {
            opts->input_file = argv[i];
        }
    }

    if (!opts->input_file) {
        log_error("Error: input file required");
        log_error("Usage: lambda layout <input.html> [options]");
        return false;
    }

    return true;
}

/**
 * Main layout command implementation using Lambda CSS and Radiant layout
 */
int cmd_layout(int argc, char** argv) {
    // Initialize logging system
    FILE *file = fopen("log.txt", "w");
    if (file) { fclose(file); }
    log_parse_config_file("log.conf");

    // Parse command-line options
    LayoutOptions opts;
    if (!parse_layout_args(argc, argv, &opts)) {
        return 1;
    }

    log_debug("Lambda Layout Command");
    log_debug("  Input: %s", opts.input_file);
    log_debug("  Output: %s", opts.output_file ? opts.output_file : "(stdout)");
    log_debug("  CSS: %s", opts.css_file ? opts.css_file : "(inline only)");
    log_debug("  Viewport: %dx%d", opts.viewport_width, opts.viewport_height);

    // Create memory pool for this operation
    Pool* pool = pool_create();
    if (!pool) {
        log_error("Failed to create memory pool");
        return 1;
    }

    // get cwd
    Url* cwd = get_current_dir();
    Url* input_url = url_parse_with_base(opts.input_file, cwd);

    // Detect file type by extension and load appropriate document
    DomDocument* doc = nullptr;
    const char* ext = strrchr(opts.input_file, '.');

    if (ext && (strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0)) {
        // Load LaTeX document: parse LaTeX → convert to HTML → layout
        log_info("[Layout] Detected LaTeX file, using LaTeX→HTML pipeline");
        doc = load_latex_doc(input_url, opts.viewport_width, opts.viewport_height, pool);
    } else {
        // Load HTML document with Lambda CSS system
        doc = load_lambda_html_doc(
            input_url,
            opts.css_file,
            opts.viewport_width,
            opts.viewport_height,
            pool
        );
    }

    if (!doc) {
        log_error("Failed to load document");
        pool_destroy(pool);
        return 1;
    }

    // Initialize UI context in headless mode for layout computation
    log_debug("[Layout] Initializing UI context (headless mode)...");
    UiContext ui_context;
    memset(&ui_context, 0, sizeof(UiContext));

    if (ui_context_init(&ui_context, true) != 0) {
        log_error("Failed to initialize UI context");
        free_document(doc);
        pool_destroy(pool);
        return 1;
    }

    // Set viewport dimensions
    ui_context.window_width = opts.viewport_width;
    ui_context.window_height = opts.viewport_height;

    // Create surface for layout calculations
    log_debug("[Layout] Creating surface for layout calculations...");
    ui_context_create_surface(&ui_context, opts.viewport_width, opts.viewport_height);
    log_debug("[Layout] Surface created");

    // Process @font-face rules from stored stylesheets
    // This must happen after UiContext is initialized but before layout
    process_document_font_faces(&ui_context, doc);

    // Perform layout computation
    log_debug("[Layout] About to call layout_html_doc...");
    log_debug("[Layout] lambda_html_root=%p, dom_root=%p",
            (void*)doc->html_root, (void*)doc->root);

    ui_context.document = doc;
    layout_html_doc(&ui_context, doc, false);

    log_debug("[Layout] layout_html_doc returned");

    if (!doc->view_tree || !doc->view_tree->root) {
        log_warn("Layout computation did not produce view tree");
    } else {
        log_info("[Layout] Layout computed successfully!");
    }

    // Write output using Radiant's print_view_tree function
    log_debug("[Layout] Preparing output...");

    // Use print_view_tree to generate complete layout tree JSON
    // It writes to /tmp/view_tree.json or custom path if --view-output is specified
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("[Layout] Calling print_view_tree for complete layout output...");
        print_view_tree((ViewElement*)doc->view_tree->root, doc->url, 1.0f, opts.view_output_file);
        log_debug("[Layout] Layout tree written to %s", opts.view_output_file ? opts.view_output_file : "/tmp/view_tree.json");
    } else {
        log_warn("No view tree available to output");
    }

    // Cleanup
    log_debug("[Cleanup] Starting cleanup...");
    log_debug("[Cleanup] Cleaning up UI context...");
    ui_context_cleanup(&ui_context);
    log_debug("[Cleanup] Destroying pool...");
    pool_destroy(pool);
    log_debug("[Cleanup] Complete");
    log_notice("Completed layout command successfully");
    return 0;
}
