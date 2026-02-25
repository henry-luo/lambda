# Radiant Float Layout Enhancement Proposal

## Executive Summary

This document proposes structural enhancements to Radiant's float layout implementation based on analysis of Ladybird's superior architecture. The goal is to make float layout more robust, efficient, and compliant with CSS specifications.

## Current State Analysis

### Test Results (make layout suite=position)

**Current Statistics:**
- Total Tests: 224
- Successful: 6 (2.7%)
- Failed: 218 (97.3%)

**Passing Tests:**
- `position_007_absolute_basic` - 100% elements, 100% text
- `position_010_relative_basic` - 100% elements, 100% text

**Representative Failures:**
- `float-001` through `float-006` - ~60-80% elements
- `float-applies-to-*` series - 14-57% elements
- `position_001_float_left` - 80% elements, 14% text
- `position_002_float_right` - 60% elements, 0% text
- `float-non-replaced-width-*` series - 20-80% elements

### Current Architecture Issues

1. **Single Float Storage**: Only stores ONE float per side
   ```cpp
   if (ctx->left_count == 0) {
       ctx->left_floats = float_box;
   } else {
       printf("DEBUG: WARNING - Multiple left floats not fully supported yet\n");
       free(float_box);  // LEAKS FUNCTIONALITY
   }
   ```

2. **No Vertical Extent Tracking**: FloatBox stores position but Y-based queries are inefficient
   ```cpp
   typedef struct FloatBox {
       ViewBlock* element;
       int x, y, width, height;  // bounds stored but no extent-based lookup
       CssEnum float_side;
   } FloatBox;
   ```

3. **Hardcoded Adjustments**: Line adjustment has container-specific hacks
   ```cpp
   if (container && container->width == 376 && container->height >= 276) {
       // This looks like our float test container (400px - 24px = 376px)
       // HARDCODED adjustment for specific test!
   }
   ```

4. **No Proper Float List Management**: Uses single pointers instead of dynamic arrays

5. **Missing Space Query Function**: No efficient `space_at_y()` API

---

## Proposed Architecture

### 1. Enhanced FloatBox Structure

```cpp
typedef struct FloatBox {
    ViewBlock* element;         // The floating element

    // Margin box bounds (for positioning calculations)
    float margin_left;          // element->x - margin
    float margin_top;           // element->y - margin
    float margin_right;         // element->x + width + margin
    float margin_bottom;        // element->y + height + margin

    // Content box bounds (for rendering)
    float x, y, width, height;

    // Side identifier
    CssEnum float_side;         // CSS_VALUE_LEFT or CSS_VALUE_RIGHT

    // Linked list for multiple floats
    struct FloatBox* next;
} FloatBox;
```

### 2. Separate Left/Right Float Lists with Efficient Queries

```cpp
typedef struct FloatSideData {
    FloatBox* head;             // Head of linked list of floats
    FloatBox* tail;             // Tail for O(1) append
    int count;                  // Number of floats on this side
    float current_intrusion;    // Current horizontal intrusion at current_y
} FloatSideData;

typedef struct FloatContext {
    FloatSideData left;         // Left floats
    FloatSideData right;        // Right floats

    float current_y;            // Current vertical position in layout
    float container_left;       // Left edge of containing block content area
    float container_right;      // Right edge of containing block content area
    float container_top;        // Top of containing block content area

    ViewBlock* container;       // Containing block establishing this context

    // BFC flag - floats contained within this context
    bool establishes_bfc;       // Block Formatting Context
} FloatContext;
```

### 3. Space Query API

```cpp
/**
 * Query available horizontal space at a given Y coordinate
 *
 * @param ctx Float context to query
 * @param y Y coordinate to check (in container coordinates)
 * @param line_height Height of the line being placed
 * @return Structure with available left/right boundaries
 */
typedef struct {
    float left;                 // Left edge of available space
    float right;                // Right edge of available space
    float available_width;      // right - left
    bool has_left_float;        // True if left float affects this Y
    bool has_right_float;       // True if right float affects this Y
} AvailableSpace;

AvailableSpace float_space_at_y(FloatContext* ctx, float y, float line_height);

/**
 * Find Y position where specified width is available
 * Used for placing new floats that don't fit at current Y
 */
float float_find_y_for_width(FloatContext* ctx, float required_width, float start_y);

/**
 * Find clear position - Y coordinate below all floats of specified type
 */
float float_find_clear_position(FloatContext* ctx, CssEnum clear_value);
```

