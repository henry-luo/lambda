# Radiant Table Layout Enhancement Plan

**Date:** October 20, 2025
**Analysis of:** `radiant/layout_table.cpp`
**Test Suite Results:** `make test-layout suite=table` (0/48 passing)

---

## Executive Summary

The Radiant table layout engine has a solid foundation but suffers from systematic issues that prevent browser-accurate rendering. After analyzing test results and code, I've identified **7 critical problem areas** that explain the 0% pass rate across 48 table tests.

**Key Finding:** The layout engine has the right structure (grid system, colspan/rowspan support, border models) but fails in:
1. **Text positioning** (relative coordinate system issues)
2. **Column width calculation** (minor measurement errors compound)
3. **Display property handling** (CSS `display: table-*` not fully integrated)
4. **Cell padding application** (inconsistent box model)
5. **Caption positioning** (incorrect height and Y-offset)
6. **Inline text wrapping** (missing text flow implementation)
7. **Fixed layout algorithm** (incomplete CSS width resolution)

---

## Test Results Analysis

### Overall Statistics
- **Total Tests:** 48
- **Passing:** 0 (0%)
- **Failing:** 48 (100%)
- **Common Pattern:** Element structure often correct (10-100% match), but text positioning always fails (0%)

### Key Test Cases Analyzed

#### 1. **table_001_simple_table.html** (100% element match, 0% text match)
```
Browser: Table 59.13px × 72px
Radiant: Table 62px × 72px (2.9px wider - acceptable)

Browser: Cell "A" at (25, 25)
Radiant: Cell "A" at (16, 16) (9px offset - FAIL)
```

**Issue:** Text positioned at cell corner (16,16) instead of with padding offset (25,25).
**Root Cause:** Text positioning doesn't account for cell padding properly.

#### 2. **table_vertical_alignment.html** (0% match)
```
Browser: Table 747.67px × 572px
Radiant: Table 299px × 420px (448.7px narrower - FAIL)

Browser: Cell "This content should..." shows full wrapped text
Radiant: Cell shows "This " only (text not wrapping)
```

**Issues:**
- Column widths drastically too narrow (299px vs 747px)
- Inline text not wrapping within cell boundaries
- Vertical alignment offsets not applied

#### 3. **table_003_fixed_layout.html** (0% match)
```html
<div class="container" style="display: table; table-layout: fixed; width: 400px;">
```

```
Browser: Container 414px × 300px (respects width + border + padding)
Radiant: Container 404px × 146px (wrong dimensions)

Browser: Cell heights 123px (distributed from container height)
Radiant: Cell heights 40px (content-based, ignoring fixed layout)
```

**Issues:**
- CSS `display: table` property not triggering table layout
- Fixed layout algorithm not distributing container height to rows
- Border-spacing calculation errors

---

## Code Architecture Review

### Strengths ✅
1. **Clean separation of concerns** - Parser, layout engine, grid system well-organized
2. **Comprehensive colspan/rowspan support** - Grid occupancy matrix works correctly
3. **Border model implementation** - Both collapse and separate modes handled
4. **CSS integration** - Proper use of lexbor for style resolution
5. **Relative positioning system** - Correct concept (cell → row → tbody → table)

### Critical Issues ❌

---

## Problem 1: Text Positioning (Affects 100% of tests)

### Current Behavior
```cpp
// layout_table.cpp:1285-1313
for (View* text_child = ((ViewGroup*)cell)->child; text_child; text_child = text_child->next) {
    if (text_child->type == RDT_VIEW_TEXT) {
        ViewText* vt = (ViewText*)text_child;
        // Text positioned relative to cell at (0,0) - ignoring padding!
        log_debug("Text child in cell - originally at (%d,%d)", vt->x, vt->y);
        // No padding offset applied
    }
}
```

**Test Evidence:**
- Simple table: Text at (16,16), should be at (25,25) - **9px padding missing**
- All tests show text 8-13px offset from browser position

### Root Cause
1. **Cell padding not applied to text position** - Text laid out relative to cell border edge
2. **Line box positioning incorrect** - `lycon->line.left` starts at 0 instead of padding offset
3. **Layout context not initialized** - When calling `layout_flow_node()` for cell content

