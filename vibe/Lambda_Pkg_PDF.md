# Lambda PDF Package Proposal

> **Location:** `lambda/package/pdf/`
> **References:**
> - Existing C++ PDF parser: `lambda/input/input-pdf.cpp` (~3,200 lines)
> - Existing C++ PDF→ViewTree pipeline: `radiant/pdf/` (~3,400 lines: `pdf_to_view.cpp`, `operators.cpp`, `pages.cpp`, `fonts.cpp`, `coords.cpp`)
> - PDF.js (`ref/pdf.js/`) — Mozilla's reference JavaScript implementation
> - LaTeX/Chart packages (`lambda/package/latex/`, `lambda/package/chart/`) — proven Lambda package patterns
> - Prior design docs: `vibe/Pdf_to_View.md`, `vibe/Pdf_View_Design.md`
> **Goal:** Convert a parsed PDF (Lambda element tree produced by the existing C/C++ parser) to **SVG (primary)** and **HTML (secondary)** for display under Radiant, written entirely in Lambda Script.

---

## 1. Goals & Scope

### 1.1 Primary Goal

Build a **pure Lambda Script package** at `lambda/package/pdf/` that takes the Lambda data tree produced by the existing C/C++ PDF parser (`input-pdf.cpp`) and emits **SVG** elements (one `<svg>` per page, optionally wrapped in an `<html>` shell) suitable for:

- Display under Radiant via the existing SVG/HTML rendering pipeline
- Standalone browser display
- Conversion to PNG/PDF via `format(result, 'svg' | 'html')`

### 1.2 Why a Lambda Script Layer?

The current C++ pipeline `radiant/pdf/pdf_to_view.cpp` directly walks the parsed PDF tree and constructs a `ViewTree` of `ViewBlock`/`ViewText` nodes. This works but is:

| Issue | Detail |
|-------|--------|
| **Hard to extend** | Adding a new operator, font mapping, or color-space requires recompiling C++ |
| **Tightly coupled to Radiant** | Output is Radiant-internal `ViewTree`, not portable to web/embed |
| **Not data-format agnostic** | Cannot easily target SVG, HTML, or canvas instructions from the same converter |
| **Stateful & imperative** | Graphics state stack, content-stream interpreter mix transformation with rendering |

A Lambda Script package decouples **interpretation of the PDF tree** from **rendering target**, mirroring the LaTeX and Chart package architectures already validated in the codebase.

### 1.3 Scope

| In Scope | Out of Scope |
|----------|--------------|
| PDF Lambda tree → SVG element tree (per page) | PDF parsing / decompression / xref resolution (stays in C/C++) |
| PDF Lambda tree → HTML wrapper (multi-page document) | Image decoding (JPEG/JBIG2/JPX) — stays in C/C++ |
| Content-stream operator interpretation in Lambda | Font subsetting / glyph rasterization — stays in C/C++ |
| Graphics state stack (CTM, color, fonts) in Lambda | Form fields / interactive widgets (Phase 2+) |
| Text positioning, kerning (TJ arrays) | JavaScript actions / annotations (Phase 2+) |
| Path construction (m, l, c, re, h) → SVG path data | Encryption / digital signatures |
| Color spaces: DeviceGray, DeviceRGB, DeviceCMYK, ICCBased | Patterns, shadings (Phase 2) |
| Standard 14 font mapping → CSS font-family | Tagged PDF / accessibility tree (Phase 3) |
| Multi-page navigation in HTML wrapper | Form XObject recursion limit / Type 3 fonts (Phase 2) |

### 1.4 What Stays in C/C++

The decision is **explicit and final** for this proposal:

