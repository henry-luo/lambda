# Lambda Math Package — Phase 5: MathLive Box Model Migration

> Continues `Lambda_Pkg_Math.md` → `Math2` → `Math3` → `Math4`.
> Math4 proved the structural thesis: replacing per-case constants with
> metric-driven Rule 15/18/VBox geometry moved the corpus from 763/921 to
> ~825/921 while preserving the protected upstream baseline at 206/206.
> Math5 is the cleanup and convergence phase: migrate the current implementation
> to the MathLive box model itself, then use that model to retire the remaining
> hardcoded islands instead of adding more side-channel fields.

> Current status (2026-10-09): Phases 11–12 replace and remove the MathLive
> renderer. Phases 1–10 below record its historical migration.

## 1. Current State

As of the Math4 endpoint:

- protected upstream baseline: **206/206, 0 regressions**;
- full corpus: **825/921**;
- `atoms/fraction.ls`: Rule 15 metric path is live; `frac_bar_spec` is gone;
- `atoms/scripts.ls`: sup/sub/both are mostly Rule 18 metric-driven;
- radicals, matrix family, `array`, `cases`/`rcases`, `dcases`, vertical-bar
  stacked delimiters, and integral side-limits have metric-driven ports;
- `strut_total` and `strut_depth_em` are deleted.

But Phase A's original "single box field" target is not complete. The runtime
still carries parallel vertical fields:

```
height, depth
render_height, render_depth, render_total
height_raw, depth_raw
left_right_render_depth, left_right_render_total
```

The important lesson from Math4 is that these fields should not be deleted by
assertion. Some of the values they encode are real MathLive structures:

- a `VBox` can have one layout `height/depth`, while its children have explicit
  CSS `top`, `height`, and pstrut styles;
- `bbox`/`enclose` border overlays can need CSS dimensions that are not a
  simple `height + depth`;
- line accents and wide accents are visual stacks, not a scalar correction;
- delimiters size against content extents through MathLive delimiter routines,
  not through a separate Lambda-only `left_right_render_*` channel.

Math5 therefore changes the target from:

> Delete `render_*` and `*_raw` once enough producers expose raw values.

to:

> Rebuild the box tree the way MathLive does, so each public box has one
> full-precision `height/depth`, and any visual/overlay extent lives in nested
> boxes, CSS styles, or helper-specific geometry records rather than in the
> public box record.

## 2. Design Goal

Mirror MathLive's core model:

```ts
class Box {
  height: number;   // full precision, above baseline
  depth: number;    // full precision, below baseline
  width: number;
  italic: number;
  skew: number;
  maxFontSize: number;
  children: Box[];
  styles: Map;
}

class VBox extends Box {
  // computed by makeRows()/getVListChildrenAndDepth()
  height = maxPos;
  depth = -minPos;
}
```

Emission is where CSS values become strings. The only numeric stringification
rule for em values is MathLive's CEIL@2:

```
fmt_ml_em(v) = Math.ceil(v * 100) / 100 + "em"
```

The Lambda target box record becomes:

```lambda
{
    element,       // emitted node tree, still Lambda elements
    height,        // full precision layout height
    depth,         // full precision layout depth
    width,
    italic, skew,
    max_font_size,
    type
}
```

`height_raw` and `depth_raw` disappear because `height/depth` are already raw.
`render_height`, `render_depth`, and `render_total` disappear because visual
CSS extents are represented inside the element tree, as MathLive does. The
temporary migration marker has been removed now that all math package boxes use
the same one-box-field model.

## 3. Non-Goals

- Do not rewrite the parser, AST builder, or LaTeX normalization.
- Do not change MathLive snapshot goldens.
- Do not touch unrelated Lambda runtime or JS engine code.
- Do not hand-tune new per-case tables. If a case needs a constant, identify the
  MathLive source of that constant: font metric, TeX sigma, register, style
  scale, pstrut rule, delimiter recipe, or browser workaround.

## 4. Remaining Hardcoded Islands

The known Math5 targets are:

| Area | Current shape | Desired MathLive-shaped fix |
|------|---------------|-----------------------------|
| `box.ls` hbox | one-box-field path; no `render_*`, `*_raw`, or `left_right_render_*` producers/readers remain | keep the census guard at zero |
| `math.ls` emit | root struts emit directly from public `height/depth` | keep root emission as the only CEIL@2 strut stringification site |
| `render.ls` line accents | `line_accent_box(...)` plus overline/underline simple/tall templates deleted; line accents route through shared MathLive-style VList stacks | keep line-accent coverage green |
| `render.ls` wide accents | non-SVG wide accent vertical extents and centering derive from base clearance plus glyph metrics; SVG accents use stretchy accent boxes | extend metric coverage only when new accent forms are added |
| `atoms/enclose.ls` bbox | public box uses layout `height/depth`; no `render_total` producer remains | keep bbox and box-field census gates green |
| `atoms/array.ls` smallmatrix | `matrix_table_metrics` fallback deleted; dynamic row walk is the only non-equation path | close remaining matrix extended diffs against MathLive's row/cell operation order |
| `atoms/scripts.ls` large op text limits | `render_large_op_limits_vlist` deleted; text and symbol limits route through `make_limits_stack` | close remaining large-op/script metric drifts in mixed expressions |
| `atoms/delimiters.ls` arrows/groups | old level-3 fallback helper deleted; arrows/groups now route through `render_extensible_recipe_delim` | finish replacing the remaining recipe constants with a fuller MathLive extensible-symbol derivation when available |
| `render.ls` script radicals | `make_script_sqrt_spec` and `make_sqrt_spec` deleted; script/scriptscript radicals route through `sqrt_geom` plus an indexed scaled-coordinate projection | fold indexed script projection into shared VBox/radical derivation |
| fixtures | `fraction_branch_fixture.mjs` describes Rule 15 geometry and `script_fixture.mjs` covers Rule 18 script geometry | expand fixtures only when a newly migrated atom needs a guard |

## 5. Migration Strategy

The migration used a two-model bridge:

1. **Legacy boxes** keep today's fields and behavior.
2. **ML boxes** use only full-precision `height/depth` plus element/CSS
   structure.
3. Accessors hide the difference:

```lambda
fn box_h(b) { b.height }
fn box_d(b) { b.depth }
fn legacy_emit_h(b) { if (b.render_height != null) b.render_height else b.height }
fn legacy_emit_d(b) { if (b.render_depth != null) b.render_depth else b.depth }
```

During migration, parents could not accidentally treat a legacy rounded box as
an ML full-precision box. That bridge is now retired: all producers are
MathLive-shaped boxes, the legacy accessors and fields are gone, and the probe
asserts the absence of side-channel fields directly.

## 6. Phased Plan

### Phase 0 — Gates and Instrumentation

Goal: make the remaining work measurable before changing behavior.

Work:

- add a box-field census script/report for `render_*`, `height_raw/depth_raw`,
  and `left_right_render_*` producers and readers;
- done: update `fraction_branch_fixture.mjs` so it no longer claims to test
  `frac_bar_spec`; it should assert Rule 15 intermediate shifts and final HTML;
- done: add `script_fixture.mjs` for Rule 18: sup-only, sub-only, both,
  descenders, nested scripts, fraction children, and inline big-op limits;
- add `diff_harness --cluster-by atom` or an equivalent report postprocessor;
- done: update the "box model" probe fixture to dump root box metadata and
  assert the final one-box-field invariant directly.