### Solution
```cpp
// In layout_table_cell_content()
void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    ViewTableCell* tcell = static_cast<ViewTableCell*>(cell);

    // Calculate content area START position (after border + padding)
    int content_start_x = 1; // 1px left border
    int content_start_y = 1; // 1px top border

    if (tcell->bound) {
        content_start_x += tcell->bound->padding.left;
        content_start_y += tcell->bound->padding.top;
    }

    // Set up line box with correct padding offset
    lycon->line.left = content_start_x;
    lycon->line.right = cell->width - 1 - (tcell->bound ? tcell->bound->padding.right : 0);
    lycon->line.advance_x = content_start_x; // Start text here!

    // Position block content
    lycon->block.x = content_start_x;
    lycon->block.y = content_start_y;

    // Re-layout children with correct offsets
    for (DomNode* cc = tcell->node->first_child(); cc; cc = cc->next_sibling()) {
        layout_flow_node(lycon, cc);
    }
}
```

**Expected Impact:** Fix text positioning in all 48 tests (primary failure mode)

---

## Problem 2: Column Width Calculation (Affects width-sensitive tests)

### Current Behavior
```cpp
// layout_table.cpp:519-547
int measure_cell_min_width(ViewTableCell* cell) {
    int content_width = 0;

    // Measure content width
    for (View* child = ((ViewGroup*)cell)->child; child; child = child->next) {
        if (child->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            child_width = text->width + 2; // +2 for text margin
        }
    }

    // Add padding and border
    total_width = content_width + padding + 2;

    return total_width; // Often 1-3px wider than browser
}
```

**Test Evidence:**
```
Browser: Column 29.56px
Radiant: Column 31px (1.44px wider)

Over 3 columns: 88.68px vs 93px = 4.32px error
Table border-spacing compounds this to ~10px total error
```

### Root Cause
1. **Text measurement precision** - Integer rounding loses sub-pixel accuracy
2. **Arbitrary text margin** - `+2` adjustment not browser-accurate
3. **Empty cell minimum** - 20px minimum too large (browser uses ~16px)
4. **Border-spacing double-counted** - Added in both column width and table width

### Solution
```cpp
int measure_cell_min_width(ViewTableCell* cell) {
    float content_width = 0.0f; // Use float for sub-pixel precision

    // Measure with no arbitrary adjustments
    for (View* child = ((ViewGroup*)cell)->child; child; child = child->next) {
        if (child->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            content_width = fmaxf(content_width, text->width);
            // No +2 adjustment - trust text metrics
        }
    }

    // Empty cell minimum (match browser behavior)
    if (content_width < 1.0f) {
        content_width = 1.0f; // Minimal content space
    }

    // Add cell padding from CSS
    float padding_h = 0.0f;
    if (cell->bound && cell->bound->padding.left >= 0) {
        padding_h = cell->bound->padding.left + cell->bound->padding.right;
    }

    // Add border (1px each side for border: 1px solid)
    float total_width = content_width + padding_h + 2.0f;

    // Minimum usable cell width
    if (total_width < 16.0f) total_width = 16.0f; // Browser minimum

    return (int)roundf(total_width); // Round to nearest pixel
}
```

**Expected Impact:** Reduce column width errors from 5-10px to <2px

---

## Problem 3: Display Property Handling (Affects CSS table layouts)

### Current Behavior
```cpp
// layout_table.cpp:224-240
DisplayValue child_display = resolve_display(child->as_element());

if (tag == HTM_TAG_CAPTION || child_display.inner == CSS_VALUE_TABLE_CAPTION) {
    // Caption handled
}
else if (tag == HTM_TAG_THEAD || child_display.inner == CSS_VALUE_TABLE_ROW_GROUP) {
    // Row group handled
}
// ... BUT: display: table on <div> NOT checked in layout.cpp!
```

**Test Evidence:**
```html
<!-- table_003_fixed_layout.html -->
<div style="display: table; table-layout: fixed; width: 400px;">
    <div style="display: table-row;">
        <div style="display: table-cell;">Cell</div>
    </div>
</div>
```

**Radiant:** Renders as block `<div>` (404px × 146px)
**Browser:** Renders as table (414px × 300px)

### Root Cause
The `layout_flow_node()` function in `layout.cpp` doesn't check for `display: table` on non-`<table>` elements:

```cpp
// layout.cpp (assumed structure)
void layout_flow_node(LayoutContext* lycon, DomNode* node) {
    if (node->tag() == HTM_TAG_TABLE) {
        layout_table_content(lycon, node, display);
    }
    // MISSING: Check display.inner == CSS_VALUE_TABLE
    else if (display.outer == CSS_VALUE_BLOCK) {
        layout_block(lycon, node, display);
    }
}
```

