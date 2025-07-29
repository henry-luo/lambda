#ifndef MATH_METRICS_H
#define MATH_METRICS_H

#include "../typeset.h"

// Default math constants (based on Computer Modern and TeX defaults)
// All values are in units of em (relative to font size)

// Mathematical font metrics structure
typedef struct MathFontMetrics {
    // Basic font metrics
    float units_per_em;
    float ascent;
    float descent;
    float line_gap;
    float cap_height;
    float x_height;
    
    // Math-specific metrics
    float math_leading;
    float axis_height;          // Height of mathematical axis (center of +, -, =)
    float accent_base_height;   // Height for positioning accents
    float flattened_accent_base_height;
    
    // Script positioning
    float subscript_shift_down;
    float subscript_top_max;
    float subscript_baseline_drop_min;
    float superscript_shift_up;
    float superscript_shift_up_cramped;
    float superscript_bottom_min;
    float superscript_baseline_drop_max;
    
    // Script spacing
    float sub_superscript_gap_min;
    float superscript_bottom_max_with_subscript;
    float space_after_script;
    
    // Large operator positioning
    float upper_limit_gap_min;
    float upper_limit_baseline_rise_min;
    float lower_limit_gap_min;
    float lower_limit_baseline_drop_min;
    
    // Fraction parameters
    float fraction_rule_thickness;
    float fraction_numerator_shift_up;
    float fraction_numerator_display_style_shift_up;
    float fraction_denominator_shift_down;
    float fraction_denominator_display_style_shift_down;
    float fraction_numerator_gap_min;
    float fraction_num_display_style_gap_min;
    float fraction_denominator_gap_min;
    float fraction_denom_display_style_gap_min;
    
    // Stack parameters (for binomial coefficients, etc.)
    float stack_top_shift_up;
    float stack_top_display_style_shift_up;
    float stack_bottom_shift_down;
    float stack_bottom_display_style_shift_down;
    float stack_gap_min;
    float stack_display_style_gap_min;
    float stretch_stack_top_shift_up;
    float stretch_stack_bottom_shift_down;
    float stretch_stack_gap_above_min;
    float stretch_stack_gap_below_min;
    
    // Radical parameters
    float radical_vertical_gap;
    float radical_display_style_vertical_gap;
    float radical_rule_thickness;
    float radical_extra_ascender;
    float radical_kern_before_degree;
    float radical_kern_after_degree;
    float radical_degree_bottom_raise_percent;
    
    // Overbar and underbar
    float overbar_vertical_gap;
    float overbar_rule_thickness;
    float overbar_extra_ascender;
    float underbar_vertical_gap;
    float underbar_rule_thickness;
    float underbar_extra_descender;
    
    // Skewed fraction parameters
    float skewed_fraction_horizontal_gap;
    float skewed_fraction_vertical_gap;
    
    // General parameters
    float script_percent_scale_down;        // Typically 0.7
    float script_script_percent_scale_down; // Typically 0.5
    float delimited_sub_formula_min_height;
    float display_operator_min_height;
    
    // Font variants available
    bool has_script_variant;
    bool has_scriptscript_variant;
    bool has_display_variant;
    bool has_text_variant;
} MathFontMetrics;

// Math spacing constants (in units of 18ths of an em)
// Based on The TeXbook, Appendix G
typedef struct MathSpacing {
    // Spacing matrix [left_class][right_class]
    // Values: 0=no space, 1=thin space, 2=medium space, 3=thick space, 4=invalid
    int spacing_matrix[8][8];
    
    // Spacing amounts in em units
    float thin_space;           // 3/18 em
    float medium_space;         // 4/18 em  
    float thick_space;          // 5/18 em
    float neg_thin_space;       // -3/18 em
    float neg_medium_space;     // -4/18 em
    float neg_thick_space;      // -5/18 em
    
    // Additional spacing
    float quad_space;           // 1 em
    float en_space;             // 0.5 em
    float hair_space;           // 1/18 em
} MathSpacing;

// Default mathematical spacing rules
extern const MathSpacing DEFAULT_MATH_SPACING;

// Math font metrics management
MathFontMetrics* math_font_metrics_create(Font* font);
void math_font_metrics_destroy(MathFontMetrics* metrics);
MathFontMetrics* get_default_math_metrics(void);
void load_math_metrics_from_font(MathFontMetrics* metrics, Font* font);

// Math constants conversion
MathConstants* convert_font_metrics_to_constants(MathFontMetrics* metrics, float font_size);
void scale_math_constants(MathConstants* constants, float scale_factor);

// Font size calculations for different math styles
float calculate_script_size(float base_size, MathFontMetrics* metrics);
float calculate_scriptscript_size(float base_size, MathFontMetrics* metrics);
float get_size_for_math_style(float base_size, MathStyle style, MathFontMetrics* metrics);

// Spacing calculations
float calculate_math_spacing(MathClass left_class, MathClass right_class, MathStyle style);
float get_thin_space(float font_size);
float get_medium_space(float font_size);
float get_thick_space(float font_size);
float get_quad_space(float font_size);

