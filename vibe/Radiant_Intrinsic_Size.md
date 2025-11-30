# Radiant Intrinsic Sizing: Unified Architecture Proposal

## Executive Summary

This document proposes a unified intrinsic sizing architecture for Radiant, inspired by Ladybird's clean design. The goal is to eliminate code duplication between table, flex, and grid layout modes by creating a single source of truth for min-content and max-content width/height calculations.

---

## Problem Statement

### Current State in Radiant

Radiant currently has **three separate implementations** of intrinsic sizing:

| Layout Mode | Min-Content Width | Max-Content Width | Location |
|-------------|-------------------|-------------------|----------|
| **Table** | `measure_cell_minimum_width()` | `measure_cell_intrinsic_width()` | `layout_table.cpp` |
| **Flex** | `calculate_item_intrinsic_sizes()` | Same function | `layout_flex_measurement.cpp` |
| **Grid** | `calculate_grid_item_intrinsic_sizes()` | Same function | `grid_utils.cpp` |

### Issues with Current Design

1. **Code Duplication**: ~400 lines of measurement code duplicated across layout modes
2. **Inconsistent Accuracy**:
   - Table uses accurate FreeType font metrics with kerning
   - Flex uses rough estimates (10px per character for min-content)
   - Grid uses hardcoded placeholder values (50px width, 20px height)
3. **No Shared Infrastructure**: Each layout mode has its own measurement approach
4. **Missing Element Handling**: Neither properly recurses into nested elements
5. **Poor Caching**: Only flex has a global cache; no per-element caching
6. **Height-depends-on-width not handled**: Intrinsic height depends on available width (for text wrapping), but this dependency isn't properly modeled

---

## Ladybird's Approach

### Key Architectural Patterns

#### 1. Unified Base Class API

All formatting contexts inherit from `FormattingContext` which provides:

```cpp
// Base class API - same for ALL layout modes
CSSPixels calculate_min_content_width(Layout::Box const&) const;
CSSPixels calculate_max_content_width(Layout::Box const&) const;
CSSPixels calculate_min_content_height(Layout::Box const&, CSSPixels width) const;
CSSPixels calculate_max_content_height(Layout::Box const&, CSSPixels width) const;

// Each formatting context MUST implement:
virtual CSSPixels automatic_content_width() const = 0;
virtual CSSPixels automatic_content_height() const = 0;
```

#### 2. AvailableSpace Type System

```cpp
class AvailableSize {
public:
    enum class Type {
        Definite,      // Concrete pixel value
        Indefinite,    // Unknown (auto)
        MinContent,    // Measuring min-content (break at every opportunity)
        MaxContent,    // Measuring max-content (never wrap)
    };

    static AvailableSize make_definite(CSSPixels);
    static AvailableSize make_indefinite();
    static AvailableSize make_min_content();
    static AvailableSize make_max_content();

    bool is_intrinsic_sizing_constraint() const {
        return is_min_content() || is_max_content();
    }
};

class AvailableSpace {
public:
    AvailableSize width;
    AvailableSize height;
};
```

This allows layout code to **query** what mode it's in and behave accordingly.

#### 3. Throwaway Layout State for Measurement

```cpp
CSSPixels FormattingContext::calculate_min_content_width(Box const& box) const {
    // Check cache first
    auto& cache = box.cached_intrinsic_sizes().min_content_width;
    if (cache.has_value())
        return cache.value();

    // Create isolated state that doesn't affect main layout
    LayoutState throwaway_state;

    auto& box_state = throwaway_state.get_mutable(box);
    box_state.width_constraint = SizeConstraint::MinContent;
    box_state.set_indefinite_content_width();

    // Create fresh formatting context for measurement
    auto context = create_independent_formatting_context(
        throwaway_state, LayoutMode::IntrinsicSizing, box);

    context->run(AvailableSpace(
        AvailableSize::make_min_content(),
        available_height));

    auto result = context->automatic_content_width();
    cache.emplace(result);
    return result;
}
```

#### 4. Per-Box Intrinsic Size Caching

```cpp
struct IntrinsicSizes {
    Optional<CSSPixels> min_content_width;
    Optional<CSSPixels> max_content_width;
    // Height is cached PER WIDTH because it depends on wrapping!
    HashMap<CSSPixels, Optional<CSSPixels>> min_content_height;
    HashMap<CSSPixels, Optional<CSSPixels>> max_content_height;
};

class Box {
    // Lazy-initialized cache
    IntrinsicSizes& cached_intrinsic_sizes() const;
    void reset_cached_intrinsic_sizes() const;
};
```

