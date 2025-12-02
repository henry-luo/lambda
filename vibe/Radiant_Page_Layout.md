# Radiant Layout Engine - Gap Analysis Report

This document analyzes the layout capabilities of the Radiant HTML/CSS rendering engine, focusing specifically on layout features required to properly render web pages. Rendering features (backgrounds, gradients, shadows, etc.) are excluded from this analysis.

## Executive Summary

Radiant has **strong layout fundamentals** with complete implementations of block, inline, and flexbox layouts. The main gaps are:
1. **CSS Custom Properties** - Parser fails on `--*` variables (critical for modern CSS)
2. **`@media` Query Evaluation** - Responsive layouts broken
3. **`::before` / `::after`** - Generated content not supported
4. **CSS Grid** - Algorithm exists but needs integration
5. **`position: sticky`** - Not implemented

---

## Recently Fixed Issues

### ✅ HTML 1.0 Support (cern.html - World's First Web Page)

**Problem**: Ancient HTML 1.0 documents (like cern.html from 1991) use a different structure:
- `<HEADER>` tag instead of `<head>`
- `<BODY>` as a sibling of `<HEADER>`, not wrapped in `<html>`
- `<NEXTID>` as a void element (was being parsed as container)

**Symptoms**: Layout showed only `[view-block:header]` with `[nil-view]` children, no body content rendered.

**Solution** (implemented in `lambda/input/input-html.cpp`):
1. Added `nextid` and `isindex` to HTML void elements list
2. HTML parser detects HTML 1.0 format (HEADER + BODY as root siblings, no DOCTYPE)
3. Creates synthetic `<html>` wrapper element
4. Renames `<HEADER>` to `<head>` for standard DOM structure
5. Result: `<html><head>...</head><body>...</body></html>`

**Files Changed**:
- `lambda/input/input-html-tokens.cpp` - Added legacy void elements
- `lambda/input/input-html.cpp` - HTML 1.0 detection and normalization
- `radiant/view.hpp` - Added `HTML1_0` to `HtmlVersion` enum
- `radiant/cmd_layout.cpp` - HTML 1.0 version detection

---

## 1. Layout Modes

### ✅ Complete

| Layout Mode | Status | Notes |
|-------------|--------|-------|
| **Block flow** | ✅ Complete | Margin collapsing, width/height calculation |
| **Inline flow** | ✅ Complete | Text wrapping, line breaking, vertical alignment |
| **Flexbox** | ✅ Complete | All flex properties, multi-line, alignment, gap |
| **Float** | ✅ Complete | FloatBox system with clear property |

### ⚠️ Partial / In Progress

| Layout Mode | Status | Notes |
|-------------|--------|-------|
| **CSS Grid** | ⚠️ Partial | `layout_grid_container()` implemented in `layout_grid.cpp`, called from `layout_block.cpp` |
| **Table** | ⚠️ Partial | Basic structure works, column width distribution needs work |

---

## 2. Positioning

| Position Value | Status | Notes |
|----------------|--------|-------|
| `static` | ✅ Complete | Default behavior |
| `relative` | ✅ Complete | Offset applied after normal flow |
| `absolute` | ✅ Complete | Containing block resolution works |
| `fixed` | ✅ Complete | Viewport-relative positioning |
| `sticky` | ❌ **Missing** | Scroll-based hybrid positioning |

**Implementation Location**: `radiant/layout_positioned.cpp`

---

## 3. Box Model Properties

| Property | Status | Notes |
|----------|--------|-------|
| `margin` (all sides) | ✅ Complete | Including `auto` centering |
| `padding` (all sides) | ✅ Complete | |
| `border-width` (all sides) | ✅ Complete | |
| `width` / `height` | ✅ Complete | px, %, auto |
| `min-width` / `max-width` | ✅ Complete | Constraint clamping in `adjust_min_max_width()` |
| `min-height` / `max-height` | ✅ Complete | Constraint clamping in `adjust_min_max_height()` |
| `box-sizing` | ✅ Complete | `content-box`, `border-box` |

**Implementation Location**: `radiant/layout_block.cpp`

---

## 4. Display Property Values

| Value | Status | Notes |
|-------|--------|-------|
| `block` | ✅ Complete | |
| `inline` | ✅ Complete | |
| `inline-block` | ✅ Complete | |
| `flex` | ✅ Complete | |
| `inline-flex` | ⚠️ Partial | Treated as flex |
| `grid` | ⚠️ Partial | Layout algorithm exists |
| `inline-grid` | ❌ Missing | |
| `table` / `table-*` | ⚠️ Partial | Basic support |
| `none` | ✅ Complete | Element hidden |
| `contents` | ❌ Missing | Box removed, children promoted |

---

## 5. Generated Content (Layout Impact)

| Feature | Status | Impact on Layout |
|---------|--------|------------------|
| `::before` | ❌ **Missing** | Creates inline box before element content |
| `::after` | ❌ **Missing** | Creates inline box after element content |
| `content` property | ❌ **Missing** | Required for pseudo-elements |
| List markers (`::marker`) | ⚠️ Basic | `list-style-type` works |

**Note**: `::before` and `::after` pseudo-elements affect layout by inserting generated boxes. Many modern designs rely on these for layout purposes (clearfixes, decorative elements that take space, etc.).

---

## 6. Overflow & Scrolling

| Property | Status | Notes |
|----------|--------|-------|
| `overflow: visible` | ✅ Complete | Default |
| `overflow: hidden` | ✅ Complete | Clipping implemented |
| `overflow: scroll` | ✅ Complete | ScrollPane system |
| `overflow: auto` | ✅ Complete | |
| `overflow: clip` | ✅ Complete | |

