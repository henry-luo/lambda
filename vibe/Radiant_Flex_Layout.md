## Key Data Structures
### FlexContainerLayout Structure

```cpp
typedef struct {
    // Flex container properties
    int direction;               // row, column, row-reverse, column-reverse
    int wrap;                   // nowrap, wrap, wrap-reverse
    int justify;                // flex-start, center, space-between, etc.
    int align_items;            // flex-start, center, stretch, etc.
    int align_content;          // For multi-line flex containers

    // Gap support (CRITICAL)
    int row_gap, column_gap;    // Gap between flex items

    // Calculated dimensions
    int main_axis_size;         // Container main axis size
    int cross_axis_size;        // Container cross axis size

    // Flex lines for wrapping
    FlexLineInfo* lines;
    int line_count;

    // Flex items
    ViewBlock** flex_items;
    int item_count;

    bool needs_reflow;
} FlexContainerLayout;
```

## Flexbox Layout Implementation

### Flex Layout Algorithm

The flexbox implementation follows the CSS Flexbox specification with these phases:

#### Phase 1: Flex Item Collection

```cpp
int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    // Traverse container children
    // Initialize flex properties if not set
    if (child->flex_basis == 0 && child->flex_grow == 0 && child->flex_shrink == 0) {
        child->flex_basis = -1;  // auto - use intrinsic size
        child->flex_grow = 0;    // don't grow by default
        child->flex_shrink = 1;  // can shrink by default
        child->order = 0;        // default order
    }
}
```

#### Phase 2: Flex Basis Calculation

```cpp
int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout) {
    if (item->flex_basis == -1) {
        // auto - use content size
        int basis = get_main_axis_size(item, flex_layout);
        return basis;
    } else if (item->flex_basis_is_percent) {
        // percentage basis
        int container_size = is_main_axis_horizontal(flex_layout) ?
                           flex_layout->main_axis_size : flex_layout->cross_axis_size;
        return (container_size * item->flex_basis) / 100;
    } else {
        // fixed length
        return item->flex_basis;
    }
}
```

#### Phase 3: Space Distribution

```cpp
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    // Calculate total basis size
    int total_basis_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int basis = calculate_flex_basis(item, flex_layout);
        set_main_axis_size(item, basis, flex_layout);
        total_basis_size += basis;
    }

    // Add gap space
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    total_basis_size += gap_space;

    // Calculate and distribute free space
    int free_space = container_main_size - total_basis_size;
    // ... flex-grow/shrink distribution logic
}
```

#### Phase 4: Item Positioning

```cpp
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    // Apply justify-content
    switch (justify) {
        case CSS_VALUE_FLEX_START:
            current_pos = 0;
            break;
        case CSS_VALUE_CENTER:
            current_pos = (container_size - total_size_with_gaps) / 2;
            break;
        case CSS_VALUE_SPACE_BETWEEN:
            spacing = (container_size - total_item_size - gap_space) / (line->item_count - 1);
            break;
    }

    // Position each item with gaps
    for (int i = 0; i < line->item_count; i++) {
        set_main_axis_position(item, current_pos, flex_layout);
        current_pos += get_main_axis_size(item, flex_layout);
        if (i < line->item_count - 1) {
            current_pos += gap_value;  // Add gap between items
        }
    }
}
```

### Critical Flex Implementation Details

#### Border Offset Handling

```cpp
void set_main_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout) {
    // CRITICAL: Account for container border offset
    ViewBlock* container = (ViewBlock*)item->parent;
    int border_offset = 0;

    if (container && container->bound && container->bound->border) {
        if (is_main_axis_horizontal(flex_layout)) {
            border_offset = container->bound->border->width.left;
        } else {
            border_offset = container->bound->border->width.top;
        }
    }

    if (is_main_axis_horizontal(flex_layout)) {
        item->x = position + border_offset;
    } else {
        item->y = position + border_offset;
    }
}
```

#### Box Model Integration

```cpp
int get_border_box_width(ViewBlock* item) {
    // FIXED: Use item dimensions directly - no double-subtraction
    // The block layout phase already calculated correct dimensions based on box-sizing
    return item->width;
}
```

## Things to Watch Out For

### 1. Double-Subtraction Bug

**Problem**: Subtracting padding/borders twice in flex calculations
**Solution**: Trust the block layout phase box-sizing calculations

### 2. Flex Property Initialization

**Problem**: ViewBlock objects created with all zeros, causing flex_basis=0 instead of auto
**Solution**: Initialize flex properties in `collect_flex_items()`

### 3. Gap vs Spacing Confusion

**Problem**: Mixing CSS gap property with justify-content spacing
**Solution**: Calculate gaps separately and add to total space calculations

### 4. Content vs Border-Box Dimensions

**Problem**: Inconsistent use of content_width vs width across layout phases
**Solution**: Establish clear conventions for which phase uses which dimensions

### 5. Memory Management

**Problem**: Flex containers and lines need proper cleanup
**Solution**: Implement `cleanup_flex_container()` with proper memory deallocation
