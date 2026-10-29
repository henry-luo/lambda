# Chart gallery startup performance

## Scope and acceptance

Reduce startup work for `./lambda.exe demo chart` → Normal charts while retaining
all 44 dashboard examples, three columns on desktop, centered charts, one column
on mobile, and the existing playback, selection, filtering and navigation.
Preserve the existing dashboard SVG golden and curve precision. Qualify with the
chart golden suite, native collection replays, the Lambda baseline, and paired
release first-frame measurements.

## Phase 1: path sampling

A release layout diagnostic spent 1,334 ms loading/generating the document,
157 ms in layout and 9 ms serializing its layout tree. CPU samples and temporary
per-call instrumentation identified coordinate projection, adaptive path
sampling and picking outlines as significant generation costs. Nested function
times were used to locate work, not summed as independent phases.

The changes are confined to the chart package:

- `path_geometry.ls` reuses interval endpoints and recursive subdivision
  endpoints. An identity projection keeps straight SVG segments exact without
  probing their flatness. Finite-coordinate and sampler-option checks remain.
- `coordinate.ls` declares Cartesian projections without fisheye transforms
  affine when sampling straight boundaries. Eight intervals still produce the
  same nine endpoints, using nine projection evaluations rather than forty.
  Curved and nonlinear projections retain adaptive sampling at the same
  precision and recursion limit.
- Path warping parses each path once and only constructs the separate unwarped
  outline when a pixel-sized marker needs it. Cartesian and custom projection
  avoid computing unused polar geometry.

Sample reuse relies on the pure `fn` contract in **S12.1.1v3**. No template state
or event scheduling was changed. A presentation-cache experiment was discarded
because it did not preserve live updates.

`test_coordinate_grammar.ls` additionally checks nonlinear projection of a
straight source segment, endpoint preservation, invalid precision, and errors
at an interior sample. Its existing `[]` golden remains unchanged.

### Qualification

Five interleaved pairs of fresh processes, after builds and regression runners
exited, used the same pinned release executable and frozen package snapshots.
Filesystem caches were warm. The 1180 × 900 native replay asserts 44 cards and
44 chart SVG roots, captures the first frame, then closes. An earlier run under
concurrent regression load is retained separately and excluded from these
results.

| Median wall time | Original | Optimized | Reduction |
| --- | ---: | ---: | ---: |
| Process start to first captured frame | 1,928.9 ms | 1,664.7 ms | 13.7% |
| Complete replay and process exit | 1,982.4 ms | 1,716.5 ms | 13.4% |

All ten runs exited successfully with four assertions passing per run. This
measures the all-chart page through native PNG capture, rather than just script
execution; it is a local warm-cache result, not a cold-storage guarantee.

Validation:

- `make test-lambda-baseline TEST_JOBS=4`: **8,217/8,217**, exit 0
  (2,112 input checks and 6,105 Lambda checks), including **127/127 chart
  goldens**. The complete dashboard HTML/SVG golden is unchanged.
- Native collection replay: **26/26** interaction assertions and **21/21**
  navigation assertions. Playback, pan/zoom, independent selection, linked
  filtering, scientific output and diagrams remain available.
- Mobile replay: **7/7** assertions, including all 44 examples, one column,
  horizontal centering and no horizontal page overflow. Desktop captures were
  inspected for three columns and centered output.
- Coordinate regression script matches its golden on both `interp` and `jit`.
- Scoped `git diff --check` passed. No native engine changes remain from the
  temporary instrumentation, and no goldens were regenerated.

Artifacts under `temp/chart-load/` include `benchmark.json`, `bench.py`,
`benchmark-events.json`, `source-sha256.json`, `shared-source-sha256.json`,
`lambda-baseline.log`, `lambda-baseline-summary.json`, `lambda-gtest-final.json`,
the native replay results, and desktop captures. `before/` and `after/` retain
the chart source snapshots; `shared/package/` freezes their unchanged imports.
The pinned release executable `runtime.exe` has SHA-256
`89ff3710a9bf1fdff14eb5d74c315a469c435e7c228224896c9f460b02fac22c`.

At the end of phase 1, startup still included generating and laying out every
chart before presenting the page. Phase 2 addresses that remaining eager work.