### Solution
```cpp
// In layout.cpp
void layout_flow_node(LayoutContext* lycon, DomNode* node) {
    DisplayValue display = resolve_display(node->as_element());

    // Check CSS display property BEFORE tag name
    if (display.inner == CSS_VALUE_TABLE || node->tag() == HTM_TAG_TABLE) {
        layout_table_content(lycon, node, display);
        return;
    }

    if (display.inner == CSS_VALUE_TABLE_CELL || node->tag() == HTM_TAG_TD || node->tag() == HTM_TAG_TH) {
        // Handle table-cell (may need special block context)
        layout_table_cell_as_block(lycon, node, display);
        return;
    }

    // Continue with other layout types...
}
```

**Expected Impact:** Fix 8 tests using CSS `display: table-*` properties

---

## Problem 4: Cell Padding Application (Affects text and height)

### Current Behavior
```cpp
// layout_table.cpp:1374-1410
// Read cell padding
int padding_vertical = 0;
if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
    padding_vertical = tcell->bound->padding.top + tcell->bound->padding.bottom;
} else {
    log_debug("No CSS padding found, using default 0");
    padding_vertical = 0;
}

// Use explicit CSS height if provided, otherwise use content height
if (explicit_cell_height > 0) {
    cell_height = explicit_cell_height; // CSS height already includes padding
} else {
    cell_height = content_height;
    cell_height += padding_vertical;  // Add CSS padding
    cell_height += 2;  // CSS border
}
```

**Issue:** Inconsistent assumptions
- Sometimes padding included in CSS height
- Sometimes padding added separately
- Text positioning doesn't use padding at all

### Root Cause
**Confusion about CSS box model:**
- CSS `height` property specifies **content box** height (excludes padding/border)
- But code assumes CSS height **includes** padding
- Lexbor may return different box models depending on context

### Solution
**Standardize on CSS box-sizing behavior:**
```cpp
// Always treat CSS height as content-box (standard CSS behavior)
int calculate_cell_height(ViewTableCell* tcell, int content_height) {
    int padding_v = 0;
    if (tcell->bound) {
        padding_v = tcell->bound->padding.top + tcell->bound->padding.bottom;
    }

    int border_v = 2; // 1px top + 1px bottom

    // Check for explicit CSS height
    int css_height = get_css_height(tcell); // Returns content-box height or 0

    if (css_height > 0) {
        // CSS height = content area only
        // Total height = CSS height + padding + border
        return css_height + padding_v + border_v;
    } else {
        // Auto height: measure content + padding + border
        return content_height + padding_v + border_v;
    }
}
```

**Expected Impact:** Consistent cell height calculations, better text alignment

---

## Problem 5: Caption Positioning (Affects layout tests)

### Current Behavior
```cpp
// layout_table.cpp:563-571
ViewBlock* caption = nullptr;
int caption_height = 0;

for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
    if (child->node && child->node->tag() == HTM_TAG_CAPTION) {
        caption = child;
        if (caption->height > 0) {
            caption_height = caption->height + 8; // Add margin
        }
    }
}
```

**Test Evidence:**
```
Browser: Caption (16, 16, 747.67×18)
Radiant: Caption (16, 16, 299×0) - HEIGHT IS ZERO!
```

### Root Cause
1. **Caption content not laid out** - Caption block created but children not processed
2. **Caption height not measured** - Remains at default 0
3. **Margin calculation arbitrary** - +8px not browser-accurate

### Solution
```cpp
// In build_table_tree()
if (tag == HTM_TAG_CAPTION) {
    ViewBlock* caption = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);
    if (caption) {
        // Set up layout context for caption
        lycon->parent = (ViewGroup*)caption;
        lycon->prev_view = nullptr;
        lycon->elmt = child;

        // Initialize block dimensions (caption takes table width)
        lycon->block.width = lycon->line.right - lycon->line.left;
        lycon->block.height = 0;
        lycon->block.advance_y = 0;

        // Layout caption content (text, inline elements)
        for (DomNode* cc = child->first_child(); cc; cc = cc->next_sibling()) {
            layout_flow_node(lycon, cc);
        }

        // Finalize caption height
        caption->height = lycon->block.advance_y;

        // Read CSS margin-bottom for spacing
        int caption_margin = 0;
        if (caption->bound && caption->bound->margin.bottom > 0) {
            caption_margin = caption->bound->margin.bottom;
        } else {
            caption_margin = 8; // Default browser margin
        }

        // Store caption for later positioning
        table->caption = caption;
        table->caption_height = caption->height + caption_margin;
    }
}
```

