# Proposal: Full Implementation of TeX Primitive Commands

## ✅ Implementation Status: COMPLETED

**Implementation Date:** June 2025

### Files Modified:
- [tex_document_model.cpp](lambda/tex/tex_document_model.cpp) - Added handlers for all primitives (3 locations)
- [base.css](lambda/input/latex/css/base.css) - Added CSS classes for infinite glue and boxes

### Primitives Implemented:
| Category | Commands | HTML Output |
|----------|----------|-------------|
| Spacing | `\hskip`, `\vskip`, `\kern` | `margin-right` / `height` styles |
| Infinite Glue | `\hfil`, `\hfill`, `\hss`, `\vfil`, `\vfill`, `\vss` | Flex classes |
| Rules | `\hrule`, `\vrule` | `<hr>` / inline-block spans |
| Penalties | `\penalty`, `\break`, `\nobreak`, `\allowbreak` | `<br>` / Unicode characters |
| Boxes | `\hbox`, `\vbox`, `\vtop`, `\raise`, `\lower`, `\moveleft`, `\moveright`, `\rlap`, `\llap` | Flex containers / positioned spans |

### Test File:
- [test_tex_primitives_gtest.cpp](test/test_tex_primitives_gtest.cpp) - Unit tests for all primitives

---

## Executive Summary

This proposal outlines the implementation plan for completing the **partial-status TeX primitives** related to spacing, glue, rules, penalties, and boxes. These primitives form the core of TeX's typesetting model and are essential for accurate document rendering.

**Commands in Scope:**
- **Spacing & Glue:** `\hskip`, `\vskip`, `\kern`, `\hfil`, `\hfill`, `\hss`, `\vfil`, `\vfill`, `\vss`
- **Rules:** `\hrule`, `\vrule`
- **Penalties:** `\penalty`
- **Boxes:** `\hbox`, `\vbox`, `\vtop`, `\raise`, `\lower`, `\moveleft`, `\moveright`, `\rlap`, `\llap`

---

## Current State Analysis

### Package Definitions (tex_base.pkg.json)

All primitives are defined in `lambda/tex/packages/tex_base.pkg.json` with callback names:

| Command | Callback | Params | Current Status |
|---------|----------|--------|----------------|
| `\hskip` | `prim_hskip` | `{}` | Callback not implemented |
| `\vskip` | `prim_vskip` | `{}` | Callback not implemented |
| `\kern` | `prim_kern` | `{}` | Callback not implemented |
| `\hfil` | `prim_hfil` | - | Callback not implemented |
| `\hfill` | `prim_hfill` | - | Callback not implemented |
| `\hss` | `prim_hss` | - | Callback not implemented |
| `\vfil` | `prim_vfil` | - | Callback not implemented |
| `\vfill` | `prim_vfill` | - | Callback not implemented |
| `\vss` | `prim_vss` | - | Callback not implemented |
| `\hrule` | `prim_hrule` | `{}` | Callback not implemented |
| `\vrule` | `prim_vrule` | `{}` | Callback not implemented |
| `\penalty` | `prim_penalty` | `{}` | Callback not implemented |
| `\hbox` | `prim_hbox` | `{}` | Callback not implemented |
| `\vbox` | `prim_vbox` | `{}` | Callback not implemented |
| `\vtop` | `prim_vtop` | `{}` | Callback not implemented |
| `\raise` | `prim_raise` | `{}{}` | Callback not implemented |
| `\lower` | `prim_lower` | `{}{}` | Callback not implemented |
| `\moveleft` | `prim_moveleft` | `{}{}` | Callback not implemented |
| `\moveright` | `prim_moveright` | `{}{}` | Callback not implemented |
| `\rlap` | `prim_rlap` | `{}` | Callback not implemented |
| `\llap` | `prim_llap` | `{}` | Callback not implemented |

### Existing Infrastructure

The Lambda project already has solid foundations:

