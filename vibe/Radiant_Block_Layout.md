# Radiant Block Layout - Improvement Proposal

## Executive Summary

This document analyzes the current state of Radiant's block layout implementation based on:
1. The Radiant Layout Design document
2. Code analysis of `layout_block.cpp` and related files
3. Test results from `make layout suite=box` (0/252 tests passing)

The analysis identifies key issues preventing test success and proposes targeted improvements.

---

## 1. Current Architecture Overview

### 1.1 Block Layout Pipeline

```
layout_block() → layout_block_content() → setup_inline() → layout_block_inner_content()
                                                               ↓
                                     ┌─────────────────────────────────────────┐
                                     │ Display Inner Switch:                    │
                                     │ - FLOW: prescan_and_layout_floats() +   │
                                     │         layout_flow_node() for children │
                                     │ - FLEX: layout_flex_content()           │
                                     │ - GRID: layout_grid_content()           │
                                     │ - TABLE: layout_table()                 │
                                     └─────────────────────────────────────────┘
                                                               ↓
                                            finalize_block_flow()
```

### 1.2 Key Data Structures

| Structure | Purpose | Issues Identified |
|-----------|---------|-------------------|
| `Blockbox` | Block formatting context state | Margin collapsing needs refinement |
| `Linebox` | Inline formatting state | Float integration incomplete |
| `BlockFormattingContext` | BFC for floats (new system) | Parallel with legacy FloatContext |
| `FloatContext` | Legacy float positioning | Needs full replacement |

---

## 2. Test Results Analysis

**Suite: `box`** - 252 tests, 0 passing

### 2.1 Failure Categories

| Category | Test Count | Pass Rate | Root Cause |
|----------|------------|-----------|------------|
| Block-in-inline | ~90 tests | 0-80% | Missing inline splitting logic |
| BFC height | 3 tests | 14-20% | Float containment incomplete |
| BFC general | ~12 tests | 16-80% | BFC establishment rules |
| Block height | ~15 tests | 14-83% | Auto height calculation |
| Block width | ~10 tests | 42-80% | Width constraint resolution |
| Inline-block | ~25 tests | 0-25% | Baseline/sizing issues |
| Float tests | 3 tests | 20-80% | Float positioning |
| Various blocks | ~20 tests | 0-83% | Various layout issues |

### 2.2 Critical Failure Patterns

#### Pattern 1: Block-in-Inline Not Implemented
**Tests affected:** `block-in-inline-*` (90+ tests)

Per CSS 2.2 Section 9.2.1.1, when a block-level element is inside an inline element:
> "The inline box is broken around the block box(es), and the inline boxes before and after the break are enclosed in anonymous block boxes."

**Current code does NOT handle this.** The `layout_inline()` function simply iterates children without detecting block-level interruptions.

```cpp
// Current: layout_inline.cpp - no block detection
if (child) {
    do {
        layout_flow_node(lycon, child);  // Block children not handled specially
        child = child->next_sibling;
    } while (child);
}
```

**Required behavior:**
1. Detect block-level child within inline parent
2. Break inline into anonymous boxes before/after
3. Create three anonymous blocks: inline-before, block, inline-after

#### Pattern 2: BFC Height Expansion Incomplete
**Tests affected:** `block-formatting-context-height-*`

The test `block-formatting-context-height-001.htm` shows:
```html
<div id="container" style="position: absolute">
    <div id="float" style="float: left; margin-bottom: 50px; height: 50px">
    </div>
</div>
```
Expected: container height = 50 (float) + 50 (margin) = 100px

Current code handles this only partially:
```cpp
// layout_block.cpp:1070-1090
bool creates_bfc = block->scroller &&
    (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
     block->scroller->overflow_y != CSS_VALUE_VISIBLE);
```

**Issue:** `position: absolute` also creates BFC but isn't detected here.

#### Pattern 3: Inline-Block Baseline Issues
**Tests affected:** `inline-block-*` (0-25% pass rate)

Inline-block baseline calculation needs work:
- Empty inline-blocks should use bottom margin edge
- With content, use last line box baseline
- Current implementation doesn't distinguish these cases

#### Pattern 4: Float Positioning Edge Cases
**Tests affected:** `baseline_806_float_left`, `baseline_813_float_right`, `float-001`

The float pre-scanning logic (`prescan_and_layout_floats()`) uses rough heuristics:
```cpp
// layout_block.cpp:475-485
// Rough estimate: 8px per character
preceding_content_width += char_count * 8.0f;
// ...
float float_width = 100.0f;  // Conservative estimate
```

