# Lambda Math Package Proposal

> **Location:** `lambda/package/math/`  
> **Reference:** MathLive (`ref/mathlive/`), Chart package (`lambda/package/chart/`)  
> **Goal:** Turn LaTeX math into static (and eventually editable) HTML, written entirely in Lambda Script

---

## 1. Project Goals & Scope

### 1.1 Primary Goal

Build a **pure Lambda Script package** at `lambda/package/math/` that converts a LaTeX math AST (already produced by tree-sitter-latex-math) into MathLive-compatible HTML elements. The package takes parsed Lambda elements as input and produces `<span>` element trees as output, which can be serialized to HTML via `format(result, 'html')`.

### 1.2 Why a Lambda Package?

The existing C++ pipeline (`tex_math_bridge → tex_math_ast_typeset → tex_html_render`) hardcodes the entire flow in ~5,000 lines of C++. A Lambda package offers:

- **Prototyping velocity** — iterate on rendering logic without recompiling
- **User extensibility** — users can import the package and customize rendering
- **Consistency** — follows the same module pattern as the chart package
- **Editability path** — Lambda's functional data model naturally supports immutable snapshots + incremental edits (matching MathLive's Atom tree philosophy)

### 1.3 Scope

| In Scope | Out of Scope (for now) |
|----------|----------------------|
| LaTeX math AST → static HTML elements | Interactive editing / cursor / selection |
| MathLive-compatible CSS class structure | IME input, virtual keyboard |
| Core constructs (fracs, roots, scripts, delimiters, accents, matrices, big ops) | Full LaTeX document processing |
| Reuse tree-sitter-latex-math parsed AST | Parsing LaTeX (already done in C++) |
| Port mathlive snapshot tests for verification | Visual regression testing |
| Inline and display math styles | MathML output |

### 1.4 Replaces C++ Math Typesetter

This package **replaces** the existing C++ math-to-HTML pipeline (`tex_math_bridge` → `tex_math_ast_typeset` → `tex_html_render`, ~5,000 lines of C++ across `lambda/tex/`). Once feature-complete, the C++ HTML renderer for math will be deprecated. The C++ pipeline may still be maintained for DVI/PDF output paths, but all HTML math rendering will go through this Lambda package.

---

## 2. Feasibility Study

### 2.1 What Lambda Already Provides

| Capability | Status | Details |
|-----------|--------|---------|
| **Tree-sitter LaTeX math parser** | ✅ Ready | Dedicated grammar at `lambda/tree-sitter-latex-math/grammar.js` (538 lines) with its own compiled library (`libtree-sitter-latex-math.a`). Separate from the document-level `tree-sitter-latex` grammar. Parses math-mode content into 40+ node types. `input-latex-ts.cpp` function `parse_math_to_ast()` creates a `TSParser`, sets the `tree_sitter_latex_math()` language, parses the math string, then `convert_math_node()` recursively converts the tree-sitter CST into Lambda elements with named attributes (`numer`, `denom`, `base`, `sub`, `sup`, etc.) |
| **Element construction** | ✅ Ready | Lambda elements (`<span class: "ML__mathit"; "x">`) map directly to HTML spans. The chart package demonstrates complex SVG element construction in pure Lambda |
| **Module system** | ✅ Ready | `import`/`pub` system proven by the chart package (12 modules, qualified access) |
| **HTML formatter** | ✅ Ready | `format(element, 'html')` serializes Lambda elements to HTML strings |
| **Pattern matching** | ✅ Ready | `match` expressions dispatch on element tag names (`case "fraction": ...`) |
| **Element traversal** | ✅ Ready | `name(el)`, `el.attr`, `el[i]`, `len(el)` — sufficient for walking the math AST |
| **String concatenation** | ✅ Ready | `++` operator for building CSS class strings |
| **Math utilities** | ✅ Ready | `round()`, `floor()`, `ceil()`, `max()`, `min()` — needed for metric calculations |
| **Map construction** | ✅ Ready | Context objects can be passed as maps: `{style: 'display', size: 1.0, color: "black"}` |
| **For comprehensions** | ✅ Ready | `for (child in children) render(child, ctx)` — natural for recursive rendering |
| **Recursive functions** | ✅ Ready | Essential for tree-walking; Lambda supports full recursion |

### 2.2 Language Features Assessment

#### Must-Have (required before starting)

| Feature | Status | Notes |
|---------|--------|-------|
| **Float arithmetic** | ✅ Available | Font metrics are all in `em` units (floats). Lambda has full float support |
| **String formatting for numbers** | ⚠️ Partial | Need `fmt_num(x, decimals)` — chart package has one in `util.ls` that can be imported or replicated |
| **Element attribute access by name** | ✅ Available | `el.numer`, `el.denom`, `el.base` etc. work for tree-sitter-produced AST elements |
| **Conditional element attributes** | ⚠️ Needs verification | Need to conditionally include `style:` attributes on elements. Currently doable via map spread or conditional construction |

#### Good-to-Have (would improve implementation quality)

| Feature                                       | Impact | Notes                                                                                                                                                        |
| --------------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Default map merging**                       | Medium | Context inheritance (`{...parent_ctx, style: 'script'}`) — verify spread works in maps                                                                       |
| **`let` bindings in `match` arms**            | Medium | Complex match arms with intermediate bindings. Confirmed working per language spec                                                                           |
| **Multi-line string literals**                | Low    | For embedding CSS constants. Already supported                                                                                                               |
| **Tail-call optimization**                    | Low    | TCO confirmed supported. Deep recursive math nesting is safe                                                                                                 |
| **Named element construction from variables** | High   | Confirmed working — chart package does `<rect x: x, y: y>`. Dynamic attrs also via `<el (map_expr), c: d; ...>`                                             |
| **Dynamic map construction**                  | Low    | `{a: b, (map_expr), ...}` — merge maps inline. Useful for building attr maps conditionally                                                                  |
| **`map()` for lookup tables**                 | Low    | `map([k, v, k, v, ...])` constructor builds maps from flat key-value lists. Ideal for symbol tables                                                          |