| Component | Lines | Why retained |
|-----------|-------|--------------|
| `lambda/input/input-pdf.cpp` | ~3,200 | Parser is byte-oriented, performance-critical, deals with binary streams, xref tables, indirect references |
| `lambda/input/pdf_decompress.c` | ~? | FlateDecode/LZW/ASCII85 — must be native for speed |
| `radiant/pdf/fonts.cpp` (font *loading*) | partial | Font file resolution and FreeType handles |
| `radiant/pdf/operators.cpp` (low-level **stream tokenizer**) | partial | Byte-level scanning is best in C |
| `radiant/pdf/cmd_view_pdf.cpp` | ~? | Viewer shell (GLFW window, keyboard) |

The **interpretation** layer (`radiant/pdf/pdf_to_view.cpp`, ~2,000 of its lines) is what the Lambda package **replaces** — but only after a parallel implementation proves equivalent and the C++ path can be removed without regressions.

---

## 2. Output Format Decision: SVG vs. HTML

This is the central design question. Recommendation summarized first, rationale follows.

### 2.1 Recommendation: **SVG primary, HTML as multi-page shell only**

| Layer | Format | Purpose |
|-------|--------|---------|
| **Per-page rendering** | **SVG** | Faithful PDF visual reproduction (1 SVG per page); selectable text via native `<text>` |
| **Multi-page document shell** | **HTML** | Page navigation, scrolling, toolbar |
| **Text selection** | **SVG `<text>` (no separate HTML layer)** | We will enhance SVG text emission so native browser selection works as well as HTML — see §2.6 |

Concretely, output looks like:

```html
<html>
  <body class="pdf-document">
    <div class="pdf-page" data-page="1">
      <svg viewBox="0 0 612 792" width="612" height="792"> ... page 1 graphics + selectable text ... </svg>
    </div>
    <div class="pdf-page" data-page="2"> <svg> ... </svg> </div>
  </body>
</html>
```

If only a single page is requested, the package emits the bare `<svg>`.

### 2.2 Why SVG (not HTML/CSS) for Page Content

PDF is fundamentally a **fixed-layout, vector-graphics imaging model** built on PostScript. SVG matches this model 1:1; HTML/CSS does not.

| PDF Feature | SVG Mapping | HTML/CSS Mapping |
|-------------|-------------|------------------|
| Absolute device-space coordinates | `<svg viewBox>` + nested `<g transform>` | `position: absolute` everywhere, fragile |
| Current Transformation Matrix (CTM) | `transform="matrix(a b c d e f)"` directly | `transform: matrix(...)` works but doesn't compose with text flow |
| Path construction (m/l/c/h/re) | `<path d="M ... L ... C ... Z">` direct mapping | No equivalent — would need inline SVG anyway |
| Stroke/fill with line width, dash, cap, join | Native SVG attributes | No equivalent in CSS for arbitrary paths |
| Text at exact baseline + matrix | `<text transform="matrix(...)" x="0" y="0">` | CSS text positioning is line-box based, not glyph-precise |
| TJ kerning arrays (per-glyph dx) | `<text><tspan dx="...">` per glyph or per run | Requires per-character `<span>` with negative margin — fragile |
| Color spaces (CMYK, ICC) → device RGB | `fill="rgb(...)"` after Lambda-side conversion | Same |
| Clipping paths (W, W*) | `<clipPath>` element | `clip-path: path(...)` (newer CSS, less portable) |
| Patterns / shadings / gradients | `<pattern>`, `<linearGradient>`, `<radialGradient>` | CSS gradients limited; SVG-in-CSS still needed |
| Form XObjects (reusable groups) | `<symbol>` + `<use>` | No native equivalent |
| Image XObjects | `<image href="data:...">` | `<img>` works but loses transform integration |
| Page boundary | `<svg viewBox>` with explicit width/height | Needs fixed-size container, page-break hacks for print |

**Conclusion:** Every PDF imaging primitive has a near-direct SVG counterpart. HTML/CSS would force us to fight the layout engine for things PDFs deliberately specify pixel-by-pixel.

### 2.3 Why HTML as the *Wrapper* (and only the wrapper)

HTML excels at things SVG does poorly:

- **Multi-page scrolling / pagination UI** — natural with `<div>` flow
- **Text selection** — browsers handle text selection over real DOM text nodes; SVG `<text>` selection is buggy across engines. PDF.js's solution is a transparent HTML text layer over the canvas; we can mirror this with SVG underneath.
- **Toolbar, page indicator, zoom controls** — trivially HTML
- **Hyperlinks (PDF link annotations)** — `<a href>` overlays

So the Lambda package returns an HTML document whose body contains one SVG per page. If the consumer (e.g., Radiant viewer) only needs a single page's graphics, it can request just the SVG element.

### 2.4 Why Not Pure HTML/CSS

Briefly considered, rejected because:

1. **Text positioning fidelity is unattainable.** PDFs commonly position each glyph individually via `Tm`/`Td` and TJ kerning. Reproducing this with `<span style="position:absolute; left:...; top:...">` per glyph technically works but balloons DOM size 10–50× and still mis-renders due to font-metric differences.
2. **Vector graphics have no HTML equivalent.** Any non-trivial PDF with shapes/paths forces inline SVG anyway — at which point, why split formats?
3. **Browser print/PDF-export of HTML pages re-flows content** — defeating the purpose of fixed-layout fidelity.

### 2.5 Why Not Pure SVG

- **No good multi-page model.** SVG has no native page concept; embedding several `<svg>` siblings in a parent SVG works but lacks scroll/pagination semantics.
- **No standard place for navigation chrome.**

The hybrid (HTML shell containing per-page SVGs) wins.

### 2.6 SVG Text Selection (Decision: SVG-only, no HTML overlay)

Native SVG `<text>` is selectable, copyable, and find-in-page-able in all modern browsers. The known rough edges (jumpy multi-run selection, glyph-by-glyph highlight, missing inter-run spaces in copied text, no double-click word-select) all stem from PDFs producing **many disconnected text runs** per visual line. We address these at the **interpretation layer** rather than by adding a parallel HTML text layer:

| SVG text problem | Lambda-side fix in `text.ls` |
|------------------|------------------------------|
| Many `<text>` nodes per visual line → jumpy selection, broken word-select | **Line clustering pass:** group runs by y-coordinate (modulo CTM) and emit one `<text>` per visual line, with `<tspan x="...">` per run. Browsers then treat the line as a single selectable unit. |
| Missing spaces when copying across runs | **Whitespace synthesis:** when run B's start-x exceeds run A's end-x by more than ~0.25 em, insert a literal space in the DOM between them. |
| Selection direction inconsistent under `transform` | Apply CTM to the whole `<text>` (not per-glyph) wherever possible; keep `<tspan dx>` only for true intra-word kerning. |
| Find-in-page misses substrings | Same as above — collapsing runs into per-line `<text>` nodes makes substring searches hit. |
| Per-glyph highlight noise | Resolved automatically once runs are merged into single `<text>` per line. |
| Vertical / RTL text | Use `writing-mode` and `direction` attributes; emit `<text>` per line in logical (reading) order. |

The line-clustering pass is the same heuristic PDF.js uses internally to build its HTML text layer — we apply it directly to the SVG output instead, avoiding the duplicate-DOM cost. Estimated **~250 LOC** in `text.ls`.

For accessibility (screen readers), emit `<title>` inside each line's `<text>` containing the plain text, and set `role="text"` on the `<svg>` itself.

---

## 3. Architecture

### 3.1 Pipeline

