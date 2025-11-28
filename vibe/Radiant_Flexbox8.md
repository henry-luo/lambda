# Flex Layout Analysis and Refactoring Plan

**Date**: November 28, 2025
**Last Updated**: November 28, 2025
**Context**: Post-DOM/View tree unification refactoring

## Executive Summary

The flex layout implementation under `radiant/` was developed before the major design change that unified the View tree with the DOM tree. This analysis reviews the current implementation, identifies issues causing test failures, and proposes a comprehensive refactoring plan to modernize the flex layout system.

**Test Status (Updated)**:

| Suite | Passing | Total | Status |
|-------|---------|-------|--------|
| flex | 1/5 | 5 | 🔴 |
| flex-nest | 0/12 | 12 | 🔴 |
| baseline | 115/122 | 122 | 🟡 (7 table-related failures) |

**Current Individual Test Results (flex suite)**:
- ✅ flex_005_flex_grow - PASS
- ❌ flex_011_flex_shrink - 83.3% elements, 60% text
- ❌ flex_018_min_max_width - 100% elements, 75% text
- ❌ flex_019_nested_flex - 60% elements, 100% text
- ❌ flex_021_zero_basis - 83.3% elements, 75% text

**Progress Summary**:
| Task | Status | Notes |
|------|--------|-------|
| Task 1: Fix Flex-Shrink | ⚠️ Partial | Weighted formula added, still failing |
| Task 2: Eliminate Tree Redundancy | ✅ Done | `collect_and_prepare_flex_items()` created |
| Task 3: CSS Style Resolution | ✅ Done | `styles_resolved` flag + `reset_styles_resolved()` |
| Task 4: Consolidate Constraints | ✅ Done | `apply_flex_constraint()` unified function |
| Task 5: Enhanced Logging | ✅ Done | Comprehensive logging added |
| Task 6: Test-Driven Fixes | ⏳ In Progress |

---

## Part 1: Current Implementation Analysis

### 1.1 Architecture Overview

The flex layout system spans multiple files:
- **`layout_flex.cpp`** - Core flex algorithm (9 phases)
- **`layout_flex_multipass.cpp`** - Multi-pass orchestration (2 passes)
- **`layout_flex_measurement.cpp`** - Content measurement
- **`layout_block.cpp`** - Integration with block layout

### 1.2 Current Phase Structure

#### **Multi-Pass Structure (layout_flex_multipass.cpp)**

**PASS 1**: Create Views with Measurements
- Iterates through DOM children (`measure_child->next_sibling`)
- Calls `measure_flex_child_content()` - measures content
- Calls `layout_flow_node_for_flex()` - creates View nodes
- **Issue**: Double iteration over tree (DOM then View)

**PASS 2**: Run Enhanced Flex Algorithm
- Calls `layout_flex_container_with_nested_content()`
  - Which calls `run_enhanced_flex_algorithm()`
    - Which calls `layout_flex_container()` (core algorithm)
    - Then applies auto-margin centering
  - Then calls `layout_final_flex_content()` for nested content

#### **Core Flex Algorithm Phases (layout_flex.cpp: layout_flex_container)**

**Phase 1**: Collect flex items
- Traverses `container->first_child` to find flex items
- Filters out text nodes, absolute positioned, hidden items
- **Issue**: Still references old View hierarchy patterns

**Phase 2**: Sort items by order property
- Insertion sort by CSS `order` value
- Maintains document order for equal values

**Phase 2.5**: Resolve constraints
- NEW: Added constraint resolution
- Calls `apply_constraints_to_flex_items()`
- Resolves min/max width/height for all items

**Phase 3**: Create flex lines
- Handles wrapping (wrap/nowrap/wrap-reverse)
- Calculates flex basis for each item
- Groups items into lines based on available space

**Phase 4**: Resolve flexible lengths
- **CRITICAL**: Implements iterative constraint resolution
- Calculates flex-grow/flex-shrink
- **Bug**: Flex-shrink calculation is incorrect (see test failures)

**Phase 5**: Calculate cross sizes for lines
- Determines height of each flex line
- Handles stretch/auto sizing

**Phase 6**: Align items on main axis
- Implements justify-content
- **Issue**: Gap handling has issues with positioning

**Phase 7**: Align items on cross axis
- Implements align-items/align-self
- Handles baseline alignment

**Phase 8**: Align content
- Aligns flex lines (multi-line containers)
- Implements align-content