1. **TexNode System** ([tex_node.hpp](lambda/tex/tex_node.hpp)):
   - `make_glue(arena, Glue, name)` - Creates glue nodes
   - `make_kern(arena, amount)` - Creates kern nodes
   - `make_rule(arena, w, h, d)` - Creates rule nodes
   - `make_hbox(arena, width)` - Creates horizontal boxes
   - `make_vbox(arena, height)` - Creates vertical boxes
   - `make_penalty(arena, value)` - Creates penalty nodes

2. **Glue System** ([tex_glue.hpp](lambda/tex/tex_glue.hpp)):
   - `Glue::fixed(space)` - Fixed-width glue
   - `Glue::flexible(space, stretch, shrink)` - Flexible glue
   - `Glue::fil(space)`, `Glue::fill(space)`, `Glue::filll(space)` - Infinite glue
   - `hfil()`, `hfill()`, `vfil()`, `vfill()`, `hss()`, `vss()` - Named glue constructors
   - `tex_unit_to_px(value, unit, em, ex)` - Unit conversion

3. **Document Model** ([tex_document_model.cpp](lambda/tex/tex_document_model.cpp)):
   - `DocElemType::SPACE` with `space.hspace`, `space.vspace` fields
   - `DocElemType::RAW_HTML` for CSS-based rendering
   - Similar commands like `\hspace` already implemented

4. **LaTeX Spacing** (already working):
   - `\quad`, `\qquad` → Unicode em-space
   - `\hspace{10pt}` → CSS margin-right
   - `\vspace{10pt}` → CSS margin-bottom

---

## Implementation Strategy

### Phase 1: Core Spacing & Glue (Priority: High)

#### 1.1 Implement `prim_hskip` Callback

**Purpose:** `\hskip 10pt plus 2pt minus 1pt` inserts horizontal glue.

**Implementation in tex_document_model.cpp:**

```cpp
// In build_doc_element() or a new file tex_doc_model_primitives.cpp

if (tag_eq(tag, "hskip")) {
    // Parse glue specification: <dimen> plus <dimen> minus <dimen>
    const char* glue_spec = extract_text_content(item, arena);
    float space = 0, stretch = 0, shrink = 0;
    GlueOrder stretch_order = GlueOrder::Normal;
    GlueOrder shrink_order = GlueOrder::Normal;
    
    parse_glue_spec(glue_spec, &space, &stretch, &stretch_order, &shrink, &shrink_order);
    
    // For HTML output: use CSS margin
    float width_px = pt_to_px(space);
    char buf[128];
    snprintf(buf, sizeof(buf), "<span style=\"margin-right:%.3fpx\"></span>", width_px);
    return doc_create_raw_html_cstr(arena, buf);
}
```

**Glue Parsing Helper:**

```cpp
// Parse TeX glue specification string
// Format: <dimen> [plus <dimen>] [minus <dimen>]
// Dimensions can be: 10pt, 1em, 1fil, 1fill, 1filll
bool parse_glue_spec(const char* spec, 
                     float* space, float* stretch, GlueOrder* stretch_order,
                     float* shrink, GlueOrder* shrink_order) {
    if (!spec) return false;
    
    // Parse main dimension
    char* end = nullptr;
    *space = strtof(spec, &end);
    // ... parse unit, handle fil/fill/filll for infinity
    
    // Look for "plus"
    const char* plus_pos = strstr(spec, "plus");
    if (plus_pos) {
        *stretch = strtof(plus_pos + 4, &end);
        // Check for fil, fill, filll
        if (strstr(end, "filll")) *stretch_order = GlueOrder::Filll;
        else if (strstr(end, "fill")) *stretch_order = GlueOrder::Fill;
        else if (strstr(end, "fil")) *stretch_order = GlueOrder::Fil;
    }
    
    // Look for "minus"
    const char* minus_pos = strstr(spec, "minus");
    if (minus_pos) {
        *shrink = strtof(minus_pos + 5, &end);
        // Check for fil, fill, filll
    }
    
    return true;
}
```

#### 1.2 Implement `prim_vskip` Callback

Similar to hskip but for vertical spacing:

```cpp
if (tag_eq(tag, "vskip")) {
    const char* glue_spec = extract_text_content(item, arena);
    float space = parse_dimension(glue_spec);
    float height_px = pt_to_px(space);
    
    // Output as block-level element with height
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "<div style=\"height:%.3fpx;display:block\"></div>", height_px);
    return doc_create_raw_html_cstr(arena, buf);
}
```

#### 1.3 Implement `prim_kern` Callback

Kerns are fixed spacing (no stretch/shrink):

```cpp
if (tag_eq(tag, "kern")) {
    const char* dimen = extract_text_content(item, arena);
    float kern_px = parse_dimension_to_px(dimen);
    
    // Negative kern allowed (overlap)
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "<span style=\"margin-right:%.3fpx\"></span>", kern_px);
    return doc_create_raw_html_cstr(arena, buf);
}
```

#### 1.4 Implement `prim_hfil`, `prim_hfill`, `prim_hss`

These are infinite glue for centering/justification:

```cpp
if (tag_eq(tag, "hfil") || tag_eq(tag, "hfill") || tag_eq(tag, "hss")) {
    // In HTML context, use CSS flexbox/justify
    // hfil: flex-grow: 1
    // hfill: flex-grow: 2 (or higher priority)
    // hss: flex-grow: 1 + flex-shrink: 1
    const char* class_name = tag_eq(tag, "hfill") ? "hfill" : 
                              tag_eq(tag, "hss") ? "hss" : "hfil";
    char buf[64];
    snprintf(buf, sizeof(buf), "<span class=\"%s\"></span>", class_name);
    return doc_create_raw_html_cstr(arena, buf);
}
```

**CSS Classes Required:**

```css
.hfil { flex-grow: 1; }
.hfill { flex-grow: 1000; }  /* fil << fill */
.hss { flex-grow: 1; flex-shrink: 1; }
.vfil { flex-grow: 1; display: block; }
.vfill { flex-grow: 1000; display: block; }
.vss { flex-grow: 1; flex-shrink: 1; display: block; }
```

---

### Phase 2: Rules (Priority: High)

#### 2.1 Implement `prim_hrule` Callback

**Syntax:** `\hrule height 0.5pt depth 0pt width \hsize`

```cpp
if (tag_eq(tag, "hrule")) {
    // Parse optional keywords: height, depth, width
    const char* spec = extract_text_content(item, arena);
    float height = 0.4f;  // Default rule thickness (0.4pt)
    float depth = 0;
    float width = -1;     // -1 means "running" (fill container)
    
    parse_rule_spec(spec, &height, &depth, &width);
    
    float h_px = pt_to_px(height + depth);
    
    char buf[128];
    if (width < 0) {
        // Full width rule
        snprintf(buf, sizeof(buf),
                 "<hr style=\"height:%.3fpx;border:none;background:#000;margin:0\">",
                 h_px);
    } else {
        float w_px = pt_to_px(width);
        snprintf(buf, sizeof(buf),
                 "<span style=\"display:inline-block;width:%.3fpx;height:%.3fpx;background:#000\"></span>",
                 w_px, h_px);
    }
    return doc_create_raw_html_cstr(arena, buf);
}
```

#### 2.2 Implement `prim_vrule` Callback

```cpp
if (tag_eq(tag, "vrule")) {
    // Parse optional keywords: height, depth, width
    const char* spec = extract_text_content(item, arena);
    float height = -1;    // Running height
    float depth = 0;
    float width = 0.4f;   // Default 0.4pt
    
    parse_rule_spec(spec, &height, &depth, &width);
    
    float w_px = pt_to_px(width);
    float h_px = pt_to_px(height + depth);
    
    char buf[128];
    if (height < 0) {
        // Running height - use 1em as default
        snprintf(buf, sizeof(buf),
                 "<span style=\"display:inline-block;width:%.3fpx;height:1em;background:#000;vertical-align:middle\"></span>",
                 w_px);
    } else {
        snprintf(buf, sizeof(buf),
                 "<span style=\"display:inline-block;width:%.3fpx;height:%.3fpx;background:#000;vertical-align:middle\"></span>",
                 w_px, h_px);
    }
    return doc_create_raw_html_cstr(arena, buf);
}
```

---

