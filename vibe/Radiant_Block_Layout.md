# Radiant Block Layout - Improvement Proposal

## Executive Summary

This document analyzes the current state of Radiant's block layout implementation based on:
1. The Radiant Layout Design document
2. Code analysis of `layout_block.cpp` and related files
3. Test results from `make layout suite=box` (0/252 tests passing)

The analysis identifies key issues preventing test success and proposes targeted improvements.

**Key Refactoring:** Unify `Blockbox`, `FloatContext`, and `BlockFormattingContext` into a single `BlockContext` structure.

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

### 1.2 Current Data Structures (To Be Unified)

| Structure                | Purpose                        | Issues Identified                  |
| ------------------------ | ------------------------------ | ---------------------------------- |
| `Blockbox`               | Block formatting context state | Margin collapsing needs refinement |
| `Linebox`                | Inline formatting state        | Float integration incomplete       |
| `BlockFormattingContext` | BFC for floats (new system)    | Parallel with legacy FloatContext  |
| `FloatContext`           | Legacy float positioning       | Needs full replacement             |

### 1.3 Proposed Unified Structure: `BlockContext`

The three block-related structures will be unified into a single `BlockContext`:

```cpp
/**
 * BlockContext - Unified Block Formatting Context
 *
 * Combines the functionality of:
 * - Blockbox (layout state)
 * - FloatContext (legacy float management)
 * - BlockFormattingContext (new BFC system)
 *
 * Per CSS 2.2 Section 9.4.1, a BFC is established by:
 * - Root element
 * - Floats (float != none)
 * - Absolutely positioned elements
 * - Inline-blocks
 * - Table cells/captions
 * - Overflow != visible
 * - display: flow-root
 * - Flex/Grid items
 */
typedef struct BlockContext {
    // =========================================================================
    // Layout State (from Blockbox)
    // =========================================================================
    float content_width;        // Computed content width for inner content
    float content_height;       // Computed content height for inner content
    float advance_y;            // Current vertical position (includes padding.top + border.top)
    float max_width;            // Maximum content width encountered
    float max_height;           // Maximum content height encountered
    float line_height;          // Current line height
    float init_ascender;        // Initial ascender at line start
    float init_descender;       // Initial descender at line start
    float lead_y;               // Leading space when line_height > font size
    CssEnum text_align;         // Text alignment
    float given_width;          // CSS specified width (-1 if auto)
    float given_height;         // CSS specified height (-1 if auto)

    // =========================================================================
    // BFC Hierarchy
    // =========================================================================
    struct BlockContext* parent;           // Parent block context
    ViewBlock* establishing_element;       // Element that established this BFC (if any)
    bool is_bfc_root;                      // True if this context establishes a new BFC

    // =========================================================================
    // Float Management (unified from FloatContext + BlockFormattingContext)
    // =========================================================================
    FloatBox* left_floats;      // Linked list of left floats
    FloatBox* right_floats;     // Linked list of right floats
    int left_float_count;
    int right_float_count;
    float lowest_float_bottom;  // Optimization: track lowest float edge

    // Content area bounds (for float calculations)
    float float_left_edge;      // Left edge of content area
    float float_right_edge;     // Right edge of content area

    // =========================================================================
    // Memory
    // =========================================================================
    Pool* pool;                 // Memory pool for float allocations
} BlockContext;
```

**Benefits of unification:**
1. Single source of truth for block state
2. No parallel tracking of floats
3. BFC hierarchy naturally represented via `parent` pointer
4. Simpler API: `lycon->block` handles everything

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

### 3.1 Priority 0: Unify Block Context (Foundation)

**Estimated Impact:** Enables all other improvements + cleaner codebase

#### 3.1.1 New Unified `BlockContext` Structure

Replace `Blockbox`, `FloatContext`, and `BlockFormattingContext` with unified `BlockContext`.

**File changes:**

1. **`layout.hpp`** - Replace `Blockbox` definition:
```cpp
// OLD:
typedef struct Blockbox { ... } Blockbox;
struct FloatContext;
struct BlockFormattingContext;

// NEW:
typedef struct BlockContext { ... } BlockContext;
```

2. **`LayoutContext`** - Simplify members:
```cpp
// OLD:
struct LayoutContext {
    Blockbox block;
    struct FloatContext* current_float_context;
    struct BlockFormattingContext* bfc;
    bool owns_bfc;
    ...
};

// NEW:
struct LayoutContext {
    BlockContext block;  // Unified block context
    ...
};
```

3. **Delete files:**
   - `layout_bfc.hpp` → Merged into `layout.hpp`
   - `layout_bfc.cpp` → Merged into `layout_block.cpp` or new `block_context.cpp`
   - Float functions in `layout_positioned.hpp/cpp` → Merged

#### 3.1.2 BlockContext API