#### Feature Gap Analysis

| Gap                                       | Severity | Workaround                                                                                                                        |
| ----------------------------------------- | -------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **No string `repeat(n)`**                 | Low      | For constructing stretchy delimiters. Can implement as recursive function                                                         |
| **No `char_code()` / `from_char_code()`** | Low      | For Unicode codepoint manipulation. Can pre-build lookup maps instead                                                             |
| **No mutable accumulator (by design)**    | Low      | Pure functional — use `for` comprehensions + element concatenation. Not a gap, it's the design philosophy                        |

### 2.3 Architecture Validation

The chart package proves the architecture is viable:

```
chart package:   <chart> element → parse → compute scales → build <svg> elements → format('html')
math package:    <math> AST      → parse → compute metrics → build <span> elements → format('html')
```

Both follow the same pattern: **input element tree → functional transformation → output element tree**.

### 2.4 Key Risk: Font Metrics

MathLive uses detailed font metrics (height, depth, italic correction, kern pairs) from TeX fonts to compute precise layout. The Lambda package has two options:

1. **Simplified metrics** — Use a compact lookup table of essential metrics (character heights/depths for ~200 key symbols). Sufficient for correct structure, may have minor spacing differences.
2. **Full metrics** — Port MathLive's `font-metrics.ts` σ/ξ parameters as a Lambda map constant. ~50 named constants.

**Recommendation:** Start with option 2 (full metrics as constants). It's a one-time data entry that pays dividends in rendering accuracy.

---

## 3. Package Design

### 3.1 Module Structure

```
lambda/package/math/
├── math.ls              # Main entry point: pub fn render(ast), pub fn render_latex(str)
├── parse.ls             # AST normalization: tree-sitter elements → internal representation
├── context.ls           # Rendering context: math style, font, color, size
├── box.ls               # Box model: create_box(), vbox(), hbox(), skip_box()
├── render.ls            # Core renderer: atom → box tree dispatch
├── atoms/
│   ├── fraction.ls      # \frac, \dfrac, \tfrac, \cfrac, \binom
│   ├── radical.ls       # \sqrt, \sqrt[n]{}
│   ├── scripts.ls       # Superscript, subscript, limits
│   ├── delimiter.ls     # \left...\right, \big..\Big, sized delimiters
│   ├── accent.ls        # \hat, \vec, \bar, \overline, \overbrace, etc.
│   ├── operator.ls      # \sum, \prod, \int (extensible symbols + big ops)
│   ├── array.ls         # matrix, pmatrix, bmatrix, cases, aligned, array
│   ├── text.ls          # \text{}, \textrm{}, \mbox{}
│   ├── style.ls         # \mathbf, \mathrm, \mathbb, \mathcal, \displaystyle
│   ├── color.ls         # \textcolor, \colorbox, \color
│   ├── spacing.ls       # \quad, \qquad, \, \: \; \! \hspace \kern
│   └── enclose.ls       # \boxed, \fbox, \cancel (future)
├── symbols.ls           # Symbol tables: command name → Unicode character
├── metrics.ls           # Font metrics: σ/ξ TeX parameters, char dimensions
├── spacing_table.ls     # Inter-atom spacing rules (TeX 8×8 table)
├── css.ls               # CSS class name builder, default stylesheet string
└── util.ls              # Numeric helpers, em conversion
```

### 3.2 Data Flow

```
                    ┌─────────────────────────────────────┐
                    │           Lambda Runtime             │
                    │                                      │
  LaTeX string ──→  │  input("...", {type:'math',          │
  "\\frac{a}{b}"   │         flavor:'latex'})             │
                    │           │                          │
                    │    tree-sitter-latex-math            │
                    │           │                          │
                    │    <math                             │
                    │      <fraction cmd: "\\frac"         │
                    │        numer: <group "a">            │
                    │        denom: <group "b">            │
                    │      >                               │
                    │    >                                  │
                    │           │                          │
                    │     math.render(ast)                 │
                    │           │                          │
                    │    <span class: "ML__latex"          │
                    │      <span class: "ML__strut" ...>   │
                    │      <span class: "ML__mfrac" ...>   │
                    │      ...                             │
                    │    >                                  │
                    │           │                          │
                    │    format(result, 'html')            │
                    └───────────│──────────────────────────┘
                                ↓
              <span class="ML__latex">...</span>
```

### 3.3 Core Abstractions

#### Context (context.ls)

```lambda
// rendering context — passed down during tree walk
// immutable: child contexts are new maps with overridden fields
pub fn make_context(parent_ctx, overrides) {
    // merge parent with overrides, compute derived values
    let style = overrides.style or parent_ctx.style   // 'display' | 'text' | 'script' | 'scriptscript'
    let size = compute_size(style)                     // scale factor
    let color = overrides.color or parent_ctx.color
    {style: style, size: size, color: color,
     font: overrides.font or parent_ctx.font,
     class_prefix: "ML"}
}
```

#### Box (box.ls)

A box is a Lambda map with rendering metadata, wrapping an element:

