# LaTeX Math HTML Output Design

**Date:** January 20, 2026  
**Status:** Proposal  
**Goals:** 
1. Add HTML output for math formulas, reusing existing DVI typesetting
2. **Support math in full LaTeX document HTML output** - when converting a complete LaTeX document to HTML, embedded math should render properly

---

## 1. Overview

### Primary Goals

| Goal | Description | Use Case |
|------|-------------|----------|
| **Math Formula HTML** | `\frac{a}{b}` → `<span>...</span>` | Math snippets, API |
| **Full Document Math** | `.tex` with `$...$` → HTML with rendered math | Academic papers |

### MathLive's Architecture (Reference)

MathLive has a **three-phase** rendering pipeline:

```
LaTeX String
    │
    ▼
Phase 1: Lexer + Parser → Atom[] (semantic AST)
    │     (e.g., GenfracAtom for \frac)
    │
    ▼
Phase 2: Atom.render(context) → Box[] (layout tree with dimensions)
    │     (calculates height, depth, width for each box)
    │
    ▼
Phase 3: Box.toMarkup() → HTML string
         (generates <span> elements with CSS)
```

**Key insight**: MathLive's `Box` IS the typeset output - it contains dimensions calculated during `render()`, then `toMarkup()` converts to HTML.

#### MathLive SSR Pipeline (`convertLatexToMarkup()`)

From `mathlive/src/public/mathlive-ssr.ts`:

```typescript
export function convertLatexToMarkup(text: string, options?: {...}): string {
    // 1. Parse LaTeX to Atom tree
    const root = new Atom({ mode: 'math', type: 'root' });
    root.body = parseLatex(text, context);
    
    // 2. Typeset: Atom → Box (calculates all dimensions)
    let box = root.render(effectiveContext);
    
    // 3. Optimize box tree (merge adjacent boxes)
    box = coalesce(applyInterBoxSpacing(box, effectiveContext));
    
    // 4. Add struts for baseline handling
    box = makeStruts(box, { classes: 'ML__latex' });
    
    // 5. Convert to HTML markup
    return box.toMarkup();
}
```

**Important detail**: `makeStruts()` wraps the box tree with baseline struts to ensure proper vertical alignment in surrounding text.

#### `makeStruts()` - Baseline Handling

From `mathlive/src/core/box.ts`:

```typescript
export function makeStruts(box: Box, options: { classes?: string }): Box {
    // Top strut: ensures height above baseline
    const topStrut = new Box(null, { classes: 'ML__strut' });
    topStrut.height = box.height;
    topStrut.setStyle('height', `${box.height}em`);
    
    // Bottom strut: ensures depth below baseline  
    const bottomStrut = new Box(null, { classes: 'ML__strut ML__bottom' });
    bottomStrut.setStyle('height', `${box.height + box.depth}em`);
    bottomStrut.setStyle('vertical-align', `${-box.depth}em`);
    
    return new Box([topStrut, bottomStrut, box], { classes: options.classes });
}
```

The struts are zero-width `<span>` elements that "hold space" for proper baseline alignment.

### Our Architecture (Similar)

```
LaTeX String
    │
    ▼
Phase A: parse_math_string_to_ast() → MathASTNode (AST)
    │     (similar to MathLive's Atom[])
    │
    ▼
Phase B: typeset_math_ast() → TexNode (layout)
    │     (similar to MathLive's Box[] - has dimensions)
    │
    ├──► render_to_dvi() → DVI output (existing)
    │
    └──► render_to_html() → HTML output (NEW - this proposal)
```

**Our TexNode is analogous to MathLive's Box** - both contain calculated dimensions. We just need to add `toMarkup()` equivalent.

---

## 2. MathLive Box → HTML Details

### `toMarkup()` Method

From `mathlive/src/core/box.ts`, the `toMarkup()` method:

```typescript
toMarkup(): string {
    let body = this.value ?? '';
    
    // 1. Render children recursively
    if (this.children) 
        for (const box of this.children) body += box.toMarkup();
    
    // 2. SVG overlay if needed (for radicals, etc.)
    let svgMarkup = '';
    if (this.svgBody) svgMarkup = svgBodyToMarkup(this.svgBody);
    
    // 3. Build CSS classes and styles
    const props: string[] = [];
    // ... classes, id, attributes
    
    // 4. Apply dimensions via inline styles
    if (this.hasExplicitWidth)
        cssProps.width = `${Math.ceil(this._width * 100) / 100}em`;
    if (this.scale !== 1.0)
        styles.push(`font-size: ${Math.ceil(this.scale * 10000) / 100}%`);
    
    // 5. Wrap in <span>
    return `<span ${props.join(' ')}>${body}${svgMarkup}</span>`;
}
```

