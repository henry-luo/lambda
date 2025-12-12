# Taffy vs Radiant Layout Engine: Analysis and Recommendations

## Executive Summary

This document provides a comprehensive analysis of Taffy (a Rust-based CSS layout library) and compares it with Radiant (Lambda's C++ layout engine). Key findings and recommendations are provided to enhance Radiant's grid layout capabilities.

### Test Results

| Test Suite | Pass Rate | Status |
|------------|-----------|--------|
| Baseline | 953/953 (100%) | ✅ Stable |
| Grid | 76/290 (26.2%) | 🔄 In Progress |

### Implementation Status

The following Taffy-inspired enhancements have been implemented:

| Component | File | Status |
|-----------|------|--------|
| Coordinate Types | `radiant/grid_types.hpp` | ✅ Complete |
| CellOccupancyMatrix | `radiant/grid_occupancy.hpp` | ✅ Complete |
| Enhanced GridTrack | `radiant/grid_track.hpp` | ✅ Complete |
| Track Sizing Algorithm | `radiant/grid_sizing_algorithm.hpp` | ✅ Complete |
| Auto-Placement | `radiant/grid_placement.hpp` | ✅ Complete |
| Unit Tests | `test/test_grid_enhanced_gtest.cpp` | ✅ Complete |
| Grid Enhanced Adapter | `radiant/grid_enhanced_adapter.hpp` | ✅ Complete |
| Relative Positioning Fix | `radiant/grid_positioning.cpp` | ✅ Complete |
| Grid Column Flow Height | `radiant/intrinsic_sizing.cpp` | ✅ Complete |
| fit-content() Parsing | `radiant/resolve_css_style.cpp` | ✅ Complete |
| fit-content() Track Sizing | `radiant/grid_sizing.cpp` | ✅ Complete |

### Recent Bug Fixes (December 2025)

| Issue | Fix | Files Modified |
|-------|-----|----------------|
| Grid items positioned with absolute coordinates | Changed to relative coordinates per Radiant design | `grid_positioning.cpp` |
| justify-self/align-self values overwritten | Removed reset code in `collect_grid_items` | `layout_grid.cpp` |
| CSS `order` property not working for grid | Added order field to GridItemProp, sorting in collect | `view.hpp`, `layout_grid.cpp`, `resolve_css_style.cpp` |
| Nested grid not detected | Added display resolution in `init_grid_item_view` | `layout_grid_multipass.cpp` |
| Grid column flow height calculation | Take max(child heights) for column flow | `intrinsic_sizing.cpp` |
| Padding not read during intrinsic sizing | Added fallback to read from CSS specified_style | `intrinsic_sizing.cpp` |
| Double-counted padding in grid measurement | Removed redundant padding addition | `layout_grid_multipass.cpp` |
| fi/gi union type ambiguity | Added `item_prop_type` tracking enum | `dom_element.hpp`, `view_pool.cpp` |
| place-self shorthand missing | Added CSS property handler | `resolve_css_style.cpp` |
| fit-content() function not parsed | Added parsing in `parse_css_value_to_track_size` | `resolve_css_style.cpp` |
| fit-content percentage vs px | Added `is_percentage` check in adapter conversion | `grid_enhanced_adapter.hpp` |
| fit-content track sizing missing | Added GRID_TRACK_SIZE_FIT_CONTENT handling | `grid_sizing.cpp` |
| Single track value not parsed | Added LENGTH/PERCENTAGE handling for grid-template-rows | `resolve_css_style.cpp` |
| Zero-width space breaks not detected | Added U+200B detection in intrinsic sizing | `intrinsic_sizing.cpp` |
| ZeroWidthSpace HTML entity missing | Added entity to html_entities.cpp table | `html_entities.cpp` |

---

## 1. Architecture Comparison

### 1.1 Taffy Architecture

**Key Design Principles:**

1. **Trait-Based Abstraction** - Taffy uses Rust traits (`LayoutPartialTree`, `TraverseTree`, etc.) to decouple the layout algorithm from the tree implementation. This enables:
   - Users to bring their own tree/node structures
   - Layout algorithms to work on any tree that implements the traits
   - Clean separation between style querying and layout computation

2. **Two-Level API**:
   - **High-level**: `TaffyTree` provides built-in node storage and automatic dispatch
   - **Low-level**: Direct algorithm functions (`compute_grid_layout`, `compute_flexbox_layout`) for embedding into custom systems

3. **Compute-on-Demand with Caching**:
   - `compute_cached_layout()` wraps layout functions with cache checks
   - Per-node caching based on `known_dimensions`, `available_space`, and `run_mode`
   - Cache invalidation is explicit

4. **Unified Layout Output**:
   ```rust
   struct LayoutOutput {
       size: Size<f32>,
       content_size: Size<f32>,  // For overflow/scroll
       first_baselines: Point<Option<f32>>,
   }
   ```

### 1.2 Radiant Architecture

**Current Design:**

1. **Unified DOM/View Tree** - DOM nodes extend to View types directly:
   ```cpp
   DomNode → DomElement → ViewBlock
   ```
   This is simpler but more coupled.

2. **LayoutContext as Central State**:
   ```cpp
   struct LayoutContext {
       DomDocument* doc;
       ViewGroup* view;
       DomElement* elmt;
       BlockContext block;
       Linebox line;
       // ... container-specific contexts
   };
   ```

3. **Pool-Based Allocation**: All layout structures use memory pools for fast allocation/bulk deallocation.

### 1.3 Key Differences

| Aspect | Taffy | Radiant |
|--------|-------|---------|
| Language | Rust | C++ |
| Tree Ownership | Trait-abstracted | Unified DOM/View |
| Caching | Built-in, per-node | Limited |
| Memory | Rust ownership | Pool allocation |
| Coordinate System | Abstract axis (Inline/Block) | Direct (x/y) |
| Style Access | Trait methods | Direct pointer access |

---

## 2. Grid Implementation Comparison

### 2.1 Taffy Grid Implementation

**Files:**
- `compute/grid/mod.rs` - Main entry point (600+ lines)
- `compute/grid/track_sizing.rs` - Track sizing algorithm
- `compute/grid/placement.rs` - Auto-placement algorithm
- `compute/grid/alignment.rs` - Track/item alignment
- `compute/grid/explicit_grid.rs` - Explicit grid resolution
- `compute/grid/implicit_grid.rs` - Implicit grid handling
- `compute/grid/types/` - Supporting types

**Algorithm Flow:**
```
1. Compute available grid space
2. Resolve explicit grid (templates, named areas)
3. Estimate implicit grid size
4. Place grid items (explicit + auto-placement)
5. Initialize grid tracks
6. Track sizing algorithm (intrinsic → maximize → flexible)
7. Compute container size
8. Resolve percentage track sizes
9. Re-run sizing if needed (for percentage-based tracks)
10. Track alignment (justify-content/align-content)
11. Item positioning and alignment
```

**Key Data Structures:**

```rust
// Grid track representation
struct GridTrack {
    kind: GridTrackKind,  // Track or Gutter
    min_track_sizing_function: MinTrackSizingFunction,
    max_track_sizing_function: MaxTrackSizingFunction,
    offset: f32,
    base_size: f32,
    growth_limit: f32,
    // Scratch values for sizing algorithm
    item_incurred_increase: f32,
    base_size_planned_increase: f32,
    growth_limit_planned_increase: f32,
    infinitely_growable: bool,
}

// Grid item representation
struct GridItem {
    node: NodeId,
    source_order: u16,
    row: Line<OriginZeroLine>,
    column: Line<OriginZeroLine>,
    // Cached style values
    size, min_size, max_size: Size<Dimension>,
    align_self, justify_self: AlignSelf,
    // Track crossing flags
    crosses_flexible_row/column: bool,
    crosses_intrinsic_row/column: bool,
    // Contribution caches
    min_content_contribution_cache: Size<Option<f32>>,
    max_content_contribution_cache: Size<Option<f32>>,
    minimum_contribution_cache: Size<Option<f32>>,
}

// Cell occupancy tracking
struct CellOccupancyMatrix {
    inner: Grid<CellOccupancyState>,
    columns: TrackCounts,
    rows: TrackCounts,
}
```

**Coordinate Systems:**
- `GridLine` - 1-based CSS grid line numbers
- `OriginZeroLine` - 0-based internal coordinates
- Track vector index - Index into `Vec<GridTrack>`

### 2.2 Radiant Grid Implementation (Current)

**Files:**
- `grid.hpp` - Data structures and declarations
- `grid_sizing.cpp` - Track sizing
- `grid_positioning.cpp` - Item positioning
- `grid_advanced.cpp` - Advanced features (minmax, repeat)
- `grid_utils.cpp` - Utility functions
- `layout_grid_multipass.cpp` - Main entry point

**Current Limitations:**
1. No `CellOccupancyMatrix` for auto-placement collision detection
2. Limited support for named grid lines/areas
3. No `auto-fill`/`auto-fit` implementation
4. Basic `minmax()` support but incomplete
5. No `fit-content()` support
6. Missing subgrid support
7. Simpler track sizing (doesn't fully implement CSS Grid spec phases)

---

## 3. Good Designs from Taffy to Adopt

### 3.1 CellOccupancyMatrix for Auto-Placement

**Why it's good:**
- Efficient collision detection during auto-placement
- Dynamically expandable in all directions
- Tracks both explicitly placed and auto-placed items
- Enables dense packing algorithm

**Recommendation for Radiant:**
```cpp
// Add to grid.hpp
struct CellOccupancyMatrix {
    enum State { UNOCCUPIED, DEFINITELY_PLACED, AUTO_PLACED };

    std::vector<State> cells;
    int rows, cols;
    TrackCounts row_counts, col_counts;

    void expand_to_fit(int row_start, int row_end, int col_start, int col_end);
    bool area_is_unoccupied(int row_start, int row_end, int col_start, int col_end);
    void mark_area(int row_start, int row_end, int col_start, int col_end, State);
};
```

### 3.2 Dual Coordinate System (Origin-Zero + CSS 1-Based)

**Why it's good:**
- Clean separation between CSS-facing API and internal algorithms
- Easier negative line handling
- Clearer mapping between explicit and implicit tracks

**Recommendation:**
```cpp
// Internal 0-based line representation
struct OriginZeroLine {
    int16_t value;
    int into_track_index(TrackCounts counts) const;
};

// Track counts for coordinate translation
struct TrackCounts {
    uint16_t negative_implicit;
    uint16_t explicit;
    uint16_t positive_implicit;

    uint16_t len() const { return negative_implicit + explicit + positive_implicit; }
    OriginZeroLine implicit_start_line() const;
    OriginZeroLine implicit_end_line() const;
};
```

### 3.3 Separated Min/Max Track Sizing Functions

**Why it's good:**
- Direct mapping to CSS `minmax(min, max)` syntax
- Enables proper intrinsic sizing behavior
- Cleaner `fit-content()` implementation

**Taffy's approach:**
```rust
struct GridTrack {
    min_track_sizing_function: MinTrackSizingFunction,
    max_track_sizing_function: MaxTrackSizingFunction,
}
```

**Recommendation for Radiant:**
```cpp
// Replace current GridTrackSize with
struct TrackSizingFunction {
    enum Type { LENGTH, PERCENT, FR, AUTO, MIN_CONTENT, MAX_CONTENT, FIT_CONTENT };
    Type type;
    float value;  // Length, percent, fr value, or fit-content limit

    bool is_intrinsic() const;
    bool is_flexible() const;
    float definite_value(float parent_size) const;
};

struct GridTrackSizing {
    TrackSizingFunction min;
    TrackSizingFunction max;
};
```

### 3.4 Item Contribution Caching

**Why it's good:**
- Avoids redundant child layout during track sizing
- Critical for performance with many items
- Properly invalidated between sizing passes

**Recommendation:**
```cpp
// Add to GridItemProp or separate cache structure
struct GridItemCache {
    float min_content_width, min_content_height;
    float max_content_width, max_content_height;
    float minimum_contribution_width, minimum_contribution_height;
    bool width_valid, height_valid;

    void invalidate() { width_valid = height_valid = false; }
};
```

### 3.5 Scratch Values for Track Sizing

**Why it's good:**
- Avoids modifying base_size directly during distribution
- Enables correct "distribute beyond limits" behavior
- Clean flush operation after each phase

**Taffy's scratch values:**
```rust
item_incurred_increase: f32,       // Per-item contribution to track
base_size_planned_increase: f32,   // Pending addition to base_size
growth_limit_planned_increase: f32, // Pending addition to growth_limit
infinitely_growable: bool,         // For grow-beyond-limits
```

### 3.6 Gutters as Tracks

**Why it's good:**
- Uniform track handling (no special gutter logic)
- Gaps naturally participate in alignment
- Simpler offset calculation

**Recommendation:**
Instead of separate gap handling, interleave gutter tracks:
```
[gutter][track][gutter][track][gutter][track][gutter]
```
Where gutters are fixed-size tracks with the gap value.

---

## 4. Proposed Grid Enhancements for Radiant

### Phase 1: Core Infrastructure (Essential)

#### 4.1 Implement CellOccupancyMatrix

```cpp
// radiant/grid_occupancy.hpp
class CellOccupancyMatrix {
public:
    enum State : uint8_t {
        UNOCCUPIED = 0,
        DEFINITELY_PLACED = 1,
        AUTO_PLACED = 2
    };

private:
    std::vector<State> cells_;
    TrackCounts rows_;
    TrackCounts cols_;

public:
    CellOccupancyMatrix(TrackCounts cols, TrackCounts rows);

    void expand_to_fit(Line<OriginZeroLine> row_span, Line<OriginZeroLine> col_span);
    void mark_area(AbsoluteAxis primary, Line<OriginZeroLine> primary_span,
                   Line<OriginZeroLine> secondary_span, State state);
    bool line_area_is_unoccupied(AbsoluteAxis primary, Line<OriginZeroLine> primary_span,
                                  Line<OriginZeroLine> secondary_span) const;

    // For auto-placement cursor
    std::optional<OriginZeroLine> last_of_type(AbsoluteAxis axis,
                                                OriginZeroLine start_at, State kind) const;
};
```

#### 4.2 Refactor Track Sizing Functions

```cpp
// radiant/grid_track.hpp
struct MinTrackSizingFunction {
    enum Type : uint8_t {
        LENGTH, PERCENT, AUTO, MIN_CONTENT, MAX_CONTENT
    };
    Type type;
    float value;

    bool is_intrinsic() const { return type >= AUTO; }
    float definite_value(float parent_size) const;
};

struct MaxTrackSizingFunction {
    enum Type : uint8_t {
        LENGTH, PERCENT, AUTO, MIN_CONTENT, MAX_CONTENT, FIT_CONTENT, FR
    };
    Type type;
    float value;  // For FR: flex factor, for FIT_CONTENT: limit

    bool is_intrinsic() const;
    bool is_flexible() const { return type == FR; }
    float flex_factor() const { return type == FR ? value : 0.0f; }
};

struct GridTrack {
    GridTrackKind kind;  // TRACK or GUTTER
    MinTrackSizingFunction min_sizing;
    MaxTrackSizingFunction max_sizing;

    float offset;
    float base_size;
    float growth_limit;

    // Scratch values for track sizing
    float item_incurred_increase;
    float base_size_planned_increase;
    float growth_limit_planned_increase;
    bool infinitely_growable;
    bool is_collapsed;

    void flush_base_size_increases();
    void flush_growth_limit_increases(bool set_infinitely_growable);
};
```

#### 4.3 Implement Full Track Sizing Algorithm

Following CSS Grid spec §11.5-11.7:

```cpp
// radiant/grid_track_sizing.cpp
void track_sizing_algorithm(
    LayoutContext* lycon,
    AbstractAxis axis,
    float axis_min_size,
    float axis_max_size,
    AlignContent axis_alignment,
    Size<AvailableSpace> available_space,
    Size<std::optional<float>> inner_node_size,
    std::vector<GridTrack>& axis_tracks,
    std::vector<GridTrack>& other_axis_tracks,
    std::vector<GridItem>& items)
{
    // 11.4: Initialize track sizes
    initialize_track_sizes(axis_tracks, inner_node_size.get(axis));

    // 11.5: Resolve intrinsic track sizes
    resolve_intrinsic_track_sizes(lycon, axis, axis_tracks, other_axis_tracks,
                                  items, available_space, inner_node_size);

    // 11.6: Maximize tracks
    maximize_tracks(axis_tracks, inner_node_size.get(axis), available_space.get(axis));

    // 11.7: Expand flexible tracks
    expand_flexible_tracks(lycon, axis, axis_tracks, items, axis_min_size, axis_max_size,
                          available_space.get(axis), inner_node_size);

    // 11.8: Stretch auto tracks (if align-content: stretch)
    if (axis_alignment == AlignContent::STRETCH) {
        stretch_auto_tracks(axis_tracks, axis_min_size, available_space.get(axis));
    }
}
```

### Phase 2: Auto-Placement Algorithm

```cpp
// radiant/grid_placement.cpp
void place_grid_items(
    CellOccupancyMatrix& occupancy,
    std::vector<GridItem>& items,
    GridAutoFlow auto_flow,
    AlignItems align_items,
    AlignItems justify_items,
    const NamedLineResolver& resolver)
{
    AbsoluteAxis primary = auto_flow.primary_axis();
    AbsoluteAxis secondary = primary.other();

    // Step 1: Place definitely-positioned items
    for (auto& [index, node, placement, style] : in_flow_children()) {
        if (placement.horizontal.is_definite() && placement.vertical.is_definite()) {
            auto [row_span, col_span] = place_definite_item(placement, primary);
            record_placement(occupancy, items, node, index, style,
                           align_items, justify_items, primary,
                           row_span, col_span, State::DEFINITELY_PLACED);
        }
    }

    // Step 2: Place items with definite secondary axis
    for (auto& [index, node, placement, style] : in_flow_children()) {
        if (placement.get(secondary).is_definite() &&
            !placement.get(primary).is_definite()) {
            auto [primary_span, secondary_span] =
                place_definite_secondary_item(occupancy, placement, auto_flow);
            record_placement(...);
        }
    }

    // Step 3: Auto-place remaining items
    auto grid_position = grid_start_position();
    for (auto& [index, node, placement, style] : in_flow_children()) {
        if (!placement.get(secondary).is_definite()) {
            auto [primary_span, secondary_span] =
                place_indefinite_item(occupancy, placement, auto_flow, grid_position);
            record_placement(...);

            // Update cursor based on dense/sparse packing
            grid_position = auto_flow.is_dense()
                ? grid_start_position()
                : std::make_pair(primary_span.end, secondary_span.start);
        }
    }
}
```

### Phase 3: Named Lines and Areas

```cpp
// radiant/grid_named.hpp
struct GridTemplateArea {
    const char* name;
    uint16_t row_start, row_end;
    uint16_t col_start, col_end;
};

class NamedLineResolver {
    std::vector<GridTemplateArea> areas_;
    std::vector<std::pair<const char*, uint16_t>> col_names_;  // name, index
    std::vector<std::pair<const char*, uint16_t>> row_names_;
    uint16_t explicit_col_count_, explicit_row_count_;

public:
    Line<OriginZeroGridPlacement> resolve_column_names(
        const Line<GridPlacement>& placement) const;
    Line<OriginZeroGridPlacement> resolve_row_names(
        const Line<GridPlacement>& placement) const;

    uint16_t area_column_count() const;
    uint16_t area_row_count() const;
};
```

### Phase 4: auto-fill/auto-fit

```cpp
// radiant/grid_explicit.cpp
struct AutoRepeatResult {
    uint16_t repetition_count;
    uint16_t track_count;
};

AutoRepeatResult compute_auto_repetition(
    const GridContainerStyle& style,
    float available_space,
    AbstractAxis axis,
    const std::vector<TrackSizingFunction>& repeat_tracks)
{
    // Sum fixed-size tracks outside repeat
    float fixed_sum = sum_fixed_tracks_outside_repeat(style, axis);

    // Calculate repeat track contribution
    float repeat_sum = sum_repeat_track_sizes(repeat_tracks, available_space);

    // Available for repetition
    float space_for_repeat = available_space - fixed_sum;

    // Calculate repetition count
    uint16_t count = std::max(1u, (uint16_t)(space_for_repeat / repeat_sum));

    return { count, (uint16_t)repeat_tracks.size() };
}
```

### Phase 5: Alignment Implementation

```cpp
// radiant/grid_alignment.cpp
void align_tracks(
    float container_size,
    Rect<float> padding_border,
    std::vector<GridTrack>& tracks,
    AlignContent alignment)
{
    float used_size = 0;
    for (const auto& track : tracks) {
        used_size += track.base_size;
    }
    float free_space = container_size - used_size;
    float origin = padding_border.start;

    int num_tracks = count_non_collapsed_tracks(tracks);

    // Apply alignment fallback for negative free space
    auto effective_alignment = apply_alignment_fallback(free_space, num_tracks, alignment);

    float offset = origin;
    for (size_t i = 0; i < tracks.size(); i++) {
        bool is_gutter = (i % 2 == 0);
        bool is_first = (i == 1);

        float alignment_offset = is_gutter ? 0.0f
            : compute_alignment_offset(free_space, num_tracks, 0, effective_alignment,
                                       false, is_first);

        tracks[i].offset = offset + alignment_offset;
        offset = tracks[i].offset + tracks[i].base_size;
    }
}
```

---

## 5. Implementation Roadmap

### Milestone 1: Foundation (2-3 weeks) ✅ COMPLETE
- [x] Implement `CellOccupancyMatrix`
- [x] Refactor `GridTrack` with min/max sizing functions
- [x] Add `TrackCounts` and `OriginZeroLine` coordinate types
- [x] Implement scratch values for track sizing
- [x] Add item contribution caching

### Milestone 2: Track Sizing (2-3 weeks) ✅ COMPLETE
- [x] Implement full §11.5 intrinsic track sizing
- [x] Implement §11.6 maximize tracks
- [x] Implement §11.7 expand flexible tracks
- [x] Implement §11.8 stretch auto tracks
- [x] Add fit-content() parsing and track sizing
- [ ] Support percentage re-resolution

### Milestone 3: Placement (2 weeks) ✅ COMPLETE
- [x] Implement 3-step auto-placement algorithm
- [x] Add dense packing support
- [x] Support `grid-auto-flow: row dense | column dense`

### Milestone 4: Named Features (1-2 weeks) 🔄 IN PROGRESS
- [ ] Implement `NamedLineResolver`
- [ ] Support `grid-template-areas`
- [ ] Support named line references in placements

### Milestone 5: Advanced (2-3 weeks) ✅ PARTIAL COMPLETE
- [x] Implement `auto-fill` / `auto-fit` (basic support)
- [x] Implement `fit-content()` (px and % arguments)
- [x] Add track alignment (`justify-content`, `align-content`)
- [x] Add item alignment (`justify-items`, `align-items`, `justify-self`, `align-self`)

### Milestone 6: Testing & Polish (ongoing)
- [x] Port basic grid test fixtures (76/290 passing, up from 65)
- [x] Compare against browser layout (baseline 953/953 passing)
- [ ] Performance optimization
- [x] Fix nested grid support
- [x] Fix relative coordinate system

### Additional Fixes Completed
- [x] Grid item relative positioning (Radiant coordinate system compliance)
- [x] Grid column flow intrinsic height calculation
- [x] CSS `order` property for grid items
- [x] `place-self` shorthand property
- [x] Union type tracking for flex/grid item properties

---

## 6. Conclusion

Taffy provides an excellent reference implementation for CSS Grid. The key architectural lessons are:

1. **Use a cell occupancy matrix** for efficient auto-placement
2. **Separate min/max track sizing functions** for cleaner intrinsic sizing
3. **Use scratch values** in track sizing to avoid state corruption
4. **Cache item contributions** for performance
5. **Treat gutters as tracks** for uniform handling
6. **Dual coordinate system** (CSS 1-based vs internal 0-based) for cleaner algorithms

By following these patterns, Radiant's grid implementation can achieve full CSS Grid Level 1 compliance while maintaining good performance.
