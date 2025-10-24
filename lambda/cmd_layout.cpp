/**
 * lambda layout - HTML Layout Command with Lambda CSS
 *
 * Computes layout for HTML documents using Lambda-parsed HTML/CSS and Radiant's layout engine.
 * This is separate from the Lexbor-based layout to use Lambda's CSS system.
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
#include "input/input.h"
#include "input/css/css_integration.h"
#include "input/css/css_style_node.h"
#include "input/css/dom_element.h"
#include "input/css/selector_matcher.h"
}

#include "../radiant/dom.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward declarations
Element* get_html_root_element(Input* input);
const char* extract_inline_css(Element* root);

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
 * Extract inline CSS from <style> tags in the HTML document
 * Returns concatenated CSS text or nullptr if none found
 */
const char* extract_inline_css(Element* root) {
    if (!root) return nullptr;

    // TODO: Implement style tag extraction
    // For now, return nullptr - external CSS via -c flag will work
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

    // Parse CSS if provided
    CssStylesheet* stylesheet = nullptr;
    if (css_filename) {
        char* css_content = read_text_file(css_filename);
        if (css_content) {
            log_debug("Parsing CSS file: %s", opts.css_file);
            log_debug("CSS content length: %zu bytes", strlen(css_content));

            // Copy CSS content to pool so it stays alive with the stylesheet
            size_t css_len = strlen(css_content);
            char* css_pool_copy = (char*)pool_alloc(pool, css_len + 1);
            if (css_pool_copy) {
                strcpy(css_pool_copy, css_content);
                free(css_content);  // Free the original, use pool copy

                // Parse the CSS using pool-allocated copy
                stylesheet = css_parse_stylesheet(css_engine, css_pool_copy, css_filename);

                if (!stylesheet) {
                    log_error("Warning: failed to parse CSS file");
                } else {
                    log_debug("Successfully parsed CSS: %d rules", stylesheet->rule_count);
                }
            } else {
                log_error("Failed to allocate memory for CSS");
                free(css_content);
            }
        } else {
            log_error("Failed to read CSS file: %s", opts.css_file);
        }
    }

    // Create DomElement wrapper for CSS styling
    TypeElmt* root_type = (TypeElmt*)root->type;
    DomElement* css_dom = dom_element_create(pool, root_type->name.str, (void*)root);
    if (!css_dom) {
        log_error("Failed to create DOM element for CSS");
        css_engine_destroy(css_engine);
        pool_destroy(pool);
        return 1;
    }

    log_debug("Created DOM element wrapper for styling");

    // Apply CSS stylesheet if available
    if (stylesheet && css_dom->specified_style) {
        log_debug("Applying CSS stylesheet...");

        // Apply styles - this would normally involve selector matching
        // For now, just report that the stylesheet was parsed
        CssEngineStats stats = css_engine_get_stats(css_engine);
        log_debug("CSS Statistics:");
        log_debug("  Rules processed: %zu", stats.rules_processed);
        log_debug("  Selectors processed: %zu", stats.selectors_processed);
        log_debug("  Properties processed: %zu", stats.properties_processed);
        if (stats.parse_errors > 0) {
            log_debug("  Parse errors: %zu", stats.parse_errors);
        }
    }

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
