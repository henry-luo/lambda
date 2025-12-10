# Radiant Block Layout Improvements - Phase 2

## Analysis Summary

Based on running `make layout suite=box` (213 tests), the following improvements have been made:

### Current Status (After Fixes)
- **Passing tests**: 5 out of 213 (2.3%)
- **Tests with 90%+ element match**: 13+ tests
- **Tests with 80%+ element match**: 25+ tests
- **Highest element match**: 97.0% (box_012_overflow)

### Fixes Implemented
1. **CSS auto value handling**: Fixed `height: auto` and `width: auto` to return -1 instead of 0
2. **BFC height expansion**: Absolute positioned elements now properly expand height to contain floats
3. **JSON output for block-in-inline**: Fixed view tree output for blocks inside inline elements

---

## Issue Categories

### 1. BFC Height Calculation with Floats (High Priority)
**Affected tests:** `block-formatting-context-height-001/002/003`, many `block-in-inline-*`

**Problem:** When a BFC-establishing element (position:absolute, overflow:hidden, etc.) contains floats with margin-bottom, the container's auto height should include the float's margin. Currently showing height=0.

**Test case `block-formatting-context-height-001.htm`:**
```html
<div id="container" style="position: absolute; height: auto;">
    <div id="float" style="float: left; height: 50px; margin-bottom: 50px;"></div>
</div>
```
- Expected: container height = 100px (50px float + 50px margin)
- Actual: container height = 0px

**Root cause:** `layout_abs_block()` in `layout_positioned.cpp` doesn't include float bottom margins when calculating auto height for BFC roots.

**Fix location:** `layout_positioned.cpp` lines 290-330

---

### 2. Block-in-Inline Splitting (High Priority)
**Affected tests:** 100+ `block-in-inline-*` tests

**Problem:** CSS 2.1 Section 9.2.1.1 requires that when a block box is inside an inline box, the inline box must be "broken around" the block, creating anonymous block boxes.

**Test case `block-in-inline-003.htm`:**
```html
<div class="inline">  <!-- display: inline -->
    <div class="block">Content</div>
</div>
```

**Expected:** Three anonymous boxes:
1. Anonymous block containing inline content before block
2. The block element itself
3. Anonymous block containing inline content after block

**Current behavior:** Block element shows as `<undefined>` tag, no splitting occurs.

**Fix location:** `layout_inline.cpp` and `layout_flow_node()` in `layout.cpp`

---

### 3. Absolute Position Y-Coordinate (Medium Priority)
**Affected tests:** `block-formatting-context-*`, various height tests

**Problem:** Absolutely positioned elements appear at wrong Y position (y=16 instead of y=50 in browser).

**Root cause:** The container's Y position should be below the preceding `<p>` element. The issue may be in how `static position` is calculated for abs-positioned elements.

**Fix location:** `layout_positioned.cpp` - `calculate_absolute_position()`

---

### 4. Ex Unit Height Calculation (Medium Priority)
**Affected tests:** `height-083`, `height-084`

**Problem:** `height: 6ex` calculates to 60px instead of expected ~53.84px.

**Root cause:** The `ex` unit should use the x-height of the font (height of lowercase 'x'), not a fixed ratio.

**Fix location:** CSS value resolution in `resolve_css_style.cpp`

---

### 5. Inline-Block Overflow Width (Medium Priority)
**Affected tests:** `inline-block-width-002a/002b`

**Problem:** Inline-block with child that overflows doesn't cause proper wrapping.

**Test case:**
```html
<div style="width: 160px">
    x
    <div style="display: block">
        <div style="width: 320px">y</div>  <!-- overflows -->
    </div>
    z  <!-- should be on next line -->
</div>
```

**Current:** 'z' appears at x=168 (same line as overflow)
**Expected:** 'z' at x=8 (wrapped to next line)

**Fix location:** `layout_block.cpp` - inline-block width handling

---

### 6. Table Row View Generation (Lower Priority)
**Affected tests:** `blocks-017/018/019`

**Problem:** `<tr>` elements inside `<tbody>` don't generate views.

**Fix location:** `layout_table.cpp`

---

## Implementation Plan

### Phase 1: BFC Height with Floats (Highest Impact)

**File:** `radiant/layout_positioned.cpp`

**Changes:**
1. In `layout_abs_block()`, after calling `layout_block_inner_content()`:
   - Check if this block establishes a BFC (it does for position:absolute)
   - Find the maximum bottom of all floated descendants including their margins
   - Expand block height to contain floats if auto height

```cpp
// After layout_block_inner_content(lycon, block);
// Add BFC height expansion for floats
if (lycon->block.is_bfc_root || block_context_establishes_bfc(block)) {
    float max_float_bottom = 0;
    // Check floats in block context
    for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
        max_float_bottom = max(max_float_bottom, fb->margin_box_bottom);
    }
    for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
        max_float_bottom = max(max_float_bottom, fb->margin_box_bottom);
    }
    // If auto height, expand to contain floats
    if (!(lycon->block.given_height >= 0 || (block->position->has_top && block->position->has_bottom))) {
        if (max_float_bottom > block->height) {
            block->height = max_float_bottom;
        }
    }
}
```

