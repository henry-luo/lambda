# Lambda CSS Implementation Progress

**Last Updated**: January 10, 2025

## Overview

Lambda CSS is a parallel CSS implementation to Lexbor for the Lambda HTML parser. This document tracks the incremental implementation of CSS property resolution, currently at **12. **Group 20: Trans3. **Group 21: T4. **Group 22: Filte5. **Group 23: Advanced Layout** (8 properties)
   - `contain` (ID: 290) - Containment types (layout, paint, size, style)
   - `content-visibility` (ID: 291) - Keywords: visible, hidden, auto
   - `aspect-ratio` (ID: 292) - Width/height ratio values
   - `object-fit` (ID: 293) - Keywords: fill, contain, cover, scale-down, none
   - `object-position` (ID: 294) - Position values for replaced elements
   - `resize` (ID: 295) - Keywords: none, both, horizontal, vertical
   - `scroll-behavior` (ID: 296) - Keywords: auto, smooth
   - `overscroll-behavior` (ID: 297) - Keywords: auto, contain, none

6. **Group 24: CSS Custom Properties and Variables** (4 properties)ts** (8 properties)
   - `filter` (ID: 280) - Filter functions (blur, brightness, contrast, etc.)
   - `backdrop-filter` (ID: 281) - Backdrop filter effects
   - `mix-blend-mode` (ID: 282) - Element blending modes
   - `isolation` (ID: 283) - Keywords: auto, isolate
   - `box-shadow` (ID: 284) - Box shadow effects
   - `drop-shadow` (filter function) - Drop shadow filter
   - `blur` (filter function) - Blur filter
   - `brightness` (filter function) - Brightness filter

5. **Group 23: Advanced Layout** (8 properties)roperties** (6 properties)
   - `transition` (ID: 270) - Shorthand for all transition properties
   - `transition-property` (ID: 271) - CSS properties to transition
   - `transition-duration` (ID: 272) - Transition duration (s, ms)
   - `transition-timing-function` (ID: 273) - Keywords: ease, linear, cubic-bezier
   - `transition-delay` (ID: 274) - Transition delay (s, ms)
   - `transition-behavior` (ID: 275) - Keywords: normal, allow-discrete

4. **Group 22: Filter and Effects** (8 properties)erties** (8 properties)
   - `transform` (ID: 250) - Transform functions (translate, rotate, scale, etc.)
   - `transform-origin` (ID: 251) - Transform origin point
   - `transform-style` (ID: 252) - Keywords: flat, preserve-3d
   - `perspective` (ID: 253) - 3D perspective distance
   - `perspective-origin` (ID: 254) - 3D perspective origin
   - `backface-visibility` (ID: 255) - Keywords: visible, hidden
   - `transform-box` (ID: 256) - Keywords: content-box, border-box, fill-box
   - `will-change` (ID: 257) - Performance optimization hints

3. **Group 21: Transition Properties** (6 properties)50+ properties supported**.

## Current Status

### ✅ Implemented Properties: 103/150+

**Group 1: Core Typography & Color** (5 properties) ✅
- `color` (ID: 96) - Color values with keyword mapping
- `font-size` (ID: 81) - Length values (px, em, rem, %) with keyword support (xx-small → xx-large)
- `font-weight` (ID: 82) - Numeric (100-900) and keyword (normal, bold, bolder, lighter)
- `font-family` (ID: 80) - Font name mapping (Arial, serif, sans-serif, monospace, etc.)
- `line-height` (ID: 83) - Length values and unitless numbers

**Group 2: Box Model Basics** (10 properties) ✅
- `width` (ID: 16) - Length/percentage values, stored in `BlockProp->given_width`
- `height` (ID: 17) - Length/percentage values, stored in `BlockProp->given_height`
- `margin-top` (ID: 24) - Stored in `BoundaryProp->margin.top`
- `margin-right` (ID: 25) - Stored in `BoundaryProp->margin.right`
- `margin-bottom` (ID: 26) - Stored in `BoundaryProp->margin.bottom`
- `margin-left` (ID: 27) - Stored in `BoundaryProp->margin.left`
- `padding-top` (ID: 35) - Stored in `BoundaryProp->padding.top`
- `padding-right` (ID: 36) - Stored in `BoundaryProp->padding.right`
- `padding-bottom` (ID: 37) - Stored in `BoundaryProp->padding.bottom`
- `padding-left` (ID: 38) - Stored in `BoundaryProp->padding.left`

