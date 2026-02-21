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

---

## 10. Phase 2 Progress Report

### 10.1 Status: Complete

Phase 2 rendering support for all core constructs is implemented and working. All 10 Phase 2 test expressions render without crashes. Baseline regression: **328/328** existing tests pass — zero regressions.

### 10.2 C++ Prerequisite Fixes

Two critical C++ issues had to be resolved before Phase 2 rendering could work:

#### C++ Fix 1: Math input returns simple map instead of AST

- **Symptom:** `input("\\sqrt{x}", {type: 'math', flavor: 'latex'})` returned `{type: :latex-math, source: "\\sqrt{x}", display: false}` — a flat map with no parsed structure.
- **Root cause:** `parse_math()` in `input-math.cpp` stored the raw source string instead of routing through the tree-sitter-latex-math parser.
- **Fix:** Modified `input-math.cpp` to call `parse_math_latex_to_ast()` (new public wrapper in `input-latex-ts.cpp`) which creates a `TSParser`, sets the `tree_sitter_latex_math()` language, parses the math string, then calls `convert_math_node()` recursively. Also added the declaration in `input.hpp`.
- **Files changed:** `input-math.cpp`, `input-latex-ts.cpp` (+wrapper function + forward declaration), `input.hpp` (+declaration).
- **Impact:** Unblocked entire Phase 2 — without this, the math package had no structured AST to render.

#### C++ Fix 2: Anonymous token fields return null from `ts_node_child_by_field_name()`

- **Symptom:** `accent.cmd`, `big_operator.op`, `fraction.cmd` were all `null` in the AST despite the grammar defining them as named fields.
- **Root cause:** Fields referencing hidden token rules (`_accent_cmd`, `_big_operator_cmd`, `_frac_cmd`, etc.) use `token()` patterns with `_` prefix in the tree-sitter grammar. Tree-sitter makes these nodes anonymous, and `ts_node_child_by_field_name()` cannot find anonymous nodes — it returns null.
- **Fix:** Added `extract_leading_command()` helper function that extracts the `\command` from the parent node's source text. Applied to all 14 `cmd` field handlers and 1 `op` field handler as an `else` fallback when field lookup returns null.
- **Files changed:** `input-latex-ts.cpp` (+helper function, 15 handler modifications).
- **Impact:** Fixed command extraction for `\hat`, `\vec`, `\bar`, `\sum`, `\int`, `\frac`, `\binom`, `\text`, `\overline`, `\underset`, `\overset`, `\mathbf`, `\color`, `\boxed`, `\phantom`, `\hspace`, `\kern`, and all other command-bearing node types.

### 10.3 Lambda Render Fixes

| File | Fix | Details |
|------|-----|---------|
| `render.ls` | `render_radical`: `node.body` → `node.radicand` | AST uses `radicand` attribute, not `body` |
| `render.ls` | `render_big_op`: `node.cmd` → `node.op` | Big operators store their command in `op`, not `cmd` |
| `render.ls` | `render_big_op`: added limits rendering | Split into `render_big_op_with_limits`, `render_big_op_display`, `render_big_op_inline` helper functions. Display mode stacks limits above/below via `vbox`; inline mode places them beside the operator with `MSUBSUP` class |
| `render.ls` | `render_accent`: now works | Was already correct — just needed the C++ `cmd` field fix to populate `\hat` → accent lookup |

### 10.4 Phase 2 Test Results

All 10 Phase 2 test expressions render without errors:

| # | Expression | File | Validates |
|---|-----------|------|-----------|
| 1 | `\sqrt{x}` | `m1.tex` | Radical with radicand |
| 2 | `\sqrt[3]{x}` | `m2.tex` | Radical with index |
| 3 | `\left( \frac{a}{b} \right)` | `m3.tex` | Auto-sized delimiters |
| 4 | `\hat{x}` | `m4.tex` | Accent with combining character |
| 5 | `\overline{AB}` | `m5.tex` | Overline accent |
| 6 | `\sum_{i=0}^{n} x_i` | `m6.tex` | Big operator with limits |
| 7 | `\int_0^1 f(x) dx` | `m7.tex` | Integral with limits |
| 8 | `\text{for all } x` | `m8.tex` | Text command |
| 9 | `\vec{v}` | `m9.tex` | Vector accent |
| 10 | `\bar{x}` | `m10.tex` | Bar accent |

AST attribute verification:
- `accent.cmd = \hat` ✅ (was `null` before C++ fix)
- `big_operator.op = \sum` ✅ (was `null` before C++ fix)
- `big_operator.lower` populated ✅
- `big_operator.upper` populated ✅
- `fraction.cmd = \frac` ✅ (was `null` before C++ fix)
- `radical.radicand` populated ✅

### 10.5 Bugs Found (Phase 2)

#### Bug 7: `input()` resolves first argument as file path, not inline string

- **Symptom:** `input("\\sqrt{x}", {type: 'math', flavor: 'latex'})` treated the string as a file/URL path and failed to find it.
- **Root cause:** The `input()` function always resolves its first argument as a path. There is no inline-string parsing mode.
- **Workaround:** Write math expressions to `.tex` files first, then `input("temp/m1.tex", {type: 'math', flavor: 'latex'})`.
- **Status:** Design limitation. A `render_latex(string)` convenience function (§6.4 item 5) would bypass this.

