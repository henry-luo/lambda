# Radiant Grid Layout Improvement Proposal

## Executive Summary

The grid layout implementation has critical gaps that prevent CSS Grid properties from working correctly. This document outlines the analysis, proposes specific improvements, and tracks implementation progress.

## Current State Analysis

### Test Results - Initial State (Before Implementation)
| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| Baseline | 683 | 683 | 100% |
| Grid | 0 | 39 | 0% |

### Test Results - After Phase 1 Implementation
| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| Baseline | 683 | 683 | 100% |
| Grid | 1 | 39 | 2.6% |

**Progress:** Phase 1 implemented CSS property resolution and fixed auto-placement to handle span values. Baseline tests remain unaffected.

### Root Causes of Grid Test Failures

#### Issue 1: Missing CSS Property Resolution (Critical) - ✅ FIXED

`resolve_css_style.cpp` now handles grid item placement properties:

**Implemented:**
- `CSS_PROPERTY_GRID_TEMPLATE_COLUMNS`
- `CSS_PROPERTY_GRID_TEMPLATE_ROWS`
- `CSS_PROPERTY_GRID_COLUMN_START` ✅ Added
- `CSS_PROPERTY_GRID_COLUMN_END` ✅ Added
- `CSS_PROPERTY_GRID_ROW_START` ✅ Added
- `CSS_PROPERTY_GRID_ROW_END` ✅ Added
- `CSS_PROPERTY_GRID_COLUMN` (shorthand) ✅ Added
- `CSS_PROPERTY_GRID_ROW` (shorthand) ✅ Added
- `CSS_PROPERTY_GRID_AUTO_FLOW` ✅ Added

**Still Missing:**
- `grid-area` (complex shorthand)
- `grid-auto-rows`
- `grid-auto-columns`

**Key Fix:** The CSS parser returns "span" as `CSS_VALUE_TYPE_CUSTOM` (type 17), not `CSS_VALUE_TYPE_KEYWORD`. Updated code to check both types.

#### Issue 2: Primitive Content Measurement - ⏳ Pending

Grid's `measure_grid_item_intrinsic()` uses crude estimation:
```cpp
// Current: ~8px per character assumption
int estimated_width = text_len * 8;
int estimated_height = 20;
```

Flex uses accurate FreeType-based measurement via `intrinsic_sizing.cpp`:
```cpp
TextIntrinsicWidths measure_text_intrinsic_widths(lycon, text, length);
// Returns actual glyph-based min/max content widths
```

#### Issue 3: Static Auto-Placement State - ✅ FIXED

Moved auto-placement cursors from static variables to `GridContainerLayout` struct:
```cpp
// grid.hpp - Added fields:
int auto_row_cursor;
int auto_col_cursor;

// layout_grid.cpp - Initialized in init_grid_container():
grid->auto_row_cursor = 1;
grid->auto_col_cursor = 1;
```

## Implementation Log

### Changes Made

#### 1. view_pool.cpp - Added `alloc_grid_item_prop()`
```cpp
void alloc_grid_item_prop(LayoutContext* lycon, ViewSpan* span) {
    if (!span->gi) {
        span->gi = (GridItemProp*)arena_alloc(lycon->arena, sizeof(GridItemProp));
        memset(span->gi, 0, sizeof(GridItemProp));
        span->gi->is_grid_auto_placed = true;
        span->gi->justify_self = CSS_VALUE_AUTO;
        span->gi->align_self = CSS_VALUE_AUTO;
    }
}
```

#### 2. resolve_css_style.cpp - Grid property handling
- Added cases for `CSS_PROPERTY_GRID_COLUMN_START/END`, `CSS_PROPERTY_GRID_ROW_START/END`
- Added shorthand parsing for `CSS_PROPERTY_GRID_COLUMN`, `CSS_PROPERTY_GRID_ROW`
- Handles both `CSS_VALUE_TYPE_KEYWORD` and `CSS_VALUE_TYPE_CUSTOM` for "span" keyword
- Negative values indicate span (e.g., -2 means "span 2")

#### 3. layout_grid.cpp - Span handling in placement
- Updated `place_grid_items()` to recognize negative span values
- Added logic to compute actual end positions from span values
- Replaced static cursor variables with per-container state
- Added infinite loop protection with iteration limits

#### 4. grid.hpp - Auto-placement cursor fields
```cpp
// Added to GridContainerLayout:
int auto_row_cursor;
int auto_col_cursor;
```

## Proposed Changes - Phase 2

### Phase 2: Measurement Improvements (Priority: High)
case CSS_PROPERTY_GRID_ROW:
    // Parse shorthand "start / end" or "span N"

case CSS_PROPERTY_GRID_AREA:
    // Parse "row-start / col-start / row-end / col-end" or named area

case CSS_PROPERTY_GRID_AUTO_FLOW:
    // Parse "row", "column", "dense", "row dense", "column dense"
```

### Phase 2: Reuse Flex Measurement Infrastructure

Replace primitive measurement in `layout_grid_multipass.cpp`:

**Before:**
```cpp
void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item, ...) {
    // Simple estimation: ~8px per character
    int text_len = strlen(text);
    int estimated_width = text_len * 8;
}
```

**After:**
```cpp
void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item, ...) {
    // Use unified intrinsic sizing (same as flex)
    IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, (DomElement*)item);
    *min_width = sizes.min_content;
    *max_width = sizes.max_content;
    // ... height measurement using same approach
}
```

### Phase 3: Fix Auto-Placement State

Move static variables to `GridContainerLayout`:

```cpp
struct GridContainerLayout : GridProp {
    // ... existing fields ...

    // Auto-placement cursor (replaces static variables)
    int auto_cursor_row;
    int auto_cursor_column;
};
```

Initialize in `init_grid_container()` and reset per-container.

## Implementation Plan

### Step 1: Add `alloc_grid_item_prop()` helper
Create helper function in `resolve_css_style.cpp` (similar to `alloc_flex_item_prop()`).

### Step 2: Add CSS property cases
Add switch cases for all missing grid properties.

### Step 3: Update measurement
Replace primitive measurement with calls to `intrinsic_sizing.cpp` functions.

### Step 4: Fix auto-placement
Remove static variables, use per-container state.

## Code Reuse from Flex Layout

| Flex Component | Grid Equivalent | Action |
|----------------|-----------------|--------|
| `intrinsic_sizing.cpp` functions | Grid measurement | Direct reuse |
| `measurement_cache` | Grid measurement | Already shared |
| `init_flex_item_view()` pattern | `init_grid_item_view()` | Similar structure |
| `layout_flex_absolute_children()` | `layout_grid_absolute_children()` | Nearly identical |

## Expected Outcomes

After implementation:
1. Tests using `grid-column`, `grid-row`, `span` will work
2. Content-based track sizing will be accurate
3. Nested grids will work correctly
4. Many of the 39 grid tests should pass

## Files to Modify

1. `radiant/resolve_css_style.cpp` - Add CSS property handling
2. `radiant/layout_grid_multipass.cpp` - Update measurement
3. `radiant/layout_grid.cpp` - Fix auto-placement state
4. `radiant/grid.hpp` - Add auto-cursor fields if needed