**Group 3: Background & Borders** (13 properties) ✅
- `background-color` (ID: 71) - Color values, stored in `BoundaryProp->background`
- `border-top-width` (ID: 53) - Length values, stored in `BoundaryProp->border.top`
- `border-right-width` (ID: 54)
- `border-bottom-width` (ID: 55)
- `border-left-width` (ID: 56)
- `border-top-style` (ID: 57) - Keywords: none, solid, dashed, dotted, double
- `border-right-style` (ID: 58)
- `border-bottom-style` (ID: 59)
- `border-left-style` (ID: 60)
- `border-top-color` (ID: 61) - Color values
- `border-right-color` (ID: 62)
- `border-bottom-color` (ID: 63)
- `border-left-color` (ID: 64)

**Group 4: Layout Properties** (4 properties) ✅
- `display` (ID: 1) - Keywords: block, inline, inline-block, flex, grid, none
  - Sets `ViewGroup.display.outer` and `ViewGroup.display.inner`
- `position` (ID: 2) - Keywords: static, relative, absolute, fixed, sticky
  - Allocates `PositionProp`, sets `ViewBlock->position->position`
- `top` (ID: 3) - Length/percentage values
  - Sets `ViewBlock->position->top` and `has_top` flag
- `left` (ID: 6) - Length/percentage values
  - Sets `ViewBlock->position->left` and `has_left` flag

**Group 5: Text Properties** (4 properties) ✅
- `text-align` (ID: 11) - Keywords: left, right, center, justify
  - Stored in `BlockProp->text_align`
- `text-decoration` (ID: 12) - Keywords: none, underline, overline, line-through
  - Stored in `FontProp->text_deco`
- `vertical-align` (ID: 15) - Keywords: baseline, top, middle, bottom, sub, super, text-top, text-bottom
  - Stored in `InlineProp->vertical_align`
  - Also supports length and percentage values (logged but not fully stored yet)
- `cursor` (ID: 22) - Keywords: auto, pointer, text, move
  - Stored in `InlineProp->cursor`

**Group 6: Remaining Position Properties** (3 properties) ✅
- `right` (ID: 7) - Length/percentage values
  - Sets `ViewBlock->position->right` and `has_right` flag
- `bottom` (ID: 8) - Length/percentage values
  - Sets `ViewBlock->position->bottom` and `has_bottom` flag
- `z-index` (ID: 9) - Integer values or 'auto' keyword
  - Sets `ViewBlock->position->z_index`

**Group 7: Float and Clear** (2 properties) ✅
- `float` (ID: 4) - Keywords: left, right, none
  - Stored in `ViewBlock->position->float_prop`
- `clear` (ID: 5) - Keywords: left, right, both, none
  - Stored in `ViewBlock->position->clear`

**Group 8: Overflow Properties** (3 properties) ✅
- `overflow` (ID: 18) - Keywords: visible, hidden, scroll, auto
  - Sets both `ViewBlock->scroller->overflow_x` and `overflow_y`
- `overflow-x` (ID: 19) - Keywords: visible, hidden, scroll, auto
  - Stored in `ViewBlock->scroller->overflow_x`
- `overflow-y` (ID: 20) - Keywords: visible, hidden, scroll, auto
  - Stored in `ViewBlock->scroller->overflow_y`

**Group 9: White-space Property** (1 property) ✅
- `white-space` (ID: 14) - Keywords: normal, nowrap, pre, pre-wrap, pre-line
  - Stored in `BlockProp->white_space`
  - **Note**: Added `white_space` field to `BlockProp` structure

**Group 10: Visibility and Opacity** (3 properties) ✅
- `visibility` (ID: 21) - Keywords: visible, hidden
  - Stored in `ViewSpan->visibility`
- `opacity` (ID: 23) - Number (0.0-1.0) or percentage
  - Stored in `InlineProp->opacity`
  - **Note**: Added `opacity` field to `InlineProp` structure
- `clip` (ID: 28) - rect() function (partial implementation)
  - Sets `ViewBlock->scroller->has_clip` flag
  - **Note**: Full rect() parsing not yet implemented

**Group 11: Box Sizing** (1 property) ✅
- `box-sizing` (ID: 29) - Keywords: content-box, border-box
  - Stored in `BlockProp->box_sizing`

**Group 12: Advanced Typography** (10 properties) ✅
- `font-style` (ID: 83) - Keywords: normal, italic, oblique
  - Stored in `FontProp->font_style`
- `font-variant` (ID: 84) - Keywords: normal, small-caps
  - Resolution implemented (field not yet added to FontProp)
- `letter-spacing` (ID: 87) - Length values or 'normal' keyword
  - Resolution implemented (field not yet added to FontProp)
- `word-spacing` (ID: 88) - Length values or 'normal' keyword
  - Resolution implemented (field not yet added to FontProp)
- `text-transform` (ID: 92) - Keywords: none, capitalize, uppercase, lowercase
  - Resolution implemented (field not yet added to BlockProp)
- `text-shadow` (ID: 94) - 'none' keyword or shadow values
  - Resolution implemented (field not yet added to FontProp)
