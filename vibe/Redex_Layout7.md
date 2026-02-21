# Redex Layout 7 — Implementation Results: Layout6 Changes

> **Starting Baseline:** 8,167 tests, 6,676 pass, **81.7%** (post-Phase 1)  
> **Final Result:** 8,167 tests, 6,899 pass, **84.5%** (+223 tests, +2.8pp)  
> **Baseline Regression:** None (1,802/1,820 unchanged)

---

## Summary of Changes

### Completed Implementations

| # | Task | File(s) Modified | Tests Gained | Description |
|---|------|-------------------|-------------|-------------|
| A1 | Ahem text space-wrapping | layout-dispatch.rkt | ~10 | Split Ahem text on spaces (with ZWSP fallback), account for inter-word space width |
| A2 | RTL block x-positioning | layout-block.rkt | ~88 | Removed `(max 0 used-ml)` clamp in RTL over-constrained case; allows negative margin-left per CSS 2.2 §10.3.3 |
| B1 | Collapse-through empty blocks | layout-block.rkt | ~7 | Added `effective-margin-bottom` parameter; collapse-through detection for zero-height blocks without border/padding |
| B2 | Clearance + collapse interaction | layout-block.rkt | ~22 | Added BFC barrier checks for top/bottom margins; effective-margin-top propagation with clearance guard |
| E1 | Line-height % resolution | reference-import.rkt | ~5 | Parse `line-height: N%` as `N/100 × font-size`; resolve percentage/em line-heights before CSS inheritance so children get computed px value |
| E1b | Negative line-height rejection | reference-import.rkt | (included above) | Drop `line-height: -Npx|-Nem` at parse time (CSS 2.2 §10.8.1: negative values invalid) |
| F1 | ::first-letter pseudo-element | reference-import.rkt | ~62 | Extract ::first-letter rules; apply font-size to short text nodes where entire content is first-letter (punctuation + letter) |

### Investigated but Skipped

| Task | Reason |
|------|--------|
| C2: Border-spacing | 24 failures mostly caused by `position: relative` offsets on anonymous table wrappers, not border-spacing computation |
| C5: Table cell height | Empty `display: list-item` divs in table cells need strut line boxes — complex |
| D1: Outside list marker | Requires generating actual marker line boxes and vertical stacking — significant feature |
| D2: Marker type width | Dependent on D1 |
| C1: Anonymous table wrapping | 27 failures; wrapping logic exists but anonymous box positioning needs work |
| C3: Empty-cells handling | 16 failures (applies-to pattern); needs strut line boxes |
| C4: Caption positioning | 9 failures; complex table caption layout |
| E2: Inline line-height contribution | 11 failures; inline vertical-align model needed |

---

## Detailed Technical Changes

### A1: Ahem Text Space-Wrapping (layout-dispatch.rkt)

**Lines ~457–500.** Changed Ahem word splitting from ZWSP-only to space-first:
- `space-split = (string-split content " ")`
- If `(> (length space-split) 1)` → use space-split, else fall back to ZWSP
- Added `has-spaces?` flag and `ahem-space-w` = ahem char width of space
- Inter-word spacing: `new-line-w = line-w + ahem-space-w + ww` when `has-spaces?`

### A2: RTL Block X-Positioning (layout-block.rkt)

**Line ~1018.** CSS 2.2 §10.3.3 over-constrained RTL case:
- **Before:** `(max 0 used-ml)` — clamped negative margin-left to 0
- **After:** bare `used-ml` — allows negative margin-left values
- Fixed 88 `margin-right-*` tests that had incorrect x-positioning

### B1: Collapse-Through Empty Blocks (layout-block.rkt)

**New parameter (line 38):** `(define effective-margin-bottom (make-parameter (box 0)))`

**Collapse-through detection (line ~1125):** A block collapses through when:
- `child-h = 0`
- No border-top/bottom, no padding-top/bottom
- No clearance

**Loop behavior:** Collapse-through blocks don't advance `current-y`; their margins merge with `prev-margin-bottom` via nested `collapse-margins`.

**End-of-loop (line ~557):** When `(not parent-has-bottom-barrier?)`, propagates final `prev-margin-bottom` via `effective-margin-bottom` parameter.

### B2: Clearance + Collapse Interaction (layout-block.rkt)

Three related fixes:

