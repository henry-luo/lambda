/**
 * lambda layout - HTML Layout Command with Lambda CSS
 *
 * Computes layout for HTML documents using Lambda-parsed HTML/CSS and Radiant's layout engine.
 * This is separate from the Lexbor-based CSS system.
 *
 * Usage:
 *   lambda layout input.html [-o output.json] [-c styles.css] [-w 1200] [-h 800]
 *
 * Options:
 *   -o, --output FILE    Output file for layout results (default: stdout)
 *   -c, --css FILE       External CSS file to apply
 *   -w, --width WIDTH    Viewport width in pixels (default: 1200)
 *   -h, --height HEIGHT  Viewport height in pixels (default: 800)
 *   --format FORMAT      Output format: json, text (default: text)
 *   --debug              Enable debug output
 */

extern "C" {
#include "../lib/mempool.h"
#include "../lib/file.h"
#include "../lib/string.h"
#include "../lib/url.h"
#include "../lib/log.h"
#include "../lambda/input/css/css_engine.h"
#include "../lambda/input/css/css_style_node.h"
#include "../lambda/input/css/dom_element.h"
#include "../lambda/input/css/selector_matcher.h"
#include "../lambda/input/css/document_styler.h"
}

#include "../lambda/input/input.h"
#include "../radiant/dom.hpp"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// External C++ function declarations from Radiant
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void print_view_tree(ViewGroup* view_root, lxb_url_t* url, float pixel_ratio, DocumentType doc_type);

// Forward declarations
Element* get_html_root_element(Input* input);
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent);
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count);
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool);
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count);
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool);
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool);
// DomNode* build_radiant_dom_node(DomElement* css_elem, Element* html_elem, Pool* pool);  // OBSOLETE: DomNode now wraps DomElement directly
ViewGroup* compute_layout_tree(DomNode* root_node, LayoutContext* lycon);
void print_item(StrBuf *strbuf, Item item, int depth=0, char* indent=NULL);
void dom_element_print(DomElement* element, StrBuf* buf, int indent);