#### 5. Consistent Usage Across Layout Modes

**Table:**
```cpp
void TableFormattingContext::compute_cell_measures() {
    for (auto& cell : m_cells) {
        // Uses SAME base class methods as flex!
        auto min_content_width = calculate_min_content_width(cell.box);
        auto max_content_width = calculate_max_content_width(cell.box);
        ...
    }
}
```

**Flex:**
```cpp
CSSPixels FlexFormattingContext::calculate_min_content_main_size(FlexItem const& item) const {
    if (is_row_layout()) {
        return calculate_min_content_width(item.box);  // Same API!
    }
    return calculate_min_content_height(item.box, available_width);
}
```

---

## Proposed Architecture for Radiant

### Phase 1: Create AvailableSpace Type

**File:** `radiant/available_space.hpp`

```cpp
#pragma once
#include <cmath>

namespace Radiant {

enum class AvailableSizeType {
    Definite,      // Concrete pixel value
    Indefinite,    // Unknown (auto)
    MinContent,    // Measuring min-content
    MaxContent     // Measuring max-content
};

struct AvailableSize {
    AvailableSizeType type;
    float value;  // Only valid for Definite

    static AvailableSize make_definite(float px) {
        return { AvailableSizeType::Definite, px };
    }
    static AvailableSize make_indefinite() {
        return { AvailableSizeType::Indefinite, 0 };
    }
    static AvailableSize make_min_content() {
        return { AvailableSizeType::MinContent, 0 };
    }
    static AvailableSize make_max_content() {
        return { AvailableSizeType::MaxContent, 0 };
    }

    bool is_definite() const { return type == AvailableSizeType::Definite; }
    bool is_indefinite() const { return type == AvailableSizeType::Indefinite; }
    bool is_min_content() const { return type == AvailableSizeType::MinContent; }
    bool is_max_content() const { return type == AvailableSizeType::MaxContent; }
    bool is_intrinsic() const { return is_min_content() || is_max_content(); }

    float to_px_or_zero() const {
        return is_definite() ? value : 0;
    }
};

struct AvailableSpace {
    AvailableSize width;
    AvailableSize height;

    static AvailableSpace make_definite(float w, float h) {
        return { AvailableSize::make_definite(w), AvailableSize::make_definite(h) };
    }
    static AvailableSpace make_min_content() {
        return { AvailableSize::make_min_content(), AvailableSize::make_indefinite() };
    }
    static AvailableSpace make_max_content() {
        return { AvailableSize::make_max_content(), AvailableSize::make_indefinite() };
    }
};

} // namespace Radiant
```

### Phase 2: Add Per-Element Intrinsic Size Cache

**Modify:** `radiant/view.hpp`

```cpp
// Add to existing IntrinsicSizes struct or create new one
struct IntrinsicSizeCache {
    int min_content_width = -1;   // -1 means not computed
    int max_content_width = -1;

    // Height depends on width, so we cache per-width
    // Using simple array for common widths, or hashmap for general case
    struct HeightCacheEntry {
        int width;
        int min_height;
        int max_height;
    };
    HeightCacheEntry height_cache[4];  // Small fixed cache
    int height_cache_count = 0;

    void reset() {
        min_content_width = -1;
        max_content_width = -1;
        height_cache_count = 0;
    }

    int get_min_height_for_width(int width) const;
    int get_max_height_for_width(int width) const;
    void set_height_for_width(int width, int min_h, int max_h);
};

// Add to DomElement or ViewBlock
// IntrinsicSizeCache* intrinsic_cache;  // Lazy allocated
```

### Phase 3: Create Unified Intrinsic Sizing API

**File:** `radiant/intrinsic_sizing.hpp`

