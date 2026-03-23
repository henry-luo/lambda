# Radiant Flexbox Implementation - Comprehensive Improvement Proposal

**Date**: November 28, 2025
**Status**: In Progress - Phase 1 Complete
**Author**: Generated from Yoga analysis and flex test suite evaluation

---

## Executive Summary

After importing 250 test cases from Facebook's Yoga flexbox library and running them against Lambda's flex implementation, we now have a clear picture of our implementation gaps. This proposal outlines a comprehensive improvement plan based on:

1. **Test Results Analysis**: Started at 84/255 (32.9%), now at **120/255 (47.1%)**
2. **Yoga Architecture Study**: Key differences in algorithm design
3. **Targeted Improvements**: Prioritized fixes for maximum impact

### Test Results Summary

| Category | Initial | Current | Status |
|----------|---------|---------|--------|
| aligncontent | 18/64 (28.1%) | 32+ Improved | 🟢 Much Improved |
| alignitems | 7/31 (22.6%) | - | 🔴 Needs Work |
| alignself | 4/5 (80.0%) | - | 🟢 Mostly Working |
| flex (grow/shrink/basis) | 3/15 (20.0%) | - | 🔴 Needs Work |
| flexdirection | 17/55 (30.9%) | - | 🟡 Partial Support |
| flexwrap | 2/24 (8.3%) | Improved | 🟡 In Progress |
| gap | 13/33 (39.4%) | - | 🟡 Partial Support |
| justifycontent | 20/28 (71.4%) | - | 🟢 Mostly Working |
| **TOTAL** | **84/255 (32.9%)** | **138/255 (54.1%)** | **+54 tests** |

---

## Implementation Progress

### ✅ Completed Improvements

#### 1. Single-line align-content fix (layout_flex.cpp)
- Removed early return in `align_content()` for `line_count <= 1`
- Updated Phase 8 to call `align_content` for wrapping containers (not just multi-line)
- **Result**: Fixed singleline wrap tests with align-content

#### 2. Overflow fallback alignment (layout_flex.cpp)
- Implemented `fallback_alignment()` - returns ALIGN_START for space-* when free_space < 0
- Implemented `fallback_justify()` - returns JUSTIFY_FLEX_START for space-* when free_space < 0
- **Result**: Safe overflow behavior for negative remaining space

#### 3. Percentage width/height resolution for flex items
- **view.hpp**: Added `given_width_percent` and `given_height_percent` fields to `BlockProp`
- **resolve_css_style.cpp**: Store raw percentage values during CSS resolution
- **view_pool.cpp**: Initialize percentage fields to NAN in `alloc_block_prop`
- **layout_flex.cpp**:
  - Initialize `main_axis_size`/`cross_axis_size` in `init_flex_container`
  - Re-resolve percentage widths/heights in `collect_and_prepare_flex_items`
- **layout_flex_multipass.cpp**: Call `collect_and_prepare_flex_items` in `layout_flex_container_with_nested_content`
- **Result**: Fixed all 14 wrapped-negative-space tests (percentage items now sized correctly relative to flex container, not ancestor)

#### 4. Wrap-reverse improvements (layout_flex.cpp)
- Updated `align_items_cross_axis()` for wrap-reverse positioning with STRETCH
- Updated `align_content()` for wrap-reverse line reversal and space-evenly support
- Container height finalization (Phase 7.5) for auto-height containers
- **Result**: 5/6 wrap-reverse tests passing (up from 0/6)

#### 5. Align-content: stretch with proper line positioning (layout_flex.cpp + layout_flex_measurement.cpp)
- **layout_flex.hpp**: Added `cross_position` field to `FlexLineInfo` to track line's absolute position
- **layout_flex.cpp**:
  - Added `item_has_definite_cross_size()` and `item_will_stretch()` helper functions
  - Modified `calculate_line_cross_sizes()` to skip items with auto cross-size that will be stretched
  - Reordered phases: Phase 7 (finalize container) → Phase 8 (align_content) → Phase 9 (align_items_cross_axis)
  - `align_content()` now stores line's `cross_position` instead of moving items
  - `align_items_cross_axis()` now uses `line->cross_position + cross_pos` for absolute positioning
  - Fixed stretch logic to always set size to line's cross_size (not just when item is smaller)
- **layout_flex_measurement.cpp**: Improved DIV height estimation to return 0 for empty divs (not 56px)
- **Result**: Fixed align-content-stretch-row-with-children and similar tests (now at 138/255 = 54.1%)

