# Lambda TikZ Package — PGFPlots and LaTeX Graphics

**Status:** Initial native subset implemented in the working tree; the broader profile below remains a proposal.

**Date:** 2026-09-28.

**Scope:** A shipped Lambda package for native TikZ drawing and PGFPlots plotting inside LaTeX documents, with a bounded compatibility profile.

**Source review:** Current working tree based on `8d1205ec1`; implementation observations below are source inspections, not test results.

**Spec linkage:** [D7.2.1–D7.2.4](../doc/Lambda_Formal_Design.md#d72-script-packages) govern package ownership, source distribution, and namespace; [D7.1.2v2 and D7.1.5](../doc/Lambda_Formal_Design.md#d71-build-packaging-and-layering) govern IO and Mark construction; [S12.1.1v2](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system) governs pure rendering versus procedural effects; [S1.8](../doc/Lambda_Formal_Semantics.md#s1-core-principles) excludes runtime string execution; [S7.4](../doc/Lambda_Formal_Semantics.md#s74-the-three-failure-channels) governs errors. No existing formal ruling is revised here. Proposed package contracts remain recommendations pending ratification; no new ruling-ID series is introduced.

**Implemented slice:** `parse(source, {type: "tikz"})` builds a bounded Mark syntax tree; `lambda.doc.tikz.tikz.render(source)` and `.render_ast(island)` render simple 2D line paths, positioned labels, coordinate/function line and scatter plots, linear/log axes, grids, and legends. The LaTeX bridge preserves raw `tikzpicture` source, embeds successful plots, and shows a source-bearing diagnostic when rendering fails. It reuses chart scale/SVG helpers and the math package's measured box entry point under **D7.2.1–D7.2.4**. This is narrower than the proposed first plotting release in §4: scope/style execution, named coordinates, richer paths, inline tables, explicit tick policy, clipping outside axis limits, portable math-label SVG, and measured plain-text labels remain unimplemented. Unsupported constructs fail visibly rather than being approximated.

## 1. Recommendation

Add **`lambda.doc.tikz`**, with TikZ and PGFPlots as two front ends sharing one drawing pipeline:

```text
LaTeX document                         Standalone TikZ / Lambda plot data
       |                                             |
preserved graphics source                    parser / data adapter
       +---------------------+-----------------------+
                             |
                 source-faithful TikZ Mark
                             |
          scoped options + bounded expression evaluation
                             |
              TikZ drawing model / PGFPlots axes
                             |
             label measurement + final geometry
                             |
                    shared picture scene
                             |
           SVG geometry + measured text/math labels
                             |
          LaTeX HTML / Radiant view / document export
```

The first useful release should render ordinary **2D scientific plots**: coordinate and function plots, linear/log axes, ticks, grids, legends, and mathematical labels. Implement the TikZ drawing primitives needed by these plots and common annotations first. Broader diagram libraries, 3D surfaces, and arbitrary TeX programming come later, if justified by a fixture corpus.

This is a native implementation of a **documented subset of the input languages**, not a port of all PGF TeX macros. Normal rendering should require no TeX installation, browser JavaScript, network service, or child process. Use upstream TeX rendering as an offline comparison oracle. An explicitly selected external renderer can be evaluated separately for documents outside the native profile.

PGF is the underlying graphics macro system; TikZ is its drawing syntax, and PGFPlots builds plots on top of PGF/TikZ. Consequently, recognizing `\addplot` alone is insufficient: plotting also needs scoped styles, coordinates, paths, text placement, and clipping. [PGF package](https://ctan.org/pkg/pgf), [PGFPlots package](https://ctan.org/pkg/pgfplots), [PGFPlots–TikZ interoperability](https://tikz.dev/pgfplots/reference-tikzinteroperability).

## 2. Existing assets and actual gaps

| Area | Current source evidence | Consequence for this proposal |
|---|---|---|
| LaTeX input | [`input-latex-c.cpp`](../lambda/input/input-latex-c.cpp), `DirectLatexParser::parse_environment`, parses ordinary environments into children; math environments separately retain source and a math AST. | Preserve graphics source before generic child parsing. Do not reconstruct TikZ from flattened text. |
| LaTeX package | [`latex.ls`](../lambda/package/latex/latex.ls), `render`, collects macros and analysis before dispatch; [`render.ls`](../lambda/package/latex/render.ls) dispatches `picture` and falls back to generic command rendering. | Add an explicit graphics bridge and scoped graphics context; generic fallback is not a renderer. |
| Existing graphics | [`elements/picture.ls`](../lambda/package/latex/elements/picture.ls), `render_picture`, already emits SVG for the LaTeX `picture` environment. | Preserve that surface and its tests. Share suitable geometry helpers instead of adding a third independent implementation. |
| Charts | [`chart.ls`](../lambda/package/chart/chart.ls), `render_spec`, and [`scale.ls`](../lambda/package/chart/scale.ls), `scale_apply`/`scale_ticks`, implement a chart pipeline. [`svg.ls`](../lambda/package/chart/svg.ls) supplies SVG constructors and paths. | Reuse mechanics after checking their contracts; PGFPlots owns its compatibility rules and defaults. |
| Math | [`math.ls`](../lambda/package/math/math.ls), `render_math`, emits HTML; [`box.ls`](../lambda/package/math/box.ls) already represents width, height, depth, and elements. | Reuse typesetting and metrics. There is no demonstrated general math-to-SVG exporter to assume. |
| Graphs | [`graph/layout.ls`](../lambda/package/graph/layout.ls), `layout_custom`/`from_velmts`, uses measured children; [`graph/scene.ls`](../lambda/package/graph/scene.ls) provides semantic scene comparisons. | Reuse the measurement pattern and comparison approach. A coordinate drawing is not a graph-layout problem. |
| Serialization | [`latex/to_html.ls`](../lambda/package/latex/to_html.ls), `to_html`, serializes element trees; [`format-latex.cpp`](../lambda/format/format-latex.cpp), `format_latex_element`, has a generic command fallback. | Verify SVG names/attributes in HTML, and add explicit graphics-source round trips if emitting LaTeX. |
| Packaging | [`build_ast.cpp`](../lambda/runtime/build_ast.cpp), `append_shipped_package_module_path`, maps `lambda.*` to the package tree; [`prepare_release.sh`](../utils/prepare_release.sh) copies that tree. | Ship ordinary `.ls` sources; no new loader, registry, or package manager is needed. |

The repository contains [`test/latex/fixtures/tikz/`](../test/latex/fixtures/tikz/) and an earlier [LaTeX graphics proposal](latex/Latex_Typeset_Design5_Graphics.md). Some older tracking text names a `tex_pgf_driver.cpp` and partial TikZ support. This review found no corresponding active implementation under `lambda/` or `radiant/`. Treat those documents as historical design context and the fixtures as candidate inputs, not evidence of current conformance. Several fixtures use advanced libraries such as `spy`; they should initially exercise unsupported-feature reporting.

## 3. Package boundary and reuse

### 3.1 Public namespace

Use these entry modules:

```text
lambda.doc.tikz.tikz        TikZ source and drawing entry points
lambda.doc.tikz.pgfplots    PGFPlots source and structured plot entry points
```

Place them under `lambda/package/doc/tikz/`. With the current generic resolver, `lambda.doc.tikz.tikz` maps directly to `package/doc/tikz/tikz.ls`. This follows **D7.2.4** without adding another exceptional mapping like the existing `lambda.doc.math.* → package/math/` mapping. Keep PGFPlots in the same package so paths, options, labels, and rendering have one owner.

The existing LaTeX entry point currently uses `lambda.latex.latex`; changing its namespace is outside this proposal. It can import the new package through its canonical name. Native parsing belongs to `lambda-io`; package logic must not make IO depend on runtime evaluation or Radiant (**D7.1.2v2, D7.1.5**).

### 3.2 Ownership

| Owner | Responsibility |
|---|---|
| Native input adapter | Bounded lexical scanning, source spans, syntax recognition, Mark construction, parse diagnostics. |
| TikZ package | Supported key semantics, scopes, styles, coordinates, expression AST evaluation, drawing normalization. |
| PGFPlots submodule | Data selection, sampling, axis domains, tick policy, legends, plot styles, axis-relative annotations. |
| Shared graphics helpers | Proven common affine/path operations, clipping primitives, SVG construction, reusable scale mechanics. |
| Math/LaTeX packages | Label parsing, math boxes, text formatting, fonts/CSS, captions and cross-references. |
| Host/document layer | Resource acquisition, font readiness, measurement services, rendering, export, cancellation. |

Do not lower every PGFPlots axis directly into the existing Vega-like `chart.render_spec`: default domains, ticks, cycle lists, units, and bounding boxes can differ. First reuse pure scale/path helpers with matching contracts. When an operation needs use by chart, picture, and TikZ, extract that operation into one shared module and retain both existing callers' behavior through adapters and regression tests. Do not fork the chart renderer or introduce a second SVG painter.

Likewise, Graph IR is not a general drawing scene. TikZ's explicit coordinates and path order should survive intact; graph routing should only be involved in a future, explicitly supported graph-drawing library.

## 4. Compatibility contract

### 4.1 A named, finite profile

Publish a versioned capability manifest for the native subset. Each admitted command, environment, key, coordinate form, and expression function must link to positive and negative fixtures. The package version and the upstream `compat` value are different concepts.

As of this review, CTAN lists **PGF 3.1.12** and **PGFPlots 1.18.3**. The browsed HTML PGFPlots manual identifies itself as **1.18.2**. Pin reference-generation tool versions independently of the initial **`compat=1.18` subset**; reconcile relevant manual/release differences against that pinned engine. Do not interpret `compat=1.18` as a claim of complete compatibility. [PGF release metadata](https://ctan.org/pkg/pgf), [PGFPlots release metadata](https://ctan.org/pkg/pgfplots), [HTML manual](https://tikz.dev/pgfplots/reference-tikzinteroperability).

Recommended behavior:

- An explicit supported compatibility value selects the corresponding tested subset defaults.
- Missing `compat` selects the package's pinned profile with a diagnostic. Do not silently inherit whichever upstream version happens to be installed.
- Unknown values and unsupported libraries are reported before drawing. `compat=newest` must not imply unbounded support.
- Parse and retain unknown syntax with its original span when recovery is possible. A valid but unsupported construct is distinct from malformed syntax.
- Strict rendering fails on unsupported semantics. Document preview may instead show an escaped source placeholder plus a diagnostic for the whole affected picture; it must not silently display a plausible but incomplete scientific plot.

### 4.2 Scope by stage

| Feature family | Initial plotting release | Subsequent extensions |
|---|---|---|
| Containers | `tikzpicture`, nested `scope`; `axis`, `semilogxaxis`, `semilogyaxis`, `loglogaxis` | Inline `\tikz`, polar/group axes |
| Paths | `\path`, `\draw`, `\fill`, `\filldraw`, `\clip`; move/line/close, rectangle, circle, ellipse, cubic Bézier; simple arrow tips | Arc variants, richer arrow library, path decorations/intersections |
| Coordinates | Explicit Cartesian coordinates, dimensions, configured x/y basis, named coordinates; `axis cs` | Polar, `+`/`++`, calc expressions, more coordinate systems |
| Nodes and labels | Explicitly positioned text/math; rectangle/circle shapes; center/cardinal/base anchors | Relative positioning, matrices, trees, specialized shapes |
| Styles | Ordered key lists, common colors and mixes, stroke/fill/opacity/dash settings; supported `.style`/`.append style`, `\tikzset`, `\pgfplotsset` | Parameterized styles and more key handlers |
| Plot input | `coordinates`, bounded function expressions, inline numeric tables | Explicitly supplied external tables, additional column transforms |
| Plot types | Line and scatter/`only marks` | Step, bar, area, error bars, stacked plots, heatmaps |
| Axis presentation | Limits, domain, sample count, dimensions, labels/title, explicit and automatic ticks, grids, legend entries, plot cycle, clipping | Symbolic/date coordinates, secondary axes, richer formatting |
| Programming | Literal keys, named styles, supported numeric expression grammar | Bounded `\foreach` and simple parameter substitution if corpus evidence justifies them |
| Advanced PGF ecosystem | Diagnose and preserve | Separately scoped libraries; no promise of arbitrary package loading |

Not included in the native profile: arbitrary TeX expansion, catcode changes, `\def` programming, PGF key `.code` execution, `\directlua`, shell/gnuplot plot sources, TikZ externalization, `remember picture`/page overlays, 3D meshes/surfaces, or arbitrary `\usepackage` extensions. Supporting a library name requires actual semantics and tests; accepting `\usetikzlibrary` is not enough.

The upstream plot language includes external-program inputs as well as ordinary numeric data. These must remain distinguishable in the source model and cannot become an implicit effect in rendering. [PGFPlots coordinate input](https://tikz.dev/pgfplots/reference-addplot).

## 5. Preserve syntax before interpreting it

### 5.1 LaTeX handoff

Teach the direct LaTeX parser to capture a `tikzpicture` as a graphics island with:

- the exact environment source and body/options spans;
- source identity and half-open byte offsets into the original document;
- the environment kind and surrounding scoped graphics declarations;
- no lossy paragraph reconstruction of paths or option lists.

Preserve `\tikzset`, `\pgfplotsset`, library declarations, and supported macro definitions in document order. A single global collection of definitions cannot model styles that change between pictures or inside groups. The bridge should fold declarations through an explicit immutable context and restore it on leaving a scope (**S1.4, D7.2.1**). Initially, unsupported macro-dependent coordinates must receive diagnostics instead of being guessed from the LaTeX package's generic macro output.

Implement source-boundary handling with the existing scanner as a starting point, then extend the shared helper where necessary. Its current group scanner handles nesting and escapes, but does not itself skip comments. The environment-end search also needs tests for commented delimiters. Percent comments, escaped braces, nested environments, and delimiters inside label groups must not terminate the island early. Do not edit the vendored Tree-sitter LaTeX grammar.

### 5.2 One syntax parser

Add one compact native parser, callable both from the LaTeX adapter and from a proposed `input(..., {type: "tikz", source: true})` adapter. It handles a complete picture or a defined fragment mode; fragments use the same productions and explicit supplied context. Standalone source is parsed as document data, never passed to Lambda's compiler or an eval facility (**S1.8**).

The parser needs balanced delimiters and a token stream, not regex replacement or `split(",")`: option values, coordinates, expression arguments, and labels can all contain commas. Keep option order, duplicate keys, local declarations, semicolon boundaries, and the distinction between a dimension and a dimensionless coordinate.

Suggested syntax categories are `picture`, `scope`, `path`, `node`, `coordinate`, `axis`, `plot`, `legend_entry`, `key_list`, `expression`, and `unsupported`. These are ordinary Mark elements/maps; they need no new runtime TypeId. Unsupported nodes retain source and location even when the renderer refuses them.

For LaTeX output, teach the formatter to write preserved graphics islands verbatim when unchanged. Its current unknown-element fallback is insufficient for round-tripping an environment. Source retention supports editing and diagnostics; it does not promise byte-identical formatting of an entire reconstructed document.

## 6. Drawing and plotting semantics

### 6.1 TikZ geometry

Normalize supported syntax into an ordered scene containing groups, paths, clips, label references, and styles. Keep physical lengths distinct from model coordinates until geometry is resolved.

- Convert physical units consistently: `1in = 72.27pt = 72bp = 96 CSS px`; `cm`/`mm` derive from inches. `em`/`ex` require the explicit label/font context. Never equate TeX `pt` with CSS `px`.
- Preserve the TikZ coordinate basis, including its default centimetre vectors, separately from physical stroke widths and font sizes. [TikZ coordinates](https://tikz.dev/tikz-coordinates).
- Compose transforms in source order. Coordinate scaling does not automatically scale line widths, dashes, or node text. Support `transform shape` only when its distinct behavior is implemented. [TikZ transformations](https://tikz.dev/tikz-transformations).
- Keep one consistent internal coordinate orientation and apply the SVG y-axis conversion at a defined boundary. Do not mirror text or invert arrow orientation accidentally.
- Carry geometric bounds, painted bounds, clip state, and baseline separately. Strokes, Bézier extrema, arrow tips, and labels contribute to appropriate extents; mathematical coordinate limits are not painted bounds.
- Resolve node anchors from measured boxes. A missing named coordinate is a diagnostic, not `(0,0)`.
- Preserve draw order and scoped clips. SVG IDs for clips/markers must include a deterministic caller-supplied picture prefix so repeated pictures cannot reference each other's definitions.

TikZ's baseline option controls alignment with surrounding text; the default picture placement and an explicit `baseline=0pt` are not interchangeable. Return baseline/depth metadata for the LaTeX wrapper and verify both cases. [TikZ picture scope and baseline](https://tikz.dev/tikz-scopes).

### 6.2 PGFPlots axes

Resolve an axis in passes:

1. Parse and validate all series; resolve literal tables and supported expression ASTs.
2. Sample function series and preserve explicit discontinuity markers.
3. Derive shared domains from all eligible series, then apply explicit limits and the selected profile's enlargement rules.
4. Compute ticks and label requests, measure them, and resolve plot rectangle, margins, and legends.
5. Transform data, clip plots, and lower marks/annotations into the same scene as TikZ paths.

Upstream postpones axis drawing until it has collected the plots. Lambda should similarly avoid finalizing scales when it sees the first series. Preserve the difference between bare `\addplot`, `\addplot[options]`, and `\addplot+[options]`: cycle styling is not applied identically. [PGFPlots plot collection and options](https://tikz.dev/pgfplots/reference-addplot).

The PGFPlots adapter owns auto-limit, tick, log-base, and size semantics. Chart helpers may supply arithmetic, but their defaults must not leak into this profile. In particular, verify `width`/`height` with and without `scale only axis`, reversed axes, `axis equal image`, and explicit tick labels before admitting those keys. Reject still-unimplemented variants rather than partially applying them. [PGFPlots scaling options](https://tikz.dev/pgfplots/reference-scaling).

Numeric handling must be explicit:

- The expression grammar admits literals, variables, parentheses, arithmetic, and a finite function table such as `sin`, `cos`, `exp`, `ln`, `sqrt`, and `abs`. Function names are identifiers in this grammar, never host calls selected from arbitrary text.
- Trigonometric functions default to degrees; implement conversions such as `deg(x)` and `rad(x)` consistently. Do not inherit Lambda's trigonometric units by accident. [PGF mathematical expressions](https://tikz.dev/math-parsing).
- Convert admitted samples to floating point deliberately (**S1.3**), with diagnostics for values that cannot be represented under the profile. Preserve source data separately from transformed coordinates.
- Non-finite samples and invalid log-domain values cannot enter SVG coordinates. Implement and test the selected profile's discard/jump policy, reporting affected counts; an axis with no usable data is an error.
- A line must not bridge an explicitly marked discontinuity. Finite samples alone cannot prove continuity: start with deterministic sampling and documented limits; any later adaptive refinement must be bounded and tested at poles and steep but continuous curves.
- Empty or single-value domains, overflow, extremely large offsets, and tiny spans need explicit tests. Do not let division by zero produce an apparently valid blank picture.

## 7. Labels, measurement, and output

### 7.1 HTML/Radiant is the first complete target

Mathematical labels are essential to the first plotting release. Do not reduce `$x^2$` to plain text or estimate its size from character count.

Use the existing math renderer and expose a small shared box-returning entry point, factoring the box construction already used by `math.render_math`. A label carries its element tree, advance width, ascent, descent, font/style identity, and baseline. Text labels use the existing font/layout substrate; native font measurement already exists in [`lib/font/font.h`](../lib/font/font.h), but a package-level service must be designed and verified rather than assumed to exist.

Separate preparation from final layout:

```text
prepare(source, context) → drawing plan + label requests
host/package label service → measured boxes
layout(plan, measured boxes) → final picture scene
```

Measurements are explicit inputs to pure geometry. Font loading and document-dependent measurements belong to the host. In Radiant, reuse the measured-child/custom-layout pattern demonstrated by the graph package. A headless caller must supply a supported font/metric context; missing measurements should not silently become approximate results.

For HTML, compose SVG geometry with positioned HTML math/text labels in a picture wrapper. Preserve scene ordering by using ordered geometry/label layers where needed, and use the same positions and baseline in Radiant. A single always-on-top label overlay cannot represent arbitrary TikZ draw order. Restrict initially unsupported combinations of transformed or clipped HTML labels explicitly.

Do not make `foreignObject` a universal backend assumption: HTML parsing recognizes the tag, but that alone does not establish support across Radiant, SVG export, and PDF conversion. The primary mixed HTML/SVG path should work without that dependency.

### 7.2 Portable SVG and document export

Distinguish output contracts:

| Output | Proposed contract |
|---|---|
| HTML element tree | SVG geometry plus measured HTML/math labels and required stylesheet dependencies; primary first-release target. |
| Radiant document PDF/PNG | Render that composed document using the existing document path; verify label placement, clipping, fonts, and vector behavior on actual artifacts. |
| Standalone SVG | Self-contained geometry and admitted text labels. General mathematical labels require a shared math-to-vector/glyph-outline capability or an explicitly declared restricted profile. |
| LaTeX source | Preserve imported graphics source; generation from structured plot data can be a later feature with its own round-trip contract. |

Do not advertise arbitrary-math SVG export merely because HTML plots render correctly. `to_svg` should return a clear error when the scene needs unavailable portable label assets. Math-to-vector work belongs with the math/font infrastructure so chart, picture, and TikZ can share it.

Return an accessible title/description and, for plots, optional source data or a table alternative. Escape all label text and attributes through the existing serializer; raw TikZ strings never become HTML markup.

## 8. Proposed API and LaTeX experience

All names below are proposed, not current exports.

| API | Result and purpose |
|---|---|
| `tikz.parse(source, options)` | Source Mark plus syntax diagnostics; raises on unrecoverable malformed input. |
| `tikz.prepare(ast, context, options)` | Normalized drawing plan, label requests, diagnostics, and selected profile. |
| `tikz.layout(plan, measurements, options)` | Final scene with bounds, baseline, and resolved labels. |
| `tikz.render(source, context, options)` | Convenience composition for a context providing the required pure label services; returns HTML elements and metadata. |
| `tikz.render_ast(ast, context, options)` | Same rendering contract for a preserved LaTeX graphics island. |
| `tikz.to_svg(scene, options)` | Portable SVG element tree or an explicit unsupported-label error. |
| `pgfplots.render(spec, context, options)` | Structured Lambda data lowered into the same axis model used by parsed PGFPlots. |

Strict high-level functions use declared raised-error returns for malformed or unsupported rendering requests (**S7.4.2**), with stable error codes and source details (**S7.4.4**). Successful results contain ordinary diagnostic data for warnings. Preview recovery belongs to the document adapter, which catches the error and renders a visible placeholder. An empty SVG is not an error protocol.

Example future use, assuming a supplied rendering context:

```lambda no-run
// no-run: proposed package exports and a caller-provided label context.
import tikz: lambda.doc.tikz.tikz

pub fn plot(source, context) any^ {
    let result = tikz.render(source, context, {
        profile: "pgfplots-1.18-subset",
        id_prefix: "figure-1"
    })^
    result.element
}
```

The rendering context contains immutable styles/macros admitted by the profile, font metrics or pure measurement callbacks, already-resolved resources, and document identity. Pure functions cannot call procedural callbacks (**S12.1.1v2**). APIs that acquire files or invoke tools must be separately colored `pn`; no hidden subprocess fallback belongs inside `render`.

The corresponding LaTeX input should work without Lambda-specific syntax:

```tex
\documentclass{article}
\usepackage{pgfplots}
\pgfplotsset{compat=1.18}
\begin{document}
\begin{figure}
  \centering
  \begin{tikzpicture}
    \begin{axis}[
      width=8cm, height=5cm,
      xlabel={$x$}, ylabel={$f(x)$},
      grid=major, domain=-2:2, samples=81]
      \addplot[blue] {x^2};
      \addlegendentry{$x^2$}
      \addplot+[only marks] coordinates {(-1,1) (0,0) (1,1)};
      \addlegendentry{Samples}
    \end{axis}
  \end{tikzpicture}
  \caption{A function and measured samples.}
  \label{fig:quadratic}
\end{figure}
\end{document}
```

The LaTeX package continues to own figure numbering, caption rendering, and cross-references. Its bridge passes source and context to TikZ and embeds the returned picture. `\usepackage{pgfplots}` enables known package semantics; it does not load arbitrary `.sty` files. The bridge must pass graphics options through explicitly because the current renderer's analysis context does not automatically carry new options.

## 9. Resources, limits, and caching

Inline coordinates/tables require no IO. For later `table {data.csv}` support, the host resolves names relative to the containing document and supplies bytes or parsed tables under logical resource IDs. Parsing and layout never fetch paths or URLs. This follows the source/artifact separation in **D7.1.2v2**. Missing data is a reported error.

Apply configurable limits to source size, token count, nesting, expression depth, plots, samples, path segments, labels, and any future loop expansion. Check budgets before materializing large arrays. Suggested initial guardrails should be benchmarked rather than treated as upstream compatibility limits; exhaustion returns a structured diagnostic with the relevant limit.

Keep package bindings immutable and rendering state per call/context (**D7.2.1**). Cache keys include source content, inherited styles/macros, resource content digests, compatibility profile, package version, font/metric identity, and sizing options. A path or source hash alone is insufficient. Cache intermediate plans separately from measured layouts; apply document-specific SVG ID prefixes at embedding time.

Native Mark ASTs remain Input-owned (**D4.1.3**); package values use the existing runtime ownership model. New allocating host bridges must use precise rooting under **D1.5v2**, with no borrowed source pointer outliving its owner and no conservative stack scan. Cache lifetime must be explicit before enabling reuse across documents.

## 10. Alternatives and why this boundary is useful

| Approach | Advantage | Cost / limitation | Recommendation |
|---|---|---|---|
| Native bounded parser + Lambda renderer | Integrates with existing packages, supports live document rendering, no TeX dependency | Compatibility must be earned feature by feature; text measurement is substantial work | Primary implementation. |
| External TeX + a vector converter | Uses upstream package semantics and ecosystem | Toolchain deployment, process latency, fonts, resource access, and process isolation; engine-specific limitations remain | Offline reference generation initially; optional explicit procedural backend later. |
| Embed a complete TeX engine | Could execute PGF macros inside the product | A new language runtime and packaging/lifecycle project, much larger than a plotting package | Defer. |
| Translate source straight into chart specs | Quick for a few simple plots | Loses TikZ paths, scoped semantics, label positioning, and PGFPlots defaults | Reuse chart primitives instead. |
| LaTeXML-style PGF system driver | Reuses upstream macros after TeX digestion | A driver alone is insufficient without the macro-expansion machinery assumed by the older design | Revisit only with a separate TeX execution proposal. |

If a native extension eventually becomes necessary, use the existing Jube contracts (**D7.3–D7.5**) rather than linking a new TeX runtime into core as an incidental plotting dependency. Do not patch vendor code in place.

Upstream license metadata must accompany any redistributed sources or reference assets: CTAN lists multiple licenses for PGF and GPLv3-or-later for PGFPlots. This proposal does not recommend copying their TeX implementations into Lambda. Record exact file provenance and applicable notices before redistribution; package-level metadata alone does not resolve every file's terms. [PGF metadata](https://ctan.org/pkg/pgf), [PGFPlots metadata](https://ctan.org/pkg/pgfplots).

## 11. Validation and delivery gates

Prefer semantic assertions before screenshots: parsed coordinates, option order, resolved domains, ticks, transformed paths, clips, baseline, label boxes, and diagnostics. Visual comparison then catches font, alignment, stroke, and clipping defects that scene checks miss.

| Gate | Required evidence |
|---|---|
| Parser fidelity | Exact source slices; nested braces/options/scopes; comments containing delimiters; escaped punctuation; malformed semicolons; unsupported nodes retained; formatter round trip. |
| TikZ geometry | Unit conversions, transform order, unscaled strokes, Bézier bounds, named anchors, clipping, arrow extents, explicit/default baseline, unique IDs across repeated pictures. |
| Plot semantics | Multi-series auto domains, `addplot`/`addplot+`, degree-based functions, explicit ticks, log-domain failures, gaps, singleton/empty input, large-offset data, deterministic sampling. |
| Labels | Fractions, powers, Greek letters, mixed text/math, rotated labels where admitted, descenders, font changes, and consistency between measurement and painting. |
| Document integration | Preamble and local styles, two pictures with different scopes, figure captions/references, standalone HTML, live Radiant view, PDF/PNG artifact inspection. |
| Failure behavior | Unknown keys/libraries, unavailable data/fonts, excessive input, prohibited executable constructs, and visible preview placeholders without partial plots. |
| Regression | Existing LaTeX `picture`, math, and chart fixtures; baseline gates appropriate to touched subsystems. |
| Packaging/performance | Imports from a packaged release outside the repository CWD; cold/warm latency, peak memory, data-size scaling, source LOC and release-binary delta. |

Use pinned upstream PGF/PGFPlots plus a pinned TeX engine and converter to generate reference artifacts from small project-owned fixtures. Record engine/package/font versions, command arguments, input hashes, and success/failure status. Compare vector geometry and measured label positions with declared tolerances; use raster diffs as supplementary evidence, not a demand for identical antialiasing. Keep reference generation optional for ordinary end-user builds and cached fixtures available to CI.

Every existing TikZ fixture must be classified as supported, intentionally unsupported, or malformed under the profile. Do not silently exclude difficult fixtures or treat the existing XML companions as proof of native support. Add small PGFPlots fixtures for each admitted feature; large showcase files are a separate integration tier.

When implementation begins, every new Lambda `*.ls` test must have its corresponding `*.txt` expectation. Run `make test-lambda-baseline` for engine/package integration and `make test262-baseline` for runtime closeout; run `make test-radiant-baseline` when the rendering/measurement path changes. New allocating native bridges additionally need forced-GC coverage with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Performance claims require `make release`, paired measurements, and archived results, never debug timings.

### Recommended milestones

1. **Prove the seams.** Preserve a graphics island, parse one path and one axis, render one plain label and one math label, and verify dimensions/baseline in HTML and Radiant. Confirm packaged imports. This spike resolves parser and measurement risks before widening syntax.
2. **Ship 2D plotting.** Complete the initial profile in §4, including unsupported-feature handling, scoped styles, line/scatter plots, function/inline table input, log axes, legends, and labels. Ship only after semantic, visual, and applicable baseline gates pass.
3. **Expand from corpus demand.** Add bar/area/error plots, supplied external tables, bounded loops, and selected coordinate/library features in independently tested slices.
4. **Improve portable export.** Add shared math/vector label output and broaden standalone SVG coverage. Evaluate 3D and an external compatibility backend as separate proposals with measured demand.

These are proposed delivery boundaries, not an implementation progress ledger or a schedule commitment. A detailed implementation plan belongs in `vibe/impl/` after the design is accepted.

## Appendix A. Likely implementation touchpoints

Keep implementation details out of the design body beyond the boundaries needed to assess feasibility. A starting file layout is:

```text
lambda/input/input-tikz.cpp              proposed syntax parser + Mark adapter
lambda/input/input-latex-c.cpp           preserve graphics islands/declarations
lambda/input/input-latex-scanner.*       shared balanced scanning improvements
lambda/input/input.cpp                  register explicit tikz input format
lambda/format/format-latex.cpp           preserved graphics-source emission

lambda/package/doc/tikz/
    tikz.ls                             public API
    pgfplots.ls                         plotting API and axis normalization
    options.ls                          scoped keys/styles and capabilities
    expression.ls                       bounded numeric AST evaluation
    path.ls                             TikZ coordinate/path semantics
    axis.ls                             PGFPlots-specific domains/ticks/layout
    scene.ls                            common drawing scene and bounds
    labels.ls                           shared math/text box adapter
    render.ls                           SVG + HTML composition

lambda/package/latex/tikz_bridge.ls      proposed document adapter
lambda/package/latex/{analyze,render}.ls graphics context and dispatch
lambda/package/math/math.ls             factor a reusable measured-box entry
test/lambda/tikz/                       proposed tests + expected .txt files
test/latex/fixtures/pgfplots/            proposed reference inputs
```

This layout is illustrative; reuse or promote existing helpers before adding a new file. Shared graphics extraction should be narrow and evidence-driven, with chart/picture callers and tests migrated together. No package-specific math renderer, font loader, SVG painter, or TeX executor should grow inside `tikz`.

Native additions follow the repository's C+ and C++17 conventions and use `MarkBuilder`, `Str`/`StrBuf`, and existing library collections. Edit `build_lambda_config.json` for any native build registration, then regenerate through `make`; do not hand-edit generated Lua or parser files. All temporary sources, references, logs, and benchmark artifacts go under `./temp/`. Changes to Radiant dimensions use floats and pass the required layout lint rule.

## Appendix B. Decisions to confirm before implementation

- Accept the native 2D subset as the first deliverable, with unsupported pictures visibly diagnosed rather than automatically invoking TeX.
- Accept HTML/Radiant math labels first and a separately gated portable-SVG math capability.
- Ratify the initial supported keys/defaults and reference toolchain against a small fixture corpus; do not advertise generic TikZ/PGFPlots compatibility before that contract exists.
- Decide whether external TeX execution deserves a later package/backend proposal. It is not required to begin the native plotting work.

The recommendation is to proceed with the first milestone: **one complete LaTeX plot with correct math labels, geometry, and diagnostics**, then broaden the supported language through verified examples.