#### Bug 8: `use format` causes parse error

- **Symptom:** Having `use sys` + `use format` before `pn main()` produced a parse error (`ERROR node at Ln 1`).
- **Root cause:** Unknown — may be a parser issue with `use` and certain identifiers.
- **Workaround:** Call `format()` directly without `use format`. The function is available as a built-in.

#### Bug 9: Inline comments break nested let-chain expressions

- **Symptom:** A deeply nested `(let ..., let ..., // comment, if (...) ...)` expression caused parse error at the comment line.
- **Root cause:** The parser does not handle `//` comments inside comma-separated let-chain expressions within parenthesized groups.
- **Fix:** Refactored complex nested expressions into separate named helper functions. This also improves readability.
- **Files changed:** `render.ls` — split `render_big_op` into `render_big_op_with_limits`, `render_big_op_display`, `render_big_op_inline`.

#### Bug 10: `format(box_result, 'xml')` renders as `<root/>`

- **Symptom:** Rendering results serialized as `<root/>` via `format(result, 'xml')`, hiding all interior content.
- **Root cause:** The render result is a map (box model: `{element: <span ...>, height: ..., width: ...}`), not a raw element. `format()` on a map wraps it in a `<root>` element with attributes from map keys, and the nested `<span>` element inside the `element` field is not expanded.
- **Workaround:** Access the inner element directly: `format(result.element, 'xml')`. Or use `math.render_standalone(ast)` which returns a fully composed element.
- **Status:** Expected behavior — not a bug, but a usability trap.

#### Bug 11: `for (i in 0 to expr)` does not parse in `pn` context

- **Symptom:** Range-based for loop `for (i in 0 to n)` produced a parse error inside a `pn` procedure block.
- **Root cause:** Unknown — `for` with range may only be supported in `fn` (functional) context.
- **Workaround:** Use manual loop: `let i = 0; loop { if (i >= n) break; ...; i = i + 1 }`.

#### Bug 12: Multiple `let x^err` bindings in same scope conflict

- **Symptom:** Having `let a^err = ...` and `let b^err = ...` in the same scope caused "duplicate definition of 'err'" compile error.
- **Root cause:** The error variable name (`err`) is shared across all error-handling bindings in the same scope.
- **Fix:** Use unique error variable names: `let a^e1 = ...`, `let b^e2 = ...`, etc.

#### Bug 13: `pn` required for `print()` and error propagation

- **Symptom:** Using `print()` or `?` error propagation inside `fn` caused compile errors.
- **Root cause:** `fn` is pure functional — side effects like `print()` and error propagation with `?` are only allowed in `pn` (procedure) blocks.
- **Fix:** Use `pn` for test scripts and any code that needs I/O or error handling.

### 10.6 Updated Lambda Script Syntax Rules

New rules discovered in Phase 2, extending §9.4:

| #   | Rule                                                                         | Consequence of Violation                          |
| --- | ---------------------------------------------------------------------------- | ------------------------------------------------- |
| 14  | `input()` first argument is always a file path — no inline string parsing    | File-not-found error                              |
| 15  | No `//` comments inside parenthesized let-chain expressions                  | Parse error                                       |
| 16  | `for (i in 0 to n)` may not work in `pn` context                            | Parse error                                       |
| 17  | Use unique error variable names: `let x^e1`, `let y^e2` — not `^err` twice  | Duplicate definition error                        |
| 18  | `print()` and `?` error propagation require `pn`, not `fn`                   | Compile error                                     |
| 19  | Don't use `use format` — call `format()` directly as a built-in             | Parse error                                       |
| 20  | Access `.element` field on box results before formatting                     | `format()` on boxes produces `<root/>`            |

### 10.7 Outstanding Work for Phase 3

Phase 3 modules not yet created (from §4 Phase 3 plan):

| Module | Feature | Priority |
|--------|---------|----------|
| `atoms/array.ls` | `matrix`, `pmatrix`, `bmatrix`, `vmatrix`, `cases`, `aligned`, `array` | High |
| `atoms/style.ls` | `\mathbf`, `\mathrm`, `\mathbb`, `\mathcal`, `\displaystyle`, `\operatorname` | High |
| `atoms/color.ls` | `\textcolor`, `\color`, `\colorbox` — named colors + hex | Medium |
| `atoms/enclose.ls` | `\boxed`, `\fbox` | Low |

## 11. Phase 3 Progress Report

### 11.1 Status: Complete

All Phase 3 atom modules are implemented and working. **14/14** Phase 3 test constructs render correctly. Baseline regression: **328/328** — zero regressions.

### 11.2 New Modules

