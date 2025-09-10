#include "math_typeset.h"
#include "typeset.h"
#include "../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Forward declaration of math parser function
extern Item input_math_from_string(const char* math_string, const char* flavor);

// Main math typesetting pipeline implementation

ViewTree* typeset_math_from_lambda_element(Element* lambda_element, const MathTypesetOptions* options) {
    if (!lambda_element) return NULL;
    
    // Step 1: Convert Lambda element tree to ViewTree using the bridge
    ViewTree* view_tree = convert_lambda_math_to_viewtree(lambda_element);
    if (!view_tree) return NULL;
    
    // Step 2: Apply typesetting options
    if (options) {
        apply_math_typeset_options(view_tree, options);
    }
    
    // Step 3: Perform layout calculations
    if (view_tree->root) {
        MathLayoutContext context = {
            .display_style = true,
            .font_size = options ? options->font_size : 12.0,
            .cramped = false
        };
        
        layout_math_expression(view_tree->root, &context);
    }
    
    return view_tree;
}

ViewTree* typeset_math_from_latex(const char* latex_expr, const MathTypesetOptions* options) {
    if (!latex_expr) return NULL;
    
    // Step 1: Create input structure for Lambda math parser
    Input* input = create_lambda_input(latex_expr);
    if (!input) return NULL;
    
    // Step 2: Parse LaTeX using Lambda math parser
    Item parsed_result = parse_lambda_math(input, MATH_FLAVOR_LATEX);
    
    // Cleanup input
    destroy_lambda_input(input);
    
    // Check if parsing succeeded
    if (parsed_result.item == ITEM_ERROR || parsed_result.item == ITEM_NULL) {
        return NULL;
    }
    
    // Step 3: Convert to ViewTree and typeset
    Element* lambda_element = (Element*)parsed_result.item;
    return typeset_math_from_lambda_element(lambda_element, options);
}

ViewTree* typeset_math_from_lambda_tree(Item math_tree, TypesetOptions* options) {
    if (math_tree.item == ITEM_ERROR || math_tree.item == ITEM_NULL) {
        log_error("typeset_math_from_lambda_tree: Invalid Lambda tree");
        return NULL;
    }
    
    // Create typeset engine
    TypesetEngine* engine = typeset_engine_create();
    if (!engine) {
        log_error("typeset_math_from_lambda_tree: Failed to create typeset engine");
        return NULL;
    }
    
    // Set up math-specific options
    MathTypesetOptions* math_options = math_typeset_options_create_default();
    if (options && options->math_options) {
        // Copy math-specific options from main options
        // This would need to be implemented based on the actual TypesetOptions structure
    }
    
    // Convert Lambda tree to view tree
    ViewNode* math_view_node = convert_lambda_math_to_viewnode(engine, math_tree);
    if (!math_view_node) {
        log_error("typeset_math_from_lambda_tree: Failed to convert to view tree");
        math_typeset_options_destroy(math_options);
        return NULL;
    }
    
    // Create math layout context
    ViewFont* math_font = get_math_font(math_options->math_font_family, 12.0);
    ViewFont* text_font = get_text_font("Times New Roman", 12.0);
    MathLayoutContext* layout_ctx = math_layout_context_create(math_font, text_font, 
                                                              math_options->default_style);
    
    // Layout the math expression
    ViewNode* laid_out_math = layout_math_expression(math_view_node, layout_ctx);
    if (!laid_out_math) {
        log_error("typeset_math_from_lambda_tree: Failed to layout math expression");
        view_node_release(math_view_node);
        math_layout_context_destroy(layout_ctx);
        math_typeset_options_destroy(math_options);
        return NULL;
    }
    
    // Create view tree to contain the math
    ViewTree* view_tree = view_tree_create();
    if (!view_tree) {
        log_error("typeset_math_from_lambda_tree: Failed to create view tree");
        view_node_release(laid_out_math);
        math_layout_context_destroy(layout_ctx);
        math_typeset_options_destroy(math_options);
        return NULL;
    }
    
    // Set up document properties
    view_tree->title = strdup("Mathematical Expression");
    view_tree->creator = strdup("Lambda Math Typesetting System");
    
    // Create a page for the math
    ViewPage* page = calloc(1, sizeof(ViewPage));
    if (page) {
        page->page_number = 1;
        page->page_size.width = 612.0;   // Letter width
        page->page_size.height = 792.0;  // Letter height
        page->content_area.origin.x = 72.0;
        page->content_area.origin.y = 72.0;
        page->content_area.size.width = 468.0;  // Letter width - 2 inch margins
        page->content_area.size.height = 648.0; // Letter height - 2 inch margins
        page->page_node = laid_out_math;
        
        view_tree->pages = calloc(1, sizeof(ViewPage*));
        view_tree->pages[0] = page;
        view_tree->page_count = 1;
    }
    
    // Set root node
    view_tree->root = laid_out_math;
    
    // Calculate final dimensions
    ViewRect bounding_box = calculate_math_bounding_box(laid_out_math);
    view_tree->document_size.width = bounding_box.size.width;
    view_tree->document_size.height = bounding_box.size.height;
    
    // Cleanup
    math_layout_context_destroy(layout_ctx);
    math_typeset_options_destroy(math_options);
    
    return view_tree;
}