## Phase 2: incremental gallery loading

The interactive normal page first constructs the top motion/exploration charts
and all 44 dashboard tile shells. Each shell holds an unapplied `chart.model`;
chart generation, DOM construction, and layout of its SVG wait for that tile's
frame callback. An empty SVG and the shared controls container reserve the same
box structure as a live chart. Reserving only the SVG height had changed grid
row sizes, so the placeholder now includes the real controls-container shape.
No manually adjusted pixel offsets are used.

`mod_gallery.ls` schedules one tile per native frame with `dom.request_frame`.
The native frame queue's token watermark puts a request made by a callback in
the next frame. Input and painting can run between tiles. Each callback reads
committed viewport geometry and chooses a pending visible tile first; pending
tiles elsewhere continue in document order. All 44 cards and their scroll
positions exist immediately, without pagination. Each applied tile owns its
chart state, so completing another tile preserves selection (**S12.1.3**,
**D4.5.3v2**).

This is page-level cooperative scheduling on the existing UI thread. It does
not introduce concurrent access to the DOM or turn an individual chart render
or an arbitrary CSS formatting context into a resumable computation. The top
charts still render eagerly. Plain static export does not run this gallery's
frame queue; the original `test/lambda/chart/chart_dashboard.ls` remains the
eager static dashboard entry point.

The implementation changes only demo source, CSS, and replay fixtures. The
native engine and chart package remain unchanged in this phase.

### Qualification

Native desktop and mobile replays verify first-frame placeholders with working
top charts; pause commands while loading remains pending; scrolling directly
to the last tile and materializing it before all earlier tiles; stable scroll
extent and position; selection surviving the remaining loads; all 44 actual
chart SVGs eventually appearing; and navigating away with work pending, then
reloading a fresh gallery.

- Desktop incremental replay: **25/25** assertions.
- Mobile incremental replay: **25/25** assertions, including centered charts
  and no horizontal overflow at 390 px.
- Existing chart interaction replay: **26/26** assertions.
- Collection navigation replay: **21/21** assertions, including scientific
  graphics and diagrams.
- Desktop and mobile dashboard captures were visually inspected. Three desktop
  columns and one mobile column remain.

The package/native baseline results above belong to phase 1. Phase 2 has no
package or native source changes; its focused native viewer replays qualify
the new loading behavior.

Release first-frame measurements and source snapshots are recorded under
`temp/chart-incremental/` using the same pinned executable as phase 1.

Five interleaved pairs of fresh release processes at 1180 × 900 produced:

| Median time to first captured frame | Eager gallery | Incremental gallery |
| --- | ---: | ---: |
| Process start through native PNG capture | 1,688.6 ms | 333.9 ms |

First-frame latency fell **80.2%**. The common replay checks all 44 card
positions and both top animation SVGs, captures the frame, then pauses a top
chart successfully and closes. All ten runs exited successfully with five
assertions passing per run. Incremental runs close with work pending, so their
total process time is not a measurement of completing all 44 charts. Completion
and interaction during loading are covered separately by the native replays.

`benchmark.json`, `benchmark-events.json`, `bench.py`, per-run phase timings,
`source-sha256.json`, and `before/demo` / `after/demo` retain the comparison.
`desktop-final.json` and `mobile-final2.json` hold the incremental replay
results; `interaction-final.json` and `navigation-final.json` hold the existing
collection results. Desktop and mobile captures are `desktop-dashboard.png`
and `mobile-dashboard.png`. Scoped `git diff --check` passed.


## Phase 3: eager loading and debug timestamps (2026-10-29)

The normal gallery now applies all 44 dashboard charts while constructing the
page. The deferred panel, frame queue, placeholders, and their CSS are removed.
`tests/eager.json` replaces the incremental replay and requires all chart SVGs
on the first frame, including the last tile, and again after navigation.
Applied chart ownership remains per view (**S12.1.3, D4.5.3v2**).

### Causes and implementation