```lambda
// a box is: {element: <span ...>, height: float, depth: float, width: float, type: symbol}
pub fn make_box(element, height, depth, width, box_type) =>
    {element: element, height: height, depth: depth,
     width: width, type: box_type}

pub fn vbox(elements, options) {
    // build ML__vlist-t structure with pstrut and table layout
    // returns a box wrapping the vlist <span> tree
}

pub fn skip_box(width_em) =>
    make_box(<span style: "display:inline-block;width:" ++ fmt(width_em) ++ "em">,
             0.0, 0.0, width_em, 'skip')
```

#### Renderer (render.ls)

```lambda
pub fn render_atom(node, ctx) {
    match name(node) {
        case "math":       render_math(node, ctx)
        case "fraction":   fraction.render(node, ctx)
        case "radical":    radical.render(node, ctx)
        case "subsup":     scripts.render(node, ctx)
        case "delimiter_group": delimiter.render(node, ctx)
        case "accent":     accent.render(node, ctx)
        case "big_operator": operator.render(node, ctx)
        case "environment": array.render(node, ctx)
        case "text_command": text.render(node, ctx)
        case "style_command": style.render(node, ctx)
        case "command":    render_command(node, ctx)
        case "operator":   render_binary_op(node, ctx)
        case "relation":   render_relation(node, ctx)
        case "space_command": spacing.render(node, ctx)
        default:           render_default(node, ctx)
    }
}
```

### 3.4 CSS Strategy

**Embedded CSS** — The MathLive core stylesheet (~810 lines of Less, compiled to ~600 lines CSS) is embedded as a string constant in `css.ls`. A `pub fn get_stylesheet()` returns it. This keeps the package self-contained and simplifies testing — no external file dependencies. The CSS will be moved to an external file when the implementation is fully tested.

The `render()` function optionally wraps output in a `<style>` + `<span>` for standalone HTML:

```lambda
// standalone mode: includes CSS inline
math.render(ast, {standalone: true})
// → <span><style>.ML__latex{...}</style><span class="ML__latex">...</span></span>

// fragment mode (default): just the math element, CSS assumed external
math.render(ast)
// → <span class="ML__latex">...</span>
```

Key classes reused from MathLive: `ML__latex`, `ML__strut`, `ML__base`, `ML__mathit`, `ML__cmr`, `ML__mfrac`, `ML__frac-line`, `ML__sqrt`, `ML__sqrt-sign`, `ML__sqrt-line`, `ML__vlist-t`, `ML__vlist-r`, `ML__vlist`, `ML__pstrut`, etc.

### 3.5 Symbol Tables (symbols.ls)

```lambda
// LaTeX command → Unicode character mapping
pub let greek_lower = {
    alpha: "α", beta: "β", gamma: "γ", delta: "δ",
    epsilon: "ε", zeta: "ζ", eta: "η", theta: "θ",
    iota: "ι", kappa: "κ", lambda: "λ", mu: "μ",
    // ... ~50 entries
}

pub let operators = {
    pm: "±", mp: "∓", times: "×", cdot: "⋅",
    leq: "≤", geq: "≥", neq: "≠", equiv: "≡",
    // ... ~100 entries
}

pub fn lookup_command(cmd) {
    greek_lower[cmd] or greek_upper[cmd] or
    operators[cmd] or arrows[cmd] or
    misc_symbols[cmd] or cmd   // fallback: raw command name
}
```

### 3.6 Font Metrics (metrics.ls)

```lambda
// TeX font metric parameters (sigma values from MathLive's font-metrics.ts)
pub let sigma = {
    slant: 0.0,                  // σ1
    space: 0.0,                  // σ2
    stretch: 0.0,                // σ3
    shrink: 0.0,                 // σ4
    xHeight: 0.431,              // σ5
    quad: 1.0,                   // σ6
    extraSpace: 0.0,             // σ7
    num1: 0.677,                 // σ8 — numerator shift (display)
    num2: 0.394,                 // σ9 — numerator shift (inline)
    num3: 0.444,                 // σ10
    denom1: 0.686,               // σ11 — denominator shift (display)
    denom2: 0.345,               // σ12 — denominator shift (inline)
    sup1: 0.413,                 // σ13 — superscript shift
    sup2: 0.363,                 // σ14
    sup3: 0.289,                 // σ15
    sub1: 0.15,                  // σ16 — subscript shift
    sub2: 0.247,                 // σ17
    supDrop: 0.386,              // σ18
    subDrop: 0.05,               // σ19
    delim1: 2.39,                // σ20
    delim2: 1.01,                // σ21
    axisHeight: 0.25,            // σ22
    // ... additional xi parameters
}

// math style scale factors
pub let style_scale = {
    display: 1.0,
    text: 1.0,
    script: 0.7,
    scriptscript: 0.5
}
```

### 3.7 Editability-Ready Design

Even though Phase 1 is static-only, the design anticipates editing:

1. **Atom identity** — Each rendered box carries a `data-atom-id` attribute (integer index into a flat atom list), matching MathLive's approach for hit-testing.

2. **Immutable snapshots** — Lambda's functional model means the rendered tree is a pure function of the AST. Editing = produce a new AST → re-render. No mutable state.

3. **Structural elements** — Placeholder atoms (`<span class="ML__placeholder">`) are rendered for empty groups, providing future edit targets.

4. **Source mapping** — Each rendered box optionally carries the original LaTeX source as a `data-latex` attribute, enabling roundtrip editing.

---

## 4. Implementation Phases

### Phase 1: Foundation (Weeks 1–2)

**Goal:** Render simple expressions (`a + b`, `x^2`, `\frac{a}{b}`)