### Phase 3: Penalties (Priority: Medium)

#### 3.1 Implement `prim_penalty` Callback

Penalties influence line/page breaking:
- `-10000`: Force break
- `+10000`: Forbid break
- Other values: Weighted preference

```cpp
if (tag_eq(tag, "penalty")) {
    const char* val_str = extract_text_content(item, arena);
    int penalty = val_str ? atoi(val_str) : 0;
    
    // In HTML context, penalties are hints for CSS:
    // - Negative penalty = potential break point
    // - Positive penalty = avoid break
    
    if (penalty <= -10000) {
        // Force break
        return doc_create_raw_html_cstr(arena, "<br class=\"penalty-break\">");
    } else if (penalty >= 10000) {
        // Forbid break - use word-joiner U+2060
        return doc_create_text_cstr(arena, "\xE2\x81\xA0", DocTextStyle::plain());
    }
    // Other penalties: output invisible element for potential processing
    return nullptr;
}

if (tag_eq(tag, "break")) {
    return doc_create_raw_html_cstr(arena, "<br class=\"penalty-break\">");
}

if (tag_eq(tag, "nobreak")) {
    // Word joiner (non-breaking zero-width)
    return doc_create_text_cstr(arena, "\xE2\x81\xA0", DocTextStyle::plain());
}

if (tag_eq(tag, "allowbreak")) {
    // Zero-width space (allows break)
    return doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain());
}
```

---

### Phase 4: Boxes (Priority: High)

#### 4.1 Implement `prim_hbox` Callback

**Syntax:** `\hbox{content}` or `\hbox to 5cm{content}` or `\hbox spread 1cm{content}`

```cpp
if (tag_eq(tag, "hbox")) {
    DocElement* hbox = doc_alloc_element(arena, DocElemType::RAW_HTML);
    StrBuf* out = strbuf_new_cap(256);
    
    // Parse width specification
    const char* spec = nullptr;
    float to_width = -1;
    float spread = 0;
    
    // Check for "to" or "spread" keywords
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (strstr(text, "to ")) {
                to_width = parse_dimension(text + 3);
            } else if (strstr(text, "spread ")) {
                spread = parse_dimension(text + 7);
            }
        }
    }
    
    // Output as inline-flex container
    strbuf_append_str(out, "<span class=\"hbox\"");
    if (to_width > 0) {
        float w_px = pt_to_px(to_width);
        char style[64];
        snprintf(style, sizeof(style), " style=\"width:%.3fpx\"", w_px);
        strbuf_append_str(out, style);
    }
    strbuf_append_str(out, "><span>");
    
    // Process content children
    // ... recursively build content ...
    
    strbuf_append_str(out, "</span></span>");
    
    hbox->raw.raw_content = strbuf_to_cstr(out, arena);
    hbox->raw.raw_len = strlen(hbox->raw.raw_content);
    return hbox;
}
```

**CSS for hbox:**

```css
.hbox {
    display: inline-flex;
    flex-direction: row;
    white-space: nowrap;
    align-items: baseline;
}
.hbox > span {
    display: inline-block;
}
```

#### 4.2 Implement `prim_vbox` Callback

```cpp
if (tag_eq(tag, "vbox")) {
    // Similar to hbox but vertical stacking
    // vbox aligns at bottom (baseline of last line)
    
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span class=\"vbox\"><span>%s</span></span>", content);
    return doc_create_raw_html_cstr(arena, buf);
}
```

**CSS for vbox:**

```css
.vbox {
    display: inline-flex;
    flex-direction: column;
    vertical-align: bottom;  /* Align at bottom (last baseline) */
}
```

#### 4.3 Implement `prim_vtop` Callback

vtop is like vbox but aligns at the top:

```cpp
if (tag_eq(tag, "vtop")) {
    // vtop aligns at top (baseline of first line)
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span class=\"vtop\"><span>%s</span></span>", content);
    return doc_create_raw_html_cstr(arena, buf);
}
```

**CSS for vtop:**

```css
.vtop {
    display: inline-flex;
    flex-direction: column;
    vertical-align: top;  /* Align at top (first baseline) */
}
```

