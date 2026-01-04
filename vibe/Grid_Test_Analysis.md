# Grid Layout Test Analysis - Structural Enhancement Proposal

## Test Results Summary

**Current Status:** 5/171 grid tests passing (2.9%) - *Updated Jan 4, 2026*

**Progress:** 26 tests now have 100% element match (text failures due to Ahem font not loading)

### Failure Pattern Distribution

| Element Match % | Text Match % | Count | Primary Issue Category |
|-----------------|--------------|-------|------------------------|
| 100% | 100% | 5 | ✅ PASSING |
| 100% | 0% | 21 | Text measurement only (Ahem font) |
| 91.7% | 100% | 12 | Minor positioning errors |
| 50% | 0-100% | 38 | Track sizing / placement |
| 33.3% | 0% | 24 | Intrinsic sizing failures |
| <30% | various | ~15 | Multiple compound issues |

---

## Issue Categories Analysis

### Category 1: Grid Template Areas ✅ **FIXED**

**Status:** Working correctly - 100% element match on `grid_005_template_areas`

**Tests Affected:** `grid_005_template_areas`, `grid_020_complex_areas`

**Resolution:** The `parse_grid_template_areas()` function was re-enabled with heap allocation to avoid stack overflow. Named areas now correctly resolve to grid positions.

---

### Category 2: Negative Line Numbers 🟡 **PARTIALLY FIXED**

**Status:** Infrastructure added, edge cases remain for grids without explicit templates

**Affected Tests:** `grid_119_negative_lines`, all tests with `grid-column: 1 / -1` syntax

**Changes Made (Jan 4, 2026):**
1. Added `has_negative_start` flag to `GridPlacement` struct
2. Added `FromNegativeLines()` factory method for `-N / -M` patterns
3. Updated `extract_grid_item_info()` to detect both-negative case
4. Updated `resolve_negative_lines_in_items()` to resolve both start and end
5. Fixed `calculate_initial_grid_extent()` to count items with negative end but positive start

**Remaining Issue:** CSS spec says negative lines reference the **explicit** grid end. With 0 explicit columns, -1 = line 1. Further edge case handling needed for grids without explicit templates.

---

### Category 3: Intrinsic Track Sizing Issues 🟡 **BLOCKED**

**Affected Tests:** All `grid_min_content_*`, `grid_max_content_*`, `grid_fit_content_*`, `grid_available_space_*` (60+ tests)

**Symptoms:**
- Track widths off by 9px (e.g., `grid_min_content_single_item`: 109px vs 100px)
- Text wrapping differences causing size variations

**Root Cause Analysis:**
1. **Font loading failure**: Tests use Ahem font embedded as base64 data URL. Radiant's font loader treats this as a file path instead of parsing the data URL, falling back to Helvetica.
2. **Measurement difference**: With fallback font, widths are 9px different from expected Ahem measurements.

**Evidence from logs:**
```
[WARN] Failed to load @font-face file: .../data:application/font-woff2
[INFO] Using fallback font: Helvetica for requested font: ahem
```

**Note:** Zero-width space (`U+200B`) handling IS working correctly in `measure_text_intrinsic_widths()`. The issue is purely font loading.

**Fix Required:**
1. Implement data URL parsing in font loader
2. Decode base64 font data and load directly into FreeType

---

### Category 4: Align-Content with Negative Free Space ✅ **FIXED**

**Status:** Fixed - all 5 negative space gap tests now pass

**Tests Now Passing:**
- `grid_align_content_center_negative_space_gap`
- `grid_align_content_end_negative_space_gap`
- `grid_justify_content_center_negative_space_gap`
- `grid_justify_content_end_negative_space_gap`
- `grid_justify_content_start_negative_space_gap`

**Changes Made (Jan 4, 2026):**
In `grid_positioning.cpp`:
1. Changed condition from `extra_space > 0` to always compute alignment when `count > 0`
2. Space distribution (space-between/around/evenly) correctly falls back to `start` with negative space
3. Center/end alignment now correctly computes negative offset for overflow centering

---

### Category 5: Absolute Positioning in Grid Context 🟡 **MEDIUM**

**Affected Tests:** All `grid_absolute_*` tests (20+ tests, mostly 50-91.7% match)

**Symptoms:**
- Inset resolution against grid lines incorrect
- `grid-column: 1 / 2` for absolute items not respected

