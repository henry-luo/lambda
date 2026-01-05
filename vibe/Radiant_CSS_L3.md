# Radiant CSS Level 3+ Enhancement Proposal

## Executive Summary

This document proposes enhancements to the Radiant layout engine to achieve more complete CSS Level 3 (and select CSS Level 4) specification compliance. The analysis is based on current implementation state in the `radiant/` directory and `lambda/input/css/` parser.

---

## 1. Current Implementation Status

### 1.1 What's Already Implemented

| Feature | Status | Files |
|---------|--------|-------|
| **Box Model** | ✅ Complete | `layout_block.cpp`, `view.hpp` |
| **Block/Inline Flow** | ✅ Complete | `layout_block.cpp`, `layout_inline.cpp` |
| **Flexbox** | ✅ Complete (9-phase) | `layout_flex_multipass.cpp`, `layout_flex.cpp` |
| **CSS Grid (Basic)** | ✅ Complete | `layout_grid_multipass.cpp`, `grid_sizing.cpp` |
| **CSS Positioning** | ✅ Complete | `layout_positioned.cpp` |
| **Float Layout** | ✅ Complete | `block_context.cpp` |
| **Table Layout** | ✅ Complete | `layout_table.cpp` |
| **CSS Variables** | ✅ Complete | `resolve_css_style.cpp` |
| **calc() Function** | ✅ Partial | `css_value_parser.cpp` |
| **Color Functions** | ✅ Partial | `resolve_css_style.cpp` (rgb, rgba, hsl, hsla) |
| **Gradients** | ✅ Complete | `render_background.cpp` (linear, radial, conic) |
| **Border Radius** | ✅ Complete | `render_border.cpp` |
| **Box Shadow** | ⚠️ Parsed | `css_style.hpp` (property defined) |
| **Text Decoration** | ✅ Basic | `render.cpp` |
| **Pseudo Elements** | ✅ Complete | `layout_block.cpp` (::before, ::after) |
| **CSS Counters** | ✅ Complete | `layout_counters.cpp` |
| **@font-face** | ✅ Complete | `font_face.cpp`, `css_font_face.cpp` |

### 1.2 Parser Support (Parsed but Not Rendered)

The CSS parser (`css_style.hpp`) defines these properties but they lack layout/render implementation:

- **Transforms**: `transform`, `transform-origin`, `transform-style`, `perspective`
- **Animations**: `animation-*`, `@keyframes`
- **Transitions**: `transition-*`
- **Filters**: `filter`, `backdrop-filter`
- **Multi-column**: `column-count`, `column-width`, `columns`, `column-gap`
- **Writing Modes**: `writing-mode`, `text-orientation`
- **Container Queries**: `container`, `container-type`, `container-name`
- **Logical Properties**: `block-size`, `inline-size`, `inset-*`
- **Scroll Snap**: `scroll-snap-type`, `scroll-snap-align`

---

## 2. Priority Enhancement Areas

### 2.1 Tier 1: High Priority (Common Use, High Impact)

#### 2.1.1 CSS Transforms (2D/3D)

**Specification**: CSS Transforms Level 1 & 2

**Current State**: Properties parsed in `css_style.hpp`, no rendering

**Required Implementation**:

```
New Files:
- radiant/transform.hpp          # Transform matrix structures
- radiant/transform.cpp          # Matrix operations, decomposition
- radiant/render_transform.cpp   # Transform application during render

Modified Files:
- radiant/view.hpp              # Add TransformProp to views
- radiant/resolve_css_style.cpp # Parse transform functions
- radiant/render.cpp            # Apply transforms before rendering
```

**Data Structures**:
```cpp
// transform.hpp
struct Transform2D {
    float a, b, c, d, tx, ty;  // 2D affine matrix
};

struct Transform3D {
    float m[16];  // 4x4 matrix for perspective
};

struct TransformProp {
    Transform3D matrix;           // Computed transform matrix
    float origin_x, origin_y;     // transform-origin (percentage or px)
    float origin_z;               // Z origin for 3D
    float perspective;            // perspective value
    CssEnum transform_style;      // flat | preserve-3d
    CssEnum backface_visibility;  // visible | hidden
};
```