### 4. Float Context Lifecycle

```cpp
// Create context when entering a block that establishes BFC
FloatContext* float_context_create(ViewBlock* container);

// Add a float to the context
void float_context_add_float(FloatContext* ctx, ViewBlock* element);

// Position a float element properly
void float_context_position_float(FloatContext* ctx, ViewBlock* element, float current_y);

// Query space for line layout
AvailableSpace float_context_space_at_y(FloatContext* ctx, float y, float line_height);

// Find position below floats (for clear property)
float float_context_clear_position(FloatContext* ctx, CssEnum clear);

// Destroy context when leaving the block
void float_context_destroy(FloatContext* ctx);
```

---

## Implementation Plan

### Phase 1: Data Structure Enhancement

**File: `radiant/layout_positioned.hpp`**

Replace current structures with enhanced versions:

```cpp
// Enhanced FloatBox with margin box tracking
typedef struct FloatBox {
    ViewBlock* element;

    // Margin box bounds (outer bounds including margins)
    float margin_box_top;
    float margin_box_bottom;
    float margin_box_left;
    float margin_box_right;

    // Border box bounds (element position and size)
    float x, y, width, height;

    CssEnum float_side;
    struct FloatBox* next;      // Linked list
} FloatBox;

// Separate lists for left and right floats
typedef struct FloatSideData {
    FloatBox* head;
    FloatBox* tail;
    int count;
} FloatSideData;

// Enhanced float context
typedef struct FloatContext {
    FloatSideData left;
    FloatSideData right;

    // Container content area bounds
    float content_left;
    float content_right;
    float content_top;
    float content_bottom;

    ViewBlock* container;
} FloatContext;

// Available space query result
typedef struct FloatAvailableSpace {
    float left;
    float right;
} FloatAvailableSpace;
```

### Phase 2: Core Query Functions

**File: `radiant/layout_positioned.cpp`**

```cpp
/**
 * Check if a float intersects with a vertical range
 */
static bool float_intersects_y_range(FloatBox* box, float y_top, float y_bottom) {
    return !(box->margin_box_bottom <= y_top || box->margin_box_top >= y_bottom);
}

/**
 * Get available space at a given Y coordinate
 */
FloatAvailableSpace float_space_at_y(FloatContext* ctx, float y, float line_height) {
    FloatAvailableSpace space;
    space.left = ctx->content_left;
    space.right = ctx->content_right;

    float y_top = y;
    float y_bottom = y + line_height;

    // Check left floats
    for (FloatBox* box = ctx->left.head; box; box = box->next) {
        if (float_intersects_y_range(box, y_top, y_bottom)) {
            // Left float: intrudes from left, update left boundary
            float float_right = box->margin_box_right;
            if (float_right > space.left) {
                space.left = float_right;
            }
        }
    }

    // Check right floats
    for (FloatBox* box = ctx->right.head; box; box = box->next) {
        if (float_intersects_y_range(box, y_top, y_bottom)) {
            // Right float: intrudes from right, update right boundary
            float float_left = box->margin_box_left;
            if (float_left < space.right) {
                space.right = float_left;
            }
        }
    }

    return space;
}

/**
 * Find Y position where specified width is available
 */
float float_find_y_for_width(FloatContext* ctx, float required_width, float start_y) {
    float y = start_y;
    float max_iterations = 1000;  // Safety limit

    while (max_iterations-- > 0) {
        FloatAvailableSpace space = float_space_at_y(ctx, y, 1.0f);
        float available = space.right - space.left;

        if (available >= required_width) {
            return y;
        }

        // Find next Y position where a float ends
        float next_y = INFINITY;

        for (FloatBox* box = ctx->left.head; box; box = box->next) {
            if (box->margin_box_bottom > y && box->margin_box_bottom < next_y) {
                next_y = box->margin_box_bottom;
            }
        }
        for (FloatBox* box = ctx->right.head; box; box = box->next) {
            if (box->margin_box_bottom > y && box->margin_box_bottom < next_y) {
                next_y = box->margin_box_bottom;
            }
        }

        if (next_y == INFINITY) break;
        y = next_y;
    }

    return y;
}

/**
 * Find clear position for clear property
 */
float float_find_clear_position(FloatContext* ctx, CssEnum clear_value) {
    float clear_y = ctx->content_top;

    if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_BOTH) {
        for (FloatBox* box = ctx->left.head; box; box = box->next) {
            if (box->margin_box_bottom > clear_y) {
                clear_y = box->margin_box_bottom;
            }
        }
    }

    if (clear_value == CSS_VALUE_RIGHT || clear_value == CSS_VALUE_BOTH) {
        for (FloatBox* box = ctx->right.head; box; box = box->next) {
            if (box->margin_box_bottom > clear_y) {
                clear_y = box->margin_box_bottom;
            }
        }
    }

    return clear_y;
}
```

