// Phase 6 — Layout / input bridge for DOM ranges and selection.
//
// The DOM (`DomNode`/`DomElement`/`DomText`) and the layout view tree are the
// *same* objects in radiant (`ViewText : DomText`, `ViewElement : DomElement`,
// etc.). After layout has run, every `DomText` carries a `TextRect` chain
// (`DomText::rect`) that describes where its glyphs are drawn. This file
// turns spec-level `DomBoundary` / `DomRange` / `DomSelection` values into
// pixel-level rectangles, and turns mouse `(x, y)` coordinates back into
// boundaries.
//
// All public functions are safe to call when no layout has been performed
// yet — they simply return false / empty results in that case.

#pragma once

#include "dom_range.hpp"
#include "view.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Layout cache resolution
// ---------------------------------------------------------------------------

// Populate the layout cache fields on `range` (`start_view` / `start_x` /
// `start_y` / `start_height` and the `end_*` counterparts) by inspecting
// the DOM nodes that the range's boundaries refer to. Sets
// `range->layout_valid = true` on success. On failure (boundary node not in
// the layout tree, no glyph data yet, etc.) the function returns false and
// leaves `layout_valid = false`.
//
// This is idempotent and cheap when `layout_valid` is already true; callers
// that mutate the DOM should call `dom_range_invalidate_layout(range)` to
// force a re-resolve.
bool dom_range_resolve_layout(DomRange* range);

// Convenience: resolve the layout for the first range of `selection`.
bool dom_selection_resolve_layout(DomSelection* selection);

// (`dom_range_invalidate_layout` is declared in `dom_range.hpp`.)

// ---------------------------------------------------------------------------
// Hit testing — pixels → DomBoundary
// ---------------------------------------------------------------------------

// Hit-test a viewport point `(vx, vy)` against the layout tree rooted at
// `root_view` (typically the document body view) and return a DomBoundary
// pointing at the closest insertion point. Falls back to `{nullptr, 0}` if
// the tree contains no text nodes.
//
// `(vx, vy)` are in CSS pixels in the same coordinate space the layout was
// performed in (i.e. the absolute coordinates `view_to_absolute_position`
// would produce, NOT physical/device pixels).
DomBoundary dom_hit_test_to_boundary(View* root_view, float vx, float vy);

// ---------------------------------------------------------------------------
// Multi-rect rendering helper
// ---------------------------------------------------------------------------

// Callback signature used by `dom_range_for_each_rect()`. `(x, y)` are in
// absolute CSS coordinates (the same coordinate space that the resolver
// fills `start_x` / `start_y` in). `userdata` is opaque.
typedef void (*DomRangeRectCb)(float x, float y, float w, float h, void* userdata);

// Iterate every visual rectangle covered by `range`. For a single-line
// range this fires once; for a multi-line range it fires once per line of
// each crossed text node. Caller must have called
// `dom_range_resolve_layout(range)` first.
//
// `uicon` is optional: when non-NULL the helper uses glyph-precise advance
// widths (matching the caret painter) so the right edge of the selection
// rectangle aligns exactly with the caret. When NULL the resolver falls
// back to linear interpolation across the rect width.
void dom_range_for_each_rect(DomRange* range, UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

// ---------------------------------------------------------------------------
// Legacy → DOM mirroring
// ---------------------------------------------------------------------------

// Phase 6 is non-invasive: existing `caret_*` and `selection_*` paths still
// drive the legacy `CaretState` / `SelectionState`. After they update the
// legacy state, they call into here to keep `state->dom_selection` in sync,
// so JavaScript reads (`window.getSelection()`) observe the same anchor /
// focus the user set with the mouse / keyboard.
//
// Both functions are no-ops when `state->dom_selection` is null. They
// allocate it lazily on first call so JS reads match what the user sees.
void dom_selection_sync_from_legacy_selection(struct RadiantState* state);
void dom_selection_sync_from_legacy_caret    (struct RadiantState* state);

// Inverse direction (Phase 6 single-source-of-truth). Reads
// `state->dom_selection` and writes the resulting (anchor/focus/caret)
// boundaries, including resolved layout x/y/height, into the legacy
// `SelectionState` and `CaretState` so the renderer (which still reads
// the legacy structs) reflects DOM-side mutations made by JS or by the
// spec algorithms (e.g. on DOM mutation). No-op when DomSelection is
// empty (clears legacy selection in that case). Re-entry guarded via
// `state->dom_selection_sync_depth`.
void legacy_sync_from_dom_selection(struct RadiantState* state);

#ifdef __cplusplus
}  // extern "C"
#endif