| Module | Deliverable |
|--------|-------------|
| `math.ls` | Entry point: `render(ast)` → `<span class="ML__latex">` wrapper with struts |
| `context.ls` | Basic context: display/text/script/scriptscript style, size scaling |
| `box.ls` | `make_box()`, `skip_box()`, `hbox()` (horizontal concatenation) |
| `render.ls` | Dispatch on AST node type, render ordinary symbols (mord) |
| `symbols.ls` | Greek letters, basic operators, relations |
| `metrics.ls` | σ parameters, style scale factors |
| `spacing_table.ls` | 8×8 inter-atom spacing table |
| `css.ls` | Class name builder, embedded stylesheet |
| `util.ls` | `fmt_em()`, numeric helpers |
| `atoms/scripts.ls` | Superscript/subscript rendering |
| `atoms/fraction.ls` | `\frac` rendering with vbox |

**Tests:** Verify against mathlive snapshot tests for: basic symbols, fractions, superscripts/subscripts.

### Phase 2: Core Constructs (Weeks 3–4)

**Goal:** Cover the most common math constructs

| Module | Deliverable |
|--------|-------------|
| `box.ls` | `vbox()` — full MathLive-compatible vlist structure |
| `atoms/radical.ls` | `\sqrt`, `\sqrt[n]{}` with SVG surd sign |
| `atoms/delimiter.ls` | `\left...\right`, sized delimiters (`\big`, `\Big`, etc.) |
| `atoms/accent.ls` | `\hat`, `\vec`, `\bar`, `\overline`, `\widehat`, etc. |
| `atoms/operator.ls` | `\sum`, `\prod`, `\int` with limits placement |
| `atoms/spacing.ls` | `\quad`, `\,`, `\:`, `\;`, `\!`, `\hspace`, `\kern` |
| `atoms/text.ls` | `\text{}`, `\textrm{}`, `\mbox{}` |

**Tests:** mathlive snapshot tests for: fractions (advanced), surds, accents, delimiters, big operators, spacing.

### Phase 3: Environments & Styling (Weeks 5–6)

**Goal:** Matrices, cases, font styles, colors

| Module | Deliverable |
|--------|-------------|
| `atoms/array.ls` | `matrix`, `pmatrix`, `bmatrix`, `vmatrix`, `cases`, `aligned`, `array` |
| `atoms/style.ls` | `\mathbf`, `\mathrm`, `\mathbb`, `\mathcal`, `\displaystyle`, `\operatorname` |
| `atoms/color.ls` | `\textcolor`, `\color`, `\colorbox` — named colors + hex |
| `atoms/enclose.ls` | `\boxed`, `\fbox` |

**Tests:** mathlive snapshot tests for: environments, sizing, colors, fonts.

### Phase 4: Polish & Compatibility (Weeks 7–8)

**Goal:** Edge cases, full test coverage, documentation

| Task | Deliverable |
|------|-------------|
| Inter-atom spacing | Correct TeX spacing rules between all atom type pairs |
| Box coalescing | Merge adjacent spans with identical classes (optimization) |
| Delimiter stretching | SVG-based extensible delimiters for tall expressions |
| Error rendering | Unknown commands rendered with `ML__error` class |
| Documentation | Usage guide, API reference, architecture doc |
| Test suite | Comprehensive Lambda test scripts + mathlive test port |

### Future Phases (Post v1)

| Phase | Description |
|-------|-------------|
| **Editing support** | Cursor model, selection, keyboard input |
| **Bidirectional** | LaTeX → AST → HTML → edits → AST → LaTeX roundtrip |
| **MathML output** | Alternative output format for accessibility |
| **Custom macros** | User-defined `\newcommand` support |

---

## 5. Test Strategy

### 5.1 Porting MathLive Tests

MathLive's `test/markup.test.ts` contains ~30 test groups with LaTeX → HTML snapshot tests. These can be ported as Lambda integration tests:

```lambda
// test/lambda/package/test_math_fractions.ls
import math: .lambda.package.math.math

// Test: \frac{a}{b}
let ast = input("\\frac{a}{b}", {type: 'math', flavor: 'latex'})
let html = math.render(ast)
format(html, 'html')
```

Expected output files (`test/lambda/package/test_math_fractions.txt`) contain the HTML string to match.

### 5.2 Test Categories (from MathLive)

| Category | # Tests | Priority |
|----------|---------|----------|
| Fractions | 9 | P1 |
| Superscript/subscript | 3 | P1 |
| Surds (roots) | 5 | P1 |
| Accents | 10 | P2 |
| Delimiters (left/right) | 26+ | P2 |
| Delimiter sizing | 4 | P2 |
| Environments | 9+ | P2 |
| Colors | 10+ | P3 |
| Fonts | 5 | P3 |
| Spacing/kern | 10 | P3 |
| Binary operators | 10 | P1 |
| Not/negation | 10 | P3 |
| Rules/dimensions | 15 | P3 |
| Box commands | 10 | P3 |
| Over/underline | 2 | P2 |
| Mode shift | 2 | P3 |
| Extensions | 8 | P3 |

### 5.3 Comparison Approach

Since our HTML output may differ from MathLive in minor ways (whitespace, attribute order), use two comparison levels:

1. **Structural match** — Parse both HTML outputs back to elements and compare tree structure (tag names, key classes, text content)
2. **Visual match** — Render both in a browser and compare screenshots (Phase 4)

---

## 6. Questions & Suggestions

### 6.1 Open Questions

1. **Math input interface** — When a user calls `input("...", {type:'math', flavor:'latex'})`, the current C++ code in `input-math.cpp` produces a simple map `{type: 'latex-math', source: "..."}`. But the tree-sitter path in `input-latex-ts.cpp` (`parse_math_to_ast()`) produces rich structured AST elements. The math package needs the rich AST. **Decision needed:** modify `input-math.cpp` to route LaTeX flavor through `parse_math_to_ast()` by default, or add a separate input flavor.