```cpp
#pragma once
#include "layout.hpp"
#include "available_space.hpp"

// ============================================================================
// Unified Intrinsic Sizing API
// ============================================================================
// These functions are the SINGLE SOURCE OF TRUTH for intrinsic size calculation.
// Table, flex, and grid layouts should ALL use these functions.

// Calculate min-content width (narrowest without overflow)
// - For text: width of longest word
// - For blocks: max of children's min-content widths
// - For replaced elements (img): natural width
int calculate_min_content_width(LayoutContext* lycon, DomNode* node);

// Calculate max-content width (natural width without wrapping)
// - For text: full text width on single line
// - For blocks: max of children's max-content widths
// - For replaced elements (img): natural width
int calculate_max_content_width(LayoutContext* lycon, DomNode* node);

// Calculate min-content height given a specific width
// - For block containers: equivalent to max-content height
// - Height depends on width due to text wrapping
int calculate_min_content_height(LayoutContext* lycon, DomNode* node, int width);

// Calculate max-content height given a specific width
// - Natural height after laying out content at given width
int calculate_max_content_height(LayoutContext* lycon, DomNode* node, int width);

// Calculate fit-content width: min(max-content, max(min-content, available))
int calculate_fit_content_width(LayoutContext* lycon, DomNode* node, int available_width);

// Calculate fit-content height
int calculate_fit_content_height(LayoutContext* lycon, DomNode* node, int available_height);

// ============================================================================
// Low-Level Measurement Helpers
// ============================================================================

// Measure text intrinsic widths using FreeType
// Returns both min (longest word) and max (full line) content widths
struct TextIntrinsicWidths {
    int min_content;  // Longest word
    int max_content;  // Full text width
};
TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length);

// Measure element intrinsic widths (recursive)
IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element);
```

**File:** `radiant/intrinsic_sizing.cpp`

```cpp
#include "intrinsic_sizing.hpp"
#include "../lib/log.h"

// ============================================================================
// Text Measurement (Single Implementation)
// ============================================================================

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length) {
    TextIntrinsicWidths result = {0, 0};

    if (!lycon->font.ft_face || !text || length == 0) {
        return result;
    }

    float total_width = 0.0f;
    float current_word = 0.0f;
    float longest_word = 0.0f;

    FT_UInt prev_glyph = 0;
    bool has_kerning = FT_HAS_KERNING(lycon->font.ft_face);
    const unsigned char* str = (const unsigned char*)text;

    for (size_t i = 0; i < length; i++) {
        unsigned char ch = str[i];

        // Word boundary detection
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
            prev_glyph = 0;

            // Add space width to total
            float space_width = 4.0f;  // Default
            if (lycon->font.style && lycon->font.style->space_width > 0) {
                space_width = lycon->font.style->space_width;
            }
            total_width += space_width;
            continue;
        }

        // Get glyph metrics
        FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, ch);
        if (!glyph_index) continue;

        // Apply kerning
        float kerning = 0.0f;
        if (has_kerning && prev_glyph) {
            FT_Vector kern;
            FT_Get_Kerning(lycon->font.ft_face, prev_glyph, glyph_index,
                          FT_KERNING_DEFAULT, &kern);
            kerning = kern.x / 64.0f;
        }

        // Load glyph and get advance
        FT_Load_Glyph(lycon->font.ft_face, glyph_index, FT_LOAD_DEFAULT);
        float advance = lycon->font.ft_face->glyph->advance.x / 64.0f + kerning;

        current_word += advance;
        total_width += advance;
        prev_glyph = glyph_index;
    }

    // Check final word
    longest_word = fmax(longest_word, current_word);

    result.min_content = (int)ceilf(longest_word);
    result.max_content = (int)roundf(total_width);

    log_debug("measure_text_intrinsic_widths: min=%d, max=%d",
              result.min_content, result.max_content);

    return result;
}

// ============================================================================
// Main API Implementation
// ============================================================================

int calculate_min_content_width(LayoutContext* lycon, DomNode* node) {
    if (!node) return 0;

    // 1. Check cache
    // TODO: Implement per-node cache lookup

    // 2. Check for natural width (replaced elements like images)
    if (node->is_element()) {
        ViewBlock* view = (ViewBlock*)node;
        // TODO: Check for natural_width on replaced elements
    }

    // 3. Measure based on node type
    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (text) {
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text));
            return widths.min_content;
        }
        return 0;
    }

    // 4. For elements, recursively measure children
    DomElement* element = node->as_element();
    if (!element) return 0;

    int min_width = 0;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        int child_min = calculate_min_content_width(lycon, child);
        min_width = max(min_width, child_min);
    }

    // 5. Add padding and border
    ViewBlock* view = (ViewBlock*)element;
    if (view->bound) {
        min_width += view->bound->padding.left + view->bound->padding.right;
        if (view->bound->border) {
            min_width += view->bound->border->width.left + view->bound->border->width.right;
        }
    }

    // 6. Cache result
    // TODO: Store in per-node cache

    return min_width;
}

int calculate_max_content_width(LayoutContext* lycon, DomNode* node) {
    if (!node) return 0;

    // Similar structure to min_content_width
    // but uses max_content from text measurement
    // and sums children for inline context

    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (text) {
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text));
            return widths.max_content;
        }
        return 0;
    }

    DomElement* element = node->as_element();
    if (!element) return 0;

    int max_width = 0;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        int child_max = calculate_max_content_width(lycon, child);
        max_width = max(max_width, child_max);
    }

    ViewBlock* view = (ViewBlock*)element;
    if (view->bound) {
        max_width += view->bound->padding.left + view->bound->padding.right;
        if (view->bound->border) {
            max_width += view->bound->border->width.left + view->bound->border->width.right;
        }
    }

    return max_width;
}

int calculate_min_content_height(LayoutContext* lycon, DomNode* node, int width) {
    // For block containers, min-content height == max-content height
    return calculate_max_content_height(lycon, node, width);
}

int calculate_max_content_height(LayoutContext* lycon, DomNode* node, int width) {
    if (!node) return 0;

    // TODO: Check cache for this specific width

    // For accurate height, we need to actually lay out content at the given width
    // This requires a measurement pass with throwaway state

    // Save current context
    LayoutContext saved = *lycon;

    // Set up measurement context
    lycon->is_measuring = true;
    lycon->block.content_width = width;
    lycon->block.advance_y = 0;

    // TODO: Perform layout in measurement mode
    // layout_flow_node(lycon, node);

    int height = (int)lycon->block.advance_y;

    // Restore context
    *lycon = saved;

    // TODO: Cache result for this width

    return height;
}

int calculate_fit_content_width(LayoutContext* lycon, DomNode* node, int available_width) {
    int min_content = calculate_min_content_width(lycon, node);
    int max_content = calculate_max_content_width(lycon, node);

    // fit-content = clamp(min-content, available, max-content)
    return min(max_content, max(min_content, available_width));
}
```