**Functions to Parse**:
- `translate(x, y)`, `translateX(x)`, `translateY(y)`, `translateZ(z)`, `translate3d(x,y,z)`
- `rotate(angle)`, `rotateX/Y/Z(angle)`, `rotate3d(x,y,z,angle)`
- `scale(x, y)`, `scaleX/Y/Z(s)`, `scale3d(x,y,z)`
- `skew(x, y)`, `skewX(angle)`, `skewY(angle)`
- `matrix(a,b,c,d,tx,ty)`, `matrix3d(...)`
- `perspective(d)`

**Rendering Approach**:
1. Pre-render: Compute accumulated transform matrix for each view
2. During render: Transform ThorVG canvas or apply matrix to coordinates
3. Handle stacking context creation (transforms create new stacking context)

**Estimated Effort**: 3-4 weeks

---

#### 2.1.2 CSS Filters

**Specification**: CSS Filter Effects Level 1

**Current State**: Properties parsed, not rendered

**Required Implementation**:

```
New Files:
- radiant/filter.hpp             # Filter definitions
- radiant/filter.cpp             # Filter application via ThorVG

Modified Files:
- radiant/view.hpp              # Add FilterProp
- radiant/resolve_css_style.cpp # Parse filter functions
- radiant/render.cpp            # Apply filters post-render
```

**Filter Functions**:
- `blur(radius)` - Gaussian blur
- `brightness(amount)` - Adjust brightness
- `contrast(amount)` - Adjust contrast
- `grayscale(amount)` - Convert to grayscale
- `hue-rotate(angle)` - Rotate hue
- `invert(amount)` - Invert colors
- `opacity(amount)` - Set opacity
- `saturate(amount)` - Adjust saturation
- `sepia(amount)` - Sepia tone
- `drop-shadow(x y blur color)` - Drop shadow

**ThorVG Integration**:
```cpp
// ThorVG provides scene effects for filters
Tvg_Paint* apply_blur(Tvg_Paint* paint, float radius);
// Use ThorVG's scene composition for multi-pass filter effects
```

**Estimated Effort**: 2 weeks

---

#### 2.1.3 Box Shadow (Complete Rendering)

**Specification**: CSS Backgrounds and Borders Level 3

**Current State**: Property parsed (`CSS_PROPERTY_BOX_SHADOW`), not rendered

**Required Implementation**:

```cpp
// In view.hpp
struct BoxShadow {
    float offset_x, offset_y;    // Shadow offset
    float blur_radius;           // Blur amount
    float spread_radius;         // Spread (expand/contract shadow)
    Color color;                 // Shadow color
    bool inset;                  // Inset shadow
    struct BoxShadow* next;      // Multiple shadows
};

// In render_background.cpp
void render_box_shadow(RenderContext* rdcon, ViewBlock* block, BoxShadow* shadows);
```

**Estimated Effort**: 1 week

---

#### 2.1.4 Multi-Column Layout

**Specification**: CSS Multi-column Layout Level 1

**Required Implementation**:

```
New Files:
- radiant/layout_multicol.hpp    # Multi-column container
- radiant/layout_multicol.cpp    # Column balancing algorithm

Modified Files:
- radiant/layout_block.cpp       # Dispatch to multicol layout
- radiant/view.hpp              # MulticolProp structure
```

**Data Structures**:
```cpp
struct MulticolProp {
    int column_count;            // auto or explicit count
    float column_width;          // Ideal column width
    float column_gap;            // Gap between columns
    CssEnum column_fill;         // auto | balance
    // Column rule (border between columns)
    float rule_width;
    CssEnum rule_style;
    Color rule_color;
};
```

**Algorithm**:
1. Calculate available width and optimal column count
2. Lay out content into single flow
3. Break content across columns at optimal points
4. Handle `break-before`, `break-after`, `break-inside`
5. Balance column heights (if `column-fill: balance`)

**Estimated Effort**: 3 weeks

---

### 2.2 Tier 2: Medium Priority (Growing Adoption)

#### 2.2.1 CSS Transitions

**Specification**: CSS Transitions Level 1

**Current State**: Properties parsed, no animation runtime

**Required Implementation**:

```
New Files:
- radiant/transition.hpp         # Transition state tracking
- radiant/transition.cpp         # Interpolation, timing functions
- radiant/animation_frame.cpp    # RequestAnimationFrame equivalent

Modified Files:
- radiant/view.hpp              # Add TransitionState to views
- radiant/resolve_css_style.cpp # Detect property changes, trigger transitions
- radiant/window.cpp            # Animation loop integration
```

**Data Structures**:
```cpp
struct TransitionDef {
    CssPropertyId property;      // Property being transitioned
    float duration;              // Duration in ms
    float delay;                 // Delay in ms
    CssEnum timing_function;     // ease, linear, ease-in, etc.
    float* bezier_control;       // Custom cubic-bezier points
};

struct ActiveTransition {
    TransitionDef* def;
    CssValue start_value;
    CssValue end_value;
    double start_time;
    bool is_running;
};

struct TransitionState {
    ActiveTransition* transitions;
    int transition_count;
};
```

**Timing Functions**:
- `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`
- `cubic-bezier(x1, y1, x2, y2)`
- `step-start`, `step-end`, `steps(n, start|end)`

**Interpolation Support** (animatable properties):
- Length values: `width`, `height`, `margin`, `padding`, `top/right/bottom/left`
- Colors: `color`, `background-color`, `border-color`
- Transforms: matrix interpolation
- Opacity: `opacity`

**Estimated Effort**: 2-3 weeks

---

#### 2.2.2 CSS Animations

**Specification**: CSS Animations Level 1

**Current State**: `@keyframes` rule type defined, not processed

**Required Implementation**:

```
New Files:
- radiant/animation.hpp          # Keyframe storage, animation state
- radiant/animation.cpp          # Animation playback, keyframe interpolation

Modified Files:
- lambda/input/css/css_engine.cpp  # Process @keyframes rules
- radiant/resolve_css_style.cpp    # Apply animation properties
- radiant/window.cpp               # Animation tick integration
```

**Data Structures**:
```cpp
struct Keyframe {
    float offset;                // 0.0 to 1.0 (from/to percentages)
    CssDeclaration** properties; // Properties at this keyframe
    int property_count;
};

struct KeyframeAnimation {
    const char* name;
    Keyframe* keyframes;
    int keyframe_count;
};

struct AnimationState {
    KeyframeAnimation* animation;
    float duration;
    float delay;
    int iteration_count;         // -1 for infinite
    CssEnum direction;           // normal, reverse, alternate, alternate-reverse
    CssEnum fill_mode;           // none, forwards, backwards, both
    CssEnum play_state;          // running, paused
    double start_time;
    int current_iteration;
};
```

**Estimated Effort**: 3 weeks (builds on transition infrastructure)

---

#### 2.2.3 CSS Grid Level 2 (Subgrid)

**Specification**: CSS Grid Layout Level 2

**Current State**: Basic grid complete, no subgrid

**Required Changes**:

```cpp
// In grid.hpp - extend GridTrackSizeType
typedef enum {
    // ... existing types ...
    GRID_TRACK_SIZE_SUBGRID      // NEW: subgrid
} GridTrackSizeType;

// In GridContainerLayout
bool subgrid_rows;               // True if grid-template-rows: subgrid
bool subgrid_columns;            // True if grid-template-columns: subgrid
GridContainerLayout* parent_grid; // Reference to parent grid for subgrid
```

**Algorithm Changes**:
1. During track sizing: If subgrid, inherit track sizes from parent
2. Named lines: Inherit from parent grid
3. Gaps: May differ from parent

**Estimated Effort**: 2 weeks

---

#### 2.2.4 Writing Modes and Logical Properties

**Specification**: CSS Writing Modes Level 4

**Current State**: Properties defined, horizontal-only layout

**Required Changes**:

```cpp
// In view.hpp
struct WritingModeProp {
    CssEnum writing_mode;        // horizontal-tb, vertical-rl, vertical-lr
    CssEnum direction;           // ltr, rtl
    CssEnum text_orientation;    // mixed, upright, sideways
};

// Logical property mapping
struct LogicalToPhysical {
    float inline_start;          // Maps to left/right/top/bottom based on writing mode
    float inline_end;
    float block_start;
    float block_end;
};
```