```
┌─────────────────────────────────────────┐
│  PDF file (binary)                      │
└────────────────────┬────────────────────┘
                     │  input(path, 'pdf')
                     ▼  [C/C++ — RETAINED]
┌─────────────────────────────────────────┐
│  lambda/input/input-pdf.cpp             │
│  - Lexer, xref, indirect-object resolve │
│  - Stream decompression                 │
│  - Builds Lambda Map/Array tree         │
└────────────────────┬────────────────────┘
                     │  Item (Map: version, objects[], trailer, …)
                     ▼  [LAMBDA — NEW PACKAGE]
┌─────────────────────────────────────────┐
│  lambda/package/pdf/                    │
│                                         │
│  pdf.ls          (entry, dispatcher)    │
│   │                                     │
│   ├─→ resolve.ls   (indirect refs,      │
│   │                 page tree walk)     │
│   ├─→ stream.ls    (content-stream      │
│   │                 operator tokenize)  │
│   ├─→ interp.ls    (graphics state      │
│   │                 stack, CTM, color)  │
│   ├─→ text.ls      (Tj/TJ/Tm → glyphs)  │
│   ├─→ path.ls      (m/l/c/re/h → SVG d) │
│   ├─→ color.ls     (DeviceGray/RGB/    │
│   │                 CMYK/ICC → sRGB)    │
│   ├─→ font.ls      (font dict → CSS    │
│   │                 font-family/weight) │
│   ├─→ image.ls     (XObject image →    │
│   │                 <image> / data URI) │
│   ├─→ svg.ls       (SVG element        │
│   │                 builder helpers)    │
│   └─→ html.ls      (multi-page wrapper, │
│                     text layer)         │
└────────────────────┬────────────────────┘
                     │  Element <html> or <svg>
                     ▼
┌─────────────────────────────────────────┐
│  format(result, 'html' | 'svg')         │
│  → string ready for Radiant or browser  │
└─────────────────────────────────────────┘
```

### 3.2 Module Layout (mirrors `lambda/package/latex/` and `chart/`)

```
lambda/package/pdf/
├── pdf.ls          // public API: pdf_to_html(tree), pdf_to_svg(tree, page_idx)
├── resolve.ls      // indirect-ref dereferencing, page-tree walk, get_page(n)
├── stream.ls       // content-stream → list of (op, operands) tuples
├── interp.ls       // operator dispatch via match; graphics state stack
├── text.ls         // text-mode operators (BT/ET/Tj/TJ/Tf/Tm/Td/TD/T*)
├── path.ls         // path-construction operators (m/l/c/v/y/re/h) + paint (S/s/f/B/b/n)
├── color.ls        // rg/RG/g/G/k/K/cs/CS/sc/SC; color-space conversion
├── font.ls         // Standard 14 mapping; embedded font hand-off to C side
├── image.ls        // Do operator + image XObject; Form XObject expansion
├── coords.ls       // PDF-space ↔ SVG-space transform helpers
├── svg.ls          // <svg>, <g>, <path>, <text>, <tspan>, <image> element builders
├── html.ls         // multi-page <html> shell, text-selection layer
└── util.ls         // shared helpers (matrix mult, decode hex strings, etc.)
```

Estimated **~3,500–4,500 lines of Lambda Script**, replacing **~2,000–2,500 lines** of the imperative C++ in `radiant/pdf/pdf_to_view.cpp` + `operators.cpp`.

### 3.3 Public API

```lambda
// lambda/package/pdf/pdf.ls

// Convert a parsed PDF (output of input(path, 'pdf')) into an HTML document
// containing one <svg> per page plus optional text-selection layer.
pub fn pdf_to_html(pdf: Map, opts: Map?) -> Element

// Render a single page as a standalone <svg> element.
pub fn pdf_to_svg(pdf: Map, page_index: int, opts: Map?) -> Element

// Inspect: page count, metadata, etc.
pub fn pdf_page_count(pdf: Map) -> int
pub fn pdf_metadata(pdf: Map) -> Map
```

Options map (all optional):

```lambda
{
  text_layer: true,       // emit transparent HTML text layer for selection
  embed_fonts: false,     // inline @font-face rules with base64 (Phase 2)
  scale: 1.0,             // viewport scale; 2.0 for HiDPI rasterization
  page_range: 1..10,      // which pages to render
  include_links: true,    // emit <a> overlays for link annotations
  background: 'white'     // page background color
}
```