| Module | Lines | Constructs | Description |
|--------|-------|------------|-------------|
| `atoms/style.ls` | 77 | `\mathbf`, `\mathrm`, `\mathbb`, `\mathcal`, `\mathfrak`, `\mathsf`, `\mathtt`, `\mathit`, `\displaystyle`, `\textstyle`, `\operatorname` | Font style commands — maps command name to CSS class, applies to rendered argument |
| `atoms/color.ls` | 118 | `\textcolor`, `\color`, `\colorbox` | Color commands — resolves named colors (red, blue, etc.) and hex codes, applies as CSS `color` or `background-color` |
| `atoms/enclose.ls` | 153 | `\boxed`, `\fbox`, `\phantom`, `\rule` | Enclosure commands — `\boxed`/`\fbox` add border, `\phantom` renders invisible, `\rule` draws a filled rectangle with parsed dimensions |
| `atoms/array.ls` | 220 | `\begin{pmatrix}`, `\begin{bmatrix}`, `\begin{vmatrix}`, `\begin{Vmatrix}`, `\begin{cases}`, `\begin{aligned}`, `\begin{array}`, `\matrix`, `\pmatrix` | Environment/matrix rendering — CSS grid layout with delimiter wrapping |

**Total new code:** 568 lines across 4 new modules.

### 11.3 Modified Files

| File | Lines | Changes |
|------|-------|---------|
| `render.ls` | 401 | Added imports for `style`, `color`, `enclose`, `arr_mod`; added dispatch cases for `style_command`, `color_command`, `phantom_command`, `box_command`, `rule_command`, `environment`, `matrix_command`, `env_body`, `matrix_body` |
| `css.ls` | 154 | Added `MTABLE` class constant and `.ML__mtable{display:inline-grid;vertical-align:middle}` CSS rule |

### 11.4 Architecture Decisions

#### CSS Grid for Matrix/Environment Layout

Environments (matrices, cases, etc.) use **CSS grid** rather than nested box structures:
- `grid-template-columns: auto auto ...` (one `auto` per column)
- `column-gap: 1em` (default), `0.2em` for `cases`
- `row-gap: 0.16em`
- Per-cell `justify-self` alignment (center, left, or right based on column spec)
- The grid element is returned as an inline map `{element: ..., height: ..., depth: ...}` directly, avoiding cross-module element JIT issues.

This approach avoids the list-flattening problem discovered during development: Lambda's `for` expression produces lists that auto-flatten when nested, making nested row/column box structures unreliable.

#### Delimiter Wrapping

Matrix environments automatically add matching delimiters:
- `pmatrix` → `(` ... `)`
- `bmatrix` → `[` ... `]`
- `vmatrix` → `|` ... `|`
- `Vmatrix` → `‖` ... `‖`
- `cases` → `{` (left only)
- Others → no delimiters

Delimiters use CSS class `ML__small-delim` and are assembled with `box.hbox()`.

### 11.5 Critical Bug Discovery: `array` Is a Reserved Word

**Root cause:** Using `import array: .lambda.package.math.atoms.array` causes JIT corruption. The alias name `array` conflicts with Lambda's built-in `array` type, causing all return values from functions called via the `array.` prefix to be corrupted.

**Symptoms:**
- `unknown type error in set_fields` (3× per function call)
- `map_get ANY type is UNKNOWN: 24` (2× per function call)
- Elements become `[error]`, numeric values become `<error>`
- The function body is irrelevant — even `fn f() => box.text_box("X", null, "mord")` fails when called as `array.f()`

**Isolation method:** Systematically simplified `array.ls` to a 10-line stub → still errored. Moved the function inline to `render.ls` → worked. Changed import alias from `array` to `arr_mod` → worked. Confirmed the alias name `array` is the sole cause.

**Fix:** `import arr_mod: .lambda.package.math.atoms.array` in `render.ls`.

**Rule added:** Never use `array`, `list`, `map`, `string`, `int`, `float`, `bool`, `null`, or other built-in type names as import aliases.

### 11.6 Test Results

All 14 Phase 3 constructs render correctly:

| # | Input | Construct | Result |
|---|-------|-----------|--------|
| 1 | `\mathbf{A}` | Bold font | `ML__mathbf` class applied |
| 2 | `\mathbb{R}` | Blackboard bold | `ML__bb` class applied |
| 3 | `\mathcal{L}` | Calligraphic | `ML__cal` class applied |
| 4 | `\displaystyle \frac{a}{b}` | Display style | Display context propagated to fraction |
| 5 | `\operatorname{sin}(x)` | Operator name | `ML__cmr` class applied |
| 6 | `\textcolor{red}{x+y}` | Text color | `color:#d32f2f` inline style |
| 7 | `\colorbox{yellow}{z}` | Color box | `background-color:#ffeb3b` + padding |
| 8 | `\boxed{E=mc^2}` | Boxed | `border:1px solid` style |
| 9 | `\phantom{x}y` | Phantom | `visibility:hidden` style |
| 10 | `\rule{2em}{0.5em}` | Rule | `ML__rule` class + width/height styles |
| 11 | `\begin{pmatrix}` | Pmatrix | `(` + 2×2 CSS grid + `)` |
| 12 | `\begin{bmatrix}` | Bmatrix | `[` + 2×3 CSS grid + `]` |
| 13 | `\begin{cases}` | Cases | `{` + 2×2 grid with `0.2em` gap |
| 14 | `\begin{vmatrix}` | Vmatrix | `|` + 2×2 CSS grid + `|` |

