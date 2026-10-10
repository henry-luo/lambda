# Lambda Math Package — Phase 5: MathLive Box Model Migration

> **DESIGN RULE 1 — TeX conformance before visual similarity.**
> User-ratified 2026-10-10. Every math layout fix MUST be derived from TeX's
> math-list algorithms and the definition of the supported TeX/LaTeX command,
> using the actual selected font's metrics or its explicitly matched TeX
> companion. Never fit coordinates, gaps, glyph shifts, sizes, or curves to a
> screenshot. Numeric constants require a cited algorithm, macro, font table,
> or authored dimension. If the required data or construction is unavailable,
> report the limitation; do not invent a visually plausible replacement.
>
> Knuth's [TeX82 `tex.web`](https://tug.ctan.org/systems/knuth/dist/tex/tex.web)
> and *The TeXbook*, Appendix G, govern math-list conversion. Supported LaTeX
> extensions use their package definitions, for example
> [mathtools](https://ctan.org/pkg/mathtools) and
> [amsmath](https://ctan.org/pkg/amsmath). MathLive/KaTeX output and raster diffs
> are diagnostic evidence, never layout authority. A successful render or
> refreshed snapshot does not establish TeX conformance. Validate the rule with
> independent TeX box/metric evidence and painted geometry across math styles.
>
> This strengthens the existing Phase 14 package policy; it changes no formal
> language ruling. Layout stays in `lambda.doc.math` (**D7.2.4**), with IO and
> native font facts respecting **D7.1.1 / D7.1.2v2**. The historical MathLive
> migration below is superseded wherever it conflicts with this rule.

> **DESIGN RULE 2 — Do not add fonts to Lambda packages.**
> User-ratified 2026-10-10: implement math support using the existing bundled
> CMU and KaTeX faces. No new font files, font families, restored STIX resources,
> or additional font dependencies for screenshot parity. A fallback composition
> must have an explicit TeX macro definition and use measured existing glyphs
> under Rule 1; it must not fabricate a missing glyph or copy another font's
> metrics. Caller-supplied fonts remain optional. Missing constructions must
> be documented. This preserves the 2026-10-08 distribution decision below
> (**D7.2.4 / D7.1.2v2**).

> Continues `Lambda_Pkg_Math.md` → `Math2` → `Math3` → `Math4`.
> Math4 proved the structural thesis: replacing per-case constants with
> metric-driven Rule 15/18/VBox geometry moved the corpus from 763/921 to
> ~825/921 while preserving the protected upstream baseline at 206/206.
> Math5 is the cleanup and convergence phase: migrate the current implementation
> to the MathLive box model itself, then use that model to retire the remaining
> hardcoded islands instead of adding more side-channel fields.

> Current status (2026-10-10): Phases 11–12 replace and remove the MathLive
> renderer; Phase 14 establishes the TeX companion. The audit appendix records
> remaining nonconforming constructions. Phases 1–10 are historical.

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
- Do not hand-tune per-case tables. Identify the TeX algorithm, package macro,
  font metric, or authored dimension behind every constant. A MathLive value
  or browser workaround alone cannot justify a production layout rule.

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
   character-count widths, per-character heuristics and KaTeX Size1–4 recipes
   with the selected font's data. The later Phase 14 user revision adds an
   explicit, provenance-tracked CM math companion for bundled CMU only. Keep
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
SVG originally carried measured glyph outlines; the RAD07-L3 revision below
preserves native text painting with embedded fonts. Chart measurement uses direct text records. Phase 12 removes
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
Explicit font queries reuse one native snapshot per `EvalContext` (2026-10-09).
The capsule owns its font contexts and complete byte/style identity independently
of GC values, replaces the entry only after successful validation, and releases
it at context teardown (**D5.4.2–D5.4.4**). Identity includes resource order,
family, size, weight, slant and every font byte; a family name or GC address is
insufficient. The key is capped at 8 MiB; larger snapshots and queries without
explicit faces retain query-local contexts. `math_metrics`, `font_metrics` and
`measure_text` share this path, avoiding repeated WOFF2 decompression for every
formula while keeping resource acquisition in Lambda IO (**D7.1.2v2**).
Platform fallback handles are also cached inside their owning `FontContext`,
not a process-global table (**D5.4.3**). A handle retains pool-owned storage;
sharing it between concurrent query/view contexts made iframe teardown release
a handle after its original pool had gone. The native two-context regression
`PlatformFallbackHandlesStayWithTheirOwningContext` and the existing LaTeX
iframe navigation fixture cover this ownership boundary.
`test_font_snapshot_reuse.ls` covers equivalent snapshots, changed bytes/styles,
corrupt input, size changes, shared measurements and missing glyphs, including
forced collection with freed-memory poisoning. Before/after release viewer
artifacts for `test/input/math_intensive_test.tex` are retained under
`temp/math-load/`; viewport PNGs at four distinct scroll positions are
byte-identical.
Three interleaved fresh-process runs per release binary (original/final/final/
original/original/final, with
`LAMBDA_AUTO_CLOSE=1`) measured median process time **20.28 s → 5.11 s**
(3.97× faster, 75% lower). This includes startup, initial rendering and teardown;
it excludes user dwell time. Each process starts with an empty native query
cache; filesystem/platform caches are warm. No build or test from this task
ran concurrently; ambient machine activity is recorded with the timing artifacts.
These numbers are macOS measurements, not cross-platform gates.

Cache validation: the snapshot regression passes with every-allocation GC and
freed-memory poisoning; all 15 font unit tests, 108 vector tests and five focused
viewer regressions pass. Three final-release iframe navigation replays and the
four-position math render comparison finish with zero tracked live allocations.
The Test262 baseline passes all 40,261 cases with no retries. The final Lambda
baseline is 6,509/6,511: `edit_view_only` and the batched
`math_test_math_html_output` mismatch also reproduce with the original release;
the short math batch produces byte-identical HTML in both releases. The full
Radiant gate retains existing CSS-list and CSS-memory mismatches; its iframe
teardown failure was fixed and verified by the focused final-release runs.
An optional native sweep remains incomplete because four standalone runners
have unrelated geometry/boundary link failures. An extra every-allocation-GC
iframe replay is also not a passing gate: the original crashes, while the final
release misses the timed URL assertions. Logs and exact artifacts remain under
`temp/math-load/`.

**RAD07-L3** (2026-10-09) changes ordinary math
glyphs to SVG `<text>` with explicit resolved family, weight, style and size.
Used bundled/supplied faces accompany each SVG as embedded `@font-face` rules;
installed-font options require the same fonts in the viewer. Unencoded MATH
variants/assembly pieces retain paths because standard SVG text cannot address
a font-local glyph ID. Geometric stretching and rules remain unchanged.
The native metrics response copies the resolved style alongside geometry
(**D4.2.2v2**); resource acquisition remains in Lambda IO (**D7.1.2v2**).

The pixel audit found CMU italic paths have about 21–24% less ink than native
Chrome text at 16px. Radiant now rasterizes eligible SVG text at its final
visible font size using the normal glyph compositor, including uniform
viewBox/device scaling. Retained rendering owns the glyph pixels after
temporary font contexts die. Rotation, skew, nonuniform stretch, paint servers,
strokes and vector export retain geometry. The same-font 16px SVG text sample
changed from −20.15%/−23.24% ink versus Chrome to +4.53%/+2.25% at 1×/2×.
See [RAD07-L3](radiant/Radiant_Issue_Ledger.md#rad07-l3) for the audit and limits.
Embedding full font resources increases standalone SVG size; no subset-font
generator is introduced. Editor Markdown projections retain generated font
declarations after sanitizing authored HTML. Native SVG tests (10), the math
corpus (921), and two document-math UI cases pass. The broader gates retain
a LaTeX corpus timeout, paged-layout failure and an interpreter round-trip
corruption reproduced with the pre-change renderer under forced GC; see
RAD07-L3 for exact checks and limitations. Full baseline acceptance is not
claimed.
SVG titles retain the source expression. The Phase 1–10 acceptance criteria
below describe the historical MathLive adapter; Phases 11–12 use the font and
SVG gates instead.

Font audit and revision (2026-10-08, user): **MATH is an optional enhancement.**
The 8 existing KaTeX WOFF2 faces and 12 CMU WOFF2 faces have no MATH table;
their glyph advances, bounds and outlines remain usable. `math_metrics` now
returns normal glyph facts and `has_math: false` / `constants: null` for these
faces. For explicit supplied/installed fonts, `fallback.ls` applies [MathML Core §5.1 defaults](https://w3c.github.io/mathml-core/#layout-constants-mathconstants)
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

Follow-up for `view test/input/math_intensive_test.tex` (2026-10-09): native
text hit queries still rebuilt the entire host cascade per glyph, and native
text measurement searched for unused ThorVG font files. Geometry queries now
share the document cascade and retain logical character-cell paths, while
ThorVG file resolution happens only for a backend that needs it. Selector
matchers record dynamic state dependencies, including failed `:hover` tests,
so scrolling preserves state-independent SVG styles without suppressing hover
updates. DOM/style/layout changes, font resources and animation generations
still expire the relevant results. The document owns retained paths and callers
receive independent copies (**D4.2.6**); these native rendering caches preserve
the package boundary (**D7.1.1 / D7.1.2v2**).

Release validation for this TeX input: two control runs had median wheel latency
4.37–4.57 seconds; the exact installed release had 151–152 milliseconds in two
confirmation runs (about 30× lower). All twelve wheel events together fell from
92.6–117.3 seconds to 8.83–8.91 seconds, excluding document startup. The controls
and candidate use identical frozen objects except the SVG renderer and selector
matcher. Control medians varied 4.7%; optimized medians varied 0.3%. All hit-target
sequences match, and five viewport PNGs are byte-identical. Cold blank-area hits
still reach about four seconds when the geometry cache for SVGs with visible
overflow must be populated.

The new wheel fixture passes 16 assertions and exits with zero tracked live
allocations; the new hover fixture passes eight pixel/target/scroll assertions.
The native vector suite passes 110 tests, UI baseline 400 fixtures, view UI 11,
DOM UI 136, and the render baseline passes. `make test-radiant-baseline` records
4,135 passes, 350 partial passes and eight failures: the same CSS list-layout
and CSS-memory regressions in both control and final releases, plus six INFO-log
assertions whose messages are compiled out in release. All six log-dependent
checks pass against an isolated debug host. Timing, binary/source hashes, object
inventory and pixel comparisons are retained in
`temp/math-scroll/performance-comparison.json`; debug runs are validation only.

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


### Phase 14 — TeX Math Rules and the CMU Companion (2026-10-09)

User decision: add provenance-tracked Computer Modern math parameters matched
to the bundled CMU profile. The original `cmsy10`, `cmsy7`, `cmsy5` and `cmex10`
TFM files are read in Lambda, with source, license and SHA-256 provenance beside
the resources. This supersedes the default-CMU fallback policy from Phase 11;
explicit supplied/installed fonts still use their own MATH data or ordinary
MathML fallback. Actual glyph geometry remains owned by the painted face.
This changes package layout policy, without changing the formal layering or
IO rulings (**D7.1.1 / D7.1.2v2**); no formal language ruling changes.

The audit uses Knuth's [TeX82 source](https://tug.ctan.org/systems/knuth/dist/tex/tex.web),
Appendix G's math-list conversion algorithms, and independent TeX `showbox`
output in `test/lambda/math/tex_reference.tex`. Fixes cover:

- Rule 18's single-character exception, script-sized compound-nucleus drops,
  display/text/cramped minima, and coupled sup/sub clearance adjustment;
- explicit style commands resetting cramped style; radical and accent nuclei
  using cramped style, with underline preserving its surrounding style;
- sequential binary-atom normalization across explicit glue, the complete
  inter-atom spacing table, and glue using the current style's math quad;
- ordinary fraction/group classification, explicit thick fraction-bar clearance,
  fraction null delimiters and TeX's delimiter factor/shortfall;
- character-accent script attachment, natural narrow accents, designed wide
  accent variants, default display limits and larger display operator glyphs.

Bundled CM parameters select 70% and 50% script sizes. The first superscript
rise is 0.412892 em in display, 0.362892 em in text, and 0.288889 em in cramped
style, before TeX's depth/clearance adjustments. A radical in the display
quadratic formula therefore uses the cramped minimum, not the base letter's
full height. Parameters for scripts come from the corresponding CMSY size;
plain TeX's fixed tenex extension parameters do not shrink with script style.

Scope: this package implements supported parsed math constructs, not the TeX
macro language or arbitrary LaTeX packages. CMU text outlines, missing glyph
italic/skew information, and geometrically scaled ordinary-font delimiters can
still differ from original CM math fonts. Matrix environments retain Lambda's
bounded layout rather than implementing arbitrary TeX alignment registers.
MathLive is a visual comparison, not the normative oracle: its standalone
cramped display radical currently differs from TeX.

Validation (2026-10-09):

- Final focused run: **71/71** math/LaTeX checks, including the new painted
  baseline/size regressions and original-MATH/ordinary-font checks. The two
  slow LaTeX cases were checked separately: `phase4_expl3` passes on replay;
  `phase3_corpus` still exceeds the harness's 60-second limit. The same
  `phase3_corpus` timeout reproduces with the original HEAD math package on
  the identical executable. Initial unpartitioned run: 71/73, two timeouts;
  this is not presented as a clean 73/73 aggregate gate.
- Final SVG smoke corpus: **921/921**, finite boxes and self-contained SVG.
  This is rendering/geometry coverage, not pixel identity with TeX.
- An independent installed TeX run confirms the text/display/cramped script
  shifts and the grouping, accent, underline and mixed-style behavior. The
  four shipped TFM files are byte-identical to the recorded originals.
- Chromium verifies the default quadratic's 70% exponent and approximately
  0.288889 em cramped rise. Eight native PNG/pdfLaTeX comparisons render
  successfully, with visible font/spacing differences; no pixel-equality
  acceptance is claimed. Existing document goldens were refreshed only after
  checking titles and painted glyphs; narrow vector accents now use the
  natural glyph instead of a stretched variant.
- Full `make test-lambda-baseline` attempt: **3994/5621**, with **2112/2112**
  input tests passing. Concurrent builds repeatedly removed/replaced the
  shared `lambda.exe`, causing missing-host failures, so this is not an
  authoritative aggregate result. Focused and corpus checks use a private
  executable copy, SHA-256
  `a492748642e6d3654f07fdb6eb3c0360aa81ee37ff504882665f60e0f3663daa`.
- Scoped `git diff --check` passes. No runtime, native layout, build-system or
  vendor code was changed for this phase.

Artifacts under `temp/`: `math_tex_final_tests.log`, `math_tex_corpus.json`,
`tex_reference.log`, `math_tex_corpus_recheck.log`,
`math_tex_original_corpus.log`, `math_tex_lambda_baseline.log`, and
`math_quadratic_corrected.png`. Independent native comparisons are under
`math_tex_focus_comparison/run-tGfYBP/`.

Reproduce with a stable host and no concurrent rebuild:

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*math_*:AutoDiscovered/*latex_*'
node test/lambda/math/run_corpus.mjs --fixture-source all --jobs 4 --report temp/math_tex_corpus.json
tex -interaction=nonstopmode -output-directory=temp test/lambda/math/tex_reference.tex
```

The TeX `showbox` oracle intentionally emits `! OK` diagnostics and exits
nonzero; inspect its logged dimensions. Production rendering needs no TeX
installation.

## Appendix — TeX conformance audit (2026-10-10)

Scope: the recent feature additions in `b7f698dd9`, `120aafaa3`, and the hook
orientation/bracket-label corrections. **These fixes are not collectively
TeX-conformant.** Restoring a missing construct and producing a recognizable
image did not establish the correctness of its geometry. The following
classification records that gap rather than treating the implementation as a
new layout rule. Architecture remains governed by **D7.2.4 / D7.1.1 /
D7.1.2v2**; this audit changes no formal language ruling.

| Area | Finding and disposition |
|---|---|
| Infinity beside arrows | Root cause was an unintended Helvetica fallback for `\infty`. Plain TeX declares it as ordinary family-2 slot `0x31`; `\rightarrow` is relation family-2 slot `0x21`. TeX's `fetch` selects that font, and ordinary nuclei retain the font baseline. Added the already distributed `KaTeX_Main` CM symbol face to the bundled fallback chain. No infinity-specific translation or ink-centering rule was added. |
| Closed multiple integral sizes | `\\oiint` / `\\oiiint` used a system glyph with no designed display size. The attempted STIX addition violated the distribution decision and has been removed, including its license, TFM and selection path. The bundled definition now composes the existing `\\iint` / `\\iiint` glyph with `\\bigcirc`, following the explicit TeX macro in `test/lambda/math/tex_bundled_integrals.tex`: centered `\\ooalign` rows, `\\vcenter`, and `\\mathop...\\nolimits`. TeX's operator, axis and script rules govern the box; no glyph is stretched or assigned another font's metrics. The contour is circular rather than STIX's oval. This is the documented bundled fallback, not a claim to reproduce STIX/esint glyphs. Explicit fonts keep their own glyphs and data. |
| Vertical arrow delimiter heights | The parser did not consume `\uparrow` and the other five vertical arrow commands after `\big`/`\Big`/`\bigg`/`\Bigg`, leaving ordinary small arrows. The bundled profile now uses the original CMSY small glyph metrics and CMEX extension recipes with TeX82 `var_delimiter`: integer shaft repeats, unchanged heads, logical box joins and axis centering. Explicit AMS sizes use `bBigg@`'s text-style box and CMR parenthesis math-strut extent, including inside scripts. Independent TeX box dumps verify all six arrows at all four sizes. This does not certify the remaining delimiter families or the separate horizontal arrow construction. |
| Stray backslashes between expressions | The `\\` row-break check was in the punctuation parser, but control symbols enter through the command parser. Outside matrices, the command therefore fell through to unknown-command painting. It now produces the existing separator AST used by alignments and serialization. LaTeX's `ltspace.dtx` defines `\\` through `\@normalcr` / `\@gnewline`, emitting layout material rather than glyphs. Independent boxed formulas verify zero natural width for these controls; `\backslash` still paints its symbol. This does not add paragraph line breaking to a standalone math SVG. |
| KaTeX logo lettering | `\KaTeX` previously painted its mixed-case command name. It now follows the reference template's `\mbox` construction: `KATEX`, a three-quarter-size A with its top aligned to T, and the `\TeX` suffix's lowered E. The kerns come from `test/screenshotter/test.tex` and LaTeX `ltlogos.dtx`; heights and x-height come from the selected text face. Math alphabet commands and script styles do not select the logo's text font. This implements `\KaTeX`; the separate `\TeX` / `\LaTeX` command fallbacks still require their own complete macro handling. |
| Sup/sub, fractions, atom spacing | The Phase 14 algorithms cite TeX82 `make_scripts`, `make_fraction`, and the math spacing table, with independent `tex_reference.tex` box dumps. They are the correct basis for placement. Their existing tests are narrower than proof that all fonts and constructs agree with TeX. |
| Bracket/brace annotation mode | The always-stacked default is justified by `mathtools` and plain/LaTeX `\overbrace`/`\underbrace` ending in `\limits`; explicit `\nolimits` overrides it. `limits_box` follows TeX82 `make_op`/big-op-spacing parameters. This validates the annotation policy, not every bracket/brace construction. |
| Bracket geometry | The previous 180-unit ends and generic overbar gap were unsourced. Replaced for `\overbracket`/`\underbracket` with `mathtools`' display-style nucleus, `.7 * fontdimen5(textfont2)` ends, `.2 * fontdimen5(textfont2)` gap, and `ht(\braceld)` rule. The original CMEX TFM supplies the latter. A profile without the required TeX extension metrics now reports an error instead of guessing. |
| Hook and other extensible arrow shapes | Flipping the hook fixed its orientation, but the 200/110/60-unit head, curl and shaft geometry is unsourced. **Nonconforming; replacement remains required.** Use the package's `\arrowfill@` / `\ext@arrow` recipe and actual font components, including `\lhook`/`\rhook` and `\joinrel`; the hand-drawn cubic is not an accepted implementation. |
| Paired reaction arrows | Natural font heads are preferable to fabricated heads. The `.2em` separation, `1.75em` minimum and half-em shortening still need a complete derivation from the supported AMS/mathtools/mhchem macro. Comments and tests repeating those constants do not certify them. **Not certified.** |
| Brace/group/line-segment construction | Centered labels have the right limit policy. Scaled outlines, the remaining 180-unit group/segment construction, and generic gaps do not reproduce the relevant TeX font assembly and macro spacing. **Nonconforming construction remains.** |
| CD diagrams and arrays | CD arrow dimensions and offsets are fabricated; array rule/dash spacing also contains unsourced values. AMS alignment classes, scoped cells and explicit row dimensions have a source-level basis, but the whole table/diagram algorithm is **not certified** against `amscd`/`amsmath`/`array`/`arydshln`. |
| Raise/reflection/vcenter, transforms | Authored `\raisebox` lengths, horizontal reflection, and the `\vcenter` math-axis equation have a rule-based basis. The fixed `\pmb`, phase, actuarial-angle and strikeout constructions do not implement their full package definitions. **Those constructions remain nonconforming.** |
| Modulo, math strut, boxes | The fixed 250/500-unit modulo spacing and 0.7/0.3em math-strut extents are not the AMS mu-glue rules or `\vphantom(`. Box padding/borders need their proper package registers. **Remaining gaps.** |
| Images and text | Raster decoding, embedding, scoped text ASTs and verbatim token preservation restore content. The `0.9em` image default is a KaTeX convention, not graphicx's natural size; general graphicx sizing is not certified. Verbatim's wrong typewriter selection and literal quote/font-encoding mismatch remain open. Content support alone is not typography conformance. |

Independent evidence retained under `temp/math-tex-audit/`:

- `font-facts.txt` and before/after SVGs identify the unintended system font;
  regression tests check CM symbol selection, unchanged baselines and math sizes.
- `bracket-oracle.tex` / `.log` use installed `mathtools` and TeX `\showbox`:
  at 10pt, `ht(\braceld) = 1.19997pt`, symbol x-height `4.30554pt`, end height
  `3.01385pt`, and gap `0.86108pt`. The implementation reads the TFM value;
  these rounded log values are test evidence, not production constants.
- `before/` and `after/` retain native PNGs, PDF references and source hashes.
  Successful comparisons and the 921-case finite-SVG smoke check do not certify
  the remaining constructions or imply pixel equality.

Integral revision (2026-10-10, user): no new fonts may be added to Lambda
packages. `temp/math-integrals/` records the superseded STIX experiment;
its font metrics are not acceptance values for the bundled renderer.
The replacement evidence is under `temp/math-closed-integrals/`.
`test/lambda/math/tex_bundled_integrals.tex` defines the bundled macro without
fitted dimensions: the integral and circle retain their natural sizes and
baselines, shared `\vphantom` extents retain the complete logical box,
`\hfil` centers both rows in their maximum width, and `\vcenter`
centers the composite on the math axis. The compound `\mathop` has no
character italic correction; default limits stay at the side and explicit
`\limits` stacks them through the usual operator rules. Its integral selects
the existing larger face only in display style. The sources for the primitives
are [LaTeX `ltplain.dtx`](https://github.com/latex3/latex2e/blob/develop/base/ltplain.dtx)
and TeX82 `make_vcenter`, `make_op` and `make_scripts`; the fallback macro itself
is Lambda's definition, not an upstream `\oiint` definition.
`tex_integral_reference.tex` independently executes this macro in pdfLaTeX.
Font advances and bounds can differ from the CM reference; pixel/style parity
is not an acceptance gate. Production needs neither STIX nor TeX.

Validation: 26/26 focused math tests and 921/921 SVG corpus formulas pass.
The independent pdfLaTeX oracle produces 17 box dumps and passes 18 axis,
style and limit-mode relations, with only the expected `! OK` diagnostics.
`make test-lambda-baseline` passes 6,581/6,582 checks; the sole failure is the
existing `edit_view_only` sanitized-attribute/math-source mismatch, matching
the earlier row-separator recheck. `baseline-final.log` records the settled
source run; the earlier `baseline.log` overlapped an edit and is invalid.
Native `Integrands` painting was reviewed against the independent PDF;
its font/contour differences remain diagnostic rather than acceptance values.

Vertical-arrow evidence is under `temp/math-vertical-arrows/`: `before/` and
`after/` retain the `DelimiterSizing` native render and independent PDF.
The checked-in `test/lambda/math/tex_delimiter_reference.tex` oracle produces
27 delimiter box dumps: all six arrows at four explicit sizes, script-style `\Bigg`,
and small/tall automatic delimiters. At 10pt, the explicit height/depth pairs
are `8.50006/3.50006`, `11.50009/6.50009`, `14.50012/9.50012` and
`17.50015/12.50015` points. Regression tests verify those logical dimensions,
unscaled head pieces, font selection and repeat counts. Only the affected
automatic-arrow SVG in the HTML golden changed; its source and surrounding
content were reviewed before replacement.

Row-separator evidence is under `temp/math-row-separators/`: the same
`DelimiterSizing` formula previously painted three unwanted backslashes and
now paints none. The delimiter reference oracle also compares `a\\b\\c\\`
with `abc`, and text-box row breaks with their plain text. The definitions
come from LaTeX's [`ltspace.dtx`](https://github.com/latex3/latex2e/blob/develop/base/ltspace.dtx),
not from the screenshot. Regression tests check separator ASTs, painting,
serialization, valid text escapes and literal `\backslash` separately.

Logo evidence is under `temp/math-katex-logo/`: `MathDefaultFonts` reproduces
the mixed-case fallback, and `after/` retains native/PDF comparisons in four
math font contexts. `test_math_katex_logo.ls` checks uppercase painting,
text-font selection, the A and E boxes, size/style handling and serialization.
`tex_logo_reference.tex` reuses the comparison's KaTeX macro definition and
independently dumps eight pdfLaTeX boxes. The kernels are defined in
[`ltlogos.dtx`](https://github.com/latex3/latex2e/blob/develop/base/ltlogos.dtx).
CMU's outlines and size-independent font metrics differ from pdfLaTeX's
optical CM sizes; the box evidence establishes the macro recipe and style
policy, not pixel equality or identical metrics for those different fonts.

For each remaining construction, obtain its package recipe and required font
data first, implement shared TeX box/glue/assembly operations, then validate
with independent TeX box dumps and native painting across text, display,
script and scriptscript styles. Never accept a screenshot-fit substitute or
refresh a golden as the sole evidence of a fix.
