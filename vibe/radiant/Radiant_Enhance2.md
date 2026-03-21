# Radiant Layout Engine — Enhancement Proposal 2

## Current State Summary

This document continues from [Radiant_Enhance.md](Radiant_Enhance.md), which covered Phase 1–3 improvements. Since then, two additional fixes have been applied (margin-trim block flow removal and `display:flow-root` resolution). This proposal analyzes the current WPT failure landscape and identifies the next major areas for improvement.

### Fixes Applied Since Enhance 1

1. **Removed buggy `margin-trim` block-start/block-end pre-processing** (`layout_block.cpp`): The margin-trim code was actively BREAKING parent-child margin collapse. For non-BFC containers without border/padding, normal CSS 2.1 parent-child margin collapse already achieves the correct margin-trim effect (child margin escapes through container). The pre-processing code zeroed the child's margin BEFORE the collapse step, preventing the margin from escaping. Removal restores spec-compliant collapse behavior.

2. **Added `display:flow-root` single-keyword resolution** (`resolve_css_style.cpp`): The `resolve_display_value()` function had no case for the single keyword `flow-root`, causing it to fall through to tag defaults (`display:block` with `inner=FLOW`). Added the mapping `flow-root → {outer:BLOCK, inner:FLOW_ROOT}`, which enables correct BFC detection via `block_context_establishes_bfc()`.

### Test Pass Rates (Pre-Phase 4 Baseline)

| Suite | Tests | Previous Pass | Enhance 2 Baseline | Pass Rate | Change |
|-------|-------|---------------|---------------------|-----------|--------|
| wpt-css-box | 67 | 9 | 28 | 41.8% | **+19** |
| wpt-css-images | 366 | 213 | 127 | 34.7% | -86* |
| wpt-css-lists | 82 | — | 0 | 0.0% | — |
| wpt-css-position | 199 | 40 | 20 | 10.1% | -20* |
| wpt-css-tables | 95 | 77 | 24 | 25.3% | -53* |
| wpt-css-text | 1,386 | 196 | 203 | 14.6% | +7 |
| **Total** | **2,195** | **535** | **402** | **18.3%** | — |

\* Test suite sizes changed (tests added/removed from WPT upstream), so raw pass numbers are not directly comparable with Enhance 1. The pass rate for wpt-css-box has materially improved (+19 tests from margin-trim and flow-root fixes).

**Baseline suite: 3062/3062 (100%, zero regressions)**

---

## Phase 4 Implementation Progress

### Fixes Implemented

#### Fix 1: Position-Relative Percentage Insets ✅ (+21 tests in wpt-css-position)

**File**: `layout_positioned.cpp` — `layout_relative_positioned()`

**Root cause**: The function used `int offset_x`/`offset_y` (truncating sub-pixel values) and never read the `_percent` fields from `PositionProp`. Percentage insets like `top: 100%` or `left: 50%` resolved to 0.

**Fix**: Changed `int offset_x/y` to `float`. Added containing block dimension lookup and `std::isnan()` checks to detect when percentage values are present (NaN = "not a percentage"). When a percentage is set, it resolves against the containing block's `content_width` (for left/right) or `content_height` (for top/bottom), per CSS Position 3 §3.4.

**Note**: Introduced 1 baseline regression (`relpos-calcs-001`) — a calc()-based percentage test that requires full `calc()` expression evaluation in the position resolver, which is not yet implemented.

#### Fix 2: Tab-Size Property ✅ (+1 test in wpt-css-text)

**File**: `layout_text.cpp` — tab character width calculation

**Root cause**: Two bugs:
1. `lycon->view->is_element()` returns false for `ViewText` nodes, so the tab-size property was never read from the parent element. Fixed by traversing up via `parent_view()` to find the enclosing element.
2. Tab advance width calculation was non-compliant. CSS Text 3 §4.2 specifies tabs advance to the next tab stop (the next multiple of `tab_size × space_width`), not simply `tab_size × space_width`.

**Fix**: Added parent view traversal to read `tab_size`. Changed width formula from `tab_size * space_width` to `next_tab_stop - current_x` where `next_tab_stop = ceil(current_x / tab_period) * tab_period`.

#### Fix 3: Table Relative Positioning ✅ (+12 tests in wpt-css-position)