// Function to determine HTML version from Lambda CSS document DOCTYPE
// This function examines the original Element tree to find DOCTYPE information
// before it gets filtered out during DomElement tree construction
HtmlVersion detect_html_version_from_lambda_element(Element* lambda_html_root, Input* input) {
    if (!input || !input->root.item) {
        log_debug("No input or root available for DOCTYPE detection");
        return HTML5;
    }

    log_debug("Detecting HTML version from Lambda Element tree");

    // The input->root contains the full parsed tree including DOCTYPE
    // It's typically a List containing multiple items (DOCTYPE, html element, etc.)
    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_LIST) {
        List* root_list = (List*)input->root.pointer;
        log_debug("Examining root list with %lld items", root_list->length);

        // Search through the list for DOCTYPE element
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = (Element*)item.pointer;
                TypeElmt* type = (TypeElmt*)elem->type;

                if (type && (strcmp(type->name.str, "!DOCTYPE") == 0 || strcmp(type->name.str, "!doctype") == 0)) {
                    log_debug("Found DOCTYPE element");

                    // Extract DOCTYPE content from the element's children
                    if (elem->length > 0) {
                        Item first_child = elem->items[0];
                        if (get_type_id(first_child) == LMD_TYPE_STRING) {
                            String* doctype_content = (String*)first_child.pointer;
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

    // No DOCTYPE found - assume HTML5 (modern default)
    log_debug("No DOCTYPE found in Lambda Element tree, defaulting to HTML5");
    return HTML5;
}

/**
 * Extract string attribute from Lambda Element
 * Returns attribute value or nullptr if not found
 */
const char* extract_element_attribute(Element* elem, const char* attr_name, Pool* pool) {
    if (!elem || !attr_name) return nullptr;

    // Create a string key for the attribute
    String* key_str = (String*)pool_alloc(pool, sizeof(String) + strlen(attr_name) + 1);
    if (!key_str) return nullptr;

    key_str->len = strlen(attr_name);
    strcpy(key_str->chars, attr_name);

    Item key;
    key.item = s2it(key_str);

    // Get the attribute value using elmt_get_typed
    TypedItem attr_value = elmt_get_typed(elem, key);

    // Check if it's a string
    if (attr_value.type_id == LMD_TYPE_STRING && attr_value.string) {
        return attr_value.string->chars;
    }

    return nullptr;
}

/**
 * Apply inline style attribute to a single DOM element
 * Inline styles have highest specificity (1,0,0,0)
 */
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool) {
    if (!dom_elem || !html_elem || !pool) return;

    // Extract 'style' attribute from html_elem
    const char* style_text = extract_element_attribute(html_elem, "style", pool);

    if (style_text && strlen(style_text) > 0) {
        fprintf(stderr, "[CSS] Applying inline style to <%s>: %s\n",
                dom_elem->tag_name, style_text);

        // Apply the inline style using the DOM element API
        int decl_count = dom_element_apply_inline_style(dom_elem, style_text);

        if (decl_count > 0) {
            fprintf(stderr, "[CSS] Applied %d inline declarations to <%s>\n",
                    decl_count, dom_elem->tag_name);
        } else {
            fprintf(stderr, "[CSS] Warning: Failed to parse inline style for <%s>\n",
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
    void* dom_child = dom_elem->first_child;

    // Iterate through HTML children to find matching elements
    for (int64_t i = 0; i < html_elem->length && dom_child; i++) {
        Item child_item = html_elem->items[i];

        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* html_child = (Element*)child_item.pointer;
            TypeElmt* child_type = (TypeElmt*)html_child->type;

            // Check if DOM child is an element node
            if (dom_node_is_element(dom_child)) {
                DomElement* dom_child_elem = (DomElement*)dom_child;

                // Recursively apply to this child
                apply_inline_styles_to_tree(dom_child_elem, html_child, pool);
                dom_child = dom_child_elem->next_sibling;
            } else {
                // Skip non-element DOM nodes (text, comments)
                if (dom_node_is_text(dom_child)) {
                    dom_child = ((DomText*)dom_child)->next_sibling;
                } else if (dom_node_is_comment(dom_child)) {
                    dom_child = ((DomComment*)dom_child)->next_sibling;
                } else {
                    dom_child = nullptr;
                }
            }
        }
    }
}

/**
 * Extract the root HTML element from parsed input
 * Skips DOCTYPE, comments, and other non-element nodes
 */
Element* get_html_root_element(Input* input) {
    if (!input) return nullptr;

    void* root_ptr = (void*)input->root.pointer;
    if (!root_ptr) return nullptr;

    List* root_list = (List*)root_ptr;

    // If root is a list, search for HTML element
    if (root_list->type_id == LMD_TYPE_LIST) {
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            TypeId item_type = get_type_id(item);

            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem = (Element*)item.pointer;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments
                if (strcmp(type->name.str, "!DOCTYPE") != 0 &&
                    strcmp(type->name.str, "!--") != 0) {
                    return elem;
                }
            }
        }
    } else if (root_list->type_id == LMD_TYPE_ELEMENT) {
        return (Element*)root_ptr;
    }

    return nullptr;
}

/**
 * Recursively collect <link rel="stylesheet"> references from HTML
 * Loads and parses external CSS files
 */
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool) {
    if (!elem || !engine || !pool) return;

    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return;

    // Check if this is a <link> element
    if (strcasecmp(type->name.str, "link") == 0) {
        // TODO: Extract 'rel' and 'href' attributes
        // For now, we'll rely on external -c flag
        // Full implementation would:
        // 1. Check rel="stylesheet"
        // 2. Extract href attribute
        // 3. Resolve relative path using base_path
        // 4. Load and parse CSS file
        // 5. Add to engine's stylesheet list
        fprintf(stderr, "[CSS] Found <link> element (attribute extraction not yet implemented)\n");
    }

    // Recursively process children
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            collect_linked_stylesheets((Element*)child_item.pointer, engine, base_path, pool);
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
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* css_text = (String*)child_item.pointer;
                if (css_text && css_text->len > 0) {
                    fprintf(stderr, "[CSS] Found <style> element with %d bytes of CSS\n", css_text->len);

                    // Parse the inline CSS
                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_text->chars, "<inline-style>");
                    if (stylesheet && stylesheet->rule_count > 0) {
                        fprintf(stderr, "[CSS] Parsed inline <style>: %zu rules\n", stylesheet->rule_count);

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
            collect_inline_styles_to_list((Element*)child_item.pointer, engine, pool, stylesheets, count);
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
                String* css_text = (String*)child_item.pointer;
                if (css_text && css_text->len > 0) {
                    fprintf(stderr, "[CSS] Found <style> element with %d bytes of CSS\n", css_text->len);

                    // Parse the inline CSS
                    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_text->chars, "<inline-style>");
                    if (stylesheet) {
                        fprintf(stderr, "[CSS] Parsed inline <style>: %zu rules\n", stylesheet->rule_count);
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
            collect_inline_styles((Element*)child_item.pointer, engine, pool);
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

    fprintf(stderr, "[CSS] Extracting CSS from HTML document...\n");

    *stylesheet_count = 0;
    CssStylesheet** stylesheets = nullptr;

    // Step 1: Collect and parse <link rel="stylesheet"> references
    fprintf(stderr, "[CSS] Step 1: Collecting linked stylesheets...\n");
    collect_linked_stylesheets(html_root, engine, base_path, pool);
    // TODO: Add linked stylesheets to array when implemented

    // Step 2: Collect and parse <style> inline CSS
    fprintf(stderr, "[CSS] Step 2: Collecting inline <style> elements...\n");
    collect_inline_styles_to_list(html_root, engine, pool, &stylesheets, stylesheet_count);

    fprintf(stderr, "[CSS] Collected %d stylesheet(s) from HTML\n", *stylesheet_count);
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
 * Recursively build DomElement tree from Lambda Element tree
 * Converts HTML parser output (Element) to CSS system format (DomElement)
 * Now includes text nodes, comments, DOCTYPE, and all other node types
 */
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent) {
    if (!elem || !pool) {
        log_debug("build_dom_tree_from_element: Invalid arguments\n");
        return nullptr;
    }

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;
    log_debug("build element: <%s> (parent: %s)", tag_name,
              parent ? parent->tag_name : "none");

    // skip comments and other non-element nodes - they should not participate in CSS cascade or layout
    if (strcmp(tag_name, "!--") == 0 || strcmp(tag_name, "!DOCTYPE") == 0 || strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip comments, DOCTYPE, and XML declarations
    }

    // skip script elements - they should not participate in layout
    // script elements have display: none by default in browser user-agent stylesheets
    if (strcasecmp(tag_name, "script") == 0) {
        return nullptr;  // Skip script elements during DOM tree building
    }

    // create DomElement
    DomElement* dom_elem = dom_element_create(pool, tag_name, (void*)elem);
    if (!dom_elem) return nullptr;

    // extract id and class attributes from Lambda Element
    const char* id_value = extract_element_attribute(elem, "id", pool);
    if (id_value) {
        dom_element_set_attribute(dom_elem, "id", id_value);
    }

    const char* class_value = extract_element_attribute(elem, "class", pool);
    fprintf(stderr, "[HTML] Element <%s>: class attribute = %s\n",
            tag_name, class_value ? class_value : "(null)");
    if (class_value) {
        // parse multiple classes separated by spaces
        char* class_copy = (char*)pool_alloc(pool, strlen(class_value) + 1);
        if (class_copy) {
            strcpy(class_copy, class_value);

            // split by spaces and add each class
            char* token = strtok(class_copy, " \t\n");
            while (token) {
                if (strlen(token) > 0) {
                    fprintf(stderr, "[HTML] Adding class '%s' to <%s>\n", token, tag_name);
                    dom_element_add_class(dom_elem, token);
                }
                token = strtok(nullptr, " \t\n");
            }
        }
    }

    // set parent relationship if provided
    if (parent) {
        dom_element_append_child(parent, dom_elem);
    }

    // Process all children - including text nodes, comments, and elements
    // Elements are Lists, so iterate through items

    log_debug("Processing %lld children for <%s> (dom_elem=%p)", elem->length, tag_name, (void*)dom_elem);
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        TypeId child_type = get_type_id(child_item);
        log_debug("  Child %lld: type=%d", i, child_type);
        if (child_type == LMD_TYPE_ELEMENT) {
            // element node - recursively build
            Element* child_elem = (Element*)child_item.pointer;
            TypeElmt* child_elem_type = (TypeElmt*)child_elem->type;
            const char* child_tag_name = child_elem_type ? child_elem_type->name.str : "unknown";

            log_debug("  Building child element: <%s> for parent <%s> (parent_dom=%p)", child_tag_name, tag_name, (void*)dom_elem);
            DomElement* child_dom = build_dom_tree_from_element(child_elem, pool, dom_elem);

            // skip if nullptr (e.g., comments, DOCTYPE were filtered out)
            if (!child_dom) {
                log_debug("  Skipped child element: <%s>", child_tag_name);
                continue;
            }

            log_debug("  Successfully built child <%s> with parent <%s>. child_dom=%p, child_dom->parent=%p",
                     child_tag_name, tag_name, (void*)child_dom, (void*)child_dom->parent);

            // The dom_element_append_child was already called in the recursive call,
            // so the parent-child and sibling relationships are already established correctly.
            // No manual linking needed!

        } else if (child_type == LMD_TYPE_STRING) {
            // Text node - create DomText and append manually (no dom_text_append_child function)
            String* text_str = (String*)child_item.pointer;
            if (text_str && text_str->len > 0) {
                DomText* text_node = dom_text_create(pool, text_str->chars);
                if (text_node) {
                    text_node->parent = dom_elem;

                    // Add text node using the same append logic as dom_element_append_child
                    if (!dom_elem->first_child) {
                        // First child
                        dom_elem->first_child = text_node;
                        text_node->prev_sibling = nullptr;
                        text_node->next_sibling = nullptr;
                    } else {
                        // Find last child and append
                        void* last_child_node = dom_elem->first_child;
                        while (last_child_node) {
                            void* next = nullptr;
                            DomNodeType type = dom_node_get_type(last_child_node);
                            if (type == DOM_NODE_ELEMENT) {
                                next = ((DomElement*)last_child_node)->next_sibling;
                            } else if (type == DOM_NODE_TEXT) {
                                next = ((DomText*)last_child_node)->next_sibling;
                            } else if (type == DOM_NODE_COMMENT || type == DOM_NODE_DOCTYPE) {
                                next = ((DomComment*)last_child_node)->next_sibling;
                            }

                            if (!next) {
                                // This is the last child, append text node here
                                if (type == DOM_NODE_ELEMENT) {
                                    ((DomElement*)last_child_node)->next_sibling = text_node;
                                } else if (type == DOM_NODE_TEXT) {
                                    ((DomText*)last_child_node)->next_sibling = text_node;
                                } else if (type == DOM_NODE_COMMENT || type == DOM_NODE_DOCTYPE) {
                                    ((DomComment*)last_child_node)->next_sibling = text_node;
                                }
                                text_node->prev_sibling = last_child_node;
                                text_node->next_sibling = nullptr;
                                break;
                            }
                            last_child_node = next;
                        }
                    }
                }
            }
        }
    }

    return dom_elem;
}

/**
 * Apply CSS stylesheet rules to DOM tree
 * Walks the tree recursively and matches selectors to elements
 */
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool) {
    if (!root || !stylesheet || !matcher || !pool) return;

    fprintf(stderr, "[CSS] Applying stylesheet with %d rules to element <%s>\n",
              stylesheet->rule_count, root->tag_name);

    // Iterate through all rules in the stylesheet
    for (int rule_idx = 0; rule_idx < stylesheet->rule_count; rule_idx++) {
        CssRule* rule = stylesheet->rules[rule_idx];
        if (!rule) {
            fprintf(stderr, "[CSS] Rule %d is NULL\n", rule_idx);
            continue;
        }

        fprintf(stderr, "[CSS] Processing rule %d, type=%d\n", rule_idx, rule->type);

        // Only process style rules (skip @media, @import, etc.)
        if (rule->type != CSS_RULE_STYLE) {
            fprintf(stderr, "[CSS] Rule %d is not a style rule, skipping\n", rule_idx);
            continue;
        }

        // Get selector from style_rule union
        CssSelector* selector = rule->data.style_rule.selector;
        if (!selector) {
            fprintf(stderr, "[CSS] Rule %d has no selector\n", rule_idx);
            continue;
        }

        // Calculate and cache specificity if not already done
        if (selector->specificity.inline_style == 0 &&
            selector->specificity.ids == 0 &&
            selector->specificity.classes == 0 &&
            selector->specificity.elements == 0) {
            selector->specificity = selector_matcher_calculate_specificity(matcher, selector);
            fprintf(stderr, "[CSS] Calculated specificity for rule %d: (%d,%d,%d,%d)\n",
                      rule_idx,
                      selector->specificity.inline_style,
                      selector->specificity.ids,
                      selector->specificity.classes,
                      selector->specificity.elements);
        }

        fprintf(stderr, "[CSS] Rule %d has selector, testing match against <%s>\n", rule_idx, root->tag_name);

        // Check if the selector matches this element
        MatchResult match_result;
        if (selector_matcher_matches(matcher, selector, root, &match_result)) {
            fprintf(stderr, "[CSS] Rule %d MATCHES <%s>: specificity (%d,%d,%d,%d)\n",
                      rule_idx, root->tag_name,
                      match_result.specificity.inline_style,
                      match_result.specificity.ids,
                      match_result.specificity.classes,
                      match_result.specificity.elements);

            // Apply all declarations from this rule
            size_t decl_count = rule->data.style_rule.declaration_count;
            fprintf(stderr, "[CSS] Applying %zu declarations from rule %d\n", decl_count, rule_idx);

            if (decl_count > 0) {
                dom_element_apply_rule(root, rule, match_result.specificity);
                fprintf(stderr, "[CSS] Applied %zu declarations to <%s>\n",
                          decl_count, root->tag_name);
            }
        } else {
            fprintf(stderr, "[CSS] Rule %d does NOT match <%s>\n", rule_idx, root->tag_name);
        }
    }

    // Recursively apply to children (only element children)
    void* child = root->first_child;
    while (child) {
        if (dom_node_is_element(child)) {
            DomElement* child_elem = (DomElement*)child;
            apply_stylesheet_to_dom_tree(child_elem, stylesheet, matcher, pool);
            child = child_elem->next_sibling;
        } else if (dom_node_is_text(child)) {
            DomText* text_node = (DomText*)child;
            child = text_node->next_sibling;
        } else if (dom_node_is_comment(child)) {
            DomComment* comment_node = (DomComment*)child;
            child = comment_node->next_sibling;
        } else {
            break; // Unknown node type
        }
    }
}



/**
 * Load HTML document with Lambda CSS system
 * Parses HTML, applies CSS cascade, builds DOM tree, returns Document for layout
 *
 * @param html_filename Path to HTML file
 * @param css_filename Optional external CSS file (can be NULL)
 * @param viewport_width Viewport width for layout
 * @param viewport_height Viewport height for layout
 * @param pool Memory pool for allocations
 * @return Document structure with Lambda CSS DOM, ready for layout
 */
Document* load_lambda_html_doc(const char* html_filename, const char* css_filename,
                                int viewport_width, int viewport_height, Pool* pool) {
    if (!html_filename || !pool) {
        log_error("load_lambda_html_doc: invalid parameters");
        return nullptr;
    }

    log_debug("[Lambda CSS] Loading HTML document: %s", html_filename);

    // Step 1: Parse HTML with Lambda parser
    char* html_content = read_text_file(html_filename);
    if (!html_content) {
        log_error("Failed to read HTML file: %s", html_filename);
        return nullptr;
    }

    // Create type string for HTML
    String* type_str = (String*)malloc(sizeof(String) + 5);
    type_str->len = 4;
    strcpy(type_str->chars, "html");

    Url* url = url_parse(html_filename);
    Input* input = input_from_source(html_content, url, type_str, nullptr);
    free(html_content);

    if (!input) {
        log_error("Failed to create input for file: %s", html_filename);
        return nullptr;
    }

    Element* html_root = get_html_root_element(input);
    if (!html_root) {
        log_error("Failed to get HTML root element");
        return nullptr;
    }

    // Detect HTML version from the original input tree (contains DOCTYPE)
    int detected_version = HTML5;  // Default fallback
    if (input && input->root.pointer) {
        detected_version = detect_html_version_from_lambda_element(nullptr, input);
        log_debug("[Lambda CSS] Detected HTML version: %d", detected_version);
    }
    StrBuf* str_buf = strbuf_new();
    print_item(str_buf, (Item){.element = html_root});
    log_debug("Parsed HTML root element:\n%s", str_buf->str);

    // Step 2: Build DomElement tree from Lambda Element tree
    log_debug("Building DomElement tree!!!");
    DomElement* dom_root = build_dom_tree_from_element(html_root, pool, nullptr);
    if (!dom_root) {
        log_error("Failed to build DomElement tree");
        return nullptr;
    }


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
        html_root, css_engine, html_filename, pool, &inline_stylesheet_count);

    // Step 6: Apply CSS cascade (external + <style> elements)
    log_debug("[Lambda CSS] Applying CSS cascade...");
    SelectorMatcher* matcher = selector_matcher_create(pool);

    // Apply external stylesheet first (lower priority)
    if (external_stylesheet && external_stylesheet->rule_count > 0) {
        apply_stylesheet_to_dom_tree(dom_root, external_stylesheet, matcher, pool);
    }

    // Apply inline stylesheets (higher priority)
    for (int i = 0; i < inline_stylesheet_count; i++) {
        if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
            apply_stylesheet_to_dom_tree(dom_root, inline_stylesheets[i], matcher, pool);
        }
    }

    // Step 7: Apply inline style="" attributes (highest priority)
    log_debug("[Lambda CSS] Applying inline style attributes...");
    apply_inline_styles_to_tree(dom_root, html_root, pool);
    log_debug("[Lambda CSS] CSS cascade complete");

    // Dump CSS computed values for testing/comparison (includes inheritance, before layout)
    strbuf_reset(str_buf);
    dom_element_print(dom_root, str_buf, 0);
    log_debug("Built DomElement tree with styles: %s", str_buf->str);

    // Step 8: Create Document structure
    Document* doc = (Document*)calloc(1, sizeof(Document));
    if (!doc) {
        log_error("Failed to allocate Document");
        return nullptr;
    }

    doc->doc_type = DOC_TYPE_LAMBDA_CSS;
    doc->lambda_dom_root = dom_root;
    doc->lambda_html_root = html_root;
    doc->html_version = detected_version;

    // Create a minimal URL structure for print_view_tree
    doc->url = (lxb_url_t*)calloc(1, sizeof(lxb_url_t));
    if (doc->url) {
        // Just set the path field which is what print_view_tree uses
        const char* path = html_filename;
        size_t len = strlen(path);
        doc->url->path.str.data = (lxb_char_t*)malloc(len + 1);
        if (doc->url->path.str.data) {
            memcpy(doc->url->path.str.data, path, len + 1);
            doc->url->path.str.length = len;
        }
    }

    doc->view_tree = nullptr;  // Will be created during layout
    doc->state = nullptr;

    log_debug("[Lambda CSS] Document loaded and styled");
    return doc;
}

