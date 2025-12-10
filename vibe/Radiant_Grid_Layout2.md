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
| Grid | 6 | 39 | 15.4% |

### Test Results - Current State (Phase 2 Complete)
| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| Baseline | 683 | 683 | 100% |
| Grid | 7 | 39 | 18% |

**Progress:**
- Phase 1: Implemented CSS property resolution and fixed auto-placement to handle span values
- Phase 2: Implemented intrinsic sizing using unified API, added `grid-auto-rows/columns` support, fixed memory ownership, fixed `fr` unit parsing, fixed test framework text node comparison

### Root Causes of Grid Test Failures

#### Issue 1: Missing CSS Property Resolution (Critical) - ✅ FIXED

`resolve_css_style.cpp` now handles grid item placement properties:

**Implemented:**
- `CSS_PROPERTY_GRID_TEMPLATE_COLUMNS` ✅
- `CSS_PROPERTY_GRID_TEMPLATE_ROWS` ✅
- `CSS_PROPERTY_GRID_COLUMN_START` ✅
- `CSS_PROPERTY_GRID_COLUMN_END` ✅
- `CSS_PROPERTY_GRID_ROW_START` ✅
- `CSS_PROPERTY_GRID_ROW_END` ✅
- `CSS_PROPERTY_GRID_COLUMN` (shorthand) ✅
- `CSS_PROPERTY_GRID_ROW` (shorthand) ✅
- `CSS_PROPERTY_GRID_AUTO_FLOW` ✅
- `CSS_PROPERTY_GRID_AUTO_ROWS` ✅ Added in Phase 2
- `CSS_PROPERTY_GRID_AUTO_COLUMNS` ✅ Added in Phase 2

**Still Missing:**
- `grid-area` (complex shorthand)
- `justify-self` (grid item alignment)
- `align-self` (grid item alignment)
- `justify-content` (grid container alignment)
- `align-content` (grid container alignment)

**Key Fix:** The CSS parser returns "span" as `CSS_VALUE_TYPE_CUSTOM` (type 17), not `CSS_VALUE_TYPE_KEYWORD`. Updated code to check both types.

#### Issue 2: Primitive Content Measurement - ✅ FIXED

Updated grid measurement to use the unified intrinsic sizing API (same as flex):