- `text-overflow` (ID: 188) - Keywords: clip, ellipsis
  - Resolution implemented (field not yet added to BlockProp)
- `word-break` (ID: 189) - Keywords: normal, break-all, keep-all
  - Resolution implemented (field not yet added to BlockProp)
- `word-wrap` (ID: 193) - Keywords: normal, break-word
  - Resolution implemented (field not yet added to BlockProp)
- `text-indent` (ID: not found) - Length values
  - Needs to be added to CSS properties definitions

**Group 13: Flexbox Properties** (10 properties) ✅
- `flex-direction` (ID: 134) - Keywords: row, row-reverse, column, column-reverse
  - Stored in `FlexProp->direction` (requires FlexProp allocation)
- `flex-wrap` (ID: 135) - Keywords: nowrap, wrap, wrap-reverse
  - Stored in `FlexProp->wrap`
- `justify-content` (ID: 136) - Keywords: flex-start, flex-end, center, space-between, space-around, space-evenly
  - Stored in `FlexProp->justify`
- `align-items` (ID: 137) - Keywords: stretch, flex-start, flex-end, center, baseline
  - Stored in `FlexProp->align_items`
- `align-content` (ID: 138) - Keywords: stretch, flex-start, flex-end, center, space-between, space-around
  - Stored in `FlexProp->align_content`
- `flex-grow` (ID: 126) - Number values (0, 1, 2, etc.)
  - Stored in `ViewSpan->flex_grow`
- `flex-shrink` (ID: 127) - Number values (0, 1, 2, etc.)
  - Stored in `ViewSpan->flex_shrink`
- `flex-basis` (ID: 128) - Length/percentage values or 'auto' keyword
  - Stored in `ViewSpan->flex_basis` (-1 for auto)
- `order` (ID: 129) - Integer values
  - Stored in `ViewSpan->order`
- `align-self` (ID: 130) - Keywords: auto, flex-start, flex-end, center, baseline, stretch
  - Stored in `ViewSpan->align_self`

**Group 14: Animation Properties** (8 properties) ✅
- `animation` (ID: 383) - Shorthand property for all animation properties
  - Logging implemented (shorthand parsing not yet fully implemented)
- `animation-name` (ID: 384) - Keywords: none, or custom animation name
  - Stored in log (animation system not yet implemented)
- `animation-duration` (ID: 385) - Time values (s, ms)
  - Uses validate_time function for parsing "2s", "500ms", etc.
- `animation-timing-function` (ID: 386) - Keywords: ease, linear, ease-in, ease-out, ease-in-out
  - Keyword mapping to Lexbor enum values
- `animation-delay` (ID: 387) - Time values (s, ms)
  - Uses validate_time function for parsing delay values
- `animation-iteration-count` (ID: 388) - Number or 'infinite' keyword
  - Supports numeric iteration counts and infinite animations
- `animation-direction` (ID: 389) - Keywords: normal, reverse, alternate, alternate-reverse
  - Direction control for animation playback
- `animation-fill-mode` (ID: 390) - Keywords: none, forwards, backwards, both
  - Controls element styling before/after animation
- `animation-play-state` (ID: 391) - Keywords: running, paused
  - Controls animation play/pause state

**Group 15: Additional Border Properties** (4 properties) ✅
- `border-top-left-radius` (ID: 66) - Length values
  - Stored in `BorderProp->radius.top_left`
- `border-top-right-radius` (ID: 67) - Length values
  - Stored in `BorderProp->radius.top_right`
- `border-bottom-right-radius` (ID: 68) - Length values
  - Stored in `BorderProp->radius.bottom_right`
- `border-bottom-left-radius` (ID: 69) - Length values
  - Stored in `BorderProp->radius.bottom_left`

**Group 16: Background Advanced Properties** (6 properties) ✅
- `background-attachment` (ID: 76) - Keywords: scroll, fixed, local
  - Resolution implemented (BackgroundProp extension needed)
- `background-origin` (ID: 77) - Keywords: border-box, padding-box, content-box
  - Resolution implemented (BackgroundProp extension needed)
- `background-clip` (ID: 78) - Keywords: border-box, padding-box, content-box
  - Resolution implemented (BackgroundProp extension needed)
- `background-position-x` (ID: 266) - Length/percentage values
  - Resolution implemented (BackgroundProp extension needed)
- `background-position-y` (ID: 267) - Length/percentage values
  - Resolution implemented (BackgroundProp extension needed)
- `background-blend-mode` (ID: 268) - Keywords: multiply, overlay, screen, etc.
  - Resolution implemented (BackgroundProp extension needed)