```cpp
// Lifecycle
void block_context_init(BlockContext* ctx, ViewBlock* element, Pool* pool);
void block_context_push(LayoutContext* lycon);  // Save current, create child
void block_context_pop(LayoutContext* lycon);   // Restore parent

// BFC Detection
bool block_context_is_bfc_root(BlockContext* ctx);
bool block_context_establishes_bfc(ViewBlock* block);

// Float Management
void block_context_add_float(BlockContext* ctx, ViewBlock* float_elem);
void block_context_position_float(BlockContext* ctx, ViewBlock* float_elem, float current_y);
FloatAvailableSpace block_context_space_at_y(BlockContext* ctx, float y, float height);
float block_context_clear_y(BlockContext* ctx, CssEnum clear_type);

// Layout State
void block_context_advance_y(BlockContext* ctx, float delta);
void block_context_update_max_width(BlockContext* ctx, float width);
```

#### 3.1.3 Migration Steps

1. **Create `BlockContext` in `layout.hpp`** with all fields
2. **Update `LayoutContext`** to use `BlockContext block`
3. **Migrate float functions** from `layout_positioned.cpp` and `layout_bfc.cpp`
4. **Search & replace** across all files:
   - `lycon->block.` → (already correct, just type changes)
   - `lycon->current_float_context` → `&lycon->block`
   - `lycon->bfc` → `&lycon->block`
5. **Remove** deprecated files/structures
6. **Test** incrementally

### 3.2 Priority 1: Implement Block-in-Inline (Anonymous Block Boxes)

**Estimated Impact:** 90+ tests

Per CSS 2.2 Section 9.2.1.1, when a block-level element is inside an inline element, the inline box must be broken around the block, with the inline portions enclosed in anonymous block boxes.

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
- `view.hpp` - Add anonymous block view type (`RDT_VIEW_ANONYMOUS_BLOCK`)

### 3.3 Priority 2: Fix BFC Establishment Detection

**Estimated Impact:** 15+ tests

With unified `BlockContext`, BFC detection becomes simpler:

```cpp
bool block_context_establishes_bfc(ViewBlock* block) {
    // CSS 2.2 Section 9.4.1 - Block formatting contexts

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

    // 8. Flex/Grid containers
    if (block->display.inner == CSS_VALUE_FLEX ||
        block->display.inner == CSS_VALUE_GRID) return true;

    return false;
}
```

**Integration with BlockContext:**
```cpp
void layout_block_content(LayoutContext* lycon, ViewBlock* block, ...) {
    // Check if this block establishes a new BFC
    if (block_context_establishes_bfc(block)) {
        lycon->block.is_bfc_root = true;
        lycon->block.establishing_element = block;
        // Reset float lists for new BFC
        lycon->block.left_floats = NULL;
        lycon->block.right_floats = NULL;
    }
    // ... rest of layout
}
```

### 3.4 Priority 3: Unified Float Management

**Estimated Impact:** 10+ tests

With `BlockContext`, float management is simplified - no more dual tracking:

```cpp
// Single entry point for float positioning
void block_context_position_float(BlockContext* ctx, ViewBlock* element, float current_y) {
    if (!element || !element->position) return;

    CssEnum side = element->position->float_prop;
    if (side != CSS_VALUE_LEFT && side != CSS_VALUE_RIGHT) return;

    // Get element dimensions including margins
    float margin_l = element->bound ? element->bound->margin.left : 0;
    float margin_r = element->bound ? element->bound->margin.right : 0;
    float margin_t = element->bound ? element->bound->margin.top : 0;
    float total_width = element->width + margin_l + margin_r;

    // CSS 2.2 Rules: Float top >= current line Y
    float min_y = fmax(0, current_y);

    // Find Y where float fits horizontally
    float y = block_context_find_y_for_width(ctx, total_width, min_y);

    // Get available space at that Y
    FloatAvailableSpace space = block_context_space_at_y(ctx, y, element->height);

    // Position the float
    if (side == CSS_VALUE_LEFT) {
        element->x = space.left + margin_l;
    } else {
        element->x = space.right - element->width - margin_r;
    }
    element->y = y + margin_t;

    // Add to float list
    block_context_add_float(ctx, element);
}

// Space query - used by inline layout to adjust line bounds
FloatAvailableSpace block_context_space_at_y(BlockContext* ctx, float y, float height) {
    FloatAvailableSpace space = {
        .left = ctx->float_left_edge,
        .right = ctx->float_right_edge,
        .has_left_float = false,
        .has_right_float = false
    };

    // Check left floats
    for (FloatBox* fb = ctx->left_floats; fb; fb = fb->next) {
        if (float_intersects_y_range(fb, y, y + height)) {
            if (fb->margin_box_right > space.left) {
                space.left = fb->margin_box_right;
                space.has_left_float = true;
            }
        }
    }

    // Check right floats
    for (FloatBox* fb = ctx->right_floats; fb; fb = fb->next) {
        if (float_intersects_y_range(fb, y, y + height)) {
            if (fb->margin_box_left < space.right) {
                space.right = fb->margin_box_left;
                space.has_right_float = true;
            }
        }
    }

    return space;
}
```