**Phase 9**: Wrap-reverse handling
- **Note**: Comment says "moved to line positioning phase"
- May be incomplete

### 1.3 Critical Issues Identified

#### **Issue 1: Incorrect Flex-Shrink Algorithm**

**Test**: flex_011_flex_shrink
**Symptom**: Items shrink with wrong proportions

Browser (correct):
```
Item 0: w=150 (flex-shrink: 0) - NO SHRINK ✓
Item 1: w=65.36 (flex-shrink: 1)
Item 2: w=65.36 (flex-shrink: 2)
```

Lambda (wrong):
```
Item 0: w=150 ✓
Item 1: w=92 ✗ (should be 65.36)
Item 2: w=34 ✗ (should be 65.36)
```

**Root Cause** (layout_flex.cpp:755-946):
```cpp
// Current implementation in resolve_flexible_lengths()
// Line 820-946: Iterative constraint resolution
```

The flex-shrink calculation doesn't correctly apply the formula:
```
shrink_amount_per_item = (negative_free_space * item_flex_shrink * item_flex_basis) /
                         (sum of all items' flex_shrink * flex_basis)
```

The current code at line 879-893 uses:
```cpp
float flex_factor = frozen[i] ? 0.0f : item->fi->flex_shrink;
total_flex_factor += flex_factor;
```

But it should weight by **both** flex-shrink AND flex-basis:
```cpp
float weighted_flex_shrink = flex_shrink * flex_basis;
total_weighted_shrink += weighted_flex_shrink;
// Then distribute: shrink_amount = (negative_free * weighted_flex_shrink) / total_weighted_shrink
```

#### **Issue 2: DOM/View Tree Redundancy**

**Current**: Two separate iterations
1. PASS 1: Iterate DOM tree (`measure_child->next_sibling`)
2. Phase 1: Iterate View tree (`container->first_child`)

**Problem**: Since View tree IS the DOM tree now (unified design), this is redundant.

**Files affected**:
- `layout_flex_multipass.cpp:461-487` (PASS 1 DOM iteration)
- `layout_flex.cpp:274-382` (Phase 1 View iteration)

#### **Issue 3: CSS Style Resolution Timing**

**Current**: Styles resolved multiple times
- Once during DOM parsing
- Again during measurement phase
- Possibly during layout phase

**Issue**: With unified tree, styles only need resolution once at the beginning.

#### **Issue 4: Constraint Resolution Scattered**

Constraints (min/max width/height) are handled in multiple places:
1. Phase 2.5: `apply_constraints_to_flex_items()`
2. Phase 4: Inside iterative loop in `resolve_flexible_lengths()`
3. Various helper functions

**Problem**: Duplication, potential conflicts, hard to debug.

#### **Issue 5: Gap Handling Complexity**

Gap calculations are scattered across:
- `calculate_gap_space()` - helper function
- `align_items_main_axis()` - applies gaps during positioning
- Various position calculations

**Issue**: Hard to verify correctness, positioning bugs like in flex_011.

---

## Part 2: Test Failure Analysis

### Test 1: flex_011_flex_shrink ❌
**Status**: 66.7% elements, 20% text
**Issue**: Incorrect flex-shrink weighted distribution (explained above)
**Priority**: HIGH - Core flex algorithm bug

### Test 2: flex_018_min_max_width ❌
**Status**: 100% elements, 75% text
**Issue**: Min/max constraints not properly enforced during flex-grow
**Likely cause**: Phase 4 constraint resolution doesn't correctly clamp during growth
**Priority**: HIGH - Constraint handling bug

### Test 3: flex_019_nested_flex ❌
**Status**: 30% elements, 0% text
**Issue**: Nested flex containers not laying out correctly
**Likely cause**: Multi-pass algorithm issues with nested content measurement
**Priority**: MEDIUM - Multi-pass coordination issue

### Test 4: flex_021_zero_basis ❌
**Status**: 83.3% elements, 75% text
**Issue**: `flex-basis: 0%` not handled correctly
**Likely cause**: Flex basis calculation doesn't handle 0 basis properly (layout_flex.cpp:427-495)
**Priority**: HIGH - Flex basis calculation bug

---

## Part 3: Refactoring Plan

### 3.1 Guiding Principles

