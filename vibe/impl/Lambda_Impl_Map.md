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
- D4.5.2 / D4.5.1v4: a native frame owns its copied Input, compiled styles,
  evaluated paths and spatial index. Document resources retain/release frames;
  detached viewports release their lease. Frames contain no GC pointers.
- D7.5.3 / D5.3.3: declared `radiant.geomap_plan`, snapshot/query functions
  and `radiant.local_point` form the native boundary. Snapshot adapters copy
  metadata into rooted Lambda values. `map.to_svg` and `render_frame` use the
  same frame painter as the viewport, without GeoJSON text attributes.

## Implemented increment

| Area | Code and behavior |
|---|---|
| Element and layout | Append `GEOMAP` to the generated name catalog without renumbering existing identities. Share the SVG/scene viewport sizing path, default 300 × 150 dimensions, CSS sizing and replaced-content classification. `<source>` and `<layer>` remain data children with no layout boxes. HTML `<map>` retains its existing identity and behavior. |
| Camera | Web Mercator, longitude/latitude in degrees, double world calculations, 512 CSS pixels per world at zoom zero, fractional zoom from −2 to 22, bearing, inverse projection, polar clamping, cursor-anchored zoom, pan and bounds fitting across the dateline. Pitch is zero; there is one world copy. |
| Geometry | Inline typed GeoJSON, Feature/FeatureCollection, GeometryCollection, Point/MultiPoint, LineString/MultiLineString and Polygon/MultiPolygon. Native connected paths unwrap longitude across the dateline and align holes to their exterior ring's world copy. |
| Paint | Ordered background, fill, line and circle layers; typed paint expressions and feature filters, hex/transparent colors, layer opacity, circle radius/strokes, fill outlines, line width/caps/joins, zoom range and visibility. Polygon fill uses even-odd holes. Compilation precedes publication so a rejected raw element cannot publish a valid prefix. |
| Export | Raster, document SVG and document PDF route through the same native path compiler. `map.to_svg` returns an SVG element through the Radiant module. Paths, clips and colors remain vector output. |
| Package | `geomap`, `source`, `layer`, `normalize`, `validate`, `from_style`, `project`, `unproject`, `fit_bounds`, `update`, `query_source`, `query_rendered`, `plan`, `render_frame`, `snapshot`, `query_displayed`, `model`, `interactive`, `to_svg`. `from_style` accepts the implemented Style Specification version 8 subset and produces `<geomap>`. |
| Interaction | Per-instance camera, drag, hover and selection state. Captured drag has a 3 CSS pixel threshold and suppresses its synthetic click. Anchored wheel/double-click zoom; arrows/plus/minus/Home/Escape; optional ordinary HTML controls and a selected-feature list. Input unprojects the committed CSS plane. |
| Queries | Stable feature IDs with source-local index fallback. Evaluated native paths feed a balanced AABB index; point/rectangle queries use the shared native SVG stroke/fill walker and reverse paint order. Displayed queries read the last painted frame. Ordinary Lambda snapshots retain historical geometry and copied metadata after resize/removal. |

The native compiler limits a map to 1,024 children, 262,144 visited vertices,
16,384 visited feature records, 262,144 emitted paths and 32 levels of geometry
nesting per frame.
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

## Retained frames, indexed picking and offline interaction

The next increment adds `GeoMapFrame` in `radiant/geomap.hpp`. It owns an Input
copy of the model, compiled layer programs, evaluated paths and a balanced
bounding-box tree. The document resource registry keys viewports by generation
checked DOM references. Unchanged paints replay the frame; invalidation checks
both DOM mutation and style-query epochs plus content-box dimensions. Direct
attribute setters can advance the style epoch without the mutation epoch, so
both are necessary. Failed replacement remains transactional and publishes the
existing pale-red diagnostic viewport rather than a valid prefix.

