# Radiant Layout Engine — CSS 2.1 Conformance Test Report (Phase 5)

**Date:** 2026-02-22  
**Test Suite:** W3C CSS 2.1 (`make layout suite=css2.1`)  
**Engine:** Lambda CSS / Radiant  
**Platform:** macOS (darwin)  
**Baseline:** Phase 4 report ([Radiant_Layout_Test4_Css.md](Radiant_Layout_Test4_Css.md))

---

## 1. Overall Results

| Metric | Phase 3 End | Phase 4 Start | **Phase 5 (Current)** | Delta (Phase 4→5) | Delta (Total) |
|--------|-------------|---------------|----------------------|-------------------|---------------|
| **Total Tests** | 9,875 | 9,875 | 9,875 | — | — |
| **Skipped** | 53 | 53 | 53 | — | — |
| **Passed** | 6,760 | 6,760 | **6,841** | **+81** | **+81** |
| **Failed** | 3,115 | 3,115 | **3,034** | −81 | −81 |
| **Pass Rate** | 68.8% | 68.8% | **69.7%** | +0.9pp | +0.9pp |

**Regressions: 0**  
**Lambda baseline: 419/419 (100%)**  
**Radiant baseline: 1,967/1,967 (100%)**

---

## 2. Fixes Implemented

Five structural fixes were implemented targeting root causes identified through failure analysis. All are spec-conformant changes — no hardcoding or test-specific workarounds.

### Fix 1: Border-width initial value when border-style is set (+45 tests)

**Root Cause:** CSS 2.1 §8.5.1 specifies that `border-width` has an initial value of `medium` (3px). When `border-style: dashed` (or any visible style) is set without an explicit `border-width`, the border should render at 3px. Radiant's `BorderProp` is zero-initialized via `pool_calloc`, so border-width defaulted to 0 — producing invisible borders.

**Fix:** Added a finalization check in `resolve_css_style.cpp`: after all CSS properties are resolved, if a border side has a visible style (not `none`/`hidden`) and width is 0 with specificity 0 (meaning never explicitly set), set width to 3.0f (medium). This is safe because any explicit `border-width: 0` declaration has specificity ≥ 1.

**File:** `radiant/resolve_css_style.cpp` (finalization step)

**Tests recovered (45):**
- `border-style-applies-to-*` (7): 004–007, 009–013
- `border-color-applies-to-*` (10): 004–013
- `border-color-shorthand-*` (4): 001–004
- `border-{top,bottom}-{color,style}-applies-to-011` (4)
- `border-color-006` (1)
- `border-style-001` through `border-style-008` (8)
- `border-top-003` (1)
- `c5512-brdr-rw-*` / `c5512-ibrdr-rw-*` (4): right border-width tests
- `c5514-brdr-lw-*` / `c5514-ibrdr-lw-*` (4): left border-width tests
- `border-bottom-style-applies-to-011`, `border-top-style-applies-to-011` (2)

### Fix 2: Counter format types (`decimal-leading-zero`, `lower-greek`, `armenian`, `georgian`) (+9 tests)

**Root Cause:** The CSS parser recognized `lower-alpha`, `upper-alpha`, `lower-roman`, `upper-roman`, and `disc`/`circle`/`square`, but four additional CSS 2.1 list-style-types were missing: `decimal-leading-zero`, `lower-greek`, `armenian`, and `georgian`. Counters using these types fell back to decimal rendering.

**Fix:** 
- Added 6 new enum values to `css_value.hpp`: `CSS_VALUE_LOWER_LATIN`, `CSS_VALUE_UPPER_LATIN`, `CSS_VALUE_DECIMAL_LEADING_ZERO`, `CSS_VALUE_LOWER_GREEK`, `CSS_VALUE_ARMENIAN`, `CSS_VALUE_GEORGIAN`
- Registered keyword→enum mappings in `css_value.cpp`
- Implemented format functions in `layout_counters.cpp`: `int_to_lower_greek()` (Greek alphabet α-ω), `int_to_armenian()` (Armenian additive numerals), `int_to_georgian()` (Georgian Mkhedruli additive numerals)
- `decimal-leading-zero` uses `%02d` formatting
- Replaced hardcoded hex switch values with enum names for readability

**Files:** `lambda/input/css/css_value.hpp`, `lambda/input/css/css_value.cpp`, `radiant/layout_counters.cpp`