**Expected Impact:** Fix caption height and positioning in 3-5 tests

---

## Problem 6: Inline Text Wrapping (Critical for multi-word cells)

### Current Behavior
```cpp
// When cell contains: "This content should be aligned to the top"
// Browser renders: Full text wrapped across 2-3 lines
// Radiant renders: "This " only (rest missing from DOM tree)
```

**Test Evidence:**
```
Browser: Cell shows "This content should be aligned to the top of the cell" (wrapped)
Radiant: Cell shows "This " only

Browser: 8 text nodes in children
Radiant: 8 text nodes BUT "content", "should", "be"... missing from Browser comparison
```

### Root Cause
**Text not wrapped during cell content layout** - The `layout_flow_node()` function creates separate text nodes for each word, but:
1. Text nodes positioned sequentially without line breaking
2. No width constraint enforcement during cell content layout
3. Text overflow not handled (clips instead of wrapping)

### Solution Approach
This is a **fundamental layout engine issue** beyond just table code:

```cpp
// In layout_table_cell_content() - enforce line wrapping
void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    // ... setup ...

    // Set line width constraint for wrapping
    lycon->line.left = content_start_x;
    lycon->line.right = cell->width - 1 - padding_right; // Hard limit
    lycon->line.advance_x = content_start_x;
    lycon->line.is_line_start = true;

    // Enable text wrapping mode
    lycon->flags |= LAYOUT_FLAG_WRAP_TEXT; // Need to add this flag

    // Layout content - should trigger line breaks at lycon->line.right
    for (DomNode* cc = tcell->node->first_child(); cc; cc = cc->next_sibling()) {
        layout_flow_node(lycon, cc);

        // Check if text overflows - force line break
        if (lycon->line.advance_x > lycon->line.right) {
            // Call line break logic
            line_break(lycon);
        }
    }
}
```

**Additional Work Required:**
- Implement `line_break()` function if not present
- Add word-wrapping logic to text layout
- Handle inline element line breaking

**Expected Impact:** Major fix for 30+ tests with multi-word cell content

---

## Problem 7: Fixed Layout Algorithm (Incomplete)

### Current Behavior
```cpp
// layout_table.cpp:923-1088 (Fixed Layout Section)
if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED) {
    // Reads explicit table width
    // Reads first row cell widths
    // Distributes width to columns

    // BUT: Does NOT handle container height distribution!
}
```

**Test Evidence:**
```html
<div style="display: table; table-layout: fixed; width: 400px; height: 300px;">
```

```
Browser: Distributes 300px height across 2 rows = 150px each → 123px content
Radiant: Uses auto height = 40px per row (only content height)
```

### Root Cause
**Fixed layout algorithm only handles width, not height:**
1. Container height property ignored
2. Row heights calculated from content only
3. CSS `height` property on table/rows not distributed

### Solution
```cpp
// In table_auto_layout() after column width calculation
if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED) {
    // Existing width distribution...

    // NEW: Distribute height to rows
    int explicit_table_height = 0;
    if (table->node && table->node->lxb_elmt) {
        const lxb_css_rule_declaration_t* height_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)table->node->lxb_elmt,
                LXB_CSS_PROPERTY_HEIGHT);
        if (height_decl && height_decl->u.height) {
            explicit_table_height = resolve_length_value(
                lycon, LXB_CSS_PROPERTY_HEIGHT, height_decl->u.height);
        }
    }

    if (explicit_table_height > 0) {
        // Count rows
        int total_rows = count_table_rows(table);

        // Subtract borders and spacing
        int content_height = explicit_table_height - table_border_height;
        if (!table->border_collapse) {
            content_height -= (total_rows + 1) * table->border_spacing_v;
        }

        // Distribute equally (or by row weights if specified)
        int row_height = content_height / total_rows;

        // Apply to all rows
        for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
            if (child->type == RDT_VIEW_TABLE_ROW) {
                child->height = row_height;
            } else if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
                for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                    if (row->type == RDT_VIEW_TABLE_ROW) {
                        row->height = row_height;
                    }
                }
            }
        }
    }
}
```

**Expected Impact:** Fix 5-8 fixed layout tests with explicit height

---

## Implementation Priority

### Phase 1: High-Impact Fixes (Target: 30+ tests passing)
1. **Text positioning with padding** (Problem 1) - 1-2 days
   - Fix `layout_table_cell_content()` line box initialization
   - Apply padding offset to text coordinates
   - Test: `table_001_simple_table.html` should pass

