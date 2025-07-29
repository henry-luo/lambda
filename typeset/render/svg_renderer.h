#ifndef SVG_RENDERER_H
#define SVG_RENDERER_H

#include "../typeset.h"

// SVG renderer structure
struct SVGRenderer {
    StrBuf* svg_content;        // SVG content buffer
    float width;                // Canvas width
    float height;               // Canvas height
    int current_page;           // Current page number
    FontManager* font_manager;  // Font manager reference
    
    // Current rendering state
    Font* current_font;         // Current font
    Color current_color;        // Current text color
    Color current_fill;         // Current fill color
    Color current_stroke;       // Current stroke color
    float current_stroke_width; // Current stroke width
    
    // Transform stack
    float* transform_stack;     // Matrix transform stack
    int transform_depth;        // Current transform depth
    int max_transform_depth;    // Maximum transform depth
    
    // SVG element ID counter
    int element_id_counter;     // For generating unique IDs
    
    // Optimization flags
    bool optimize_output;       // Enable SVG optimization
    bool embed_fonts;          // Embed font data
    bool use_css_styles;       // Use CSS for styling
    
    // Debug options
    bool show_debug_boxes;     // Show bounding boxes
    bool show_baselines;       // Show text baselines
    bool show_margins;         // Show margin guides
};

// SVG path structure for complex graphics
struct SVGPath {
    StrBuf* path_data;         // SVG path data
    Color fill_color;          // Fill color
    Color stroke_color;        // Stroke color
    float stroke_width;        // Stroke width
    bool is_closed;           // Path is closed
    
    // Path optimization
    bool use_relative_coords;  // Use relative coordinates
    int precision;            // Decimal precision
};

// SVG group structure for organizing elements
typedef struct SVGGroup {
    char* id;                 // Group ID
    char* class_name;         // CSS class name
    float transform[6];       // Transform matrix
    bool has_transform;       // Has custom transform
    Color fill_color;        // Group fill color
    Color stroke_color;      // Group stroke color
    float opacity;           // Group opacity
} SVGGroup;

// Core SVG renderer functions
SVGRenderer* svg_renderer_create(float width, float height);
void svg_renderer_destroy(SVGRenderer* renderer);
void svg_renderer_reset(SVGRenderer* renderer);
String* svg_renderer_finalize(SVGRenderer* renderer);

// Document and page rendering
void svg_render_document_page(SVGRenderer* renderer, Document* doc, int page_num);
void svg_render_page(SVGRenderer* renderer, Page* page);
void svg_render_box_tree(SVGRenderer* renderer, Box* root);
void svg_render_box(SVGRenderer* renderer, Box* box);

// Text rendering
void svg_render_text(SVGRenderer* renderer, const char* text, float x, float y, Font* font, Color color);
void svg_render_text_with_style(SVGRenderer* renderer, const char* text, float x, float y, TextStyle* style);
void svg_render_text_run(SVGRenderer* renderer, const char* text, int start, int length, float x, float y, Font* font);

// Mathematical expression rendering
void svg_render_math_box(SVGRenderer* renderer, MathBox* math_box);
void svg_render_math_expression(SVGRenderer* renderer, MathBox* root, float x, float y);
void svg_render_fraction(SVGRenderer* renderer, MathBox* fraction);
void svg_render_radical(SVGRenderer* renderer, MathBox* radical);
void svg_render_script(SVGRenderer* renderer, MathBox* script);
void svg_render_matrix(SVGRenderer* renderer, MathBox* matrix);
void svg_render_delimiter(SVGRenderer* renderer, MathBox* delimiter);

// Geometric primitives
void svg_render_line(SVGRenderer* renderer, float x1, float y1, float x2, float y2, Color color, float width);
void svg_render_rectangle(SVGRenderer* renderer, float x, float y, float width, float height, Color fill, Color stroke, float stroke_width);
void svg_render_circle(SVGRenderer* renderer, float cx, float cy, float r, Color fill, Color stroke, float stroke_width);
void svg_render_ellipse(SVGRenderer* renderer, float cx, float cy, float rx, float ry, Color fill, Color stroke, float stroke_width);
void svg_render_path(SVGRenderer* renderer, SVGPath* path);

// Advanced graphics
void svg_render_bezier_curve(SVGRenderer* renderer, float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2, Color stroke, float width);
void svg_render_arc(SVGRenderer* renderer, float x, float y, float rx, float ry, float start_angle, float end_angle, Color stroke, float width);
void svg_render_polygon(SVGRenderer* renderer, float* points, int point_count, Color fill, Color stroke, float stroke_width);

// Math-specific graphics
void svg_render_fraction_rule(SVGRenderer* renderer, float x, float y, float width, float thickness, Color color);
void svg_render_radical_sign(SVGRenderer* renderer, float x, float y, float width, float height, Color color, float thickness);
void svg_render_accent(SVGRenderer* renderer, float x, float y, uint32_t accent_char, float width, Font* font, Color color);
void svg_render_stretchy_delimiter(SVGRenderer* renderer, float x, float y, float height, uint32_t delimiter_char, Font* font, Color color);
void svg_render_matrix_delimiters(SVGRenderer* renderer, float x, float y, float width, float height, uint32_t left_delim, uint32_t right_delim, Font* font, Color color);

// Transform and coordinate system
void svg_push_transform(SVGRenderer* renderer);
void svg_pop_transform(SVGRenderer* renderer);
void svg_translate(SVGRenderer* renderer, float dx, float dy);
void svg_scale(SVGRenderer* renderer, float sx, float sy);
void svg_rotate(SVGRenderer* renderer, float angle);
void svg_set_transform(SVGRenderer* renderer, float a, float b, float c, float d, float e, float f);