#### 6. Empty element measurement fix (layout_flex_measurement.cpp)
- Removed the fallback `if (measured_height == 0) measured_height = 50;` which was incorrectly setting min-content for empty flex items
- Empty elements now correctly have min-content of 0
- **Result**: Fixed align-content-stretch-row-with-min-height and related tests

#### 7. Proper intrinsic size calculation for flex items (layout_flex_measurement.cpp)
- **measure_flex_child_content()**: Fixed to only use explicit width for `measured_width`, not container width. Items without explicit width now correctly get `measured_width = 0`.
- **calculate_item_intrinsic_sizes()**: Fixed child traversal to only use explicit dimensions. Children without explicit width/height don't contribute to parent's intrinsic size.
- **calculate_item_intrinsic_sizes()**: Fixed to skip whitespace-only text nodes when calculating intrinsic sizes.
- **calculate_item_intrinsic_sizes()**: Removed placeholder fallback values (100x50) for items with children but no content.
- **Result**: Fixed align-content-stretch-column test (column direction now correctly calculates line cross-sizes)

#### 8. Baseline alignment foundation (layout_flex.cpp)
- **calculate_item_baseline()**: New function to calculate baseline offset from item's outer margin edge
  - Handles explicit baseline from text layout
  - Recursively calculates baseline from first participating child (if laid out)
  - Synthesizes baseline from outer margin edge for empty boxes
- **find_max_baseline()**: Updated to accept container's `align_items` value
  - Now considers items with `align-self: baseline` OR container's `align-items: baseline`
  - Uses `calculate_item_baseline()` for proper baseline calculation
- **ALIGN_BASELINE case**: Updated to use `calculate_item_baseline()` for consistent baseline handling
- **Limitation**: Baseline calculation for nested flex containers is limited - children may not be laid out yet when baseline is calculated. This is an architectural limitation that requires two-pass layout to fix properly.
- **Result**: 7 baseline tests now passing (simple cases), 8 still failing (complex nested cases)

### Current Test Results: 145/255 (56.9%)
**Improvement**: +25 tests from baseline of 120/255 (47.1%)

| Category | Pass Rate | Notes |
|----------|-----------|-------|
| Baseline (wrapped-negative-space) | 14/14 (100%) | All percentage resolution tests pass |
| align-content stretch (row) | 9/10 (90%) | Most stretch tests pass |
| align-content stretch (column) | 1/1 (100%) | Column stretch now works |
| wrap-reverse | 5/6 (83%) | Most wrap-reverse tests pass |
| align-items baseline (simple) | 7/15 (47%) | Simple baseline cases pass |

### 🔄 In Progress

None at this time.

### 📋 Remaining Work

1. **Recursive baseline calculation** - Implement `calculate_baseline_recursive()` to traverse nested containers
2. **Wrap-reverse position flip** - Apply position adjustment at layout end: `container_cross - position - item_cross`
3. **Two-pass distribution refinement** - Properly freeze items hitting min/max constraints and recalculate totals

- Recursive baseline calculation
- Two-pass distribution refinement
- Additional wrap-reverse edge cases
- Min-height with align-content stretch (items with min-height constraints)

### 📋 Remaining Tasks

- Full two-pass flex distribution with frozen item tracking
- Gap refinements (cross-axis gap between lines)
- Column-reverse with borders/padding edge cases
- Margin handling in align-content stretch

---

## Part 1: Gap Analysis - What Yoga Does Better

### 1.1 Two-Pass Flex Distribution Algorithm

**Yoga's Approach** (`distributeFreeSpaceFirstPass` + `distributeFreeSpaceSecondPass`):

```cpp
// PASS 1: Detect items whose min/max constraints will trigger
// Freeze them at constrained sizes and remove from remaining space
for (auto child : flexLine.itemsInFlow) {
    if (flexLine.layout.remainingFreeSpace < 0) {
        // Calculate shrink effect
        flexShrinkScaledFactor = -child->resolveFlexShrink() * childFlexBasis;
        baseMainSize = childFlexBasis + (remainingFreeSpace / totalFlexShrinkScaledFactors) * flexShrinkScaledFactor;
        boundMainSize = boundAxis(child, mainAxis, baseMainSize, ...);

        if (baseMainSize != boundMainSize) {
            // Item hit min/max constraint - freeze it
            deltaFreeSpace += boundMainSize - childFlexBasis;
            flexLine.layout.totalFlexShrinkScaledFactors -= (-child->resolveFlexShrink() * childFlexBasis);
        }
    }
    // Similar logic for grow
}
flexLine.layout.remainingFreeSpace -= deltaFreeSpace;

// PASS 2: Distribute remaining space to unfrozen items
```