### 11.7 Lambda Syntax Rules Discovered

| # | Rule | Symptom if Violated |
|---|------|---------------------|
| 21 | `array` is a reserved type name — cannot be used as an import alias | JIT corruption: `unknown type error in set_fields`, `map_get ANY type is UNKNOWN: 24` |
| 22 | `for` expressions produce lists that auto-flatten when nested (flatMap behavior) | Nested table structures collapse into flat sequences |
| 23 | Element literal syntax: `<tag attr1: v1, attr2: v2; child1 child2>` — comma-separated attrs, semicolon before children | Parse errors |
| 24 | `for` inside element children works: `<span class: c; for (item in items) item>` | N/A — useful pattern |
| 25 | Inline maps `{element: ..., height: ...}` can be returned from `fn` to avoid cross-module element JIT issues | Elements constructed in one module may corrupt when returned to another |

---

## §12 Phase 4 Implementation Notes

### 12.1 Summary

Phase 4 delivers three functional enhancements plus one partial optimization:

| Feature | Status | Module |
|---------|--------|--------|
| Stretchy delimiters (extensible) | ✅ Complete | `atoms/delimiters.ls` |
| Sized delimiters (`\big`, `\Big`, `\bigg`, `\Bigg`) | ✅ Complete | `atoms/delimiters.ls` |
| Box coalescing (adjacent span merge) | ✅ Partial | `optimize.ls` |
| Error rendering (unknown commands) | ✅ Complete | `css.ls` (ERROR class) |
| Inter-atom spacing | ✅ Already done (Phase 1) | `spacing_table.ls` |

**Test results**: 8/8 Phase 4 tests pass, 328/328 baseline regression pass, Phase 2 + Phase 3 tests all pass.

### 12.2 New Modules

#### `atoms/delimiters.ls` (~164 lines)

Extensible delimiter rendering with two strategies:

1. **Sized fonts (levels 1–4)**: Uses KaTeX size font classes (`size1-regular` through `size4-regular`) for delimiters from 1.2em to 3.0em. Size thresholds: 1.2, 1.8, 2.4, 3.0em.

2. **CSS scaleY (level 5+)**: For delimiters taller than 3.0em, applies a `transform: scaleY(N)` CSS transform to stretch the glyph vertically.

Public API:
- `render_stretchy(delim, content_height, atom_type)` — Automatically selects size level based on content height, used by `\left...\right` delimiters
- `render_at_scale(delim, scale, atom_type)` — Renders at a specific scale factor, used by `\big`, `\Big`, `\bigg`, `\Bigg` commands

Supporting data:
- `SIZED_DELIMS` — Map of all delimiters with sized font variants
- `DELIM_CHARS` — Maps LaTeX commands (`\langle`, `\vert`, etc.) to Unicode display characters
- `SVG_DELIM_DATA` — SVG path data for delimiter segments (reserved for future SVG rendering)

#### `optimize.ls` (~65 lines)

Post-processing optimization pass that merges adjacent same-class text-only `<span>` elements to reduce DOM node count.

Public API:
- `coalesce(bx)` — Takes a box map `{element, height, depth, width, type, italic, skew}` and returns an optimized version

Internal algorithm:
1. `merge_children(el)` — Entry point; processes multi-child elements
2. `build_merged(el)` — Extracts children, recursively walks sub-elements, merges adjacent spans via `merge_list`, reconstructs element
3. `walk(c)` — Recursively processes multi-child child elements
4. `merge_list(items)` / `do_merge(items, i, acc)` — Functional fold that merges adjacent items passing `can_merge` test
5. `can_merge(a, b)` — Two elements are mergeable when both have exactly 1 text child, same CSS class, and no inline style
6. `merge_two(a, b)` — Concatenates text content into a single `<span>`

**Limitation**: Coalescing only penetrates multi-child element levels. Single-child wrapper chains (common at the top of the render tree) are not traversed due to JIT stack overflow constraints.

### 12.3 Modified Files

#### `render.ls` Changes
- Added `import delims: .lambda.package.math.atoms.delimiters`
- `render_delimiter_group`: Now uses `delims.render_stretchy()` for `\left`/`\right` delimiters. Calculates `content_height = content.height + content.depth` and passes to delimiter renderer for size selection.
- `render_sized_delim`: Now uses `delims.render_at_scale()` for `\big`/`\Big`/`\bigg`/`\Bigg` commands.

#### `math.ls` Changes
- Added `import opt: .lambda.package.math.optimize`
- Render pipeline now: `render_node(ast)` → `opt.coalesce()` → `box.make_struts()` → wrap in `<span class: ML__latex>`

### 12.4 Architecture Decisions

1. **Font-based stretching over SVG**: Sized KaTeX fonts (Size1–Size4) provide sharp, scalable delimiters at discrete levels. CSS `scaleY` handles arbitrary heights above 3.0em. SVG path stacking (top/repeat/bottom segments) is architecturally prepared but not yet rendering — the font+CSS approach covers all practical cases.

2. **Coalescing as post-processing**: Box coalescing runs after the full render tree is built, not during rendering. This keeps the renderer simple and makes the optimization optional.