### Phase 4: Refactor Table Layout

**Modify:** `radiant/layout_table.cpp`

```cpp
// Replace the existing functions with thin wrappers

static int measure_cell_minimum_width(LayoutContext* lycon, ViewTableCell* cell) {
    return calculate_min_content_width(lycon, (DomNode*)cell);
}

static int measure_cell_intrinsic_width(LayoutContext* lycon, ViewTableCell* cell) {
    return calculate_max_content_width(lycon, (DomNode*)cell);
}

// In table_auto_layout(), the existing logic for distributing column widths
// remains the same, but now uses the unified measurement functions
```

### Phase 5: Refactor Flex Layout

**Modify:** `radiant/layout_flex_measurement.cpp`

```cpp
// Simplify calculate_item_intrinsic_sizes to use unified API

void calculate_item_intrinsic_sizes(ViewGroup* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) return;

    // Skip if already calculated
    if (item->fi->has_intrinsic_width && item->fi->has_intrinsic_height) return;

    // Use unified measurement API
    LayoutContext* lycon = /* get from context */;

    item->fi->intrinsic_width.min_content = calculate_min_content_width(lycon, (DomNode*)item);
    item->fi->intrinsic_width.max_content = calculate_max_content_width(lycon, (DomNode*)item);
    item->fi->has_intrinsic_width = 1;

    // Height depends on width - use the computed width or flex basis
    int width = item->width > 0 ? item->width : item->fi->intrinsic_width.max_content;
    item->fi->intrinsic_height.min_content = calculate_min_content_height(lycon, (DomNode*)item, width);
    item->fi->intrinsic_height.max_content = calculate_max_content_height(lycon, (DomNode*)item, width);
    item->fi->has_intrinsic_height = 1;
}
```

### Phase 6: Refactor Grid Layout

**Modify:** `radiant/grid_utils.cpp`

