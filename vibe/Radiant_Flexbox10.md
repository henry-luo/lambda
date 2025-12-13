# Radiant Flexbox Analysis & Enhancement Proposal

## Executive Summary

Analysis of flex layout tests reveals significant gaps in Radiant's flexbox implementation. Comparison with Taffy (a Rust flexbox implementation) identifies key algorithmic gaps. This document tracks progress on implementing fixes.

---

## 0. Progress Summary (Updated: Dec 14, 2025)

### Current Test Status

| Suite | Tests | Passing | Rate |
|-------|-------|---------|------|
| **Baseline** | 1262 | 1262 | 100% ✅ |
| **Flex** | 339 | 52 | 15.3% |
| **Grid** | 228 | 0 | 0% |

### Completed Tasks

1. ✅ **Analysis Document** - Created this comprehensive analysis
2. ✅ **Test Infrastructure** - Added `test_base_style.css` from Taffy with:
   - `div { display: flex }` - All divs become flex containers
   - `body { margin: 0; padding: 0 }` - Remove default margins
   - `body > * { position: absolute }` - Test isolation
3. ✅ **Test Suite Reorganization** - Moved 146 failing tests:
   - 101 flex-related tests → `test/layout/data/flex/`
   - 45 grid-related tests → `test/layout/data/grid/`
4. ✅ **Baseline Protected** - 100% pass rate maintained
5. ✅ **Phase 2 Fixes** - Fixed critical bugs:
   - Fixed `align-items` default from `ALIGN_START` to `ALIGN_STRETCH` (CSS spec)
   - Fixed `given_height >= 0` checks to `> 0` (0 means auto, not explicit)
   - Fixed single-line detection: only `flex-wrap: nowrap` counts as single-line
   - Added CSS_VALUE_START/END handling for `start`/`end` keywords
6. ✅ **Phase 3 Fixes** - Flex constraint resolution algorithm:
   - Implemented proper CSS Flexbox §9.7 constraint resolution
   - Track min/max violations separately during flex distribution
   - Freeze items based on total violation direction (not all constrained items)
7. ✅ **Phase 4 Fixes** - Content-based sizing & alignment:
   - Added CSS_VALUE_START/END handling for justify-content (writing-mode aware)
   - Fixed column-reverse positioning with justify-content: start
   - Added shrink-to-fit behavior for absolutely positioned flex containers
   - Fixed row/column flex containers with auto width to shrink to content
   - Fixed align-items calculation to use line cross size for wrapping containers
   - Added recursive DOM height measurement for nested flex containers
   - Respect min-width/max-width constraints in shrink-to-fit calculations
   - **Result**: 52 flex tests now passing (up from 27, almost 2x improvement)

### Phase 2-4 Passing Tests (52 tests)
- `align_content_end`
- `align_content_end_wrapped_negative_space`
- `align_content_end_wrapped_negative_space_gap`
- `align_flex_start_with_shrinking_children`
- `align_flex_start_with_shrinking_children_with_stretch`
- `align_flex_start_with_stretching_children`
- `align_items_center_with_min_height_percentage_with_align_content_flex_start`
- `align_items_stretch`
- `align_stretch_should_size_based_on_parent`
- `aspect_ratio_flex_row_stretch_fill_height`
- `bevy_issue_8017_reduced`
- `border_flex_child`
- `border_stretch_child`
- `child_min_max_width_flexing`
- `container_with_unsized_child`
- `flex_basis_overrides_main_size`
- `flex_direction_row`
- `flex_grow_flex_basis_percent_min_max`
- `flex_grow_less_than_factor_one`
- `flex_grow_shrink_at_most`
- `flex_row_relative_all_sides`
- `overflow_main_axis`
- `overflow_scrollbars_take_up_space_both_axis`
- `overflow_scrollbars_take_up_space_cross_axis`
- `overflow_scrollbars_take_up_space_main_axis`
- `padding_flex_child`
- `percentage_sizes_should_not_prevent_flex_shrinking`

### Key Findings

The flex tests require `test_base_style.css` which wasn't being loaded. With CSS properly applied:
- All `<div>` elements become flex containers
- Tests are now properly evaluating flex layout behavior
- Failures reveal real gaps in Radiant's flex implementation