// Style management
void svg_set_font(SVGRenderer* renderer, Font* font);
void svg_set_color(SVGRenderer* renderer, Color color);
void svg_set_fill_color(SVGRenderer* renderer, Color color);
void svg_set_stroke_color(SVGRenderer* renderer, Color color);
void svg_set_stroke_width(SVGRenderer* renderer, float width);
void svg_set_opacity(SVGRenderer* renderer, float opacity);

// Groups and layers
void svg_begin_group(SVGRenderer* renderer, SVGGroup* group);
void svg_end_group(SVGRenderer* renderer);
SVGGroup* svg_group_create(const char* id, const char* class_name);
void svg_group_destroy(SVGGroup* group);

// SVG path utilities
SVGPath* svg_path_create(void);
void svg_path_destroy(SVGPath* path);
void svg_path_move_to(SVGPath* path, float x, float y);
void svg_path_line_to(SVGPath* path, float x, float y);
void svg_path_curve_to(SVGPath* path, float cx1, float cy1, float cx2, float cy2, float x, float y);
void svg_path_arc_to(SVGPath* path, float rx, float ry, float rotation, bool large_arc, bool sweep, float x, float y);
void svg_path_close(SVGPath* path);
void svg_path_reset(SVGPath* path);

// SVG optimization
void svg_optimize_path(SVGPath* path);
void svg_optimize_renderer_output(SVGRenderer* renderer);
void svg_minimize_precision(SVGRenderer* renderer, int decimal_places);
void svg_remove_redundant_attributes(SVGRenderer* renderer);

// Coordinate utilities
void svg_transform_point(SVGRenderer* renderer, float* x, float* y);
void svg_get_current_transform(SVGRenderer* renderer, float transform[6]);
void svg_point_to_user_space(SVGRenderer* renderer, float* x, float* y);

// Font handling in SVG
void svg_embed_font_data(SVGRenderer* renderer, Font* font);
char* svg_get_font_family_name(Font* font);
char* svg_get_font_style_attributes(Font* font);
void svg_render_font_definitions(SVGRenderer* renderer);

// Text measurement for SVG
float svg_measure_text_width(SVGRenderer* renderer, const char* text, Font* font);
void svg_get_text_bounds(SVGRenderer* renderer, const char* text, Font* font, float* width, float* height, float* baseline);

// SVG utilities
char* svg_color_to_string(Color color);
char* svg_format_number(float value, int precision);
char* svg_escape_text(const char* text);
void svg_add_comment(SVGRenderer* renderer, const char* comment);
void svg_add_title(SVGRenderer* renderer, const char* title);
void svg_add_description(SVGRenderer* renderer, const char* description);

// SVG metadata
void svg_set_title(SVGRenderer* renderer, const char* title);
void svg_set_description(SVGRenderer* renderer, const char* description);
void svg_add_metadata(SVGRenderer* renderer, const char* name, const char* content);
void svg_set_viewbox(SVGRenderer* renderer, float x, float y, float width, float height);

// Debug rendering
void svg_render_debug_box(SVGRenderer* renderer, Box* box, Color color);
void svg_render_debug_baseline(SVGRenderer* renderer, float x, float y, float width, Color color);
void svg_render_debug_margins(SVGRenderer* renderer, Box* box, Color color);
void svg_render_debug_grid(SVGRenderer* renderer, float spacing, Color color);

// Error handling
typedef enum {
    SVG_RENDER_SUCCESS,
    SVG_RENDER_ERROR_INVALID_PARAMS,
    SVG_RENDER_ERROR_MEMORY,
    SVG_RENDER_ERROR_FONT_MISSING,
    SVG_RENDER_ERROR_TRANSFORM_STACK_OVERFLOW,
    SVG_RENDER_ERROR_INVALID_PATH,
    SVG_RENDER_ERROR_IO
} SVGRenderResult;

SVGRenderResult svg_get_last_error(SVGRenderer* renderer);
const char* svg_get_error_message(SVGRenderResult error);

// Performance monitoring
typedef struct SVGRenderStats {
    int elements_rendered;
    int text_runs_rendered;
    int paths_rendered;
    int transforms_applied;
    float total_render_time;
    size_t svg_size_bytes;
} SVGRenderStats;

void svg_get_render_stats(SVGRenderer* renderer, SVGRenderStats* stats);
void svg_reset_render_stats(SVGRenderer* renderer);

// SVG templates and themes
typedef struct SVGTheme {
    Color background_color;
    Color text_color;
    Color heading_color;
    Color math_color;
    Color accent_color;
    Font* default_font;
    Font* heading_font;
    Font* math_font;
    float line_thickness;
} SVGTheme;

void svg_apply_theme(SVGRenderer* renderer, SVGTheme* theme);
SVGTheme* svg_create_default_theme(FontManager* font_manager);
SVGTheme* svg_create_dark_theme(FontManager* font_manager);
void svg_theme_destroy(SVGTheme* theme);

// Advanced features
void svg_render_gradient(SVGRenderer* renderer, const char* id, Color start_color, Color end_color, float x1, float y1, float x2, float y2);
void svg_render_pattern(SVGRenderer* renderer, const char* id, float width, float height, const char* pattern_content);
void svg_render_filter(SVGRenderer* renderer, const char* id, const char* filter_definition);
void svg_apply_filter(SVGRenderer* renderer, const char* filter_id);

#endif // SVG_RENDERER_H