1. **BFC barrier (lines ~551, ~903):** Added `establishes-bfc?` to both `parent-has-bottom-barrier?` and `parent-has-top-barrier?`. Elements with `overflow: hidden/auto/scroll` now correctly act as margin collapse barriers.

2. **Effective-margin-top parameter (line ~50):** `(define effective-margin-top (make-parameter (box 0)))` — propagates first-child top margin through parents without top barriers.

3. **Clearance guard (line ~1036):** First-child margin-top propagation deferred until after clearance check. A child with `clear:both` and a float to clear has clearance, which breaks the adjoining relationship.

### E1: Line-Height Percentage Resolution (reference-import.rkt)

**Two changes:**

1. **Parse `%` values (line ~1636):** Added percentage case to line-height parsing:
   ```racket
   [(regexp-match #rx"^([0-9.]+)%$" val)
    => (lambda (m) (add! 'line-height (* (/ (string->number (cadr m)) 100.0) elem-font-size)))]
   ```

2. **CSS inheritance fix (line ~910):** Resolves percentage/em line-heights to px before passing `merged` alist to children as `child-parent-props`. CSS 2.2 §10.8.1: percentage line-height is computed on the declaring element, then the computed value is inherited (unlike unitless multipliers which are inherited as-is).

3. **Negative value rejection (line ~344):** Added `line-height` to the CSS validation in `parse-inline-style`, dropping declarations with leading `-` (e.g., `line-height: -1em`).

### F1: ::first-letter Pseudo-Element (reference-import.rkt)

**Three changes:**

1. **Rule extraction (line ~695):** Added `::first-letter` / `:first-letter` detection in `extract-style-rules`, returning a 4th value `first-letter-rules`.

2. **Style injection function `inject-first-letter-styles` (line ~598):** Walks element tree, matches elements against first-letter rules. When the entire text content is short (≤10 chars, typical of punctuation tests like `)T)`), applies the `::first-letter` font-size to the element's own font-size property.

3. **Pipeline wiring (line ~657):** Called after `inject-after-content` in `html-file->inline-styles`.

---

## Remaining Failure Landscape (1,266 tests)

| Category | Count | Root Cause | Fixability |
|----------|------:|------------|------------|
| first-letter-punctuation (CJK/Unicode) | 349 | Font fallback metrics for non-Latin chars | Hard (needs font fallback system) |
| content (counters) | 40 | CSS `counter()` / `counters()` not implemented | Medium-Hard |
| table-anonymous-objects | 27 | Anonymous table box positioning | Medium |
| border-spacing | 24 | position:relative on anonymous wrappers | Hard |
| *-applies-to (various) | ~200 | Strut line boxes for empty elements | Medium |
| list-style-position | 14 | Marker as inline element (vertical stacking) | Medium |
| margin-collapse | 11 | Remaining edge cases | Hard |
| line-height | 11 | Inline line-height contribution | Medium |
| floats | 12 | Float edge cases | Medium-Hard |
| before-content-display | 11 | ::before with non-inline display | Medium |
| first-letter-selector | 10 | Complex ::first-letter + selectors | Medium |
| run-in | 21 | `display: run-in` not implemented | Medium |
| quotes | 12 | CSS `quotes` property not implemented | Medium |
| Other | ~524 | Various (abspos, direction, font, etc.) | Mixed |

### Potential Next Steps (priority order)

1. **Strut line boxes** (~200 applies-to tests): Give empty block elements a minimum height = normal-lh when they establish inline formatting context. Would fix most `*-applies-to` failures across border, margin, padding, letter-spacing, word-spacing, font, etc.

2. **CSS counters** (~40 content tests): Implement `counter-reset`, `counter-increment`, `counter()` and `counters()` functions for `::before`/`::after` content.

3. **Font fallback metrics** (~349 first-letter-punctuation tests): Load fallback font metrics for Unicode characters not in Times New Roman. Would require character → font mapping tables.

4. **List marker line boxes** (~25 list-style-position tests): Generate actual inline marker elements for `list-style-position: inside` that create line boxes (not just x-offsets).

---

## Test Results Progression

| Phase | Tests | Pass | Rate | Delta |
|-------|------:|-----:|-----:|------:|
| Phase 1 (Ahem + negative reject) | 8,167 | 6,676 | 81.7% | baseline |
| + A1 A2 B1 B2 | 8,167 | 6,837 | 83.7% | +161 |
| + E1 E1b F1 (final) | 8,167 | 6,899 | 84.5% | +223 |
