#ifndef MATH_LAYOUT_H
#define MATH_LAYOUT_H

#include "../typeset.h"

// Math box types
typedef enum {
    MATH_BOX_ORDINARY,      // Ordinary symbol (variables, etc.)
    MATH_BOX_OPERATOR,      // Binary/unary operators
    MATH_BOX_BINARY_OP,     // Binary operators (+, -, etc.)
    MATH_BOX_RELATION,      // Relational operators (=, <, etc.)
    MATH_BOX_OPENING,       // Opening delimiter (()
    MATH_BOX_CLOSING,       // Closing delimiter ())
    MATH_BOX_PUNCTUATION,   // Punctuation (,, ;)
    MATH_BOX_FRACTION,      // Fraction
    MATH_BOX_RADICAL,       // Square root, etc.
    MATH_BOX_SUPERSCRIPT,   // Superscript
    MATH_BOX_SUBSCRIPT,     // Subscript
    MATH_BOX_SUBSUP,        // Combined sub/superscript
    MATH_BOX_OVERSCRIPT,    // Over accent/script
    MATH_BOX_UNDERSCRIPT,   // Under accent/script
    MATH_BOX_MATRIX,        // Matrix
    MATH_BOX_DELIMITER,     // Stretchy delimiter
    MATH_BOX_ACCENT,        // Accent
    MATH_BOX_LARGE_OP,      // Large operator (∑, ∫, etc.)
    MATH_BOX_PHANTOM,       // Invisible spacing
    MATH_BOX_HORIZONTAL,    // Horizontal list
    MATH_BOX_VERTICAL       // Vertical list
} MathBoxType;

// Math style (affects sizing and spacing)
typedef enum {
    MATH_STYLE_DISPLAY,         // Display style (large)
    MATH_STYLE_TEXT,            // Text style (medium)
    MATH_STYLE_SCRIPT,          // Script style (small)
    MATH_STYLE_SCRIPTSCRIPT     // Script-script style (very small)
} MathStyle;

// Math class (affects spacing)
typedef enum {
    MATH_CLASS_ORD = 0,     // Ordinary
    MATH_CLASS_OP = 1,      // Large operator
    MATH_CLASS_BIN = 2,     // Binary operator
    MATH_CLASS_REL = 3,     // Relation
    MATH_CLASS_OPEN = 4,    // Opening
    MATH_CLASS_CLOSE = 5,   // Closing
    MATH_CLASS_PUNCT = 6,   // Punctuation
    MATH_CLASS_INNER = 7    // Inner (for fractions, etc.)
} MathClass;

// Math box structure
struct MathBox {
    MathBoxType type;
    MathClass math_class;
    MathStyle style;
    
    // Dimensions
    float width;
    float height;
    float depth;               // Distance below baseline
    float italic_correction;   // Italic correction for slanted text
    
    // Position
    float x, y;               // Position relative to parent
    float baseline;           // Distance from bottom to baseline
    
    // Content
    Item lambda_expr;         // Original Lambda expression
    char* text_content;       // For text-based math (numbers, variables)
    uint32_t unicode_char;    // For single character symbols
    
    // Hierarchy
    struct MathBox* parent;
    struct MathBox* first_child;
    struct MathBox* last_child;
    struct MathBox* next_sibling;
    struct MathBox* prev_sibling;
    
    // Type-specific data
    union {
        struct {
            struct MathBox* numerator;
            struct MathBox* denominator;
            float rule_thickness;
        } fraction;
        
        struct {
            struct MathBox* base;
            struct MathBox* superscript;
            struct MathBox* subscript;
            float sup_shift;
            float sub_shift;
        } script;
        
        struct {
            struct MathBox* radicand;
            struct MathBox* index;      // For nth roots
            float rule_thickness;
            float extra_ascender;
        } radical;
        