1. **Single Tree Traversal**: Leverage unified DOM/View tree - iterate once
2. **Clear Phase Separation**: Each phase has distinct responsibility
3. **Style Resolution Once**: Resolve CSS at the beginning, cache results
4. **Correctness First**: Match CSS Flexbox specification exactly
5. **Maintainability**: Clear code structure, comprehensive logging

### 3.2 Proposed Architecture

#### **New Structure: Single-Pass with Sub-Phases**

```
layout_flex_content() - Entry point
│
├─ [PREP] Initialize flex container state
│   ├─ Resolve CSS styles (ONE TIME for all children)
│   ├─ Initialize flex properties from container
│   └─ Set up main/cross axis based on flex-direction
│
├─ [PHASE 1] Collect & Prepare Flex Items
│   ├─ Single iteration over first_child → next_sibling
│   ├─ Filter: Skip text nodes, absolute, display:none
│   ├─ Create/verify View nodes (already created in unified tree)
│   ├─ Resolve flex item properties (flex-grow, flex-shrink, flex-basis)
│   └─ Calculate intrinsic sizes (min-content, max-content)
│
├─ [PHASE 2] Determine Flex Lines
│   ├─ Calculate flex basis for each item
│   ├─ Apply wrapping (nowrap/wrap/wrap-reverse)
│   └─ Group items into flex lines
│
├─ [PHASE 3] Resolve Main Sizes (CRITICAL - FIX HERE)
│   ├─ For each line:
│   │   ├─ Calculate free space (container - sum(flex_basis) - gaps)
│   │   ├─ IF free_space > 0: Apply flex-grow (weighted distribution)
│   │   ├─ IF free_space < 0: Apply flex-shrink (WEIGHTED BY BASIS!)
│   │   └─ Clamp all sizes with min/max constraints (iterative until stable)
│   └─ Set main-axis sizes for all items
│
├─ [PHASE 4] Resolve Cross Sizes
│   ├─ For each flex line, determine line cross size
│   ├─ Handle align-items: stretch → set item cross sizes
│   └─ Handle align-content for multi-line containers
│
├─ [PHASE 5] Main-Axis Positioning
│   ├─ Apply justify-content (flex-start, center, space-between, etc.)
│   ├─ Apply gaps between items
│   └─ Handle auto margins on main axis
│
├─ [PHASE 6] Cross-Axis Positioning
│   ├─ Position flex lines (align-content)
│   ├─ Position items within lines (align-items/align-self)
│   └─ Handle baseline alignment
│
└─ [PHASE 7] Finalize & Layout Nested Content
    ├─ Apply final positions to View coordinates (x, y)
    ├─ For nested flex containers: Recursively call layout_flex_content()
    └─ For nested block content: Call layout_block_content()
```

### 3.3 Specific Refactoring Tasks

#### **Task 1: Fix Flex-Shrink Algorithm** (HIGHEST PRIORITY)

**File**: `layout_flex.cpp`
**Function**: `resolve_flexible_lengths()` (line 755-946)

**Current buggy code** (line 879-893):
```cpp
// Calculate total flex factor for unfrozen items
float total_flex_factor = 0.0f;
for (int i = 0; i < line->item_count; i++) {
    float flex_factor = frozen[i] ? 0.0f :
        (free_space > 0 ? item->fi->flex_grow : item->fi->flex_shrink);
    total_flex_factor += flex_factor;
}
```

**Fix** - Apply correct CSS Flexbox formula:
```cpp
// For flex-shrink, must weight by flex-basis (scaled flex shrink factor)
float total_scaled_shrink_factor = 0.0f;
for (int i = 0; i < line->item_count; i++) {
    if (!frozen[i] && free_space < 0) {
        ViewGroup* item = (ViewGroup*)line->items[i];
        int basis = calculate_flex_basis(item, flex_layout);
        float scaled_shrink = item->fi->flex_shrink * basis;
        total_scaled_shrink_factor += scaled_shrink;
    }
}

// Then distribute negative space proportionally
for (int i = 0; i < line->item_count; i++) {
    if (!frozen[i] && free_space < 0 && total_scaled_shrink_factor > 0) {
        ViewGroup* item = (ViewGroup*)line->items[i];
        int basis = calculate_flex_basis(item, flex_layout);
        float scaled_shrink = item->fi->flex_shrink * basis;
        int shrink_amount = (int)((abs(free_space) * scaled_shrink) / total_scaled_shrink_factor);
        new_main_size = basis - shrink_amount;
        // Then apply constraints...
    }
}
```