3. **Functional fold for merging**: `do_merge` uses a tail-recursive fold pattern with an accumulator, which is the idiomatic Lambda approach for stateful iteration.

### 12.5 Critical JIT Bugs Discovered

| # | Bug Description | Workaround |
|---|----------------|------------|
| 1 | `for` comprehension inside element children MUST be on a separate line. `<span; for (c in items) c>` causes parse error. Multi-line `<span;\n for (c in items) c\n>` works. | Always put `for` on its own line inside elements |
| 2 | `merge_list` return value cannot be directly iterated in element constructor `for (c in merged) c` — produces `[[unknown type raw_pointer!!]]`. Re-extracting via `(for (j in 0 to (len(merged) - 1)) merged[j])` works. | Re-extract array elements through indexing before use in element children |
| 3 | Recursive functions through single-child element wrappers cause stack overflow even with depth limits of 10. The JIT per-function stack frame size appears very large. | Only recurse through multi-child elements |

#### Bug #3 Detail: Stack Overflow in Single-Child Element Recursion

**Goal**: Walk through single-child wrapper elements (e.g., `<span class: "ML__latex"; <span; <span; [x, y, z]>>>`) to reach multi-child levels where coalescing can happen.

**Attempt 1 — Unbounded recursion** (stack overflow):
```lambda
fn merge_children(el) {
    if (len(el) > 1) build_merged(el)
    else if (len(el) == 1)
        (let child = el[0],
         if (type(child) == "element")
             <span class: el.class, style: el.style;
                 merge_children(child)
             >
         else el)
    else el
}
```

**Attempt 2 — Depth-limited to 10** (still stack overflow):
```lambda
fn merge_at_depth(el, d) {
    if (d <= 0) el
    else if (len(el) > 1) build_merged(el)
    else if (len(el) == 1)
        (let child = el[0],
         if (type(child) == "element")
             <span class: el.class, style: el.style;
                 merge_at_depth(child, d - 1)
             >
         else el)
    else el
}

fn merge_children(el) {
    merge_at_depth(el, 10)
}
```

**Root cause**: Each recursive call constructs a `<span class: ..., style: ...; recursive_call(...)>` element inline. The JIT-compiled stack frame for a function that constructs elements appears to be very large (possibly allocating temporary space for element attribute slots, child arrays, and intermediate `Item` values). Even ~10 frames is enough to exhaust the ~8MB stack.

**Error output**:
```
signal handler: stack overflow detected (fault_addr=0x16b46bfd0, stack_limit=0x16b47c000)
exec: recovered from stack overflow via signal handler
stack overflow in function '<signal>' - possible infinite recursion
stack usage: 7 KB / 8176 KB (0.1%)
runtime error [308]: Stack overflow in '<signal>' - likely infinite recursion (stack: 7KB/8176KB)
```

Note: the reported "stack usage: 7 KB" is misleading — this is measured *after* the signal handler recovered and unwound the stack. The actual usage at overflow was ~8MB.

**Working workaround** — skip single-child wrappers entirely:
```lambda
fn merge_children(el) {
    if (len(el) > 1) build_merged(el)
    else el
}
```

This limits coalescing to multi-child levels only. Single-child wrapper chains (common at the top of the math render tree) are left as-is. The tradeoff is acceptable because coalescing's primary value is merging adjacent same-class text siblings, which only exist at multi-child levels.
| 4 | `[items[0]]` as a standalone expression causes parse error ("expected ']'"). Same syntax works as a function argument: `do_merge(items, 1, [items[0]])`. | Use `let first = items[0], [first]` or pass as function argument |
| 5 | `if/else` returning elements with different attribute shapes (e.g., `<span class: c; x>` vs `<span; x>`) crashes JIT | Always use consistent shapes: `<span class: a.class; x>` where class may be null |

### 12.6 Test Results

| # | Test | LaTeX Input | Validates |
|---|------|-------------|-----------|
| 1 | Stretchy delimiters | `\left(\frac{a}{b}\right)` | Size selection, delimiter rendering |
| 2 | Sized delimiters | `\bigl( x \bigr)` | Scale-to-class mapping |
| 3 | Coalescing candidate | `xyz` | Box coalescing pass (identity at top level) |
| 4 | Error rendering | `\undefinedcommand` | `ML__error` class on unknown commands |
| 5 | Tall delimiters | `\left[\frac{\frac{a}{b}}{\frac{c}{d}}\right]` | Multi-level size selection (2.303em height) |
| 6 | Middle delimiter | `\left( a \middle| b \right)` | `\middle` support in delimiter groups |
| 7 | Inter-atom spacing | `a + b = c` | Spacing between atom types |
| 8 | Complex integration | `\sum_{i=0}^{n}\left(\frac{x_i}{y_i}\right)^2` | All features composed |

### 12.7 Lambda Syntax Rules Discovered

