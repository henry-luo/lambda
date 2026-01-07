// math_integration.cpp - Integration of math layout with Radiant
//
// Connects the math layout engine with Radiant's view tree
// and rendering pipeline.

#include "math_integration.hpp"
#include "math_box.hpp"
#include "math_context.hpp"
#include "layout_math.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/math_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/arena.h"
#include <cstring>

namespace radiant {

// Global arena for math layout (reset between documents)
static Arena* g_math_arena = nullptr;

// ============================================================================
// Initialization
// ============================================================================

void math_init(Pool* pool) {
    if (!g_math_arena && pool) {
        g_math_arena = arena_create_default(pool);
        log_debug("math_init: initialized math arena");
    }
}

void math_cleanup() {
    if (g_math_arena) {
        arena_destroy(g_math_arena);
        g_math_arena = nullptr;
        log_debug("math_cleanup: destroyed math arena");
    }
}

// ============================================================================
// ViewMath Creation
// ============================================================================

/**
 * Set up math rendering on a DOM element.
 * This stores math data in the element's embed prop and marks it as a math view.
 */
bool setup_math_element(LayoutContext* lycon, DomElement* elem, Item math_node, bool is_display) {
    if (!elem || math_node.item == ItemNull.item) {
        return false;
    }

    // Get pool from the document's view tree
    Pool* pool = lycon->doc ? lycon->doc->view_tree->pool : nullptr;
    if (!pool) pool = lycon->pool;

    // Ensure math arena is initialized
    if (!g_math_arena && pool) {
        math_init(pool);
    }

    if (!g_math_arena) {
        log_error("setup_math_element: no math arena available (pool=%p)", (void*)pool);
        return false;
    }

    // Ensure embed prop is allocated
    if (!elem->embed) {
        elem->embed = (EmbedProp*)pool_calloc(pool, sizeof(EmbedProp));
        if (!elem->embed) {
            log_error("setup_math_element: failed to allocate EmbedProp");
            return false;
        }
    }

    // Mark element as math view
    elem->view_type = RDT_VIEW_MATH;

    // Store math-specific data in embed
    elem->embed->math_node = math_node;
    elem->embed->math_is_display = is_display;
    elem->embed->math_baseline_offset = 0;
    elem->embed->math_arena = g_math_arena;
    elem->embed->math_box = nullptr;

    // Get font size from current context
    float font_size = lycon->font.style ? lycon->font.style->font_size : 16.0f;

    // Create math context for layout
    MathContext ctx(lycon->ui_context, lycon->pool, font_size, is_display);

    // Layout the math expression
    elem->embed->math_box = layout_math(math_node, ctx, g_math_arena);

    // Apply inter-box spacing
    if (elem->embed->math_box) {
        apply_inter_box_spacing(elem->embed->math_box, ctx, g_math_arena);
    }

    // Set dimensions on element view
    if (elem->embed->math_box) {
        elem->width = elem->embed->math_box->width;
        elem->height = elem->embed->math_box->height + elem->embed->math_box->depth;
        elem->embed->math_baseline_offset = elem->embed->math_box->height;
    } else {
        elem->width = 0;
        elem->height = 0;
    }

    log_debug("setup_math_element: set up math %.1fx%.1f display=%d",
              elem->width, elem->height, is_display);

    return true;
}

// Legacy function for compatibility - creates separate ViewMath allocation
ViewMath* create_math_view(LayoutContext* lycon, Item math_node, bool is_display) {
    if (math_node.item == ItemNull.item) {
        return nullptr;
    }

    // Get pool from the document's view tree
    Pool* pool = lycon->doc ? lycon->doc->view_tree->pool : nullptr;
    if (!pool) {
        // Fallback to lycon->pool
        pool = lycon->pool;
    }

    // Ensure math arena is initialized
    if (!g_math_arena && pool) {
        math_init(pool);
    }

    if (!g_math_arena) {
        log_error("create_math_view: no math arena available (pool=%p)", (void*)pool);
        return nullptr;
    }

    // Allocate ViewMath from pool
    ViewMath* view = (ViewMath*)pool_calloc(pool, sizeof(ViewMath));
    if (!view) {
        log_error("create_math_view: failed to allocate ViewMath");
        return nullptr;
    }

    // Initialize as a ViewSpan derivative
    view->view_type = RDT_VIEW_MATH;

    // Store math-specific data
    view->math_node = math_node;
    view->is_display = is_display;
    view->baseline_offset = 0;
    view->math_arena = g_math_arena;
    view->math_box = nullptr;

    // Get font size from current context
    float font_size = lycon->font.style ? lycon->font.style->font_size : 16.0f;

    // Create math context for layout
    MathContext ctx(lycon->ui_context, lycon->pool, font_size, is_display);

    // Layout the math expression
    view->math_box = layout_math(math_node, ctx, g_math_arena);

    // Apply inter-box spacing
    if (view->math_box) {
        apply_inter_box_spacing(view->math_box, ctx, g_math_arena);
    }

    // Set dimensions on view
    if (view->math_box) {
        view->width = view->math_box->width;
        view->height = view->math_box->height + view->math_box->depth;
        view->baseline_offset = view->math_box->height;
    } else {
        view->width = 0;
        view->height = 0;
    }

    log_debug("create_math_view: created math view %.1fx%.1f display=%d",
              view->width, view->height, is_display);

    return view;
}

// ============================================================================
// Math Layout within Block Context
// ============================================================================

void layout_math_element(LayoutContext* lycon, ViewMath* math_view) {
    if (!math_view) return;

    // Position depends on display mode
    if (math_view->is_display) {
        // Display math: center on its own line
        float available_width = lycon->block.content_width;
        float x_offset = (available_width - math_view->width) / 2;
        if (x_offset < 0) x_offset = 0;

        // Start new line if needed
        if (lycon->line.advance_x > lycon->line.effective_left) {
            line_break(lycon);
        }

        math_view->x = x_offset;
        math_view->y = lycon->block.advance_y;

        // Advance past the math element
        lycon->block.advance_y += math_view->height;

        // Add some vertical margin
        float margin = math_view->height * 0.3f;
        lycon->block.advance_y += margin;

    } else {
        // Inline math: treat like inline content

        // Check if it fits on current line
        if (lycon->line.advance_x + math_view->width > lycon->line.effective_right) {
            line_break(lycon);
        }

        math_view->x = lycon->line.advance_x;

        // Vertical alignment: center on x-height
        float x_height = lycon->font.style ? lycon->font.style->font_size * 0.43f : 8.0f;
        float axis = x_height / 2;  // math axis

        // Position so math axis aligns with text baseline + axis height
        math_view->y = lycon->block.advance_y - axis + math_view->baseline_offset;

        // Update line box metrics
        float above_baseline = math_view->math_box ? math_view->math_box->height : math_view->height;
        float below_baseline = math_view->math_box ? math_view->math_box->depth : 0;

        if (above_baseline > lycon->line.max_ascender) {
            lycon->line.max_ascender = above_baseline;
        }
        if (below_baseline > lycon->line.max_descender) {
            lycon->line.max_descender = below_baseline;
        }

        // Advance horizontal position
        lycon->line.advance_x += math_view->width;
    }
}

// ============================================================================
// Dimension Queries
// ============================================================================

float get_math_width(ViewMath* math_view) {
    if (!math_view || !math_view->math_box) return 0;
    return math_view->math_box->width;
}

float get_math_height(ViewMath* math_view) {
    if (!math_view || !math_view->math_box) return 0;
    return math_view->math_box->height;
}

float get_math_depth(ViewMath* math_view) {
    if (!math_view || !math_view->math_box) return 0;
    return math_view->math_box->depth;
}

// ============================================================================
// Math Element Detection
// ============================================================================

bool is_math_element(DomNode* node) {
    if (!node) return false;

    // Check if element
    if (node->is_element()) {
        DomElement* elem = static_cast<DomElement*>(node);
        // MathML elements
        if (elem->tag() == HTM_TAG_MATH) return true;

        // Check for class="math" or similar
        if (dom_element_has_class(elem, "math")) return true;
    }

    return false;
}

// ============================================================================
// Reset Math Arena (call between documents)
// ============================================================================

void math_reset_arena() {
    if (g_math_arena) {
        arena_reset(g_math_arena);
        log_debug("math_reset_arena: reset math arena");
    }
}

} // namespace radiant
