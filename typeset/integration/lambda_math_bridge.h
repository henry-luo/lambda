#ifndef LAMBDA_MATH_BRIDGE_H
#define LAMBDA_MATH_BRIDGE_H

#include "../typeset.h"
#include "../view/view_tree.h"
#include "../layout/math_layout.h"
#include "../../lambda/lambda.h"

// Bridge between Lambda math element trees and typesetting view trees

// Main conversion functions
ViewNode* convert_lambda_math_to_viewnode(TypesetEngine* engine, Item math_item);
ViewNode* convert_math_element_to_viewnode(TypesetEngine* engine, Item element);

// Specific math element converters
ViewNode* convert_math_fraction(TypesetEngine* engine, Item frac_element);
ViewNode* convert_math_superscript(TypesetEngine* engine, Item pow_element);
ViewNode* convert_math_subscript(TypesetEngine* engine, Item sub_element);
ViewNode* convert_math_radical(TypesetEngine* engine, Item sqrt_element);
ViewNode* convert_math_sum_product(TypesetEngine* engine, Item sum_element);
ViewNode* convert_math_integral(TypesetEngine* engine, Item int_element);
ViewNode* convert_math_matrix(TypesetEngine* engine, Item matrix_element);
ViewNode* convert_math_function(TypesetEngine* engine, Item func_element);
ViewNode* convert_math_operator(TypesetEngine* engine, Item op_element);
ViewNode* convert_math_symbol(TypesetEngine* engine, Item symbol_item);
ViewNode* convert_math_text(TypesetEngine* engine, Item text_item);
ViewNode* convert_math_spacing(TypesetEngine* engine, Item spacing_element);
ViewNode* convert_math_accent(TypesetEngine* engine, Item accent_element);
ViewNode* convert_math_delimiter(TypesetEngine* engine, Item delim_element);

// Math element recognition and analysis
enum ViewMathElementType detect_math_element_type(Item element);
enum ViewMathClass get_math_class_from_element(Item element);
enum ViewMathClass get_math_class_from_operator(const char* op_name);
bool is_math_operator(const char* op_name);
bool is_large_operator(const char* op_name);
bool is_function_name(const char* name);

// Lambda element inspection utilities
bool lambda_element_has_operator(Item element, const char* op_name);
const char* get_lambda_element_operator_name(Item element);
Item get_lambda_element_child(Item element, int index);
int get_lambda_element_child_count(Item element);
const char* get_lambda_element_attribute(Item element, const char* attr_name);
bool lambda_element_is_math_element(Item element);

// Math symbol and Unicode mapping
const char* get_unicode_for_latex_symbol(const char* latex_cmd);
const char* get_unicode_for_symbol(const char* symbol);
enum ViewMathClass classify_unicode_symbol(const char* unicode);

// Math context and options
typedef struct {
    enum ViewMathStyle default_style;     // Display or inline
    double math_scale;                    // Math scaling factor
    bool use_display_mode;                // Force display mode
    const char* math_font_family;         // Math font preference
    bool render_equation_numbers;         // Show equation numbers
    MathLayoutContext* layout_context;    // Layout context
} MathConversionOptions;

MathConversionOptions* math_conversion_options_create(void);
void math_conversion_options_destroy(MathConversionOptions* options);
void set_math_conversion_options(TypesetEngine* engine, MathConversionOptions* options);

// Error handling and debugging
typedef struct {
    char* message;                        // Error message
    Item source_item;                     // Source Lambda item
    int line_number;                      // Source line (if available)
    int column_number;                    // Source column (if available)
} MathConversionError;

void report_math_conversion_error(TypesetEngine* engine, const char* message, Item source_item);
void debug_print_lambda_math_tree(Item math_item, int indent);

// Validation and verification
bool validate_lambda_math_element(Item element);
bool validate_math_viewnode(ViewNode* node);

#endif // LAMBDA_MATH_BRIDGE_H