2. **AST completeness** — 15 grammar node types currently fall through to the generic default handler in `convert_math_node()`, losing their named field attributes (see §6.3). These should be fixed in C++ before starting the Lambda package, since the package relies on well-structured AST input.

### 6.2 Unicode-Only vs KaTeX Fonts: Analysis

MathLive uses 12 KaTeX font families (20 WOFF2 files, ~296 KB total). Here is the comparison:

#### Unicode-Only (system fonts)

| Pros | Cons |
|------|------|
| Zero font loading — instant render | Missing glyphs: calligraphic (𝒜ℬ𝒞), script (𝒜ℬ), Fraktur (𝔄𝔅), Size1–4 delimiter variants |
| No external dependencies | Wrong metrics: superscript placement, fraction bar alignment, spacing all depend on TeX-specific glyph dimensions |
| Smaller payload, cacheable by browser | No extensible delimiters: tall `(`, `[`, `{` around matrices/fractions won't scale |
| Works offline, in emails, markdown | Large operators (∑∏∫) won't have display-size variants |
| Easier to maintain | Accents (ˆ˜) won't stretch over multi-character expressions |

#### KaTeX Fonts (bundled or CDN)

| Pros | Cons |
|------|------|
| Pixel-perfect TeX rendering | 296 KB font download (WOFF2, one-time cached) |
| All glyphs available including Private Use Area | Dependency on font files (CDN or bundled) |
| Precise metrics for every character (height, depth, width, italic correction, skew) | Font loading flash (FOUT) |
| Extensible delimiters via Size1–4 stacking pieces | More complex CSS (@font-face declarations) |
| Display-size large operators (∑ in display mode is larger than inline) | |
| Calligraphic, Script, Fraktur alphabets render correctly | |

#### What breaks without KaTeX fonts

| Feature | Severity | Detail |
|---------|----------|--------|
| `\left( \frac{...}{...} \right)` with tall content | **Critical** | Size1–4 fonts provide progressively larger delimiters; system fonts have only one size |
| `\mathcal{L}`, `\mathscr{F}`, `\mathfrak{g}` | **High** | Entirely missing font families; fallback is generic cursive/serif |
| `\sum`, `\prod`, `\int` in display mode | **High** | Display-size variants live in Size1/Size2 fonts |
| Superscript/subscript positioning | **Medium** | Italic correction and skew values differ from system fonts |
| `\widehat{ABC}`, `\widetilde{xyz}` | **Medium** | Multi-width accent variants in Size1–4 |
| Inter-atom spacing | **Low** | Widths slightly off → cumulative spacing drift |

#### Recommendation

**Phase 1–2: KaTeX fonts from CDN.** This gives pixel-perfect rendering from day one and makes test comparison against MathLive snapshots feasible. The CSS `@font-face` declarations are a one-time addition to the embedded stylesheet.

**Future option:** Provide a `{fonts: 'unicode'}` render option that skips KaTeX fonts for lightweight embedding contexts (emails, markdown preview) where approximate rendering is acceptable.

### 6.3 Unhandled AST Node Types (15 total)

The following node types in `tree-sitter-latex-math/grammar.js` fall through to the **generic default handler** in `convert_math_node()` (line ~600 of `input-latex-ts.cpp`). The default handler creates an element with the node type as tag name and recursively converts children, but **loses all named field attributes** — the children become anonymous positional items instead of named fields like `numer`, `cmd`, `base`, etc.

| # | Node Type | Grammar Fields Lost | Example LaTeX | Priority |
|---|-----------|--------------------|--------------|---------|
| 1 | `genfrac` | `left_delim`, `right_delim`, `thickness`, `style`, `numer`, `denom` | `\genfrac{(}{)}{0pt}{}{n}{k}` | Low (rare) |
| 2 | `infix_frac` | `numer`, `cmd`, `denom` | `a \over b`, `n \choose k` | Medium |
| 3 | `overunder_command` | `cmd`, `annotation`, `base` | `\overset{n}{X}`, `\underset{k}{Y}` | Medium |
| 4 | `extensible_arrow` | `cmd`, `below`, `above` | `\xrightarrow{f}`, `\xleftarrow[g]{h}` | Medium |
| 5 | `sized_delimiter` | `size`, `delim` | `\big(`, `\Big\{`, `\bigg[` | High |
| 6 | `color_command` | `cmd`, `color`, `content` | `\textcolor{red}{x}`, `\colorbox{yellow}{y}` | High |
| 7 | `box_command` | `cmd`, `options`, `content` | `\boxed{E=mc^2}`, `\fbox{text}` | Medium |
| 8 | `phantom_command` | `cmd`, `options`, `content` | `\phantom{x}`, `\vphantom{y}` | Low |
| 9 | `mathop_command` | `content` | `\mathop{lim\,sup}` | Low |
| 10 | `matrix_command` | `cmd`, `body` | `\pmatrix{a & b \cr c & d}` (plain TeX) | Low |
| 11 | `hspace_command` | `cmd`, `sign`, `value`, `unit` | `\hspace{1em}`, `\hspace*{2cm}` | Medium |
| 12 | `skip_command` | `cmd`, `sign`, `value`, `unit` | `\kern 3pt`, `\hskip 1em` | Medium |
| 13 | `middle_delim` | `delim` | `\left( a \middle\| b \right)` | Medium |
| 14 | `limits_modifier` | (leaf — just `\limits`/`\nolimits` text) | `\sum\limits_{i=0}` | Medium |
| 15 | `symbol_command` | (leaf — commands like `\infty`, `\partial`, `\nabla`) | `\infty`, `\forall`, `\exists` | **High** |