This causes incorrect float positioning when estimates are wrong.

---

## 3. Proposed Improvements

### 3.1 Priority 1: Implement Block-in-Inline (Anonymous Block Boxes)

**Estimated Impact:** 90+ tests

**Implementation approach:**

```cpp
// New function in layout_inline.cpp
void layout_inline_with_block_detection(LayoutContext* lycon, DomElement* inline_elem) {
    // Phase 1: Scan children for block-level elements
    ArrayList* segments = arraylist_new(4);  // inline/block segments
    InlineSegment current_inline = {0};

    for (DomNode* child = inline_elem->first_child; child; child = child->next_sibling) {
        DisplayValue display = resolve_display_value(child);
        if (is_block_level(display)) {
            // Save current inline segment
            if (current_inline.has_content) {
                arraylist_add(segments, create_inline_segment(current_inline));
            }
            // Add block segment
            arraylist_add(segments, create_block_segment(child));
            current_inline = {0};
        } else {
            current_inline.children.add(child);
            current_inline.has_content = true;
        }
    }

    // Phase 2: Create anonymous block structure
    if (segments->size > 1) {
        create_anonymous_block_structure(lycon, inline_elem, segments);
    } else {
        // No block interruption - normal inline layout
        layout_inline_normal(lycon, inline_elem);
    }
}
```

**Files to modify:**
- `layout_inline.cpp` - Add block detection
- `layout_block.cpp` - Add anonymous block creation
- `view.hpp` - Add anonymous block view type

### 3.2 Priority 2: Fix BFC Establishment Detection

**Estimated Impact:** 15+ tests

**Current code (incomplete):**
```cpp
bool creates_bfc = block->scroller &&
    (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
     block->scroller->overflow_y != CSS_VALUE_VISIBLE);
```

**Required (CSS 2.2 Section 9.4.1):**
```cpp
bool creates_bfc(ViewBlock* block) {
    // 1. Root element (html)
    if (!block->parent) return true;

    // 2. Float
    if (element_has_float(block)) return true;

    // 3. Absolutely positioned elements
    if (block->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED)) return true;

    // 4. Inline-blocks
    if (block->display.outer == CSS_VALUE_INLINE_BLOCK) return true;

    // 5. Table cells/captions
    if (block->display.inner == CSS_VALUE_TABLE_CELL ||
        block->display.inner == CSS_VALUE_TABLE_CAPTION) return true;

    // 6. Overflow != visible
    if (block->scroller &&
        (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
         block->scroller->overflow_y != CSS_VALUE_VISIBLE)) return true;

    // 7. Display: flow-root
    if (block->display.inner == CSS_VALUE_FLOW_ROOT) return true;

    // 8. Flex/Grid items
    // (handled separately)

    return false;
}
```

### 3.3 Priority 3: Unify Float Context Systems

**Estimated Impact:** 10+ tests

Current state: Two parallel float systems (`FloatContext` and `BlockFormattingContext`).

**Proposed solution:** Complete migration to `BlockFormattingContext`:

1. **Remove dual tracking:**
```cpp
// Current (layout_block.cpp:1130-1135)
layout_float_element(lycon, block);  // Legacy
if (lycon->bfc) {
    lycon->bfc->add_float(block);   // New - parallel tracking
}

// Proposed: Use only BFC
if (lycon->bfc) {
    bfc_position_float(lycon->bfc, block);
}
```

2. **Migrate float space queries:**
```cpp
// Replace float_space_at_y() with bfc->space_at_y()
```

3. **Remove legacy FloatContext entirely**

### 3.4 Priority 4: Inline-Block Baseline Calculation

**Estimated Impact:** 25+ tests

```cpp
float calculate_inline_block_baseline(ViewBlock* block) {
    // CSS 2.2 Section 10.8.1

    // Case 1: Overflow != visible or no in-flow content
    if (block->scroller &&
        block->scroller->overflow_y != CSS_VALUE_VISIBLE) {
        return block->height + (block->bound ? block->bound->margin.bottom : 0);
    }

    // Case 2: Has in-flow line boxes
    if (ViewText* last_line = find_last_line_box(block)) {
        return last_line->y + last_line->baseline;
    }

    // Case 3: No line boxes - use bottom margin edge
    return block->height + (block->bound ? block->bound->margin.bottom : 0);
}
```

