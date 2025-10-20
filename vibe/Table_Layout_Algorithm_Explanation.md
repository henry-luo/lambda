# Table Layout Algorithm - Two-Pass System Explained

**Date:** October 20, 2025
**Status:** ✅ `table-layout` CSS property detection FIXED
**Current State:** Baseline 78/78 (100%), Table 5/48 (10.4%)

---

## Recent Updates (October 20, 2025)

### ✅ FIXED: `table-layout` CSS Property Detection

The `table-layout` CSS property is now properly detected and applied:

**Issues Fixed:**
1. **String comparison bug** - Changed from `strcmp()` to `strncmp()` with length checks
   - Custom property values in lexbor are NOT null-terminated
   - Must use `strncmp(value_str, "fixed", 5)` instead of `strcmp(value_str, "fixed")`

2. **Heuristic override bug** - Modified `resolve_table_properties()` to respect CSS values
   - Added early return if `table->table_layout == TABLE_LAYOUT_FIXED`
   - CSS-declared values now take precedence over heuristics
   - Heuristic only applies when CSS doesn't specify `table-layout`

**Code Location:** `resolve_style.cpp` lines 1730-1741 (LXB_CSS_PROPERTY__CUSTOM case)

**Result:**
- Tables with `table-layout: fixed` now correctly use fixed layout algorithm
- Tables with `table-layout: auto` explicitly set use auto layout
- Tables without the property use heuristic (width+height → fixed, else auto)
- Baseline tests: 78/78 ✅ (fully restored)

---

## The Two-Pass Problem Remains

**Problem:** Text is laid out twice, first with wrong width (line.right=-2) then with correct width (line.right=54)

---

## The Table Layout Algorithm Steps

The table layout follows a **two-pass system** required by CSS table layout specification:

### Pass 1: Build Table Tree & Initial Measurement (lines 226-500)

**Function:** `build_table_tree(LayoutContext* lycon, DomNode* tableNode)`

This pass builds the table structure and does **initial content layout for measurement**:

```
1. Create table view structure
2. Process table children (caption, thead, tbody, tfoot, tr)
3. For each row:
   3.1. Create row view
   3.2. For each cell in row:
        3.2.1. Create cell view
        3.2.2. ⚠️ LAYOUT CELL CONTENT (FIRST TIME - lines 384-387)
               - cell->width = 0 at this point!
               - lycon->block.width = cell->width - 2 = 0 - 2 = -2
               - lycon->line.right = -2
               - This measures content to determine minimum cell widths
        3.2.3. Store cell in row
   3.3. Store row in table
4. Return table structure
```

**Why layout content here?**
- Need to measure text/content dimensions to calculate minimum column widths
- Auto layout algorithm needs to know how much space content requires
- This is a **measurement pass**, not final positioning

**The Problem:**
- Cell widths are not yet calculated (cell->width = 0)
- Content gets laid out with wrong parent width
- Text receives `line.right = -2` (unusable)
- Creates ViewText objects with incorrect dimensions

---

### Pass 2: Auto Layout Algorithm (lines 677-1900)

**Function:** `table_auto_layout(LayoutContext* lycon, ViewTable* table)`

This pass calculates actual table dimensions and positions:

```
1. Count columns and rows (lines 708-747)

2. Measure minimum column widths (lines 749-900)
   - For each cell: measure_cell_min_width()
   - Uses already-laid-out content dimensions
   - Determines col_min[] array

3. Calculate column widths based on table layout mode:
   - Fixed layout: Equal distribution or explicit widths
   - Auto layout: Content-based with table width constraint

4. Apply column widths to cells (lines 1400-1600)
   4.1. For each row:
        4.1.1. For each cell:
               - Calculate cell_width from col_widths[] array
               - cell->width = sum of spanned columns
               - ✅ NOW cell->width has correct value!
               - 🔄 RE-LAYOUT CELL CONTENT (SECOND TIME - line 1474)
                  Call: layout_table_cell_content(lycon, cell)
                  - cell->width = 55 (correct!)
                  - content_width = 55 - 2 = 53
                  - lycon->line.right = 53 (correct!)
                  - Clears old children (line 545)
                  - Re-layouts content with correct width

5. Calculate row heights based on content

6. Position cells within rows

7. Handle rowspan/colspan

8. Apply vertical alignment

9. Set final table dimensions
```