### VBox (Vertical Stacking)

From `mathlive/src/core/v-box.ts`, the VBox creates vertically stacked elements:

```typescript
function makeRows(row: VBoxRow): { html: string; height: number; depth: number } {
    const classes = ['ML__pstrut'];  // "phantom strut" for baseline
    let html = `<span class="${classes.join(' ')}" style="height:${body.height + body.depth}em">`;
    html += body.toMarkup();
    html += '</span>';
    
    return { html, height: body.height, depth: body.depth };
}
```

**pstrut pattern**: Each VBox row contains a "phantom strut" that establishes the row's baseline.

### Key CSS Properties

| Property | Purpose | Example |
|----------|---------|---------|
| `display: inline-block` | Horizontal flow | Boxes flow left-to-right |
| `vertical-align: Xem` | Baseline alignment | Align to surrounding text |
| `font-size: XX%` | Scaling | Sub/superscripts at 70% |
| `position: relative` + `top` | Vertical shift | Numerator/denominator |
| `height` + `vertical-align` | Strut sizing | Baseline handling |

---

## 3. HTML Box Model (from MathLive)

MathLive uses `<span>` elements with CSS for positioning:

```html
<!-- Fraction example: \frac{a}{b} -->
<span class="ML__mfrac" style="display:inline-block">
  <span class="ML__vlist" style="display:inline-block;vertical-align:middle">
    <span class="ML__numer" style="display:block;text-align:center">a</span>
    <span class="ML__frac-line" style="border-bottom:1px solid"></span>
    <span class="ML__denom" style="display:block;text-align:center">b</span>
  </span>
</span>
```

Key CSS patterns:
- `display: inline-block` for horizontal flow
- `vertical-align` for baseline alignment
- `position: relative` + `top/bottom` for vertical shifts
- Font scaling via `font-size: XX%`

---

## 4. Implementation Plan

### 4.1 New Files

| File | Description |
|------|-------------|
| `lambda/tex/tex_html_render.hpp` | HTML renderer API |
| `lambda/tex/tex_html_render.cpp` | TexNode → HTML conversion |

### 4.2 Core Function

```cpp
// tex_html_render.hpp
namespace tex {

// Render TexNode tree to HTML string
// Returns HTML markup for the math formula
char* render_texnode_to_html(TexNode* node, Arena* arena);

// Render with options (font size, CSS class prefix)
struct HtmlRenderOptions {
    float base_font_size_px = 16.0f;
    const char* class_prefix = "ML";  // MathLive-compatible
    bool include_styles = true;       // Include inline styles
};

char* render_texnode_to_html(TexNode* node, Arena* arena, const HtmlRenderOptions& opts);

} // namespace tex
```

### 4.3 TexNode to HTML Mapping

| TexNode Type | HTML Output |
|--------------|-------------|
| CHAR | `<span class="ML__ord">X</span>` |
| HLIST | `<span class="ML__hlist">...children...</span>` |
| VLIST | `<span class="ML__vlist" style="vertical-align:Xem">...</span>` |
| RULE | `<span class="ML__rule" style="width:Xem;height:Yem"></span>` |
| KERN | (spacing via margin-right) |
| GLUE | (spacing via margin-right) |

---

## 5. CLI Integration

```bash
# Render math to HTML (new)
./lambda.exe math "\frac{a}{b}" --html

# Output:
# <span class="ML__mfrac">...</span>

# Save to file
./lambda.exe math "\frac{a}{b}" --html -o formula.html

# Full HTML document with CSS
./lambda.exe math "\frac{a}{b}" --html --standalone -o formula.html
```

---

## 6. Testing Strategy

### 6.1 Generate MathLive Reference Files

Use MathLive to generate HTML reference files for test formulas:

```bash
# Script: utils/generate_mathlive_refs.js
# Generates HTML files in test/latex/reference/

node utils/generate_mathlive_refs.js test/latex/test_simple_math.tex
# Output: test/latex/reference/test_simple_math.html
```

### 6.2 Test Suite

```cpp
// test/test_latex_html_compare_gtest.cpp
TEST_F(HtmlCompareTest, SimpleMath) {
    // Compare Lambda HTML output with MathLive reference
    EXPECT_TRUE(compare_html_output("test_simple_math"));
}
```

---

