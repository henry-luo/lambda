#pragma once
#include "view.hpp"

/**
 * RenderBackend — Abstract render dispatch interface.
 *
 * A vtable of function pointers that each output backend (SVG, PDF, …)
 * implements. The shared tree walker (render_walk.cpp) traverses the
 * View tree once and dispatches drawing through these callbacks.
 *
 * The raster backend (render.cpp / ThorVG) is NOT wired through this
 * interface because it has HiDPI scaling, pixel-level post-processing,
 * scrollbar rendering, and other complexities that do not map cleanly
 * to this abstraction. It may be migrated in a future phase.
 *
 * All coordinates are in CSS logical pixels. Each callback receives
 * the backend's own opaque context pointer (ctx).
 */

typedef struct RenderBackend RenderBackend;

struct RenderBackend {
    void* ctx;   // backend-specific context (SvgRenderContext*, PdfRenderContext*, etc.)

    // ── Boundary rendering (background, borders, shadow, outline) ──────
    // Called for every block/inline element that has a BoundaryProp.
    // abs_x/abs_y = absolute CSS-px position of the element on the page.
    void (*render_bound)(void* ctx, ViewBlock* view, float abs_x, float abs_y);

    // ── Text rendering ─────────────────────────────────────────────────
    // Called for every ViewText node.
    // abs_x/abs_y = absolute position of the text's containing block.
    void (*render_text)(void* ctx, ViewText* text, float abs_x, float abs_y,
                        FontBox* font, Color color);

    // ── Image rendering ────────────────────────────────────────────────
    // Called for blocks with embed->img.
    void (*render_image)(void* ctx, ViewBlock* block, float abs_x, float abs_y);

    // ── Inline SVG passthrough ─────────────────────────────────────────
    // Called for HTM_TAG_SVG blocks. If NULL, skipped.
    void (*render_inline_svg)(void* ctx, ViewBlock* block, float abs_x, float abs_y);

    // ── Children group wrappers ────────────────────────────────────────
    // Emits container markup around a block's children (e.g. <g class="block"> in SVG).
    // begin returns an opaque cookie; end receives it for matched close.
    void (*begin_block_children)(void* ctx, ViewBlock* block);
    void (*end_block_children)(void* ctx, ViewBlock* block);
    void (*begin_inline_children)(void* ctx, ViewSpan* span);
    void (*end_inline_children)(void* ctx, ViewSpan* span);

    // ── Opacity wrapper ────────────────────────────────────────────────
    // Called around a block's content when opacity < 1.
    void (*begin_opacity)(void* ctx, float opacity);
    void (*end_opacity)(void* ctx);

    // ── Transform wrapper ──────────────────────────────────────────────
    // Called around a block's content when CSS transforms are present.
    void (*begin_transform)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*end_transform)(void* ctx);

    // ── Column rules ───────────────────────────────────────────────────
    void (*render_column_rules)(void* ctx, ViewBlock* block, float abs_x, float abs_y);

    // ── Font setup ─────────────────────────────────────────────────────
    // Called when a view node specifies a font. Backend can update its
    // font state (e.g. PDF needs to call HPDF_Page_SetFontAndSize).
    void (*on_font_change)(void* ctx, FontProp* font_prop);
};

// Shared tree-walk state (managed by render_walk.cpp, passed to callbacks via ctx).
typedef struct {
    float x, y;           // accumulated absolute position (CSS logical px)
    FontBox font;          // inherited font
    Color color;           // inherited color
    UiContext* ui_context;
} RenderWalkState;

// ── Shared tree walker API ────────────────────────────────────────────
// Traverses the View tree and dispatches through `backend` callbacks.
void render_walk_block(RenderBackend* backend, RenderWalkState* state, ViewBlock* block);
void render_walk_inline(RenderBackend* backend, RenderWalkState* state, ViewSpan* span);
void render_walk_children(RenderBackend* backend, RenderWalkState* state, View* first_child);