```cpp
// Now uses: measure_element_intrinsic_widths() and calculate_min/max_content_height()
IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, (DomElement*)item);
*min_width = sizes.min_content;
*max_width = sizes.max_content;
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

#### Issue 4: Memory Ownership in GridTrack - ✅ FIXED

Added `owns_size` flag to `GridTrack` to prevent double-free crashes:
```cpp
typedef struct GridTrack {
    GridTrackSize* size;
    bool owns_size;  // true if this track owns the size pointer
    // ...
} GridTrack;
```

Cleanup now respects ownership:
```cpp
if (grid->computed_rows[i].owns_size) {
    destroy_grid_track_size(grid->computed_rows[i].size);
}
```

#### Issue 5: `fr` Unit Parsing in grid-auto-rows/columns - ✅ FIXED

Single-value `fr` units were being parsed as pixels. Now uses `parse_css_value_to_track_size()`:
```cpp
// Before: GRID_TRACK_SIZE_LENGTH with value=1 (wrong!)
// After:  GRID_TRACK_SIZE_FR with value=100 (correct - 1.00fr)
GridTrackSize* track_size = parse_css_value_to_track_size(value);
```

#### Issue 6: Test Framework Text Node Comparison - ✅ FIXED

Fixed three issues in `test/layout/test_radiant_layout.js`:

1. **`getAllChildren()`**: Browser reference stores text nodes in separate `textNodes` array
2. **`filterForComparison()`**: Browser text nodes have `rects` at top level, not under `layout`
3. **Text node layout comparison**: Fixed extraction of browser layout from `rects` array

## Implementation Log

### Phase 1 Changes

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

### Phase 2 Changes

#### 5. layout_grid_multipass.cpp - Unified intrinsic sizing
- Replaced primitive character-based estimation with `measure_element_intrinsic_widths()`
- Added proper height calculation using `calculate_min_content_height()` / `calculate_max_content_height()`
- Grid measurement now matches flex layout accuracy

#### 6. view.hpp - GridProp additions
```cpp
// Added fields for auto track sizing:
GridTrackList* grid_auto_rows;
GridTrackList* grid_auto_columns;
```

#### 7. grid.hpp - Memory ownership tracking
```cpp
// Added to GridTrack:
bool owns_size;  // Tracks ownership of size pointer
```

#### 8. grid_sizing.cpp - Ownership initialization
- Sets `owns_size = false` for tracks that share pointers from templates
- Sets `owns_size = true` for newly created auto sizes

#### 9. layout_grid.cpp - Safe cleanup
- Only destroys track sizes where `owns_size == true`

#### 10. resolve_css_style.cpp - `fr` unit handling
- `CSS_PROPERTY_GRID_AUTO_ROWS`: Now uses `parse_css_value_to_track_size()` for proper `fr` handling
- `CSS_PROPERTY_GRID_AUTO_COLUMNS`: Same fix applied

#### 11. test/layout/test_radiant_layout.js - Text comparison fixes
- `getAllChildren()`: Added handling for browser `textNodes` array
- `filterForComparison()`: Added check for `child.node.rects`
- Text layout comparison: Fixed browser layout extraction from `rects[0]`

## Remaining Issues to Address

### Priority 1: Grid Item Alignment Properties
| Property | Status | Impact |
|----------|--------|--------|
| `justify-self` | ❌ Not implemented | Items stretch to fill cell instead of aligning |
| `align-self` | ❌ Not implemented | Items stretch vertically instead of aligning |

**Example failure (grid_109_justify_self.html):**
- Items have `width=376` (full row) instead of `width=80` (specified)
- Items should be positioned at start/center/end based on `justify-self` value

### Priority 2: Grid Container Alignment Properties
| Property | Status | Impact |
|----------|--------|--------|
| `justify-content` | ❌ Not implemented | Grid tracks don't align within container |
| `align-content` | ❌ Not implemented | Grid tracks don't align vertically |

### Priority 3: Advanced Grid Features
| Feature | Status | Impact |
|---------|--------|--------|
| `grid-auto-flow: dense` | ❌ Not implemented | Sparse placement instead of dense packing |
| Negative line numbers | ❌ Not implemented | `grid-column: -1` doesn't work |
| `grid-area` shorthand | ❌ Not implemented | Can't use `grid-area: 1 / 2 / 3 / 4` |
| Named grid lines | ❌ Partial | `[name]` syntax not fully supported |
| `order` property | ❌ Not implemented | Visual order differs from DOM order |

### Priority 4: Nested Grid Support
- Nested grids partially work but may have sizing issues
- Need to verify intrinsic sizing of nested grid containers

## Expected Test Improvements

After implementing alignment properties (`justify-self`, `align-self`):
- `grid_109_justify_self.html` - Should pass
- `grid_110_align_self.html` - Should pass
- Several other tests that depend on item alignment

After implementing container alignment (`justify-content`, `align-content`):
- `grid_111_justify_content.html` - Should improve
- `grid_112_align_content.html` - Should improve

## Files Modified

| File | Changes |
|------|---------|
| `radiant/resolve_css_style.cpp` | CSS property handling, `fr` unit fix |
| `radiant/layout_grid_multipass.cpp` | Unified intrinsic sizing |
| `radiant/layout_grid.cpp` | Auto-placement state, cleanup safety |
| `radiant/grid_sizing.cpp` | Track ownership initialization |
| `radiant/grid.hpp` | `owns_size` field in GridTrack |
| `radiant/view.hpp` | `grid_auto_rows/columns` in GridProp |
| `radiant/view_pool.cpp` | `alloc_grid_item_prop()` helper |
| `test/layout/test_radiant_layout.js` | Text node comparison fixes |
