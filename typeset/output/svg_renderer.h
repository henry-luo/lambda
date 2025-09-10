#ifndef SVG_RENDERER_H
#define SVG_RENDERER_H

#include "renderer.h"
#include "../view/view_tree.h"

// SVG-specific renderer structure
typedef struct SVGRenderer {
    ViewRenderer base;          // Base renderer
    
    // SVG-specific state
    StrBuf* svg_content;        // SVG content buffer
    double viewport_width;      // Viewport width
    double viewport_height;     // Viewport height
    int element_id_counter;     // For generating unique IDs
    
    // Font handling
    bool embed_fonts;           // Embed font data
    struct ViewFontRegistry* fonts; // Font registry
    
    // Optimization
    bool optimize_paths;        // Optimize SVG paths
    int decimal_precision;      // Decimal precision
} SVGRenderer;

// SVG renderer creation and basic API
SVGRenderer* svg_renderer_create(void);
bool svg_render_view_tree(SVGRenderer* renderer, ViewTree* tree, StrBuf* output);

// SVG render options (extends base ViewRenderOptions)
typedef struct SVGRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // SVG-specific options
    bool embed_fonts;           // Embed font data in SVG
    bool optimize_paths;        // Optimize SVG paths
    int decimal_precision;      // Decimal precision (2 default)
    bool use_viewbox;           // Use viewBox attribute
    
    // Text rendering
    bool convert_text_to_paths; // Convert text to paths
    bool use_css_fonts;         // Use CSS font declarations
} SVGRenderOptions;

SVGRenderOptions* svg_render_options_create_default(void);

// SVG renderer implementation functions
bool svg_renderer_initialize(ViewRenderer* renderer, ViewRenderOptions* options);
bool svg_renderer_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output);
bool svg_renderer_render_node(ViewRenderer* renderer, ViewNode* node);

// SVG math rendering functions
void svg_render_math_element(SVGRenderer* renderer, ViewNode* math_node);
void svg_render_math_fraction(SVGRenderer* renderer, ViewNode* fraction_node);
void svg_render_math_script(SVGRenderer* renderer, ViewNode* script_node);
void svg_render_math_radical(SVGRenderer* renderer, ViewNode* radical_node);
void svg_render_math_matrix(SVGRenderer* renderer, ViewNode* matrix_node);
void svg_render_math_delimiter(SVGRenderer* renderer, ViewNode* delimiter_node);
void svg_render_math_atom(SVGRenderer* renderer, ViewNode* atom_node);
void svg_render_math_operator(SVGRenderer* renderer, ViewNode* operator_node);
void svg_render_math_function(SVGRenderer* renderer, ViewNode* function_node);

// SVG math-specific utilities
void svg_render_fraction_line(SVGRenderer* renderer, double x, double y, double width, double thickness);
void svg_render_radical_symbol(SVGRenderer* renderer, double x, double y, double width, double height);
void svg_render_matrix_delimiters(SVGRenderer* renderer, ViewRect bounds, const char* style);
void svg_position_scripts(SVGRenderer* renderer, ViewNode* base, ViewNode* super, ViewNode* sub);
void svg_render_math_spacing(SVGRenderer* renderer, double amount);

// SVG math symbol rendering
void svg_render_math_symbol(SVGRenderer* renderer, const char* symbol, const char* unicode, 
                           double x, double y, double font_size);
void svg_render_large_operator(SVGRenderer* renderer, const char* operator_name, 
                              double x, double y, double font_size);
void svg_render_accent(SVGRenderer* renderer, const char* accent_type, 
                      double x, double y, double width);
void svg_renderer_finalize(ViewRenderer* renderer);
void svg_renderer_cleanup(ViewRenderer* renderer);

// SVG-specific rendering functions
void svg_write_header(SVGRenderer* renderer, ViewTree* tree);
void svg_write_footer(SVGRenderer* renderer);
void svg_start_group(SVGRenderer* renderer, ViewNode* node);
void svg_end_group(SVGRenderer* renderer);
void svg_render_text_run(SVGRenderer* renderer, ViewNode* node);
void svg_render_rectangle(SVGRenderer* renderer, ViewNode* node);
void svg_render_line(SVGRenderer* renderer, ViewNode* node);
void svg_render_math_element(SVGRenderer* renderer, ViewNode* node);
void svg_escape_text(SVGRenderer* renderer, const char* text);

// Additional SVG render options for simple testing
typedef struct {
    double width;
    double height;
    double margin_left;
    double margin_top;
    double margin_right;
    double margin_bottom;
    const char* background_color;
} SVGRenderOptions;

// Public API functions
StrBuf* render_view_tree_to_svg(ViewTree* tree, SVGRenderOptions* options);

// Internal function for testing
StrBuf* render_view_tree_to_svg_internal(ViewTree* tree, SVGRenderOptions* options);

// Simple wrapper for testing
StrBuf* render_view_tree_to_svg_simple(SVGRenderer* renderer, ViewTree* tree);

#endif // SVG_RENDERER_H