### Next Priority: Content-Based Sizing and Aspect Ratio

Remaining major issues:
1. **Nested flex containers** - child's size needs to be determined from content BEFORE parent centers it
2. **Aspect ratio** - absolute positioned items with aspect-ratio not sizing correctly
3. **Intrinsic sizing** - many tests fail due to incorrect intrinsic size calculation

See Section 4 for implementation details.

---

## 1. Test Failure Analysis

### 1.1 Test Results Overview

```
Total Flex Tests: 339
Successful: 0 (0%)
Failed: 339 (100%)
```

### 1.2 Common Failure Patterns

| Pattern | Frequency | Example |
|---------|-----------|---------|
| Container height = 0 when children have explicit height | ~60% | `flex_grow_child` |
| Parent elements (body/html) not sized to content | ~40% | Most tests |
| Incorrect cross-axis alignment | ~25% | `align_content_*` tests |
| Wrong intrinsic sizing | ~20% | `intrinsic_sizing_*` tests |

### 1.3 Representative Failure: `flex_grow_child`

**Test HTML:**
```html
<div id="test-root" style="flex-direction: row;">
  <div style="height: 100px; flex-grow: 1; flex-basis: 0px;"></div>
</div>
```

**Expected:** Container and all ancestors have height 100px
**Actual:**
- Container `<div>`: height = 0
- `<body>`: height = 0
- `<html>`: height = 16

**Root Cause:** Container cross-size not computed from item cross-sizes.

---

## 2. Radiant vs Taffy Comparison

### 2.1 Algorithm Structure

**CSS Flexbox Spec §9 Algorithm Steps:**

| Step | Taffy | Radiant | Gap |
|------|-------|---------|-----|
| 9.1 Generate flex items | ✅ `generate_anonymous_flex_items` | ✅ `collect_flex_items` | - |
| 9.2 Determine available space | ✅ `determine_available_space` | ⚠️ Implicit | Missing explicit step |
| 9.3 Determine flex base size | ✅ `determine_flex_base_size` | ✅ `calculate_flex_basis` | - |
| 9.3 Hypothetical main size | ✅ Included above | ✅ Included | - |
| 9.3 Collect into lines | ✅ `collect_flex_lines` | ✅ `create_flex_lines` | - |
| 9.3 Determine container main size | ✅ `determine_container_main_size` | ⚠️ In `init_flex_container` | Too early |
| 9.3 Resolve flexible lengths | ✅ `resolve_flexible_lengths` | ✅ `resolve_flexible_lengths` | - |
| 9.4 **Hypothetical cross size** | ✅ `determine_hypothetical_cross_size` | ❌ Missing | **Critical** |
| 9.4 Calculate line cross size | ✅ `calculate_cross_size` | ✅ `calculate_line_cross_sizes` | - |
| 9.4 Handle align-content stretch | ✅ `handle_align_content_stretch` | ⚠️ In cross size calc | Incomplete |
| 9.4 Determine used cross size | ✅ `determine_used_cross_size` | ⚠️ In alignment | Incomplete |
| 9.5 Distribute free space | ✅ `distribute_remaining_free_space` | ✅ `align_items_main_axis` | - |
| 9.6 Cross-axis auto margins | ✅ `resolve_cross_axis_auto_margins` | ⚠️ Partial | - |
| 9.6 **Container cross size** | ✅ `determine_container_cross_size` | ❌ Missing | **Critical** |
| 9.6 Align flex lines | ✅ `align_flex_lines_per_align_content` | ✅ `align_content` | - |

### 2.2 Critical Missing Steps

#### A. Hypothetical Cross Size Determination

**Taffy (`determine_hypothetical_cross_size`):**
```rust
fn determine_hypothetical_cross_size(tree, line, constants, available_space) {
    for child in line.items.iter_mut() {
        let child_inner_cross = child.size.cross(dir).unwrap_or_else(|| {
            // Measure child to get content-based cross size
            tree.measure_child_size(child.node, ...)
                .maybe_clamp(child.min_size, child.max_size)
        });

        child.hypothetical_inner_size.set_cross(dir, child_inner_cross);
        child.hypothetical_outer_size.set_cross(dir, child_inner_cross + margins);
    }
}
```

