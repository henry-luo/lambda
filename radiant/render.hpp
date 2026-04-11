#pragma once
#include "view.hpp"
#include "state_store.hpp"
#include "rdt_vector.hpp"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

typedef struct {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color;
    RdtVector vec;      // platform-agnostic vector renderer

    UiContext* ui_context;

    // Transform state
    RdtMatrix transform;           // Current combined transform matrix
    bool has_transform;            // True if non-identity transform is active

    // HiDPI scaling: CSS logical pixels -> physical surface pixels
    float scale;                   // pixel_ratio (1.0 for standard, 2.0 for Retina, etc.)
    
    // Selection state for text rendering
    SelectionState* selection;     // Current selection (if any)

    // Phase 18: Dirty-region tracking for render tree clipping
    DirtyTracker* dirty_tracker;   // NULL = full repaint (no clipping)
} RenderContext;

// Function declarations
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);

// UI overlay rendering (focus, caret, selection)
void render_focus_outline(RenderContext* rdcon, RadiantState* state);
void render_caret(RenderContext* rdcon, RadiantState* state);
void render_selection(RenderContext* rdcon, RadiantState* state);
void render_ui_overlays(RenderContext* rdcon, RadiantState* state);