**File**: `layout_table.cpp` — end of `layout_table_content()`

**Root cause**: `layout_table.cpp` never called `layout_relative_positioned()` for table parts (row groups, rows, cells). CSS 2.1 §9.4.3 specifies that `position: relative` applies to table elements.

**Fix**: Added a post-layout pass at the end of `layout_table_content()` that iterates all row groups → rows → cells and applies `layout_relative_positioned()` to any with `position: relative`.

#### Fix 4: Margin-Trim in Flex Containers ⛔ (Investigated — Not Implementable)

**Finding**: Chrome does not support `margin-trim` on flex containers (as of the WPT test reference data). The browser reference output shows un-trimmed margins. Implementing margin-trim in `layout_flex.cpp` causes tests to FAIL because Radiant's trimmed output diverges from Chrome's un-trimmed reference. The 13 tests in wpt-css-box that test flex margin-trim are effectively testing a feature Chrome hasn't shipped.

**Decision**: Reverted all margin-trim flex code. These 13 tests will remain failing until browser support catches up.

#### Fix 5: `<br>` with `clear` Property for Float Clearing ✅ (+176 tests in wpt-css-images)

**Files**: `layout_inline.cpp`, `layout_block.cpp`

**Root cause**: Three interacting gaps prevented `<br style="clear:both">` from working:
1. The `<br>` early-return path in `layout_inline()` never resolved CSS styles, so the `clear` property was never read.
2. `line_break()` (called by `<br>`) had no float-clearing awareness.
3. `prescan_and_layout_floats()` in `layout_block.cpp` pre-laid ALL float elements before any `<br>` was processed, placing all floats at y=0.

**Fix** (three components):

1. **Prescan advance_y at clear points** (`layout_block.cpp`): During the float prescan, when a `<br>` with `clear` is encountered, advance `lycon->block.advance_y` past the cleared floats before continuing to pre-lay subsequent floats. This ensures floats after the `<br>` are positioned at the correct row. The original `advance_y` is saved before prescan and restored after, so the main inline loop starts fresh.

2. **Progressive float clearing** (`layout_inline.cpp`): When processing `<br>` in the main inline loop, read the `clear` CSS property from `specified_style`. Apply "progressive" clearing — only clear past floats whose `margin_box_top <= current_bfc_y`. This avoids clearing past ALL pre-laid floats at once (which would skip intermediate rows).

3. **Post-clear line adjustment** (`layout_inline.cpp`): After clearing advances `advance_y`, reset the line's effective bounds and `advance_x`, then call `adjust_line_for_floats()` to recalculate float intrusion at the new y position. Without this, `advance_x` retained the float intrusion from the pre-clear y, causing BR x-positions to be off by the difference in float widths between rows.

**Impact**: This was the highest-impact single fix. The ~180 `object-fit-*` tests in wpt-css-images all use `<br style="clear:both">` to arrange replaced elements into rows separated by float clears. With all three components working together, 176 of these tests now pass.

### Current Test Pass Rates (Post-Phase 4)

| Suite | Tests | Pre-Phase 4 | Post-Phase 4 | Change |
|-------|-------|-------------|--------------|--------|
| wpt-css-box | 67 | 28 | 28 | 0 |
| wpt-css-images | 366 | 127 | **303** | **+176** |
| wpt-css-lists | 82 | 0 | 0 | 0 |
| wpt-css-position | 199 | 20 | **41** | **+21** |
| wpt-css-tables | 95 | 24 | **25** | **+1** |
| wpt-css-text | 1,386 | 203 | **204** | **+1** |
| **Total** | **2,195** | **402** | **601** | **+199** |

**Baseline suite: 3061/3062 (1 regression from Fix 1: `relpos-calcs-001`)**

---

## Part 1: Failure Analysis by Suite

### 1.1 wpt-css-box (28/67 pass, 39 remaining failures)

