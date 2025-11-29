# Radiant Table Layout Enhancement Plan (Phase 6)

## Executive Summary

The table layout test suite shows a **96.8% failure rate** (416/430 tests failed). After detailed analysis comparing Lambda's JSON output (which correctly converts to absolute coordinates) with browser references, the root causes are identified as algorithmic issues in width calculation, font size inheritance, and whitespace handling.

## Test Results Summary

| Test Category | Total | Passed | Failed | Pass Rate |
|---------------|-------|--------|--------|-----------|
| basic_6XX (colspan/rowspan) | 6 | 0 | 6 | 0% |
| table-anonymous-block-XXX | 19 | 1 | 18 | 5.3% |
| table-anonymous-objects-XXX | 210 | 0 | 210 | 0% |
| table-height-algorithm-XXX | 32 | 0 | 32 | 0% |
| table-visual-layout-XXX | 25 | 6 | 19 | 24% |
| table_XXX (basic) | 20 | 2 | 18 | 10% |
| Other | 118 | 5 | 113 | 4.2% |
| **Total** | **430** | **14** | **416** | **3.2%** |

## Detailed Failure Analysis

### Issue 1: Font Size Compounding (Critical - 50% of failures)

**Symptom:** Cell font-size is 256px instead of 32px

**Evidence from `table-anonymous-objects-001.htm`:**
```
CSS chain: body(16px) → div(2em=32px) → span(display:table)
Browser: font-size = 32px
Lambda:  font-size = 256px (8x expected!)
```

**Root Cause:** The `font-size: 2em` is being applied multiple times in the inheritance chain. When layout processes the table, it appears to:
1. Inherit 32px from parent
2. Re-apply the 2em multiplier again during table layout
3. Possibly re-apply during cell content layout

**Location:** `radiant/layout_table.cpp` in `layout_table_cell_content()` and `measure_cell_intrinsic_width()`

**Fix:** Ensure font context is properly saved/restored without re-applying inherited em values.

### Issue 2: Intrinsic Width Explosion (Critical - 50% of failures)

**Symptom:** Table width is 7516px instead of ~155px

**Evidence:**
```
Browser reference:
  span[display:table] width: 154.66px
  span[display:table-cell] width: 77.33px

Lambda output:
  span[display:table] width: 7516px
  span[display:table-cell] width: 3755px
```

**Root Cause:** In `measure_cell_intrinsic_width()`:
1. Uses infinite constraint width (10000px) for measurement
2. Font size is 256px (from Issue 1)
3. Text "Cell 1" measures at 341px per character with wrong font
4. Two cells × 3755px = 7510px + borders = 7516px

**The chain reaction:**
1. Wrong font size (256px instead of 32px)
2. Text measurement uses huge font → huge preferred width
3. Auto table width = sum of preferred widths
4. Result: 50x expected width

**Fix:**
1. First fix font size inheritance
2. Ensure `setup_font()` in PCW measurement uses correct inherited font, not re-computed font

### Issue 3: Whitespace in Text Content (Major)

**Symptom:** Text content includes trailing whitespace/newlines

**Evidence:**
```
Lambda: content: "Cell 1\n          " (17 chars)
Browser: text: "Cell 1" (6 chars)
```

**Root Cause:** The table cell text extraction includes significant whitespace from the HTML source.

**Location:** Text node creation in `lambda/input/input-html.cpp` or `build_ast.cpp`

**Fix:** Normalize whitespace in table cell text content.

### Issue 4: Minor Column Width Discrepancies (Minor)

**For tests that are "close" (basic_603, etc.):**

| Metric | Browser | Lambda | Delta |
|--------|---------|--------|-------|
| Table width | 206.52px | 207px | +0.48px |
| Cell 1 width | 103.25px | 104px | +0.75px |
| Cell 2 x | 119.25px | 120px | +0.75px |

**Root Cause:** Rounding differences in CSS 2.1 column width distribution algorithm.

**Priority:** Low - these are within acceptable tolerance after major issues fixed.

### Issue 5: Vertical Alignment Text Positioning (Moderate)

**Symptom:** Text positioned at y:9 (top) instead of y:43 (middle)

**Evidence from rowspan test:**
```
Browser: text "Tall cell" at y:43 (centered in 72px cell)
Lambda:  text "Tall cell" at y:9 (top-aligned after padding)
```

**Root Cause:** `apply_cell_vertical_alignment()` is called but doesn't properly offset text for `vertical-align: middle`.

**Location:** `radiant/layout_table.cpp` line 888-951

### Issue 6: Row Height Calculation for Rowspan (Moderate)

**Symptom:** Table dimensions are correct but cell positions vary

**Evidence:**
```
Browser: td:2 (col B) at x:86.47
Lambda:  td:2 (col B) at x:78
```

**Root Cause:** Column width calculation doesn't account for rowspan cells correctly when the spanning cell is wider than non-spanning cells.

---

## Root Cause Summary