### 3.4 Graphics-State Stack — Functional Implementation

PDF's `q`/`Q` operators push/pop graphics state. In Lambda we model the state as an **immutable map**, and the stack is a list of those maps. Each operator returns an updated state; no mutation.

```lambda
// graphics state record
type GState = {
  ctm:           [float; 6],   // current transformation matrix
  fill_color:    Color,
  stroke_color:  Color,
  line_width:    float,
  line_cap:      int,
  line_join:     int,
  miter_limit:   float,
  dash:          [float],
  font:          FontRef?,
  font_size:     float,
  text_matrix:   [float; 6],
  text_line_matrix: [float; 6],
  char_spacing:  float,
  word_spacing:  float,
  leading:       float,
  rise:          float,
  clip:          [PathOp]?
}

// interpret a stream into an SVG element subtree
fn interpret(ops: [Op], state: GState, stack: [GState]) -> (Element, GState, [GState])
```

This is a direct port of the C++ `PDFStreamParser` struct, but pure-functional. Pattern matching via `match` dispatches on the operator tag.

---

## 4. Mapping Examples

### 4.1 Text — `BT / Tf / Tm / Tj / ET`

PDF:
```
BT
  /F1 12 Tf
  1 0 0 1 100 700 Tm
  (Hello) Tj
ET
```

Lambda interpreter produces:

```lambda
<text transform: "matrix(1 0 0 -1 100 92)"  // y flipped to SVG space
      font-family: "Helvetica" font-size: "12"
      fill: "rgb(0,0,0)">
  "Hello"
</text>
```

### 4.2 Path — `m / l / c / h / S`

PDF:
```
100 100 m
200 100 l
200 200 l
h
S
```

Lambda interpreter produces:

```lambda
<path d: "M 100 692 L 200 692 L 200 592 Z"
      stroke: "rgb(0,0,0)" fill: "none" stroke-width: "1"/>
```

### 4.3 TJ Kerning Array

PDF:
```
[(He) -50 (l) 20 (lo)] TJ
```

Lambda interpreter produces:

```lambda
<text transform: "..." font-family: "..." font-size: "12">
  <tspan>"He"</tspan>
  <tspan dx: "0.6">"l"</tspan>      // -50/1000 * 12 = -0.6 → adjust
  <tspan dx: "-0.24">"lo"</tspan>
</text>
```

### 4.4 Multi-Page Wrapper

```lambda
<html>
  <head>
    <style>
      .pdf-page { display: block; margin: 8px auto; box-shadow: 0 1px 3px rgba(0,0,0,.3); background: white; }
      .pdf-page svg { display: block; }
      .pdf-page svg text { user-select: text; }
    </style>
  </head>
  <body class: "pdf-document">
    <div class: "pdf-page" data-page: "1">
      <svg viewBox: "0 0 612 792" width: "612" height: "792"> ...page 1... </svg>
    </div>
    <div class: "pdf-page" data-page: "2">
      <svg viewBox: "0 0 612 792" width: "612" height: "792"> ...page 2... </svg>
    </div>
  </body>
</html>
```

---

## 5. PDF.js Lessons Adopted

| PDF.js technique | Adopted in Lambda PDF package |
|------------------|-------------------------------|
| Operator-list intermediate representation | `stream.ls` produces `[Op]` list before interpretation |
| Line-clustering of text runs (used by PDF.js to build its text layer) | Same heuristic, but applied to SVG `<text>` directly — see §2.6 |
| Standard 14 font CSS fallback table | `font.ls` table mapping (Helvetica → "Arial, sans-serif", etc.) |
| Per-page lazy rendering | `pdf_to_svg(pdf, n)` is page-scoped; consumer can render on demand |
| Image XObject as `<img src="data:...">` | `image.ls` emits data URIs |
| CMYK → sRGB approximation table | `color.ls` |
| Form XObject as recursively interpreted op-list | `interp.ls` recurses with saved CTM |