**Impact:** Items #5 (`sized_delimiter`), #6 (`color_command`), and #15 (`symbol_command`) are high-priority because they are commonly used. `symbol_command` is especially critical — `\infty`, `\partial`, `\forall`, `\exists`, `\nabla`, `\cdots`, `\ldots` all fall through to the default handler and lose their semantic identity.

**Recommendation:** Fix all 15 in `convert_math_node()` before starting the Lambda package. Each fix is ~10–20 lines of C++ (extract named fields via `ts_node_child_by_field_name()` and set them as element attributes). Estimated effort: ~2 hours total.

### 6.4 Suggestions

1. **Fix the 15 unhandled AST node types first** (see §6.3). This is a prerequisite — the Lambda package should receive clean, well-structured ASTs.

2. **Route `input("...", {type:'math', flavor:'latex'})` through `parse_math_to_ast()`** — modify `input-math.cpp` so the default LaTeX path produces the rich tree-sitter AST instead of the simple `{type: 'latex-math', source: "..."}` map. The package needs structured AST input.

3. **Start with a "golden set" of 20 expressions** — Rather than porting all mathlive tests at once, start with 20 representative expressions that cover every atom type:

    ```
    x + y                           # basic symbols + binary op
    x^2                             # superscript
    x_i                             # subscript
    x_i^2                           # combined sub/sup
    \frac{a}{b}                     # fraction
    \sqrt{x}                        # root
    \sqrt[3]{x}                     # nth root
    \sum_{i=0}^{n} x_i              # big operator with limits
    \int_0^1 f(x) dx               # integral
    \left( \frac{a}{b} \right)      # auto-delimiters
    \hat{x}                         # accent
    \overline{AB}                   # overline
    \begin{pmatrix} a & b \\ c & d \end{pmatrix}  # matrix
    \begin{cases} x & y \end{cases} # cases
    \alpha + \beta                  # greek
    \mathbf{A} \cdot \mathbf{B}     # font style
    \textcolor{red}{x}              # color
    a \quad b                       # spacing
    \text{for all } x               # text mode
    \binom{n}{k}                    # binomial
    ```

4. **Package auto-discovery** — Currently `lambda/package/chart/` is imported via `import chart: .lambda.package.chart.chart`. Consider if there should be a package registry or convention (`import 'math'` resolving to `lambda/package/math/math.ls`).

5. **Consider a `render_latex(string)` convenience function** — That calls `input()` + `render()` in one step:

    ```lambda
    import math: .lambda.package.math.math
    math.render_latex("\\frac{a}{b}")   // → HTML element tree
    ```

---

## 7. Dependency Summary

### Required (already available)

| Dependency | Source |
|-----------|--------|
| Tree-sitter-latex-math parser | `lambda/tree-sitter-latex-math/grammar.js` + `input-latex-ts.cpp` |
| Lambda element system | Core runtime |
| HTML formatter | `lambda/format/format-html.cpp` |
| Lambda module system | `import`/`pub` |

### Reference (read-only)

| Reference | Purpose |
|-----------|---------|
| MathLive source (`ref/mathlive/src/`) | Rendering algorithms, CSS classes, font metrics |
| MathLive CSS (`ref/mathlive/css/core.less`) | Stylesheet to port |
| MathLive tests (`ref/mathlive/test/markup.test.ts`) | Test cases to port |
| Existing C++ HTML renderer (`lambda/tex/tex_html_render.cpp`) | Already MathLive-compatible; reference for VList structure |
| Existing MathLive analysis (`vibe/MathLive_Analysis.md`, `vibe/Mathlive.md`) | Architecture study |

### New (to create)

| Artifact | Location |
|----------|----------|
| Math package modules | `lambda/package/math/*.ls` |
| Test scripts | `test/lambda/package/test_math_*.ls` |
| Expected outputs | `test/lambda/package/test_math_*.txt` |

---

## 8. Success Criteria

| Metric | Target |
|--------|--------|
| Golden set (20 expressions) | 100% structurally correct HTML |
| MathLive test categories P1 | ≥ 90% output match |
| MathLive test categories P2 | ≥ 80% output match |
| Package size | ≤ 15 modules, ≤ 3000 lines total |
| Render performance | < 10ms per expression (interpreted) |
| C++ AST fixes complete | All 15 unhandled node types produce named attributes |
| Replaces C++ HTML path | `tex_html_render.cpp` no longer needed for math HTML |

---

## Appendix A: MathLive Rendering Pipeline (Reference)

```
LaTeX string
    │
    ▼
Tokenizer (tokenizer.ts)
    │  tokens
    ▼
Parser (parser.ts)
    │  Atom tree
    ▼
Atom.render(context) ──→ Box tree
    │                     ├── Box (horizontal span)
    │                     ├── VBox (vertical stack, table layout)
    │                     ├── SkipBox (inline-block spacer)
    │                     └── SvgBox (stretchy symbols)
    ▼
applyInterBoxSpacing()
    │
    ▼
coalesce() ──→ merge adjacent same-class spans
    │
    ▼
makeStruts() ──→ add ML__strut / ML__strut--bottom
    │
    ▼
Box.toMarkup() ──→ HTML string
```

## Appendix B: AST Node → Atom Type Mapping