**Group 17: Table Properties** (8 properties) ✅
- `table-layout` (ID: 470) - Keywords: auto, fixed
  - Stored in `ViewTable->table_layout` enum (TABLE_LAYOUT_AUTO/FIXED)
- `border-collapse` (ID: 466) - Keywords: separate, collapse
  - Stored in `ViewTable->border_collapse` boolean
- `border-spacing` (ID: 467) - Length values for cell spacing
  - Stored in `ViewTable->border_spacing_h` and `border_spacing_v`
- `caption-side` (ID: 468) - Keywords: top, bottom
  - Stored in `ViewTable->caption_side` enum (CAPTION_SIDE_TOP/BOTTOM)
- `empty-cells` (ID: 469) - Keywords: show, hide
  - Stored in `ViewTable->empty_cells` enum (EMPTY_CELLS_SHOW/HIDE)
- `vertical-align` (ID: 325) - Keywords: top, middle, bottom, baseline (for table cells)
  - Stored in `ViewTableCell->vertical_align` enum
- `text-align` (ID: 324) - Keywords: left, right, center (for table cells)

**Group 18: List Properties** (6 properties) ✅
- `list-style-type` (ID: 205) - Keywords: disc, circle, square, decimal, none
  - Stored in `BlockProp->list_style_type` with keyword mapping (0x0220-0x0225)
- `list-style-position` (ID: 206) - Keywords: inside, outside
  - Stored in `BlockProp->list_style_position` PropValue
- `list-style-image` (ID: 207) - URL values or none
  - Stored in `BlockProp->list_style_image` string with URL validation
- `list-style` (ID: 208) - Shorthand combining type, position, image
  - Parsed into individual list-style components
- `counter-reset` (ID: 471) - Counter management identifiers
  - Stored in `BlockProp->counter_reset` string field
- `counter-increment` (ID: 472) - Counter increment values
  - Stored in `BlockProp->counter_increment` string field
  - Already implemented in Group 3, works for table cells
- **Note**: Added keywords collapse, separate, show, hide to keyword_map

### 📋 Remaining Properties: ~47-50

**Priority Groups for Implementation (Groups 19-20)**:

1. **Group 19: CSS Grid Extended** (15 properties) 🎯 **NEXT**
   - `grid-template-columns` (ID: 114) - Track sizing functions
   - `grid-template-rows` (ID: 115) - Track sizing functions
   - `grid-template-areas` (ID: 116) - Named grid areas
   - `grid-column-start` (ID: 117) - Grid line positioning
   - `grid-column-end` (ID: 118) - Grid line positioning
   - `grid-row-start` (ID: 119) - Grid line positioning
   - `grid-row-end` (ID: 120) - Grid line positioning
   - `grid-column` (shorthand) - Column positioning
   - `grid-row` (shorthand) - Row positioning
   - `grid-area` (shorthand) - Area positioning
   - `grid-auto-columns` (ID: 121) - Implicit track sizing
   - `grid-auto-rows` (ID: 122) - Implicit track sizing
   - `grid-auto-flow` (ID: 123) - Grid item placement algorithm
   - `gap` (ID: 124) - Shorthand for row-gap and column-gap
   - `justify-items` (ID: 125) - Default justify-self for grid items

2. **Group 20: Transform Properties** (8 properties) 🎯 **NEXT**
   - `transform` (ID: 250) - Transform functions (translate, rotate, scale, etc.)
   - `transform-origin` (ID: 251) - Transform origin point
   - `transform-style` (ID: 252) - Keywords: flat, preserve-3d
   - `perspective` (ID: 253) - 3D perspective distance
   - `perspective-origin` (ID: 254) - 3D perspective origin
   - `backface-visibility` (ID: 255) - Keywords: visible, hidden
   - `transform-box` (ID: 256) - Keywords: content-box, border-box, fill-box
   - `will-change` (ID: 257) - Performance optimization hints

3. **Group 21: Transition Properties** (6 properties)
   - `transform` (ID: 250) - Transform functions (translate, rotate, scale, etc.)
   - `transform-origin` (ID: 251) - Transform origin point
   - `transform-style` (ID: 252) - Keywords: flat, preserve-3d
   - `perspective` (ID: 253) - 3D perspective distance
   - `perspective-origin` (ID: 254) - 3D perspective origin
   - `backface-visibility` (ID: 255) - Keywords: visible, hidden
   - `transform-box` (ID: 256) - Keywords: content-box, border-box, fill-box
   - `will-change` (ID: 257) - Performance optimization hints

3. **Group 21: Transition Properties** (6 properties)
   - `transition` (ID: 270) - Shorthand for all transition properties
   - `transition-property` (ID: 271) - CSS properties to transition
   - `transition-duration` (ID: 272) - Transition duration (s, ms)
   - `transition-timing-function` (ID: 273) - Keywords: ease, linear, cubic-bezier
   - `transition-delay` (ID: 274) - Transition delay (s, ms)
   - `transition-behavior` (ID: 275) - Keywords: normal, allow-discrete