The large debug-only stall came from logging, rather than deferred loading or
layout. The default DEBUG configuration emits hundreds of thousands of records
during eager chart generation. `get_timestamp` called `localtime` and `strftime`
for every record even though timestamps have only second precision. A standalone
logger benchmark compiled with `-O3 -DNDEBUG` and the release logging library
isolated this cost: 25,000 timestamped records took **2,178.4 ms**, versus
**5.6 ms** with timestamps disabled.

`lib/log.c` now retains the formatted timestamp for the current second and date
format in thread-local storage. A second or format change refreshes it through
`localtime_r` / `localtime_s`, avoiding the shared `localtime` result as well.
The same release benchmark takes **8.0 ms** with timestamps and **5.9 ms** without.
Existing DEBUG messages and `log.conf` are preserved. This changes diagnostics
only, consistent with **D5.4.4** ("diagnostics may never feed semantics").

Release diagnostics also identified avoidable chart generation work:

- Arc sampling now computes endpoint-to-center conversion once per segment,
  including the adaptive length table used by arrowheads.
- Adaptive sampling validates interval endpoints once and validates only newly
  sampled interior points during subdivision. Sampling precision, recursion
  limits, endpoints, and the existing dashboard golden remain unchanged. This
  reuse follows the pure-function contract in **S12.1.1v3**.
- Interaction decoration samples displayed picking outlines only when a point
  parameter or behavior requests `nearest`. Direct mark events retain their row
  metadata and selections; nearest picking retains displayed geometry.
- Charts without timeline or enter/update/exit animation render directly. Their
  render notifications do not initialize an unnecessary playback clock or
  regenerate the static SVG. Animated charts retain the existing playback path.

### Release comparison

A pinned release executable ran isolated before/after package snapshots with
the same eager demo, data, CSS, viewport, and imports. Four interleaved pairs
include one warm-up pair; the following values are medians of the remaining
three. The layout command reports generation and layout through first render;
this comparison does not include native PNG capture.

| Median | Original eager package | Optimized eager package |
| --- | ---: | ---: |
| Document generation | 1,097.3 ms | 739.4 ms |
| Layout | 312.5 ms | 312.2 ms |
| Through first render | 1,416.9 ms | 1,060.1 ms |
| Complete process | 1,499.6 ms | 1,119.7 ms |

The package changes reduce generation by **32.6%** and first render by **25.2%**.
The much larger debug logging improvement is isolated by the release logger
benchmark above; no debug performance benchmark was used.

### Qualification

- Debug build succeeded. With default DEBUG logging, desktop eager replay:
  **23/23** assertions; mobile at 390 px: **24/24**; existing interaction replay:
  **26/26**; collection navigation, including scientific graphics and diagrams:
  **21/21**. Desktop and mobile captures were inspected.
- The chart suite's **128** goldens pass, including the new
  `test_startup_geometry.ls` / `.txt` regression. It checks exact arc samples,
  invalid endpoint/interior coordinates, direct row metadata, and both nearest
  selection and nearest behavior geometry. Existing goldens were not rewritten.
- The Lambda baseline initially reported **8,420/8,425**. Four failures were
  sandbox-denied loopback binds; the server runner passed **152/152** with socket
  access. The checkout's unrelated `navigation_archive` failure passed on a
  standalone rerun. All five initially failing cases therefore passed their
  focused reruns; the initial aggregate command itself exited nonzero.
- A release-built logging check verifies time/date format switches, timestamp
  disabling/re-enabling, second rollover, and four concurrent logging threads.
- Scoped `git diff --check` passed.

Artifacts are under `temp/chart-eager/`: `benchmark.json`, `bench.py`, isolated
`before` / `after` source snapshots, the pinned release/debug executables,
`log-bench*.{c,log}`, `log-check.{c,txt}`, build/baseline logs, focused reruns,
and native replay results. Desktop capture is
`temp/chart-demo/eager-dashboard.png`; mobile capture is `mobile.png`.


## Phase 4: quiet paint compatibility probes (2026-10-29)

The morph sampler called the strict `lambda.slide.color.parse` on both ends of
every changing paint attribute, then caught its errors to choose the existing
crossfade fallback. Valid SVG paints such as `none`, a missing attribute, and
`url(#...)` are not normalized colors. The strict parser therefore raised and
logged diagnostics on every sampled frame even though the chart could continue.
A minimal `none`/solid-color fill-and-stroke transition reproduced the paired
`color: chart animation` / `slide: chart animation` diagnostics.