**Evidence:**
```
grid_absolute_resolved_insets:
  Item with left:auto, right:auto: Radiant (35,35) vs Browser (20,20)
  Item with insets stretching: Radiant 130×130 vs Browser 160×160
```

**Root Cause:** Absolute positioned items inside grid:
1. `auto` insets should resolve to grid area edges
2. Stretching should use grid area as containing block
3. Current code may not pass grid area to absolute positioning logic

**Fix Required:**
1. When laying out absolute children of grid, set containing block to resolved grid area
2. Handle `grid-column`/`grid-row` on position:absolute items

---

### Category 6: Text Measurement Differences 🟢 **LOW** (Cosmetic)

**Affected Tests:** Most tests with "Text 0%"

**Symptoms:**
- Text widths consistently ~6px wider in Radiant
- Example: "Item 1" = Radiant `44.5×18` vs Browser `38.91×16`

**Root Cause:** Different font rendering/metrics between:
- Radiant (FreeType) vs Browser (platform native)
- Font size interpretation differences

**Impact:** Low - element positions are correct, only text box metrics differ

**Fix Options:**
1. Increase text tolerance in test harness (currently 5.2px)
2. Use test-specific reference fonts
3. Accept as platform difference

---

## Recommended Implementation Order

### Phase 1: Critical Fixes ✅ **COMPLETED**

1. **Grid-template-areas parsing** ✅ DONE
   - Fixed stack overflow with heap allocation
   - ~20 area-based tests now have correct element positions

2. **Negative line number resolution** ✅ PARTIALLY DONE
   - Added `has_negative_start` flag and `FromNegativeLines()` factory
   - Works for grids with explicit templates
   - Edge cases remain for implicit-only grids

3. **Negative free space handling** ✅ DONE
   - Fixed alignment functions for overflow cases
   - 5 new tests passing

### Phase 2: Track Sizing Accuracy 🟡 **IN PROGRESS**

3. **Data URL font loading** (~2-3 days)
   - Parse `data:` URLs in font loader
   - Decode base64 and load into FreeType
   - Will unblock ~60 intrinsic sizing tests

4. **Intrinsic sizing calibration** (after font fix)
   - Verify measurements with Ahem font
   - Should auto-resolve once fonts load correctly

### Phase 3: Advanced Features 🔴 **NOT STARTED**

5. **Absolute positioning in grid** (~3-4 days)
   - Pass grid area as containing block
   - Handle grid placement on absolute items

6. **Text measurement calibration** (~1 day)
   - Adjust tolerances or use platform-aware references

---

## Structural Enhancements Proposal

### Enhancement A: Separate Explicit vs Computed Grid Counts

**Current State:**
```cpp
struct GridContainerLayout {
    int explicit_row_count;      // From grid-template-rows
    int explicit_column_count;   // From grid-template-columns
    int computed_row_count;      // Used inconsistently
    int computed_column_count;   // Mixed explicit + implicit
};
```

**Proposed:**
```cpp
struct GridContainerLayout {
    // Explicit grid (from template definitions)
    int explicit_row_count;
    int explicit_column_count;

    // Implicit grid extensions (items placed beyond explicit)
    int implicit_rows_before;    // Rows before explicit grid
    int implicit_rows_after;     // Rows after explicit grid
    int implicit_cols_before;    // Columns before explicit grid
    int implicit_cols_after;     // Columns after explicit grid

    // Total grid size
    int total_row_count() { return implicit_rows_before + explicit_row_count + implicit_rows_after; }
    int total_column_count() { return implicit_cols_before + explicit_column_count + implicit_cols_after; }
};
```

**Benefit:** Clear separation for negative line resolution and auto-placement

### Enhancement B: GridLine Type for Line Resolution

**Current State:** Integers with magic values and `_is_span` flags

**Proposed:**
```cpp
enum class GridLineType {
    Auto,           // auto
    Line,           // explicit line number (positive or negative)
    Span,           // span N
    Named           // named line reference
};

struct GridLine {
    GridLineType type;
    int16_t value;          // Line number, span count, or name index

    // Resolution methods
    int16_t resolve_to_line(int explicit_count, int total_count) const;
    bool is_definite() const { return type == GridLineType::Line; }
};

struct GridItemProp {
    GridLine row_start, row_end;
    GridLine col_start, col_end;
    // ... rest unchanged
};
```