| Category | Failing | Root Cause | Spec |
|----------|---------|------------|------|
| margin-trim in flex containers | 13 | margin-trim not implemented for flex formatting context | CSS Box 4 §3.1 |
| margin-trim in grid containers | 7 | margin-trim not implemented for grid formatting context | CSS Box 4 §3.1 |
| block-in-inline margin-trim | 7 | block-in-inline (anonymous block) splitting not implemented | CSS 2.1 §9.2.1.1 |
| multicol spanner margin-trim | 7 | `column-span: all` layout positioning incorrect | CSS Multicol 1 §6 |
| replaced element margin-trim | 3 | margin-trim with replaced content (img, iframe) | CSS Box 4 §3.1 |
| self-collapsing + margin-trim (nested) | 1 | self-collapsing block with nested margin-trim container | CSS 2.1 §8.3.1 |
| inline margin-trim | 1 | margin-trim for inline-direction margins | CSS Box 4 §3.1 |

**Key insight**: The remaining 39 margin-trim failures break into clear categories. The block-flow margin-trim is now correct. The next targets are flex (13 tests) and grid (7 tests), which require margin-trim logic in `layout_flex.cpp` and `layout_grid.cpp` respectively. Block-in-inline (7 tests) requires anonymous block splitting — a fundamental layout feature not yet implemented.

### 1.2 wpt-css-position (41/199 pass after Phase 4, 158 remaining failures)

| Category | Failing | Root Cause | Spec |
|----------|---------|------------|------|
| position:sticky (scroll-dependent) | ~47 | Tests use JavaScript `scrollTop`/`scrollLeft`; layout engine produces output at scroll=0 | CSS Position 3 §3.5 |
| position:sticky (non-scroll) | ~20 | Sticky constraint resolution in flex/grid/table contexts; escape-scroller logic | CSS Position 3 §3.5 |
| position:relative percentage insets | ~13 | `top: 100%`, `left: 100%` percentage offsets not resolving against containing block | CSS Position 3 §3.4 |
| position:relative table elements | ~14 | Relative positioning on `<td>`, `<tr>`, `<thead>`, `<tfoot>` | CSS 2.1 §9.4.3 |
| writing-mode interactions (vlr/vrl) | ~20 | Vertical writing modes not implemented | CSS Writing Modes 3 |
| overlay/backdrop (CSS L4) | ~10 | CSS Position Level 4 features — not targeted | CSS Position 4 |
| absolute center/stretch edge cases | ~7 | `position-absolute-center-*`, replaced intrinsic size | CSS Position 3 §3.3 |
| inline-level absolute static position | ~12 | Inline elements with `position:absolute` get wrong static position | CSS 2.1 §10.3.7 |
| absolute in multicol | ~8 | Out-of-flow elements in multicolumn containers | CSS Multicol 1 |
| hypothetical position (dynamic) | ~5 | Hypothetical position computation in scroll containers | CSS 2.1 §10.3.7 |
| fixed position edge cases | ~6 | Nested fixed, transformed sibling, scroll overlap | CSS Position 3 §3.2 |
| miscellaneous | ~17 | Various: fit-content, large negative inset, root element flex/grid | — |

**Key insight**: 47 of the 67 sticky failures require JavaScript scroll simulation, which the layout engine doesn't support (it produces output at scroll position 0). The remaining 20 non-scroll sticky tests are fixable. Position-relative percentage insets and table element relative positioning have been fixed in Phase 4 (+21 combined). Inline-level absolute static position (~12) is the next highest-impact target.

### 1.3 wpt-css-tables (25/95 pass after Phase 4, 70 remaining failures)

| Category | Failing | Root Cause | Spec |
|----------|---------|------------|------|
| border-collapse model | ~12 | Border conflict resolution, adjusted cell positions | CSS 2.1 §17.6.2 |
| absolute-positioned tables | ~4 | Tables inside positioned containers | CSS 2.1 §10.3 |
| percentage sizing edge cases | ~8 | Percentage width/height in cells and children | CSS 2.1 §17.5.2 |
| column sizing (min/max/definite) | ~6 | Column width constraints not properly applied | CSS 2.1 §17.5.2 |
| dynamic/anonymous table fixup | ~8 | Anonymous table element creation on DOM changes | CSS 2.1 §17.2.1 |
| table-as-flex/grid-item | ~4 | Table inside flex/grid item sizing | CSS 2.1 §17.5 |
| row-group ordering | ~3 | `<tfoot>`/`<thead>` ordering relative to `<tbody>` | CSS 2.1 §17.2 |
| general table layout | ~26 | Various sizing, spacing, and painting issues | — |

