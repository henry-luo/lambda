# Native Lambda/Radiant Map Implementation

**Started:** 2026-10-09. **Design:** [Lambda_Pkg_Map.md](../Lambda_Pkg_Map.md).

The user selected native `<geomap>` for Phase 1 and a JavaScript API over the
same renderer for Phase 2. This record distinguishes implemented coverage from
the larger proposal. It does not change any formal ruling.

## Contracts

- D7.2.4: the public package is `lambda.map`, with explicit entry
  `lmd/package/map.ls` and private implementation modules beneath `map/`.
- S12.1.1v2 / S12.1.3: camera calculations, normalization, querying and SVG
  export are functions; DOM reads, focus and capture in event handlers remain
  procedural. Interaction state belongs to each reactive view instance.
- S7.4.1: invalid package input returns an ordinary error value. Unsupported
  layer types, expressions and properties are rejected explicitly.
- D4.5.2 / D4.5.1v4: the native painter borrows a Mark description only during
  compilation and publishes owned paths to PaintIR. There is no retained map
  registry, GC pointer, GPU handle or external resource in this first increment.
- D7.5.3 / D5.3.3: `map.to_svg` uses the declared `radiant.geomap_svg` function;
  its native adapter roots its argument and calls the same paint compiler used
  by the viewport. It does not serialize typed GeoJSON into HTML attributes.

## Implemented increment

| Area | Code and behavior |
|---|---|
| Element and layout | Append `GEOMAP` to the generated name catalog without renumbering existing identities. Share the SVG/scene viewport sizing path, default 300 × 150 dimensions, CSS sizing and replaced-content classification. `<source>` and `<layer>` remain data children with no layout boxes. HTML `<map>` retains its existing identity and behavior. |
| Camera | Web Mercator, longitude/latitude in degrees, double world calculations, 512 CSS pixels per world at zoom zero, fractional zoom from −2 to 22, bearing, inverse projection, polar clamping, cursor-anchored zoom, pan and bounds fitting across the dateline. Pitch is zero; there is one world copy. |
| Geometry | Inline typed GeoJSON, Feature/FeatureCollection, GeometryCollection, Point/MultiPoint, LineString/MultiLineString and Polygon/MultiPolygon. Native connected paths unwrap longitude across the dateline and align holes to their exterior ring's world copy. |
| Paint | Ordered background, fill, line and circle layers; typed paint expressions and feature filters, hex/transparent colors, layer opacity, circle radius, line width, zoom range and visibility. Polygon fill uses even-odd holes; lines use butt caps and round joins. Compilation precedes publication so a rejected raw element cannot publish a valid prefix. |
| Export | Raster, document SVG and document PDF route through the same native path compiler. `map.to_svg` returns an SVG element through the Radiant module. Paths, clips and colors remain vector output. |
| Package | `geomap`, `source`, `layer`, `normalize`, `validate`, `from_style`, `project`, `unproject`, `fit_bounds`, `update`, `query_source`, `query_rendered`, `model`, `interactive`, `to_svg`. `from_style` accepts the implemented Style Specification version 8 subset and produces `<geomap>`. |
| Interaction | Reactive per-instance camera state, pointer capture for dragging, cursor-anchored wheel zoom and arrow/plus/minus keyboard navigation. Events measure the committed content box, including CSS border/padding and device density. |
| Queries | Source feature queries retain explicit feature IDs, with source-local index fallback. Rendered queries respect layer order, zoom, visibility, opacity, clipping and polygon holes, using shared chart geometry helpers. This increment performs a linear scan and describes the current model rather than a retained historical frame. |

The native compiler limits a map to 1,024 children, 262,144 visited vertices,
16,384 visited feature records and 32 levels of geometry nesting per paint.
These counters include repeated traversal for different layers. A raw invalid
element produces a pale red viewport and logs its diagnostic; normalized
package input returns an error value earlier. Native painting requires typed
GeoJSON data; text can be parsed by Lambda with `parse(text, 'json')` before
constructing a source.

## Initial viewport validation

The initial reproduction is `test/map/offline.ls` rendered by the pre-change
binary to `temp/geomap-before.png`: the map collapses to a generic empty box.
After the native viewport integration the same fixture paints its background,
land, hole, road and marker with CSS chrome beside an ordinary SVG.