**Lambda's Current Issue**: We don't properly implement the two-pass constraint resolution. When items hit min/max bounds, we don't remove them from the distribution pool and recalculate.

**Impact**: Tests like `flex_011_flex_shrink`, `yoga-flex-flex-shrink-to-zero`, and many shrink-related tests fail.

### 1.2 FlexLine Data Structure

**Yoga's FlexLine** (`FlexLine.h`):

```cpp
struct FlexLine {
    const std::vector<yoga::Node*> itemsInFlow;  // Items in this line
    const float sizeConsumed;                     // Total size consumed
    const size_t numberOfAutoMargins;             // For auto margin handling
    FlexLineRunningLayout layout;                 // Running totals during layout
};

struct FlexLineRunningLayout {
    float totalFlexGrowFactors{0.0f};
    float totalFlexShrinkScaledFactors{0.0f};
    float remainingFreeSpace{0.0f};
    float mainDim{0.0f};
    float crossDim{0.0f};
};
```

**Lambda's Current Issue**: Our `FlexLineInfo` structure is less complete:
- Missing `numberOfAutoMargins` for proper auto margin handling
- Running totals not tracked during distribution
- No separation of scaled shrink factors vs grow factors

### 1.3 Baseline Alignment

**Yoga's Baseline Calculation** (`Baseline.cpp`):

```cpp
float calculateBaseline(const yoga::Node* node) {
    // Recursive baseline search through flex items
    yoga::Node* baselineChild = nullptr;
    for (auto child : node->getLayoutChildren()) {
        if (child->getLineIndex() > 0) break;  // Only first line
        if (child->style().positionType() == PositionType::Absolute) continue;
        if (resolveChildAlignment(node, child) == Align::Baseline ||
            child->isReferenceBaseline()) {
            baselineChild = child;
            break;
        }
        if (baselineChild == nullptr) baselineChild = child;  // Fallback
    }
    if (baselineChild == nullptr) {
        return node->getLayout().measuredDimension(Dimension::Height);
    }
    const float baseline = calculateBaseline(baselineChild);  // Recursive
    return baseline + baselineChild->getLayout().position(PhysicalEdge::Top);
}
```

**Lambda's Current Issue**: We have basic baseline support but:
- Not recursive through nested flex containers
- Missing `isReferenceBaseline` handling
- Baseline in column direction should fall back to `FlexStart` (Yoga does this in `resolveChildAlignment`)

**Impact**: All `yoga-alignitems-align-baseline-*` tests fail (8+ tests).

### 1.4 Align-Content with Wrap

**Yoga's Multi-Line Alignment** (Step 8 in `CalculateLayout.cpp`):

```cpp
// Calculate extra space per line for stretch
if (alignContent == Align::Stretch) {
    extraSpacePerLine = remainingAlignContentDim / static_cast<float>(lineCount);
}

// For each line, recalculate with stretch height
for (size_t i = 0; i < lineCount; i++) {
    lineHeight += extraSpacePerLine;  // Distribute extra space

    // Re-measure stretch items with new line height
    if (alignItem == Align::Stretch && !child->hasDefiniteLength(...)) {
        calculateLayoutInternal(child, ...);  // Re-layout with new cross size
    }
}
```

**Lambda's Current Issue**:
- `align-content: stretch` doesn't properly distribute cross-axis space to lines
- Items with `align-items: stretch` don't get re-measured with the stretched line height
- Single-line containers behave differently from multi-line

**Impact**: All `yoga-aligncontent-align-content-stretch-*` tests fail (14+ tests).

### 1.5 Wrap-Reverse Handling

**Yoga's Approach** (end of `calculateLayoutImpl`):

```cpp
// As we only wrapped in normal direction yet, we need to reverse positions
if (performLayout && node->style().flexWrap() == Wrap::WrapReverse) {
    for (auto child : node->getLayoutChildren()) {
        if (child->style().positionType() != PositionType::Absolute) {
            child->setLayoutPosition(
                node->getLayout().measuredDimension(dimension(crossAxis)) -
                    child->getLayout().position(flexStartEdge(crossAxis)) -
                    child->getLayout().measuredDimension(dimension(crossAxis)),
                flexStartEdge(crossAxis));
        }
    }
}
```