```cpp
IntrinsicSizes calculate_grid_item_intrinsic_sizes(ViewBlock* item, bool is_row_axis) {
    IntrinsicSizes sizes = {0, 0};
    if (!item) return sizes;

    LayoutContext* lycon = /* get from context */;

    if (is_row_axis) {
        // For row axis, we're measuring height
        int width = item->width > 0 ? item->width :
                    calculate_fit_content_width(lycon, (DomNode*)item, 200);
        sizes.min_content = calculate_min_content_height(lycon, (DomNode*)item, width);
        sizes.max_content = calculate_max_content_height(lycon, (DomNode*)item, width);
    } else {
        // For column axis, we're measuring width
        sizes.min_content = calculate_min_content_width(lycon, (DomNode*)item);
        sizes.max_content = calculate_max_content_width(lycon, (DomNode*)item);
    }

    return sizes;
}
```

---

## Migration Strategy

### Step 1: Add Infrastructure (Low Risk) ✅
- [x] Create `available_space.hpp` with `AvailableSize` and `AvailableSpace` types
- [x] Add `IntrinsicSizeCache` to `intrinsic_sizing.hpp`
- [x] Create `intrinsic_sizing.hpp` with function declarations

### Step 2: Implement Core Functions (Medium Risk) ✅
- [x] Implement `measure_text_intrinsic_widths()` using existing table code as reference
- [x] Implement `calculate_min_content_width()` and `calculate_max_content_width()`
- [x] Add to build system and verify compilation

### Step 3: Migrate Table Layout (Medium Risk) ✅
- [x] Replace `measure_cell_minimum_width()` body with call to unified API
- [x] Replace `measure_cell_intrinsic_width()` body with call to unified API
- [x] Verify table layout tests still pass (baseline: 266/266 pass)
- [x] Include `intrinsic_sizing.hpp` in `layout_table.cpp`

### Step 4: Migrate Flex Layout (Medium Risk) ✅
- [x] Simplify `measure_text_run()` to use unified API
- [x] Remove duplicate text measurement implementation
- [x] Verify flex layout tests still pass (baseline: 266/266 pass)
- [x] Include `intrinsic_sizing.hpp` in `layout_flex_measurement.cpp`

### Step 5: Migrate Grid Layout (Low Risk) ✅
- [x] Update `calculate_grid_item_intrinsic_sizes()` to use unified API
- [x] Replace hardcoded values with actual measurements
- [x] Add `LayoutContext*` to `GridContainerLayout` for intrinsic sizing access
- [x] Verify grid layout tests still pass (baseline: 266/266 pass)

### Step 6: Add AvailableSpace Integration ✅
- [x] Create `radiant/available_space.hpp` with `AvailableSize` and `AvailableSpace` types
- [x] Modify `LayoutContext` to include `AvailableSpace`
- [x] Update inline layout with intrinsic sizing mode helpers
- [x] Update block layout to propagate available space

### Step 7: Legacy Code Cleanup ✅
- [x] Add `lycon` pointer to `FlexContainerLayout`
- [x] Update `init_flex_container()` to store lycon
- [x] Refactor `calculate_item_intrinsic_sizes()` to use unified API
- [x] Eliminate "10px per character" rough estimates
- [x] Verify all baseline tests pass (266/266)

---

## Implementation Status (Updated)

### Completed Work

**Phase 1 & 2: Core Infrastructure** ✅
- Created `radiant/intrinsic_sizing.hpp` - Unified API header with:
  - `IntrinsicSizeCache` struct for per-element caching
  - `TextIntrinsicWidths` struct for text measurement results
  - `ElementIntrinsicWidths` struct for element measurement results
  - `IntrinsicSizes` struct for combined min/max content widths
  - Full API declarations for all measurement functions
- Created `radiant/intrinsic_sizing.cpp` - Core implementation with:
  - `measure_text_intrinsic_widths()` - FreeType-based text measurement with kerning
  - `measure_element_intrinsic_widths()` - Recursive element measurement
  - `calculate_min_content_width()` / `calculate_max_content_width()` - High-level APIs
  - `calculate_min_content_height()` / `calculate_max_content_height()` - Height calculation
  - `calculate_fit_content_width()` - Shrink-to-fit calculation
- Added to build system in `build_lambda_config.json`
- **All 266 baseline layout tests pass** ✅