Cross-format rendered checks exposed missing PaintIR lowering in the shared PDF
path: even-odd fills were skipped, stroke caps/joins were omitted and primitive
alpha was ignored. The first-party `lib/pdf_writer` now emits the corresponding
PDF path/graphics-state operations, and `render_pdf.cpp` preserves them. The
ordinary SVG beside the map also covers the nested lowerer's inherited opacity.
No vendored PDF code was changed.

The stroke fixture also reproduces PDF's device-hairline interpretation of a
zero-width stroke. Map line width zero means no paint, so the map compiler
validates that geometry and omits its path before lowering. The writer's
hairline semantics remain available to other PDF callers.

The focused runner is `test/test_map_gtest.cpp`; Lambda goldens are in
`test/lambda/map/`, explicitly wired into the nonrecursive discovery list in
`test/test_lambda_gtest.cpp`. Fixture `.ls` files under `test/map/` are rendered
documents, not golden-driven unit scripts. Every functional unit script has a
matching `.txt` golden.

```bash
make test-map
make test-map-export
make test-svg-export
make lint ARGS='--rule ^no-int-cast-radiant$'
make test-lambda-baseline
make test262-baseline
make test-radiant-baseline
./lambda.exe render test/map/offline.ls -o temp/geomap-map.svg
./lambda.exe render test/map/offline.ls -o temp/geomap-map.pdf
pdftocairo -png -singlefile -r 96 temp/geomap-map.pdf temp/geomap-pdf
rsvg-convert temp/geomap-map.svg -o temp/geomap-svg.png
```

| Check | Result on macOS, 2026-10-09 |
|---|---|
| Native camera/layout/paint/input | **10/10 passed**, including 2× raster density, real drag/capture/wheel/keyboard input, two independent maps and removal of a non-layout data layer. Rebuilt after the interrupted session and passed again in `temp/map-native-current.log`. |
| Package goldens | **5/5 under each of `interp`, `auto` and `jit`**; the broader `AutoDiscovered/*map_*` selection passes **39/39**. `temp/map-*-{interp,auto,jit}.txt`, `temp/map-functional-final.log`; pinned-host reruns are recorded below. |
| Map PNG/SVG/PDF exports | **12/12 passed**: four fixtures across three formats, independently rasterized with librsvg/Poppler. Holes, dateline crossings, clipping, layer alpha, butt caps, round joins and zero-width line suppression retain their pixels; vector exports contain no raster images. `temp/map-export-final.log`. |
| Shared SVG/PDF export matrix | **108/108 passed**, covering 18 existing fixtures × 2 densities × 3 formats. `temp/map-svg-export-regression.log`. |
| Radiant integer-cast lint | **Passed**. `temp/map-lint-final.log`. |
| Lambda + input baseline | **6,531/6,532 passed**. The failing `edit_view_only` check reports a Markdown math round-trip mismatch in interpreter/auto mode and passes with `LAMBDA_EXEC_BACKEND=jit`. Its root cause remains unresolved; no golden or engine workaround was applied. `temp/map-lambda-baseline.log`, `temp/map-editor-{interp,auto,jit}.txt`. |
| Test262 baseline | **40,256 fully passing, 5 slow tests recovered on retry, 0 final failures**. The five Unicode identifier tests remain non-fully-passing under the runner's timing gate; this is **not a clean Test262 gate**. `temp/map-test262-baseline.log`, `temp/map-test262-artifacts/`. |
| Radiant baseline | **4,141 passing, 350 partially passing, 3 failures out of 4,494** in the aggregate; **failed**. Failures: `change-list-descendant-display`, `DoomNativeGameplayAndEpisodeLifecycle` and `PageLoadAndRecascadeStayWithinBaseline`. The preceding 59 scene, 8 map and 368 focused view tests also passed; the two newer map regressions pass in the focused runner. `temp/map-radiant-baseline-final.log`. |

The initial sandboxed Radiant run stalled in the first native OpenGL test with
macOS GUI-service connection errors. That test passed outside the sandbox, as
did the subsequent native graphics run. The CSS list baseline regression
`change-list-descendant-display` reproduces independently; its complete layout
tree is identical to saved batch artifacts from 11:43 and 13:02, before
`radiant/geomap.cpp` was created at 15:34. This is evidence of an existing layout
failure, not a passing broad gate. No baseline was lowered.

The Doom failure is the `lifecycle` replay exiting with 143 after its 600-second
timeout; the other five replays completed. CSS memory reports 375,038 live bytes
against a 373,940-byte budget for jqueryui and 669,594 against 667,901 for
linuxmint, at both initial cascade and recascade. Their causes remain
unresolved; these results are not attributed to the map or declared pre-existing.
The raw diagnostics are `temp/_radiant_view_cmd.log` and
`temp/_radiant_css_cascade_memory.log`.