**Lambda's Current Issue**: We have wrap-reverse handling but it's not correctly flipping positions after the main layout pass.

**Impact**: All `yoga-flexwrap-wrap-reverse-*` tests fail (7+ tests).

### 1.6 Fallback Alignment on Overflow

**Yoga's Fallback Logic** (`Align.h`):

```cpp
constexpr Align fallbackAlignment(Align align) {
    switch (align) {
        case Align::SpaceBetween:
        case Align::Stretch:
            return Align::FlexStart;
        case Align::SpaceAround:
        case Align::SpaceEvenly:
            return Align::FlexStart;  // Safe center fallback
        default:
            return align;
    }
}
```

When `remainingFreeSpace < 0`, Yoga falls back to safe alignment to prevent content overflow:

```cpp
const auto alignContent = remainingAlignContentDim >= 0
    ? node->style().alignContent()
    : fallbackAlignment(node->style().alignContent());
```

**Lambda's Current Issue**: We don't implement overflow fallback for align-content or justify-content.

**Impact**: `yoga-aligncontent-*-wrapped-negative-space` tests fail.

### 1.7 Gap Handling in Wrapped Lines

**Yoga's Gap Calculation**:

```cpp
// In line breaking
const float childLeadingGapMainAxis = child == firstElementInLine ? 0.0f : gap;

// In total size calculation
sizeConsumed += flexBasisWithMinAndMaxConstraints + childMarginMainAxis + childLeadingGapMainAxis;

// Cross-axis gap between lines
const float appliedCrossGap = lineCount != 0 ? crossAxisGap : 0.0f;
totalLineCrossDim += flexLine.layout.crossDim + appliedCrossGap;
```

**Lambda's Current Issue**:
- Gap not correctly applied to first item in line (should be 0)
- Cross-axis gap (row-gap) not applied between flex lines

**Impact**: Many `yoga-gap-*` tests fail.

---

## Part 2: Architectural Improvements

### 2.1 Refactor FlexLine Structure

```cpp
// Proposed new FlexLine structure
struct FlexLine {
    View** items;                      // Array of flex items in this line
    int item_count;                    // Number of items in line

    // Running layout state (Yoga-inspired)
    float total_flex_grow;             // Sum of flex-grow for items in line
    float total_flex_shrink_scaled;    // Sum of (flex-shrink * basis) for items
    float remaining_free_space;        // Space to distribute
    float main_dim;                    // Main axis dimension
    float cross_dim;                   // Cross axis dimension (max item height)

    // Additional metadata
    int num_auto_margins;              // Count of auto margins for distribution
    int start_index;                   // Index of first item in main array
};
```

### 2.2 Implement Two-Pass Distribution

```cpp
// Phase 4: Resolve flexible lengths (refactored)
void resolve_flexible_lengths(FlexContainerLayout* flex, FlexLine* line) {
    // PASS 1: Freeze items hitting min/max constraints
    float delta_free_space = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewGroup* item = (ViewGroup*)line->items[i]->as_element();
        float flex_basis = get_flex_basis(item, flex);

        if (line->remaining_free_space < 0) {
            // Shrinking
            float shrink_factor = get_flex_shrink(item) * flex_basis;
            float base_size = flex_basis +
                (line->remaining_free_space / line->total_flex_shrink_scaled) * shrink_factor;
            float bound_size = apply_min_max_constraints(item, base_size, flex);

            if (base_size != bound_size) {
                // Item hit constraint - freeze it
                delta_free_space += bound_size - flex_basis;
                line->total_flex_shrink_scaled -= shrink_factor;
                mark_item_frozen(item);
            }
        } else if (line->remaining_free_space > 0) {
            // Growing - similar logic
        }
    }
    line->remaining_free_space -= delta_free_space;

    // PASS 2: Distribute to unfrozen items
    for (int i = 0; i < line->item_count; i++) {
        if (is_item_frozen(item)) continue;
        // Apply final flex distribution
    }
}
```

### 2.3 Implement Recursive Baseline Calculation