PDF.js techniques **not** adopted:

- **Worker thread architecture** — N/A in Lambda (single-threaded scripting context)
- **Direct canvas rendering** — we go through SVG instead, since Radiant already has an SVG renderer
- **Separate HTML text layer** — we instead enhance SVG `<text>` emission (line clustering + whitespace synthesis) so native SVG selection matches HTML quality (see §2.6)
- **Web font loader / font glyph rebuilding** — kept in C++ (`fonts.cpp` + FreeType)

---

## 6. Implementation Roadmap

| Phase | Deliverable | Lines (est.) |
|-------|-------------|--------------|
| **Phase 1: Skeleton** | `pdf.ls`, `resolve.ls`, `stream.ls`; emit empty `<svg>` per page with correct viewBox | ~600 |
| **Phase 2: Text** | `interp.ls`, `text.ls`, `font.ls`; render text-only PDFs (e.g., simple academic papers) | +1,000 |
| **Phase 3: Paths & Color** | `path.ls`, `color.ls`; render vector graphics, charts, line art | +800 |
| **Phase 4: Images** | `image.ls`; embed raster images via data URI (decoding stays in C) | +400 |
| **Phase 5: HTML shell + text-selection enhancement** | `html.ls` multi-page nav; line-clustering pass in `text.ls` for high-quality SVG text selection | +500 |
| **Phase 6: Form XObjects, clipping, patterns** | Recursive XObject expansion, `<clipPath>`, basic gradients | +700 |
| **Phase 7: Outstanding content-stream operators** | Implement remaining PDF operators currently no-op'd by the interpreter (see §6.1) | +600 |
| **Phase 8: Parity & retire C++ pipeline** | Replace `radiant/pdf/pdf_to_view.cpp`; remove C++ interpretation path once parity is reached | — |
| **Phase 9+ (deferred):** Annotations (highlights, notes, links beyond basic), form fields, JavaScript actions, Tagged-PDF accessibility mapping | Out of scope for initial release | — |

### 6.1 Phase 7 — Outstanding Operators

Audit of the current dispatcher (`interp.ls`, `text.ls`, `path.ls`) against the PDF 1.7 / ISO 32000-1 operator set and PDF.js's interpreter shows the following operators are accepted but silently no-op'd through the catch-all in `text.ls`. Phase 7 implements them in priority order.

| PDF op | pdf.js IR name | Module | Effect of absence today | Priority |
|---|---|---|---|---|
| `Tc` | setCharSpacing | `text.ls` | Inter-glyph spacing wrong; concatenated words | High |
| `Tw` | setWordSpacing | `text.ls` | Word gaps wrong on space character | High |
| `Tz` | setHScale | `text.ls` | Horizontally-stretched text rendered at 100% | High |
| `Ts` | setTextRise | `text.ls` | Super/subscript on baseline | High |
| `Tr` | setTextRenderingMode | `text.ls` | Stroked / clipped / invisible text rendered as fill | Medium |
| `W`, `W*` | clip / eoClip | `path.ls` | Clipping regions ignored — content bleeds outside intended bounds | High |
| `sh` | shadingFill | new `shading.ls` | Gradients & shadings invisible | Medium |
| `BI` / `ID` / `EI` | beginInlineImage / endInlineImage | `image.ls` | Inline images dropped | Medium |
| `MP`, `DP` | markPoint, markPointProps | `interp.ls` (no-op ack) | Marked-point metadata lost (cosmetic) | Low |
| `BMC`, `BDC`, `EMC` | beginMarkedContent*, endMarkedContent | `interp.ls` (no-op ack) | Tagged-PDF / accessibility regions lost | Low |
| `BX`, `EX` | beginCompat / endCompat | `interp.ls` (no-op ack) | Compatibility section unmarked | Low |
| `ri`, `i` | setRenderingIntent / setFlatness | `interp.ls` (no-op ack) | Cosmetic only; ack to silence dispatcher | Low |
| `d0`, `d1` | setCharWidth / setCharWidthAndBounds | `font.ls` | Type-3 font glyph procs not supported | Low (rare) |