4. **Group 22: Filter and Effects** (8 properties)

6. **Group 22: Filter and Effects** (8 properties)
   - `filter` (ID: 280) - Filter functions (blur, brightness, contrast, etc.)
   - `backdrop-filter` (ID: 281) - Backdrop filter effects
   - `mix-blend-mode` (ID: 282) - Element blending modes
   - `isolation` (ID: 283) - Keywords: auto, isolate
   - `box-shadow` (ID: 284) - Box shadow effects
   - `drop-shadow` (filter function) - Drop shadow filter
   - `blur` (filter function) - Blur filter
   - `brightness` (filter function) - Brightness filter

7. **Group 23: Advanced Layout** (8 properties)
   - `contain` (ID: 290) - Containment types (layout, paint, size, style)
   - `content-visibility` (ID: 291) - Keywords: visible, hidden, auto
   - `aspect-ratio` (ID: 292) - Width/height ratio values
   - `object-fit` (ID: 293) - Keywords: fill, contain, cover, scale-down, none
   - `object-position` (ID: 294) - Position values for replaced elements
   - `resize` (ID: 295) - Keywords: none, both, horizontal, vertical
   - `scroll-behavior` (ID: 296) - Keywords: auto, smooth
   - `overscroll-behavior` (ID: 297) - Keywords: auto, contain, none

8. **Group 24: CSS Custom Properties and Variables** (4 properties)
   - CSS Custom Properties (--*) - Variable declarations
   - `var()` function - Variable usage
   - `@property` at-rule - Custom property definitions
   - Cascading and inheritance of custom properties

### ❌ Deprecated/Low Priority Groups

**Group 13: Flexbox Properties** (15+ properties) ✅ COMPLETED
   - All flexbox properties implemented in Group 13

**Group 14: Grid Properties** (20+ properties) → Split into Group 19
   - Basic grid properties to be implemented in Group 19

10. **Group 14: Transform and Animation** (10+ properties) → Split into Groups 14, 20, 21
    - `transform` (ID: 115)
    - `transform-origin` (ID: 116)
    - `transition` (ID: 117)
    - `transition-property` (ID: 118)
    - `transition-duration` (ID: 119)
    - `transition-timing-function` (ID: 120)
    - `animation` (ID: 121)
    - `animation-name` (ID: 122)
    - `animation-duration` (ID: 123)
    - Plus additional animation properties

11. **Group 15: Additional Border Properties** (10+ properties)
    - `border-radius` (ID: 65)
    - `border-top-left-radius` (ID: 66)
    - `border-top-right-radius` (ID: 67)
    - `border-bottom-left-radius` (ID: 68)
    - `border-bottom-right-radius` (ID: 69)
    - `border-image` (ID: 70)
    - Plus additional border properties

12. **Group 16: Background Advanced** (10+ properties)
    - `background-image` (ID: 72)
    - `background-repeat` (ID: 73)
    - `background-position` (ID: 74)
    - `background-size` (ID: 75)
    - `background-attachment` (ID: 76)
    - `background-clip` (ID: 77)
    - `background-origin` (ID: 78)
    - Plus additional background properties

## Implementation Architecture

### File Structure

**Core Implementation Files**:
- `radiant/lambda_css_resolve.cpp` (1233 lines) - Main CSS property resolution logic
- `radiant/lambda_css_resolve.h` - Header declarations
- `lambda/input/css/css_parser.c` (318 lines) - CSS tokenization and parsing
- `lambda/input/css/css_engine.c` - CSS cascade and stylesheet management
- `lambda/input/css/dom_element.c` - DomElement tree with AVL-based style storage
- `radiant/view_pool.cpp` (1153+ lines) - ViewTree printing and management

### Property Resolution Flow

```
CSS Declaration (from AVL tree)
    ↓
resolve_css_property(prop_id, decl, lycon)
    ↓
switch (prop_id) {
    case CSS_PROPERTY_XXX:
        1. Extract value from CssDeclaration
        2. Parse value type (length/percentage/keyword/color)
        3. Map keywords to Lexbor enum values (if applicable)
        4. Store in appropriate ViewSpan/ViewBlock structure
        5. Log for debugging
}
    ↓
ViewTree properties set (font_size, color, margin, etc.)
    ↓
Layout engine computes final dimensions
```

### Data Storage Structures

**Typography Properties** → `FontProp` and `InlineProp`
- `font_size`, `font_weight`, `font_family`, `line_height`, `color`

**Box Model** → `BlockProp`
- `given_width`, `given_height`

