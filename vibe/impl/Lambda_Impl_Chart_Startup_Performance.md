# Chart gallery startup performance

## Scope and acceptance

Reduce startup work for `./lambda.exe demo chart` → Normal charts while retaining
all 44 dashboard examples, three columns on desktop, centered charts, one column
on mobile, and the existing playback, selection, filtering and navigation.
Preserve the existing dashboard SVG golden and curve precision. Qualify with the
chart golden suite, native collection replays, the Lambda baseline, and paired
release first-frame measurements.

## Diagnosis and implementation

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

## Qualification

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

Remaining startup time includes generating every chart, loading source modules,
layout, and the initial animated examples. The gallery still renders all 44
examples up front; this change introduces no pagination or deferred charts.