Gate:

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --strict
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --no-baseline
```

Expected: strict still fails due to extended corpus, but baseline regressions
must stay at 0. `--no-baseline` should remain at or above the current corpus
count before each migration phase is considered complete.

### Phase 1 — Core Box/VBox Helpers

Goal: give Lambda a reusable MathLive-shaped primitive instead of reimplementing
`makeVList` formulas in each atom.

Work:

- add `box.ml_box(...)` constructor:
  - sets `height/depth` full precision;
  - sets `max_font_size`;
  - does not set `height_raw`, `depth_raw`, or `render_*`;
- add `box.set_style_em(...)` / `util.fmt_ml_em(...)` helpers matching
  MathLive CEIL@2;
- add a `vbox.ls` helper or a `box.vbox_*` group that ports MathLive:
  - `getVListChildrenAndDepth`;
  - `makeRows`;
  - modes: `individualShift`, `top`, `bottom`, `shift`, `firstBaseline`;
  - pstrut = `max(child.maxFontSize, child.height) + 2`;
  - child wrapper `top = -pstrut - currPos - child.depth`;
- make existing fraction/script/array code call the helper only in a shadow
  assertion path first, comparing computed `height/depth/top` to current values.

Gate:

- no HTML output change;
- add probes for `\frac{a}{b}`, `x^2`, `x_2`, `x_2^3`,
  `\sqrt{x}`, `\begin{pmatrix}a&b\\c&d\end{pmatrix}`.

### Phase 2 — Convert Already-Metric Producers to ML Boxes

Goal: remove side-channel fields from constructs whose geometry is already
metric-driven.

Order:

1. `atoms/fraction.ls` bar fractions and no-bar fractions;
2. `atoms/scripts.ls` `render_sup_only`, `render_sub_only`, `render_both`;
3. `render.ls` display/text radicals;
4. `atoms/array.ls` matrix family, `array`, `cases`, `rcases`, `dcases`;
5. `render_integral_inline_scripts`;
6. vertical-bar stacked delimiters.

For each producer:

- build the element via the shared VBox helper;
- return `height/depth` as full-precision `maxPos/-minPos`;
- remove local `height_raw/depth_raw` and redundant `render_height/render_depth`;
- keep a temporary legacy adapter only when a non-converted parent still needs
  it; mark it with a comment naming the parent.

Gate:

- the named upstream categories stay green:
  `FRACTIONS`, `SUPERSCRIPT/SUBSCRIPT`, `SURDS`, `ENVIRONMENTS`,
  `LEFT/RIGHT`, `RULE AND DIMENSIONS`;
- baseline remains 206/206;
- field census shows fewer producers after each substep.

### Phase 3 — Collapse Root Strut Emission

Goal: make `math.ls` match MathLive root strut emission.

Work:

- done: simplify `math.ls` to one MathLive-style strut path:

```lambda
let h = box.height
let d = box.depth
strut.height = fmt_ml_em(h)
bottom.height = fmt_ml_em(h + d)
bottom.vertical_align = fmt_ml_em(0.0 - d)
```

- done: delete the legacy `render_*`/`*_raw` root branch.

Gate:

- no baseline regression;
- root ML coverage should rise as Phase 2 proceeds;
- when a category is 100% ML-rooted, add it as a strict subgate.

### Phase 4 — Delimiter Sizing Without `left_right_render_*`

Goal: replace Lambda's delimiter side-channel with MathLive delimiter routines.

Work:

- introduce a delimiter extent function:

```lambda
fn delimiter_target_extent(content, context) {
    let axis = met.AXIS_HEIGHT * context.scale
    let max_dist = max(content.height - axis, content.depth + axis)
    let target = max_dist / 500.0 * 901.0 * 2.0
    ...
}
```

following MathLive `makeLeftRightDelim`;

- make `\left...\right` ask the content box for its public `height/depth`;
- route sized parentheses/brackets/braces through shared delimiter helpers;
- route array stacked brackets/braces through `make_stacked_delim`;
- route arrows/groups through the named extensible-delimiter recipe path, then
  replace the remaining recipe constants with derived MathLive inputs;
- delete `left_right_render_depth` and `left_right_render_total` once all readers
  are gone.

Gate:

- `LEFT/RIGHT` stays 55/55;
- delimiter sizing commands stay green;
- matrix and cases environments stay green;
- explicit probes for Size1-Size4 parentheses, brackets, braces, vertical bars,
  and arrows.

### Phase 5 — Enclose/BBox as Box Trees

Goal: eliminate `render_total` where it encodes overlay/border CSS extent.

Work:

- port MathLive `BoxAtom`/`EncloseAtom` structure for `\boxed`, `\fbox`,
  `\bbox`, `\rule`, and related commands;
- represent overlay/border as a child element with explicit CSS, not as parent
  `render_total`;
- public box `height/depth` describes layout; visual border height lives in the
  overlay child style;
- done: remove `render_total` from `atoms/enclose.ls`.

Gate:

- `BOX` stays 9/9;
- `RULE AND DIMENSIONS` stays 15/15;
- bbox cases in the extended corpus do not regress.

### Phase 6 — Accents and Over/Under Stacks

Goal: retire `line_accent_box(...)` hardcoded extents and wide-accent constants.

Work:

- port MathLive `AccentAtom` and `OverunderAtom` logic:
  - base clearance = `min(base.height, X_HEIGHT)`;
  - accent body height from glyph/SVG metrics;
  - stack via shared VBox;
  - wide accents use the correct stretchy/SVG accent body instead of scalar
    `accent_body_height_raw` constants;
- convert `\hat`, `\tilde`, `\bar`, `\dot`, `\ddot`, `\vec`,
  `\widehat`, `\widetilde`, `\overline`, `\underline`, over/under braces where
  applicable;
- done: route `\overline` through one shared MathLive `VBox({shift:0})`
  construction for simple, wide, and tall bases;
- done: route `\underline` through the mirrored VList construction
  `[line, 3*rule gap, base]`, deleting the simple/tall templates.
- done: collapse rounded accent body heights into `ceil2(precise glyph height)`
  and derive wide-base vertical extents from base clearance.
- done: replace the wide non-SVG centering shim with a no-spacing visual-width
  walk over the accent body's plain glyph text.

Gate:

- `ACCENTS` stays 10/10;
- `OVER/UNDERLINE` stays 2/2;
- extended failures for `\hat{x}\widehat{xy}` and
  `\tilde{y}\widetilde{abc}` close or move to a known non-accent cause.

### Phase 7 — Smallmatrix and Script-Style Arrays

Goal: remove the last array table fallback.

Work:

- trace MathLive's `ArrayAtom` path for `smallmatrix`:
  - cell style and scaling factor;
  - `arraystretch`;
  - `arraycolsep`;
  - row arstrut;
  - exact `makeRows` accumulation order;
- port smallmatrix to `compute_dyn_metrics` or a dedicated MathLive-compatible
  `compute_smallmatrix_metrics`;
- done: delete `matrix_table_metrics`; dynamic row walking is now the only
  non-equation table-metric path.

Gate:

- `smallmatrix` extended failures close;
- matrix family remains green;
- no regression in delimited matrices.

### Phase 8 — Large Operators and Text Limits

Goal: remove legacy limit vlist code and finish Rule 18/limit migration.

Work:

- route text operators (`\lim`, `\max`, `\det`, etc.) through
  `make_limits_stack`;
- done: remove `render_large_op_limits_vlist`; symbol and text paths both use
  the shared helper;
- verify integral side-limits still use side-script Rule 18 rather than
  centered limit stacks;
- done: remove related `render_height/render_depth/render_total` fields in
  scripts.

Gate:

- `\lim_{h \to 0} ...` extended failures close;
- big-operator baseline stays green;
- no regression in fractions containing `\lim`.

### Phase 9 — Script Radicals and Remaining Fallbacks

Goal: remove the remaining radical spec fallbacks.

Work:

- done: extend `sqrt_geom` to unindexed script/scriptscript radicals;
- done: replace the indexed script radical `make_sqrt_spec` special case with a
  scaled-coordinate projection derived from the small surd glyph, math axis,
  and script rule thickness;
- preserve MathLive's `Box.setTop` threshold behavior;
- ensure radical index stack uses shared VBox and full-precision dimensions;
- done: delete `make_script_sqrt_spec` and legacy `make_sqrt_spec` paths.

Gate:

- `SURDS` stays 5/5;
- nested radical/script/fraction cases do not regress;
- field census shows no radical `render_total` fallback except legitimate SVG
  child styles.

### Phase 10 — Delete Legacy Fields

Goal: finish Phase A for real.

Prerequisites:

- no producer intentionally sets:
  - `render_height`;
  - `render_depth`;
  - `render_total`;
  - `height_raw`;
  - `depth_raw`;
  - `left_right_render_depth`;
  - `left_right_render_total`;
- no reader depends on them;
- root ML coverage is 100% for the MathLive corpus.

Work:

- done: delete legacy accessors from `box.ls`, `math.ls`, `optimize.ls`, and
  atom forwarders;
- done: remove the temporary migration marker after the one-box model became
  universal;
- done: simplify `math.ls` to a single strut emission path;
- done: update docs and fixtures to describe the final model.

Gate:

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --strict
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --no-baseline
make test-lambda-baseline
```