---

## Why Two Passes Are Necessary

This is **mandated by CSS table layout specification**:

### Auto Layout Algorithm Requirements:

1. **Cannot determine column widths without measuring content**
   - Need to know minimum width content requires
   - Need to know preferred width (no wrapping)
   - This requires laying out content

2. **Cannot lay out content without knowing column widths**
   - Text wrapping depends on available width
   - Block children need parent width for percentage calculations
   - This requires calculated column widths

3. **Circular dependency resolved by two passes:**
   ```
   Pass 1: Layout content → Measure → Get minimum widths
   Pass 2: Calculate widths → Re-layout content with correct widths
   ```

### Fixed Layout vs Auto Layout:

**Fixed Layout:**
- Column widths from CSS or equal distribution
- Could theoretically skip Pass 1 measurement
- But implementation uses same code path

**Auto Layout:**
- MUST measure content first
- Uses measurement to determine optimal column widths
- No way around two-pass system

---

## The Current Problem

### Debug Output Shows:

```bash
# PASS 1 - During build_table_tree()
layout_text: text='This content...', line.left=0.000000, line.right=-2.000000, line.advance_x=0.000000

# PASS 2 - During table_auto_layout() after layout_table_cell_content()
layout_text: text='This content...', line.left=1.000000, line.right=54.000000, line.advance_x=1.000000
```

### What's Happening:

1. **Pass 1 (Measurement):**
   - Cell width = 0 (not yet calculated)
   - Content width = 0 - 2 = -2
   - line.right = -2
   - Text laid out with unusable width
   - Creates ViewText objects that are positioned wrong

2. **Pass 2 (Final Layout):**
   - Column widths calculated: col_widths[0] = 55
   - Cell width set: cell->width = 55
   - Content width = 55 - 2 = 53
   - line.right = 54 (correct!)
   - `layout_table_cell_content()` tries to clear old children (line 545)
   - Re-layouts text with correct width
   - Creates NEW ViewText objects with correct dimensions

### The Bug:

The clearing mechanism at line 545-551 in `layout_table_cell_content()`:

```cpp
// Clear existing children (they were laid out with wrong parent width)
// We need to save and clear the child list, then re-layout from DOM
((ViewGroup*)cell)->child = nullptr;
if (cell->first_child) {
    // Note: Not freeing memory here, assuming it will be garbage collected later
    // or that the layout system handles this
    cell->first_child = nullptr;
}
```

**This may not be sufficient because:**
1. ViewText objects from Pass 1 might still exist in memory
2. The view pool might not properly recycle them
3. Links between views might not be fully cleared
4. The old ViewText objects might interfere with rendering

---

## Solutions

### Option 1: Skip Pass 1 Content Layout (RECOMMENDED)

**Idea:** Don't lay out cell content during `build_table_tree()`, only measure minimum width from DOM.

**Implementation:**
```cpp
// In build_table_tree(), lines 384-387, REPLACE:
for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
    layout_flow_node(lycon, cc);
}

// WITH: Direct measurement without layout
int min_width = measure_cell_content_min_width_from_dom(cellNode);
cell->min_width = min_width;
```

**Pros:**
- ✅ Eliminates double layout
- ✅ No wrong-width ViewText objects created
- ✅ Cleaner, more efficient
- ✅ Matches browser behavior more closely

**Cons:**
- ⚠️ Need to implement `measure_cell_content_min_width_from_dom()`
- ⚠️ May not handle complex content (nested blocks, images)

---

### Option 2: Improve Pass 1 Layout with Better Width Estimate

**Idea:** During Pass 1, use a reasonable width estimate instead of 0.

**Implementation:**
```cpp
// In build_table_tree(), line 372, CHANGE:
lycon->block.width = cell->width - 2; // subtract border

// TO:
// Use table width or viewport width as estimate
int estimated_width = 600; // default
if (table->width > 0) {
    estimated_width = table->width / estimated_column_count;
}
lycon->block.width = estimated_width;
lycon->line.right = estimated_width;
```

**Pros:**
- ✅ Pass 1 layout has usable width
- ✅ Text wrapping can happen during measurement
- ✅ More accurate measurement