/**
 * Compute layout using Radiant layout engine (simplified for Lambda)
 * Returns the root ViewGroup with computed layout
 *
 * Note: This is a simplified version that doesn't initialize the full Radiant
 * subsystems (fonts, images, etc.). For full layout with text rendering,
 * use the Radiant window system.
 */
ViewGroup* compute_layout_tree(DomNode* root_node, int viewport_width, int viewport_height, Pool* pool) {
    if (!root_node) return nullptr;

    fprintf(stderr, "[Layout] Radiant layout computation currently requires full UI context\n");
    fprintf(stderr, "[Layout] CSS styling complete - layout computation pending\n");
    fprintf(stderr, "[Layout] For full layout, run: ./radiant.exe %s\n", "(html_file)");

    // TODO: Implement minimal layout computation without full UiContext
    // This requires:
    // 1. Implementing view_pool_init(), layout_init(), layout_cleanup() as standalone
    // 2. Creating minimal font subsystem for text measurement
    // 3. Implementing basic box model computation
    //
    // For now, return nullptr to indicate layout not computed
    // The CSS styling is still fully functional and applied

    return nullptr;
}

/**
 * Parse command-line arguments
 */
struct LayoutOptions {
    const char* input_file;
    const char* output_file;
    const char* css_file;
    int viewport_width;
    int viewport_height;
    bool debug;
};

