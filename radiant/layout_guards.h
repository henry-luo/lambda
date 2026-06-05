#pragma once

// ============================================================================
// Layout Safety Guards — centralized limits preventing pathological inputs
// from causing stack overflow, CPU timeout, or exponential work.
// ============================================================================

// maximum DOM nesting depth. guards call-stack overflow in layout_flow_node()
// and layout_abs_block() — both use the same lycon->depth counter.
//
// The block-flow recursion (layout_flow_node → layout_block → layout_block_content
// → layout_block_inner_content → layout_flow_node) puts several large frames on
// the native 8 MB main-thread stack per nesting level. The safe ceiling is
// build-dependent and the difference is dramatic (measured empirically):
//
//   - Release (-O2, no ASan): ~4 KB/level → overflows beyond ~2000 levels.
//   - Debug (-O0 + AddressSanitizer): ~56 KB/level → overflows around ~136
//     levels. The inflation is almost entirely instrumentation — ASan stack
//     redzones plus -O0 giving every local in these ~2000-line functions its
//     own non-overlapping slot — not genuinely large locals (the only sizeable
//     ones, the pa_block/pa_line/pa_font context copies, total ~1.5 KB).
//
// So the guard value must track the build: keep the full 300 in release (still
// 6x below the native ceiling), and cap the ASan debug build well under its
// ~136 threshold so it truncates gracefully instead of crashing (SIGSEGV).
#ifdef NDEBUG
constexpr int MAX_LAYOUT_DEPTH = 300;
#else
constexpr int MAX_LAYOUT_DEPTH = 100;
#endif

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