**Benefit:** No ambiguity between span vs negative line, cleaner resolution code

### Enhancement C: Named Area Registry

**Current State:** Linear search through `grid_areas[]` for each lookup

**Proposed:**
```cpp
struct GridAreaRegistry {
    HashMap<const char*, GridArea> areas;  // O(1) lookup by name

    void define_area(const char* name, int row_start, int row_end,
                     int col_start, int col_end);
    bool resolve_placement(const char* area_name,
                          int* row_start, int* row_end,
                          int* col_start, int* col_end);
};
```

**Benefit:** Faster area lookup, cleaner separation of concerns

### Enhancement D: TrackSizingContext for Unified Algorithm

**Current State:** Multiple track sizing functions with varying parameters

**Proposed:** Already partially implemented in `grid_sizing_algorithm.hpp`, extend usage:

```cpp
struct TrackSizingContext {
    std::vector<EnhancedGridTrack>* axis_tracks;
    float axis_available_space;
    float axis_inner_size;
    float gap;

    // NEW: Negative space handling
    bool has_negative_free_space;
    float collapsed_gap;  // Gap after collapse for overflow

    // Methods
    float total_track_size() const;
    float free_space() const;
    void collapse_gaps_if_needed();
};
```

**Benefit:** Centralized handling of edge cases like negative free space

---

## Testing Strategy

### Unit Tests to Add

1. **Negative Line Resolution Tests**
   ```cpp
   TEST(GridLineResolution, NegativeLineInExplicitGrid) {
       // -1 in 3-column explicit grid = line 4
       EXPECT_EQ(resolve_negative_line(-1, 3), 4);
       // -2 in 3-column explicit grid = line 3
       EXPECT_EQ(resolve_negative_line(-2, 3), 3);
   }
   ```

2. **Template Areas Parsing Tests**
   ```cpp
   TEST(GridAreas, ParseSimpleAreas) {
       GridProp grid;
       parse_grid_template_areas(&grid, "\"header header\" \"sidebar main\"");
       EXPECT_EQ(grid.area_count, 3);
       EXPECT_EQ(find_area(&grid, "header").column_end, 3);
   }
   ```

3. **Intrinsic Size with Break Opportunities**
   ```cpp
   TEST(IntrinsicSizing, ZeroWidthSpace) {
       // "HH<U+200B>HH" should break at ZWSP
       float min_content = measure_min_content("HH\u200BHH");
       // Should be width of "HH", not "HHHH"
       EXPECT_LT(min_content, measure_max_content("HHHH"));
   }
   ```

### Regression Test Categories

After fixes, these test groups should pass:

| Category | Test Pattern | Expected Pass Rate |
|----------|--------------|-------------------|
| Template Areas | `grid_*_template_areas*`, `grid_*_complex_areas*` | 100% |
| Negative Lines | `grid_*_negative_lines*`, tests with `1 / -1` | 100% |
| Min-content | `grid_min_content_*` | >90% |
| Alignment | `grid_align_*` | 100% |
| Absolute | `grid_absolute_*` | >80% |

---

## Estimated Impact

Current progress and projections:

| Milestone | Tests Passing | Element 100% | Notes |
|-----------|---------------|--------------|-------|
| Initial | 1/171 (0.6%) | ~17 | Before fixes |
| **Current** | **5/171 (2.9%)** | **26** | After Phase 1 |
| After font fix | ~30/171 (18%) | ~80 | Intrinsic tests unblocked |
| After absolute fix | ~50/171 (29%) | ~100 | Grid positioning complete |
| Full completion | ~160/171 (94%) | ~165 | Platform text differences remain |

The remaining ~6% would be text measurement differences that may need platform-specific tolerance adjustments.

---

## Change Log

### January 4, 2026
- **Category 1 (Template Areas):** Confirmed working, 100% element match
- **Category 2 (Negative Lines):** Added `has_negative_start`, `FromNegativeLines()`, updated resolution logic
- **Category 4 (Negative Space):** Fixed in `grid_positioning.cpp`, 5 tests now passing
- **Category 3 (Intrinsic Sizing):** Identified as font loading issue, not algorithm issue
- **Baseline tests:** 1665/1665 (100%) maintained
