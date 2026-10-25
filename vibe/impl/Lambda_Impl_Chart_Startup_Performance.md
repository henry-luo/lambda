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
