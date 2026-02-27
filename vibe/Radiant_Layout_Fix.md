# Radiant Layout Engine — CSS 2.1 Fixes

CSS 2.1 test suite: **8228 → 8281 (+53)** of 9567 tests. Baseline: 2302/2303 (pre-existing `table-anonymous-block-006` failure unchanged).

## Files Modified

| File | Changes |
|------|---------|
| `radiant/layout_block.cpp` | Fix 1 (float avoidance height), Fix 3 (auto margin + floats), Fix 4 (inline-block overflow), Fix 6 (margin collapse) |
| `radiant/layout_table.cpp` | Fix 2 (table auto-width in BFC), Fix 5 (caption explicit width) |
| `radiant/intrinsic_sizing.cpp` | Fix 7 (font-size in intrinsic measurement) |
| `radiant/layout_inline.cpp` | Fix 8 (per-inline line-height), Fix 9 (abspos bounding box) |
| `radiant/layout_text.cpp` | Fix 8 (line_break expanded inline LH flag) |
| `radiant/layout.hpp` | Fix 8 (`has_expanded_inline_lh` field in Linebox) |
| `radiant/resolve_css_style.cpp` | Fix 10 (font shorthand numeric weight + named size) |

---

## Fix 1 — Float Avoidance Uses Actual Element Height

**Spec**: CSS 2.1 §9.5 — BFC border box must not overlap float margin boxes.

**Bug**: `block_context_space_at_y()` was called with `height = 1.0f` instead of the element's actual border-box height. A float occupying the vertical range below the first pixel was ignored.

**Fix** (`layout_block.cpp`): Use the element's actual border-box height (or `lowest_float_bottom - bfc_y` for auto-height elements) when querying float avoidance space.

**Tests**: +6

## Fix 2 — Auto-Width Table Uses BFC Content Width

**Spec**: CSS 2.1 §17.5.2.2 — table width constrained by available space.

**Bug**: `table_auto_layout()` used `parent->width` as available width, ignoring float intrusion already accounted for in `lycon->block.content_width`.

**Fix** (`layout_table.cpp`): Prioritize `lycon->block.content_width` when available (set by BFC float avoidance logic), fall back to `parent->width`.

**Tests**: +2

## Fix 3 — Auto Margin Resolution Accounts for Float Reduction

**Spec**: CSS 2.1 §10.3.3 — auto margins use remaining space.

**Bug**: `margin-left: auto` / `margin-right: auto` used full `pa_block->content_width`, ignoring that BFC float avoidance had already reduced the available width.

**Fix** (`layout_block.cpp`): Subtract `bfc_available_width_reduction` from `pa_block->content_width` before computing auto margins.

**Tests**: +1

## Fix 4 — Inline-Block Overflow on Empty Line

**Spec**: CSS 2.1 §9.4.2 — inline content wraps when line is full.

**Bug**: When an inline-block's width exceeded the available line width on an empty line (first item), `line_break()` was called spuriously, producing an infinite layout loop or extra empty line.

**Fix** (`layout_block.cpp`): Guard `line_break()` with `!lycon->line.is_line_start` — an overflowing inline-block on an empty line must stay on that line.

**Tests**: +2

## Fix 5 — Caption Explicit CSS Width

**Spec**: CSS 2.1 §17.4.1 — table captions respect their own CSS width.

**Bug**: `caption->width` was unconditionally overwritten with `table_width` at both initial layout and positioning stages, discarding any explicit CSS width.

**Fix** (`layout_table.cpp`): At top/bottom caption positioning, only override with `table_width` when `caption->blk->given_width <= 0` (no explicit CSS width). Caption re-layout uses `caption->width` instead of `table_width` for content width.

**Tests**: +2

## Fix 6 — Margin Collapse for Self-Collapsing Block Chains

**Spec**: CSS 2.1 §8.3.1 — adjoining margins collapse.

**Bug**: Three related issues in parent-child margin collapse:
1. Guard skipped parent-child collapse when `margin-top == 0` but bottom margin was non-zero on a self-collapsing child.
2. Three-way collapse (parent top + child top + child bottom) missed the child's pre-existing margin chain from its own descendants.
3. Parent collapse through a chain of self-collapsing children didn't unify all margins.

**Fix** (`layout_block.cpp`): Corrected all three collapse paths to properly propagate margins through self-collapsing block chains.

**Tests**: +9

## Fix 7 — Font-Size in Intrinsic Width Measurement

**Spec**: CSS 2.1 §17.5.2.2 — intrinsic width uses element's own font properties.

**Bug**: The font-size CSS lookup was nested inside the `if (font_family_decl)` block. Elements with `font-size` but no `font-family` (e.g., `#small { font-size: 10pt }`) never triggered `setup_font()`.

**Fix** (`intrinsic_sizing.cpp`): Restructured so `font-size` check is a sibling of the `font-family` check, not a child. Either trigger alone calls `setup_font()`.

**Tests**: +9

## Fix 8 — Per-Inline Line-Height (Half-Leading)

**Spec**: CSS 2.1 §10.8.1 — each inline box uses its own `line-height` for the half-leading model. The line box height is the distance between the uppermost box top and lowermost box bottom.

**Bug**: All text on a line used the parent block's `line-height` for half-leading. An inline element like `<span style="font: 1.5in/1em ahem">` had its own line-height ignored; `line_break()` capped `used_line_height` to the parent's `css_line_height` in the text-only branch.

**Fix**:
- `layout_inline.cpp`: After `setup_font()`, call `setup_line_height()` for inline elements with their own explicit `line-height` or `font` shorthand declaration (detected via `style_tree_get_declaration()` on the element's `specified_style`). Save/restore `lycon->block.line_height` around the call.
- `layout.hpp`: Added `has_expanded_inline_lh` flag to `Linebox`.
- `layout_text.cpp`: In `line_break()`, use `max(css_line_height, font_line_height)` when `has_expanded_inline_lh` is set (an inline's own line-height exceeds the parent's). Otherwise, keep the existing text-only branch that trusts `css_line_height` (needed for `line-height: 0` and similar cases where `max_ascender + max_descender` overstates due to ≥0 clamping).

**Tests**: +4 (vertical-align-112, 113, 119, 120)

## Fix 9 — Abspos Children Excluded from Inline Bounding Box

**Spec**: CSS 2.1 §9.3.1, §10.6.3 — absolutely positioned elements are out of flow.

**Bug**: `compute_span_bounding_box()` included absolutely/fixed positioned children in the inline span's bounding box, making the span report the child's dimensions instead of 0×0.

**Fix** (`layout_inline.cpp`): Added `is_out_of_flow_child()` helper; skip such children when computing bounding box union.

**Tests**: +3 (containing-block-011, 013, 015)

## Fix 10 — Font Shorthand: Numeric Weight + Named Size

**Spec**: CSS 2.1 §15.6, §15.7 — font shorthand accepts numeric weights (100–900) and named sizes (small, large, etc.).

**Bug**: Font shorthand parsing loop handled `LENGTH`, `PERCENTAGE`, `KEYWORD`, `STRING`, `CUSTOM` value types but not `NUMBER` (numeric weights like 900). Named font-size keywords from `CSS_VALUE_GROUP_FONT_SIZE` were also unrecognized, causing the `/line-height` that follows to be misinterpreted as font-size.

**Fix** (`resolve_css_style.cpp`): Added `CSS_VALUE_TYPE_NUMBER` branch mapping values 1–1000 to `weight_value`. Added `CSS_VALUE_GROUP_FONT_SIZE` branch that sets `size_value` and correctly handles the subsequent `/line-height` separator.

**Tests**: +2
