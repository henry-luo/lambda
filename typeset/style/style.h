#ifndef STYLE_H
#define STYLE_H

#include "../typeset.h"

// Color structure
typedef struct Color {
    float r, g, b, a;           // RGBA components (0.0 - 1.0)
} Color;

// Predefined colors
extern const Color COLOR_BLACK;
extern const Color COLOR_WHITE;
extern const Color COLOR_RED;
extern const Color COLOR_GREEN;
extern const Color COLOR_BLUE;
extern const Color COLOR_TRANSPARENT;

// Text alignment
typedef enum {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT,
    TEXT_ALIGN_JUSTIFY
} TextAlign;

// Vertical alignment
typedef enum {
    VERTICAL_ALIGN_TOP,
    VERTICAL_ALIGN_MIDDLE,
    VERTICAL_ALIGN_BOTTOM,
    VERTICAL_ALIGN_BASELINE,
    VERTICAL_ALIGN_SUB,
    VERTICAL_ALIGN_SUPER
} VerticalAlign;

// Display types
typedef enum {
    DISPLAY_BLOCK,
    DISPLAY_INLINE,
    DISPLAY_INLINE_BLOCK,
    DISPLAY_MATH_BLOCK,
    DISPLAY_MATH_INLINE,
    DISPLAY_TABLE,
    DISPLAY_TABLE_ROW,
    DISPLAY_TABLE_CELL,
    DISPLAY_LIST_ITEM,
    DISPLAY_NONE
} DisplayType;

// Text decoration
typedef struct TextDecoration {
    bool underline;
    bool overline;
    bool strikethrough;
    Color underline_color;
    Color overline_color;
    Color strikethrough_color;
    float line_thickness;
} TextDecoration;

// Text style structure
typedef struct TextStyle {
    Font* font;                 // Font to use
    Color color;                // Text color
    float font_size;            // Font size override
    uint32_t font_weight;       // Font weight override
    bool italic;                // Italic override
    TextDecoration* decoration; // Text decoration
    TextAlign alignment;        // Text alignment
    VerticalAlign vertical_align; // Vertical alignment
    float letter_spacing;       // Additional letter spacing
    float word_spacing;         // Additional word spacing
    float line_height;          // Line height multiplier
    
    // Text effects
    bool small_caps;            // Small capitals
    float text_indent;          // First line indent
    bool hyphenate;             // Allow hyphenation
    
    // Reference counting for shared styles
    int ref_count;
} TextStyle;

// Layout style structure
typedef struct LayoutStyle {
    // Margins
    float margin_top;
    float margin_bottom;
    float margin_left;
    float margin_right;
    
    // Padding
    float padding_top;
    float padding_bottom;
    float padding_left;
    float padding_right;
    
    // Dimensions
    float width;                // Fixed width (-1 for auto)
    float height;               // Fixed height (-1 for auto)
    float min_width;            // Minimum width
    float min_height;           // Minimum height
    float max_width;            // Maximum width
    float max_height;           // Maximum height
    
    // Display and positioning
    DisplayType display;        // Display type
    bool page_break_before;     // Force page break before
    bool page_break_after;      // Force page break after
    bool page_break_inside;     // Allow page break inside
    
    // Background and borders
    Color background_color;     // Background color
    Color border_color;         // Border color
    float border_width;         // Border width
    
    // List-specific
    int list_style_type;        // List bullet/number type
    float list_indent;          // List indentation
    
    // Table-specific
    float column_width;         // Table column width
    bool column_span;           // Column spanning
    
    // Reference counting
    int ref_count;
} LayoutStyle;

// Style creation and destruction
TextStyle* text_style_create(void);
void text_style_destroy(TextStyle* style);
TextStyle* text_style_copy(TextStyle* style);
TextStyle* text_style_ref(TextStyle* style);
void text_style_unref(TextStyle* style);