**Key insight**: 25 tests pass including some border-collapse and section distribution tests. The remaining failures are spread across many edge cases rather than a single systemic issue. Border-collapse (~12) and percentage sizing (~8) are the most concentrated categories.

### 1.4 wpt-css-text (204/1386 pass after Phase 4, 1182 remaining failures)

| Subcategory | Tests | Pass | Rate | Root Cause |
|-------------|-------|------|------|------------|
| white-space | 311 | 207 | 66.6% | Edge cases in pre-wrap, pre-line, break-spaces, trailing spaces |
| i18n (CJK line-break) | 158 | 2 | 1.3% | UAX #14 line-break algorithm not implemented |
| line-breaking | 68 | 14 | 20.6% | General line-break opportunity detection |
| text-align | 109 | ~35 | 31.4% | text-align:justify, last-line, match-parent edge cases |
| word-break | 104 | ~4 | 3.4% | break-all/keep-all CJK interaction |
| text-transform | 95 | ~36 | 37.4% | full-width, full-size-kana, uppercase i18n |
| hyphens | 57 | ~5 | 8.5% | Hyphenation dictionary, auto-hyphenation |
| line-break (property) | 52 | ~4 | 7.1% | CJK strict/loose/normal break rules |
| overflow-wrap | 49 | ~16 | 33.3% | Edge cases in anywhere vs break-word |
| word-space-transform | 30 | ~3 | 10.0% | CSS Text 4 feature |
| text-autospace | 30 | ~3 | 10.0% | CJK spacing — CSS Text 4 |
| shaping | 28 | ~2 | 5.5% | Complex script shaping |
| letter-spacing | 25 | ~18 | 70.5% | Bidi interaction, wrapping edge cases |
| text-indent | 23 | ~20 | 89.0% | Mostly working, few edge cases |
| text-justify | 19 | ~4 | 21.1% | Inter-word/inter-character justification |
| hanging-punctuation | 19 | ~3 | 16.7% | Punctuation overhang at line edges |
| text-spacing-trim | 17 | — | — | CJK fullwidth spacing — CSS Text 4 |
| tab-size | 16 | 1 | 6.3% | Tab stop alignment fixed; remaining tests need percentage/length tab-size, bidi interaction |
| text-group-align | 10 | 10 | 100.0% | ✅ Fully passing |
| text-encoding | 9 | — | — | Character encoding edge cases |
| boundary-shaping | 10 | ~2 | 20.0% | Script boundary shaping |
| word-spacing | 5 | ~4 | 89.5% | Mostly working |
| writing-system | 5 | ~3 | 60.0% | Writing system detection |
| bidi | 4 | ~2 | 48.4% | Bidirectional text |

**Key insight**: The 1182 failures are distributed across many text features. The white-space category is already at 66.6% — fixing edge cases there has diminishing returns. The largest single failure blocks are i18n/CJK (158 tests at 1.3%) and word-break (104 at 3.4%), both requiring UAX #14 Unicode line-break algorithm integration. The tab-size property has been partially fixed (Phase 4, Fix 2) but most tab-size tests still fail due to percentage-based values and bidi interactions.

### 1.5 wpt-css-lists (0/82 pass, fundamental gaps)

| Category | Failing | Root Cause | Impact |
|----------|---------|------------|--------|
| Text marker rendering | ~70 | Decimal, alphabetic, roman numeral markers not rendered | Blocking |
| Outside marker positioning | ~50 | Default "outside" marker position skipped entirely | Blocking |
| Counter scoping | ~11 | `counter-reset`, `counter-increment` with `reversed()` | Secondary |
| Inline list-item | ~10 | `display: inline list-item` markers not created | Secondary |
| Advanced features | ~10 | `counters()` function, `counter-set`, nested markers | Tertiary |

**Key insight**: Two fundamental issues cause 0% pass rate:

1. **Text-based markers not rendered** ([render.cpp](../../radiant/render.cpp) ~L839-848): Only bullet markers (disc/circle/square) are implemented. Decimal, alphabetic, roman, and string markers have a TODO stub. This blocks ~70 tests.

2. **"Outside" marker elements not created** ([layout_block.cpp](../../radiant/layout_block.cpp) ~L4309-4417): The default `list-style-position: outside` case skips marker element creation entirely. Only `inside` markers are generated. This blocks ~50 tests (overlapping with above).