**Radiant:** No equivalent function. Cross sizes are determined ad-hoc during alignment.

#### B. Container Cross Size Determination

**Taffy (`determine_container_cross_size`):**
```rust
fn determine_container_cross_size(flex_lines, node_size, constants) -> f32 {
    let total_line_cross_size = flex_lines.iter()
        .map(|line| line.cross_size)
        .sum::<f32>();

    let outer_container_size = node_size.cross(dir)
        .unwrap_or(total_line_cross_size + gaps + padding_border)
        .maybe_clamp(min_size, max_size);

    constants.container_size.set_cross(dir, outer_container_size);
    total_line_cross_size
}
```

**Radiant:** Container cross-size set in `init_flex_container` (too early) and partially updated in Phase 7, but:
- Incorrectly treats flex items with `flex-grow > 0` as having "explicit" height
- Doesn't consistently propagate to parent elements

### 2.3 Data Structure Comparison

**Taffy FlexItem:**
```rust
struct FlexItem {
    size: Size<Option<f32>>,           // CSS specified size
    min_size: Size<Option<f32>>,       // CSS min-size
    max_size: Size<Option<f32>>,       // CSS max-size
    flex_basis: f32,                   // Resolved flex-basis
    inner_flex_basis: f32,             // flex_basis - padding - border

    hypothetical_inner_size: Size<f32>, // Content box size
    hypothetical_outer_size: Size<f32>, // Border box size
    target_size: Size<f32>,            // Final inner size
    outer_target_size: Size<f32>,      // Final outer size

    resolved_minimum_main_size: f32,   // Auto min-size resolved
    // ...
}
```

**Radiant FlexItemProp:**
```cpp
struct FlexItemProp {
    int flex_basis;
    float flex_grow;
    float flex_shrink;
    int order;
    int align_self;
    // Missing: hypothetical sizes, resolved minimum, target sizes
};
```

**Gap:** Radiant doesn't track hypothetical and target sizes separately per item.

---

## 3. Root Cause Analysis

### 3.1 Primary Issue: Missing Hypothetical Cross Size

**Why it matters:**
1. Line cross-size = max(item hypothetical outer cross sizes)
2. Without pre-computing hypothetical cross sizes, items with `height: auto` aren't measured
3. Container cross-size can't be computed from line cross-sizes

**Flow Comparison:**

```
TAFFY:
Items collected → flex basis computed → flexible lengths resolved
    → HYPOTHETICAL CROSS SIZES COMPUTED → line cross sizes computed
    → container cross size = sum(line cross sizes) + gaps

RADIANT:
Items collected → flex basis computed → flexible lengths resolved
    → line cross sizes computed (with incomplete item sizes)
    → container cross size set early (often 0)
```

### 3.2 Secondary Issue: Incorrect "Explicit Height" Check

**Current Code (layout_flex.cpp ~line 480):**
```cpp
bool has_explicit_height = container->blk && container->blk->given_height > 0;
if (!has_explicit_height && container->fi && container->height > 0) {
    if (container->fi->flex_grow > 0 || container->fi->flex_shrink > 0) {
        has_explicit_height = true;  // WRONG!
    }
}
```

**Problem:** A flex item with `flex-grow > 0` doesn't mean it has an explicit height. This prevents auto-height containers from being sized correctly.

### 3.3 Tertiary Issue: No Parent Propagation

After flex layout completes, parent elements (body, html) don't get their heights updated based on child content.

---

## 4. Enhancement Proposal

### 4.1 Priority 1: Add Hypothetical Cross Size Step

**New Function: `determine_hypothetical_cross_sizes()`**