**Reference**: [CSS Flexbox Level 1 - Section 9.7](https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths)

#### **Task 2: Eliminate Redundant Tree Traversals** ✅ COMPLETED

**Status**: COMPLETED on November 28, 2025

**Files Modified**:
- `layout_flex.cpp` - Added `collect_and_prepare_flex_items()` function
- `layout_flex.hpp` - Added function declaration
- `layout_flex_multipass.cpp` - Updated `layout_flex_content()` to use unified function

**Implementation**:
Created new unified function `collect_and_prepare_flex_items()` that combines:
1. Content measurement (`measure_flex_child_content()`)
2. View creation (`create_lightweight_flex_item_view()`)
3. Flex item collection (filtering, caching, sizing)

**Key Changes**:
```cpp
// NEW: Single-pass collection in layout_flex.cpp
int collect_and_prepare_flex_items(LayoutContext* lycon,
                                   FlexContainerLayout* flex_layout,
                                   ViewBlock* container);

// MODIFIED: layout_flex_content() now uses unified pass
log_info("=== UNIFIED PASS: Collect, measure, and prepare flex items ===");
int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, block);

// MODIFIED: layout_flex_container() skips collection if already done
if (flex_layout->item_count > 0 && flex_layout->flex_items) {
    // Items already collected - skip Phase 1
} else {
    // Legacy fallback for backward compatibility
    item_count = collect_flex_items(flex_layout, container, &items);
}
```

**Benefits**:
- Eliminates redundant tree traversal (was iterating twice)
- Reduces code duplication
- Single point of truth for flex item preparation
- Backward compatible (old `collect_flex_items()` kept for edge cases)

**Original proposed function** (for reference):
```cpp
// In layout_flex.cpp
void collect_and_prepare_flex_items(
    LayoutContext* lycon,
    FlexContainerLayout* flex_layout,
    ViewBlock* container
) {
    log_enter();

    View* child = container->first_child;
    int item_index = 0;

    while (child) {
        // Skip non-element children
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }

        ViewGroup* item = (ViewGroup*)child->as_element();

        // Filter: absolute, fixed, hidden
        if (should_skip_flex_item(item)) {
            child = child->next_sibling;
            continue;
        }

        // Resolve CSS styles ONCE (if not already done)
        if (!item->css_resolved) {
            resolve_flex_item_styles(lycon, item);
            item->css_resolved = true;
        }

        // Measure intrinsic sizes (needed for flex-basis: auto)
        measure_flex_item_intrinsic_sizes(lycon, item);

        // Add to flex items array
        ensure_flex_items_capacity(flex_layout, item_index + 1);
        flex_layout->flex_items[item_index++] = (View*)item;

        child = child->next_sibling;
    }

    flex_layout->item_count = item_index;
    log_leave();
}
```

**Note**: The old PASS 1 loop in `layout_flex_multipass.cpp` has been replaced with a single call to `collect_and_prepare_flex_items()`. The `collect_flex_items()` function is kept for backward compatibility but skipped when items are already collected.

#### **Task 3: CSS Style Resolution - Once Only** ✅ COMPLETED

**Implementation**:
Rather than creating a new file, the solution leverages the existing `dom_node_resolve_style()` function with a simple guard flag.

**Changes Made**:

1. **`lambda/input/css/dom_element.hpp`** - Added flag to DomElement:
```cpp
bool styles_resolved;  // Flag to track if styles resolved in current layout pass
```

2. **`radiant/layout.cpp`** - Modified `dom_node_resolve_style()`:
```cpp
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    if (node && node->is_element()) {
        DomElement* dom_elem = node->as_element();
        if (dom_elem && dom_elem->specified_style) {
            // Check if styles already resolved in this layout pass
            if (dom_elem->styles_resolved) {
                log_debug("[CSS] Skipping style resolution for <%s> - already resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
                return;
            }
            resolve_lambda_css_styles(dom_elem, lycon);
            dom_elem->styles_resolved = true;
            log_debug("[CSS] Resolved styles for <%s> - marked as resolved",
                dom_elem->tag_name ? dom_elem->tag_name : "unknown");
        }
    }
}
```

3. **`radiant/layout.cpp`** - Added reset functions:
```cpp
static void reset_styles_resolved_recursive(DomNode* node) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        elem->styles_resolved = false;
        DomNode* child = elem->first_child;
        while (child) {
            reset_styles_resolved_recursive(child);
            child = child->next_sibling;
        }
    }
}

void reset_styles_resolved(DomDocument* doc) {
    if (!doc || !doc->root) return;
    log_debug("[CSS] Resetting styles_resolved flags for all elements");
    reset_styles_resolved_recursive(doc->root);
}
```