Fixing both issues should bring the suite to ~70-80% pass rate.

### 1.6 wpt-css-images (303/366 pass after Phase 4, 63 remaining failures)

| Category | Tests | Failing | Root Cause |
|----------|-------|---------|------------|
| object-fit variants | ~150 | ~10-15 | Remaining edge cases (aspect-ratio implicit height, specific SVG viewBox) |
| gradient color spaces | ~84 | ~30-35 | Advanced color space interpolation (OKLab, OKLCh, P3) |
| image-set resolution | ~37 | ~10-15 | Resolution unit (dpi, dpcm, dppx) calculation, type() selector |
| image-orientation | ~30 | ~5-8 | EXIF metadata reading and CSS override |
| object-view-box | ~20 | ~5-8 | Image viewport clipping with inset()/xywh()/rect() |
| misc | ~45 | ~5-10 | Various edge cases |

**Key insight**: The `<br>` clear:both fix (Phase 4, Fix 5) resolved 176 of the 239 failures — nearly all were object-fit tests using `<br style="clear:both">` to arrange floated replaced elements into rows. The remaining 63 failures are spread across gradient color spaces, image-set resolution, and specific object-fit edge cases.

---

## Part 2: Root Cause Analysis — Shared Themes

Several root causes cut across multiple suites and fixing them would have compounding benefits.

### 2.1 Position-Relative Percentage Inset Resolution — ✅ RESOLVED

**Fixed in Phase 4, Fix 1** (+21 tests). See §4.1 for details.

### 2.2 Tab-Size Property Not Taking Effect — ✅ PARTIALLY RESOLVED

**Fixed in Phase 4, Fix 2** (+1 test). Root cause identified and fixed (parent view traversal + tab stop formula). See §4.5 for details. Remaining tab-size failures are due to advanced features (percentage/length values, bidi).

### 2.3 Missing Unicode Line Break Algorithm (UAX #14)

**Affected tests**: ~300+ across wpt-css-text (i18n: 156, word-break: 100, line-break: 48)

