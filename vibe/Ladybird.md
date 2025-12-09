# Comparative Analysis: Radiant vs Ladybird Layout Engines

## Executive Summary

Based on analyzing both codebases, here are the **key areas where Ladybird's architecture is superior** that Radiant could improve on.

---

## 1. Layout State Separation (Critical)

**Ladybird's Approach:**
```cpp
// Ladybird uses a separate, immutable LayoutState that can be committed or discarded
struct LayoutState {
    struct UsedValues {
        CSSPixels content_width, content_height;
        CSSPixels margin_left, margin_right, margin_top, margin_bottom;
        CSSPixels border_left, border_right, border_top, border_bottom;
        CSSPixels padding_left, padding_right, padding_top, padding_bottom;
        CSSPixelPoint offset;  // Position relative to containing block
        Vector<LineBox> line_boxes;  // For IFC containers
        bool m_has_definite_width, m_has_definite_height;
        SizeConstraint width_constraint, height_constraint;
    };
    OrderedHashMap<Layout::Node, UsedValues> used_values_per_layout_node;
    void commit(Box& root);  // Makes changes permanent
};
```

**Radiant's Current Approach:**
```cpp
// Radiant directly modifies view objects during layout
typedef struct LayoutContext {
    View* view;
    BlockContext block;  // current state embedded in context
    Linebox line;
    // ...
} LayoutContext;
```

**Recommendation:** Create a `LayoutState` equivalent that stores computed values separately. This allows:
- Speculative layout for intrinsic sizing without side effects
- Clean separation of "specified" vs "computed" vs "used" values
- Easier debugging (snapshot the state at any point)

---

## 2. Formatting Context Hierarchy (Important)

**Ladybird's Approach:**
```cpp
// Clear class hierarchy with virtual methods
class FormattingContext {
    virtual void run(AvailableSpace const&) = 0;
    // Shared utilities for all formatting contexts
};

class BlockFormattingContext : public FormattingContext { /* ... */ };
class InlineFormattingContext : public FormattingContext { /* ... */ };
class FlexFormattingContext : public FormattingContext { /* ... */ };
```

**Radiant's Current Approach:**
```cpp
// Functions for each layout type, sharing global LayoutContext
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void layout_flex_container(LayoutContext* lycon, ViewBlock* container);
```

**Recommendation:** Abstract the formatting context concept:
- Create a `FormattingContext` base struct or C++ class
- Each type (Block, Inline, Flex, Grid) becomes a proper context object
- Encapsulates context-specific state (floats for BFC, line boxes for IFC)

---

## 3. Inline Layout Producer-Consumer Pattern (Important)

**Ladybird's Approach:**
```cpp
// Clean separation: InlineLevelIterator produces items, LineBuilder consumes
class InlineLevelIterator {
    struct Item {
        enum class Type { Text, Element, ForcedBreak, AbsolutelyPositionedElement, FloatingElement };
        Type type;
        Node* node;
        RefPtr<Gfx::GlyphRun> glyph_run;  // Pre-shaped text
        CSSPixels width, padding_start, padding_end, border_start, border_end;
        CSSPixels margin_start, margin_end;
        bool is_collapsible_whitespace;
    };
    Optional<Item> next();  // Produces items one at a time
    CSSPixels next_non_whitespace_sequence_width();  // Lookahead for line breaking
};

class LineBuilder {
    void append_box(...);
    void append_text_chunk(...);
    void break_line(...);
    void break_if_needed(CSSPixels next_item_width);
    void update_last_line();  // Apply vertical alignment
};
```

**Radiant's Current Approach:**
```cpp
// layout_text.cpp handles text and line breaking together
void layout_text(LayoutContext* lycon, DomNode *text_node) {
    // Character-by-character layout with embedded line-breaking logic
    do {
        if (rect->x + rect->width > lycon->line.right) { // line filled up
            // Line break logic embedded here
        }
    } while (*str);
}
```

**Recommendation:**
1. Create an `InlineItem` abstraction (text run, inline-block, BR, float)
2. Create an `InlineIterator` to produce items from DOM
3. Create a `LineBuilder` that accumulates items and handles breaks

---

## 4. Available Space Abstraction (Medium Priority)

**Ladybird's Approach:**
```cpp
class AvailableSize {
    enum class Type { Definite, Indefinite, MinContent, MaxContent };
    Type m_type;
    CSSPixels m_value;
};

class AvailableSpace {
    AvailableSize width;
    AvailableSize height;
};
```

**Radiant's Current Approach:**
```cpp
// Uses -1 as a magic value for "auto"
lycon->block.given_width = -1;  // means auto
lycon->block.given_height = -1; // means auto
```

**Recommendation:** Create an `AvailableSize` type that explicitly represents:
- Definite values (specific pixel value)
- Indefinite (auto, shrink-to-fit)
- Min-content mode (for intrinsic sizing)
- Max-content mode (for intrinsic sizing)

---