| Issue | Impact | Affected Tests | Fix Complexity |
|-------|--------|---------------|----------------|
| Font size compounding | 50% | 210+ | Medium |
| Width explosion | 50% | 210+ | Medium (chained to #1) |
| Whitespace in text | 10% | ~40 | Easy |
| Vertical alignment | 5% | ~30 | Easy |
| Column width precision | 2% | ~20 | Low priority |
| Rowspan positioning | 3% | ~15 | Medium |

---

## Enhancement Plan

### Phase 1: Fix Font Size Inheritance (Week 1)

**Goal:** Eliminate font size compounding in table layout

**Tasks:**
1. Audit `layout_table_cell_content()` - ensure font context saved before and restored after
2. Audit `measure_cell_intrinsic_width()` - ensure it uses inherited font, not re-computed
3. Check `setup_font()` calls - verify em values aren't re-applied
4. Add unit test for `font-size: 2em` in nested table context

**Files:**
- `radiant/layout_table.cpp`: lines 953-1034, 1045-1160
- `radiant/layout.cpp`: `setup_font()` function

**Validation:**
```bash
make layout test=table-anonymous-objects-001
# Expected: font-size should be 32, not 256
```

### Phase 2: Fix Whitespace Handling (Week 1)

**Goal:** Normalize text content in table cells

**Tasks:**
1. In table cell text extraction, collapse consecutive whitespace to single space
2. Trim leading/trailing whitespace from cell text content
3. Verify this doesn't break pre-formatted text (respect `white-space` property)

**Files:**
- `lambda/input/input-html.cpp` or `radiant/layout_table.cpp`

**Validation:**
```bash
make layout test=table-anonymous-objects-001
# Expected: text content should be "Cell 1", not "Cell 1\n          "
```

### Phase 3: Validate Width Calculation (Week 2)

**Goal:** With font fixed, verify width algorithm produces correct results

After Phase 1+2, re-run tests. The width explosion should be fixed as a side effect.

**If still failing:**
1. Review `measure_cell_intrinsic_width()` algorithm
2. Compare PCW calculation with CSS 2.1 spec Section 17.5.2
3. Add debug logging for width contribution from each cell

### Phase 4: Fix Vertical Alignment (Week 2)

**Goal:** Center text vertically in table cells

**Tasks:**
1. In `apply_cell_vertical_alignment()`, calculate actual content height
2. Compute correct y-offset for middle alignment: `(cell_height - content_height) / 2`
3. Apply offset to text children after cell height is finalized

**Files:**
- `radiant/layout_table.cpp`: lines 888-951

**Validation:**
```bash
make layout test=basic_604_table_rowspan
# Expected: "Tall cell" text should be vertically centered
```

### Phase 5: Column Width Precision (Week 3)

**Goal:** Match browser column width distribution exactly

**Tasks:**
1. Review CSS 2.1 Section 17.5.2.2 column width distribution
2. Use floating-point arithmetic throughout, round only at final pixel assignment
3. Handle remainder distribution consistently (round-robin to columns)

**Files:**
- `radiant/layout_table.cpp`: `table_auto_layout()` function

### Phase 6: Rowspan/Colspan Edge Cases (Week 3)

**Goal:** Handle complex spanning scenarios correctly

**Tasks:**
1. Fix rowspan height distribution across spanned rows
2. Fix colspan width when spanning columns have different widths
3. Handle cells that span both rows and columns

---

## Success Metrics

| Phase | Target Pass Rate | Key Validation |
|-------|------------------|----------------|
| Current | 3.2% | - |
| Phase 1-2 | 50% | anonymous-objects tests pass |
| Phase 3 | 65% | basic table tests pass |
| Phase 4 | 75% | height-algorithm tests pass |
| Phase 5-6 | 90% | visual-layout tests pass |

## Implementation Notes

### Font Context Management

The key insight is that table layout should NOT re-apply computed styles. The font context should be:

```cpp
// CORRECT pattern
void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    // Save CURRENT font (already has inherited values)
    FontBox saved_font = lycon->font;

    // Use cell's computed font directly - DON'T re-resolve em units
    if (cell->font) {
        lycon->font = *cell->font;  // Direct copy, not setup_font()
    }

    // Layout content...

    // Restore
    lycon->font = saved_font;
}
```

### PCW Measurement Context

```cpp
// CORRECT pattern
int measure_cell_intrinsic_width(LayoutContext* lycon, ViewTableCell* cell) {
    FontBox saved_font = lycon->font;

    // Use cell's COMPUTED font, not style font
    // The cell->font should already have resolved font-size in pixels
    if (cell->font && cell->font->font_size > 0) {
        lycon->font.font_size = cell->font->font_size;  // Already resolved
    }

    // Measure...

    lycon->font = saved_font;
}
```

---

## Quick Wins

1. **Add font-size logging** in table layout to diagnose inheritance chain
2. **Log PCW values** before and after cell measurement
3. **Create minimal repro test** for font-size compounding

```html
<!-- Minimal test case for font-size compounding -->
<div style="font-size: 2em">
  <span style="display: table">
    <span style="display: table-cell">X</span>
  </span>
</div>
<!-- Expected: X should be 32px (2em × 16px) not 256px -->
```