Acceptance:

- protected baseline: 206/206, 0 regressions;
- full corpus: strictly greater than Math4's 825/921, target ≥ 900/921 before
  considering the migration complete;
- field census for legacy vertical fields returns zero.

### Phase 11 — Metrics From the Rendered Font (2026-10-08)

Goal: remove the MathLive/KaTeX font dependency from production math geometry.
Before this phase, CSS could select CMU/Latin Modern while `metrics_data.mark`
described fixed MathLive faces, and `text_box` estimated width as `0.8em` per
character. Moving those tables from Lambda source to Mark did not fix this
font/measurement mismatch.

Work, in dependency order:

1. Expose batched `radiant.measure_text` and `radiant.font_metrics` over the
   existing font and SVG text machinery. Accept text/style records directly;
   return advance, separate ink/logical bounds, baseline and font identity in
   CSS pixels. Share resolution, fallback and placement with painting; keep
   full precision and cache within the font-resource lifetime. Chart labels
   migrate from constructing HTML measurement requests to this API.
2. Read OpenType `MATH` in `lib/font`: constants, italic corrections, accent
   attachments, math kerns, glyph variants and assemblies. Expose copied math
   facts through Radiant. Reuse the existing CMU/KaTeX faces in production;
   none of these 20 faces has this table. MATH is optional: absent tables use
   normal glyph metrics and shared layout defaults. Noto Sans Math supplies
   test-only coverage for the enhanced path.