**Layout Changes**:
- `layout_block.cpp`: Swap width/height axes for vertical modes
- `layout_inline.cpp`: Change text flow direction
- `layout_text.cpp`: Rotate glyphs for vertical text (FreeType vertical metrics)
- All property resolution: Map logical to physical based on writing mode

**Estimated Effort**: 4-5 weeks (significant refactoring)

---

#### 2.2.5 Container Queries

**Specification**: CSS Containment Level 3

**Current State**: Properties defined, not evaluated

**Required Implementation**:

```cpp
// In css_style.hpp - already defined
CSS_RULE_CONTAINER  // @container rule

// New structures needed
struct ContainerQueryCondition {
    const char* container_name;  // Named container or implicit
    CssEnum feature;             // width, height, inline-size, block-size
    CssEnum comparator;          // min, max, exact
    float value;
};

// In view.hpp
struct ContainmentProp {
    CssEnum container_type;      // normal, size, inline-size
    const char* container_name;
    bool is_query_container;
};
```

**Algorithm**:
1. During style resolution: Identify container query contexts
2. Before layout: Evaluate container conditions
3. Apply/remove rules based on container dimensions
4. Handle circular dependencies (container size depends on content)

**Estimated Effort**: 3 weeks

---

#### 2.2.6 Scroll Snap

**Specification**: CSS Scroll Snap Level 1

**Current State**: Properties defined, not implemented

**Required Implementation**:

```cpp
// In view.hpp
struct ScrollSnapProp {
    CssEnum snap_type_x;         // none, mandatory, proximity
    CssEnum snap_type_y;
    CssEnum snap_align_x;        // start, end, center
    CssEnum snap_align_y;
    float snap_margin_top, snap_margin_right, snap_margin_bottom, snap_margin_left;
    float snap_padding_top, snap_padding_right, snap_padding_bottom, snap_padding_left;
};
```

**Integration with Scroller**:
```cpp
// In scroller.cpp
void scroller_snap_to_point(ScrollPane* sp, float target_x, float target_y);
void scroller_find_snap_points(ScrollPane* sp, float current_x, float current_y);
```

**Estimated Effort**: 2 weeks

---

### 2.3 Tier 3: Lower Priority (Future-Proofing)

#### 2.3.1 CSS Masking and Clipping

**Specification**: CSS Masking Level 1

**Features**:
- `clip-path`: Clip to shape (circle, ellipse, polygon, path, url)
- `mask-image`: Image-based masking
- `mask-mode`: alpha, luminance, match-source

**ThorVG Support**: Use `tvg_paint_set_mask_method()` (already used in `render_border.cpp`)

**Estimated Effort**: 2 weeks

---

#### 2.3.2 CSS Shapes

**Specification**: CSS Shapes Level 1

**Features**:
- `shape-outside`: Float around shapes
- `shape-margin`: Margin around shape
- `shape-image-threshold`: For image-based shapes

**Estimated Effort**: 3 weeks

---

#### 2.3.3 CSS Cascade Layers

**Specification**: CSS Cascade Level 5

**Current State**: `CSS_RULE_LAYER` defined, not processed

**Required**:
- Parse `@layer` rules and layer ordering
- Modify cascade resolution to respect layer order

**Estimated Effort**: 1 week

---

#### 2.3.4 :has() and Advanced Selectors

**Specification**: CSS Selectors Level 4

**Current State**: Stub implementations in `css_engine.cpp`

**Functions to Implement**:
- `:has()` - Parent/sibling selector
- `:is()` - Selector list matching
- `:where()` - Zero-specificity selector matching
- `:not()` - Negation (with complex selectors)

**Algorithm Challenge**: `:has()` requires reverse DOM traversal

**Estimated Effort**: 2 weeks

---

#### 2.3.5 @supports Feature Queries

**Specification**: CSS Conditional Rules Level 3

**Current State**: Rule type defined, not evaluated

**Required**:
- Property support detection
- Boolean operators (and, or, not)
- Selector support detection

**Estimated Effort**: 1 week

---

## 3. Implementation Architecture