| # | Rule | Symptom if Violated |
|---|------|---------------------|
| 26 | `for` in element children MUST be on its own line | Parse error: `ERROR node at Ln X` spanning the entire function body |
| 27 | `[expr[n]]` as standalone return value is parsed as subscript, not array-with-subscript | Parse error: "expected ']'" |
| 28 | Function return values from `merge_list` (multi-branch return array) produce `raw_pointer` when used in `for (c in result) c` inside element constructor | `[[unknown type raw_pointer!!]]` in element children |
| 29 | Recursive single-child element traversal overflows stack even with depth limit 10 — JIT stack frames are very large | Stack overflow signal handler fires |
| 30 | `<span; for (c in items) c>` single-line fails but `<span;\n    for (c in items) c\n>` multi-line works | Parse error vs working code |

### 12.8 Module Inventory (Phase 4 Complete)

```
lambda/package/math/
├── math.ls              # Entry point (48 lines)
├── render.ls            # Core renderer (398 lines)
├── box.ls               # Box model (233 lines)
├── css.ls               # CSS classes (126 lines)
├── context.ls           # Display/text context (42 lines)
├── metrics.ls           # Font metrics (197 lines)
├── symbols.ls           # Symbol table (379 lines)
├── util.ls              # Utilities (47 lines)
├── spacing_table.ls     # Inter-atom spacing (91 lines)
├── optimize.ls          # Box coalescing (65 lines)  ← NEW
└── atoms/
    ├── fraction.ls      # Fractions (148 lines)
    ├── scripts.ls       # Sub/superscripts (192 lines)
    ├── spacing.ls       # Spacing commands (68 lines)
    ├── style.ls         # Style/font commands (77 lines)
    ├── color.ls         # Color commands (118 lines)
    ├── enclose.ls       # Boxing/enclosure (153 lines)
    ├── array.ls         # Matrix environments (220 lines)
    └── delimiters.ls    # Stretchy delimiters (164 lines) ← NEW
```

Total: 18 modules, ~2,568 lines of Lambda Script.

## Appendix D: Completeness Analysis — Lambda Math vs. MathLive

This appendix documents a comprehensive audit of the Lambda math package against MathLive's full feature set, identifying what is covered, what is missing, and the priority path toward broader coverage.

### D.1 Grammar Node Type Coverage (94%)

The tree-sitter-latex-math grammar (`lambda/tree-sitter-latex-math/grammar.js`) produces 33 atom-level node types. The `render.ls` dispatch table handles **31 of 33**:

| # | Node Type | Status |
|---|-----------|:------:|
| 1 | `symbol` | ✅ |
| 2 | `number` | ✅ |
| 3 | `symbol_command` | ✅ |
| 4 | `operator` | ✅ |
| 5 | `relation` | ✅ |
| 6 | `punctuation` | ✅ |
| 7 | `group` | ✅ |
| 8 | `fraction` | ✅ |
| 9 | `binomial` | ✅ |
| 10 | `genfrac` | ✅ |
| 11 | `radical` | ✅ |
| 12 | `delimiter_group` | ✅ |
| 13 | `sized_delimiter` | ✅ |
| 14 | `overunder_command` | ✅ |
| 15 | `extensible_arrow` | ❌ Missing |
| 16 | `accent` | ✅ |
| 17 | `box_command` | ✅ |
| 18 | `color_command` | ✅ |
| 19 | `rule_command` | ✅ |
| 20 | `phantom_command` | ✅ |
| 21 | `big_operator` | ✅ |
| 22 | `mathop_command` | ❌ Missing |
| 23 | `matrix_command` | ✅ |
| 24 | `environment` | ✅ |
| 25 | `text_command` | ✅ |
| 26 | `style_command` | ✅ |
| 27 | `space_command` | ✅ |
| 28 | `hspace_command` | ✅ |
| 29 | `skip_command` | ✅ |
| 30 | `command` | ✅ (generic fallback) |
| 31 | `subsup` | ✅ |
| 32 | `infix_frac` | ✅ |
| 33 | `brack_group` | ✅ (mapped to `render_group`) |

**Missing renderers** (grammar already parses these):
- **`extensible_arrow`** — `\xrightarrow`, `\xleftarrow`, `\xRightarrow`, `\xLeftarrow`, `\xleftrightarrow`, `\xhookleftarrow`, `\xhookrightarrow`, `\xmapsto`. Currently falls through to `default` → `render_default`, losing the arrow SVG body and above/below annotations.
- **`mathop_command`** — `\mathop{...}` (custom operator with limits behavior). Falls through to `default`, losing operator-with-limits semantics.

### D.2 Symbol Coverage (250/506 = 49%)

Lambda `symbols.ls` contains **250 symbols** vs. MathLive's **~506** unique LaTeX commands:

| Category | Lambda | MathLive (approx) |
|----------|-------:|-------------------:|
| Greek lowercase | 30 | 34 |
| Greek uppercase | 11 | 11 |
| Binary operators | 27 | 52 |
| Relations | 36 | 86 |
| Arrows | 30 | 62 |
| Miscellaneous symbols | 54 | 79 |
| Big operators | 16 | 28 |
| Accents | 14 | 22 |
| Operator names | 32 | 32 |
| **Total** | **250** | **~506** |

**Major missing symbol categories** (~256 symbols):

