#ifndef MATH_LAYOUT_H
#define MATH_LAYOUT_H

#include "../view/view_tree.h"
#include "../../lambda/lambda.h"

// Math layout engine for mathematical typesetting

// Main math layout functions
ViewNode* layout_math_expression(ViewNode* math_node, MathLayoutContext* ctx);
ViewNode* layout_math_atom(ViewNode* atom_node, MathLayoutContext* ctx);
ViewNode* layout_math_fraction(ViewNode* fraction_node, MathLayoutContext* ctx);
ViewNode* layout_math_script(ViewNode* script_node, MathLayoutContext* ctx, bool is_superscript);
ViewNode* layout_math_radical(ViewNode* radical_node, MathLayoutContext* ctx);
ViewNode* layout_math_matrix(ViewNode* matrix_node, MathLayoutContext* ctx);
ViewNode* layout_math_delimiter(ViewNode* delimiter_node, MathLayoutContext* ctx);
ViewNode* layout_math_function(ViewNode* function_node, MathLayoutContext* ctx);
ViewNode* layout_math_operator(ViewNode* operator_node, MathLayoutContext* ctx);
ViewNode* layout_math_accent(ViewNode* accent_node, MathLayoutContext* ctx);
ViewNode* layout_math_underover(ViewNode* underover_node, MathLayoutContext* ctx);
ViewNode* layout_math_spacing(ViewNode* spacing_node, MathLayoutContext* ctx);

// Math positioning and spacing
double calculate_math_spacing(enum ViewMathClass left, enum ViewMathClass right, enum ViewMathStyle style);
void position_math_elements(ViewNode* container, MathLayoutContext* ctx);
MathMetrics calculate_math_metrics(ViewFont* font, enum ViewMathStyle style);
void apply_math_style_scaling(MathLayoutContext* ctx, enum ViewMathStyle new_style);

// Math font and glyph handling
ViewFont* get_math_font(const char* font_name, double size);
ViewFont* get_text_font(const char* font_name, double size);
uint32_t get_math_glyph(ViewFont* font, const char* symbol);
double get_glyph_width(ViewFont* font, uint32_t glyph_id);
double get_glyph_height(ViewFont* font, uint32_t glyph_id);
double get_glyph_depth(ViewFont* font, uint32_t glyph_id);

// Math style utilities
enum ViewMathStyle get_smaller_style(enum ViewMathStyle style);
enum ViewMathStyle get_superscript_style(enum ViewMathStyle style);
enum ViewMathStyle get_subscript_style(enum ViewMathStyle style);
bool is_display_style(enum ViewMathStyle style);
double get_style_scale_factor(enum ViewMathStyle from_style, enum ViewMathStyle to_style);

// Math class detection
enum ViewMathClass detect_math_class_from_symbol(const char* symbol);
enum ViewMathClass detect_math_class_from_operator(const char* op_name);
bool is_large_operator(const char* op_name);
bool needs_limits(const char* op_name, enum ViewMathStyle style);

// Layout context management
MathLayoutContext* math_layout_context_create(ViewFont* math_font, ViewFont* text_font, enum ViewMathStyle style);
MathLayoutContext* math_layout_context_copy(MathLayoutContext* ctx);
void math_layout_context_destroy(MathLayoutContext* ctx);
void math_layout_context_set_style(MathLayoutContext* ctx, enum ViewMathStyle style, bool cramped);

// Math element creation helpers
ViewNode* create_math_atom_node(const char* symbol, const char* unicode);
ViewNode* create_math_fraction_node(ViewNode* numerator, ViewNode* denominator);
ViewNode* create_math_script_node(ViewNode* base, ViewNode* script, bool is_superscript);
ViewNode* create_math_radical_node(ViewNode* radicand, ViewNode* index);
ViewNode* create_math_matrix_node(ViewNode** rows, int row_count, int* col_counts, const char* delim_style);
ViewNode* create_math_delimiter_node(const char* open_delim, const char* close_delim, ViewNode* content);
ViewNode* create_math_function_node(const char* function_name, ViewNode* argument);
ViewNode* create_math_operator_node(const char* operator_name, ViewNode* operand);
ViewNode* create_math_accent_node(const char* accent_type, ViewNode* base);
ViewNode* create_math_spacing_node(double amount, const char* space_type);

// Math dimension calculation
void calculate_math_node_dimensions(ViewNode* node, MathLayoutContext* ctx);
double calculate_math_height(ViewNode* node);
double calculate_math_depth(ViewNode* node);
double calculate_math_width(ViewNode* node);
ViewRect calculate_math_bounding_box(ViewNode* node);

// Math positioning utilities
void position_fraction_elements(ViewNode* fraction_node, MathLayoutContext* ctx);
void position_script_elements(ViewNode* script_node, MathLayoutContext* ctx);
void position_radical_elements(ViewNode* radical_node, MathLayoutContext* ctx);
void position_matrix_elements(ViewNode* matrix_node, MathLayoutContext* ctx);
void position_delimiter_elements(ViewNode* delimiter_node, MathLayoutContext* ctx);

// Math validation and debugging
bool validate_math_layout(ViewNode* math_node);
void debug_print_math_tree(ViewNode* math_node, int indent);
void debug_print_math_metrics(MathMetrics* metrics);

#endif // MATH_LAYOUT_H