**Implementation notes:**

- `Tc`/`Tw`/`Tz`/`Ts` extend `GState` (already declared in §3.4) and feed into the `text.ls` glyph-positioning math; `Tw` only applies to literal space-character (`0x20`) advances.
- `Tr` maps to SVG paint mode: 0=fill, 1=stroke, 2=fill+stroke, 3=invisible (skip emit), 4–7=add-to-clip variants (combine with `<clipPath>`).
- `W`/`W*` mark the *current path* as the next clip — applied at the next path-paint operator (`n`, `S`, `f`, …) by wrapping subsequent emits in `<clipPath id="...">` + `clip-path="url(#...)"`.
- `sh` requires a small `shading.ls` translating PDF shading dicts (Types 2/3 axial/radial) into SVG `<linearGradient>` / `<radialGradient>`.
- `BI…ID…EI` reuses `image.ls` decoding helpers; the inline image dict is already adjacent in the stream.
- `BMC`/`BDC`/`EMC` should be tracked as a stack so Phase 9 accessibility work can hook in, but emit nothing for now.
- Add an explicit `_op_noop_ack(opr)` in `interp.ls` so the dispatcher stops falling through to the text-module catch-all for non-text operators (cleaner debug logs).

**Acceptance:** every operator above is dispatched explicitly (no silent fall-through), and dedicated `test/lambda/pdf/phase7_*.ls` cases cover at least `Tc`+`Tw`, `Ts`, `Tr` mode 1 (stroked text), `W` clipping, and one `sh` axial gradient with golden `.txt` outputs.

Total Lambda Script: **~4,000 lines**.
C++ retained: parser (~3,200) + low-level helpers (~600).
C++ retired: ~2,000 lines from `radiant/pdf/pdf_to_view.cpp` + parts of `operators.cpp`.

---

## 7. Decisions Made

1. **Output format: SVG-only for page content, HTML as multi-page shell.** No separate HTML text layer — selection quality is achieved by line-clustering inside the SVG text emitter (§2.6).

2. **C-side parser extensions (committed to this proposal):**
   - **Attach `to_unicode: Map` per font dict.** `input-pdf.cpp` parses each font's `/ToUnicode` CMap stream and exposes a `cid → unicode-string` map directly on the font Map. (Implementation included with this proposal.)
   - **Flatten the page tree.** `input-pdf.cpp` resolves `/Type /Pages` recursively and exposes a top-level `pages: [Map]` array on the root, where each entry is a fully-dereferenced page Map with inherited resources/MediaBox merged in. (Implementation included with this proposal.)
   - **Pre-tokenize content streams (deferred to Phase 2):** add `ops: [(name: string, operands: [Item])]` to each content stream. Decision deferred until profiling confirms it's needed.

3. **Coexistence strategy.** The Lambda package will eventually **replace** `radiant/pdf/pdf_to_view.cpp`, but during development both pipelines coexist behind a flag (`--pdf-engine=cpp` default → switch to `ls` → retire `cpp`). Parity validated against the `vibe/Pdf_View_Testing.md` corpus.

4. **Annotations scope.** Phase 5 covers link annotations only (`<a>` overlays inside the SVG). Highlights, notes, form fields, JavaScript actions → **deferred to Phase 7+**.

## 7.1 Remaining Open Questions

1. **`format(elem, 'svg')` robustness.** Need to confirm SVG-namespace handling (xmlns, xlink:href), self-closing tags, and CDATA escaping for `<style>` blocks. If gaps exist, fix in `lambda/format/format-html.cpp` (or add a dedicated `format-svg.cpp`).

2. **Radiant SVG loader stress under data-URI images.** Multi-MB base64 `<image>` payloads from scanned PDFs may stress Radiant's SVG loader. Worth a feasibility spike before Phase 4.