- **Negated relations** (~40): `\nless`, `\nleq`, `\ngeq`, `\ngtr`, `\nleqslant`, `\ngeqslant`, `\ncong`, `\nsim`, `\nparallel`, `\nVDash`, `\nvdash`, `\nvDash`, `\nsubseteq`, `\nsupseteq`, `\precnsim`, `\succnsim`, etc.
- **AMS relations** (~50): `\leqslant`, `\geqslant`, `\lesssim`, `\gtrsim`, `\approxeq`, `\thickapprox`, `\lessgtr`, `\gtrless`, `\curlyeqprec`, `\curlyeqsucc`, `\Vdash`, `\vDash`, `\Vvdash`, `\between`, `\pitchfork`, `\backepsilon`, `\therefore`, `\because`, etc.
- **AMS binary operators** (~25): `\ltimes`, `\rtimes`, `\leftthreetimes`, `\rightthreetimes`, `\intercal`, `\dotplus`, `\doublebarwedge`, `\divideontimes`, `\boxminus`, `\boxplus`, `\boxtimes`, `\circleddash`, `\circledast`, `\circledcirc`, etc.
- **AMS arrows** (~20): `\dashrightarrow`, `\dashleftarrow`, `\Rrightarrow`, `\Lleftarrow`, `\leftarrowtail`, `\rightarrowtail`, `\twoheadleftarrow`, `\twoheadrightarrow`, `\upuparrows`, `\downdownarrows`, `\rightsquigarrow`, `\leadsto`, `\multimap`, etc.
- **AMS ordinals** (~25): `\square`, `\Box`, `\blacksquare`, `\blacktriangle`, `\blacktriangledown`, `\lozenge`, `\blacklozenge`, `\complement`, `\diagup`, `\measuredangle`, `\sphericalangle`, `\backprime`, `\eth`, `\mho`, `\Finv`, `\Game`, etc.
- **Negated arrows** (~12): `\nleftarrow`, `\nrightarrow`, `\nRightarrow`, `\nLeftarrow`, `\nleftrightarrow`, `\nLeftrightarrow`, etc.
- **St. Mary's Road / specialty** (~30): `\llbracket`, `\rrbracket`, `\Lbag`, `\Rbag`, `\boxbar`, `\lightning`, etc.
- **Missing delimiters** (~8): `\ulcorner`, `\urcorner`, `\llcorner`, `\lrcorner`, `\lparen`, `\rparen`, `\lbrack`, `\rbrack`
- **Missing Greek** (~4): `\digamma`, `\varkappa`, `\coppa`, `\sampi` (AMS/archaic)
- **Additional integrals** (~5): `\oiint`, `\oiiint`, `\intclockwise`, `\varointclockwise`, `\ointctrclockwise`

### D.3 MathLive Features with No Lambda Equivalent

| Feature | MathLive Source | Description |
|---------|-----------------|-------------|
| **Chemistry (mhchem)** | `mhchem.ts` (2603 lines) | `\ce{}`, `\pu{}` chemical equations |
| **Extensible arrows** | `extensible-symbols.ts` | `\xrightarrow[below]{above}` with SVG arrow body |
| **Extensible over/under SVGs** | `extensible-symbols.ts` | `\overrightarrow`, `\overleftarrow`, `\overleftrightarrow`, `\overgroup`, `\underrightarrow`, `\underleftarrow`, etc. |
| **`\mathop{...}`** | `styling.ts` | Custom operator with limits placement |
| **Cancel / strikethrough** | `enclose.ts` | `\cancel`, `\bcancel`, `\xcancel`, `\sout` |
| **`\enclose` (MathML)** | `enclose.ts` | Arbitrary menclose notations |
| **Font sizing** | `styling.ts` | `\tiny`, `\small`, `\large`, `\Large`, `\LARGE`, `\huge`, `\Huge` |
| **`\mathchoice`** | `styling.ts` | Display/text/script/scriptscript branching |
| **`\mathbin`/`\mathrel`/`\mathord`** | `styling.ts` | Explicit atom type override |
| **`\operatorname{...}`** | `styling.ts` | Custom named operators (e.g., `\operatorname{argmax}`) |
| **`\not` (negation prefix)** | `styling.ts` | Generic negation via overlay slash |
| **Tooltips** | `styling.ts` | `\mathtip`, `\texttip` |
| **HTML integration** | `styling.ts` | `\href`, `\class`, `\cssId`, `\htmlStyle`, `\htmlData` |
| **`\raisebox`/`\raise`/`\lower`** | `styling.ts` | Vertical positioning |
| **`\char`/`\unicode`** | `styling.ts` | Arbitrary Unicode by codepoint |
| **Text-mode accents** | `accents.ts` | `\^{a}`, `` \`{e} ``, `\'{o}`, `\"{u}`, `\~{n}`, `\c{c}` |
| **`\overarc`/`\overparen`** | `accents.ts` | Arc accents |
| **`\utilde`** | `accents.ts` | Under-tilde |
| **`\overunderset`** | `styling.ts` | Combined over+under simultaneously |
| **`\cfrac[l]`/`\cfrac[r]`** | `functions.ts` | Left/right-aligned continued fractions |
| **`\pdiff`** | `functions.ts` | Partial derivative shorthand |
| **`\ang`** | `functions.ts` | Angle from siunitx |
| **`\mathstrut`** | `styling.ts` | Invisible parenthesis-height strut |
| **`\fcolorbox`** | `styling.ts` | Framed color box |
| **Starred matrix variants** | `environments.ts` | `pmatrix*`, `bmatrix*`, etc. |
| **`eqnarray`/`subequations`** | `environments.ts` | Equation environments |
| **`\displaylines`** | `functions.ts` | Multi-line display |

