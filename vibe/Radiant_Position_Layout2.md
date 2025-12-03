# Radiant Position Layout Improvement Plan

## Executive Summary

This document proposes a structural redesign of the CSS positioning subsystem in Radiant to improve compliance with CSS 2.2 specifications, particularly for `float`, `clear`, `position: relative`, and `position: absolute` layouts. The current implementation passes only **6 out of 216 position tests** (2.8%), indicating fundamental architectural issues that require systematic improvement.

---

## 1. Current State Analysis

### 1.1 Test Results Summary

| Category | Pass | Fail | Pass Rate |
|----------|------|------|-----------|
| Float basic | 1 | 5 | ~17% |
| Float applies-to | 1 | 14 | ~7% |
| Float sizing | 0 | 12 | 0% |
| Float complex | 2 | 100+ | ~2% |
| Position absolute | 0 | 3 | 0% |
| Position relative | 0 | 2 | 0% |
| Clear | 0 | 4 | 0% |

### 1.2 Key Failure Patterns Identified

**A. Float Positioning Issues**
1. Floats not taken out of normal flow correctly during inline layout
2. Text does not flow around floats (no line boundary adjustment)
3. Float stacking rules (CSS 2.2 Section 9.5.1 Rules 1-8) not fully implemented
4. Shrink-to-fit width calculation inconsistent

**B. Float Context Problems**
1. `FloatContext` created per-block but needs inheritance for nested blocks
2. `adjust_line_for_floats()` called but coordinate transformation incorrect
3. Float space queries don't account for float-in-normal-flow timing

**C. Clear Property Issues**
1. `clear` applied after child layout instead of affecting initial Y position
2. Parent `advance_y` not updated when child clears

**D. Inline/Float Interaction**
1. Inline spans don't trigger float at encounter point
2. Floats placed at block layout time, not at content encounter time

---

## 2. Root Cause Analysis

### 2.1 Architectural Mismatch

The current implementation treats floats as **block-level positioned elements**, but CSS floats are fundamentally **inline-level layout disruptors**. The key insight from CSS 2.2:

> "A float is a box that is shifted to the left or right **on the current line**."

Current code flow:
```
layout_block() → layout_block_content() → layout_float_element()
                                              ↓
                                    (adds to FloatContext AFTER layout)
```

Should be:
```
layout_flow_node() → detect_float() → position_float_at_current_line()
                                              ↓
                                    (affect subsequent line layout IMMEDIATELY)
```

### 2.2 Missing CSS 2.2 Float Rules

CSS 2.2 Section 9.5.1 defines 8 rules for float positioning:

| Rule | Description | Implemented? |
|------|-------------|--------------|
| 1 | Left float outer left edge may not be to the left of containing block left | Partial |
| 2 | If current float is left-floated and earlier left floats exist, place right of them OR below | **No** |
| 3 | Right of left float may not be to right of any adjacent right float's left edge | **No** |
| 4 | Float's top may not be higher than top of its containing block | Partial |
| 5 | Float's top may not be higher than any earlier block/line box | **No** |
| 6 | Float's top may not be higher than any earlier float's top | **No** |
| 7 | Left float with left floats to its left: place edge below of them if no room | **No** |
| 8 | Float must be placed as high as possible | **No** |

### 2.3 Coordinate System Confusion

Current code mixes coordinate systems:
- `FloatBox` stores container-relative coordinates
- `Linebox` stores block-relative coordinates
- `adjust_line_for_floats()` attempts conversion but has bugs

---

## 3. Proposed Architecture

### 3.1 New Float Model

#### 3.1.1 Float Encounter Point (FEP)

A float should be **encountered** and **positioned** when inline content reaches it, not when its block container is laid out.

```cpp
struct FloatEncounter {
    ViewBlock* float_element;
    float encounter_y;        // Y position when float was encountered (relative to BFC)
    CssEnum float_side;       // CSS_VALUE_LEFT or CSS_VALUE_RIGHT
};
```

#### 3.1.2 Block Formatting Context (BFC) Owner

Every BFC maintains its own float state. BFC is established by:
- Root element
- Floats (`float` != `none`)
- Absolutely positioned elements
- Block containers with `overflow` != `visible`
- `display: flow-root`
- Flex/grid items
- Table cells