**Implementation Location**: `radiant/layout_block.cpp` (`finalize_block_flow()`)

---

## 7. Text Layout Properties

| Property | Status | Notes |
|----------|--------|-------|
| `text-align` | ✅ Complete | left, center, right, justify |
| `text-indent` | ✅ Complete | |
| `line-height` | ✅ Complete | normal, number, length, % |
| `vertical-align` | ✅ Complete | For inline elements |
| `white-space` | ✅ Complete | normal, nowrap, pre, etc. |
| `word-break` | ⚠️ Partial | |

---

## 8. CSS Parsing Issues Affecting Layout

### 🔴 Critical: CSS Custom Properties (`--*`)

**Problem**: The CSS parser fails on all custom property declarations (token type 27).

**Symptoms**:
```
[CSS Parser] Expected IDENT for property, got token type 27
[CSS Parser] After parsing: decl=0x0
```

**Impact**:
- Modern CSS heavily uses custom properties
- `cnn_lite.html` has hundreds of `var(--color-*)` declarations that all fail
- Results in missing styles that affect layout (widths, margins, etc.)

**Location**: `lambda/input/css/css_parser.cpp`

### 🔴 Critical: `@media` Query Evaluation

**Problem**: Media queries are tokenized but rules inside are skipped.

**Current Code** (`radiant/cmd_layout.cpp:475`):
```cpp
// Only process style rules (skip @media, @import, etc.)
```

**Impact**:
- Responsive layouts completely broken
- Test pages use `@media` extensively (77 occurrences in test pages)

### 🟡 Medium: `calc()` Expressions

**Problem**: Complex `calc()` expressions may not evaluate correctly in all contexts.

**Impact**: Calculated widths/heights may be wrong.

---

## 9. Priority Recommendations

### 🔴 High Priority (Required for modern web pages)

1. **CSS Custom Properties + `var()` Function**
   - Location: `lambda/input/css/css_parser.cpp`, `css_value_parser.cpp`
   - Issue: Token type 27 (`--*`) fails parsing
   - Impact: Modern CSS completely broken

2. **`@media` Query Evaluation**
   - Location: `radiant/cmd_layout.cpp`
   - Issue: Media rules skipped entirely
   - Solution: Evaluate media conditions, apply matching rules

3. **`::before` / `::after` Pseudo-elements**
   - Location: Needs new implementation in style resolution + layout
   - Impact: Missing generated content boxes affect layout

### 🟡 Medium Priority

4. **CSS Grid Layout Integration**
   - Location: `radiant/layout_grid.cpp`, `radiant/grid.hpp`
   - Status: Algorithm exists (`layout_grid_container()` is implemented and called)
   - Needs: Testing and potential fixes for edge cases

5. **`position: sticky`**
   - Location: `radiant/layout_positioned.cpp`
   - Needs: Scroll position tracking during layout

6. **`display: contents`**
   - Box model removed, children promoted to parent

### 🟢 Lower Priority

7. **`display: inline-grid`**
   - Inline-level grid container

8. **Complete table layout algorithm**
   - Column width distribution per CSS 2.1 spec

9. **Multi-column layout** (`column-count`, `column-width`)
   - Not commonly used in test pages

---

## 10. Test Page Analysis

| Page | Layout Issues |
|------|---------------|
| `cnn_lite.html` | CSS variables (hundreds!), @media queries |
| `zengarden.html` | `::before`/`::after`, external CSS |
| `sample5.html` | Flexbox works, but gradients/shadows missing (rendering) |
| `npr.html` | Basic layout works |
| `legible.html` | Minimal CSS, should work |
| `cern*.html` | No CSS, works fine |
| `html2_spec.html` | Pure HTML, works |

---

## 11. Architecture Notes

### Layout Flow
```
layout_html_doc()
  └── layout_block() / layout_inline()
        ├── dom_node_resolve_style()  // CSS resolution
        ├── layout_block_content()
        │     ├── setup_inline()
        │     └── layout_block_inner_content()
        │           ├── CSS_VALUE_FLOW → layout_flow_node()
        │           ├── CSS_VALUE_FLEX → layout_flex_content()
        │           ├── CSS_VALUE_GRID → layout_grid_container()
        │           └── CSS_VALUE_TABLE → layout_table()
        └── finalize_block_flow()
```

### Key Files
- `radiant/layout.cpp` - Main layout coordinator
- `radiant/layout_block.cpp` - Block layout, box model
- `radiant/layout_inline.cpp` - Inline/text layout
- `radiant/layout_flex.cpp` - Flexbox (2498 lines, complete)
- `radiant/layout_grid.cpp` - Grid layout
- `radiant/layout_table.cpp` - Table layout
- `radiant/layout_positioned.cpp` - Positioning (relative, absolute, fixed)
- `radiant/layout_text.cpp` - Text measurement and line breaking

---

## 12. Conclusion

Radiant's layout engine is **fundamentally solid** for CSS 2.1 and Flexbox layouts. The primary blockers for rendering modern web pages are:

1. **CSS parsing issues** (custom properties, @media) - These prevent styles from being applied at all
2. **Generated content** (`::before`/`::after`) - Affects layout calculations
3. **Grid layout** - Needs testing and integration work

Fixing the CSS parsing issues (#1 and #2 in High Priority) would immediately improve compatibility with most modern web pages.