**Remove deprecated code:**
- `layout_float_element()` → `block_context_position_float()`
- `float_space_at_y()` → `block_context_space_at_y()`
- `prescan_and_layout_floats()` → Simplify with accurate measurement

### 3.5 Priority 4: Inline-Block Baseline Calculation

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

### 3.6 Priority 5: Margin Collapsing Edge Cases

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

### Phase 0: BlockContext Unification (Week 1)
- [ ] Define `BlockContext` struct in `layout.hpp`
- [ ] Update `LayoutContext` to use `BlockContext block`
- [ ] Migrate float functions into `BlockContext` methods
- [ ] Remove `FloatContext` and `BlockFormattingContext`
- [ ] Update all references: `lycon->bfc`, `lycon->current_float_context`
- [ ] Delete `layout_bfc.hpp`, `layout_bfc.cpp`
- [ ] Clean up float code in `layout_positioned.hpp/cpp`

### Phase 1: BFC & Float Fixes (Week 2)
- [ ] Implement `block_context_establishes_bfc()`
- [ ] Fix float pre-scanning with accurate measurement
- [ ] BFC height expansion to contain floats

### Phase 2: Block-in-Inline (Week 3-4)
- [ ] Design anonymous block data structures
- [ ] Implement inline splitting algorithm
- [ ] Add `RDT_VIEW_ANONYMOUS_BLOCK` view type
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
| Phase 0 complete (BlockContext) | 10/252 (4%) | Week 1 |
| Phase 1 complete (BFC/Floats) | 40/252 (16%) | Week 2 |
| Phase 2 complete (Block-in-Inline) | 130/252 (52%) | Week 4 |
| Phase 3 complete (Inline-Block) | 190/252 (75%) | Week 5 |
| Phase 4 complete (Refinements) | 220/252 (87%) | Week 6 |

---

## 6. Code Quality Notes

### 6.1 Files Changed in Refactoring

| File | Changes |
|------|---------|
| `layout.hpp` | Replace `Blockbox` with `BlockContext`, remove forward decls |
| `layout_block.cpp` | Update all `lycon->block` usage, remove BFC/FloatContext code |
| `layout_inline.cpp` | Add block-in-inline detection, use `BlockContext` for floats |
| `layout_text.cpp` | Use `BlockContext` for line adjustment |
| `layout_positioned.hpp/cpp` | Remove `FloatContext` definition, keep positioning functions |
| `layout_bfc.hpp/cpp` | **DELETE** - merged into `BlockContext` |
| `layout_table.cpp` | Update `Blockbox` → `BlockContext` references |
| `layout_flex_*.cpp` | Update `Blockbox` → `BlockContext` references |
| `layout_grid_*.cpp` | Update `Blockbox` → `BlockContext` references |
| `resolve_css_style.cpp` | Update `lycon->block.pa_block` → `lycon->block.parent` |

### 6.2 Current Code Observations

**Strengths:**
- Clear separation between layout phases
- Good logging infrastructure
- Memory pool usage for allocations

**Areas for improvement:**
- ~~Duplicate float handling code (legacy + BFC)~~ → Fixed by unification
- Magic numbers in float pre-scanning
- Complex conditional chains for BFC detection

### 6.3 Recommended Naming Convention

```cpp
// BlockContext methods use block_context_ prefix
void block_context_init(...);
void block_context_add_float(...);
FloatAvailableSpace block_context_space_at_y(...);

// Or as C++ methods if preferred
struct BlockContext {
    void init(...);
    void add_float(...);
    FloatAvailableSpace space_at_y(...);
};
```

---

## 7. References

- [CSS 2.2 Section 9.2.1.1](https://www.w3.org/TR/CSS22/visuren.html#anonymous-block-level) - Anonymous block boxes
- [CSS 2.2 Section 9.4.1](https://www.w3.org/TR/CSS22/visuren.html#block-formatting) - Block formatting contexts
- [CSS 2.2 Section 9.5.1](https://www.w3.org/TR/CSS22/visuren.html#float-position) - Float positioning
- [CSS 2.2 Section 10.6.7](https://www.w3.org/TR/CSS22/visudet.html#root-height) - Auto heights for BFC
- [CSS 2.2 Section 10.8.1](https://www.w3.org/TR/CSS22/visudet.html#line-height) - Inline-block baseline

---

## Appendix A: Before/After Structure Comparison

### Before (Current)
```
LayoutContext
├── Blockbox block           // Layout state
├── FloatContext* current_float_context  // Legacy float system
├── BlockFormattingContext* bfc          // New float system (parallel!)
├── bool owns_bfc
└── Linebox line
```

### After (Proposed)
```
LayoutContext
├── BlockContext block       // Unified: layout state + floats + BFC
│   ├── content_width, advance_y, ...  // Layout state
│   ├── left_floats, right_floats      // Float lists
│   ├── is_bfc_root, establishing_element  // BFC info
│   └── parent                          // BFC hierarchy
└── Linebox line
```

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