**Spacing** → `BoundaryProp->margin` and `BoundaryProp->padding`
- Each has `Spacing` structure with `top`, `right`, `bottom`, `left` fields

**Background & Borders** → `BoundaryProp`
- `background` (RGBA color)
- `border` (Spacing structure for widths)
- Border styles and colors stored separately

**Layout** → `ViewGroup.display` and `ViewBlock->position`
- `display.outer` and `display.inner` (Lexbor enum values)
- `PositionProp` with `position`, `top`, `right`, `bottom`, `left` fields

### Helper Functions

**Keyword Mapping**:
```cpp
int map_css_keyword_to_lexbor(const char* keyword)
// Binary search through sorted keyword_map array
// Returns Lexbor enum value (e.g., CSS_VALUE_BLOCK = 0x00ef)
```

**Color Parsing**:
```cpp
uint32_t color_name_to_rgb(const char* color_name)
// Maps color names to RGBA values
// Supports 25+ color keywords (red, blue, orange, lightgray, etc.)
```

**Length Value Parsing**:
```cpp
float parse_css_length(const char* value, float base_size)
// Parses "10px", "2em", "50%", etc.
// Returns float value in pixels
```

**Font Property Mapping**:
```cpp
int map_lambda_font_size_keyword(const char* keyword)
int map_lambda_font_weight_keyword(const char* keyword)
int map_lambda_font_family_keyword(const char* keyword)
// Map font keywords to Lexbor enum values
```

## Bug Fixes and Issues

### ✅ Fixed: Property ID Mapping Issue

**Problem**: All properties were getting `prop_id = -1` (CSS_PROPERTY_UNKNOWN)

**Root Cause**: CSS parser was hardcoding `CSS_PROP_UNKNOWN` and `token.value` field was NULL

**Solution**: Extract property names from `token.start`/`length` fields using:
```c
char* prop_name = pool_calloc(pool, len + 1, 1);
memcpy(prop_name, token.start, len);
prop_name[len] = '\0';
CssPropertyId id = css_property_id_from_string(prop_name);
```

**Result**: Property IDs now correctly resolved (font-size=81, color=96, etc.)

**Files Modified**:
- `lambda/input/css/css_parser.c` (lines 85-100)

### ✅ Fixed: Font Weight View Tree Crash

**Problem**: Printing Lambda CSS numeric font_weight values crashed when calling `lxb_css_value_by_id()`

**Root Cause**: `print_inline_props()` assumed all documents used Lexbor enum values, but Lambda CSS uses numeric values (400, 700, etc.)

**Solution**: Added `DocumentType` parameter to printing functions, check doc_type before enum lookup:
```cpp
if (doc_type == DOC_TYPE_LAMBDA_CSS) {
    // Lambda CSS uses numeric font_weight directly
    strbuf_append_format(buf, "weight:%d", span->font->weight);
} else {
    // Lexbor uses enum values that need lookup
    const lxb_css_data_t* data = lxb_css_value_by_id(NULL, span->font->weight);
    strbuf_append_format(buf, "weight:%s", data->name);
}
```

**Files Modified**:
- `radiant/view_pool.cpp` (lines 276-318)

### ✅ Fixed: CSS Keyword Values NULL

**Problem**: Border styles (solid, dashed) and colors were not being parsed

**Root Cause**: Same `token.value` NULL issue for CSS keywords

**Solution**: Extract keywords from `token.start`/`length` in `css_parse_declaration_from_tokens()`:
```c
char* keyword = pool_calloc(pool, len + 1, 1);
memcpy(keyword, token.start, len);
keyword[len] = '\0';
```

**Result**: All keywords (solid, red, lightgray, etc.) now correctly parsed

**Files Modified**:
- `lambda/input/css/css_parser.c` (lines 155-205)

### ✅ Fixed: Missing Color Keywords

**Problem**: Colors like "orange" and "lightgray" were not recognized

**Root Cause**: Incomplete color keyword map

**Solution**: Extended `color_name_to_rgb()` function to include missing colors:
```cpp
else if (strcasecmp(color_name, "orange") == 0) {
    return 0xFFA500FF; // RGB(255, 165, 0)
} else if (strcasecmp(color_name, "lightgray") == 0 ||
           strcasecmp(color_name, "lightgrey") == 0) {
    return 0xD3D3D3FF; // RGB(211, 211, 211)
}
```

**Result**: Extended color support to 25+ color keywords

**Files Modified**:
- `radiant/lambda_css_resolve.cpp`

### 🔧 Known Issues

**Issue 1: Token Value Field Unreliable**

**Status**: WORKAROUND IMPLEMENTED

**Description**: CSS parser's `CssToken.value` field is often NULL, requiring manual extraction from `token.start`/`length` fields

**Impact**: All token value extraction must use start/length approach

**Workaround**: Implemented extraction in both property name parsing and keyword parsing