bool parse_layout_args(int argc, char** argv, LayoutOptions* opts) {
    // Initialize defaults
    opts->input_file = nullptr;
    opts->output_file = nullptr;
    opts->css_file = nullptr;
    opts->viewport_width = 1200;  // Standard viewport width for layout tests (matches browser reference)
    opts->viewport_height = 800;  // Standard viewport height for layout tests (matches browser reference)
    opts->debug = false;

    // Parse arguments
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                opts->output_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--css") == 0) {
            if (i + 1 < argc) {
                opts->css_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -c requires an argument\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
            if (i + 1 < argc) {
                opts->viewport_width = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: -w requires an argument\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) {
            if (i + 1 < argc) {
                opts->viewport_height = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: -h requires an argument\n");
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
        fprintf(stderr, "Error: input file required\n");
        fprintf(stderr, "Usage: lambda layout <input.html> [options]\n");
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

    // Load HTML document with Lambda CSS system
    Document* doc = load_lambda_html_doc(
        opts.input_file,
        opts.css_file,
        opts.viewport_width,
        opts.viewport_height,
        pool
    );

    if (!doc) {
        log_error("Failed to load HTML document");
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

    // Perform layout computation
    log_debug("[Layout] About to call layout_html_doc...");
    log_debug("[Layout] doc=%p, doc_type=%d", (void*)doc, doc->doc_type);
    log_debug("[Layout] lambda_html_root=%p, lambda_dom_root=%p",
            (void*)doc->lambda_html_root, (void*)doc->lambda_dom_root);

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
    // It writes to /tmp/view_tree.json which is what the test framework expects
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("[Layout] Calling print_view_tree for complete layout output...");
        print_view_tree((ViewGroup*)doc->view_tree->root, doc->url, 1.0f, doc->doc_type);
        log_debug("[Layout] Layout tree written to /tmp/view_tree.json");
    } else {
        log_warn("No view tree available to output");
    }

    // Cleanup
    log_debug("[Cleanup] Starting cleanup...");
    log_debug("[Cleanup] Cleaning up UI context...");
    ui_context_cleanup(&ui_context);
    log_debug("[Cleanup] Freeing document...");
    free_document(doc);
    log_debug("[Cleanup] Destroying pool...");
    pool_destroy(pool);
    log_debug("[Cleanup] Complete");

    return 0;
}