## 5. Margin Collapsing State Machine (Medium Priority)

**Ladybird's Approach:**
```cpp
class BlockMarginState {
    CSSPixels m_current_positive_collapsible_margin { 0 };
    CSSPixels m_current_negative_collapsible_margin { 0 };
    Function<void(CSSPixels)> m_block_container_y_position_update_callback;
    bool m_box_last_in_flow_child_margin_bottom_collapsed { false };

    void add_margin(CSSPixels margin) {
        if (margin < 0)
            m_current_negative_collapsible_margin = min(margin, m_current_negative_collapsible_margin);
        else
            m_current_positive_collapsible_margin = max(margin, m_current_positive_collapsible_margin);
    }

    CSSPixels current_collapsed_margin() const {
        return m_current_positive_collapsible_margin + m_current_negative_collapsible_margin;
    }
};
```

**Radiant's Current Approach:**
```cpp
// layout_block.cpp: ad-hoc margin collapsing in multiple places
if (block->parent_view()->first_placed_child() == block) {  // first child
    if (block->bound->margin.top > 0) {
        // Manual collapsing logic
    }
}
// ... and more collapsing logic for siblings and bottom margins
```

**Recommendation:**
- Create a `MarginCollapsingState` struct
- Track positive/negative margins separately (CSS spec requires this)
- Use a callback pattern for deferred Y positioning (needed for parent-child collapse)

---

## 6. Float Context Encapsulation (Medium Priority)

**Ladybird's Approach:**
```cpp
struct FloatingBox {
    Box const& box;
    UsedValues& used_values;
    CSSPixels offset_from_edge;
    CSSPixels top_margin_edge;
    CSSPixels bottom_margin_edge;
    CSSPixelRect margin_box_rect_in_root_coordinate_space;
};

struct FloatSideData {
    Vector<FloatingBox> all_boxes;
    Vector<FloatingBox&> current_boxes;  // Currently active at this Y
    CSSPixels current_width;
};

// Query function - efficient Y-coordinate based lookup
SpaceUsedAndContainingMarginForFloats space_used_and_floats(CSSPixels y) const {
    SpaceUsedAndContainingMarginForFloats result;
    for (auto const& floating_box : m_left_floats.all_boxes.in_reverse()) {
        auto rect = floating_box.margin_box_rect_in_root_coordinate_space;
        if (rect.contains_vertically(y)) {
            result.left_used_space = floating_box.offset_from_edge + ...;
            break;
        }
    }
    return result;
}
```

**Radiant's Current Approach:**
```cpp
struct FloatContext;  // Forward declared
FloatContext* get_current_float_context(LayoutContext* lycon);
void adjust_line_for_floats(LayoutContext* lycon, FloatContext* float_ctx);
```

**Recommendation:** The current `FloatContext` approach is reasonable, but enhance it with:
- Store floats with their vertical extents for efficient Y-based queries
- Separate left and right float lists
- Add `space_at_y()` query function

---

## 7. Intrinsic Sizing with Throwaway State (Important)

**Ladybird's Approach:**
```cpp
CSSPixels calculate_min_content_width(Box const& box) const {
    // Check cache first
    auto& cache = box.cached_intrinsic_sizes().min_content_width;
    if (cache.has_value()) return cache.value();

    // Create throwaway layout state - KEY PATTERN
    LayoutState throwaway_state;
    auto& box_state = throwaway_state.get_mutable(box);
    box_state.width_constraint = SizeConstraint::MinContent;
    box_state.set_indefinite_content_width();

    // Run layout, get result, cache it, return
    auto context = create_independent_formatting_context(throwaway_state, LayoutMode::IntrinsicSizing, box);
    context->run(AvailableSpace(AvailableSize::make_min_content(), ...));

    auto result = context->automatic_content_width();
    cache = result;  // Cache the result
    return cache.value();
}
```

**Radiant's Current Approach:**
```cpp
// layout_flex_measurement.hpp - has is_measuring flag
typedef struct LayoutContext {
    bool is_measuring;  // When true, layout is for measuring intrinsic sizes
} LayoutContext;
```

**Recommendation:** Implement the throwaway state pattern:
1. Create a `LayoutState` that can be allocated temporarily
2. Run full layout with that state
3. Extract intrinsic sizes
4. Discard the state (or cache if needed)
5. Cache results per-node to avoid redundant calculations

---

## 8. Writing Mode Abstraction (Low Priority for Now)

**Ladybird's Approach:**
```cpp
// Uses inline_offset/block_offset instead of x/y
struct LineBoxFragment {
    CSSPixels m_inline_offset, m_block_offset;
    CSSPixels m_inline_length, m_block_length;
};

class LineBox {
    CSS::Direction m_direction;
    CSS::WritingMode m_writing_mode;
};
```

**Radiant's Current Approach:**
```cpp
struct TextRect {
    float x, y;
    float width, height;
};
```