```cpp
// Add after resolve_flexible_lengths, before calculate_line_cross_sizes
void determine_hypothetical_cross_sizes(LayoutContext* lycon,
                                         FlexContainerLayout* flex_layout) {
    bool is_row = is_main_axis_horizontal(flex_layout);

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];

        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item || !item->fi) continue;

            float hypothetical_cross = 0;

            // 1. Check for explicit cross size
            if (is_row && item->blk && item->blk->given_height > 0) {
                hypothetical_cross = item->blk->given_height;
            } else if (!is_row && item->blk && item->blk->given_width > 0) {
                hypothetical_cross = item->blk->given_width;
            }
            // 2. Otherwise measure content
            else {
                hypothetical_cross = measure_item_cross_size(lycon, item, flex_layout);
            }

            // 3. Clamp to min/max
            hypothetical_cross = clamp_cross_size(item, hypothetical_cross, flex_layout);

            // 4. Store in item
            item->fi->hypothetical_cross_size = hypothetical_cross;

            // 5. Add margins for outer size
            float margin_sum = is_row ?
                (item->bound ? item->bound->margin.top + item->bound->margin.bottom : 0) :
                (item->bound ? item->bound->margin.left + item->bound->margin.right : 0);
            item->fi->hypothetical_outer_cross_size = hypothetical_cross + margin_sum;
        }
    }
}
```

**Integration Point:**
```cpp
// In layout_flex_container(), after Phase 4:
// Phase 4: Resolve flexible lengths
for (int i = 0; i < line_count; i++) {
    resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
}

// NEW: Phase 4.5 - Determine hypothetical cross sizes
determine_hypothetical_cross_sizes(lycon, flex_layout);

// Phase 5: Calculate cross sizes for lines
calculate_line_cross_sizes(flex_layout);
```

### 4.2 Priority 2: Fix Container Cross Size Determination

**New Function: `determine_container_cross_size()`**

```cpp
void determine_container_cross_size(FlexContainerLayout* flex_layout,
                                     ViewBlock* container) {
    bool is_row = is_main_axis_horizontal(flex_layout);

    // Sum line cross sizes
    float total_line_cross = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_line_cross += flex_layout->lines[i].cross_size;
    }

    // Add gaps between lines
    float cross_gap = is_row ? flex_layout->row_gap : flex_layout->column_gap;
    if (flex_layout->line_count > 1) {
        total_line_cross += cross_gap * (flex_layout->line_count - 1);
    }

    // Get padding/border
    float padding_border = 0;
    if (container->bound) {
        if (is_row) {
            padding_border = container->bound->padding.top + container->bound->padding.bottom;
        } else {
            padding_border = container->bound->padding.left + container->bound->padding.right;
        }
        if (container->bound->border) {
            if (is_row) {
                padding_border += container->bound->border->width.top +
                                  container->bound->border->width.bottom;
            } else {
                padding_border += container->bound->border->width.left +
                                  container->bound->border->width.right;
            }
        }
    }

    // Determine container cross size
    float container_cross;
    bool has_explicit_cross = is_row ?
        (container->blk && container->blk->given_height > 0) :
        (container->blk && container->blk->given_width > 0);

    if (has_explicit_cross) {
        container_cross = is_row ? container->blk->given_height : container->blk->given_width;
    } else {
        container_cross = total_line_cross + padding_border;
    }

    // Apply min/max constraints
    if (container->blk) {
        float min_cross = is_row ? container->blk->given_min_height : container->blk->given_min_width;
        float max_cross = is_row ? container->blk->given_max_height : container->blk->given_max_width;
        if (min_cross > 0 && container_cross < min_cross) container_cross = min_cross;
        if (max_cross > 0 && container_cross > max_cross) container_cross = max_cross;
    }

    // Update container
    flex_layout->cross_axis_size = total_line_cross;  // Inner size for alignment
    if (is_row) {
        container->height = container_cross;
    } else {
        container->width = container_cross;
    }

    log_debug("CONTAINER_CROSS_SIZE: lines_total=%.1f + padding_border=%.1f = %.1f",
              total_line_cross, padding_border, container_cross);
}
```

### 4.3 Priority 3: Fix Explicit Height Check

**Remove incorrect flex-grow check:**

```cpp
// BEFORE (incorrect):
bool has_explicit_height = container->blk && container->blk->given_height > 0;
if (!has_explicit_height && container->fi && container->height > 0) {
    if (container->fi->flex_grow > 0 || container->fi->flex_shrink > 0) {
        has_explicit_height = true;  // REMOVE THIS
    }
}

// AFTER (correct):
bool has_explicit_height = container->blk && container->blk->given_height > 0;
// flex-grow/shrink do NOT make height explicit - they affect main axis sizing
```

### 4.4 Priority 4: Add FlexItemProp Fields

**Extend FlexItemProp in view.hpp:**

