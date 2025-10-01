#pragma once

#include "../view.hpp"
#include "../../lambda/lambda.h"
#include "../../lambda/input/input.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for math integration
Item input_math(Input* input, const char* math_content);
void parse_math(Input* input, const char* math_string, const char* flavor);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// Mathematical typesetting using existing Radiant views + minimal extensions
class MathLayoutEngine {
private:
    UiContext* ui_context;
    VariableMemPool* pool;
    FontProp* math_font;
    FontProp* text_font;
    
    // Math integration state
    Input* math_input;
    Context* lambda_context;
    
    // Math styling constants
    struct MathConstants {
        double display_scale;           // Scale factor for display math (1.2)
        double script_scale;            // Scale factor for superscripts/subscripts (0.7)
        double scriptscript_scale;      // Scale factor for nested scripts (0.5)
        double fraction_line_thickness; // Fraction line thickness
        double radical_rule_thickness;  // Square root line thickness
        double axis_height_ratio;       // Math axis height as ratio of font size
        double sup_shift_ratio;         // Superscript shift ratio
        double sub_shift_ratio;         // Subscript shift ratio
    };
    
    MathConstants constants;
    
public:
    // Constructor/Destructor
    MathLayoutEngine(UiContext* ui_context);
    ~MathLayoutEngine();
    
    // Main math processing (reuse existing input-math.cpp)
    ViewSpan* layout_math_expression(Item math_ast, bool is_display_mode);
    ViewSpan* layout_math_expression(const char* math_content, const char* flavor, bool is_display_mode);
    
    // Math element types (extend existing ViewSpan/ViewBlock)
    ViewSpan* layout_math_fraction(Item numerator, Item denominator);    // Use nested ViewSpan
    ViewSpan* layout_math_superscript(Item base, Item superscript);      // Use positioned ViewSpan
    ViewSpan* layout_math_subscript(Item base, Item subscript);          // Use positioned ViewSpan
    ViewSpan* layout_math_radical(Item radicand, Item index);            // Use ViewSpan with special styling
    ViewTable* layout_math_matrix(Item matrix_node);                     // Reuse existing ViewTable
    ViewSpan* layout_math_delimiter(Item content, const char* left, const char* right);
    ViewSpan* layout_math_function(Item function_node);                  // sin, cos, log, etc.
    ViewSpan* layout_math_operator(Item operator_node);                  // +, -, ×, ÷, ∫, ∑
    ViewSpan* layout_math_accent(Item base, const char* accent_type);    // Hat, tilde, bar
    
    // Mathematical symbols (extend existing text rendering)
    ViewText* create_math_symbol(const char* latex_command);             // Reuse ViewText
    ViewText* create_math_operator(const char* operator_name);           // Reuse ViewText
    ViewText* create_math_function(const char* function_name);           // Reuse ViewText
    ViewText* create_math_variable(const char* variable_name);           // Reuse ViewText
    ViewText* create_math_number(const char* number_text);               // Reuse ViewText
    
    // Math spacing and positioning (extend existing layout)
    void apply_math_spacing(ViewSpan* math_span, const char* spacing_type);
    void position_math_elements(ViewSpan* container, ViewSpan** elements, int count);
    ViewSpan* create_math_spacing(double amount_pt);
    
    // Font and sizing (extend existing FontProp)
    FontProp* get_math_font(int math_style, double base_size);
    void apply_math_font_sizing(ViewSpan* span, int math_style);
    FontProp* create_math_font_variant(const char* variant, double size_factor);
    
    // Integration with input-math.cpp
    Item parse_math_with_existing_parser(const char* math_content, const char* flavor);
    ViewSpan* convert_math_ast_to_view(Item math_ast, bool is_display_mode);
    
    // Math element processing
    ViewSpan* process_math_element_by_type(Item element, bool is_display_mode);
    ViewSpan* process_math_group(Item group_element, bool is_display_mode);
    ViewSpan* process_math_sequence(Array* elements, bool is_display_mode);
    
    // Layout calculations
    void calculate_math_dimensions(ViewSpan* math_span);
    void position_superscript_subscript(ViewSpan* base, ViewSpan* superscript, ViewSpan* subscript);
    void position_fraction_parts(ViewSpan* fraction_container, ViewSpan* numerator, ViewSpan* denominator);
    void position_radical_parts(ViewSpan* radical_container, ViewSpan* radicand, ViewSpan* index);
    
    // Math constants and utilities
    double get_math_constant(const char* constant_name, double font_size);
    double calculate_math_axis_height(double font_size);
    double calculate_script_shift(bool is_superscript, double font_size);
    
    // Error handling and debugging
    void log_math_error(const char* message, Item element);
    bool validate_math_element(Item element);
    void debug_math_layout(ViewSpan* math_span, const char* description);

private:
    // Internal initialization
    void initialize_math_constants();
    void initialize_math_fonts();
    void initialize_math_parser();
    void cleanup_math_parser();
    
    // Internal math processing
    ViewSpan* create_math_container(const char* math_type);
    ViewSpan* wrap_in_math_span(ViewText* text_node, const char* math_class);
    void apply_math_styling(ViewSpan* span, const char* math_type, bool is_display);
    
    // Symbol and operator handling
    const char* get_unicode_for_latex_symbol(const char* latex_command);
    const char* get_math_font_for_symbol(const char* symbol_type);
    ViewText* create_symbol_with_font(const char* unicode_char, const char* font_family);
    
    // Layout helpers
    void set_math_position(ViewSpan* span, int x, int y);
    void set_math_dimensions(ViewSpan* span, int width, int height);
    int calculate_baseline_offset(ViewSpan* span, bool is_display);
    
    // Math-specific view creation
    ViewSpan* create_positioned_math_span(ViewSpan* content, int x_offset, int y_offset);
    ViewSpan* create_scaled_math_span(ViewSpan* content, double scale_factor);
    ViewSpan* create_math_line(int width, int thickness);
};

// Math symbol definitions and utilities
class MathSymbolRegistry {
private:
    struct MathSymbolDef {
        const char* latex_command;
        const char* unicode_char;
        const char* font_family;
        double relative_size;
        const char* symbol_class;  // "operator", "relation", "binary", "ordinary"
    };
    
    static MathSymbolDef symbol_table[];
    static int symbol_count;
    
public:
    // Symbol lookup
    static const MathSymbolDef* find_symbol(const char* latex_command);
    static const char* get_unicode_for_symbol(const char* latex_command);
    static const char* get_symbol_class(const char* latex_command);
    static double get_symbol_size_factor(const char* latex_command);
    
    // Symbol categories
    static bool is_large_operator(const char* latex_command);
    static bool is_binary_operator(const char* latex_command);
    static bool is_relation_symbol(const char* latex_command);
    static bool is_delimiter(const char* latex_command);
    
    // Greek letters and special characters
    static bool is_greek_letter(const char* latex_command);
    static const char* get_greek_unicode(const char* latex_command, bool uppercase);
};

// Utility functions for math layout
extern "C" {
    // C interface for math layout
    ViewSpan* math_layout_expression(UiContext* ui_context, const char* math_content, const char* flavor, bool is_display);
    ViewSpan* math_layout_from_ast(UiContext* ui_context, Item math_ast, bool is_display);
    
    // Math symbol utilities
    const char* math_get_unicode_for_latex(const char* latex_command);
    bool math_is_display_operator(const char* latex_command);
    
    // Math constants
    double math_get_display_scale(void);
    double math_get_script_scale(void);
    double math_get_axis_height_ratio(void);
}

#endif // __cplusplus