**Future Fix**: Consider refactoring CSS parser to populate `token.value` field properly

---

**Issue 2: Missing Property IDs for Some Properties**

**Status**: INVESTIGATION NEEDED

**Description**: Some CSS properties may not have entries in the `css_property_id_from_string()` function

**Impact**: Properties return `CSS_PROPERTY_UNKNOWN` even with correct parsing

**Next Steps**: Audit all 150+ properties and ensure complete property ID mapping

---

**Issue 3: ViewTree Printing Incomplete for Some Properties**

**Status**: IN PROGRESS

**Description**: Position properties (top, left, right, bottom) recently added to view tree printing, but many other properties not yet displayed

**Impact**: Debugging new properties requires manual log inspection

**Solution**: Continue adding property printing as new groups are implemented

**Files to Modify**:
- `radiant/view_pool.cpp` - `print_inline_props()` and `print_block_props()`

## Testing Methodology

### Test File Creation

After each group implementation, create a test HTML file:

```html
<!DOCTYPE html>
<html>
<head>
    <style>
        /* Test properties from current group */
        body {
            property1: value1;
            property2: value2;
        }
    </style>
</head>
<body>
    <h1>Testing [group name]</h1>
    <p>Description of test properties</p>
</body>
</html>
```

### Verification Steps

1. **Build**: `make build 2>&1 | grep -E "(error|Error|Linking lambda)"`
   - Expected: `Linking lambda`, `Errors: 0`

2. **Parse Test**: `./lambda.exe layout temp/test_[group].html 2>&1 | grep "CSS Parser"`
   - Expected: Property names mapped to correct IDs

3. **Log Check**: `tail -300 log.txt | grep -E "(Property names)"`
   - Expected: Values being set with correct types

4. **View Tree Check**: `cat view_tree.txt | head -30`
   - Expected: Properties visible in view tree output

5. **Incremental Testing**: Test after EACH group before proceeding to next

### Test Files

**Location**: `temp/test_*.html`

**Current Test Files**:
- `temp/test_layout.html` - Group 4 layout properties (display, position, top, left)
- Previous test files for Groups 1-3

## Implementation Pattern

### Adding a New Property Group

**Step 1: Identify Properties**
- Check property IDs from `lambda/input/css/css_properties.h`
- Identify storage structures (FontProp, BlockProp, etc.)
- List required keyword mappings

**Step 2: Implement Resolution**
In `radiant/lambda_css_resolve.cpp`, add cases to `resolve_css_property()`:

```cpp
case CSS_PROPERTY_XXX: {
    // 1. Extract value string
    const char* value = decl->value;

    // 2. Parse value type
    if (is_length(value)) {
        float px_value = parse_css_length(value, base_size);
        // Store in structure
        target->property = px_value;
    } else if (is_keyword(value)) {
        int lexbor_value = map_css_keyword_to_lexbor(value);
        // Store enum value
        target->property = lexbor_value;
    }

    // 3. Add logging
    LOG_DEBUG("[CSS] Property: %s -> %.2f",
              css_property_name(prop_id), px_value);
    break;
}
```

**Step 3: Add View Tree Printing**
In `radiant/view_pool.cpp`, update printing functions:

```cpp
// For inline properties (typography, color, etc.)
if (property_exists) {
    strbuf_append_format(buf, "property:%.1f ", value);
}

// For block properties (layout, position, etc.)
if (block->property) {
    strbuf_append_format(buf, "property:%s ", value);
}
```

**Step 4: Create Test File**
Create `temp/test_[group].html` with properties to test

**Step 5: Build and Verify**
```bash
make build
./lambda.exe layout temp/test_[group].html
cat view_tree.txt | head -30
tail -200 log.txt | grep "CSS"
```

**Step 6: Document Progress**
Update this file with completed properties

## Performance Considerations

### Current Performance

- **CSS Parsing**: O(n) where n = CSS text length
- **Selector Matching**: O(m × k) where m = rules, k = elements
- **AVL Tree Lookup**: O(log n) where n = properties per element
- **Property Resolution**: O(p) where p = properties to resolve
- **Layout Computation**: O(n) where n = elements in tree

### Optimization Opportunities

1. **Property Caching**: Cache computed values to avoid re-parsing
2. **Batch Resolution**: Resolve related properties together (e.g., all margins)
3. **Keyword Interning**: Use string interning for repeated keywords
4. **Parallel Processing**: Resolve independent subtrees in parallel

## Next Steps

### Immediate (Groups 19-20: CSS Grid Extended + Transform Properties)

