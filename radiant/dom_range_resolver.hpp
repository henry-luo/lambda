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

// Variant of dom_range_for_each_rect that emits rects only for the given
// text node (`target_text`). Used by the inline text painter so the
// selection background can be drawn per-fragment immediately before the
// glyphs of that fragment, ensuring text renders on top of the highlight
// rather than being obscured by an after-the-fact overlay.
void dom_range_for_each_rect_in_text(struct DomRange* range,
    struct DomText* target_text, struct UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

// Further restricted variant: emit at most one rect, for the given
// `target_rect` within `target_text`. Used by render_text_view to
// interleave selection paint with per-fragment inline backgrounds.
void dom_range_for_each_rect_in_text_rect(struct DomRange* range,
    struct DomText* target_text, struct TextRect* target_rect,
    struct UiContext* uicon, DomRangeRectCb cb, void* userdata);

// ---------------------------------------------------------------------------
// Projection → DOM mirroring
// ---------------------------------------------------------------------------

// Compatibility direction for older projection entry points. Most callers now
// update DomSelection first, but projection-only paths can still call into here
// to keep `state->dom_selection` in sync.
//
// Both functions are no-ops when `state->dom_selection` is null. They
// allocate it lazily on first call so JS reads match what the user sees.
void dom_selection_sync_from_selection_projection(struct DocState* state);
void dom_selection_sync_from_caret_projection    (struct DocState* state);

// Canonical direction. Reads StateStore's EditingSelection/DomSelection facade
// and refreshes the projection structs with anchor/focus/caret boundaries plus
// resolved layout x/y/height when the selection mutation seq has advanced.
void state_store_refresh_caret_projection(struct DocState* state);

// Register a glyph-precise X resolver. When set, `dom_range_for_each_rect()`
// uses it instead of linear interpolation so that the right edge of the
// selection rectangle aligns pixel-exactly with the caret (which is painted
// using the same glyph walker). The function must return rect-relative x
// (i.e. the same coordinate space as `TextRect::x`).
typedef float (*GlyphXResolverFn)(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, int byte_offset);
void dom_range_set_glyph_x_resolver(GlyphXResolverFn fn);

// Convenience wrapper around the registered resolver. Falls back to linear
// interpolation across the rect's width when no resolver is registered.
float dom_range_glyph_x_for_byte_offset(struct UiContext* uicon,
    struct ViewText* text, struct TextRect* rect, int byte_offset);

// Inverse of GlyphXResolverFn: given a rect-relative X (in the same
// coordinate space as `TextRect::x`), return the byte offset whose visual
// position is closest to that X. Used by vertical caret navigation
// (Up/Down arrows) so the caret lands at the same visual column on the
// new line. Falls back to linear interpolation when not registered.
typedef int (*ByteOffsetForXResolverFn)(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, float target_local_x);
void dom_range_set_byte_offset_for_x_resolver(ByteOffsetForXResolverFn fn);

// Convenience wrapper around the registered resolver. Falls back to linear
// interpolation across the rect's width when no resolver is registered.
int dom_range_byte_offset_for_x(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, float target_local_x);

#ifdef __cplusplus
}  // extern "C"
#endif