The public example in `doc/Lambda_Packages.md` was rendered and visually checked
as `temp/map-doc-example.png`. Generated images and logs live under `temp/` and
are not part of the committed fixtures.

The final map export check used the separately linked release host
`temp/map-final-lambda.exe` (SHA-256
`bccc1685097a0e950b3ba5200d8dd5276c9bfe903d5595816ecd2ffbb92c734f`),
with `LAMBDA_BIN` selecting it in `test/map/check_exports.cjs`. Its host export
check passes. This includes the last map-only zero-width line fix; the shared
SVG/PDF matrix and broad gates started before that fix. Other work in this
shared checkout changed the main `lambda.exe` during the broad run, so those
aggregate results do not certify a single frozen final-worktree binary.
After the broad run, the same 12 export cases also passed with the current main
`lambda.exe` (SHA-256
`9165687b90aa0f0d654471465e6bea27dfadb992b062c04b5acef11ee3e08af8`),
including zero-width line suppression. Its host export check passes too.
The log is `temp/map-export-main-final.log`.

A subsequent concurrent build removed the root executable during a functional
rerun (`temp/map-functional-current.log`). The rebuilt harness therefore runs
from `temp/map-validation/`, with its `lambda.exe` linked to the preserved
release host and test/resource directories linked to this checkout. This
keeps the actual golden harness and current package sources while pinning its
CLI dependency. The broader selection passes **39/39** there, including the
five map cases in auto mode; those five also pass **5/5** in interpreter mode
and **5/5** in forced JIT mode. Logs: `temp/map-functional-pinned.log`,
`temp/map-interp-pinned.log`, `temp/map-jit-pinned.log`.

## Expression and filter increment — 2026-10-09

`radiant/geomap_style.{hpp,cpp}` now compiles the supported expression language
once per layer paint or query batch. The same evaluator serves native raster,
SVG/PDF, package normalization and rendered-feature queries through declared
Radiant functions (D7.5.3). It records feature/zoom dependencies, checks arity,
static types, zoom placement, increasing stops and distinct match labels, and
validates unused branches and constant failures. The compiler has depth 32 and
a shared budget of 1,024 expression visits/match labels per layer.

Missing properties return null; `has` preserves present-null distinction.
Boolean tests use strict expression types, independent of Lambda truthiness.
Filters evaluate at floored zoom while paint remains fractional. Bad runtime
paint properties use black/opacity 1/radius 5/width 1 defaults; bad filters
exclude features. Native painting logs fallback bits once per layer. Static
errors return ordinary package errors (S7.4.1), and raw native errors retain
transactional rejection of the whole map, including preceding valid layers.

Compiled nodes borrow the rooted description for one synchronous call
(D4.5.2, D5.3.3). D4.3.1 and D4.3.2v2 keep object headers and strings stable;
array/map readers are acquired afresh during evaluation so compacted backing
data is not retained across result allocations. Batch evaluation roots the layer and feature array while
allocating result records. It accepts up to 16,384 features and propagates a
batch error from the public query. The query applies the evaluated visibility,
alpha and radius/width, and does not normalize the model again for every layer.
It still scans projected geometry rather than retaining a frame index.

The [supported manifest](../../test/map/README.md) and
[`expression_cases.json`](../../test/map/expression_cases.json) freeze the
subset. `test/map/package-lock.json` pins the ISC-licensed upstream style
specification evaluator at 26.4.4. The independent runner compares 31 evaluated
cases and 11 compilation rejections. Its report records binary and corpus
SHA-256 values. This pins expression acceptance only; preparation's independent
geographic/browser captures and tiled asset corpus remain open.

Rendered checks use `test/map/expressions.ls`; the negative fixture
`invalid_expression.ls` verifies that a constant error in an unused branch
rejects the preceding valid background too. Functional tests add
`test/lambda/map/expression.{ls,txt}`. They cover missing/null, assertions,
lazy boolean branches, explicit objects, labels, stops, zoom restrictions,
property defaults, filter failures, query size and batch evaluation.

The reader-lifetime crash found during implementation came from a property
iterator borrowing a temporary `MapReader`. The compiler retains that reader
through traversal. Pinned upstream comparison then exposed numeric `coalesce`
accepting literal null and expected types not reaching unused branches. The
compiler now propagates expected output types and validates constant failures.
No runtime workaround or golden relaxation was used.

