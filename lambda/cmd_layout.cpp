/**
 * lambda layout - HTML Layout Command with Lambda CSS
 *
 * Computes layout for HTML documents using Lambda-parsed HTML/CSS and Radiant's layout engine.
 * This is separate from the Lexbor-based CSS system.
 *
 * Usage:
 *   lambda layout input.html [-o output.json] [-c styles.css] [-w 800] [-h 600]
 *
 * Options:
 *   -o, --output FILE    Output file for layout results (default: stdout)
 *   -c, --css FILE       External CSS file to apply
 *   -w, --width WIDTH    Viewport width in pixels (default: 800)
 *   -h, --height HEIGHT  Viewport height in pixels (default: 600)
 *   --format FORMAT      Output format: json, text (default: text)
 *   --debug              Enable debug output
 */

extern "C" {
#include "../lib/mempool.h"
#include "../lib/file.h"
#include "../lib/string.h"
#include "../lib/url.h"
#include "../lib/log.h"
#include "input/css/css_integration.h"
#include "input/css/css_style_node.h"
#include "input/css/dom_element.h"
#include "input/css/selector_matcher.h"
#include "input/css/document_styler.h"
}

#include "input/input.h"
#include "../radiant/dom.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward declarations
Element* get_html_root_element(Input* input);
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent);
void apply_stylesheet_to_dom_tree(DomElement* root, CssStylesheet* stylesheet, SelectorMatcher* matcher, Pool* pool);
CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count);
void collect_linked_stylesheets(Element* elem, CssEngine* engine, const char* base_path, Pool* pool);
void collect_inline_styles_to_list(Element* elem, CssEngine* engine, Pool* pool, CssStylesheet*** stylesheets, int* count);
void apply_inline_style_attributes(DomElement* dom_elem, Element* html_elem, Pool* pool);
void apply_inline_styles_to_tree(DomElement* dom_elem, Element* html_elem, Pool* pool);

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
    DomElement* dom_child = dom_elem->first_child;

    // Iterate through HTML children to find matching elements
    for (int64_t i = 0; i < html_elem->length && dom_child; i++) {
        Item child_item = html_elem->items[i];

        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* html_child = (Element*)child_item.pointer;
            TypeElmt* child_type = (TypeElmt*)html_child->type;

            // Skip non-element nodes (DOCTYPE, comments)
            if (child_type &&
                strcmp(child_type->name.str, "!DOCTYPE") != 0 &&
                strcmp(child_type->name.str, "!--") != 0) {

                // Recursively apply to this child
                apply_inline_styles_to_tree(dom_child, html_child, pool);
                dom_child = dom_child->next_sibling;
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
 */
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent) {
    if (!elem || !pool) return nullptr;

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;

    // Skip DOCTYPE, comments, and text nodes
    if (strcmp(tag_name, "!DOCTYPE") == 0 || strcmp(tag_name, "!--") == 0) {
        return nullptr;
    }

    // Create DomElement
    DomElement* dom_elem = dom_element_create(pool, tag_name, (void*)elem);
    if (!dom_elem) return nullptr;

    // TODO: Extract id and class attributes from Lambda Element
    // For now, create elements without attributes - this can be enhanced
    // by properly parsing Element attribute data structure

    // Set parent relationship if provided
    if (parent) {
        dom_element_append_child(parent, dom_elem);
    }

    // Process child elements
    // Elements are Lists, so iterate through items
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        TypeId child_type = get_type_id(child_item);

        // Only process element nodes (skip text nodes for CSS purposes)
        if (child_type == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child_item.pointer;
            build_dom_tree_from_element(child_elem, pool, dom_elem);
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

    // Recursively apply to children
    DomElement* child = root->first_child;
    while (child) {
        apply_stylesheet_to_dom_tree(child, stylesheet, matcher, pool);
        child = child->next_sibling;
    }
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
    opts->viewport_width = 800;
    opts->viewport_height = 600;
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
 * Main layout command implementation using Lambda CSS
 */
int cmd_layout(int argc, char** argv) {
    // Parse command-line options
    LayoutOptions opts;
    if (!parse_layout_args(argc, argv, &opts)) {
        return 1;
    }

    log_debug("  Input: %s", opts.input_file);
    log_debug("  Output: %s", opts.output_file ? opts.output_file : "(stdout)");
    log_debug("  CSS: %s", opts.css_file ? opts.css_file : "(inline only)");
    log_debug("  Viewport: %dx%d", opts.viewport_width, opts.viewport_height);

    // Debug: Check pointers
    log_debug("DEBUG: opts structure:");
    log_debug("  input_file ptr: %p = %s", (void*)opts.input_file, opts.input_file);
    log_debug("  css_file ptr: %p = %s", (void*)opts.css_file, opts.css_file ? opts.css_file : "NULL");
    log_debug("  output_file ptr: %p = %s", (void*)opts.output_file, opts.output_file ? opts.output_file : "NULL");


    // Save CSS filename before creating pool (avoid any potential issues)
    const char* css_filename = opts.css_file;

    // Debug: Check opts structure integrity
    log_debug("  opts.viewport_width = %d", opts.viewport_width);
    log_debug("  opts.viewport_height = %d", opts.viewport_height);
    log_debug("  css_filename = %p -> %s", (void*)css_filename, css_filename ? css_filename : "NULL");

    // Create memory pool for this operation
    Pool* pool = pool_create();

    log_debug("DEBUG after pool_create:");
    log_debug("  &opts = %p", (void*)&opts);
    log_debug("  opts.input_file = %p -> %s", (void*)opts.input_file, opts.input_file ? opts.input_file : "NULL");
    log_debug("  opts.css_file = %p -> %s", (void*)opts.css_file, opts.css_file ? opts.css_file : "NULL");
    log_debug("  css_filename = %p -> %s", (void*)css_filename, css_filename ? css_filename : "NULL");

    log_debug("Created memory pool");

    // Read input HTML file
    char* html_content = read_text_file(opts.input_file);
    if (!html_content) {
        log_error("Failed to read input file: %s", opts.input_file);
        pool_destroy(pool);
        return 1;
    }

    log_debug("Read HTML file: %zu bytes", strlen(html_content));

    // Parse HTML using Lambda parser
    String* type_str = (String*)malloc(sizeof(String) + 5);
    type_str->len = 4;
    strcpy(type_str->chars, "html");

    log_debug("Created type string");

    Url* url = url_parse(opts.input_file);

    log_debug("Parsed URL");
    log_debug("Parsed URL, about to call input_from_source");

    Input* input = input_from_source(html_content, url, type_str, nullptr);

    free(html_content);
    if (!input) {
        log_error("Failed to parse HTML");
        pool_destroy(pool);
        return 1;
    }
    else {
        log_debug("Parsed HTML successfully");
    }

    // Get root HTML element
    Element* root = get_html_root_element(input);
    if (!root) {
        log_error("No HTML root element found");
        pool_destroy(pool);
        return 1;
    }
    log_debug("Parsed HTML root: <%s>", ((TypeElmt*)root->type)->name.str);

    // Create CSS Engine
    CssEngine* css_engine = css_engine_create(pool);
    if (!css_engine) {
        log_error("Failed to create CSS engine");
        pool_destroy(pool);
        return 1;
    }

    // Configure CSS engine
    css_engine_set_viewport(css_engine, opts.viewport_width, opts.viewport_height);

    // Build complete DomElement tree from Lambda Element tree
    fprintf(stderr, "[DEBUG] Building DOM tree from Element tree...\n");
    DomElement* dom_root = build_dom_tree_from_element(root, pool, nullptr);
    if (!dom_root) {
        log_error("Failed to build DOM tree");
        css_engine_destroy(css_engine);
        pool_destroy(pool);
        return 1;
    }

    fprintf(stderr, "[DEBUG] Built DOM tree: root=<%s>\n", dom_root->tag_name);

    // === NEW APPROACH: Comprehensive CSS extraction and application ===
    // This mimics browser behavior:
    // 1. Parse external CSS from -c flag (if provided)
    // 2. Extract and parse <link> stylesheets
    // 3. Extract and parse <style> elements
    // 4. Apply all stylesheets to DOM
    // 5. Apply inline style attributes

    // Step 1: External CSS from command line (highest priority for development)
    CssStylesheet* external_stylesheet = nullptr;
    if (css_filename) {
        char* css_content = read_text_file(css_filename);
        if (css_content) {
            fprintf(stderr, "[CSS] Parsing external CSS file: %s\n", css_filename);

            size_t css_len = strlen(css_content);
            char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
            if (css_pool_copy) {
                strcpy(css_pool_copy, css_content);
                free(css_content);

                external_stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);
                if (external_stylesheet) {
                    fprintf(stderr, "[CSS] Parsed external CSS: %zu rules\n", external_stylesheet->rule_count);
                }
            } else {
                free(css_content);
            }
        }
    }

    // Step 2 & 3: Extract CSS from HTML (linked stylesheets and <style> elements)
    int inline_stylesheet_count = 0;
    CssStylesheet** inline_stylesheets = extract_and_collect_css(root, css_engine, opts.input_file, pool, &inline_stylesheet_count);

    // Step 4: Apply all stylesheets to DOM tree
    // Apply in cascade order: external -> inline <style> -> inline attributes
    if ((external_stylesheet && external_stylesheet->rule_count > 0) || inline_stylesheet_count > 0) {
        fprintf(stderr, "[CSS] Applying stylesheets to DOM tree...\n");

        // Create selector matcher
        SelectorMatcher* matcher = selector_matcher_create(pool);
        if (!matcher) {
            log_error("Failed to create selector matcher");
            css_engine_destroy(css_engine);
            pool_destroy(pool);
            return 1;
        }

        // Apply external stylesheet first (lower priority)
        if (external_stylesheet && external_stylesheet->rule_count > 0) {
            fprintf(stderr, "[CSS] Applying external stylesheet (%zu rules)...\n", external_stylesheet->rule_count);
            apply_stylesheet_to_dom_tree(dom_root, external_stylesheet, matcher, pool);
        }

        // Apply inline stylesheets (higher priority)
        for (int i = 0; i < inline_stylesheet_count; i++) {
            if (inline_stylesheets[i] && inline_stylesheets[i]->rule_count > 0) {
                fprintf(stderr, "[CSS] Applying inline stylesheet %d (%zu rules)...\n",
                        i + 1, inline_stylesheets[i]->rule_count);
                apply_stylesheet_to_dom_tree(dom_root, inline_stylesheets[i], matcher, pool);
            }
        }

        fprintf(stderr, "[CSS] All CSS stylesheets applied to DOM tree\n");

        CssEngineStats stats = css_engine_get_stats(css_engine);
        log_debug("CSS Statistics:");
        log_debug("  Rules processed: %zu", stats.rules_processed);
        log_debug("  Selectors processed: %zu", stats.selectors_processed);
        log_debug("  Properties processed: %zu", stats.properties_processed);
        if (stats.parse_errors > 0) {
            log_debug("  Parse errors: %zu", stats.parse_errors);
        }
    } else {
        fprintf(stderr, "[CSS] No external stylesheet to apply\n");
    }

    // Step 5: Apply inline style attributes (highest specificity)
    fprintf(stderr, "[CSS] Applying inline style attributes to DOM tree...\n");
    apply_inline_styles_to_tree(dom_root, root, pool);
    fprintf(stderr, "[CSS] Inline style attributes applied\n");

    // Create DomNode wrapper for layout computation
    // Note: Full layout engine integration would go here
    // For now, output the structure with computed styles

    log_debug("Layout computation complete");
    log_debug("Note: Full layout engine integration pending");

    // Write output
    FILE* out = stdout;
    if (opts.output_file) {
        out = fopen(opts.output_file, "w");
        if (!out) {
            log_error("Error: failed to open output file: %s", opts.output_file);
            css_engine_destroy(css_engine);
            pool_destroy(pool);
            return 1;
        }
    }

    // Output basic structure
    TypeElmt* root_type = (TypeElmt*)root->type;
    fprintf(out, "{\n");
    fprintf(out, "  \"engine\": \"lambda-css\",\n");
    fprintf(out, "  \"viewport\": {\"width\": %d, \"height\": %d},\n",
            opts.viewport_width, opts.viewport_height);
    fprintf(out, "  \"root\": {\n");

    fprintf(out, "    \"tag\": \"%s\",\n", root_type->name.str);
    fprintf(out, "    \"x\": 0,\n");
    fprintf(out, "    \"y\": 0,\n");
    fprintf(out, "    \"width\": %d,\n", opts.viewport_width);
    fprintf(out, "    \"height\": %d\n", opts.viewport_height);
    fprintf(out, "  }\n");
    fprintf(out, "}\n");

    if (opts.output_file) {
        fclose(out);
        if (opts.debug) {
            printf("Layout written to: %s\n", opts.output_file);
        }
    }

    // Cleanup
    css_engine_destroy(css_engine);
    pool_destroy(pool);

    return 0;
}