```cpp
struct BlockFormattingContext {
    ViewBlock* establishing_element;
    
    // Float lists sorted by margin_box_top
    std::vector<FloatBox> left_floats;
    std::vector<FloatBox> right_floats;
    
    // For efficient space queries
    float highest_float_bottom;  // For early exit optimization
    
    // BFC coordinate origin (absolute position of establishing element)
    float origin_x, origin_y;
    
    // Content area bounds (relative to origin)
    float content_left, content_right;
    float content_top;  // content_bottom is unbounded
};
```

### 3.2 Revised Float Positioning Algorithm

#### Phase 1: Float Encounter
When `layout_flow_node()` encounters a float:
1. Layout the float element (determine its width/height)
2. Call `bfc_encounter_float()` with current line position

#### Phase 2: Float Placement
```cpp
void bfc_encounter_float(BlockFormattingContext* bfc, 
                         ViewBlock* float_elem, 
                         float current_line_y) {
    // CSS 2.2 Rule 4 & 5 & 6: Float top >= max(containing_block_top, current_line_top, earlier_floats_top)
    float min_y = max(bfc->content_top, current_line_y);
    for (auto& f : bfc->left_floats) min_y = max(min_y, f.margin_box_top);
    for (auto& f : bfc->right_floats) min_y = max(min_y, f.margin_box_top);
    
    // Find Y where float fits horizontally
    float placement_y = find_y_for_float(bfc, float_elem, min_y);
    
    // CSS 2.2 Rule 1 & 2: Position on left/right edge, avoiding other floats
    float x = position_float_horizontally(bfc, float_elem, placement_y);
    
    // Update float element position
    float_elem->x = x;
    float_elem->y = placement_y;
    
    // Add to BFC float list
    add_float_to_bfc(bfc, float_elem);
}
```

### 3.3 Revised Line Layout with Float Awareness

```cpp
struct Linebox {
    // ... existing fields ...
    
    // Float-aware bounds (may be narrower than container)
    float effective_left;   // Adjusted for left floats
    float effective_right;  // Adjusted for right floats
    
    // Current line's float intrusion state
    bool has_float_intrusion;
};

void update_line_bounds_for_floats(LayoutContext* lycon) {
    BlockFormattingContext* bfc = get_current_bfc(lycon);
    if (!bfc) return;
    
    float line_y = lycon->block.advance_y + get_block_offset_in_bfc(lycon);
    float line_height = lycon->block.line_height;
    
    // Query available space at current line position
    FloatAvailableSpace space = bfc_space_at_y(bfc, line_y, line_height);
    
    // Convert from BFC coordinates to local block coordinates
    lycon->line.effective_left = space.left - get_block_left_in_bfc(lycon);
    lycon->line.effective_right = space.right - get_block_left_in_bfc(lycon);
    
    // Adjust advance_x if needed
    if (lycon->line.advance_x < lycon->line.effective_left) {
        lycon->line.advance_x = lycon->line.effective_left;
    }
}
```

### 3.4 Clear Property Handling

The `clear` property must be processed **before** laying out the clearing element's content:

```cpp
void apply_clear_before_layout(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position || block->position->clear == CSS_VALUE_NONE) return;
    
    BlockFormattingContext* bfc = get_current_bfc(lycon);
    if (!bfc) return;
    
    float clear_y = bfc_find_clear_position(bfc, block->position->clear);
    float current_y = lycon->block.advance_y + get_block_offset_in_bfc(lycon);
    
    if (clear_y > current_y) {
        float delta = clear_y - current_y;
        block->y += delta;
        lycon->block.advance_y += delta;
        
        // Update parent's advance_y for container height
        update_parent_advance_for_clear(lycon, delta);
    }
}
```

---

## 4. Implementation Plan

### Phase 1: BFC Infrastructure (Week 1)

**Goal:** Establish proper BFC tracking throughout layout

1. **Create `BlockFormattingContext` struct**
   - File: `radiant/layout_bfc.hpp` (new)
   - Lifecycle management
   - Float list management

2. **Identify BFC establishment points**
   - Modify `layout_block()` to detect BFC-establishing conditions
   - Create BFC for root, floats, abs pos, overflow != visible

3. **BFC inheritance**
   - Non-BFC-establishing blocks inherit parent's BFC
   - Track BFC in `LayoutContext`

