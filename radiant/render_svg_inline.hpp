#pragma once
/**
 * render_svg_inline.hpp - Inline SVG Rendering via RdtVector
 *
 * Renders SVG elements embedded in HTML documents by converting SVG
 * element trees directly to RdtVector draw calls.
 *
 * Key functions:
 * - render_svg_to_vec(): Convert SVG Element tree to rdt_ draw calls
 * - render_inline_svg(): Render SVG block in document context
 */

#include "view.hpp"
#include "rdt_vector.hpp"
#include "display_list.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/hashmap.h"

struct FontContext;  // forward declaration from lib/font/font.h
typedef struct RenderContext RenderContext;

typedef const char* (*SvgImageResolverFn)(void* context, int image_id);

// ============================================================================
// SVG ViewBox Structure
// ============================================================================

struct SvgViewBox {
    float min_x, min_y;
    float width, height;
    bool has_viewbox;
};

// ============================================================================
// SVG Intrinsic Size
// ============================================================================

struct SvgIntrinsicSize {
    float width;
    float height;
    float aspect_ratio;          // width / height
    bool has_intrinsic_width;
    bool has_intrinsic_height;
};

// ============================================================================
// SVG Render Context (internal state during rendering)
// ============================================================================

struct SvgRenderContext {
    Element* svg_root;           // root <svg> element
    Pool* pool;                  // memory pool
    FontContext* font_ctx;       // font context for font resolution (may be nullptr)
    RdtVector* vec;              // target vector renderer for direct drawing
    DisplayList* dl;             // display list for deferred rendering (Phase 1, may be nullptr)
    const char* source_path;      // source SVG path for resolving nested resources
    SvgImageResolverFn image_resolver;  // optional resolver for document-owned image handles
    void* image_resolver_context;
    RdtMatrix transform;         // accumulated transform from root (viewBox × group × element)
    
    // pixel ratio for text sizing - text font sizes need to be divided by this
    // because the entire SVG scene is scaled by pixel_ratio after building
    float pixel_ratio;
    
    // viewBox transform state
    float viewbox_x, viewbox_y;
    float viewbox_width, viewbox_height;
    float scale_x, scale_y;      // viewport / viewBox ratio
    float translate_x, translate_y;
    
    // inherited style state
    Color fill_color;
    Color stroke_color;
    Color current_color;         // CSS 'color' property for currentColor keyword
    float stroke_width;
    float opacity;
    bool fill_none;
    bool stroke_none;

    // inherited text properties (used by <text>/<tspan> when not on element itself)
    const char* inherited_font_family;   // pointer into Element attribute string memory (lifetime of SVG element tree)
    float inherited_font_size;            // 0 means not set
    int inherited_font_weight;            // 0 means not set
    const char* inherited_text_anchor;

    // current viewport size in user-coordinate units (parent for nested <svg>).
    // Used to resolve omitted width/height on a nested <svg> element ("100%").
    float current_viewport_w;
    float current_viewport_h;
    
    // gradient/pattern definitions from <defs>
    HashMap* defs;               // id → SvgDefTable*

    // lightweight SVG-document stylesheet cache from embedded <style> nodes
    void* style_rules;           // SvgStyleRule* (private to render_svg_inline.cpp)
    int style_rule_count;
    int style_rule_capacity;
};

extern "C" void svg_register_pdf_image_resolver(Element* svg_root, Item pdf_root);
extern "C" void svg_unregister_image_resolvers_for_tree(Element* root);
extern "C" bool svg_get_registered_image_resolver(Element* svg_root,
                                                   SvgImageResolverFn* out_resolver,
                                                   void** out_context);

// ============================================================================
// Public API
// ============================================================================

/**
 * Parse SVG viewBox attribute
 * @param viewbox_attr The viewBox attribute string (e.g., "0 0 100 50")
 * @return Parsed viewBox structure
 */
SvgViewBox parse_svg_viewbox(const char* viewbox_attr);

/**
 * Calculate SVG intrinsic size from element attributes
 * Per CSS Images Level 3, SVG intrinsic size is determined by:
 * 1. Explicit width/height attributes
 * 2. viewBox ratio if only one dimension specified
 * 3. viewBox dimensions if no width/height specified
 * 4. Default 300×150 if nothing specified (per HTML spec)
 *
 * @param svg_element The <svg> Element
 * @return Intrinsic size with aspect ratio
 */
SvgIntrinsicSize calculate_svg_intrinsic_size(Element* svg_element);

/**
 * Render SVG element tree directly to an RdtVector using rdt_ draw calls.
 * No ThorVG scene tree is constructed — shapes are drawn immediately.
 *
 * @param vec Target vector renderer
 * @param svg_element The <svg> Element from HTML5 parser
 * @param viewport_width Target rendering width (CSS pixels)
 * @param viewport_height Target rendering height (CSS pixels)
 * @param pool Memory pool for allocations
 * @param pixel_ratio Device pixel ratio (for text size adjustment)
 * @param font_ctx Font context for SVG text rendering (may be nullptr)
 * @param base_transform Optional transform to apply to the entire SVG
 */
void render_svg_to_vec(RdtVector* vec, Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio = 1.0f,
                      FontContext* font_ctx = nullptr,
                      const RdtMatrix* base_transform = nullptr,
                      DisplayList* dl = nullptr,
                      const Color* initial_current_color = nullptr,
                      const Color* initial_fill_color = nullptr,
                      const char* source_path = nullptr,
                      float initial_opacity = 1.0f);

/**
 * Render inline SVG element in document context
 * Called by raster and vector render walkers when element is HTM_TAG_SVG.
 *
 * @param rdcon Render context with canvas, scale, clip, etc.
 * @param view ViewBlock for the SVG element
 */
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);

// ============================================================================
// SVG Parsing Utilities
// ============================================================================

/**
 * Parse SVG length value (number with optional unit)
 * Supports: px, em, ex, pt, pc, cm, mm, in, % (percentage returned as-is)
 *
 * @param value Length string (e.g., "100", "50px", "10%")
 * @param default_value Value to return if parsing fails
 * @return Parsed length in user units (pixels for most cases)
 */
float parse_svg_length(const char* value, float default_value);

/**
 * Parse SVG color value
 * Supports: named colors, #rgb, #rrggbb, #rgba, #rrggbbaa, rgb(), rgba()
 *
 * @param value Color string
 * @return Parsed color (black with alpha=255 if parsing fails)
 */
Color parse_svg_color(const char* value);

/**
 * Parse SVG transform attribute
 * Supports: translate, scale, rotate, skewX, skewY, matrix
 *
 * @param transform_str Transform attribute string
 * @param matrix Output 3x3 matrix (row-major)
 * @return true if parsing succeeded
 */
bool parse_svg_transform(const char* transform_str, float matrix[6]);