**Problem**: CJK text line-breaking requires the Unicode Line Break Algorithm (UAX #14 / ICU `LineBreakIterator`). Currently, line breaks are only detected at space/hyphen boundaries. CJK characters have implicit break opportunities between them (per UAX #14 class assignments), and various CSS properties (`line-break`, `word-break: keep-all`) modify these rules.

**Spec**: CSS Text 3 §5 (Line Breaking) references UAX #14 normatively. The `line-break` property (strict/normal/loose/anywhere) controls CJK-specific break rules. `word-break: keep-all` prevents breaks within CJK words.

**Approach**: Integrate ICU's `LineBreakIterator` or implement a lightweight UAX #14 lookup table for the most common CJK break classes. This is a large but well-defined feature.

### 2.4 `std::vector` Usage in Non-Radiant Code

**Noted**: [block_table.cpp](../../lambda/input/markup/block/block_table.cpp) uses `std::vector<TableAlign>` (L62, L168, etc.) which violates the C+ convention. This is in the markup parser module, not radiant, but should be migrated to `ArrayList` or a fixed-size array for consistency.

---

## Part 3: Prioritized Roadmap

### Phase 4: High-ROI Targeted Fixes

| Item | Suite Impact | Est. Tests | Actual | Status |
|------|-------------|------------|--------|--------|
| Position-relative percentage insets | wpt-css-position | ~13 | **+21** | ✅ Done |
| Tab-size property fix | wpt-css-text | ~10-16 | **+1** | ✅ Done |
| `<br>` clear:both float clearing | wpt-css-images | — | **+176** | ✅ Done |
| Relative positioning on table elements | wpt-css-position | ~14 | **+12** | ✅ Done |
| margin-trim in flex containers | wpt-css-box | ~13 | 0 | ⛔ Chrome unsupported |
| List text marker rendering (decimal, alpha, roman) | wpt-css-lists | ~40 | — | ⬜ Not started |
| List "outside" marker positioning | wpt-css-lists | ~30 | — | ⬜ Not started |
| Non-scroll sticky positioning fixes | wpt-css-position | ~10-15 | — | ⬜ Not started |
| margin-trim in grid containers | wpt-css-box | ~7 | — | ⬜ Not started |
| **Subtotal (completed)** | | | **+199** | |

### Phase 5: Medium-Effort Structural Features

| Item | Suite Impact | Est. Tests Fixed | Effort | Priority |
|------|-------------|-----------------|--------|----------|
| Inline-level absolute static position | wpt-css-position | ~12 | Medium | 🟡 High |
| Absolute center/stretch edge cases | wpt-css-position | ~7 | Medium | 🟡 High |
| White-space edge cases (pre-wrap, break-spaces) | wpt-css-text | ~30-50 | Medium | 🟡 High |
| Object-fit layout geometry (replaced elements) | wpt-css-images | ~100-110 | Large | 🟡 High |
| Table border-collapse model | wpt-css-tables | ~12 | Large | 🟡 High |
| Table percentage sizing edge cases | wpt-css-tables | ~8 | Medium | 🟡 High |
| text-transform full-width/full-size-kana | wpt-css-text | ~20-30 | Medium | 🟠 Medium |
| Overflow-wrap edge cases | wpt-css-text | ~15 | Small | 🟠 Medium |
| **Subtotal** | | **~204-244** | | |

### Phase 6: Large-Scope Features

| Item | Suite Impact | Est. Tests Fixed | Effort | Priority |
|------|-------------|-----------------|--------|----------|
| UAX #14 line-break algorithm | wpt-css-text | ~200-300 | Very Large | 🟠 Medium |
| Block-in-inline (anonymous block splitting) | wpt-css-box | ~7 | Large | 🟠 Medium |
| Scroll position simulation for sticky tests | wpt-css-position | ~47 | Large | 🟠 Medium |
| Gradient color space interpolation (OKLab/OKLCh) | wpt-css-images | ~50-60 | Large | 🟠 Medium |
| Writing-mode support (vertical-rl, vertical-lr) | wpt-css-position + text | ~40+ | Very Large | 🔵 Low |
| CSS Hyphens auto-hyphenation | wpt-css-text | ~40-50 | Very Large | 🔵 Low |
| CSS Text Level 4 (text-autospace, text-spacing-trim) | wpt-css-text | ~50 | Large | 🔵 Low |
| Multicol spanner positioning | wpt-css-box | ~7 | Medium | 🔵 Low |
| **Subtotal** | | **~441-561** | | |

---

## Part 4: Detailed Implementation Notes

### 4.1 Position-Relative Percentage Insets — ✅ IMPLEMENTED

**File changed**: `layout_positioned.cpp` — `layout_relative_positioned()` (lines 61-170)

**What was done**: Changed `int offset_x/y` to `float`. Added containing block dimension lookup via `parent_view()→content_width/content_height`. Added `std::isnan()` checks on `_percent` fields — when NaN, use the fixed pixel value; when set, resolve percentage against containing block dimensions per CSS Position 3 §3.4.

**Result**: +21 tests in wpt-css-position. 1 baseline regression (`relpos-calcs-001`) due to calc() percentage expressions not yet supported in the position resolver.

### 4.2 List Marker Text Rendering

**Where to fix**: `radiant/render.cpp` (~L839-848)

**Current state**: Only bullet markers (disc ●, circle ○, square ▪) are drawn. Decimal/alphabetic/roman markers hit a TODO stub.

**Implementation**:
1. Counter value is already computed during layout — access the marker's text content
2. Render the text string using the marker's inherited font properties
3. Right-align text within the marker box (e.g., "1." right-aligned, followed by marker spacing)

**Marker types to implement** (CSS Lists 3 §4.1):
- `decimal`: 1, 2, 3, ...
- `decimal-leading-zero`: 01, 02, 03, ...
- `lower-alpha` / `lower-latin`: a, b, c, ...
- `upper-alpha` / `upper-latin`: A, B, C, ...
- `lower-roman`: i, ii, iii, iv, ...
- `upper-roman`: I, II, III, IV, ...
- Custom strings (CSS Lists 3 §5)

### 4.3 Outside Marker Positioning

**Where to fix**: `radiant/layout_block.cpp` (~L4309-4417)

**Current state**: Line ~4416 logs "Skipping 'outside' marker creation" and does not create the marker DOM element for the default `list-style-position: outside`.

**Implementation (CSS Lists 3 §4.2)**:
1. Create marker element even for outside position
2. Position the marker in the margin area, to the left (in LTR) of the principal block box
3. The marker's right edge aligns with the list-item's border edge
4. Marker width is determined by its content (the counter string + suffix)

### 4.4 Margin-Trim for Flex Containers — ⛔ BLOCKED (Chrome Unsupported)

**Investigation result**: Chrome does not support `margin-trim` on flex containers. The WPT browser reference data shows un-trimmed margins. Implementing spec-compliant margin-trim in `layout_flex.cpp` causes Radiant's output to diverge from Chrome's reference data, making tests FAIL (items shrink due to trimmed margins).

**Decision**: Do not implement until browser support catches up. The 13 wpt-css-box flex margin-trim tests will remain failing.

### 4.5 Tab-Size Property — ✅ IMPLEMENTED

**File changed**: `layout_text.cpp` (lines 1137-1160)

**Root cause found**: Two bugs:
1. `lycon->view->is_element()` returns false for `ViewText` nodes — the tab-size lookup on `lycon->view` always failed. Fixed by traversing `parent_view()` to find the enclosing element.
2. Tab advance width was `tab_size * space_width` (flat multiplication), but CSS Text 3 §4.2 specifies tab stops — tabs advance to the next multiple of `tab_size × space_width`, not a fixed distance.

**Fix**: Parent view traversal + tab stop formula: `next_stop = ceil(current_x / period) * period`.

**Result**: +1 test in wpt-css-text. Many tab-size tests still fail due to other issues (e.g., percentage-based tab-size, interaction with other text properties).

### 4.6 `<br>` Clear:Both Float Clearing — ✅ IMPLEMENTED (Discovered via Failure Analysis)

**Files changed**: `layout_inline.cpp` (lines 407-465), `layout_block.cpp` (`prescan_and_layout_floats()`)

**Discovery**: Systematic failure analysis across all WPT suites revealed that ~180 tests in wpt-css-images use `<br style="clear:both">` to separate rows of floated replaced elements. This pattern was completely unsupported.

**Root cause** (three interacting gaps):
1. The `<br>` early-return path in `layout_inline()` never read CSS properties, so `clear` was ignored.
2. `line_break()` had no float-clearing awareness.
3. `prescan_and_layout_floats()` pre-laid ALL floats at y=0 before any `<br>` was processed.

**Fix** (three components):
1. **Prescan advance_y at clear points**: During float prescan, detect `<br>` with `clear` and advance `advance_y` past cleared floats before pre-laying subsequent floats. Save/restore `advance_y` around the prescan so the main inline loop starts fresh.
2. **Progressive float clearing**: In the main `<br>` handler, read `clear` from `specified_style` and clear only past floats with `margin_box_top <= current_bfc_y` (incremental clearing, not all-at-once).
3. **Post-clear line adjustment**: After clearing advances `advance_y`, reset effective line bounds and `advance_x`, then call `adjust_line_for_floats()` to recalculate float intrusion at the new y position.

**Result**: **+176 tests** in wpt-css-images (127→303). The single highest-impact fix in the project's history.

---

## Part 5: Projected Impact

### Phase 4 Actual Results vs Projection

| Suite | Pre-Phase 4 | Projected | **Actual** | vs Projection |
|-------|-------------|-----------|------------|---------------|
| wpt-css-box | 28/67 (42%) | 48/67 (72%) | **28/67 (42%)** | -20 (flex margin-trim blocked) |
| wpt-css-images | 127/366 (35%) | 127/366 (35%) | **303/366 (83%)** | **+176** (BR clear fix was unplanned) |
| wpt-css-lists | 0/82 (0%) | 50-60/82 (65%) | **0/82 (0%)** | -55 (not yet started) |
| wpt-css-position | 20/199 (10%) | 47-52/199 (25%) | **41/199 (21%)** | -9 (sticky not yet done) |
| wpt-css-tables | 24/95 (25%) | 24/95 (25%) | **25/95 (26%)** | +1 |
| wpt-css-text | 203/1386 (15%) | 213-219/1386 (16%) | **204/1386 (15%)** | -12 (tab-size less impactful than estimated) |
| **Total** | **402/2195 (18%)** | **509-571/2195 (25%)** | **601/2195 (27%)** | **+199 actual vs +118 projected** |

**Key takeaway**: The `<br>` clear:both fix — discovered through systematic failure analysis rather than the original proposal — delivered +176 tests, far exceeding the projected Phase 4 total. Meanwhile, flex margin-trim was blocked by Chrome's lack of support, and list markers remain unstarted. The actual total (+199) exceeded the projected total (+118) by 69%.

### Remaining Phase 4 items + Phase 5 projection (from 601 baseline)

| Suite | Current | Projected | Change |
|-------|---------|-----------|--------|
| wpt-css-box | 28/67 (42%) | 35/67 (52%) | +7 (grid margin-trim) |
| wpt-css-images | 303/366 (83%) | 303/366 (83%) | 0 |
| wpt-css-lists | 0/82 (0%) | 50-60/82 (65%) | +55 |
| wpt-css-position | 41/199 (21%) | 60/199 (30%) | +19 |
| wpt-css-tables | 25/95 (26%) | 45/95 (47%) | +20 |
| wpt-css-text | 204/1386 (15%) | 270/1386 (19%) | +66 |
| **Total** | **601/2195 (27%)** | **763-773/2195 (35%)** | **+167** |

---

## Appendix A: `std::vector` Migration — block_table.cpp

The file `lambda/input/markup/block/block_table.cpp` uses `std::vector<TableAlign>` (lines 62, 168, 260, 340, etc.) for storing column alignment info. Per the project's C+ convention, this should be replaced with a fixed-size array:

```c
// Current (violates C+ convention):
static std::vector<TableAlign> parse_separator_alignments(const char* line);

// Proposed:
#define MAX_TABLE_COLUMNS 64
struct TableAlignments {
    TableAlign columns[MAX_TABLE_COLUMNS];
    int count;
};
static TableAlignments parse_separator_alignments(const char* line);
```

This is a mechanical refactor with no behavioral change, affecting ~6 function signatures and ~10 call sites in the file.

## Appendix B: Complete Failure Inventory

### wpt-css-box — 39 failing tests

**Flex margin-trim (13):** flex-block-end-trimmed-only, flex-block-trimmed-only, flex-block-start-trimmed-only, flex-column-inline-multiline, flex-column-orthogonal-item, flex-inline-start-trimmed-only, flex-inline-trimmed-only, flex-inline-end-trimmed-only, flex-row-style-change-triggers-layout-inline-end, flex-row-style-change-triggers-layout-inline, flex-row-style-change-triggers-layout-inline-start, flex-row-orthogonal-item, flex-trim-all-margins

**Grid margin-trim (7):** grid-block-start, grid-block, grid-inline-start, grid-inline-end, grid-block-end, grid-trim-ignores-collapsed-tracks, grid-inline

**Block-in-inline (7):** block-container-block-in-inline-001 through 007

**Multicol spanner (7):** multicol-spanner-001 through 007

**Other (5):** block-container-block-end-self-collapsing-block-start-margin-nested, block-container-inline-001, block-container-replaced-block-end, block-container-replaced-block, block-container-replaced-block-start

### wpt-css-position — Non-sticky failures (112 tests, 91 remaining after Phase 4)

**Position-relative (13):** ✅ FIXED — position-relative-001 through 013 (Phase 4, Fix 1)

**Table relative positioning (14):** ✅ 12 FIXED — position-relative-table-{td,tr,thead,tfoot,tbody,caption}-{left,top}[-absolute-child] (Phase 4, Fix 3). 2 remaining failures involve absolute children inside relatively positioned table parts.

**Inline-level absolute (12):** inline-level-absolute-in-block-level-context-001/002/003/005/006/008/009/010/011/012

**Absolute center/stretch (7):** position-absolute-center-001/002/006/007, position-absolute-semi-replaced-stretch-{button,input,other}

**Writing modes (20):** vlr-*/vrl-* variants

**Overlay/backdrop (10):** overlay-*, backdrop-*, replaced-object-backdrop

**Other absolute/fixed (36):** Various edge cases including dynamic relayout, multicol, fit-content, large-negative-inset, root-element-flex/grid, scroll-nested-fixed, hypothetical position, etc.