### Phase 2: Float Positioning Rewrite (Week 2)

**Goal:** Correct float placement according to CSS 2.2 rules

1. **Float encounter at correct time**
   - Modify `layout_flow_node()` to detect and handle floats
   - Move float positioning from `layout_block()` to `layout_flow_node()`

2. **Implement CSS 2.2 Section 9.5.1 rules**
   - Rule-by-rule implementation with tests
   - `find_y_for_float()` implementing Rules 4-8
   - `position_float_horizontally()` implementing Rules 1-3

3. **Float intrinsic sizing**
   - Use existing `calculate_fit_content_width()` correctly
   - Apply shrink-to-fit before positioning

### Phase 3: Line Layout Integration (Week 3)

**Goal:** Text flows around floats correctly

1. **Line boundary adjustment**
   - Call `update_line_bounds_for_floats()` at line start
   - Track `effective_left`/`effective_right` in `Linebox`

2. **Word wrap with floats**
   - Modify `layout_text()` to respect float-adjusted bounds
   - Handle word wrap that causes float encounter mid-line

3. **Line break past floats**
   - When line is full due to floats, break and try next Y

### Phase 4: Clear and Integration (Week 4)

**Goal:** Clear works correctly, all tests pass

1. **Clear before layout**
   - Move clear handling to before child layout
   - Proper parent advance_y propagation

2. **BFC height expansion**
   - Containers containing only floats get correct height
   - `overflow: hidden` creates BFC and contains floats

3. **Test validation**
   - Run full position test suite
   - Fix edge cases

---

## 5. Data Structure Changes

### 5.1 New: `layout_bfc.hpp`

```cpp
#pragma once
#include "view.hpp"
#include <vector>

struct FloatBox {
    ViewBlock* element;
    float margin_box_left, margin_box_top;
    float margin_box_right, margin_box_bottom;
    CssEnum float_side;
};

struct BlockFormattingContext {
    ViewBlock* establishing_element;
    BlockFormattingContext* parent_bfc;  // For nested BFC
    
    std::vector<FloatBox> left_floats;
    std::vector<FloatBox> right_floats;
    
    float origin_x, origin_y;           // Absolute position
    float content_left, content_right;  // Relative to origin
    
    // API
    void add_float(ViewBlock* element);
    FloatAvailableSpace space_at_y(float y, float height);
    float find_y_for_float(ViewBlock* element, float min_y);
    float find_clear_position(CssEnum clear);
};

// Factory function
BlockFormattingContext* create_bfc(ViewBlock* establishing_element);
```

### 5.2 Modified: `LayoutContext`

```cpp
struct LayoutContext {
    // ... existing fields ...
    
    // Replace current_float_context with:
    BlockFormattingContext* bfc;  // Current BFC (inherited or owned)
    bool owns_bfc;                // True if this block created the BFC
    
    // Remove: FloatContext* current_float_context;
};
```

### 5.3 Modified: `Linebox`

```cpp
struct Linebox {
    float left, right;           // Original container bounds
    float effective_left;        // Adjusted for left floats
    float effective_right;       // Adjusted for right floats
    // ... rest unchanged ...
};
```

---

## 6. Key Algorithm Changes

### 6.1 Float Placement (CSS 2.2 Section 9.5.1)

```cpp
// Implements Rules 4, 5, 6, 8: Find highest valid Y for float
float BlockFormattingContext::find_y_for_float(ViewBlock* element, float min_y) {
    CssEnum side = element->position->float_prop;
    float total_width = element->width + margins(element);
    
    float y = min_y;
    int iterations = 0;
    
    while (iterations++ < 1000) {  // Safety limit
        FloatAvailableSpace space = space_at_y(y, element->height);
        float available = space.right - space.left;
        
        if (available >= total_width) {
            // Check if placement is valid (no overlap)
            float x = (side == CSS_VALUE_LEFT) ? 
                      space.left : 
                      space.right - total_width;
            if (is_valid_placement(element, x, y)) {
                return y;
            }
        }
        
        // Move down to where more space might be available
        y = find_next_float_bottom(y);
        if (y == INFINITY) break;
    }
    
    return y;
}
```

### 6.2 Line Layout with Float Awareness