### Phase 3: Float Positioning Algorithm

```cpp
/**
 * Position a float element within the context
 * CSS 2.2 Section 9.5.1 - Float Positioning Rules
 */
void float_context_position_float(FloatContext* ctx, ViewBlock* element, float current_y) {
    CssEnum float_side = element->position->float_prop;

    // Get element's margin box dimensions
    float margin_left = element->bound ? element->bound->margin.left : 0;
    float margin_right = element->bound ? element->bound->margin.right : 0;
    float margin_top = element->bound ? element->bound->margin.top : 0;
    float margin_bottom = element->bound ? element->bound->margin.bottom : 0;

    float total_width = element->width + margin_left + margin_right;
    float total_height = element->height + margin_top + margin_bottom;

    // Rule 1: Float's top may not be higher than top of any earlier float
    float min_y = current_y;

    // Rule 2: Find Y position where the float fits
    float y = float_find_y_for_width(ctx, total_width, min_y);

    // Rule 3: Position horizontally based on float side
    FloatAvailableSpace space = float_space_at_y(ctx, y, total_height);

    float x;
    if (float_side == CSS_VALUE_LEFT) {
        x = space.left + margin_left;
    } else {  // CSS_VALUE_RIGHT
        x = space.right - element->width - margin_right;
    }

    // Set element position
    element->x = x;
    element->y = y + margin_top;

    // Add to float context
    float_context_add_float(ctx, element);
}

/**
 * Add a positioned float to the context
 */
void float_context_add_float(FloatContext* ctx, ViewBlock* element) {
    FloatBox* box = (FloatBox*)calloc(1, sizeof(FloatBox));
    box->element = element;
    box->x = element->x;
    box->y = element->y;
    box->width = element->width;
    box->height = element->height;
    box->float_side = element->position->float_prop;

    // Calculate margin box bounds
    float margin_left = element->bound ? element->bound->margin.left : 0;
    float margin_right = element->bound ? element->bound->margin.right : 0;
    float margin_top = element->bound ? element->bound->margin.top : 0;
    float margin_bottom = element->bound ? element->bound->margin.bottom : 0;

    box->margin_box_left = element->x - margin_left;
    box->margin_box_top = element->y - margin_top;
    box->margin_box_right = element->x + element->width + margin_right;
    box->margin_box_bottom = element->y + element->height + margin_bottom;

    // Add to appropriate list
    FloatSideData* side = (box->float_side == CSS_VALUE_LEFT) ? &ctx->left : &ctx->right;

    if (!side->head) {
        side->head = side->tail = box;
    } else {
        side->tail->next = box;
        side->tail = box;
    }
    side->count++;
}
```

### Phase 4: Line Layout Integration

**File: `radiant/layout_text.cpp`**

```cpp
void line_reset(LayoutContext* lycon) {
    // ... existing reset code ...

    // Query float context for line boundaries
    FloatContext* float_ctx = get_current_float_context(lycon);
    if (float_ctx) {
        FloatAvailableSpace space = float_space_at_y(
            float_ctx,
            lycon->block.advance_y,
            lycon->block.line_height
        );
        lycon->line.left = space.left;
        lycon->line.right = space.right;
    }
}
```

### Phase 5: Clear Property Implementation

```cpp
void layout_clear_element(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position || block->position->clear == CSS_VALUE_NONE) return;

    FloatContext* float_ctx = get_current_float_context(lycon);
    if (!float_ctx) return;

    float clear_y = float_find_clear_position(float_ctx, block->position->clear);

    // Move block below the cleared floats
    if (clear_y > lycon->block.advance_y) {
        float delta = clear_y - lycon->block.advance_y;
        lycon->block.advance_y = clear_y;
        block->y += delta;
    }
}
```

---

## CSS 2.2 Float Rules Reference

From CSS 2.2 Section 9.5.1, the float positioning rules are:

1. **Left-to-Right/Right-to-Left**: The outer left edge of a left-float may not be to the left of the left edge of its containing block. Analogous for right floats.