LayoutStyle* layout_style_create(void);
void layout_style_destroy(LayoutStyle* style);
LayoutStyle* layout_style_copy(LayoutStyle* style);
LayoutStyle* layout_style_ref(LayoutStyle* style);
void layout_style_unref(LayoutStyle* style);

// Default styles
TextStyle* create_default_text_style(FontManager* font_manager);
TextStyle* create_heading_text_style(FontManager* font_manager, int level);
TextStyle* create_code_text_style(FontManager* font_manager);
TextStyle* create_math_text_style(FontManager* font_manager);

LayoutStyle* create_default_layout_style(void);
LayoutStyle* create_block_layout_style(void);
LayoutStyle* create_inline_layout_style(void);
LayoutStyle* create_paragraph_layout_style(void);
LayoutStyle* create_heading_layout_style(int level);

// Style property setters
void text_style_set_font(TextStyle* style, Font* font);
void text_style_set_color(TextStyle* style, Color color);
void text_style_set_font_size(TextStyle* style, float size);
void text_style_set_font_weight(TextStyle* style, uint32_t weight);
void text_style_set_italic(TextStyle* style, bool italic);
void text_style_set_alignment(TextStyle* style, TextAlign alignment);
void text_style_set_line_height(TextStyle* style, float line_height);

void layout_style_set_margins(LayoutStyle* style, float top, float bottom, float left, float right);
void layout_style_set_padding(LayoutStyle* style, float top, float bottom, float left, float right);
void layout_style_set_dimensions(LayoutStyle* style, float width, float height);
void layout_style_set_display(LayoutStyle* style, DisplayType display);
void layout_style_set_background_color(LayoutStyle* style, Color color);

// Style merging and inheritance
TextStyle* text_style_merge(TextStyle* base, TextStyle* override);
LayoutStyle* layout_style_merge(LayoutStyle* base, LayoutStyle* override);
void text_style_inherit(TextStyle* child, TextStyle* parent);
void layout_style_inherit(LayoutStyle* child, LayoutStyle* parent);

// Color utilities
Color color_create_rgb(float r, float g, float b);
Color color_create_rgba(float r, float g, float b, float a);
Color color_create_from_hex(const char* hex);  // "#FF0000", "#F00", etc.
Color color_create_from_name(const char* name); // "red", "blue", etc.
bool color_equals(Color a, Color b);
char* color_to_hex(Color color);
char* color_to_rgb_string(Color color);

// Text decoration utilities
TextDecoration* text_decoration_create(void);
void text_decoration_destroy(TextDecoration* decoration);
void text_decoration_set_underline(TextDecoration* decoration, bool enabled, Color color);
void text_decoration_set_overline(TextDecoration* decoration, bool enabled, Color color);
void text_decoration_set_strikethrough(TextDecoration* decoration, bool enabled, Color color);

// Style computation utilities
float compute_line_height(TextStyle* text_style);
float compute_total_width(LayoutStyle* layout_style, float content_width);
float compute_total_height(LayoutStyle* layout_style, float content_height);
float compute_available_width(LayoutStyle* layout_style, float container_width);
float compute_available_height(LayoutStyle* layout_style, float container_height);

// Style application helpers
void apply_text_style_to_font(TextStyle* style, Font** font);
bool style_allows_page_break(LayoutStyle* style);
bool style_requires_block_layout(LayoutStyle* style);
bool style_is_inline(LayoutStyle* style);

// CSS-like property parsing (for future use)
bool parse_color_property(const char* value, Color* color);
bool parse_font_size_property(const char* value, float* size);
bool parse_margin_property(const char* value, float margins[4]);
bool parse_padding_property(const char* value, float padding[4]);

// Style debugging and inspection
void text_style_print_debug(TextStyle* style);
void layout_style_print_debug(LayoutStyle* style);
char* text_style_to_string(TextStyle* style);
char* layout_style_to_string(LayoutStyle* style);

#endif // STYLE_H