**Tests recovered (9):** `content-counter-006`, `content-counter-011` through `content-counter-015`, `content-counters-011`, `content-counters-012`, `content-counters-015`

### Fix 3: Inline element horizontal margins (+20 tests)

**Root Cause:** `layout_inline.cpp` completely lacked any margin handling for inline elements. The word "margin" did not appear in the file. Per CSS 2.1 §8.3, horizontal margins on inline elements are not collapsed and must affect the element's position. The `inline_left_edge` and `inline_right_edge` calculations only included `border + padding`, omitting margin entirely.

**Fix:** Added `span->bound->margin.left` to `inline_left_edge` and `span->bound->margin.right` to `inline_right_edge` calculations in `layout_inline.cpp`. These values feed into inline box positioning, ensuring inline elements respect their horizontal margins.

**File:** `radiant/layout_inline.cpp`

**Tests recovered (20):**
- `bidi-box-model-*` (10): 010–012, 014–015, 019–021, 023–024
- `c5502-imrgn-r-*` (2): 005, 006 (right margin tests)
- `c5504-imrgn-l-*` (2): 005, 006 (left margin tests)
- `c5505-imrgn-000` (1)
- `text-indent-*-ref-inline-margin` (2): 115, wrap-001
- `first-letter-selector-012`, `ltr-basic`, `white-space-normal-009` (3)

### Fix 4: Root element (`<html>`) margin positioning (+1 test)

**Root Cause:** `layout_html_root()` in `layout.cpp` always positioned the root `<html>` element at x=0, y=0 with full viewport width, ignoring any CSS margins applied to the root element. Per CSS 2.1, the root element participates in normal box model including margins.

**Fix:** After style resolution in `layout_html_root()`, read `html->bound->margin` values and adjust `html->x`, `html->y`, `html->width`, `html->content_width`, and `lycon->block.content_width` accordingly.

**File:** `radiant/layout.cpp`

**Tests recovered (1):** `margin-collapse-001`

### Fix 5: Table cell `::before`/`::after` pseudo-elements (+6 tests)

**Root Cause:** `layout_table_cell_content()` in `layout_table.cpp` had no pseudo-element generation code. `::before` and `::after` pseudo-elements on `<td>`, `<th>`, and `<table>` elements were silently ignored. The pattern for pseudo-element generation existed in `layout_block_inner_content()` but was never replicated for table cells.

**Fix:**
- Added forward declarations for `alloc_pseudo_content_prop()`, `generate_pseudo_element_content()`, and `insert_pseudo_into_dom()` to `layout.hpp`
- Added pseudo-element generation in `layout_table_cell_content()` before the child iteration loop, following the same pattern as `layout_block_inner_content()`

**Files:** `radiant/layout.hpp`, `radiant/layout_table.cpp`

**Tests recovered (6):** `content-052`, `content-081`, `empty-cells-inherited-001`, `quotes-applies-to-005`, `quotes-applies-to-006`, `table-columns-example-002`

---

## 3. Files Changed

| File | Change Summary |
|------|---------------|
| `radiant/resolve_css_style.cpp` | Border-width medium default in finalization step |
| `lambda/input/css/css_value.hpp` | +6 enum values for counter format types |
| `lambda/input/css/css_value.cpp` | +6 keyword→enum registrations |
| `radiant/layout_counters.cpp` | +3 format functions (greek, armenian, georgian), decimal-leading-zero, enum name cleanup |
| `radiant/layout_inline.cpp` | Horizontal margin in inline edge calculations |
| `radiant/layout.cpp` | Root element margin handling in `layout_html_root()` |
| `radiant/layout.hpp` | +3 forward declarations for pseudo-element functions |
| `radiant/layout_table.cpp` | Pseudo-element generation in `layout_table_cell_content()` |

---

## 4. Outstanding Failures — Breakdown (3,034 remaining)

### 4.1 By Category