**Cons:**
- ⚠️ Still double layout (wasteful)
- ⚠️ Estimate might be wrong
- ⚠️ Complex to get right column count estimate

---

### Option 3: Better Child Clearing in Pass 2

**Idea:** Ensure all Pass 1 views are properly destroyed before Pass 2.

**Implementation:**
```cpp
// In layout_table_cell_content(), lines 545-551, REPLACE:
((ViewGroup*)cell)->child = nullptr;
if (cell->first_child) {
    cell->first_child = nullptr;
}

// WITH: Proper recursive cleanup
free_view_tree((ViewGroup*)cell);
((ViewGroup*)cell)->child = nullptr;
cell->first_child = nullptr;
```

**Pros:**
- ✅ Ensures old views don't interfere
- ✅ Proper memory management

**Cons:**
- ⚠️ Still wastes CPU on double layout
- ⚠️ Need to implement `free_view_tree()`
- ⚠️ May cause issues if views are pooled

---

## Recommended Solution: Text Node Consolidation (NOT Width Estimation)

### ❌ Abandoned Approach: Pass 1 Width Estimation

**What was tried:**
```cpp
// ABANDONED - This broke baseline tests
int estimated_cell_width = 200; // fallback
if (table->width > 0) {
    int estimated_cols = 3;
    estimated_cell_width = (table->width - 20) / estimated_cols;
}
lycon->block.width = estimated_cell_width;
```

**Why it failed:**
- Broke `table_simple_007_percentage_width` test (11.1% match)
- Auto layout tables that were working started failing
- Width estimation interfered with proper column width calculation
- The real problem is text nodes are split by word, not the measurement width

**Lesson learned:** Width estimation in Pass 1 is a band-aid that causes regressions. The root cause is text node splitting, not measurement width.

---

### ✅ Correct Approach: Text Node Consolidation (FUTURE)

**The Real Problem:**
- DOM parsing creates separate text nodes for each word: `["This ", "content ", "should "]`
- Each `layout_text()` call only sees one word
- Line breaking logic exists but can't break across nodes
- Pass 1 width (even if correct) won't help because text is already split

**The Real Solution:**
Consolidate adjacent text nodes **before** layout (see `Text_Wrapping_Implementation_Plan.md`):

```cpp
// In layout_table_cell_content() - BEFORE laying out children
consolidate_text_nodes(tcell->node);

// Then layout with correct width
for (DomNode* cc = tcell->node->first_child(); cc; cc = cc->next_sibling()) {
    layout_flow_node(lycon, cc);  // Now text nodes contain full sentences
}
```

**Expected impact:** +20-25 passing tests (from 5/48 to 25-30/48)

---

## Expected Impact

### With Fix:
- ✅ Text gets reasonable width even in Pass 1
- ✅ Line breaking can work during measurement
- ✅ Pass 2 re-layout produces correct final result
- ✅ Expected: 20-25 more tests passing

### Test Results Prediction:
- Before: 5/48 passing (10.4%)
- After: 25-30/48 passing (52-62%)
- Improvement: +20-25 tests (mostly text wrapping issues)

---

## Code Locations Summary

| Location | Line | Purpose | Current State |
|----------|------|---------|---------------|
| `build_table_tree()` | 226-500 | Pass 1: Structure + Measurement | ⚠️ Lays out with width=-2 |
| Cell content layout | 384-387 | Pass 1: Initial layout | ⚠️ Wrong width |
| `table_auto_layout()` | 677-1900 | Pass 2: Calculate widths | ✅ Working |
| `layout_table_cell_content()` | 502-590 | Pass 2: Re-layout | ⚠️ Clearing incomplete |
| Cell width assignment | 1466 | Sets correct width | ✅ Working |
| Re-layout call | 1474 | Pass 2: Final layout | ✅ Working |

---

## Next Steps

1. ✅ **Understand the problem** (DONE - This document)
2. ⏭️ **Implement quick fix** - Better width estimate in Pass 1
3. ⏭️ **Test** - Run table test suite
4. ⏭️ **Verify** - Check if text wrapping works
5. ⏭️ **Measure** - Count passing tests

**Implementation Time:** 30 minutes
**Expected Improvement:** +20-25 passing tests
