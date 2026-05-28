#pragma once
/**
 * render_svg_inline.hpp - Inline SVG Rendering via PaintIR/DisplayList
 *
 * Renders SVG elements embedded in HTML documents by converting SVG
 * element trees into the shared painter recording path.
 *
 * Key functions:
 * - render_svg_build_subscene(): Capture an SVG Element tree for export lowering
 * - render_inline_svg(): Render SVG block in document context
 */

#include "view.hpp"
#include "rdt_vector.hpp"
#include "display_list.h"
#include "paint_ir.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/hashmap.h"

struct FontContext;  // forward declaration from lib/font/font.h
typedef struct RenderContext RenderContext;
typedef const char* (*SvgImageResolverFn)(void* context, int image_id);

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

struct SvgInlineRenderContext {
    Element* svg_root;           // root <svg> element
    Pool* pool;                  // memory pool
    FontContext* font_ctx;       // font context for font resolution (may be nullptr)
    DisplayList* dl;             // required display list target for deferred rendering
    PaintList* paint_list;       // required PaintIR gateway used before lowering to dl
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

void render_svg_build_subscene(PaintSvgSubscene* subscene,
                      Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio,
                      FontContext* font_ctx,
                      const RdtMatrix* base_transform,
                      const Bound* content_clip,
                      const Color* initial_current_color,
                      const Color* initial_fill_color,
                      const char* source_path,
                      float initial_opacity,
                      bool initial_fill_none,
                      const Color* initial_stroke_color,
                      bool initial_stroke_none,
                      float initial_stroke_width);

void render_svg_inline_register_paint_ir_lowerers(void);

/**
 * Render an SVG element tree through DisplayList record/replay into an existing
 * RdtVector target. This is used for offscreen SVG pictures so they follow the
 * same replay path as raster output instead of immediate rdt_* emission.
 */
void render_svg_to_vec_via_display_list(RdtVector* vec, Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio = 1.0f,
                      FontContext* font_ctx = nullptr,
                      const RdtMatrix* base_transform = nullptr,
                      const Color* initial_current_color = nullptr,
                      const Color* initial_fill_color = nullptr,
                      const char* source_path = nullptr,
                      float initial_opacity = 1.0f,
                      bool initial_fill_none = false,
                      const Color* initial_stroke_color = nullptr,
                      bool initial_stroke_none = true,
                      float initial_stroke_width = -1.0f);

/**
 * Render inline SVG element in document context
 * Called by raster and vector render walkers when element is HTM_TAG_SVG.
 *
 * @param rdcon Render context with canvas, scale, clip, etc.
 * @param view ViewBlock for the SVG element
 */
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);