        struct {
            struct MathBox** cells;     // 2D array of cells
            int rows;
            int cols;
            float* row_heights;
            float* col_widths;
            float row_separation;
            float col_separation;
        } matrix;
        
        struct {
            struct MathBox* nucleus;
            struct MathBox* limits_above;
            struct MathBox* limits_below;
            bool limits_display_style;
        } large_op;
        
        struct {
            struct MathBox* inner;
            uint32_t left_delim;
            uint32_t right_delim;
            float min_height;
        } delimited;
        
        struct {
            struct MathBox* base;
            struct MathBox* accent;
            bool is_wide;
        } accented;
    } u;
    
    // Font information
    Font* font;
    float font_size;
    
    // Layout state
    bool is_positioned;
    bool needs_layout;
    
    // Debug information
    char* debug_name;
};

// Math constants structure (from OpenType MATH table)
struct MathConstants {
    // General constants
    float script_percent_scale_down;
    float script_script_percent_scale_down;
    float delimited_sub_formula_min_height;
    float display_operator_min_height;
    float math_leading;
    
    // Axis and baseline
    float axis_height;
    float accent_base_height;
    float flattened_accent_base_height;
    
    // Subscripts
    float subscript_shift_down;
    float subscript_top_max;
    float subscript_baseline_drop_min;
    
    // Superscripts
    float superscript_shift_up;
    float superscript_shift_up_cramped;
    float superscript_bottom_min;
    float superscript_baseline_drop_max;
    
    // Sub-superscript gaps
    float sub_superscript_gap_min;
    float superscript_bottom_max_with_subscript;
    float space_after_script;
    
    // Upper and lower limits
    float upper_limit_gap_min;
    float upper_limit_baseline_rise_min;
    float lower_limit_gap_min;
    float lower_limit_baseline_drop_min;
    
    // Stack (fraction-like structures)
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
    
    // Fractions
    float fraction_rule_thickness;
    float fraction_numerator_shift_up;
    float fraction_numerator_display_style_shift_up;
    float fraction_denominator_shift_down;
    float fraction_denominator_display_style_shift_down;
    float fraction_numerator_gap_min;
    float fraction_num_display_style_gap_min;
    float fraction_denominator_gap_min;
    float fraction_denom_display_style_gap_min;
    
    // Skewed fractions (like 1/2)
    float skewed_fraction_horizontal_gap;
    float skewed_fraction_vertical_gap;
    
    // Overbar and underbar
    float overbar_vertical_gap;
    float overbar_rule_thickness;
    float overbar_extra_ascender;
    float underbar_vertical_gap;
    float underbar_rule_thickness;
    float underbar_extra_descender;
    
    // Radicals
    float radical_vertical_gap;
    float radical_display_style_vertical_gap;
    float radical_rule_thickness;
    float radical_extra_ascender;
    float radical_kern_before_degree;
    float radical_kern_after_degree;
    float radical_degree_bottom_raise_percent;
};

// Main math layout functions
MathBox* layout_math_expression(Item lambda_math, Font* math_font, MathStyle style);
MathBox* layout_math_from_string(const char* math_text, Font* math_font, MathStyle style);
void layout_math_box(MathBox* box, MathStyle style);
void position_math_box_children(MathBox* box);

// Math box creation
MathBox* math_box_create(MathBoxType type);
void math_box_destroy(MathBox* box);
void math_box_destroy_tree(MathBox* root);

// Math box hierarchy
void math_box_append_child(MathBox* parent, MathBox* child);
void math_box_remove_child(MathBox* parent, MathBox* child);
void math_box_insert_before(MathBox* reference, MathBox* new_box);

