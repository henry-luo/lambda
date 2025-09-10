#ifndef MATH_TYPESET_H
#define MATH_TYPESET_H

#include "view/view_tree.h"
#include "layout/math_layout.h"
#include "integration/lambda_math_bridge.h"
#include "../lambda/lambda.h"

// Main typesetting functions
ViewTree* typeset_math_from_latex(const char* latex_math, TypesetOptions* options);
ViewTree* typeset_math_from_lambda_tree(Item math_tree, TypesetOptions* options);

/**
 * Typeset mathematical expression from Lambda element tree
 * Integrates directly with Lambda math parser output
 * @param lambda_element Root element from Lambda math parser
 * @param options Typesetting options (can be NULL for defaults)
 * @return ViewTree ready for rendering, or NULL on error
 */
ViewTree* typeset_math_from_lambda_element(Element* lambda_element, const MathTypesetOptions* options);

// Integration with existing typeset system
ViewNode* process_math_element_in_document(TypesetEngine* engine, Item math_element);
void integrate_math_into_document_flow(ViewTree* document, ViewNode* math_node);

// Math-specific typeset options
typedef struct {
    enum ViewMathStyle default_style;     // Display or inline
    double math_scale;                    // Math scaling factor
    bool use_display_mode;                // Force display mode
    const char* math_font_family;         // Math font preference
    bool render_equation_numbers;         // Show equation numbers
    double baseline_skip;                 // Baseline skip for multi-line math
    double math_surround;                 // Space around inline math
} MathTypesetOptions;

MathTypesetOptions* math_typeset_options_create_default(void);
void math_typeset_options_destroy(MathTypesetOptions* options);

// Math element processing workflow
typedef struct {
    Item source_lambda_tree;              // Original Lambda tree
    ViewNode* view_tree_root;             // Converted view tree
    MathLayoutContext* layout_context;    // Layout context
    ViewRect bounding_box;                // Final bounding box
    double baseline_offset;               // Baseline offset
} MathTypesetResult;

MathTypesetResult* process_math_expression(TypesetEngine* engine, Item math_expr, 
                                         MathTypesetOptions* options);
void math_typeset_result_destroy(MathTypesetResult* result);

// Math layout and positioning utilities
ViewNode* layout_inline_math(ViewNode* math_node, MathLayoutContext* ctx);
ViewNode* layout_display_math(ViewNode* math_node, MathLayoutContext* ctx);
void position_math_baseline(ViewNode* math_node, double baseline_y);
double calculate_math_baseline_offset(ViewNode* math_node);

// Math element validation and optimization
bool validate_math_tree_structure(ViewNode* math_root);
void optimize_math_layout(ViewNode* math_root);
void simplify_math_tree(ViewNode* math_root);

// Error handling and diagnostics
typedef enum {
    MATH_TYPESET_SUCCESS,
    MATH_TYPESET_ERROR_INVALID_INPUT,
    MATH_TYPESET_ERROR_PARSING_FAILED,
    MATH_TYPESET_ERROR_LAYOUT_FAILED,
    MATH_TYPESET_ERROR_MEMORY_ERROR,
    MATH_TYPESET_ERROR_FONT_MISSING
} MathTypesetErrorCode;

typedef struct {
    MathTypesetErrorCode code;
    char* message;
    Item source_item;
    int line_number;
    int column_number;
} MathTypesetError;

void report_math_typeset_error(TypesetEngine* engine, MathTypesetErrorCode code, 
                              const char* message, Item source_item);

// Performance and metrics
typedef struct {
    int total_math_elements;              // Total math elements processed
    int atoms_count;                      // Number of atoms
    int fractions_count;                  // Number of fractions
    int scripts_count;                    // Number of super/subscripts
    int radicals_count;                   // Number of radicals
    double layout_time_ms;                // Layout time in milliseconds
    double render_time_ms;                // Rendering time in milliseconds
    size_t memory_used_bytes;             // Memory used in bytes
} MathTypesetMetrics;

MathTypesetMetrics* calculate_math_typeset_metrics(ViewTree* math_tree);
void debug_print_math_metrics(MathTypesetMetrics* metrics);

#endif // MATH_TYPESET_H