4. **`radiant/layout.cpp`** - Called reset at layout start in `layout_init()`:
```cpp
void layout_init(LayoutContext* lycon, DomDocument* doc, UiContext* uicon) {
    // ... initialization ...

    // Reset styles_resolved flags for all elements before layout
    reset_styles_resolved(doc);

    // ... rest of initialization ...
}
```

**Result**: CSS style resolution now happens exactly once per element per layout pass, regardless of how many times `dom_node_resolve_style()` is called from different code paths (layout_block, layout_inline, layout_table, layout_flex_measurement, etc.).

#### **Task 4: Consolidate Constraint Handling**

**New function** in `layout_flex.cpp`:
```cpp
// Apply min/max constraints to a computed flex size
// Returns: clamped size
int apply_flex_constraints(
    ViewGroup* item,
    int computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout
) {
    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    int min_size, max_size;
    if (is_main_axis) {
        if (is_horizontal) {
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width;
        } else {
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height;
        }
    } else {
        if (is_horizontal) {
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height;
        } else {
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width;
        }
    }

    // Clamp: max(min_size, min(computed_size, max_size))
    int clamped = computed_size;
    if (min_size > 0) clamped = max(clamped, min_size);
    if (max_size > 0 && max_size < INT_MAX) clamped = min(clamped, max_size);

    log_debug("Constraint clamp: computed=%d, min=%d, max=%d, final=%d",
              computed_size, min_size, max_size, clamped);

    return clamped;
}
```

**Use this function** in:
- Phase 3 (resolve flexible lengths) - during flex-grow/shrink
- Phase 4 (resolve cross sizes) - when stretch applies
- Anywhere a size is computed

**Implementation**: ✅ COMPLETED

The following consolidated functions were added to `layout_flex.cpp`:

1. **`apply_flex_constraint()`** - Single source of truth for constraint clamping:
   - Takes item, computed size, axis flag, and flex layout context
   - Returns optional hit_min/hit_max flags for iteration control
   - Handles both main and cross axis constraints based on flex direction

2. **`apply_stretch_constraint()`** - Specialized for ALIGN_STRETCH:
   - Applies cross-axis constraints when stretching items
   - Uses `apply_flex_constraint()` internally

**Refactored Code**:
- `resolve_flexible_lengths()` - replaced 30+ lines of inline constraint checking with single function call
- `align_items_cross_axis()` - ALIGN_STRETCH case now uses `apply_stretch_constraint()`

**Header Updates**:
- Added declarations to `layout_flex.hpp` for external use

#### **Task 5: Enhanced Logging & Debugging** ✅ COMPLETED

Add comprehensive `log_info()` and `log_debug()` statements:

```cpp
// At each phase boundary
log_info("=== FLEX PHASE %d: %s ===", phase_num, phase_name);

// For each item in critical loops
log_debug("Item %d: basis=%d, grow=%.2f, shrink=%.2f, computed=%d",
          i, basis, flex_grow, flex_shrink, computed_size);

// Before/after positioning
log_debug("Position BEFORE: item %d at (%.1f, %.1f)", i, item->x, item->y);
// ... positioning code ...
log_debug("Position AFTER: item %d at (%.1f, %.1f)", i, item->x, item->y);

// Free space calculations
log_info("Line %d: container_size=%d, total_basis=%d, gaps=%d, free_space=%d",
         line_idx, container_size, total_basis, gap_space, free_space);
```

This will make debugging much easier, especially for test failures.

#### **Task 6: Test-Driven Fixes**

For each failing test:
1. Enable detailed logging (`log.conf` → DEBUG level)
2. Run test: `./lambda.exe layout test/layout/data/flex/flex_XXX.html`
3. Compare output with reference
4. Identify exact calculation that differs
5. Fix the specific phase/function
6. Verify test passes
7. Run ALL flex tests to ensure no regressions

**Order**:
1. ⚠️ Fix flex_011_flex_shrink (Task 1) - Weighted formula added, still issues
2. ❌ Fix flex_021_zero_basis (flex-basis: 0% handling) - Not fixed
3. ❌ Fix flex_018_min_max_width (constraint application during grow) - Not fixed
4. ❌ Fix flex_019_nested_flex (multi-pass nested content) - Not fixed