| Tree-sitter AST Node | MathLive Atom Type | Lambda Package Module |
|-----------------------|-------------------|-----------------------|
| `symbol` (letter) | `mord` | `render.ls` |
| `number` | `mord` | `render.ls` |
| `operator` | `mbin` | `render.ls` |
| `relation` | `mrel` | `render.ls` |
| `punctuation` | `mpunct` | `render.ls` |
| `subsup` | `subsup` | `atoms/scripts.ls` |
| `fraction` | `genfrac` | `atoms/fraction.ls` |
| `binomial` | `genfrac` | `atoms/fraction.ls` |
| `radical` | `surd` | `atoms/radical.ls` |
| `delimiter_group` | `leftright` | `atoms/delimiter.ls` |
| `accent` | `accent` | `atoms/accent.ls` |
| `big_operator` | `extensible-symbol` | `atoms/operator.ls` |
| `environment` | `array` | `atoms/array.ls` |
| `text_command` | `text` | `atoms/text.ls` |
| `style_command` | `group` (styled) | `atoms/style.ls` |
| `space_command` | `spacing` | `atoms/spacing.ls` |
| `command` (greek) | `mord` | `symbols.ls` |
| `group` | `group` | `render.ls` |
| `color_command` | `group` (colored) | `atoms/color.ls` |
| `box_command` | `box` | `atoms/enclose.ls` |

## Appendix C: Inter-Atom Spacing Table (from TeX)

```
         ord  op  bin  rel  open close punct inner
ord       0    1   *2    *3   0    0    0     *1
op        1    1    -    *3   0    0    0     1
bin      *2    *2   -     -  *2    -    -    *2
rel      *3    *3   -     0  *3    0    0    *3
open      0    0    -     0   0    0    0     0
close     0    1   *2    *3   0    0    0    *1
punct     *1   *1   -    *1  *1   *1   *1    *1
inner     *1   1   *2    *3  *1    0   *1     *1

0 = no space, 1 = thin (3mu), 2 = medium (4mu), 3 = thick (5mu)
* = space only in display/text style (not script/scriptscript)
- = impossible combination
```

---

## 9. Phase 1 Progress Report

### 9.1 Status: Complete

All Phase 1 deliverables are implemented and passing. 12 modules totaling **1,763 lines** of Lambda Script.

| Module | Lines | Status | Notes |
|--------|-------|--------|-------|
| `math.ls` | 47 | ✅ Done | Entry point: `render_math()`, `render_display()`, `render_inline()`, `render_standalone()`, `stylesheet()` |
| `context.ls` | 123 | ✅ Done | Style derivation (display/text/script/scriptscript), context constructors |
| `box.ls` | 232 | ✅ Done | `make_box`, `text_box`, `skip_box`, `hbox`, `vbox`, `build_vbox`, `make_struts`, `box_cls`, `box_styled` |
| `render.ls` | 469 | ✅ Done | Dispatch on ~30 AST node types, inter-atom spacing insertion, recursive rendering |
| `symbols.ls` | 198 | ✅ Done | Greek letters, operators, relations, arrows, accents, delimiter sizes |
| `metrics.ls` | 102 | ✅ Done | TeX σ/ξ parameters, style-indexed arrays, `at()` accessor |
| `spacing_table.ls` | 77 | ✅ Done | 8×8 inter-atom spacing matrix, atom type classification |
| `css.ls` | 152 | ✅ Done | ~45 CSS class constants, embedded MathLive stylesheet |
| `util.ls` | 72 | ✅ Done | `fmt_em()`, `text_of()`, `map_children()`, type-check helpers |
| `atoms/fraction.ls` | 134 | ✅ Done | `\frac` with display/inline style, vbox numerator/denominator/bar layout |
| `atoms/scripts.ls` | 105 | ✅ Done | Superscript, subscript, combined sub+sup with vlist |
| `atoms/spacing.ls` | 52 | ✅ Done | `\quad`, `\qquad`, `\,`, `\:`, `\;`, `\!`, `\hspace`, `\kern` |

### 9.2 Test Results

Six test expressions all produce correct output with **zero runtime errors**:

| # | Expression | Validates |
|---|-----------|-----------|
| 1 | `x` | Basic symbol → `<span class="ML__mathit">x</span>` |
| 2 | `a + b` | Binary op with inter-atom spacing (mediumspace) |
| 3 | `x^2` | Superscript vlist with correct height (0.363em) |
| 4 | `\frac{a}{b}` (inline) | Fraction with bar, numerator/denominator shifts |
| 5 | `\frac{a}{b}` (display) | Display-style fraction with larger shifts (0.59em) |
| 6 | Standalone render | Full output with embedded CSS stylesheet |

Baseline regression: **328/328** existing tests pass — zero regressions.

### 9.3 Bugs Fixed

Six significant Lambda JIT runtime issues were discovered and worked around during implementation:

#### Bug 1: `type(x) == 'string'` returns `error` instead of `bool`

- **Symptom:** Type dispatch in `render_node` never matched any branch; raw AST elements passed through unrendered.
- **Root cause:** `type()` returns a TYPE value (not a symbol/string). Comparing a TYPE with `==` against a string literal produces `error`, which is falsy, so all branches silently fail.
- **Fix:** Replaced all `type(x) == 'string'` patterns with `x is string`, `x is element`, `x is int`, etc. For null: use `x == null` (not `x is null`).
- **Files changed:** `util.ls` (2 replacements), `render.ls` (3 replacements).

#### Bug 2: `format(x, 'html')` wraps output in DOCTYPE and converts lists to `<ul><li>`

- **Symptom:** Output wrapped in `<!DOCTYPE html><html><body>...` with list children converted to unordered list items.
- **Root cause:** The `'html'` format mode applies full HTML document semantics, including list-to-`<ul>` conversion.
- **Fix:** Use `format(x, 'xml')` for raw element serialization without HTML document wrapping.
- **Files changed:** `math.ls`, test scripts.