// Type-specific layout functions
MathBox* layout_math_fraction(Item numerator, Item denominator, Font* font, MathStyle style);
MathBox* layout_math_superscript(Item base, Item exponent, Font* font, MathStyle style);
MathBox* layout_math_subscript(Item base, Item subscript, Font* font, MathStyle style);
MathBox* layout_math_subsup(Item base, Item subscript, Item superscript, Font* font, MathStyle style);
MathBox* layout_math_radical(Item radicand, Item index, Font* font, MathStyle style);
MathBox* layout_math_matrix(Item matrix_expr, Font* font, MathStyle style);
MathBox* layout_math_large_op(Item op_expr, Font* font, MathStyle style);
MathBox* layout_math_delimiter(MathBox* inner, uint32_t left, uint32_t right, Font* font);
MathBox* layout_math_accent(Item base, uint32_t accent_char, Font* font, MathStyle style);

// Math symbol and operator layout
MathBox* layout_math_symbol(const char* symbol, Font* font, MathStyle style);
MathBox* layout_math_number(const char* number, Font* font, MathStyle style);
MathBox* layout_math_identifier(const char* identifier, Font* font, MathStyle style);
MathBox* layout_math_operator(const char* operator, Font* font, MathStyle style);

// Math spacing
float get_math_spacing(MathClass left_class, MathClass right_class, MathStyle style);
void apply_math_spacing(MathBox* box);
float get_italic_correction(MathBox* box);

// Math style utilities
MathStyle get_script_style(MathStyle current_style);
MathStyle get_scriptscript_style(MathStyle current_style);
float get_style_size_multiplier(MathStyle style);
bool is_display_style(MathStyle style);
bool is_cramped_style(MathStyle style);
MathStyle get_cramped_style(MathStyle style);

// Math constants access
MathConstants* get_math_constants(Font* math_font);
MathConstants* get_default_math_constants(void);
void set_math_constants_from_font(MathConstants* constants, Font* font);

// Math box measurement
void measure_math_box(MathBox* box);
float math_box_get_total_width(MathBox* box);
float math_box_get_total_height(MathBox* box);
float math_box_get_total_depth(MathBox* box);

// Lambda expression parsing for math
MathBox* parse_lambda_math_expression(Item expr, Font* font, MathStyle style);
MathClass get_math_class_from_lambda_symbol(const char* symbol);
MathBoxType get_math_box_type_from_lambda_op(const char* op);

// Math font utilities
bool font_has_math_table(Font* font);
Font* get_math_font_variant(Font* base_font, MathStyle style);
float get_math_font_size(Font* font, MathStyle style);

// Math layout context
typedef struct MathLayoutContext {
    Font* math_font;
    MathStyle current_style;
    MathConstants* constants;
    bool display_mode;
    float scale_factor;
    
    // Environment state
    bool in_subscript;
    bool in_superscript;
    bool in_fraction;
    bool in_radical;
    
    // Layout caches
    void* symbol_cache;
    void* spacing_cache;
} MathLayoutContext;

MathLayoutContext* math_layout_context_create(Font* math_font, MathStyle style);
void math_layout_context_destroy(MathLayoutContext* ctx);
void math_layout_context_push_style(MathLayoutContext* ctx, MathStyle new_style);
void math_layout_context_pop_style(MathLayoutContext* ctx);

// Advanced math features
MathBox* layout_math_cases(Item cases_expr, Font* font, MathStyle style);
MathBox* layout_math_aligned(Item aligned_expr, Font* font, MathStyle style);
MathBox* layout_math_array(Item array_expr, Font* font, MathStyle style);
MathBox* layout_math_phantom(Item phantom_expr, Font* font, MathStyle style);

// Math debugging and validation
void math_box_print_tree(MathBox* root, int indent);
bool math_box_validate_tree(MathBox* root);
char* math_box_to_string(MathBox* box);
void math_box_print_debug_info(MathBox* box);

// Math box utilities
MathBox* math_box_find_by_type(MathBox* root, MathBoxType type);
void math_box_walk_tree(MathBox* root, void (*callback)(MathBox*, void*), void* user_data);
MathBox* math_box_copy(MathBox* source);
void math_box_scale(MathBox* box, float scale_factor);

#endif // MATH_LAYOUT_H