### 3.1 New Module Structure

```
radiant/
├── transform.hpp/cpp        # CSS Transforms
├── animation.hpp/cpp        # CSS Animations runtime
├── transition.hpp/cpp       # CSS Transitions
├── filter.hpp/cpp           # CSS Filters
├── layout_multicol.cpp      # Multi-column layout
├── render_shadow.cpp        # Box/text shadow rendering
├── render_filter.cpp        # Filter effect application
└── snap.hpp/cpp             # Scroll snap logic

lambda/input/css/
├── css_keyframes.cpp        # @keyframes processing
├── css_container.cpp        # @container evaluation
└── css_layer.cpp            # @layer cascade integration
```

### 3.2 Animation Runtime Architecture

```cpp
// Central animation manager
class AnimationManager {
public:
    void tick(double timestamp);         // Called each frame
    void add_transition(View* view, ActiveTransition* trans);
    void add_animation(View* view, AnimationState* anim);
    void remove_all(View* view);         // On view destruction

private:
    std::vector<std::pair<View*, ActiveTransition*>> transitions;
    std::vector<std::pair<View*, AnimationState*>> animations;
};

// Integration point in window.cpp
void window_animation_frame(GLFWwindow* window) {
    double now = glfwGetTime() * 1000.0;  // ms
    animation_manager->tick(now);
    if (animation_manager->has_active()) {
        ui_context->needs_repaint = true;
    }
}
```

### 3.3 Transform Stack

```cpp
// Transform accumulation during render traversal
class TransformStack {
public:
    void push(const Transform3D& transform);
    void pop();
    Transform3D current() const;          // Accumulated matrix
    void apply_to_canvas(Tvg_Canvas* canvas);

private:
    std::vector<Transform3D> stack;
    Transform3D accumulated;
};
```

---

## 4. Testing Strategy

### 4.1 New Test Suites

```
test/layout/transform/     # Transform rendering tests
test/layout/animation/     # Animation playback tests
test/layout/filter/        # Filter effect tests
test/layout/multicol/      # Multi-column tests
test/layout/container/     # Container query tests
test/layout/writing/       # Writing mode tests
```

### 4.2 Reference Comparison

Continue using Puppeteer-based browser comparison:

```bash
# Run transform tests
make layout suite=transform

# Run animation snapshot tests (at specific keyframes)
make layout suite=animation
```

### 4.3 Visual Regression

For filters and transforms, add pixel-diff tolerance:

```cpp
// Allow slight rendering differences due to filter implementation
const float FILTER_PIXEL_TOLERANCE = 2.0f;  // RGB difference per channel
```

---

## 5. Performance Considerations

### 5.1 Transform Optimization

- Cache computed transform matrices on views
- Invalidate only when transform property changes
- Use hardware-accelerated compositing (future: GPU rendering)

### 5.2 Animation Performance

- Use frame budget management (16ms for 60fps)
- Skip intermediate frames if behind
- Batch property updates before repaint

### 5.3 Filter Rendering

- Render filter targets to offscreen buffer
- Cache filtered results when content unchanged
- Consider filter complexity in render order

---

## 6. Implementation Roadmap

### Phase 1: Core Visual Effects & Layout (Q1-Q2 2026)

| Week | Deliverable |
|------|-------------|
| 1-2  | Box shadow rendering |
| 3-6  | CSS Transforms (2D + basic 3D) |
| 7-8  | CSS Filters (blur, brightness, grayscale) |
| 9-11 | Multi-column layout |

### Phase 2: Animation System (Q3 2026)

| Week | Deliverable |
|------|-------------|
| 12-14| CSS Transitions |
| 15-17| CSS Animations + @keyframes |
| 18-19| Transform animations |

### Future: Advanced Features

The following features are planned for later phases:

| Feature | Estimated Effort | Priority |
|---------|------------------|----------|
| Writing modes (vertical text) | 4-5 weeks | Medium |
| Container queries | 3 weeks | Medium |
| Subgrid | 2 weeks | Medium |
| Scroll snap | 2 weeks | Low |
| Advanced selectors (:has, :is, :where) | 2 weeks | Low |
| CSS masking/clipping | 2 weeks | Low |

