# Nested Flex Layout in Radiant

This document describes how nested flex layout is handled in the Radiant layout engine, the challenges encountered, solutions implemented, and remaining work.

## Overview

Nested flex layout occurs when a flex container is itself a flex item within another flex container. This is common in modern CSS layouts:

```html
<div class="outer-flex">           <!-- Outer flex container (column) -->
  <div class="item">               <!-- Flex item -->
    <div class="inner-flex">       <!-- Nested flex container (row) -->
      <div class="inner-item">A</div>
      <div class="inner-item">B</div>
    </div>
  </div>
</div>
```

## The Core Challenge: Chicken-and-Egg Problem

Flex layout has a fundamental ordering problem:

1. **The flex algorithm needs container dimensions** to distribute space among items
2. **Auto-height containers need item sizes** to determine their own height
3. **Nested flex containers are both flex items AND flex containers** simultaneously

### Dual Role Problem

A nested flex container must fulfill two roles:

| Role | Requirements |
|------|--------------|
| **As Flex Item** | Needs flex-basis, flex-grow, flex-shrink; participates in parent's space distribution |
| **As Flex Container** | Needs to run its own flex algorithm for its children |

The layout must process both roles in the correct order.

## Layout Algorithm Flow

### Multi-Pass Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ layout_flex_content()                                           │
│   ├── init_flex_container()          // Initialize axis sizes   │
│   ├── collect_and_prepare_flex_items() // Measure items        │
│   ├── AUTO-HEIGHT CALCULATION        // Calculate from items   │
│   ├── layout_flex_container_with_nested_content()              │
│   │     ├── run_enhanced_flex_algorithm()                      │
│   │     │     └── layout_flex_container()  // Main algorithm   │
│   │     ├── layout_final_flex_content()    // Position content │
│   │     └── reposition_baseline_items()                        │
│   └── layout_flex_absolute_children()                          │
└─────────────────────────────────────────────────────────────────┘
```

### Key Entry Points

| Function | Location | Purpose |
|----------|----------|---------|
| `layout_flex_content` | `layout_flex_multipass.cpp` | Top-level entry for flex layout |
| `layout_flex_container_with_nested_content` | `layout_flex_multipass.cpp` | Handles nested containers with content |
| `layout_flex_container` | `layout_flex.cpp` | Core flex algorithm (7 phases) |
| `collect_and_prepare_flex_items` | `layout_flex.cpp` | Unified measurement and collection |

## Challenges Encountered and Solutions

### Challenge 1: Auto-Height Calculation Timing

**Problem:** When `init_flex_container` runs, flex items haven't been measured yet, so `cross_axis_size` (height for row flex) is 0.

**Symptoms:**
```
init_flex_container: main_axis_size=160.0, cross_axis_size=0.0 (content: 160x0)
```

Nested horizontal flex containers end up with `height=0` in the view tree.

**Solution:** Added auto-height calculation AFTER `collect_and_prepare_flex_items` but BEFORE `run_enhanced_flex_algorithm`:

```cpp
// In layout_flex_container_with_nested_content(), after collect_and_prepare_flex_items:

// Check if container has explicit height from CSS
bool has_explicit_height = flex_container->blk && flex_container->blk->given_height > 0;

if (flex_layout && is_main_axis_horizontal(flex_layout) && !has_explicit_height) {
    // Row flex with auto height: calculate height from flex items
    int max_item_height = 0;
    DomNode* child = flex_container->first_child;
    while (child) {
        if (child->is_element()) {
            ViewGroup* item = (ViewGroup*)child->as_element();
            if (item && item->height > 0 && item->height > max_item_height) {
                max_item_height = (int)item->height;
            }
        }
        child = child->next_sibling;
    }
    if (max_item_height > 0) {
        flex_layout->cross_axis_size = (float)max_item_height;
        flex_container->height = (float)max_item_height;
    }
}
```

**Location:** `radiant/layout_flex_multipass.cpp`, lines 160-240

---

### Challenge 2: Explicit Height Detection

**Problem:** The check `given_height >= 0` treated `height=0` as explicit, when it should be auto.

**Symptoms:** Phase 7 would skip height updates with message:
```
Phase 7: Container has explicit height, not updating
```

**Solution:** Changed the check from `>= 0` to `> 0`:

```cpp
// Before (incorrect):
bool has_explicit_height = container->blk && container->blk->given_height >= 0;