`map.plan(model, viewport)` exposes a self-contained ordinary Lambda frame,
including camera, dimensions, revision, SVG, path records and the index.
`query_rendered(frame, ...)` accepts points or boxes and traverses the index
before inspecting candidates in reverse paint order. `snapshot(node)` and
`query_displayed(node, ...)` are procedures reading the last painted frame;
they never implicitly paint a newer model. Frame leases survive resize and
removal. Public snapshots own copied feature metadata, with JSON depth/node
quotas, and native consumers retain a frame during result allocation. This
implements D4.5.2/D5.3.3 without a GC-managed native registry.

The map and SVG queries share `dom_geometry_path_query`, extending the existing
path walker with rectangle intersection for fills, stroke segments, caps and
joins. The index uses painted stroke extents, including the miter limit. Fill
outlines and circle fill/stroke paths deduplicate to one feature/layer result.
Self-crossing contours use non-collinearity to admit fill boundaries: signed
area alone can cancel despite painted lobes. A regression covers that case and
a collinear contour. Packed frame queries validate index bounds, recursion,
path style enums and a 64 MiB path-text budget.

The exact property manifest in `test/map/README.md` now includes
`fill-outline-color`, circle stroke color/width/opacity, static line caps and
joins. These use the same compiled expression programs and PaintIR lowering.
The established profile keeps butt/round defaults; it does not imply every
MapLibre line property or default. Programs are retained within each frame;
sharing them across different camera plans is still outstanding.

Native event coordinates use the shared `view_client_to_local` inverse plane
homography through declared procedural `radiant.local_point` (D7.5.3).
Rotation, perspective and singular transforms have focused tests. Reactive
state now includes hover, selection and drag suppression. A 3 CSS pixel
threshold separates a click from dragging; a completed drag suppresses the
synthetic click. Double-click zoom is anchored, Shift reverses it, Home resets
and Escape clears selection. Optional ordinary HTML controls provide zoom,
reset and fit; an optional live list exposes selected feature names.
`on_select`/`on_hover` carry feature/point/geographic details and `on_camera`
carries an input-driven camera change, all through procedural callbacks
(S12.1.3). Autonomous resize callbacks and animation are still open.

Planning computed numeric coordinate arrays exposed a shared ownership bug in
`MarkBuilder::deep_copy`: the numeric-array branch assumed 8-byte elements and
dropped shape/stride metadata. It now copies the actual element width in logical
C order, owns shape dimensions/strides and clears view/backing/borrowed flags.
A compact int16 transposed-array regression destroys the borrowed data before
checking the owned copy; a float32 reversed-view regression covers negative
strides with signed source offsets. Snapshot string copying also uses the
stored byte length, preserving embedded NUL bytes in JSON property values.
This fixes the data boundary rather than changing the map to avoid packed
numeric arrays.

Current validation artifacts are under `temp/map-next/`. Earlier validation
tables above remain historical; they are not a clean gate for this increment.

