#pragma once

// ============================================================================
// Layout Safety Guards — centralized limits preventing pathological inputs
// from causing stack overflow, CPU timeout, or exponential work.
// ============================================================================

// maximum DOM nesting depth. guards call-stack overflow in layout_flow_node()
// and layout_abs_block() — both use the same lycon->depth counter.
constexpr int MAX_LAYOUT_DEPTH = 300;

// maximum total layout node count per pass. prevents O(n) on 50k+ element docs.
constexpr int MAX_LAYOUT_NODES = 50000;

// maximum flex container nesting depth. prevents O(2^n) exponential blowup
// from deeply nested flex-in-flex containers.
constexpr int MAX_FLEX_DEPTH = 16;

// maximum grid container nesting depth. in optimized builds the grid multipass
// cycle (layout_grid_content / layout_final_grid_content /
// layout_grid_item_final_content_multipass + baseline) is inlined into a single
// function whose frame measures ~1.5 MB, so the 8 MB main-thread stack only fits
// ~5 nested grids. kept well below that; grid nested deeper than this is skipped.
constexpr int MAX_GRID_DEPTH = 4;

// maximum iframe nesting depth. prevents infinite recursion for self-referencing iframes.
constexpr int MAX_IFRAME_DEPTH = 3;