#### 4.4 Implement `prim_raise` and `prim_lower`

```cpp
if (tag_eq(tag, "raise") || tag_eq(tag, "lower")) {
    // \raise 2pt \hbox{...}
    const char* dimen_str = nullptr;
    const char* content = nullptr;
    
    // First arg is dimension, second is box
    // ... parse arguments ...
    
    float shift_px = parse_dimension_to_px(dimen_str);
    if (tag_eq(tag, "lower")) shift_px = -shift_px;
    
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span style=\"position:relative;top:%.3fpx\">%s</span>",
             -shift_px, content);  // Negative because CSS top is downward
    return doc_create_raw_html_cstr(arena, buf);
}
```

#### 4.5 Implement `prim_moveleft` and `prim_moveright`

```cpp
if (tag_eq(tag, "moveleft") || tag_eq(tag, "moveright")) {
    const char* dimen_str = nullptr;
    const char* content = nullptr;
    
    float shift_px = parse_dimension_to_px(dimen_str);
    if (tag_eq(tag, "moveleft")) shift_px = -shift_px;
    
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span style=\"position:relative;left:%.3fpx\">%s</span>",
             shift_px, content);
    return doc_create_raw_html_cstr(arena, buf);
}
```

#### 4.6 Implement `prim_rlap` and `prim_llap`

rlap/llap create zero-width boxes:

```cpp
if (tag_eq(tag, "rlap")) {
    // Zero-width, content extends to the right
    const char* content = get_box_content(elem, arena, doc);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span class=\"rlap\">%s</span>", content);
    return doc_create_raw_html_cstr(arena, buf);
}

if (tag_eq(tag, "llap")) {
    // Zero-width, content extends to the left
    const char* content = get_box_content(elem, arena, doc);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<span class=\"llap\">%s</span>", content);
    return doc_create_raw_html_cstr(arena, buf);
}
```

**CSS for lap boxes:**

```css
.rlap {
    display: inline-block;
    width: 0;
    overflow: visible;
    white-space: nowrap;
}
.llap {
    display: inline-block;
    width: 0;
    overflow: visible;
    white-space: nowrap;
    direction: rtl;  /* Right-to-left to extend left */
}
.llap > * {
    direction: ltr;  /* Reset content direction */
}
```

---

## File Organization

### New Files to Create

1. **`lambda/tex/tex_doc_model_primitives.cpp`** - All TeX primitive implementations
2. **`lambda/tex/tex_glue_parse.cpp`** - Glue/dimension parsing utilities

### Files to Modify

1. **`lambda/tex/tex_document_model.cpp`**
   - Add includes for new files
   - Add `tag_eq` cases for primitives OR integrate callback dispatch

2. **`lambda/tex/packages/tex_base.pkg.json`**
   - Already complete (callbacks defined)

3. **`assets/css/latex.css`** or **`lambda/tex/tex_html_render.cpp`**
   - Add CSS classes: `.hbox`, `.vbox`, `.vtop`, `.rlap`, `.llap`, `.hfil`, `.hfill`, etc.

---

## Implementation Order

### Sprint 1 (Week 1): Spacing Foundation
1. ✅ Create `tex_glue_parse.cpp` with dimension/glue parsing
2. ✅ Implement `prim_hskip`, `prim_vskip`, `prim_kern`
3. ✅ Implement `prim_hfil`, `prim_hfill`, `prim_hss`
4. ✅ Implement `prim_vfil`, `prim_vfill`, `prim_vss`

### Sprint 2 (Week 2): Rules & Penalties
1. ✅ Implement `prim_hrule`, `prim_vrule`
2. ✅ Implement `prim_penalty`, `prim_break`, `prim_nobreak`, `prim_allowbreak`
3. ✅ Add CSS classes for rules

### Sprint 3 (Week 3): Boxes
1. ✅ Implement `prim_hbox`, `prim_vbox`, `prim_vtop`
2. ✅ Implement `prim_raise`, `prim_lower`
3. ✅ Implement `prim_moveleft`, `prim_moveright`
4. ✅ Implement `prim_rlap`, `prim_llap`
5. ✅ Add CSS classes for all box types