```cpp
float calculate_baseline(View* node) {
    ViewGroup* element = (ViewGroup*)node->as_element();
    if (!element) {
        return node->height;  // Fallback for text nodes
    }

    // Check for custom baseline function (future)
    // if (element->has_baseline_func()) return element->baseline(...);

    View* baseline_child = nullptr;
    for (View* child = node->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewGroup* child_elem = (ViewGroup*)child->as_element();

        // Skip absolute positioned
        if (child_elem->in_line && child_elem->in_line->position == CSS_VALUE_ABSOLUTE) continue;

        // Check if this child has baseline alignment
        AlignType align = resolve_child_alignment(element, child_elem);
        if (align == ALIGN_BASELINE) {
            baseline_child = child;
            break;
        }

        // First non-absolute child as fallback
        if (baseline_child == nullptr) {
            baseline_child = child;
        }
    }

    if (baseline_child == nullptr) {
        return node->height;
    }

    // Recursive baseline calculation
    float baseline = calculate_baseline(baseline_child);
    return baseline + baseline_child->y;
}
```

### 2.4 Add Overflow Fallback Alignment

```cpp
AlignType fallback_alignment(AlignType align) {
    switch (align) {
        case ALIGN_SPACE_BETWEEN:
        case ALIGN_STRETCH:
            return ALIGN_FLEX_START;
        case ALIGN_SPACE_AROUND:
        case ALIGN_SPACE_EVENLY:
            return ALIGN_FLEX_START;  // Safe fallback
        default:
            return align;
    }
}

JustifyType fallback_justify(JustifyType justify) {
    switch (justify) {
        case JUSTIFY_SPACE_BETWEEN:
            return JUSTIFY_FLEX_START;
        case JUSTIFY_SPACE_AROUND:
        case JUSTIFY_SPACE_EVENLY:
            return JUSTIFY_FLEX_START;  // Safe fallback
        default:
            return justify;
    }
}

// In align_content distribution:
float remaining = cross_axis_size - total_line_cross_dim;
AlignType effective_align = remaining >= 0
    ? flex->align_content
    : fallback_alignment(flex->align_content);
```

---

## Part 3: Implementation Roadmap

### Phase 1: Critical Fixes (Priority: HIGH) ✅ COMPLETE

**Week 1: Overflow Fallback & Percentage Resolution**
- [x] Implement `fallback_alignment()` for align-content
- [x] Implement `fallback_justify()` for justify-content
- [x] Apply fallback when remaining space < 0
- [x] Add `given_width_percent`/`given_height_percent` to BlockProp
- [x] Re-resolve percentage widths in `collect_and_prepare_flex_items`
- [x] Initialize axis sizes in `init_flex_container`

**Actual Test Improvements**: +36 tests (84 → 120)

**Week 2: Wrap-Reverse & Single-Line Align-Content**
- [x] Fix wrap-reverse line order in `align_content()`
- [x] Fix wrap-reverse item positioning with explicit height
- [x] Fix single-line containers to apply align-content when wrapping enabled
- [x] Container height finalization for auto-height containers (Phase 7.5)

**Actual Test Improvements**: Included in +36 above

### Phase 2: Remaining High-Priority Items (IN PROGRESS)

**Baseline Alignment**
- [ ] Implement recursive `calculate_baseline()`
- [ ] Add baseline fallback for column direction
- [ ] Handle `isReferenceBaseline` marker
- [ ] Update cross-axis alignment for baseline items

**Expected Test Improvements**: +8-10 tests (align-baseline tests)

**Two-Pass Distribution Refinement**
- [ ] Implement `freeze_constrained_items()` first pass
- [ ] Track frozen state per item during distribution
- [ ] Recalculate remaining space after freezing
- [ ] Update distribution to skip frozen items

**Expected Test Improvements**: +20-30 tests (flex shrink/grow tests)

### Phase 3: Edge Cases (Priority: MEDIUM)

**Align-Content Stretch**
- [ ] Calculate `extraSpacePerLine` for stretch
- [ ] Distribute extra space to each line's cross dimension
- [ ] Re-measure stretch items with new line height
- [ ] Handle single-line vs multi-line correctly

**Expected Test Improvements**: +14 tests (align-content-stretch tests)

**Gap Refinements**
- [ ] Fix gap = 0 for first item in line
- [ ] Implement cross-axis gap (row-gap) between lines
- [ ] Handle gap with wrap correctly

**Expected Test Improvements**: +10 tests (gap tests)

### Phase 4: Polish & Performance (Priority: LOW)

**Auto Margins & Misc**
- [ ] Track `numberOfAutoMargins` per line
- [ ] Distribute remaining space to auto margins
- [ ] Handle edge cases from remaining failing tests

---

## Part 4: Tracking Metrics

### Progress History