// Math axis and baseline calculations
float calculate_math_axis_height(Font* font, MathFontMetrics* metrics);
float calculate_baseline_shift_for_style(MathStyle style, MathFontMetrics* metrics);

// Fraction layout calculations
typedef struct FractionMetrics {
    float rule_thickness;
    float numerator_shift_up;
    float denominator_shift_down;
    float numerator_gap_min;
    float denominator_gap_min;
    float axis_height;
} FractionMetrics;

FractionMetrics calculate_fraction_metrics(MathStyle style, MathFontMetrics* font_metrics, float font_size);
float calculate_fraction_rule_thickness(MathStyle style, MathFontMetrics* metrics, float font_size);

// Script layout calculations
typedef struct ScriptMetrics {
    float superscript_shift_up;
    float subscript_shift_down;
    float gap_min;
    float script_size;
    float scriptscript_size;
} ScriptMetrics;

ScriptMetrics calculate_script_metrics(MathStyle style, MathFontMetrics* font_metrics, float font_size);
float calculate_superscript_shift(MathStyle style, bool has_subscript, MathFontMetrics* metrics, float font_size);
float calculate_subscript_shift(MathStyle style, bool has_superscript, MathFontMetrics* metrics, float font_size);

// Radical layout calculations
typedef struct RadicalMetrics {
    float rule_thickness;
    float vertical_gap;
    float extra_ascender;
    float kern_before_degree;
    float kern_after_degree;
    float degree_raise_percent;
} RadicalMetrics;

RadicalMetrics calculate_radical_metrics(MathStyle style, MathFontMetrics* font_metrics, float font_size);

// Large operator calculations
typedef struct LargeOpMetrics {
    float display_size;
    float text_size;
    float min_height;
    float upper_limit_gap;
    float lower_limit_gap;
    float limit_baseline_rise;
    float limit_baseline_drop;
} LargeOpMetrics;

LargeOpMetrics calculate_large_op_metrics(MathStyle style, MathFontMetrics* font_metrics, float font_size);
bool should_use_display_limits(const char* operator_name, MathStyle style);

// Delimiter calculations
float calculate_delimiter_height(float inner_height, float inner_depth, MathFontMetrics* metrics);
float calculate_delimiter_axis_shift(float delimiter_height, MathFontMetrics* metrics);

// Accent positioning
float calculate_accent_position(MathBox* base, MathBox* accent, MathFontMetrics* metrics);
float calculate_accent_skew(MathBox* base, uint32_t accent_char, MathFontMetrics* metrics);

// Matrix and array calculations
typedef struct MatrixMetrics {
    float row_separation;
    float column_separation;
    float baseline_separation;
    float delim_shortfall;
    float axis_height;
} MatrixMetrics;

MatrixMetrics calculate_matrix_metrics(MathStyle style, MathFontMetrics* font_metrics, float font_size);

// Default math constants for common fonts
MathFontMetrics* get_computer_modern_metrics(void);
MathFontMetrics* get_latin_modern_metrics(void);
MathFontMetrics* get_stix_math_metrics(void);
MathFontMetrics* get_asana_math_metrics(void);
MathFontMetrics* get_tex_gyre_termes_metrics(void);

// Math font detection and loading
bool font_has_math_constants(Font* font);
MathFontMetrics* extract_math_metrics_from_font(Font* font);
MathFontMetrics* create_approximated_math_metrics(Font* font);

// Measurement utilities
float em_to_points(float em_value, float font_size);
float points_to_em(float point_value, float font_size);
float scale_for_math_style(float value, MathStyle style);

// Math metrics validation
bool validate_math_metrics(MathFontMetrics* metrics);
void fix_invalid_math_metrics(MathFontMetrics* metrics);
void print_math_metrics(MathFontMetrics* metrics);

// Style-specific metrics
typedef struct StyleMetrics {
    float font_size;
    float axis_height;
    float rule_thickness;
    float default_line_thickness;
    float big_op_spacing1;      // Space above big operators
    float big_op_spacing2;      // Space below big operators
    float big_op_spacing3;      // Space above limits
    float big_op_spacing4;      // Space below limits
    float big_op_spacing5;      // Minimum space around big operators
} StyleMetrics;

StyleMetrics calculate_style_metrics(MathStyle style, MathFontMetrics* font_metrics, float base_font_size);

// Cramped style adjustments
float adjust_for_cramped_style(float shift_up, bool is_cramped);
MathStyle make_cramped(MathStyle style);
bool is_cramped(MathStyle style);

// TeX compatibility constants
#define TEX_THIN_SPACE 3        // 3/18 em
#define TEX_MEDIUM_SPACE 4      // 4/18 em  
#define TEX_THICK_SPACE 5       // 5/18 em
#define TEX_QUAD_SPACE 18       // 18/18 em = 1 em

// Font size ratios from TeX
#define TEX_SCRIPT_RATIO 0.7f
#define TEX_SCRIPTSCRIPT_RATIO 0.5f

// Default rule thickness (typically 0.04 em)
#define DEFAULT_RULE_THICKNESS 0.04f

#endif // MATH_METRICS_H