### Phase 2: Block-in-Inline Detection

**File:** `radiant/layout.cpp` - `layout_flow_node()`

**Changes:**
1. When processing a block element whose parent has inline display:
   - Detect this case
   - Break the current line
   - Layout the block normally
   - Continue with remaining inline content after

```cpp
// In layout_flow_node, before switch(display.outer):
// Check for block-in-inline situation
ViewElement* parent_view = (ViewElement*)node->parent;
if (parent_view && parent_view->display.outer == CSS_VALUE_INLINE &&
    display.outer == CSS_VALUE_BLOCK) {
    // Block inside inline - need special handling
    log_debug("Block-in-inline detected: %s inside %s",
              node->node_name(), parent_view->node_name());
    // Break line and layout block
    if (!lycon->line.is_line_start) {
        line_break(lycon);
    }
    layout_block(lycon, node, display);
    // Line will be reset after block
    return;
}
```

### Phase 3: Static Position for Absolute Elements

**File:** `radiant/layout_positioned.cpp` - `calculate_absolute_position()`

**Changes:**
1. When calculating position for abs elements without explicit top/left:
   - Use the "static position" - where the element would have been in normal flow
   - This requires tracking the flow position before removing from flow

---

## Testing Strategy

After each fix, run:
```bash
make layout test=block-formatting-context-height-001  # Verify BFC fix
make layout test=block-in-inline-003                   # Verify block-in-inline
make layout suite=box                                   # Full regression
```

---

## Success Metrics

- Phase 1: `block-formatting-context-height-*` tests pass (3 tests)
- Phase 2: `block-in-inline-003` and similar basic tests pass
- Overall: Improve from 0% to >20% pass rate on box suite

---

## Files to Modify

1. `radiant/layout_positioned.cpp` - BFC height calculation
2. `radiant/layout.cpp` - Block-in-inline detection
3. `radiant/layout_inline.cpp` - Anonymous block box creation
4. `radiant/block_context.cpp` - Float margin tracking

---

## Implementation Order

1. ✅ Create plan document
2. ✅ Fix BFC height with floats in `layout_positioned.cpp`
   - Added BFC establishment for absolute elements
   - Added static position Y calculation using `pa_block->advance_y`
   - Added BFC height expansion loop for floats
3. ✅ Fix CSS auto height/width handling in `resolve_css_style.cpp`
   - Changed `height: auto` to return -1 instead of 0
   - Changed `width: auto` to return -1 instead of 0
   - This allows proper auto-sizing logic to work
4. ✅ Test BFC fix
   - `block-formatting-context-height-001`: Container now at (8, 50, 100×100) ✓
   - `block-formatting-context-height-002`: Container elements matching ✓
   - `block-formatting-context-height-003`: 71.4% elements matching ✓
5. ⬜ Fix block-in-inline basic detection
6. ⬜ Test block-in-inline
7. ⬜ Run full box suite regression

## Progress Summary

### Tests with 100% Element Match
- `block-non-replaced-width-007` - 100% elements, 50% text
- `inline-block-width-002b` - 100% elements, 66.7% text

### Tests with 80%+ Element Match
- 25 tests now have 80%+ element match rate

### Key Fixes Applied

1. **CSS auto value handling** (`resolve_css_style.cpp`):
   - `height: auto` now returns -1 (not 0) to indicate auto-sizing
   - `width: auto` now returns -1 (not 0) to indicate auto-sizing
   - This fixes the condition `!(given_height >= 0)` to correctly detect auto height

2. **BFC establishment for absolute elements** (`layout_positioned.cpp`):
   - Absolute positioned elements now properly establish BFC
   - Float tracking is initialized for absolute containers
   - Float margins are tracked via `margin_box_bottom`

3. **BFC height expansion** (`layout_positioned.cpp`):
   - After child layout, check all floats in BFC
   - Expand container height to include float margins if auto height

4. **JSON output for block-in-inline** (`view_pool.cpp`):
   - Fixed `print_inline_json` to call `print_block_json` for block children
   - Previously block children inside inline elements were output as `{"type": "block"}` only

## Test Results After Fixes

**Passing Tests (5/213 = 2.3%)**:
- `block-in-inline-003` ✅
- `block-in-inline-004` ✅
- `block-in-inline-005` ✅
- `block-in-inline-nested-001` ✅
- `block-in-inline-percents-001` ✅

**Tests with 100% Element Match**: 7 tests
**Tests with 90%+ Element Match**: 13 tests
**Tests with 80%+ Element Match**: 25+ tests

## Remaining Issues to Address

1. **Block-in-inline text handling**: Tests 001/002 have text node issues
2. **html/body height propagation**: Many tests fail because html/body heights differ from browser
3. **ex unit calculation**: Height tests with `6ex` calculate incorrectly
4. **Table row view generation**: Table tests missing row views