The shared parser now exposes `lambda.color.try_parse(value) -> array?` for
compatibility probes. The existing strict `parse` delegates to it and retains
its raised-error contract (**S7.4.1–S7.4.2**). Invalid numeric components in
functional color syntax become a failed probe instead of an RGBA array
containing an error. Chart sampling uses the non-raising probe, retaining normal
color/alpha interpolation and the existing incompatible-paint crossfade policy.
The minimal reproducer produces byte-identical SVG without runtime errors.
No logging level or error reporting policy was changed (**D5.4.4**).

The new error-sensitive regression also exposed entering containers passing an
absent source parent to `content`. Their keyed source-child list is now empty,
so bar/point keyframes do not report that separate boundary failure. This keeps
sampling pure and reproducible (**S12.1.1v3**).

`test_animation_colors.ls` / `.txt` covers supported/unsupported probes,
solid-color and premultiplied-alpha interpolation, missing paints, unresolved
paint references, entering containers, and the demo's bar/point keyframes.
`test_animation_grammar.ls` separately checks explicit `fallback: "error"`
diagnostics alongside its existing negative cases. The native
`ChartAnimationTests.PaintFallbackDoesNotRaiseAcrossTiers` test checks both the
golden output and captured runtime diagnostics on `interp`, `auto`, and `jit`;
golden-only comparisons had allowed the handled raises to go unnoticed.

Focused verification: **156/156** chart/slide/native tests. The normal-page
interaction replay passes **26/26**, including replay, pause/resume, seeking,
pan/zoom, selection and filtering, with **zero runtime/error records**. A separate
negative probe confirms the strict shared and slide parsers retain their raised
diagnostics. Artifacts are under `temp/chart-animation-color/`.


The full Lambda baseline completed **8,426/8,428** on its initial run. The quiet
animation check initially included an explicit-error negative assertion; that
assertion now lives with the negative animation grammar cases, and both tests
pass their focused rerun. The concurrently edited JS resident-module test also
passes its isolated rerun. Thus both initially failing cases pass on rerun;
the initial aggregate command itself exited nonzero. No unrelated JS/runtime
source was changed for this repair.


## Phase 5: runtime predicates and prepared projections (2026-10-29)

### Scope and causes

The remaining click-to-page delay is synchronous generation and layout of the
complete eager gallery. This milestone keeps all 44 dashboard examples on the
first frame, the unchanged dashboard golden, per-view selection and playback,
and the desktop/mobile layouts. Qualification uses paired release layout and
index-to-page capture runs, a native predicate-allocation regression, chart
goldens, debug interaction/navigation replays, and the Lambda baseline.

A debug first-frame diagnostic contained about 45 MB of output. More than
250,000 records incorrectly described ordinary arrays/ranges as unsupported
sources: generic iteration asks `item_keys` for a name-key list even when the
source has only positions. The runtime now recognizes those supported sources
and returns no name keys without the misleading diagnostic (**S8.1.1v2,
S8.1.3v2**). No logging configuration or severity threshold changes.

Runtime `is` checks also invoked the reporting validator, including constructing
error trees and correction suggestions that a Boolean caller discards. They
now use its existing predicate mode and preserve the caller's previous mode.
Generic map/element kind checks reuse `lambda_type_matches` directly; these
compact types have no shape to inspect (**D3.2.1**). Explicit validation retains
its reporting path, and failed virtual reads still propagate as errors.

The first baseline exposed two pre-existing discrepancies in predicate mode:
numeric literal aliases took the numeric-carrier shortcut, and positional
array patterns inferred success from an empty error list after merging a
failed predicate verdict. Literal aliases now retain their value check, and
sequence patterns retain their merged validity bit instead of inferring it
from the number of diagnostic messages
(**S11.1.1v3, S11.1.6v3, S11.2.1**). The unchanged
`ledger_literal_contracts` and `element_content_pattern` goldens exercise these
cases, including all execution tiers through the parity suite.

The chart package removes further repeated work within one pure render:

- A static interactive chart reuses the behavior plan already computed for its
  controls instead of compiling the same behaviors again to render the image.