### D.4 What IS Working Well

The Lambda math package competently handles the **core 85–90%** of everyday math rendering:

- **Fractions**: `\frac`, `\dfrac`, `\tfrac`, `\cfrac`, `\binom`, `\dbinom`, `\tbinom`, `\genfrac`, infix (`\over`, `\atop`, `\choose`, etc.)
- **Scripts**: subscript, superscript, combined sub+sup, `\limits`/`\nolimits`
- **Radicals**: `\sqrt` with optional index
- **Delimiters**: `\left`/`\right` with stretchy rendering (4 size levels + CSS scaleY), `\middle`, all sized variants (`\big` through `\Bigg`)
- **Accents**: 14 accent types — `\hat`, `\bar`, `\vec`, `\dot`, `\ddot`, `\widehat`, `\widetilde`, `\overbrace`, `\underbrace`, `\overline`, `\underline`, etc.
- **Big operators**: `\sum`, `\prod`, `\int`, `\iint`, `\iiint`, `\oint`, display-mode limit placement
- **Environments/matrices**: `array`, `matrix`, `pmatrix`, `bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`, `cases`, `aligned`, `gathered`, etc.
- **Styling**: `\mathrm`, `\mathbb`, `\mathcal`, `\mathfrak`, `\mathscr`, `\mathbf`, `\mathsf`, `\mathtt`, `\displaystyle`, `\textstyle`, etc.
- **Text**: `\text`, `\textrm`, `\textbf`, `\textit`, `\mbox`
- **Colors**: `\color`, `\textcolor`, `\colorbox`
- **Spacing**: `\,`, `\:`, `\;`, `\!`, `\quad`, `\qquad`, `\hspace`, `\kern`, `\hskip`, `\mskip`
- **Boxes/phantoms**: `\boxed`, `\fbox`, `\bbox`, `\phantom`, `\hphantom`, `\vphantom`, `\smash`, `\llap`, `\rlap`
- **Rules**: `\rule` with width/height/depth dimensions
- **Inter-atom spacing**: Full Knuth spacing table (7×7 atom types)
- **Post-processing**: Text node coalescing via `optimize.ls`

### D.5 Coverage Summary

| Metric | Value |
|--------|-------|
| Grammar node types handled | 31 / 33 (94%) |
| Symbol coverage vs. MathLive | 250 / 506 (49%) |
| Core math feature coverage | ~85–90% of common expressions |
| Advanced/AMS feature coverage | ~40% |
| Missing renderer dispatch entries | 2 (`extensible_arrow`, `mathop_command`) |
| Missing MathLive features (no equivalent) | ~27 features |
| Missing AMS symbols (table entries) | ~256 commands |

**Practical interpretation**: ~85–90% of typical math content (textbook equations, homework, academic papers) uses fractions, scripts, radicals, Greek letters, basic operators/relations, arrows, delimiters, matrices, and accents — all of which are fully handled. The remaining ~10–15% relies on AMS extended symbols, extensible arrows, cancel notations, and specialty features.

### D.6 Priority Path to Broader Coverage

Ranked by impact-to-effort ratio:

1. **Add `extensible_arrow` renderer** — grammar already parses it; needs dispatch case + render function with SVG arrow body. Covers `\xrightarrow`, `\xleftarrow`, and 7 other commands.
2. **Add `mathop_command` renderer** — grammar already parses it; simple dispatch + big-operator-style limits logic. Covers `\mathop{...}`.
3. **Expand `symbols.ls` with ~150 AMS symbols** — pure table additions (Unicode codepoints + atom types). Covers negated relations, AMS arrows, AMS binary operators, AMS ordinals. Largest single improvement to symbol coverage.
4. **Add `\cancel`/`\bcancel`/`\xcancel`** — very common in educational content; diagonal/back-diagonal line overlay via CSS.
5. **Add `\operatorname{...}` support** — frequently used for custom function names (`\operatorname{argmax}`, `\operatorname{Tr}`). Render as upright text with operator spacing.
6. **Add font sizing commands** — `\small`, `\large`, `\Large`, etc. Map to CSS font-size multipliers.
7. **Add `\mathbin`/`\mathrel`/`\mathord`** — atom type override for correct spacing; simple wrapper that sets atom type on child.
8. **Add `\not` generic negation** — overlay slash on following symbol via CSS positioning.
9. **Expand delimiter table** — add corner brackets (`\ulcorner`, etc.) and aliases (`\lparen`, `\lbrack`).
10. **Chemistry (`\ce{}`)** — large feature (MathLive's implementation is 2600 lines); lowest priority unless chemistry use cases are targeted.