### 3.5 Priority 5: Margin Collapsing Edge Cases

**Estimated Impact:** 10+ tests

Current margin collapsing misses several cases:

| Case | Current Status | Fix Required |
|------|---------------|--------------|
| First/last child | ✅ Implemented | - |
| Adjacent siblings | ✅ Implemented | - |
| Empty blocks | ❌ Missing | Through-collapsing |
| Clearance | ⚠️ Partial | Complete implementation |
| Negative margins | ❌ Missing | Absolute comparison |

```cpp
// Empty block through-collapsing
if (is_empty_block(block) &&
    !block->bound->border &&
    block->bound->padding.top == 0 &&
    block->bound->padding.bottom == 0) {
    // Collapse top and bottom margins through the empty block
    float collapsed = max(block->bound->margin.top, block->bound->margin.bottom);
    // ... apply collapsed margin to parent/siblings
}
```

---

## 4. Implementation Roadmap

### Phase 1: Foundation (Week 1-2)
- [ ] Implement `creates_bfc()` helper function
- [ ] Unify float context (migrate to BFC only)
- [ ] Fix float pre-scanning heuristics with accurate measurement

### Phase 2: Block-in-Inline (Week 3-4)
- [ ] Design anonymous block data structures
- [ ] Implement inline splitting algorithm
- [ ] Add tests for block-in-inline scenarios

### Phase 3: Inline-Block & Baseline (Week 5)
- [ ] Fix inline-block baseline calculation
- [ ] Handle vertical-align for inline-blocks
- [ ] Add shrink-to-fit width for inline-blocks

### Phase 4: Refinements (Week 6)
- [ ] Complete margin collapsing edge cases
- [ ] Handle negative margins correctly
- [ ] Performance optimization for float queries

---

## 5. Testing Strategy

### 5.1 Unit Test Focus Areas

```bash
# High-value test groups to target first:
make layout test=block-formatting-context-height-001  # BFC height
make layout test=block-in-inline-001                  # Basic block-in-inline
make layout test=inline-block-001                     # Basic inline-block
make layout test=float-001                            # Basic float
```

### 5.2 Success Metrics

| Milestone | Tests Passing | Target Date |
|-----------|---------------|-------------|
| Phase 1 complete | 30/252 (12%) | Week 2 |
| Phase 2 complete | 120/252 (48%) | Week 4 |
| Phase 3 complete | 180/252 (71%) | Week 5 |
| Phase 4 complete | 220/252 (87%) | Week 6 |

---

## 6. Code Quality Notes

### 6.1 Current Code Observations

**Strengths:**
- Clear separation between layout phases
- Good logging infrastructure
- Memory pool usage for allocations

**Areas for improvement:**
- Duplicate float handling code (legacy + BFC)
- Magic numbers in float pre-scanning
- Complex conditional chains for BFC detection

### 6.2 Recommended Refactoring

1. **Extract BFC logic to `layout_bfc.cpp`** - Currently split across multiple files
2. **Create `anonymous_box.cpp`** - For block-in-inline handling
3. **Add `baseline.cpp`** - Centralize baseline calculations

---

## 7. References

- [CSS 2.2 Section 9.2.1.1](https://www.w3.org/TR/CSS22/visuren.html#anonymous-block-level) - Anonymous block boxes
- [CSS 2.2 Section 9.4.1](https://www.w3.org/TR/CSS22/visuren.html#block-formatting) - Block formatting contexts
- [CSS 2.2 Section 10.6.7](https://www.w3.org/TR/CSS22/visudet.html#root-height) - Auto heights for BFC
- [CSS 2.2 Section 10.8.1](https://www.w3.org/TR/CSS22/visudet.html#line-height) - Inline-block baseline

---

## Appendix A: Test Categories Reference

| Category Pattern | Description | CSS Section |
|------------------|-------------|-------------|
| `block-in-inline-*` | Block boxes inside inline | 9.2.1.1 |
| `block-formatting-context-*` | BFC establishment and behavior | 9.4.1 |
| `block-non-replaced-*` | Block box sizing | 10.3, 10.6 |
| `block-replaced-*` | Replaced element sizing | 10.3, 10.6 |
| `inline-block-*` | Inline-block layout | 10.3.8, 10.6.6 |
| `height-*` | Height calculation | 10.6 |
| `width-*` | Width calculation | 10.3 |
| `float-*` | Float positioning | 9.5 |
| `blocks-*` | General block tests | Various |