**Recommendation:** For future writing-mode support, consider:
- Using `inline_offset`/`block_offset` terminology
- Abstracting coordinate access through methods

---

## 9. Line Box Data Structure (Important)

**Ladybird's Approach:**
```cpp
class LineBox {
    Vector<LineBoxFragment> m_fragments;
    CSSPixels m_baseline;
    CSSPixels m_inline_length, m_block_length;
};

class LineBoxFragment {
    Node const& m_layout_node;
    size_t m_start, m_length_in_code_units;  // For text
    CSSPixels m_inline_offset, m_block_offset;
    CSSPixels m_inline_length, m_block_length;
    CSSPixels m_baseline;
    RefPtr<Gfx::GlyphRun> m_glyph_run;  // Pre-shaped text
};
```

**Radiant's Current Approach:**
```cpp
typedef struct Linebox {
    float left, right;
    float advance_x;
    float max_ascender, max_descender;
    View* start_view;
    // ...
} Linebox;
```

**Recommendation:** Create proper `LineBox` and `LineBoxFragment` structures:
- Store fragments/views explicitly in the line box
- Each fragment knows its baseline
- Makes vertical alignment a clean second pass

---

## 10. Vertical Alignment Two-Pass (Medium Priority)

**Ladybird's Approach:**

**Pass 1**: Calculate line box baseline from all fragments
```cpp
// Start with the "strut" - imaginary zero-width box
auto strut_baseline = [&] {
    auto& font = containing_block.first_available_font();
    auto font_metrics = font.pixel_metrics();
    auto typographic_height = font_metrics.ascent + font_metrics.descent;
    auto leading = line_height - typographic_height;
    auto half_leading = leading / 2;
    return font_metrics.ascent + half_leading;
}();

CSSPixels line_box_baseline = strut_baseline;
for (auto& fragment : line_box.fragments()) {
    CSSPixels fragment_baseline = ...;
    line_box_baseline = max(line_box_baseline, fragment_baseline);
}
```

**Pass 2**: Position fragments based on alignment relative to baseline
```cpp
for (auto& fragment : line_box.fragments()) {
    CSSPixels new_fragment_block_offset;
    switch (vertical_align) {
    case CSS::VerticalAlign::Baseline:
        new_fragment_block_offset = alphabetic_baseline;
        break;
    case CSS::VerticalAlign::Top:
        new_fragment_block_offset = m_current_block_offset + effective_box_top_offset;
        break;
    // ...
    }
    fragment.set_block_offset(new_fragment_block_offset);
}
```

**Radiant's Current Approach:**
```cpp
void line_break(LayoutContext* lycon) {
    if (lycon->line.max_ascender > lycon->block.init_ascender || ...) {
        // Adjust views retroactively
        View* view = lycon->line.start_view;
        do { view_vertical_align(lycon, vw); } while (vw);
    }
}
```

**Recommendation:** The current approach works but could be cleaner:
1. First pass: Collect all items in the line, calculate max baseline
2. Second pass: Position each item based on its alignment

---

## Priority Improvement Roadmap

| Priority | Improvement | Effort | Impact |
|----------|-------------|--------|--------|
| 🔴 High | Layout State Separation | High | Critical for intrinsic sizing, debugging |
| 🔴 High | Formatting Context Hierarchy | Medium | Better organization, maintainability |
| 🟡 Medium | Inline Producer-Consumer Pattern | Medium | Cleaner text layout code |
| 🟡 Medium | AvailableSpace Abstraction | Low | Cleaner API, fewer magic values |
| 🟡 Medium | Margin Collapsing State Machine | Medium | More correct edge cases |
| 🟡 Medium | Line Box Data Structure | Medium | Cleaner vertical alignment |
| 🟢 Low | Writing Mode Abstraction | Low | Future-proofing |

---

## Quick Wins (Can Implement Soon)

1. **Replace `-1` magic values** with an `AvailableSize` enum/struct
2. **Create `LineBox` struct** that holds fragments explicitly
3. **Extract margin collapsing logic** into a dedicated function/struct
4. **Cache intrinsic size calculations** per-node to avoid redundant layout

---

## Key Architectural Differences Summary

| Aspect | Ladybird | Radiant |
|--------|----------|---------|
| Layout state | Separate, immutable, can be discarded | Directly modifies views |
| Formatting contexts | Class hierarchy with virtual run() | Functions sharing LayoutContext |
| Inline layout | Producer-consumer (Iterator + LineBuilder) | Monolithic character loop |
| Size constraints | AvailableSize enum (Definite/Indefinite/MinContent/MaxContent) | -1 magic value for auto |
| Margin collapsing | Stateful accumulator with callbacks | Ad-hoc inline checks |
| Line boxes | Explicit LineBox with LineBoxFragment vector | Implicit via view chain |
| Intrinsic sizing | Throwaway state + caching | is_measuring flag |
| Coordinates | inline_offset/block_offset (writing-mode aware) | x/y (horizontal only) |