2. **Inline text wrapping** (Problem 6) - 3-4 days
   - Implement line breaking in cell content
   - Handle text overflow and word wrapping
   - Test: `table_vertical_alignment.html` should show full text

3. **Column width precision** (Problem 2) - 1 day
   - Use float for sub-pixel accuracy
   - Remove arbitrary adjustments
   - Test: All tests should have <2px width errors

### Phase 2: CSS Property Support (Target: 40+ tests passing)
4. **Display property handling** (Problem 3) - 2 days
   - Check `display: table-*` in layout.cpp
   - Support CSS table layouts on `<div>` elements
   - Test: `table_003_fixed_layout.html` should work

5. **Cell padding consistency** (Problem 4) - 1 day
   - Standardize box-sizing behavior
   - Consistent padding application
   - Test: Cell heights should match browser

### Phase 3: Advanced Features (Target: 45+ tests passing)
6. **Caption positioning** (Problem 5) - 1 day
   - Layout caption content properly
   - Calculate correct height and margins
   - Test: Caption tests should pass

7. **Fixed layout algorithm** (Problem 7) - 2 days
   - Add height distribution logic
   - Handle row height constraints
   - Test: Fixed layout tests with height should pass

---

## Testing Strategy

### Unit Tests (Per Problem)
```bash
# Test text positioning
make test-layout test=table_001_simple_table.html

# Test column widths
make test-layout test=basic_602_table_minimal.html

# Test display properties
make test-layout test=table_003_fixed_layout.html

# Test text wrapping
make test-layout test=table_vertical_alignment.html

# Test captions
make test-layout test=table_005_caption_header.html
```

### Regression Testing
```bash
# After each fix, run full suite
make test-layout suite=table

# Track pass rate improvement:
# Phase 1: 0% → 30%
# Phase 2: 30% → 40%
# Phase 3: 40% → 45%
```

### Browser Comparison
For each major fix, compare with browser using:
```bash
make test-layout test=<test_name>.html 2>&1 | grep "Radiant:"
```

Look for:
- Element position differences < 5px (acceptable)
- Text position differences < 2px (goal)
- Width/height differences < 3px (good)

---

## Code Quality Improvements

### 1. Reduce Code Duplication
Current code has repeated patterns:
```cpp
// Row group processing
for (ViewBlock* child = table->first_child; ...) {
    if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
        for (ViewBlock* row = child->first_child; ...) {
            // Cell processing code...
        }
    }
}

// Direct row processing
for (ViewBlock* child = table->first_child; ...) {
    if (child->type == RDT_VIEW_TABLE_ROW) {
        // Same cell processing code duplicated!
    }
}
```

**Refactor to:**
```cpp
void process_table_row(LayoutContext* lycon, ViewTableRow* row, int* current_y) {
    // Unified cell processing logic
}

// Call from both paths
for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
    if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
        for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
            if (row->type == RDT_VIEW_TABLE_ROW) {
                process_table_row(lycon, (ViewTableRow*)row, &current_y);
            }
        }
    } else if (child->type == RDT_VIEW_TABLE_ROW) {
        process_table_row(lycon, (ViewTableRow*)child, &current_y);
    }
}
```

### 2. Better Error Handling
Add validation and fallbacks:
```cpp
// Before accessing cell properties
if (!cell || !cell->node || !cell->node->lxb_elmt) {
    log_warn("Invalid cell structure - skipping");
    return;
}

// Before CSS property access
const lxb_css_rule_declaration_t* decl = lxb_dom_element_style_by_id(...);
if (!decl || !decl->u.width) {
    // Use default or fallback
    return 0; // Auto width
}
```

### 3. Clearer Logging
Replace generic logs with structured output:
```cpp
// Instead of:
log_debug("Cell positioned at x=%d, y=%d (relative to row), size=%dx%d",
       cell->x, cell->y, cell->width, cell->height);

// Use:
log_cell_layout(cell, "positioned relative to row");

void log_cell_layout(ViewTableCell* cell, const char* context) {
    log_debug("[CELL %s] pos=(%d,%d) size=%dx%d padding=(%d,%d,%d,%d) %s",
           cell->node ? cell->node->name() : "unknown",
           cell->x, cell->y, cell->width, cell->height,
           cell->bound ? cell->bound->padding.top : 0,
           cell->bound ? cell->bound->padding.right : 0,
           cell->bound ? cell->bound->padding.bottom : 0,
           cell->bound ? cell->bound->padding.left : 0,
           context);
}
```

---

## Long-Term Architecture Recommendations