3. **Performance budget.** Lambda script (JIT via MIR) likely 5–20× slower than the C++ interpreter for tight loops. Profile with a 50-page PDF at end of Phase 2; if text rendering is the hot path, escalate the deferred decision to pre-tokenize content streams in C.

4. **CSS supported by Radiant for the HTML shell.** Confirm `box-shadow`, `position: absolute`, `inset`, `transform-origin` work; otherwise simplify the wrapper.

5. **Tagged-PDF / accessibility.** Mapping `/StructTreeRoot` to semantic SVG (or per-line `aria-*` attributes) — worthwhile but sized for a later phase.

6. **Embedded subsetted fonts beyond ToUnicode.** Even with the `to_unicode` map, glyph **shapes** for non-Standard-14 subsets need FreeType rendering. C side will need to either (a) emit `<image>` glyphs for unmapped fonts, or (b) ship the font subset to the browser via `@font-face`. Decision deferred to Phase 2 implementation.

---

## 8. Suggestions & Risks

### 8.1 Suggestions

- **`widths` arrays as first-class field** in font dicts at the parser level (alongside the now-implemented `to_unicode`). Avoids the Lambda font module re-walking arrays for every text run.

- **Reuse the chart package's SVG element builders.** `lambda/package/chart/svg.ls` already has helpers for `<svg>`, `<g>`, `<path>`, `<text>`. Either share via a common util or copy-then-extend.

- **Build a small testing corpus early.** 5–10 representative PDFs (text-only, vector-graphics-only, mixed, scanned image, multi-page, kerning-heavy) committed to `test/lambda/pdf/`. Each PDF gets a `.svg.txt` golden output for the first page. Use `make test-lambda` to lock in regressions.

- **Generate one self-contained SVG file as the unit of truth.** Even when wrapped in HTML, each page's SVG should be valid standalone. This makes debugging and golden-testing far easier.

- **Re-evaluate pre-tokenizing content streams** at end of Phase 2 based on profiling, not up front.

### 8.2 Risks

| Risk | Mitigation |
|------|-----------|
| Performance: Lambda interpreter slower than C++ for complex PDFs | Profile end of Phase 2; if needed, escalate to pre-tokenizing content streams in C; keep C path as fallback during development |
| Font fidelity: embedded fonts not faithfully rendered | `to_unicode` map (now in parser) covers text correctness; glyph shape via FreeType in C; emit `<image>` for glyphs only as last resort (Phase 7) |
| SVG file size for vector-heavy PDFs | Use SVG `<symbol>`+`<use>` for repeated Form XObjects; consider gzip in the Radiant load path |
| SVG `<text>` selection fragmented across runs | Line-clustering pass in `text.ls` (§2.6) merges runs into one `<text>` per visual line |
| Color-space accuracy (ICC profiles) | Phase 1 ships sRGB approximation; full ICC support deferred |
| Encrypted PDFs | Out of scope; parser already rejects them with an error |

---

## 9. Alignment with Existing Lambda Patterns

| Pattern | Used by | This package |
|---------|---------|--------------|
| Pure-Lambda transformation pkg over C parser | `lambda/package/latex/` (over tree-sitter-latex) | Same: Lambda over `input-pdf.cpp` |
| Element-tree builder + `format(_, target)` | `lambda/package/chart/` (→ SVG) | Same: → SVG, then HTML wrapper |
| `match` on tag for dispatch | `latex/render.ls`, `chart/mark.ls` | `interp.ls` matches PDF op names |
| Immutable state passed through fns | Math package context | Graphics state passed through interpreter |
| C-side parser produces clean Lambda tree | `input-latex-ts.cpp`, `input-json.cpp` | `input-pdf.cpp` (already done — see [`input-pdf.cpp`](lambda/input/input-pdf.cpp)) |

The proposal is a natural application of patterns the codebase has already validated three times.