---

## 7. Dependencies and Prerequisites

### 7.1 External Libraries

| Feature | Dependency | Status |
|---------|------------|--------|
| Transforms | ThorVG matrix ops | ✅ Available |
| Filters | ThorVG scene effects | ✅ Available |
| Animations | Timer/frame callback | ✅ GLFW available |
| Vertical text | FreeType vertical metrics | ✅ Available |

### 7.2 Internal Prerequisites

1. **Stacking context refactor**: Transforms require proper z-ordering
2. **Render pipeline restructure**: Support pre/post effects
3. **Property inheritance update**: Logical properties depend on writing mode

---

## 8. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Transform performance | Medium | High | Implement matrix caching, profile early |
| Animation frame drops | Medium | Medium | Frame budget monitoring, skip logic |
| Writing mode complexity | High | High | Phase approach, horizontal-first |
| Filter memory usage | Medium | Medium | Limit filter buffer sizes |
| Browser compatibility | Low | Low | Use WPT reference tests |

---

## 9. Success Metrics

1. **Compatibility Score**: Pass 80%+ of relevant CSS Test Suite tests
2. **Performance**: Maintain 60fps for typical pages with transforms/animations
3. **Memory**: Filter buffers < 2x visible viewport memory
4. **Code Quality**: No regression in existing layout test suite

---

## 10. References

- [CSS Transforms Level 2](https://www.w3.org/TR/css-transforms-2/)
- [CSS Transitions Level 1](https://www.w3.org/TR/css-transitions-1/)
- [CSS Animations Level 1](https://www.w3.org/TR/css-animations-1/)
- [CSS Filter Effects Level 1](https://www.w3.org/TR/filter-effects-1/)
- [CSS Multi-column Layout Level 1](https://www.w3.org/TR/css-multicol-1/)
- [CSS Writing Modes Level 4](https://www.w3.org/TR/css-writing-modes-4/)
- [CSS Containment Level 3](https://www.w3.org/TR/css-contain-3/)
- [CSS Scroll Snap Level 1](https://www.w3.org/TR/css-scroll-snap-1/)
- [ThorVG Documentation](https://www.thorvg.org/docs)

---

## Appendix A: Property Support Matrix

| Property | Parsed | Resolved | Laid Out | Rendered |
|----------|--------|----------|----------|----------|
| `transform` | ✅ | ❌ | ❌ | ❌ |
| `transform-origin` | ✅ | ❌ | ❌ | ❌ |
| `transition-*` | ✅ | ❌ | N/A | ❌ |
| `animation-*` | ✅ | ❌ | N/A | ❌ |
| `filter` | ✅ | ❌ | N/A | ❌ |
| `backdrop-filter` | ✅ | ❌ | N/A | ❌ |
| `box-shadow` | ✅ | ✅ | N/A | ❌ |
| `column-count` | ✅ | ❌ | ❌ | ❌ |
| `writing-mode` | ✅ | ❌ | ❌ | ❌ |
| `container-type` | ✅ | ❌ | ❌ | N/A |
| `scroll-snap-type` | ✅ | ❌ | N/A | ❌ |
| `clip-path` | ✅ | ❌ | N/A | ❌ |
| `mask-image` | ❌ | ❌ | N/A | ❌ |
| `aspect-ratio` | ✅ | ✅ | ✅ | N/A |
| `gap` (flex/grid) | ✅ | ✅ | ✅ | N/A |

---

## Appendix B: Color Function Coverage

| Function | Parser | Resolve | Notes |
|----------|--------|---------|-------|
| `rgb()` | ✅ | ✅ | Modern + legacy syntax |
| `rgba()` | ✅ | ✅ | |
| `hsl()` | ✅ | ⚠️ | Parsed, TODO convert |
| `hsla()` | ✅ | ⚠️ | Parsed, TODO convert |
| `hwb()` | ✅ | ❌ | |
| `lab()` | ✅ | ❌ | |
| `lch()` | ✅ | ❌ | |
| `oklab()` | ✅ | ❌ | |
| `oklch()` | ✅ | ❌ | |
| `color()` | ✅ | ❌ | |
| `color-mix()` | ✅ | ❌ | |