---

## Part 4: Implementation Roadmap

### Phase A: Bug Fixes (1-2 days)
**Goal**: Fix critical flex algorithm bugs
**Status**: ⚠️ In Progress

1. ⚠️ Implement correct flex-shrink formula (Task 1) - Partial
2. ❌ Fix flex-basis: 0% handling
3. ❌ Fix constraint application during flex-grow
4. ✅ Add comprehensive logging
5. ❌ Verify all flex tests pass

**Deliverable**: All 5 flex tests passing

### Phase B: Refactoring (2-3 days)
**Goal**: Modernize for unified DOM/View tree
**Status**: ⏳ In Progress

1. ✅ Merge tree traversals (Task 2) - COMPLETED: `collect_and_prepare_flex_items()`
2. ❌ Implement single-time style resolution (Task 3)
3. ⚠️ Consolidate constraint handling (Task 4) - Partial
4. ❌ Simplify multi-pass structure
5. ✅ Update documentation - Design doc created

**Deliverable**: Clean, maintainable flex layout code

### Phase C: Optimization (1-2 days)
**Goal**: Improve performance
**Status**: ⚠️ Partial

1. ⚠️ Cache intrinsic size measurements - Basic caching exists
2. ❌ Optimize flex line calculations
3. ❌ Reduce memory allocations
4. ❌ Profile with complex layouts

**Deliverable**: Faster flex layout engine

### Phase D: Extended Testing (ongoing)
**Goal**: Comprehensive test coverage
**Status**: ⚠️ Partial

1. ✅ Add more flex test cases - flex-nest suite (12 tests)
2. ❌ Test edge cases (0 basis, huge gaps, etc.)
3. ❌ Test nested flex containers - 0/12 passing
4. ❌ Test with grid tests to ensure no regressions

**Deliverable**: Robust flex layout implementation

---

## Part 5: Recommendations

### Immediate Actions
1. **Fix flex-shrink bug** (Task 1) - This is blocking 4 tests
2. **Add detailed logging** - Will speed up debugging other issues
3. **Document flex algorithm phases** - In code comments

### Medium-Term
1. **Refactor for unified tree** - Eliminate redundancy
2. **Create flex test suite** - More comprehensive coverage
3. **Performance profiling** - Identify bottlenecks

### Long-Term
1. **Consider CSS Grid integration** - Similar refactoring needed
2. **Alignment with Web standards** - Keep up with CSS specs
3. **Automated regression testing** - CI/CD for layout tests

---

## Appendix A: Flex Algorithm Correctness Checklist

### Flex-Shrink Formula (CSS Spec Section 9.7.2)
```
scaled_shrink_factor[i] = flex_shrink[i] * flex_basis[i]
shrink_amount[i] = (|negative_free_space| * scaled_shrink_factor[i]) /
                   (sum of all scaled_shrink_factors)
final_size[i] = flex_basis[i] - shrink_amount[i]
```

### Flex-Grow Formula (CSS Spec Section 9.7.1)
```
grow_amount[i] = (positive_free_space * flex_grow[i]) / (sum of all flex_grow values)
final_size[i] = flex_basis[i] + grow_amount[i]
```

### Constraint Application
- Must clamp after EACH iteration in iterative algorithm
- Must check if clamping changes the result → re-run if so
- Max 10 iterations to prevent infinite loops

### Gap Handling
- Gaps apply BETWEEN items, not before/after
- For N items: (N-1) gaps
- Total gap space = gap_size * (N - 1)
- Free space = container_size - sum(flex_basis) - total_gap_space

---

## Appendix B: Reference Resources

1. **CSS Flexbox Specification**: https://www.w3.org/TR/css-flexbox-1/
2. **MDN Flexbox Guide**: https://developer.mozilla.org/en-US/docs/Learn/CSS/CSS_layout/Flexbox
3. **Flexbox Algorithm Explanation**: https://www.w3.org/TR/css-flexbox-1/#layout-algorithm
4. **Lambda CSS Documentation**: `doc/Lambda_Reference.md`

---

**End of Analysis**

This document provides a comprehensive roadmap for fixing and refactoring the flex layout system. The immediate priority should be fixing the flex-shrink bug (Task 1), which will unblock most failing tests. Then proceed with the refactoring tasks to modernize the codebase for the unified DOM/View tree architecture.