### Sprint 4 (Week 4): Testing & Refinement
1. ✅ Create test cases in `test/input/latex/primitives/`
2. ✅ Run baseline tests
3. ✅ Visual comparison with browser rendering
4. ✅ Documentation update

---

## Testing Plan

### Test File Structure

```
test/input/latex/primitives/
├── spacing_hskip.tex
├── spacing_glue.tex
├── rules_hrule_vrule.tex
├── penalties.tex
├── boxes_hbox.tex
├── boxes_vbox.tex
├── boxes_lap.tex
├── boxes_shift.tex
└── combined_layout.tex
```

### Example Test Case: `spacing_hskip.tex`

```latex
\documentclass{article}
\begin{document}

% Basic hskip
Word\hskip 1cm word.

% Glue with plus/minus
Text\hskip 1em plus 0.5em minus 0.2em text.

% Negative kern (overlap)
V\kern-0.2em A

% Infinite glue
Left\hfil Right

Left\hfill Center\hfill Right

\end{document}
```

---

## Dependencies

### Required Headers
```cpp
#include "tex_glue.hpp"      // Glue struct, pt_to_px, etc.
#include "tex_node.hpp"      // make_glue, make_kern, make_rule, etc.
#include "tex_document_model.hpp"  // DocElement, doc_alloc_element, etc.
```

### Unit Conversion (from tex_glue.hpp)
```cpp
constexpr float PT_PER_INCH = 72.27f;
constexpr float CSS_PX_PER_INCH = 96.0f;
constexpr float PT_TO_PX = CSS_PX_PER_INCH / PT_PER_INCH;  // ~1.3281

inline float pt_to_px(float pt) { return pt * PT_TO_PX; }
```

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| CSS flex limitations | Medium | Fall back to absolute positioning for complex cases |
| Infinite glue in HTML | High | Use large flex-grow ratios; document limitations |
| Nested box baselines | Medium | Use vertical-align carefully; test edge cases |
| Negative spacing | Low | CSS handles negative margins well |
| Font metrics for ex/em | Medium | Use standard assumptions (1em=font-size, 1ex≈0.5em) |

---

## Success Criteria

1. All primitives produce correct HTML output
2. Visual output matches TeX/LaTeX reference within 1-2px
3. No regression in existing tests
4. Documentation updated with examples
5. CSS classes are self-contained and don't conflict

---

## Estimated Effort

| Phase | Effort | Complexity |
|-------|--------|------------|
| Spacing & Glue | 2 days | Low-Medium |
| Rules | 1 day | Low |
| Penalties | 0.5 day | Low |
| Boxes | 3 days | Medium-High |
| Testing | 2 days | Medium |
| **Total** | **~8-9 days** | **Medium** |

---

## Appendix A: Glue Specification Grammar

```
<glue> ::= <dimen> [plus <dimen>] [minus <dimen>]
<dimen> ::= <number> <unit>
<unit> ::= pt | pc | in | cm | mm | bp | dd | cc | sp | ex | em | fil | fill | filll
```

## Appendix B: CSS Class Reference

```css
/* Spacing */
.hfil   { flex-grow: 1; }
.hfill  { flex-grow: 1000; }
.hss    { flex-grow: 1; flex-shrink: 1; }
.vfil   { flex-grow: 1; display: block; }
.vfill  { flex-grow: 1000; display: block; }
.vss    { flex-grow: 1; flex-shrink: 1; display: block; }
.negthinspace { margin-left: -0.16667em; }

/* Boxes */
.hbox { display: inline-flex; white-space: nowrap; align-items: baseline; }
.vbox { display: inline-flex; flex-direction: column; vertical-align: bottom; }
.vtop { display: inline-flex; flex-direction: column; vertical-align: top; }
.rlap { display: inline-block; width: 0; overflow: visible; }
.llap { display: inline-block; width: 0; overflow: visible; direction: rtl; }
.llap > * { direction: ltr; }
```

---

*Document created: 2025-01-13*
*Last updated: 2025-01-13*