#### Bug 3: `match` expressions returning `int` or `float` produce garbage values

- **Symptom:** `style_index()` returning 0/1/2/3 via `match` produced `[unknown]` values. All downstream array indexing and metric lookups failed with `<error>`.
- **Root cause:** Lambda JIT produces incorrect values when a `match` expression returns numeric types (int/float). Match returning strings works correctly.
- **Fix:** Converted all numeric-returning `match` expressions to if-else chains.
- **Files changed:** `metrics.ls` (`style_index`, `style_scale`), `spacing_table.ls` (`atom_type_index`, `get_spacing`), `atoms/spacing.ls` (`parse_dimension`).
- **Status:** ⚠️ **Outstanding JIT bug** — not fixed at the engine level. Workaround in place.

#### Bug 4: Cross-module array indexing causes stack overflow

- **Symptom:** `met.sup1[si]` (accessing an array exported from another module by index) caused infinite recursion / stack overflow.
- **Root cause:** Lambda JIT does not correctly handle indexing into arrays that were obtained via cross-module qualified access (`module.array[i]`). Even `let d = module.array; d[i]` fails.
- **Fix:** Added `pub fn at(arr, i) => arr[i]` accessor in `metrics.ls`. All cross-module array access uses `met.at(met.sup1, si)` instead of `met.sup1[si]`.
- **Files changed:** `metrics.ls` (+1 accessor), `atoms/scripts.ls` (8 replacements), `atoms/fraction.ls` (8 replacements).
- **Status:** ⚠️ **Outstanding JIT bug** — not fixed at the engine level. Workaround in place.

#### Bug 5: Elements passed as function arguments to other modules get corrupted

- **Symptom:** `box.make_box(<span class: "ML__frac-line">)` returned a box whose `.element` was `<root/>` instead of the constructed `<span>`.
- **Root cause:** Lambda elements constructed in module A and passed as arguments to a function in module B get corrupted during cross-module function calls. The element's tag name and attributes are lost.
- **Fix:** Added `box_cls(cls, h, d, w, type)` and `box_styled(cls, style, h, d, w, type)` helper functions in `box.ls` that construct the `<span>` element internally (same module as `make_box`), avoiding cross-module element passing. Replaced all cross-module `box.make_box(<span ...>)` calls.
- **Files changed:** `box.ls` (+2 helpers), `render.ls` (5 replacements), `atoms/fraction.ls` (1 replacement), `atoms/spacing.ls` (11 replacements).
- **Status:** ⚠️ **Outstanding JIT bug** — not fixed at the engine level. Workaround in place.

#### Bug 6: `++` at start of continuation line parsed as unary `+`

- **Symptom:** CSS string concatenation with `++` at the beginning of continuation lines produced compiler warnings and incorrect results.
- **Root cause:** Lambda's parser treats `++` at the start of a line as two unary `+` operators rather than the binary concatenation operator.
- **Fix:** Restructured all multi-line `++` concatenation so the `++` operator appears at the **end** of each line, not the start of the next line.
- **Files changed:** `css.ls` (full stylesheet string restructured).

### 9.4 Lambda Script Syntax Rules Discovered

A comprehensive set of Lambda syntax constraints was identified through trial and error. These rules should be followed in all future Lambda package development:

| #   | Rule                                                                         | Consequence of Violation                          |
| --- | ---------------------------------------------------------------------------- | ------------------------------------------------- |
| 1   | Use `x is string` / `x is element` — never `type(x) == 'string'`             | Silent `error` value, all comparisons fail        |
| 2   | Use `format(x, 'xml')` — never `format(x, 'html')` for element serialization | Unwanted DOCTYPE wrapping, list→`<ul>` conversion |
| 3   | Use if-else chains for `match` returning int/float                           | Garbage / `[unknown]` values from JIT             |
| 4   | Use accessor functions for cross-module array indexing                       | Stack overflow                                    |
| 5   | Construct elements inside the module that returns them                       | Elements corrupted to `<root/>` across modules    |
| 6   | Put `++` at end of line, never at start of continuation                      | Parsed as unary `+`                               |
| 7   | Public constants: `pub name = value` — not `pub let name = value`            | Compile error                                     |
| 8   | Quote `"in"` and `"inf"` as map keys (grammar keywords)                      | Parse error                                       |
| 9   | No `*spread` in elements — use `for (child in list) child`                   | Compile error                                     |
| 10  | `max`, `min`, `sum` not valid pipe targets — use `max(list)`                 | Compile error                                     |
| 11  | No nested `fn` inside function bodies                                        | JIT error                                         |
| 12  | Symmetric if-else: both branches must be blocks or both expressions          | Compile error when mixed                          |
| 13  | Zero-param `pub fn name() => { map }` is ambiguous — use block body          | Returns function, not map                         |

### 9.5 Outstanding Work for Phase 2

Phase 2 modules not yet created (from §4 Phase 2 plan):

| Module | Feature | Priority |
|--------|---------|----------|
| `atoms/radical.ls` | `\sqrt`, `\sqrt[n]{}` with SVG surd | High |
| `atoms/delimiter.ls` | `\left...\right`, `\big`, `\Big`, sized delimiters | High |
| `atoms/accent.ls` | `\hat`, `\vec`, `\bar`, `\overline`, `\widehat` | Medium |
| `atoms/operator.ls` | `\sum`, `\prod`, `\int` with limits | High |
| `atoms/text.ls` | `\text{}`, `\textrm{}`, `\mbox{}` | Medium |

Additionally, the 15 unhandled AST node types in C++ (§6.3) remain unfixed — most notably `symbol_command` (#15), `sized_delimiter` (#5), and `color_command` (#6).