// Integration with existing typeset system
ViewNode* process_math_element_in_document(TypesetEngine* engine, Item math_element) {
    if (!engine || math_element.item == ITEM_ERROR) {
        return NULL;
    }
    
    // Create default math options
    MathTypesetOptions* options = math_typeset_options_create_default();
    options->default_style = VIEW_MATH_TEXT; // Inline style for document context
    
    // Convert and layout
    ViewNode* math_view = convert_lambda_math_to_viewnode(engine, math_element);
    if (!math_view) {
        math_typeset_options_destroy(options);
        return NULL;
    }
    
    // Create layout context for inline math
    ViewFont* math_font = get_math_font(options->math_font_family, 12.0);
    ViewFont* text_font = get_text_font("Times New Roman", 12.0);
    MathLayoutContext* layout_ctx = math_layout_context_create(math_font, text_font, VIEW_MATH_TEXT);
    
    ViewNode* laid_out = layout_inline_math(math_view, layout_ctx);
    
    math_layout_context_destroy(layout_ctx);
    math_typeset_options_destroy(options);
    
    return laid_out ? laid_out : math_view;
}

// Layout inline math (for document integration)
ViewNode* layout_inline_math(ViewNode* math_node, MathLayoutContext* ctx) {
    if (!math_node || !ctx) return NULL;
    
    // Set context to text style (inline)
    ctx->style = VIEW_MATH_TEXT;
    ctx->cramped = false;
    
    return layout_math_expression(math_node, ctx);
}

// Layout display math (standalone)
ViewNode* layout_display_math(ViewNode* math_node, MathLayoutContext* ctx) {
    if (!math_node || !ctx) return NULL;
    
    // Set context to display style
    ctx->style = VIEW_MATH_DISPLAY;
    ctx->cramped = false;
    
    return layout_math_expression(math_node, ctx);
}

// Math options management
MathTypesetOptions* math_typeset_options_create_default(void) {
    MathTypesetOptions* options = calloc(1, sizeof(MathTypesetOptions));
    if (!options) return NULL;
    
    options->default_style = VIEW_MATH_TEXT;
    options->math_scale = 1.0;
    options->use_display_mode = false;
    options->math_font_family = strdup("Latin Modern Math");
    options->render_equation_numbers = false;
    options->baseline_skip = 14.0;
    options->math_surround = 3.0;
    
    return options;
}

void math_typeset_options_destroy(MathTypesetOptions* options) {
    if (!options) return;
    
    free((char*)options->math_font_family);
    free(options);
}

// Math element processing workflow
MathTypesetResult* process_math_expression(TypesetEngine* engine, Item math_expr, 
                                         MathTypesetOptions* options) {
    if (!engine || math_expr.item == ITEM_ERROR) {
        return NULL;
    }
    
    MathTypesetResult* result = calloc(1, sizeof(MathTypesetResult));
    if (!result) return NULL;
    
    result->source_lambda_tree = math_expr;
    
    // Convert to view tree
    result->view_tree_root = convert_lambda_math_to_viewnode(engine, math_expr);
    if (!result->view_tree_root) {
        free(result);
        return NULL;
    }
    
    // Create layout context
    if (!options) {
        options = math_typeset_options_create_default();
    }
    
    ViewFont* math_font = get_math_font(options->math_font_family, 12.0);
    ViewFont* text_font = get_text_font("Times New Roman", 12.0);
    result->layout_context = math_layout_context_create(math_font, text_font, 
                                                       options->default_style);
    
    // Layout the expression
    ViewNode* laid_out = layout_math_expression(result->view_tree_root, result->layout_context);
    if (laid_out && laid_out != result->view_tree_root) {
        view_node_release(result->view_tree_root);
        result->view_tree_root = laid_out;
    }
    
    // Calculate metrics
    result->bounding_box = calculate_math_bounding_box(result->view_tree_root);
    result->baseline_offset = calculate_math_baseline_offset(result->view_tree_root);
    
    return result;
}

void math_typeset_result_destroy(MathTypesetResult* result) {
    if (!result) return;
    
    if (result->view_tree_root) {
        view_node_release(result->view_tree_root);
    }
    
    if (result->layout_context) {
        math_layout_context_destroy(result->layout_context);
    }
    
    free(result);
}

// Baseline positioning
void position_math_baseline(ViewNode* math_node, double baseline_y) {
    if (!math_node) return;
    
    double current_baseline = calculate_math_baseline_offset(math_node);
    double offset = baseline_y - current_baseline;
    
    // Adjust position
    math_node->position.y += offset;
    
    // Update bounds
    math_node->bounds.origin.y += offset;
}

double calculate_math_baseline_offset(ViewNode* math_node) {
    if (!math_node) return 0.0;
    
    // For math elements, the baseline is typically at the mathematical axis
    // For now, use a simple heuristic: 25% from the bottom
    return math_node->position.y + math_node->size.height * 0.75;
}