3. Carry the resolved font context through math boxes and emission. Replace
   character-count widths, per-character heuristics, Computer Modern sigma
   constants and KaTeX Size1–4 recipes with the selected font's data. Keep
   layout algorithms in Lambda and font parsing in `lib/font`. Emit the same
   font resources/glyphs that were measured; retire `metrics_data` after its
   consumers migrate.
4. Gate on measured/rendered agreement for two math fonts, size/style changes,
   fallback, text runs, nested scripts/fractions, accents and stretched
   delimiters. Retain MathLive snapshots as explicit compatibility fixtures;
   their font-specific HTML is not the oracle for other fonts. Run the Lambda
   and Radiant baseline gates and the Radiant float-dimension lint.

Boundaries: **D7.1.1** owns layering; **D7.1.2v2** keeps resource acquisition
in `lambda-io`; **D7.4.6 / S12.1.1v2** govern read-only host queries against a
stable font context; **D4.2.2v2** requires copied results rather than borrowed
font/document pointers. Runtime-only hosts report unavailable Radiant services
under **D7.1.6**. The OpenType [MATH specification](https://learn.microsoft.com/en-us/typography/opentype/spec/math)
defines font data; it does not replace the math layout algorithms.

Implemented: the production `math.ls` entry point now uses `font.ls`,
`typeset.ls`, `stretch.ls` and `svg_box.ls`. Radiant exposes `measure_text`,
`font_metrics` and `math_metrics`; `lib/font/font_math.c` reads MATH data.
The default is bundled CMU Serif, with existing CMU style faces and small
KaTeX symbol/alphabet faces. Noto Sans Math remains a MATH-table test fixture
under `test/lambda/math/fonts`. Explicit binary font snapshots keep resource
acquisition in Lambda IO.
SVG carries the measured glyph outlines, so downstream font substitution cannot
change geometry. Chart measurement uses direct text records. Phase 12 removes
the transitional `mathlive.ls` adapter and its fixed `metrics_data` tables.

Validation (2026-10-09): all 78 selected math/LaTeX/metrics/editor integration
checks, 15 native font tests, and four editor math UI fixtures pass. The
STIX-free default produces finite geometry for all 921 corpus formulas;
this is a smoke check, not a visual equivalence assertion. A rendered sample
covers fractions, nested scripts, radicals, operators, matrices and distinct
specialist alphabets. Tests compare dimensions and painted paths to native
facts, including default CMU italics and bundled KaTeX operator fallback.
The broader Lambda gate still has `input_model_formats` and `scene3d_assets`
failures (`E407` for OBJ/scene3d-asset parsing). Earlier Radiant validation
passed 43 scene and 257 native view tests; its button-image baseline mismatch
and two additional editor footnote/view-only-count failures remain outside
this font change. Full baseline acceptance is not claimed.

Limits: direct text measurement shares the current SVG placement/shaping
capabilities; it adds no shaping engine. MATH accents with a finite variant set
stop at the largest supplied variant. Ordinary faces without constructions
use geometric outline stretching, which can change stroke weights.
Font caches are query-local, and math
outlines trade selectable text for deterministic measured/painted geometry.
SVG titles retain the source expression. The Phase 1–10 acceptance criteria
below describe the historical MathLive adapter; Phases 11–12 use the font and
SVG gates instead.

Font audit and revision (2026-10-08, user): **MATH is an optional enhancement.**
The 8 existing KaTeX WOFF2 faces and 12 CMU WOFF2 faces have no MATH table;
their glyph advances, bounds and outlines remain usable. `math_metrics` now
returns normal glyph facts and `has_math: false` / `constants: null` for these
faces. `fallback.ls` applies [MathML Core §5.1 defaults](https://w3c.github.io/mathml-core/#layout-constants-mathconstants)
using the selected font's x-height, post underline thickness and OS/2 script
offsets. The normal font API exposes those facts; Lambda retains layout policy
(**D7.1.1 / D7.1.2v2**). No metrics from a different font are copied onto a glyph.
Absent mathematical-alphabet glyphs use ordinary italic/bold faces. Missing
symbols use explicit codepoint fallback with the actual face's own metrics,
MATH data if present, and outline. Without stretch recipes, ordinary glyphs
are scaled together with their boxes; zero-advance combining accents use
ink bounds for stretching and attachment. Invalid font data and unresolvable
glyphs remain errors. Selecting an installed family bypasses bundled resource
loading. Tests cover two ordinary CMU families alongside Noto, script styles,
normal-font rule/axis derivation, glyph fallback, and measured/painted agreement.

Distribution revision (2026-10-08, user): removed the 1,517,976-byte STIX font
and its license from the package. `bundled.ls` selects existing CMU and KaTeX
resources; it contains no glyph dimensions or stretch recipes. Default
rendering exercises the no-MATH path, including ordinary italic/bold faces,
CMU sans/monospace, and the existing script/calligraphic/fraktur/AMS alphabets.
Missing symbols try the authored bundled symbol families before platform
fallback. Native MATH parser tests now use the test-only Noto fixture;
production rendering neither reads nor requires that fixture.

### Phase 12 — Remove the Legacy MathLive Renderer

Implemented (2026-10-09, user): remove `mathlive.ls`, the HTML box/context/atom
renderer, fixed metrics and their generator, class/font tables, MathLive-only
formatting, CSS, probes and low-level compatibility tests. The public entry
point remains `lambda.doc.math.math`, with font facts supplied by Radiant and
layout policy in Lambda (**D7.1.1 / D7.1.2v2**). MATH remains optional.

Document viewer and TikZ shells no longer load the retired math stylesheets;
LaTeX document text keeps its CMU stylesheet. AMS commands in prose now use the
same font-driven SVG renderer. Shared symbol mappings, spacing policy, syntax
helpers and the fonts used by the current renderer remain.

`make test-math-corpus` replaces the strict MathLive-markup target and remains
in the Lambda baseline lane. It runs the retained formula corpus through the
public renderer, rejecting errors, nonfinite dimensions/transforms, invalid
SVG view boxes and external-font/legacy markup dependencies. Historical
reference snapshots remain test data; matching their HTML is no longer a
contract. The font-layout, ordinary-font and integration goldens continue to
check measured/painted agreement and document behavior.

Validation: 921/921 formula cases pass the SVG corpus; all 69 selected package
checks and two editor/viewer UI fixtures pass. The larger LaTeX document corpus
initially exceeded the harness's 60-second limit under concurrent load, then
passed in isolation (55 seconds); its direct output also matches the golden.
The document viewer compiles, and the production import/legacy-symbol audit
finds no remaining dependencies.
The required full interpreter sweep reports 1,053 matches, 54 exclusions,
zero tier mismatches and three timeouts. Only retired fixture entries are
removed from the committed lists; unrelated reclassifications from the shared
checkout are not part of this change.

### Phase 13 — Keep Outlined Math Responsive While Scrolling

Implemented (2026-10-09): release profiling of `math_comprehensive.md` traced
most scrolling time to SVG hit testing: repeated CSS parsing, inherited font
contexts, ancestor transforms and unused stroke geometry. Scroll-position
updates also invalidated otherwise unchanged SVG style caches.

Radiant now retains the document's SVG cascade across scrolling, invalidating
it for native style writes, DOM changes, selector state, layout and resource
changes. SMIL base-value queries remain separate from completed animation
samples. A synchronous hit walk shares font/transform results, skips stroke
work when pointer-events only needs filled geometry, and rejects fill misses
using contour bounds. Plain SVG transforms no longer allocate a CSS parser
pool. Cached SVG layers apply their device transform once, including when a
vector-backed blit is required. Visible overflow and precise stroke/clip
targeting remain supported. This stays within the existing package/native
boundary (**D7.1.1 / D7.1.2v2**); math layout and font selection remain in the
Lambda package.

Release validation: 30 headless wheel events at 1000×800 improved from about
5.4 seconds to 21 milliseconds per event including repaint; after the first
two events, the average is 15 milliseconds. The new scrolling/style-mutation
fixture, SVG animation/interaction fixtures, and native vector/CSS suites
cover cache invalidation and geometry preservation. Validation passes 394 UI
baseline checks, 128 SVG fixtures, 96 native vector checks and 81 CSS checks.
The broader visual gate retains the unchanged `pp_btn_shapes_01` threshold
mismatch; its earlier and updated renderings are pixel-identical.

## 7. Risk Register

| ID | Severity | Risk | Mitigation |
|----|----------|------|------------|
| R1 | HIGH | Deleting `render_total` before its visual extent is represented in the element tree regresses bbox/accent cases. | Convert producer to MathLive box tree first; delete field only after reader census is empty. |
| R2 | HIGH | `height/depth` become a mix of rounded and full-precision values during migration. | Retired with Phase 10: the marker/accessor bridge is gone and all math boxes use full-precision `height/depth`. |
| R3 | HIGH | Delimiter sizing changes cascade into fractions, arrays, cases, and radicals. | Convert delimiters after clean producers; keep `LEFT/RIGHT` as a strict subgate. |
| R4 | MEDIUM | hbox raw max-height/max-depth cross-terms overestimate the actual MathLive root strut. | Port hbox/atom wrapping semantics instead of summing separate raw maxima blindly. |
| R5 | MEDIUM | smallmatrix and wide accents hide MathLive-specific SVG/browser quirks. | Probe MathLive SSR and source side by side; reproduce operation order exactly. |
| R6 | MEDIUM | Fixture drift: old branch fixtures test deleted implementation details. | Update fixtures to assert semantic geometry and emitted HTML, not branch names. |

## 8. Acceptance Criteria

1. The public box model has one full-precision `height` and one full-precision
   `depth`; legacy vertical side-channel fields are gone.
2. `math.ls` emits root struts from `height/depth` only, with CEIL@2 formatting.
3. `VBox`/`makeRows` is shared across fractions, scripts, arrays, radicals,
   accents, limits, and delimiters instead of reimplemented locally.
4. Remaining hardcoded tables listed in §4 are removed or replaced by named
   MathLive/TeX inputs.
5. `fraction_branch_fixture.mjs` and `script_fixture.mjs` pass and assert the
   new geometry.
6. Protected baseline remains 206/206 with zero regressions.
7. Full corpus improves beyond Math4's current ~825/921; target ≥ 900/921 for
   this phase.

## 9. Why This Is the Right Next Step

Math4 removed the largest constant tables and proved that the renderer improves
when it follows MathLive's algorithms. But the current implementation still pays
for the old architecture: each newly metric-driven atom has to decide how to
feed `render_*`, `height_raw`, `depth_raw`, and `left_right_render_*`.

Math5 removes that tax. The work is not a cosmetic refactor; it is the substrate
needed for the remaining hard cases. Smallmatrix, wide accents, bbox, arrows,
text limits, and script radicals are all places where the right answer is not
another scalar field. The right answer is the same one MathLive uses: build the
right box tree, keep the box's layout dimensions full precision, and let
emission stringify once.
