# LaTeX package compatibility in Lambda script

**Status:** Initial implementation landed in the working tree, 2026-10-06. No formal ruling is changed by this document.

**Scope:** Common LaTeX package behavior in the shipped `lambda.latex` package, targeting the existing HTML element output and its Radiant rendering path. “Support” means the documented subset works when the corresponding `\usepackage` is declared; it does not mean executing arbitrary `.sty` files or promising byte-identical TeX output.

**Formal linkage:** [D7.2.1–D7.2.4](../doc/Lambda_Formal_Design.md#d72-script-packages) place shipped packages in source modules with immutable module bindings and the `lambda.*` namespace. [S16.9.8](../doc/Lambda_Formal_Semantics.md#s169-declarations-elements-paths) governs imports; [S1.8](../doc/Lambda_Formal_Semantics.md#s1-core-principles) forbids treating document strings as executable code. [S12.1.1v2](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system) and [S12.4.1](../doc/Lambda_Formal_Semantics.md#s124-resources) govern pure transforms and eager input. [S7.4.1–S7.4.4](../doc/Lambda_Formal_Semantics.md#s74-the-three-failure-channels) govern diagnostics. [S2.6.3–S2.6.5](../doc/Lambda_Formal_Semantics.md#s26-content) and [D2.6.5v4](../doc/Lambda_Formal_Design.md#d26-containers-and-array-storage) govern the current command-argument array contract, whose implementation is recorded in [Input_Latex.md](input/Input_Latex.md#current-command-content-contract-2026-10-02). The package contracts proposed below require review before they become rulings.

## Implementation snapshot

The static [`packages/registry.ls`](../lmd/package/latex/packages/registry.ls) activates named script adapters from `\usepackage`, checks package options, and adds diagnostics with source offsets. [`latex.render_result`](../lmd/package/latex/latex.ls) returns the body, element tree, stylesheet, metadata, diagnostics, and asset references; the older render projections remain. The direct parser preserves a command's star, raw balanced argument groups, source offsets, and `\item` optional labels while retaining the positional children required by **S2.6.3–S2.6.5, D2.6.5v4**.
Pass `base_uri` in render options for relative resources. Pass `target: "pdf"` or `target: "svg"` to receive output-specific diagnostics where the current renderer cannot honor package CSS. The file entry points derive `base_uri` from the file path.

| Adapter | Landed profile | Remaining limitation |
|---|---|---|
| `amsmath`, `amssymb` | Script math bridge for single equations, starred names, and bounded `align`/`gather`/`multline` rows and columns; single-equation numbering and math symbol table reuse | TeX-exact alignment spacing, row numbering, tags, and operator declarations are pending. |
| `graphicx` | Document-relative raster paths; PDF page selection through `lambda.pdf.pdf`; TeX dimension conversion, relative widths, scale, angle, CSS trim/clip, and sized PDF SVG wrappers | PDF embedded images lacking data URIs are diagnosed in serialized output; full TeX bounding-box semantics and PDF-export clipping remain. |
| `hyperref` | HTML references, URLs, `\hypersetup` colors and metadata | PDF link annotations, outlines, and metadata export need a host contract. |
| `geometry` | Fixed paper and margins as CSS | Radiant's PDF export currently ignores `@page` size and margins. |
| `xcolor` | Existing named/model colors plus two-color sRGB mixing | Full xcolor model and color-series syntax remain. |
| `booktabs` | Existing full-width rules, column-span `\cmidrule(lr){m-n}` with common trim flags, and `\addlinespace` | Exact TeX rule spacing remains. |
| `biblatex` | Script `.bib` reader, numeric `\cite`/`\printbibliography`, and deterministic `nty`/`none` ordering | Biber data model, locale sorting, many entry types and citation styles remain. |
| `enumitem` | Parsed `\item[label]`, list `start`/`resume`, basic label style and left margin | Full nesting policies and detailed spacing remain. |
| `microtype` | CSS kerning, tracking, word spacing, hyphenation, justification | Exact protrusion and expansion remain Appendix A. |
| `siunitx` | Common number grouping, unit/quantity commands, and composable prefix tokens | Uncertainties, scientific notation, and measured `S` columns remain; an `S` column emits a diagnostic. |
| `TikZ/PGF` | Adapter delegates pictures to the existing script TikZ engine | The engine's bounded drawing profile still applies. |

No runtime tests were run for this implementation; this table describes the code paths added, not verified conformance.

## 1. Current baseline

- [`lmd/package/latex/latex.ls`](../lmd/package/latex/latex.ls) provides `render`, `render_to_html`, and file/string entry points. Its analysis pass in [`analyze.ls`](../lmd/package/latex/analyze.ls) collects headings, counters, labels, bibliography items, and colors; [`render.ls`](../lmd/package/latex/render.ls) emits HTML elements. `\usepackage` is currently skipped in rendering and does not activate a package.
- Existing script behavior covers parts of the target set already: `\includegraphics` options, links and references, color, table rules, lists, and simple `\cite`/`thebibliography`. The separate script math renderer is called through [`math_bridge.ls`](../lmd/package/latex/math_bridge.ls). The TikZ bridge calls the existing [`lambda.doc.tikz`](../lmd/package/doc/tikz/tikz.ls) script implementation; its bounded profile is recorded in [Lambda_Pkg_Tikz.md](Lambda_Pkg_Tikz.md).
- The shipped [`lambda.pdf.pdf`](../lmd/package/pdf/pdf.ls) script package already reads parsed PDF pages and exposes `pdf_page_count` and `pdf_to_svg(pdf, page_index, opts)`. The LaTeX `\includegraphics` handler does not yet call it for a `.pdf` source.
- The production [`DirectLatexParser`](../lambda/input/input-latex-c.cpp) recognizes generic commands and environments. It flattens ordinary braced argument contents into positional children, retains bracket groups as elements, and stores raw source for `tikzpicture`. The current command-content contract is recorded in [Input_Latex.md](input/Input_Latex.md). Older `vibe/latex` notes describe a `lambda/tex` JSON package loader, but that directory is absent from the current tree; it is not the implementation path for this proposal.
- The current output target is HTML elements/strings. Radiant can render those elements and has paged-media machinery, but TeX-specific PDF annotations, exact TeX page layout, and font microtypography are not established as available package APIs here.

## 2. Package model

Place adapters under `lmd/package/latex/packages/`, for example `amsmath.ls`, `graphicx.ls`, and `hyperref.ls`. They remain ordinary Lambda script modules imported by a static registry in `lmd/package/latex/`. `\usepackage[options]{a,b}` selects named adapters from that registry; its text never becomes a dynamic Lambda import, source evaluation, or a native `.sty` loader (**S1.8, D7.2.4**). A document class supplies its existing baseline behavior. The registry records supported names, aliases, dependencies, option schemas, and package-specific hooks.

Run the document through four script stages:

1. **Preamble:** Read `\documentclass`, ordered `\usepackage` declarations, options, and explicit package dependencies. Reject an unknown package or unsupported option with a located diagnostic; do not silently ignore it. Duplicate loads with compatible options are idempotent; conflicting options produce a diagnostic.
2. **Analysis:** Fold the AST in source order into an immutable document context. Package hooks register commands, environments, styles, counters, labels, citation data, and requested assets. Scope declarations to groups and environments where LaTeX semantics require it. Retain the existing two-pass reference model, extending it for citations and package state.
3. **Rendering:** Dispatch a node first through the active package handler, then existing core handlers and user macros. Handlers return semantic HTML/SVG elements and metadata, not raw unchecked markup. The current generic fallback must report unsupported package commands instead of rendering their arguments as if the command had worked.
4. **Finalization:** Assemble the body, stylesheet, document metadata, diagnostics, and asset references. Existing `render*` APIs remain convenience projections of this result. A structured result lets a PDF or view host consume link, page, and asset requests without scraping serialized HTML.

Shared script helpers should parse balanced key/value options, dimensions, colors, counters, and scoped declarations once. The present comma-splitting [`util.parse_kv_options`](../lmd/package/latex/util.ls) is insufficient for values containing groups or nested commas. Package modules should reuse it only for the simple subset it actually handles; extend one shared parser before adding more option dialects. Keep per-document data in values passed through hooks, consistent with immutable module bindings (**D7.2.1**). Resource acquisition and path resolution belong at the input boundary; rendering the resulting data remains a pure transform (**S12.1.1v2, S12.4.1**).

## 3. Proposed compatibility slices

Each row is an initial, testable profile, not a claim of complete CTAN compatibility. The CTAN links describe the upstream packages.

| Package | Lambda script implementation in `latex/packages/` | Current reuse and limit |
|---|---|---|
| [`amsmath`](https://ctan.org/pkg/amsmath) | Register equation and alignment environments, tags/numbering, operator declarations, and common fraction/text commands. Adapt its AST to the existing script math box renderer. | Reuse `lambda.doc.math`; retain source-order equation context. Complex alignment and starred forms need faithful parser data before admission. |
| [`amssymb`](https://ctan.org/pkg/amsfonts) | Map additional AMS symbols and math alphabet commands to the existing math symbol/font model. | Some symbols already exist in `lmd/package/math/symbols.ls`; verify glyph/font coverage rather than substituting a visually different character. |
| [`graphicx`](https://ctan.org/pkg/graphicx) | Resolve image paths against the document, parse `\includegraphics` keys, and emit sized, rotated, clipped image elements with preserved aspect ratio. For `.pdf`, parse with `input(..., 'pdf')` and embed the selected page from `lambda.pdf.pdf.pdf_to_svg`. | Basic HTML image output and the PDF-page renderer already exist. Wire them together and verify page selection, sizing, cropping, and serialization as described below. EPS is deferred to Appendix B. |
| [`hyperref`](https://ctan.org/pkg/hyperref) | Resolve `\href`, `\url`, `\ref`, `\autoref`, anchors, and PDF-string metadata from the document model; emit stable targets and links. | HTML links already exist. Clickable PDF annotations, outlines, and metadata need a verified host export contract. |
| [`geometry`](https://ctan.org/pkg/geometry) | Convert paper and margin options into page style, with explicit physical-unit conversion and class defaults. | CSS and Radiant paged media provide a route for common fixed page setups. Mid-document geometry changes and TeX-exact text block calculations need separate evaluation. |
| [`xcolor`](https://ctan.org/pkg/xcolor) | Parse named colors, mixes, model conversions, `\definecolor`, and scoped text/background color. | Color elements already exist; add one normalized color representation and reject unsupported color models. |
| [`booktabs`](https://ctan.org/pkg/booktabs) | Map `\toprule`, `\midrule`, `\bottomrule`, `\cmidrule`, and spacing to table row/border semantics. | Basic rules and CSS exist. Partial rules must retain column spans instead of becoming full-width borders. |
| [`biblatex`](https://ctan.org/pkg/biblatex) | Parse supplied `.bib` text in Lambda script, collect citation requests, sort/label entries, and render a bounded set of styles and `\printbibliography`. | Existing `\cite` supports only in-document `\bibitem` numbering. Full Biber data-model, locale, and sorting compatibility is a larger project. |
| [`enumitem`](https://ctan.org/pkg/enumitem) | Apply per-list and per-item label, start, resume, spacing, indentation, and nesting options through list context and CSS. | Core lists exist. `\item[custom label]` currently needs parser preservation (see §4). |
| [`microtype`](https://ctan.org/pkg/microtype) | Apply a CSS approximation using supported kerning, tracking, word spacing, hyphenation, and justification controls where appropriate. Keep option handling and stylesheet selection in Lambda script. | This implementation does not claim glyph protrusion or font expansion. Appendix A records what exact behavior would need from text layout later. |
| [`siunitx`](https://ctan.org/pkg/siunitx) | Parse and format numbers, uncertainties, units, and common quantities; add structured decimal alignment for `S` table columns. | Text formatting is script work. Accurate table alignment depends on the host respecting emitted column structure and measured widths. |
| [`TikZ/PGF`](https://ctan.org/pkg/pgf) | Own the LaTeX `\usepackage{tikz}` adapter, declaration scope, diagnostics, and figure placement in `latex/packages/tikz.ls`. | Reuse the existing Lambda-script `lambda.doc.tikz` drawing engine and preserved `tikzpicture` island. Extend the bounded profile there; do not duplicate its parser or renderer. |

The first release should prioritize `amsmath`/`amssymb`, `graphicx` including PDF pages, `hyperref` for HTML, `xcolor`, `booktabs`, `enumitem`, `siunitx`, and the CSS-only `microtype` profile. Add `geometry` and a bounded `biblatex` profile once resource and output contracts are clear. Treat full TikZ/Biber compatibility as explicit later profiles rather than claiming them from command recognition alone. Existing script behavior may remain available to old documents during migration; new conformance claims should be tied to package activation and supported options.

### 3.1 PDF graphics integration

For `\includegraphics{figure.pdf}`, the LaTeX adapter should resolve the document-relative path, call `input(path, 'pdf')`, validate the selected one-based `page` option against `pdf_page_count`, and embed `pdf_to_svg(pdf, page - 1, {show_label: false, id_prefix: unique_figure_id})` as an element. Apply `graphicx` width, height, scale, angle, and clipping to the resulting SVG wrapper. This reuses the PDF package; it does not require a new PDF decoder in the LaTeX package.

The PDF package now intersects `CropBox` with `MediaBox` and applies the normalized clockwise `/Rotate` value to the page SVG and HTML text overlay. This follows the [PDF page dictionary and page-boundary rules](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf). The LaTeX `\includegraphics` adapter calls that API. Remaining integration gaps are:

- [`pdf.pdf_to_svg`](../lmd/package/pdf/pdf.ls) now uses [`page_geometry`](../lmd/package/pdf/coords.ls) for cropping and rotation. Verify the result when `graphicx` combines a selected PDF page with its own width, height, angle, and clipping options.
- [`pdf/image.ls`](../lmd/package/pdf/image.ls) now prefers an available data URI. It may still emit `img:<object_num>` handles for embedded images without one or an unresolved Form XObject placeholder. Radiant resolves image handles through a registry tied to the SVG element, whereas the LaTeX HTML string serializer only writes attributes. The graphicx adapter diagnoses these pages instead of emitting broken serialized content; a future generic asset export path could retain images.
- The PDF interpreter has a bounded feature set. Verify a vector-only page, a page with an embedded image, a rotated/cropped page, multiple pages, and a malformed or unsupported file. Report unsupported PDF content explicitly rather than treating a returned SVG element as proof of complete fidelity.

## 4. Engine boundary: what scripts cannot recover or control today

The following are capability requests, not permission to move package semantics into C/C++. Where a request is accepted, native code should expose neutral syntax or rendering primitives; the package-specific policy stays in `lmd/package/latex/`.

| Need | Evidence and consequence | Proposed boundary |
|---|---|---|
| **Faithful command syntax** — landed for commands | [`DirectLatexParser::parse_command`](../lambda/input/input-latex-c.cpp) now retains stars, ordered raw argument groups and offsets, and `\item` labels. Existing normalized positional children remain (**S2.6.3–S2.6.5, D2.6.5v4**). More package syntax may still need neutral AST preservation as profiles expand. | Continue extending the Lambda-owned direct parser only where source distinctions are otherwise unrecoverable; keep package policy in script. |
| **Document-relative resources** — addressed at the file entry point | `render_file*` now supplies `base_uri` from the input path; direct `render(ast, options)` callers may pass one. `graphicx` and `biblatex` use it for relative image and `.bib` paths. | If host document transforms must infer source identity from a parsed AST, expose it through a neutral input API (**D7.1.2v2**). |
| **PDF links and metadata** — likely needed for full `hyperref` | HTML `<a>` elements are available, but no package-visible PDF annotation/outline contract was found in the current LaTeX pipeline. HTML link output alone does not establish PDF support. | Add a general link/anchor/outline export path only after checking Radiant's PDF renderer. Keep target naming, reference resolution, and PDF-string policy in script. |
| **Graphic clipping on PDF export** — needed for `graphicx` trim/clip | Radiant's support matrix says CSS `clip-path` works in raster output but is ignored by PDF and SVG output ([HTML/CSS/SVG support](../doc/HTML_CSS_SVG_Support.md)). The script adapter emits `clip-path: inset(...)` after converting TeX units, so HTML/raster can apply it, but PDF export cannot. | Add generic clip-path support to vector export, or expose a neutral pre-export image crop primitive. The graphicx option policy remains in script. |
| **Exact print geometry** — needed for `geometry` PDF output | The current support matrix says `@page` is parsed only and PDF export takes neither page size nor margins from it ([HTML/CSS/SVG support](../doc/HTML_CSS_SVG_Support.md)). Script emits fixed page CSS for HTML, but cannot make PDF export honor it. | Add generic page size and margin controls to Radiant's PDF export; keep geometry option policy in script. |
| **Locale-exact bibliography collation** — possibly needed for full `biblatex` | A small `.bib` reader and common citation styles are ordinary Lambda script work, but Biber-compatible Unicode sorting, name rules, and locale tailoring require a separately defined collation contract. | Start with deterministic script ordering and documented styles. Add a general Unicode collation service only if fixtures demonstrate that script facilities cannot meet the target. |

The direct parser preservation has landed. PDF `hyperref` and exact `geometry` behavior need host capabilities; locale-exact `biblatex` sorting may need one depending on fidelity. PDF graphics use the existing PDF package and the integration work in §3.1. Future full `microtype` and EPS support are recorded in the appendices, outside this implementation. None requires embedding package-specific handlers in the engine.

## 5. Delivery and acceptance

1. Establish a small package registry and a structured document result while preserving the existing public render entry points. Record a supported-command/option matrix for every adapter.
2. Fix the neutral parser-data losses from §4, then add focused `.ls` adapters in the priority order above. Update the math bridge and shared option parser once rather than copying logic across packages.
3. Use representative upstream package examples as comparison fixtures, plus local documents combining packages, options, nested scopes, duplicate loads, missing resources, unknown commands, and forward references. Every new `*.ls` test script gets its corresponding `*.txt` expected result. Include both element-tree and serialized-HTML PDF image fixtures from §3.1. Compare rendered HTML structure and, where applicable, Radiant page/PDF output; a successful parse alone is insufficient.
4. Mark each feature **supported**, **partial**, or **unsupported** per output target. An unsupported request must return a specific diagnostic with the package, command/option, and location (**S7.4.1–S7.4.4**). Do not approximate a command merely because its arguments can be rendered.

## Appendix A. Full `microtype` requirements, deferred

This implementation uses a CSS approximation. The LaTeX script adapter may emit `font-kerning`, `letter-spacing`, `word-spacing`, `hyphens`, and `text-align` rules where the target renderer supports them. Explicit requests for unsupported protrusion or expansion settings receive a diagnostic rather than a silent no-op. These controls can improve visual spacing, but they do not reproduce the two defining `microtype` behaviors described by [CTAN](https://ctan.org/pkg/microtype):

- **Character protrusion:** selected glyph edges, especially punctuation, may extend beyond the nominal text block. Accurate behavior needs per-glyph protrusion tables and line-edge placement after shaping. A CSS margin or transform on a paragraph cannot identify and position the glyph at each final line edge.
- **Font expansion:** eligible glyphs are made slightly wider or narrower within configured limits to improve line fitting. The line breaker must account for those altered advances when choosing breaks, and painting/PDF export must use the same geometry. Fixed `letter-spacing` or `word-spacing` cannot make the same per-line choice.

If exact behavior is later required, expose generic font metrics, protrusion allowances, expansion limits, and consistent shaping/line-breaking/painting controls in Radiant. The `microtype` adapter would select profiles and settings in Lambda script. That engine work is outside the implementation proposed here; acceptance for this phase is only the documented CSS approximation.

## Appendix B. EPS graphics, deferred

EPS decoding, bounding-box interpretation, and conversion for `\includegraphics` are outside this implementation. An EPS source receives an unsupported-format diagnostic. A future proposal may add a neutral EPS import/conversion capability and reuse the `graphicx` sizing and clipping script logic. PDF pages remain in scope through §3.1 and the existing PDF package.

No runtime tests were run for this implementation; the status and limitations above come from source inspection and the cited package descriptions.