// Validation and optimization
bool validate_math_tree_structure(ViewNode* math_root) {
    if (!math_root) return false;
    
    // Basic validation: check that all math elements have proper structure
    if (math_root->type == VIEW_NODE_MATH_ELEMENT) {
        ViewMathElement* math_elem = math_root->content.math_elem;
        if (!math_elem) return false;
        
        // Type-specific validation
        switch (math_elem->type) {
            case VIEW_MATH_FRACTION:
                return math_elem->content.fraction.numerator && 
                       math_elem->content.fraction.denominator;
            case VIEW_MATH_SUPERSCRIPT:
            case VIEW_MATH_SUBSCRIPT:
                return math_elem->content.script.base && 
                       math_elem->content.script.script;
            case VIEW_MATH_RADICAL:
                return math_elem->content.radical.radicand != NULL;
            default:
                return true;
        }
    }
    
    // Recursively validate children
    ViewNode* child = math_root->first_child;
    while (child) {
        if (!validate_math_tree_structure(child)) {
            return false;
        }
        child = child->next_sibling;
    }
    
    return true;
}

void optimize_math_layout(ViewNode* math_root) {
    if (!math_root) return;
    
    // TODO: Implement layout optimizations
    // - Combine adjacent atoms
    // - Optimize spacing
    // - Simplify nested structures
}

// Error handling
void report_math_typeset_error(TypesetEngine* engine, MathTypesetErrorCode code, 
                              const char* message, Item source_item) {
    if (!engine || !message) return;
    
    log_error("Math typeset error %d: %s", code, message);
    
    // TODO: Add error to engine's error list
}

// Metrics calculation
MathTypesetMetrics* calculate_math_typeset_metrics(ViewTree* math_tree) {
    if (!math_tree) return NULL;
    
    MathTypesetMetrics* metrics = calloc(1, sizeof(MathTypesetMetrics));
    if (!metrics) return NULL;
    
    // TODO: Implement metrics calculation by traversing the tree
    // For now, provide placeholder values
    metrics->total_math_elements = 1;
    metrics->layout_time_ms = 1.0;
    metrics->render_time_ms = 0.5;
    metrics->memory_used_bytes = sizeof(ViewTree) + sizeof(ViewNode);
    
    return metrics;
}

void debug_print_math_metrics(MathTypesetMetrics* metrics) {
    if (!metrics) return;
    
    log_info("Math Typeset Metrics:");
    log_info("  Total elements: %d", metrics->total_math_elements);
    log_info("  Atoms: %d", metrics->atoms_count);
    log_info("  Fractions: %d", metrics->fractions_count);
    log_info("  Scripts: %d", metrics->scripts_count);
    log_info("  Radicals: %d", metrics->radicals_count);
    log_info("  Layout time: %.2f ms", metrics->layout_time_ms);
    log_info("  Render time: %.2f ms", metrics->render_time_ms);
    log_info("  Memory used: %zu bytes", metrics->memory_used_bytes);
}

// Placeholder function for math parser integration
Item input_math_from_string(const char* math_string, const char* flavor) {
    // This should interface with the existing input-math.cpp parser
    // For now, return an error to indicate the function needs implementation
    log_error("input_math_from_string: Function needs implementation");
    return (Item){.item = ITEM_ERROR};
}

// Integration helper functions for Lambda math parser

static Input* create_lambda_input(const char* content) {
    if (!content) return NULL;
    
    Input* input = (Input*)malloc(sizeof(Input));
    if (!input) return NULL;
    
    input->content = strdup(content);
    input->length = strlen(content);
    input->position = 0;
    input->sb = stringbuf_new(256);
    input->error_message = NULL;
    
    return input;
}

static void destroy_lambda_input(Input* input) {
    if (!input) return;
    
    if (input->content) free((void*)input->content);
    if (input->sb) stringbuf_destroy(input->sb);
    if (input->error_message) free(input->error_message);
    free(input);
}

static Item parse_lambda_math(Input* input, int flavor) {
    if (!input) return (Item){.item = ITEM_ERROR};
    
    // This would call the actual Lambda math parser from input-math.cpp
    // The function signature would be something like:
    // return input_parse_math_expression(input, flavor);
    
    // For now, we return an error to indicate the parser needs to be linked
    log_info("parse_lambda_math: Attempting to parse %d bytes with flavor %d", 
             input->length, flavor);
    
    // In a real implementation, this would parse the math expression
    // and return a Lambda element tree
    return (Item){.item = ITEM_ERROR};
}

// Apply typesetting options to view tree
static void apply_math_typeset_options(ViewTree* view_tree, const MathTypesetOptions* options) {
    if (!view_tree || !options) return;
    
    // Apply font size, style, spacing, etc.
    // This would traverse the view tree and update rendering parameters
    log_info("apply_math_typeset_options: Applying options (font_size=%.1f, display_style=%s)",
             options->font_size, options->display_style ? "true" : "false");
}