| Category | Count | Description |
|----------|-------|-------------|
| **other** | 703 | Miscellaneous tests across many CSS properties |
| **applies-to** | 563 | CSS property applied to element types it shouldn't/should affect |
| **first-\*** | 287 | `::first-letter` and `::first-line` pseudo-elements |
| **table-\*** | 256 | Table layout edge cases |
| **border-conflict** | 182 | Border-collapse conflict resolution |
| **c5xxx (legacy)** | 112 | Legacy W3C test naming |
| **float\*** | 94 | Float placement and clearing |
| **font-\*** | 93 | Font resolution, metrics, shorthand |
| **bidi-\*** | 80 | BiDi / direction (completely unimplemented) |
| **text-\*** | 76 | Text layout, transforms, decoration |
| **absolute-\*** | 74 | Absolute positioning equations |
| **margin-collapse** | 71 | Margin collapsing edge cases |
| **border-\*** | 53 | Border shorthand/width/style edge cases |
| **white-space-\*** | 43 | Whitespace processing |
| **content-\*** | 39 | Generated content |
| **vertical-align-\*** | 39 | Vertical alignment in inline contexts |
| **background-\*** | 35 | Background positioning/attachment edge cases |
| **list-\*** | 35 | List-style marker positioning |
| **padding-\*** | 35 | Padding (mostly RTL-dependent) |
| **margin-\*** | 28 | Margin (mostly RTL-dependent) |
| **min/max-\*** | 28 | Min/max width/height interactions |
| **inline-formatting-context** | 19 | Complex inline box splitting |
| **content-counter** | 15 | Counter edge cases |
| **clear-\*** | 12 | Float clearing interactions |
| **empty-cells-\*** | 12 | Empty cell visibility edge cases |
| **z-index-\*** | 11 | Stacking context ordering |
| **fixed-\*** | 9 | Fixed positioning |
| **display-\*** | 6 | Display type edge cases |
| **dynamic-\*** | 5 | Dynamic style changes (not applicable) |
| **overflow-\*** | 5 | Overflow clipping |
| **position-\*** | 5 | Position property edge cases |
| **height-\*** | 4 | Height calculation edge cases |
| **line-height-\*** | 2 | Line-height edge cases |
| **visibility-\*** | 2 | Visibility inheritance edge cases |
| **width-\*** | 1 | Width calculation edge case |

### 4.2 Root Cause Analysis

#### Tier 1 — Systemic Issues (high failure count, shared root cause)