```bash
npm ci --prefix test/map --ignore-scripts
make test-map-reference
make test-map
make test-map-export
```

| Focused check | Result and evidence |
|---|---|
| Native camera/layout/paint/input/error recovery | **12/12 passed**; `temp/map-phase1/native.log`. |
| Package goldens | **6/6 per interpreter, auto and JIT tier**; broader auto map selection **40/40**. `temp/map-phase1/package-{interp,jit,auto-broad}.log`. |
| Pinned upstream expressions | **42/42 passed**; `temp/map-phase1/reference.log`, `temp/map-expression-reference/report.json`. |
| Map exports | **15/15 passed**, five fixtures × PNG/SVG/PDF; independent librsvg/Poppler pixels and vector-only structure. `temp/map-phase1/exports.log`. |
| Precise rooting | Expression/query/batch golden agrees under forced collection and freed-memory poisoning in interpreter and JIT modes. `temp/map-phase1/expression-gc-{interp,jit}.txt`. |
| Host export and Radiant integer-cast checks | Passed; `temp/map-phase1/host-exports.log`, `temp/map-phase1/lint.log`. |

The preserved debug host SHA-256 is
`6205de8911519f88b54238167997f2308c84e853a0ac0039dbaf31b1f45c7534`.
`temp/map-phase1/provenance.json` also records the native runner, corpus, lock
file and rendered-fixture hashes. Functional reruns use the pinned host through
the isolated `temp/map-validation/` harness directory. No performance claim is
made from this debug build.

The first full Lambda baseline attempt reported **6,319/6,542 passing** with
223 failures (`temp/map-phase1/lambda-baseline.log`). Many child launches failed
inside the sandbox. The same pinned-host optimizer suite passes **54/54**
outside it after supplying its required source-directory link
(`temp/map-phase1/opt-outside-sandbox.log`). That focused recovery does not turn
the earlier aggregate into a pass. A full rerun outside the sandbox is recorded
separately in `temp/map-phase1/lambda-baseline-outside-sandbox.log`: **6,540/6,542
passed**, with `math_test_math_html_output` and `edit_view_only` still failing.
No unrelated source or golden was changed to close those failures.

The broad Radiant run still **fails**: its aggregate reports **3,930 passing,
350 partially passing and 5 failures out of 4,285**
(`temp/map-phase1/radiant-baseline.log`). The failures cover CSS list layout,
`doc_editor_indexed_math_arrows`, `js_load_bootstrap_global_binding`,
`radiant_view_math_intensive_scroll`, and CSS cascade memory. The view-command
row reports zero tests after a long run, so it provides no positive coverage
count. These shared-checkout aggregates are not evidence of a clean final-host
gate or of map causality; their root causes remain unresolved here. The earlier
Test262 result above is historical and was not rerun for this Radiant/package
increment, which does not change the JS engine.

## Remaining scope and engine assessment

Phase 1 is **in progress**, with the offline viewport increment implemented.
The following proposal requirements are still open:

1. Persistent compiled styles/frame snapshots and an indexed query structure;
   query/render equivalence for all supported topology and historical frames. Pure queries use the supplied model's
   numeric viewport dimensions; callers must supply the committed dimensions
   when CSS changes the viewport size.
2. TileJSON, URL resource adaptation with generation/cancellation and cache
   policy, raster tiles, bounded MVT decoding and tile selection/overscaling.
3. Symbol layers, font/sprite/glyph resources, collision placement and attribution
   aggregation. Advanced line-following text belongs to the later extension.
4. Fit/resize notifications, click feature notifications, animation, full
   keyboard/accessibility control UI, and inverse CSS-transform mapping for
   interactive viewports under rotation/skew/perspective.
5. Performance budgets, deterministic independent geographic reference
   comparisons, Linux/Windows runtime evidence and the Phase 2 JavaScript API.

No missing general-purpose parser syntax, rendering backend or GC mechanism
blocks the offline native map: ordinary typed elements, shared replaced sizing,
PaintIR, module calls and DOM capture are sufficient after fixing the PDF
lowering gaps found by the rendered checks. The tiled map milestone
still needs a map-specific resource lifecycle/cache adapter and an MVT decoder;
existing network/image services should be reused. Symbol placement still needs
a map-specific collision index. Precise local input coordinates under arbitrary
CSS transforms need a shared geometry unprojection interface rather than a
package approximation. These gaps are prerequisites for the corresponding
milestones, not claims that the engine has none of their lower-level primitives.