- Path/warp projection captures invariant settings once in a local callback.
  Subdivision points reuse that callback; each render/model builds its own,
  including direct-polar and custom projections.
- Flatness probes compute their two coordinate differences directly rather than
  constructing temporary interpolated arrays. Precision and recursion limits
  are unchanged.

This reuse follows **S12.1.1v3**; applied-view ownership remains **S12.1.3 /
D4.5.3v2**. Diagnostic changes do not affect semantics (**D5.4.4**).

### Qualification

Six interleaved release before/after pairs include one warm-up pair. The table
uses medians from the remaining five pairs, frozen package/demo snapshots and
pinned release executables. Each navigation run clicks the normal-page link
from the collection index, asserts all 44 chart SVGs, captures the page and
closes: **48/48 assertions across 12 runs**. Capture time is measured externally
from process start, including index load, navigation, layout and PNG capture;
it is not the sum of event-handler timings, which omit asynchronous loading.

| Metric | Before median | After median | Median improvement within each pair |
| --- | ---: | ---: | ---: |
| Document generation | 954.2 ms | 875.0 ms | 12.3% |
| Layout | 369.4 ms | 373.4 ms | -1.3% |
| Through first render | 1,302.8 ms | 1,283.7 ms | 8.8% |
| Index → normal page capture, including process startup | 1,636.9 ms | 1,616.0 ms | 12.2% |
| Complete navigation replay process | 1,686.3 ms | 1,720.0 ms | 10.1% |

The last column is the median of five matched before/after percentage changes,
not a percentage derived from the two independent medians. Shared-machine
load varies across the run: generation improves in every pair (**4.7–30.8%**),
and capture improves in four of five (**-1.9–15.5%**). The layout phase has no
consistent improvement. A separate package-only comparison using one release
executable measures **733.6 → 661.2 ms** generation (three measured interleaved
pairs after warm-up). Earlier trials are retained as `benchmark-initial.json`
and `benchmark-pre-validation-fix.json`; the final table uses the executable
containing the validator correctness fixes. No debug performance measurements
were used.

The initial baseline was **8,428/8,431** before the two predicate-mode fixes
above. The corrected parallel run was **8,430/8,431**: all semantic regressions
passed, but the Procart cross-tier test received empty output once from its
`extensions.ls` JIT child. Its complete test passes on isolated rerun, as do
five direct JIT executions with checked exit status and exact golden output.
The same baseline selection passes serially: **8,431/8,431**, exit 0
(**6,319** Lambda checks plus the unchanged passing **2,112** input-parser
checks). The standard runner uses `--sequential` with
`LAMBDA_TEST_MAX_CONCURRENT=1`; no cases are removed from the Make target's
selection.

`make test-validator` passes **101** checks across its three runners; six cases
skip and 87 are already disabled in the existing validator harness. The three
previously failing literal/structural/parity cases pass their focused rerun.

Final debug qualification:

- First-frame checks find all **44** dashboard SVGs on both versions. With the
  same default DEBUG configuration, first-frame diagnostic output decreases
  from **45,273,329 → 24,038,212 bytes** (**46.9%**), and record count from
  **610,731 → 290,798** (**52.4%**). All **262,109** unsupported-key records are
  removed. These are diagnostic counts, not debug performance measurements.
- Desktop eager replay **23/23**, 390 px mobile replay **24/24**, and interaction
  replay **26/26** pass, with no runtime errors. The replays cover independent
  selection, playback, brushing/filter input, pan/zoom, scrolling and breadcrumb
  return/reopen. Desktop/mobile captures retain centered charts and their
  three-column/one-column layouts.
- The extended startup/projection regression matches its unchanged `[]` golden
  on both `interp` and `jit`, including direct-polar, custom and transformed
  projections and separate model captures. Existing dashboard goldens are
  unchanged. The native allocation/mode-restoration test passes in the baseline.
- Scoped `git diff --check` passes. The checkout is left with the rebuilt debug
  executable; a running viewer must be restarted to load the native fixes.

Artifacts are under
`temp/chart-startup-next/`, including `bench-final.py`, `benchmark-final.json`,
the isolated sources, pinned executables and per-run logs.