| Root Cause | Est. Failures | Description |
|------------|--------------|-------------|
| **Ahem font metrics** | ~300+ | Many `applies-to-*`, `first-letter-*`, and `letter-spacing-*` tests rely on the Ahem test font (1000-unit-per-em square glyphs). Radiant uses system fonts with different metrics, causing pixel-level mismatches. This is a test infrastructure issue, not a structural CSS bug. |
| **BiDi / `direction` property** | ~120+ | `direction` and `unicode-bidi` are completely unimplemented. Affects all `bidi-*` tests directly, plus `padding-right`, `margin-right`, `border-right-*` tests that validate RTL physical mapping. Requires implementing the Unicode Bidirectional Algorithm (UAX #9). |
| **`::first-letter` edge cases** | ~280 | Float support was fixed in Phase 2 (+6), but remaining failures are from punctuation handling (which characters are part of first-letter), multi-line interaction, inheritance edge cases, and fallback font metrics not feeding into line-height calculation. |
| **Border-collapse conflict resolution** | ~180 | Column/colgroup border sources were added in Phase 1, but complex multi-cell spanning conflicts, `hidden` border propagation across row groups, and collapsed border width feeding into table sizing remain incomplete. |

#### Tier 2 — Feature Gaps (medium count, specific missing features)

| Root Cause | Est. Failures | Description |
|------------|--------------|-------------|
| **Float placement algorithm** | ~90 | Narrow-space fitting, float-next-to-float stacking, and float interaction with block formatting contexts have edge cases. `c414-flt-fit-*` (0% pass) tests specifically target these. |
| **Margin collapsing — advanced** | ~70 | Self-collapsing blocks, clearance interaction with collapsing, and through-flow collapsing (margins passing through empty blocks) are partially handled. |
| **Table anonymous box wrapping** | ~60 | CSS 2.1 §17.2.1 specifies anonymous table box generation for misparented elements. Edge cases with nested anonymous wrappers and interaction with `display: table-*` computed values. |
| **Vertical-align in inline** | ~40 | `vertical-align: baseline` requires tracking baseline positions across nested inline elements. `top`/`bottom` alignment relative to line box needs full line box height calculation first. |
| **Absolute positioning equations** | ~70 | CSS 2.1 §10.3.7/§10.6.4 constraint equations for absolutely positioned replaced/non-replaced elements aren't fully implemented. Over-constrained cases and auto-margin centering in abspos context. |

#### Tier 3 — Low-Priority / Edge Cases

| Root Cause | Est. Failures | Description |
|------------|--------------|-------------|
| **`display: run-in`** | ~15 | Removed from CSS Display Level 3. Phase 2.5 added fallthrough for unsupported run-in, but true run-in behavior requires block→inline reclassification. |
| **Fixed positioning** | ~9 | Should use viewport as containing block, currently falls back to root element. |
| **Z-index stacking contexts** | ~11 | Full stacking context sorting not implemented. `StackingBox` struct exists but is commented out. |
| **Dynamic style changes** | ~5 | Tests that modify styles via script — not applicable to static layout engine. |

---

## 5. Improvement Opportunities — Prioritized

### High ROI (structural fixes, est. +200–400 tests)

| # | Enhancement | Est. Tests | Effort | Description |
|---|-------------|-----------|--------|-------------|
| **H1** | **Ahem font support** | +300 | Medium | Load and use the Ahem test font during CSS 2.1 suite execution. Ahem has perfectly square 1em glyphs, enabling exact pixel comparisons. Most `applies-to` and `first-letter` tests use it. |
| **H2** | **Border-collapse table sizing** | +45–50 | Medium | Collapsed border widths don't correctly feed into table/cell width calculations. The `total_cell_half_borders` subtraction was partially added but needs to handle non-uniform borders and spanning cells. |
| **H3** | **Margin-collapse edge cases** | +30–40 | Medium | Self-collapsing blocks (zero height, no border/padding), clearance, and margin collapsing through empty descendants. |
| **H4** | **Float fitting algorithm** | +20–30 | Medium | Float-next-to-float in narrow spaces, and proper `clear` interaction with float positioning. |

### Medium ROI (feature work, est. +100–200 tests)

| # | Enhancement | Est. Tests | Effort | Description |
|---|-------------|-----------|--------|-------------|
| **M1** | **BiDi / direction** | +120 | Very High | Full Unicode Bidirectional Algorithm. Would fix `bidi-*`, `direction-*`, plus RTL-dependent margin/padding/border tests. High effort due to algorithm complexity. |
| **M2** | **Absolute positioning equations** | +40–50 | High | Implement CSS 2.1 §10.3.7/§10.6.4 fully — auto margins, over-constrained resolution, replaced element constraints. |
| **M3** | **Vertical-align baseline** | +25–30 | High | Track baseline positions across inline box tree. Implement `top`/`bottom` alignment relative to computed line box height. |
| **M4** | **`::first-letter` punctuation** | +30–40 | Medium | Correctly identify which preceding/following punctuation characters are part of `::first-letter`. Use Unicode category lookup. |

### Low ROI (diminishing returns)

| # | Enhancement | Est. Tests | Effort | Description |
|---|-------------|-----------|--------|-------------|
| **L1** | List-style outside marker precision | +10–15 | Low | Improve marker positioning offset calculations for `outside` position. |
| **L2** | White-space processing edge cases | +10–15 | Medium | `pre-wrap`, `pre-line` whitespace collapsing at line boundaries. |
| **L3** | Z-index stacking contexts | +5–10 | Medium | Implement proper stacking context sorting. |
| **L4** | `display: run-in` | +10–15 | Medium | CSS 2.1-only feature, removed in CSS3. Low priority. |

---

## 6. Historical Progress

| Phase | Date | Passed | Delta | Key Changes |
|-------|------|--------|-------|-------------|
| Pre-Phase 1 | — | 6,161 | — | Baseline |
| Phase 1 | 2025-02-21 | 6,591 | +430 | Negative margins, content functions, column borders, font-variant, text-align, font-family |
| Phase 2 | 2025-02-22 | 6,591 | +0 | First-letter float, replaced intrinsic sizing, list-style reassessment |
| Phase 2.5 | 2025-02-22 | 6,622 | +31 | Run-in fallthrough, empty-cells inheritance |
| Phase 3 | 2025-02-23 | 6,760 | +138 | Monospace 13px, CoreText fallback, border-collapse numerics, margin-collapse, text merging, attr() case |
| **Phase 5** | **2026-02-22** | **6,841** | **+81** | **Border-width medium, counter formats, inline margins, root margins, table pseudo-elements** |
| **Cumulative** | | | **+680** | **6,161 → 6,841 (62.4% → 69.7%)** |