2. **Float Stacking**: If the current box is left-floating, and there are any left-floating boxes generated by earlier elements, the outer left edge of the current box must be to the right of the outer right edge of the earlier box, OR its outer top must be lower than the outer bottom of the earlier box.

3. **No Overlap**: The outer right edge of a left-float may not be to the right of the outer left edge of any right-floating box that is to its right. Analogous rule for right floats.

4. **Top Constraint**: A floating box's outer top may not be higher than the outer top of any block or floated box generated by earlier elements.

5. **Line Constraint**: A floating box's outer top may not be higher than the top of any line-box containing a box generated by earlier content.

6. **Height Constraint**: A floating box must be placed as high as possible.

7. **Width Constraint**: A left-floating box that has another left-floating box to its left must have its right outer edge to the right of its containing block's right edge (i.e., it must be pushed down). Analogous for right floats.

8. **As Far As Possible**: A floating box must be placed as far to the left (or right, for right floats) as possible.

---

## Testing Strategy

### Unit Tests for Float Context

```cpp
// Test: Single left float
TEST(FloatContext, SingleLeftFloat) {
    ViewBlock container;
    container.x = 0; container.y = 0;
    container.width = 400; container.height = 300;

    FloatContext* ctx = float_context_create(&container);

    ViewBlock float_elem;
    float_elem.width = 100; float_elem.height = 50;
    float_elem.position = alloc_position_prop();
    float_elem.position->float_prop = CSS_VALUE_LEFT;

    float_context_position_float(ctx, &float_elem, 0);

    EXPECT_EQ(float_elem.x, 0);
    EXPECT_EQ(float_elem.y, 0);

    // Query space at different Y positions
    FloatAvailableSpace space_at_0 = float_space_at_y(ctx, 0, 20);
    EXPECT_EQ(space_at_0.left, 100);  // After the float
    EXPECT_EQ(space_at_0.right, 400);

    FloatAvailableSpace space_at_60 = float_space_at_y(ctx, 60, 20);
    EXPECT_EQ(space_at_60.left, 0);   // Below the float
    EXPECT_EQ(space_at_60.right, 400);

    float_context_destroy(ctx);
}

// Test: Multiple floats stacking
TEST(FloatContext, MultipleLeftFloats) {
    // ... test that multiple left floats stack correctly
}

// Test: Left and right floats together
TEST(FloatContext, LeftAndRightFloats) {
    // ... test that text flows between left and right floats
}

// Test: Clear property
TEST(FloatContext, ClearLeft) {
    // ... test that clear:left moves element below left floats
}
```

### Integration Tests

Run the existing test suite to verify no regressions:

```bash
# Must pass all baseline tests
make layout suite=baseline

# Track improvement in position tests
make layout suite=position
```

---

## Migration Steps

1. **Backup**: Save current `layout_positioned.cpp` and `layout_positioned.hpp`

2. **Update Headers**: Add new structures to `layout_positioned.hpp`

3. **Implement Core Functions**: Add the new float context functions

4. **Update Layout Integration**: Modify `line_reset()` and related functions

5. **Remove Hardcoded Hacks**: Remove the container-size-specific adjustments

6. **Run Tests**: Verify baseline passes, check position improvements

7. **Iterate**: Fix any regressions, improve based on test feedback

---

## Expected Outcomes

### Immediate Improvements

1. **Multiple floats support**: Currently only stores one float per side
2. **Correct line wrapping**: Text will properly wrap around floats
3. **Clear property**: Will correctly position elements below floats
4. **Float stacking**: Multiple floats will stack correctly

### Metrics to Track

- `make layout suite=baseline` - Must remain 100% pass
- `make layout suite=position` - Target: >50% pass rate (from current ~3%)

### Files to Modify

1. `radiant/layout_positioned.hpp` - New data structures
2. `radiant/layout_positioned.cpp` - New implementation
3. `radiant/layout_text.cpp` - Integration with line layout
4. `radiant/layout.hpp` - Updated FloatContext forward declaration

---

## Appendix: Current vs Proposed Comparison

| Aspect | Current | Proposed |
|--------|---------|----------|
| Float storage | Single pointer per side | Linked list per side |
| Y-based queries | Manual iteration | `float_space_at_y()` API |
| Margin box tracking | Position only | Full margin box bounds |
| Multiple floats | Not supported | Fully supported |
| Clear implementation | Incomplete | Proper specification compliance |
| Line adjustment | Hardcoded hacks | Dynamic query-based |
| Memory management | Manual, leak-prone | Centralized cleanup |