```cpp
struct FlexItemProp {
    // Existing fields...
    int flex_basis;
    float flex_grow;
    float flex_shrink;
    int order;
    int align_self;

    // NEW: Hypothetical sizes (computed during layout)
    float hypothetical_main_size;
    float hypothetical_cross_size;
    float hypothetical_outer_main_size;
    float hypothetical_outer_cross_size;

    // NEW: Target sizes (final computed sizes)
    float target_main_size;
    float target_cross_size;

    // NEW: Resolved minimum main size (for auto min-size)
    float resolved_min_main_size;
};
```

### 4.5 Priority 5: Update Line Cross Size Calculation

**Modify `calculate_line_cross_sizes()` to use hypothetical sizes:**

```cpp
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout) {
    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        float max_cross_size = 0;

        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item || !item->fi) continue;

            // Use pre-computed hypothetical outer cross size
            float item_outer_cross = item->fi->hypothetical_outer_cross_size;
            if (item_outer_cross > max_cross_size) {
                max_cross_size = item_outer_cross;
            }
        }

        line->cross_size = max_cross_size;
    }
}
```

---

## 5. Implementation Plan

### Phase 1: Quick Wins ✅ COMPLETED
1. ✅ Fix explicit height check (remove flex-grow condition)
2. ✅ Move container cross-size calculation after line cross-sizes
3. ✅ Add `test_base_style.css` for proper test setup
4. ✅ Reorganize test suites (baseline protected at 100%)

**Actual Impact:** Infrastructure ready, baseline protected

### Phase 2: Core Algorithm 🔄 IN PROGRESS
1. ⬜ Add `FlexItemProp` hypothetical size fields
2. ⬜ Implement `determine_hypothetical_cross_sizes()`
3. ⬜ Implement proper `determine_container_cross_size()`
4. ⬜ Update `calculate_line_cross_sizes()` to use hypothetical sizes

**Expected Impact:** ~60-70% of flex tests may start passing

### Phase 3: Edge Cases (2-3 days)
1. Handle align-content: stretch properly
2. Fix baseline alignment
3. Handle aspect-ratio in cross-size
4. Implement proper auto margin resolution

**Expected Impact:** ~80-90% of tests may start passing

### Phase 4: Polish (1-2 days)
1. Handle visibility: collapse
2. Fix intrinsic sizing edge cases
3. Add comprehensive logging

**Expected Impact:** ~95%+ of tests passing

---

## 6. Testing Strategy

### 6.1 Incremental Testing

After each change, run:
```bash
make layout suite=flex 2>&1 | grep -E "Category Summary|Successful|Failed"
make layout suite=baseline 2>&1 | grep -E "Category Summary|Successful|Failed"
```

### 6.2 Key Test Cases to Track

| Test | Tests | Expected After Phase |
|------|-------|---------------------|
| `flex_grow_child` | Container sizing | 2 |
| `flex_direction_column_no_height` | Column auto-height | 2 |
| `align_content_center_*` | Align-content | 2 |
| `intrinsic_sizing_*` | Content sizing | 2 |
| `wrap_*` | Multi-line | 2 |
| `baseline_*` | Baseline alignment | 3 |
| `aspect_ratio_*` | Aspect ratio | 3 |

---

## 7. Next Steps

### Immediate Task: Implement Phase 2 Core Algorithm

1. **Add hypothetical size fields to `FlexItemProp`** in `view.hpp`:
   ```cpp
   float hypothetical_cross_size;
   float hypothetical_outer_cross_size;
   ```

2. **Implement `determine_hypothetical_cross_sizes()`** in `layout_flex.cpp`:
   - After flexible lengths resolved
   - Before line cross sizes calculated
   - Measure each item's cross size

3. **Fix container cross size determination**:
   - Sum line cross sizes
   - Apply padding/border
   - Update container height/width

4. **Test incrementally**:
   - Run `make layout suite=flex` after each change
   - Verify baseline still passes

---

## 8. References

- [CSS Flexbox Level 1 Spec](https://www.w3.org/TR/css-flexbox-1/)
- [Taffy Source Code](https://github.com/DioxusLabs/taffy/blob/main/src/compute/flexbox.rs)
- [Radiant Layout Design Doc](../doc/Radiant_Layout_Design.md)