// After (correct):
bool has_explicit_height = container->blk && container->blk->given_height > 0;
```

**Location:** `radiant/layout_flex.cpp`, Phase 7 section (lines 382 and 414)

---

### Challenge 3: Block Finalization Overwriting Flex Height

**Problem:** After `layout_flex_content` correctly set the container height, `finalize_block_flow` was called and reset it:

```cpp
// In finalize_block_flow:
else {
    block->height = flow_height;  // flow_height is 0 for flex containers!
}
```

**Symptoms:** Container height correctly calculated as 20px, but view tree shows 0px.

**Solution:** Added early return after flex layout to skip `finalize_block_flow`:

```cpp
else if (block->display.inner == CSS_VALUE_FLEX) {
    log_debug("Setting up flex container for %s", block->node_name());
    layout_flex_content(lycon, block);
    log_debug("Finished flex container layout for %s", block->node_name());
    // Flex layout handles its own height - skip finalize_block_flow
    lycon->parent = block->parent_view();
    return;  // <-- Early return added
}
```

**Location:** `radiant/layout_block.cpp`, lines 162-169

---

### Challenge 4: Height Inheritance vs Auto-Height

**Problem:** Nested flex items inherit dimensions from parent's cross-axis. A child with `height: auto` should shrink-wrap, but was getting parent's height.

**Example:**
- Parent column flex: height 254px
- Child horizontal flex: inherits 254px as cross_axis_size
- But child has `height: auto`, should calculate from content

**Solution:** Check `has_explicit_height` flag derived from CSS `given_height`:

```cpp
// given_height > 0 means explicit CSS height
// given_height <= 0 (typically -1) means auto
bool has_explicit_height = flex_container->blk && flex_container->blk->given_height > 0;
```

---

## Axis Terminology Reference

| Flex Direction | Main Axis | Cross Axis | `main_axis_size` | `cross_axis_size` |
|----------------|-----------|------------|------------------|-------------------|
| `row` | Horizontal | Vertical | width | height |
| `column` | Vertical | Horizontal | height | width |

For auto-height calculation:
- **Row flex:** Need to calculate `cross_axis_size` (height) from items
- **Column flex:** Need to calculate `main_axis_size` (height) from items

---

## Current Test Results

| Test Suite | Pass Rate | Notes |
|------------|-----------|-------|
| Baseline | 123/123 (100%) | No regression |
| Flex | 163/255 (64%) | Minor regression from changes |
| Flex-Nest | 1/12 (8%) | Height now correct, other issues remain |

The horizontal flex height is now correctly calculated (e.g., 20px instead of 0px), but mismatches with browser (26px) remain due to content measurement issues.

---

## What Still Needs To Be Done

### 1. Content Height Measurement Accuracy

**Problem:** Flex item heights are measured as 20px but browser shows 26px.

**Analysis:**
- Browser: 11px font + 12px padding + 2px border = ~26px
- Radiant: Returning 20px (likely missing border or using wrong line-height)

**Location to investigate:** `radiant/layout_flex_measurement.cpp`, specifically:
- `measure_flex_child_content()`
- `measure_text_content()`

### 2. Line Height in Flex Items

**Problem:** Text baseline and line height calculations may not match browser behavior for flex item content.

### 3. Border Box Sizing

**Problem:** When `box-sizing: border-box` is set, the flex item dimensions should include padding and border, but content measurement may not account for this correctly.

### 4. Gap Handling in Auto-Height

**Problem:** When calculating auto-height for column flex, gaps between items need to be added:

```cpp
// Add gap spacing
if (item_count > 1 && flex_layout->row_gap > 0) {
    total_height += (int)(flex_layout->row_gap * (item_count - 1));
}
```

This is implemented but may need verification for edge cases.

### 5. Multi-Line Flex (flex-wrap)

**Problem:** Auto-height calculation currently assumes single-line flex. For wrapped flex containers, the calculation needs to sum line heights.

### 6. Percentage Heights in Nested Flex

**Problem:** When a nested flex item has `height: 50%`, it should resolve relative to the parent flex container's cross-axis size. This requires careful ordering of percentage resolution.

---

## File Reference

| File | Purpose |
|------|---------|
| `radiant/layout_flex.cpp` | Core flex algorithm, axis initialization, Phase 7 finalization |
| `radiant/layout_flex_multipass.cpp` | Multi-pass layout, auto-height calculation, nested handling |
| `radiant/layout_flex_measurement.cpp` | Content measurement for flex items |
| `radiant/layout_block.cpp` | Block layout entry, flex dispatch |
| `radiant/view.hpp` | View structures, `given_height`, `FlexItemProp` |

---

## Debugging Tips

### Enable Logging

Check `log.conf` for debug level. Relevant log patterns:

```bash
# Auto-height calculation
grep "AUTO-HEIGHT" log.txt

# Phase 7 height updates
grep "Phase 7" log.txt

# Flex container initialization
grep "init_flex_container" log.txt
```

### View Tree Inspection

```bash
# Check container heights
grep "view-block:div" view_tree.txt | head -20

# Find zero-height containers
grep "hg:0.0" view_tree.txt
```

### Test Commands

```bash
# Run specific test
make layout suite=flex-nest cases=flex_014

# Verbose output
node test/layout/test_radiant_layout.js -c flex-nest -t flex_014_nested_flex.html -v
```