| Date | Tests Passing | Rate | Milestone |
|------|--------------|------|-----------|
| Nov 28 (start) | 84/255 | 32.9% | Initial baseline |
| Nov 28 (current) | **120/255** | **47.1%** | Phase 1 complete (+36 tests) |

### Current State
- **Total Tests**: 255 (Yoga suite)
- **Passing**: 120 (47.1%)
- **Failing**: 135 (52.9%)
- **Improvement**: +36 tests (+14.2 percentage points)

### Target State
| Phase | Expected Pass Rate | Tests Passing | Status |
|-------|-------------------|---------------|--------|
| After Phase 1 | 45-50% | ~125 | ✅ Achieved (120) |
| After Phase 2 | 60-65% | ~160 | 🔄 In Progress |
| After Phase 3 | 75-80% | ~200 | Pending |
| After Phase 4 | 85-90% | ~225 | Pending |

### Key Improvements Made

| Fix | Tests Fixed | Details |
|-----|-------------|---------|
| Percentage resolution in flex items | +14 | wrapped-negative-space tests |
| Single-line align-content | +5 | singleline wrap tests |
| Overflow fallback | +5 | negative-space edge cases |
| Wrap-reverse positioning | +5 | wrap-reverse multi/single line |
| Various smaller fixes | +7 | Other edge cases |

### Key Test Categories to Track

```bash
# Run category-specific tests
make layout suite=flex 2>&1 | grep -E "yoga-aligncontent" | head -20
make layout suite=flex 2>&1 | grep -E "yoga-flexwrap-wrap-reverse" | head -10
make layout suite=flex 2>&1 | grep -E "yoga-alignitems-align-baseline" | head -10
```

---

## Part 5: Code Organization

### Completed File Changes

1. **`layout_flex.cpp`** - Core algorithm changes
   - ✅ Added `fallback_alignment()` and `fallback_justify()`
   - ✅ Fixed `align_content()` for single-line and wrap-reverse
   - ✅ Added Phase 7.5 container height finalization
   - ✅ Initialize axis sizes in `init_flex_container()`
   - ✅ Re-resolve percentages in `collect_and_prepare_flex_items()`

2. **`layout_flex_multipass.cpp`**
   - ✅ Call `collect_and_prepare_flex_items` for nested flex containers

3. **`view.hpp`**
   - ✅ Added `given_width_percent` and `given_height_percent` to `BlockProp`

4. **`resolve_css_style.cpp`**
   - ✅ Store raw percentage values during CSS resolution

5. **`view_pool.cpp`**
   - ✅ Initialize percentage fields to NAN in `alloc_block_prop()`

### Remaining File Changes

1. **`layout_flex.cpp`**
   - Add `freeze_constrained_items()` for two-pass distribution
   - Add `calculate_baseline_recursive()`

2. **`layout_flex.hpp`** - Header updates
   - Update `FlexLine` structure
   - Add frozen state tracking
   - Add baseline calculation declarations

---

## Appendix A: Yoga Source Files Reference

| Yoga File | Purpose | Lambda Equivalent |
|-----------|---------|-------------------|
| `CalculateLayout.cpp` | Main algorithm | `layout_flex.cpp` |
| `FlexLine.cpp/h` | Line calculation | Embedded in `layout_flex.cpp` |
| `Baseline.cpp/h` | Baseline calculation | To be added |
| `Align.h` | Alignment helpers | To be added |
| `FlexDirection.h` | Direction utilities | `layout_flex.hpp` |
| `BoundAxis.h` | Min/max clamping | `apply_flex_constraint()` |

## Appendix B: Test Command Reference

```bash
# Full flex test suite
make layout suite=flex

# Category-specific analysis
python3 << 'EOF'
import re
# ... analysis script from above
EOF

# Single test debugging
./lambda.exe layout test/layout/data/flex/yoga-aligncontent-align-content-stretch-row.html 2>&1

# Compare with browser reference
cat test/layout/reference/flex/yoga-aligncontent-align-content-stretch-row.json | python3 -m json.tool
```

---

## Conclusion

Yoga's flex implementation succeeds because of:

1. **Two-pass constraint resolution** - Handles min/max correctly
2. **Recursive baseline calculation** - Proper baseline alignment
3. **Overflow fallback** - Safe behavior on negative space
4. **Clean data structures** - `FlexLine` with running totals
5. **Comprehensive testing** - 300+ test cases covering edge cases

By adopting these patterns systematically, Lambda can achieve 85-90% compatibility with browser flexbox behavior, significantly improving real-world HTML/CSS rendering.