### 1. Separate Layout Phases
**Current:** Single-pass layout (measure + position simultaneously)
**Problem:** Can't handle dependencies (e.g., cell width affects text wrapping affects cell height)

**Proposed:**
```cpp
void table_layout_multi_pass(LayoutContext* lycon, ViewTable* table) {
    // Phase 1: Structure analysis
    analyze_table_structure(table); // Count rows/cols, detect spans

    // Phase 2: Width calculation (horizontal pass)
    calculate_column_widths(lycon, table);

    // Phase 3: Height calculation (vertical pass)
    calculate_row_heights(lycon, table);

    // Phase 4: Cell content layout (with known dimensions)
    layout_all_cell_content(lycon, table);

    // Phase 5: Final positioning (apply offsets)
    position_table_elements(table);
}
```

### 2. CSS Box Model Abstraction
Create helper functions for consistent box model calculations:
```cpp
struct BoxMetrics {
    int content_width;
    int content_height;
    int padding_left, padding_right, padding_top, padding_bottom;
    int border_left, border_right, border_top, border_bottom;
    int total_width;  // content + padding + border
    int total_height;
};

BoxMetrics calculate_box_metrics(View* view, int content_w, int content_h) {
    BoxMetrics m = {0};
    m.content_width = content_w;
    m.content_height = content_h;

    if (view->bound) {
        m.padding_left = view->bound->padding.left;
        // ... etc
    }

    if (view->bound && view->bound->border) {
        m.border_left = (int)view->bound->border->width.left;
        // ... etc
    }

    m.total_width = m.content_width + m.padding_left + m.padding_right
                    + m.border_left + m.border_right;
    m.total_height = m.content_height + m.padding_top + m.padding_bottom
                     + m.border_top + m.border_bottom;

    return m;
}
```

### 3. Grid System Enhancement
Current grid handles spans but could be more robust:
```cpp
struct TableGrid {
    int rows;
    int cols;
    ViewTableCell** cells; // 2D array: cells[row][col]
    bool* occupied;        // Track spanning cells

    // Methods
    void allocate(int r, int c);
    void place_cell(ViewTableCell* cell, int row, int col);
    ViewTableCell* get_cell(int row, int col);
    void calculate_dimensions();
};
```

---

## Estimated Timeline

### Conservative Estimate (20-25 days)
- **Phase 1:** 6 days (Problems 1, 2, 6)
- **Phase 2:** 3 days (Problems 3, 4)
- **Phase 3:** 3 days (Problems 5, 7)
- **Testing/Debug:** 5 days
- **Code Review/Refactor:** 3 days

### Aggressive Estimate (12-15 days)
- **Phase 1:** 4 days (parallel work on 1+2+6)
- **Phase 2:** 2 days (3+4 together)
- **Phase 3:** 2 days (5+7 together)
- **Testing/Debug:** 3 days
- **Code Review:** 1 day

**Recommendation:** Start with Phase 1, Problem 1 (text positioning). This is the **quickest win** and will immediately improve test results across the board.

---

## Success Metrics

### Code Quality
- [ ] Reduce duplication by 50% (consolidate row processing)
- [ ] Add 100+ lines of error handling
- [ ] Improve test coverage to 90%+ (48 tests)

### Test Results
- [ ] **Phase 1 Complete:** 30% pass rate (15/48 tests)
- [ ] **Phase 2 Complete:** 65% pass rate (31/48 tests)
- [ ] **Phase 3 Complete:** 90% pass rate (43/48 tests)

### Performance
- [ ] Table layout time < 10ms for simple tables
- [ ] Memory usage < 100KB per table
- [ ] No memory leaks (valgrind clean)

### Browser Compatibility
- [ ] Element position accuracy: ±5px (95% of elements)
- [ ] Text position accuracy: ±2px (90% of text nodes)
- [ ] Width/height accuracy: ±3px (95% of elements)

---

## Conclusion

The Radiant table layout engine is **80% complete** in terms of architecture but needs focused fixes on 7 specific problems. The code structure is solid, and the CSS integration is well-designed.

**Primary Issue:** Text positioning and inline content layout - this affects every single test.

**Secondary Issues:** Column width precision, display property handling, box model consistency.

**Quick Win:** Fix Problem 1 (text positioning) first - this will show immediate improvement in test results and unblock progress on other issues.

**Long-term:** Consider multi-pass layout and better CSS box model abstraction for maintainability.

The path to 90%+ test pass rate is clear and achievable within 2-3 weeks of focused development.