| Check | Current result on macOS, 2026-10-09 |
|---|---|
| Native map tests | **20/20 passed** in `native-final.log`: retained frame replay, historical paths after resize/removal/document destruction, point/box fill geometry, rotated input, perspective/singular planes, controls, hover/selection, snapshot/displayed-query parity, drag suppression and 2× density input, alongside earlier paint/camera fixtures. |
| Owned numeric-array copies | **40/40 passed** in `deepcopy-final.log`, including compact transposed and negative-stride copies. |
| Package goldens | **8/8** under interpreter and JIT with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; the automatic-tier broader map selection is **42/42**. `package-gc-{interp,jit}-final.log`, `package-auto-final.log`. Frames also reject cyclic indexes, invalid cap enums and malformed query options, and preserve embedded NUL metadata. |
| Native ownership under forced GC | **2/2 passed**, `native-gc-final.log`: retained frames and native pointer/selection callbacks with snapshot/query equality. This run preceded the final byte-counted string-copy/signed-offset edits; the final host's forced-GC package run covers those adapters. |
| PNG/SVG/PDF map exports | **21/21 passed**, `exports-final.log`: seven fixtures through native raster and independent librsvg/Poppler rasterization, including circle strokes, square caps and rotation. Vector outputs contain no raster images. The added paint/control PNGs were visually inspected. |
| Pinned expression reference | **42/42 passed**, `reference-final.log`, against style-spec 26.4.4. This remains expression coverage rather than a complete independent geographic renderer oracle. |
| Shared SVG hit tests | **7/7 fixtures, 73/73 assertions passed**, `svg-hit.log`, covering fill rules, caps, joins, dashes, transforms, instances and pointer events. |
| Radiant dimension lint | **Passed**, `lint.log`. |
| Lambda + input baseline | **5,484/5,628 passed**, `lambda-baseline.log`: input **2,112/2,112**, functional Lambda **1,311/1,312** (`edit_view_only` fails), 141 standard-library batch launches fail, Test262 preflight fails and the math runner produces no report. Outside-sandbox focused reruns recover **141/141** standard-library cases and **715/715** math corpus cases (`std-unsandbox.log`, `math-corpus.log`). These recoveries do not make the aggregate a clean pass. |
| Test262 baseline | The summary reports **40,261/40,261**, but `test262-baseline.log` records a killed AST Unicode-identifier batch with **82 lost/recovered tests**, followed by isolated retries. The unmodified named Unicode source passes alone with the pinned host (`unicode-isolated.log`). Batch stability remains unresolved; this is **not a clean gate**. |
| Radiant baseline | **Failed**, `radiant-baseline-unsandbox.log`: aggregate **4,054 passing, 350 partial and 6 failed / 4,410**. Failures include CSS list/Markdown layout, `doc_editor_indexed_math_arrows`, `dtna_styles`, `radiant_view_math_intensive_scroll` and CSS cascade memory. The view-command runner also fails `UiScriptContentSurvivesForcedGc` and a Doom replay exits **139**; it was stopped after further silence. Its aggregate row incorrectly labels **0/0** as passing and contributes no positive coverage. Shared vector tests pass **110/110** and DOM integration **136/136**; visual results are **206/212 passing, 5 expected failures and 1 skipped**, with the visual baseline gate accepted. |

The final focused host is `temp/map-next/lambda.exe`, SHA-256
`05f4d2533134755361e59f1e3135cd77743c4a11fd728f4bf2726f4a890650d9`.
`provenance.json` records the host, runners, reference corpus and source hashes.
The host was linked directly to that artifact with the generated Makefile's
`TARGET` override, preserving the root binary during broad tests. Broad gates
ran in a shared checkout with concurrent non-map edits and preceded the final
string-copy/signed-offset changes. No release performance claim is made.

## Remaining scope and engine assessment

Phase 1 is **in progress**. The offline increment now has native historical
frames, indexed point/rectangle queries, the proposed flat fill/line/circle
properties, selection callbacks and HTML navigation controls. These proposal
requirements are still open:

1. Persistent compiled source/style sharing across camera plans; dedicated
   compile/plan/resource APIs. Document-wide epoch invalidation currently
   rebuilds a viewport even for unrelated mutations. Dense/many-map performance
   budgets and a fully independent geographic reference corpus remain open.
2. TileJSON, URL resource adaptation with generation/cancellation and cache
   policy, raster tiles, bounded MVT decoding and tile selection/overscaling.
3. Symbol layers, font/sprite/glyph resources, collision placement and attribution
   aggregation. Advanced line-following text belongs to the later extension.
4. Autonomous fit/resize notifications, camera animation and broader keyboard/
   accessibility acceptance. Camera callbacks currently report input-driven
   changes; DOM size changes become visible on the next layout/paint.
5. A clean broad release gate, release performance measurements, Linux/Windows
   runtime evidence and the Phase 2 JavaScript API.

No missing general-purpose parser syntax, rendering backend or GC mechanism
blocks the offline map. This increment supplies the shared CSS-plane input
unprojection that the earlier engine assessment identified as missing. The
tiled milestone still needs a map-specific resource lifecycle/cache adapter and
an MVT decoder; existing network/image services should be reused. Symbol
placement still needs a map-specific collision index. These gaps are
prerequisites for their milestones, not claims that the engine lacks all of
their lower-level primitives.