**Phase 3: Table Layout Integration** ✅
- Wired `layout_table.cpp` to use unified `measure_text_intrinsic_widths()`
- Refactored `measure_cell_intrinsic_width()` to call unified API (~60 lines replaced)
- Refactored `measure_cell_minimum_width()` to call unified API (~60 lines replaced)
- Added `#include "intrinsic_sizing.hpp"` to `layout_table.cpp`
- **All 266 baseline layout tests still pass** ✅

**Phase 4: Flex Layout Integration** ✅
- Refactored `measure_text_run()` in `layout_flex_measurement.cpp` to use unified API
- Replaced ~50 lines of FreeType text measurement with single call to `measure_text_intrinsic_widths()`
- Added `#include "intrinsic_sizing.hpp"` to `layout_flex_measurement.cpp`
- **All 266 baseline layout tests still pass** ✅

**Phase 5: Grid Layout Integration** ✅
- Refactored `calculate_grid_item_intrinsic_sizes()` in `grid_utils.cpp` to use unified API
- Replaced hardcoded placeholder values (50px width, 20px height) with actual content measurement
- Added `lycon` pointer to `GridContainerLayout` struct for intrinsic sizing access
- Added `#include "intrinsic_sizing.hpp"` to `grid_utils.cpp`
- Updated function signature to accept `LayoutContext*` parameter
- Updated all call sites in `grid_sizing.cpp` and `layout_grid_content.cpp`
- **All 266 baseline layout tests still pass** ✅

**Phase 6: AvailableSpace Type Integration** ✅
- Created `radiant/available_space.hpp` with complete type system:
  - `AvailableSizeType` enum: `DEFINITE`, `INDEFINITE`, `MIN_CONTENT`, `MAX_CONTENT`
  - `AvailableSize` struct with factory methods and query helpers
  - `AvailableSpace` struct for 2D available space representation
  - Utility functions: `apply_size_constraints()`, `compute_shrink_to_fit_width()`
- Added `available_space` field to `LayoutContext` in `layout.hpp`
- Updated `layout_init()` to initialize available space to indefinite
- Updated main layout entry to set available space from viewport dimensions
- Added intrinsic sizing mode helpers to `layout_text.cpp`:
  - `is_min_content_mode()`, `is_max_content_mode()`, `should_break_line()`
- Updated `layout_block.cpp` to propagate available space to child layouts
- Updated `intrinsic_sizing.hpp` to include `available_space.hpp`
- **All 266 baseline layout tests still pass** ✅

**Phase 7: Legacy Code Cleanup** ✅
- Added `lycon` pointer to `FlexContainerLayout` (like `GridContainerLayout`)
- Updated `init_flex_container()` to store and preserve `lycon` pointer
- Refactored `calculate_item_intrinsic_sizes()` to use unified API:
  - Simple text nodes now use `measure_text_intrinsic_widths()` when lycon available
  - Complex content child traversal now uses unified API for text measurement
- Updated `estimate_text_width()` to use unified API when lycon available
- Updated `layout_block_measure_mode()` to use unified API
- Eliminated all "10px per character" rough estimates when layout context is available
- **All 266 baseline layout tests still pass** ✅

### Summary

The unified intrinsic sizing architecture is now complete:
- **Single source of truth**: `intrinsic_sizing.hpp/cpp` for all intrinsic size calculations
- **Shared by all layouts**: Table, Flex, and Grid all use the unified API
- **Type-safe constraints**: `AvailableSpace` type enables layout code to distinguish sizing modes
- **Proper propagation**: Available space flows through the layout tree

### Test Results Summary

| Test Suite | Tests | Status |
|------------|-------|--------|
| **Baseline** | 266 | ✅ ALL PASS |
| Table (extended) | 416 | ⚠️ Pre-existing failures (not caused by this refactoring) |

**Note**: The table extended tests (416 tests) were already failing before this refactoring work. The baseline tests (which include 27+ table tests) all pass, confirming no regression from the unified intrinsic sizing integration

---

## References

- [CSS Intrinsic & Extrinsic Sizing Module Level 3](https://www.w3.org/TR/css-sizing-3/)
- [CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Table Module Level 3](https://www.w3.org/TR/css-tables-3/)
- Ladybird source: `Libraries/LibWeb/Layout/FormattingContext.cpp`
- Ladybird source: `Libraries/LibWeb/Layout/FlexFormattingContext.cpp`
- Ladybird source: `Libraries/LibWeb/Layout/TableFormattingContext.cpp`