## 7. CSS Stylesheet

Provide a default stylesheet compatible with MathLive classes:

```css
/* lambda_math.css */
.ML__mfrac { display: inline-block; vertical-align: middle; }
.ML__vlist { display: inline-block; }
.ML__numer, .ML__denom { display: block; text-align: center; }
.ML__frac-line { border-bottom: 1px solid currentColor; }
.ML__sqrt { display: inline-block; }
.ML__sqrt-sign { /* SVG radical sign */ }
.ML__supsub { display: inline-block; }
.ML__sup { font-size: 70%; vertical-align: super; }
.ML__sub { font-size: 70%; vertical-align: sub; }
```

---

## 8. Success Criteria

### Phase 1: Math Formula HTML
- [ ] `render_texnode_to_html()` implemented
- [ ] Fractions render correctly
- [ ] Square roots render correctly
- [ ] Sub/superscripts render correctly
- [ ] `--html` flag added to `math` CLI command
- [ ] MathLive reference generator script created
- [ ] HTML comparison tests pass

### Phase 2: Full Document Math Support
- [ ] `MathRenderMode` enum added to `HtmlOutputOptions`
- [ ] `render_math_html()` updated to use `render_texnode_to_html()`
- [ ] `--math-html` / `--math-svg` CLI flags added
- [ ] Default math rendering changed to HTML+CSS
- [ ] End-to-end test: `.tex` with math → HTML with rendered formulas

---

## 9. Full LaTeX Document HTML with Math Support

### Current State

Currently, `lambda convert doc.tex -t html` converts LaTeX documents to HTML, but math is rendered via:
1. **SVG output** (via `svg_render_math_inline()`) - vector graphics
2. **Fallback** - escaped LaTeX source in `<span class="math-fallback">`

From `tex_doc_model_html.cpp`:

```cpp
if (has_svg) {
    const char* svg = svg_render_math_inline(elem->math.node, temp_arena, &svg_params);
    strbuf_append_str(out, svg);
} else if (elem->math.latex_src) {
    // Fallback: output escaped LaTeX
    html_escape_append(out, elem->math.latex_src, strlen(elem->math.latex_src));
}
```

### Goal: Native HTML Math Rendering

Add a third option using our new `render_texnode_to_html()`:

```cpp
// New rendering option in HtmlOutputOptions
enum MathRenderMode {
    MATH_RENDER_SVG,    // Current: SVG graphics
    MATH_RENDER_HTML,   // NEW: Native HTML+CSS like MathLive
    MATH_RENDER_FALLBACK // Escaped LaTeX source
};

struct HtmlOutputOptions {
    // ... existing fields ...
    MathRenderMode math_mode = MATH_RENDER_HTML;  // New default
};
```

### Implementation in `render_math_html()`

```cpp
static void render_math_html(DocElement* elem, StrBuf* out,
                             const HtmlOutputOptions& opts, int depth) {
    if (opts.math_mode == MATH_RENDER_HTML && elem->math.node) {
        // NEW: Use render_texnode_to_html() 
        char* html = render_texnode_to_html(elem->math.node, arena);
        strbuf_append_str(out, html);
    } else if (opts.math_mode == MATH_RENDER_SVG && elem->math.node) {
        // Existing SVG path
        const char* svg = svg_render_math_inline(elem->math.node, ...);
        strbuf_append_str(out, svg);
    } else if (elem->math.latex_src) {
        // Fallback: escaped source
        html_escape_append(out, elem->math.latex_src, ...);
    }
}
```

### CLI Integration

```bash
# Convert LaTeX doc to HTML with native math rendering (new default)
./lambda.exe convert paper.tex -t html -o paper.html

# Explicitly specify math rendering mode
./lambda.exe convert paper.tex -t html --math-html -o paper.html   # HTML+CSS
./lambda.exe convert paper.tex -t html --math-svg -o paper.html    # SVG graphics

# View rendered document
./lambda.exe view paper.tex   # Converts to HTML, opens in browser
```

### Benefits

| Aspect | SVG | HTML+CSS |
|--------|-----|----------|
| **File size** | Larger (paths) | Smaller (text) |
| **Selectable** | No | Yes ✓ |
| **Copy/paste** | No | Yes ✓ |
| **Searchable** | No | Yes ✓ |
| **Accessible** | Limited | Better ✓ |
| **Zoom quality** | Perfect | Perfect |

---

## 10. Future Extensions

- MathML output (accessibility)
- SVG output (vector graphics) 
- Interactive editing (cursor, selection)
