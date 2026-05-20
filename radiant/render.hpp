#pragma once
#include "view.hpp"
#include "state_store.hpp"
#include "rdt_vector.hpp"
#include "clip_shape.h"
#include "display_list.h"
#include "../lib/scratch_arena.h"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

struct RenderProfiler;
typedef struct RenderProfiler RenderProfiler;

typedef struct RenderContext {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color;
    RdtVector vec;      // platform-agnostic vector renderer

    UiContext* ui_context;

    // Display list for deferred rendering (Phase 1)
    // When non-NULL, render functions record to dl instead of drawing directly.
    DisplayList* dl;
    RetainedDisplayListCache* retained_dl_cache;

    // Transform state
    RdtMatrix transform;           // Current combined transform matrix
    bool has_transform;            // True if non-identity transform is active

    // HiDPI scaling: CSS logical pixels -> physical surface pixels
    float scale;                   // pixel_ratio (1.0 for standard, 2.0 for Retina, etc.)
    
    // Phase 18: Dirty-region tracking for render tree clipping
    DirtyTracker* dirty_tracker;   // NULL = full repaint (no clipping)
    Bound dirty_union;             // union bbox of all dirty rects (CSS pixels, valid when dirty_tracker != NULL)
    bool has_dirty_union;          // true when dirty_union is valid

    // LIFO scratch allocator for scoped temporary buffers (pixel buffers, clip masks, etc.)
    ScratchArena scratch;

    // Per-render profiling counters and timers.
    RenderProfiler* profiler;

    // Vector clip shape stack for overflow:hidden with border-radius and CSS clip-path
    ClipShape* clip_shapes[RDT_MAX_CLIP_SHAPES];
    int clip_shape_depth;

    // Suppresses automatic per-block display-list markers while a caller records
    // a wider element subtree marker around replaced/layer content.
    int element_marker_suppression_depth;
} RenderContext;

// Function declarations
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);

typedef struct RenderElementMarkerScope {
    int begin_index;
} RenderElementMarkerScope;

bool render_block_dirty_misses(RenderContext* rdcon, ViewBlock* block);
bool render_block_viewport_misses(RenderContext* rdcon, ViewBlock* block);
bool render_block_try_retained_fragment(RenderContext* rdcon, ViewBlock* block);
void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_embed_doc(RenderContext* rdcon, ViewBlock* block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_bound(RenderContext* rdcon, ViewBlock* view);
void render_outline_deferred(RenderContext* rdcon, ViewBlock* view);
void render_children(RenderContext* rdcon, View* view);
void render_raster_positioned_children(RenderContext* rdcon, ViewBlock* block);
void render_raster_view_tree(RenderContext* rdcon, ViewTree* view_tree);
RenderElementMarkerScope render_element_marker_begin(RenderContext* rdcon, ViewBlock* block);
void render_element_marker_end(RenderContext* rdcon, RenderElementMarkerScope* scope);

// Shut down the render pool (must be called before rdt_engine_term)
void render_pool_shutdown();

// Tile-based PNG rendering for large pages that would OOM with a single surface.
// Only used for PNG output.  total_width/total_height are in physical pixels.
void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree, const char* output_file,
                           int total_width, int total_height);

#include "render_overlay.hpp"
#include "render_painter.hpp"