```cpp
void layout_flow_node(LayoutContext* lycon, DomNode* node) {
    if (node->is_element()) {
        DomElement* elem = (DomElement*)node;
        DisplayValue display = resolve_display_value(elem);
        
        // Check for float BEFORE normal layout dispatch
        if (elem->is_block() && element_has_float((ViewBlock*)elem)) {
            // Float: layout then position in BFC
            layout_float_element_at_encounter(lycon, elem);
            return;  // Floats don't participate in normal flow
        }
        
        // Normal layout dispatch
        if (is_block_level(display)) {
            layout_block(lycon, elem, display);
        } else {
            layout_inline(lycon, elem, display);
        }
    } else {
        // Text node: check float-aware line bounds first
        update_line_bounds_for_floats(lycon);
        layout_text(lycon, node);
    }
}
```

---

## 7. Migration Path

### 7.1 Backward Compatibility

The refactoring maintains backward compatibility by:
1. Keeping existing API signatures where possible
2. Deprecating `FloatContext` but keeping it functional during transition
3. Feature-flagging new BFC code initially

### 7.2 Testing Strategy

1. **Unit tests per CSS 2.2 rule**
   - Create minimal test cases for each of the 8 float rules
   - Add to `test/layout/data/position/` as reference tests

2. **Incremental test enablement**
   - Start with simple float tests (floats-002 already passes)
   - Progressively enable more complex tests

3. **Browser comparison**
   - Use Puppeteer reference data generation
   - 2px tolerance for floating-point variations

---

## 8. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| BFC inheritance complexity | High | Careful parent pointer management |
| Performance regression | Medium | Space query caching, early exit |
| Edge case proliferation | Medium | Comprehensive test suite |
| Coordinate system bugs | High | Add assertion checks, unit tests |

---

## 9. Success Criteria

| Milestone | Target Pass Rate |
|-----------|------------------|
| Phase 1 complete | 10% (22 tests) |
| Phase 2 complete | 40% (86 tests) |
| Phase 3 complete | 70% (151 tests) |
| Phase 4 complete | 90%+ (194+ tests) |

---

## 10. References

- [CSS 2.2 Section 9.5.1 - Float positioning](https://www.w3.org/TR/CSS2/visuren.html#floats)
- [CSS 2.2 Section 9.4 - Normal flow](https://www.w3.org/TR/CSS2/visuren.html#normal-flow)
- [CSS 2.2 Section 10.6.7 - Height of BFC roots](https://www.w3.org/TR/CSS2/visudet.html#root-height)
- [WPT float tests](https://github.com/nicholasRenworthy/csswg-test/tree/master/css-2/floats-clear)

---

## Appendix A: Detailed Test Failure Analysis

### A.1 Sample Failure: floats-020

**Test description:** Float in the middle of text should cause text to wrap around.

**Expected behavior:**
- "Filler Text" starts at normal position
- Encounters float, continues to the right of float
- Wraps below float when it ends

**Current behavior:**
- Float positioned at block layout time
- Text ignores float presence
- No line boundary adjustment

**Fix required:**
1. Float encounter during inline layout
2. `adjust_line_for_floats()` called for each line
3. Coordinate transformation from BFC to local block

### A.2 Sample Failure: floats-029

**Test description:** Empty clearing div should push subsequent content below floats.

**Expected behavior:**
- Float appears
- Empty `<div>` with `clear: both` creates break
- Following paragraph below float

**Current behavior:**
- Clear processed but advance_y not propagated to parent
- Paragraph overlaps with float area

**Fix required:**
1. Clear before child layout
2. Parent advance_y update
3. BFC height calculation includes cleared position

---

## Appendix B: File Change Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `radiant/layout_bfc.hpp` | New | BFC data structure and API |
| `radiant/layout_bfc.cpp` | New | BFC implementation |
| `radiant/layout_positioned.cpp` | Major refactor | Use BFC instead of FloatContext |
| `radiant/layout_positioned.hpp` | Deprecate | FloatContext → BFC |
| `radiant/layout_block.cpp` | Modify | BFC creation, clear handling |
| `radiant/layout_inline.cpp` | Modify | Float encounter, line adjustment |
| `radiant/layout_text.cpp` | Modify | Float-aware line bounds |
| `radiant/layout.hpp` | Modify | Add BFC to LayoutContext |
| `test/test_layout_bfc.cpp` | New | BFC unit tests |