1. **Group 19 - CSS Grid Extended (15 properties)**:
   - Implement grid template properties (grid-template-columns, grid-template-rows, grid-template-areas)
   - Add grid positioning properties (grid-column-start/end, grid-row-start/end)
   - Implement grid shorthand properties (grid-column, grid-row, grid-area)
   - Add implicit grid properties (grid-auto-columns, grid-auto-rows, grid-auto-flow)
   - Implement gap and justify-items properties

2. **Group 20 - Transform Properties (8 properties)**:
   - Implement transform property with translate, rotate, scale, skew functions
   - Add transform-origin for setting transformation center point
   - Implement 3D transform properties (transform-style, perspective, perspective-origin)
   - Add backface-visibility for 3D transform handling
   - Implement transform-box and will-change for optimization

3. **Combined Implementation Goals**:
   - Extend ViewGrid structure for CSS Grid layout properties
   - Create new TransformProp structure for transform properties
   - Add keyword mappings for grid and transform values (fr units, repeat() function, transform functions)
   - Create comprehensive test examples with complex grids and transforms

### Short Term (Groups 21-22)

1. Transition properties (6 properties) - CSS animations and transitions system
2. Filter and effects (8 properties) - blur, shadow, blend modes, and visual effects
3. Advanced styling capabilities for modern web applications
4. Comprehensive testing of visual effects and animations

### Medium Term (Groups 23-24)

1. Advanced layout properties (8 properties) - containment, aspect-ratio, object-fit
2. CSS Custom Properties and Variables (4 properties) - CSS variables system
3. Comprehensive testing of all modern CSS features
4. Performance optimization and caching

### Long Term (Completion & Optimization)

1. Complete all remaining CSS properties to reach 150+ target
2. Add comprehensive CSS3 support for all modern web features
3. Performance optimization and property caching
4. Full compatibility testing across different CSS specifications

## Documentation References

- **Main Documentation**: `doc/Lambda_CSS_System.md` - Complete system architecture
- **Property IDs**: `lambda/input/css/css_properties.h` - Complete property ID list
- **Lexbor Values**: `lexbor/source/lexbor/css/value/const.h` - Lexbor enum values
- **View Structures**: `radiant/view.hpp` - ViewSpan, ViewBlock, FontProp, etc.

## Changelog

**January 10, 2025**:
- ✅ Completed Group 18: List Properties (6 properties)
  - Implemented: list-style-type, list-style-position, list-style-image, list-style (shorthand), counter-reset, counter-increment
  - Added list-related keywords: circle, decimal, disc, inside, outside, square (mapped to 0x0220-0x0225)
  - Extended BlockProp structure with 4 new fields for list property storage
  - Created comprehensive test file with 9 test sections covering all list property combinations
  - All properties successfully tested with lambda.exe layout command - no parsing errors
  - Total: 103/150+ properties implemented

**October 25, 2025**:
- ✅ Completed Group 12: Advanced Typography Properties (10 properties)
  - Implemented: font-style, font-variant, letter-spacing, word-spacing, text-transform, text-shadow, text-overflow, word-break, word-wrap
  - Added CSS property definitions for font-variant, letter-spacing, word-spacing, text-shadow
  - Added CSS keyword mappings for typography values (capitalize, clip, ellipsis, etc.)
  - Property resolution implemented (some fields need to be added to view structures)
  - Total: 61/150+ properties implemented
  - Need to add text-indent to CSS properties definitions

**October 24, 2025**:
- ✅ Completed Group 4: Layout Properties (4 properties)
  - Implemented: display, position, top, left
  - Added position property printing to view tree
  - Verified all properties working correctly
  - Total: 32/150+ properties implemented

**October 23, 2025** (estimated):
- ✅ Completed Group 3: Background & Borders (13 properties)
  - Fixed CSS parser keyword extraction from token start/length
  - Extended color keyword map (added orange, lightgray)
  - Implemented all border width/style/color properties
  - Implemented background-color

**October 22, 2025** (estimated):
- ✅ Completed Group 2: Box Model Basics (10 properties)
  - Implemented width, height, margins (4), padding (4)
  - Verified storage in BlockProp and BoundaryProp structures

**October 21, 2025** (estimated):
- ✅ Completed Group 1: Core Typography & Color (5 properties)
- ✅ Fixed property ID mapping issue
- ✅ Fixed font weight view tree crash
- Initial implementation of Lambda CSS property resolution

---

**Status Summary**: 103/150+ properties (69% complete)
**Next Milestone**: Groups 19-20 - CSS Grid Extended + Transform Properties (23 properties)
**Target**: Complete all 150+ properties incrementally with testing after each group

**Latest Completed**: Group 18 - List Properties ✅
- All 6 list properties implemented (list-style-type, list-style-position, list-style-image, list-style, counter-reset, counter-increment)
- Build successful, keyword mapping complete, property resolution cases added
- Comprehensive testing with nested lists, shorthands, and counter management verified
