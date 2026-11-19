# Lambda AUTO performance

## Current checkpoint

The goal is at least 20% lower geometric-mean AUTO test run time. The control
is Lambda `b03183425` on macOS arm64. Lambda starts in T0 and queues native
promotion under D8.3.1v3, D8.3.2 and D8.3.4.

- The fixed timing set has 40 rows. It includes the prior unification plan's
  20 benchmarks, typed and untyped Mandelbrot, matrix multiply, Levenshtein
  and point-in-polygon, and 12 existing golden tests. The golden rows cover
  calls, effects, wide scalars, type binders, promotion, range loops, N-D
  indexing, charts, LaTeX, DOCX, Procart and Scene3D.
- `temp/lambda-auto-tuning/manifest.json` (SHA-256
  `f138de0f6d1c0d47101b42789dd6f7a22aa67c8faf6839a0bb7caf8b7b2c5428`)
  records paths, invocation kinds and source and golden hashes. It was
  frozen before the first control timing.
- Use fresh release processes with the shipped AUTO policy and alternate
  control and candidate within each pair. Every row and sample must match
  stdout byte for byte after removing benchmark timing lines. Golden tests
  additionally check their committed expected output. Primary time is
  elapsed process time, including compile, execution and cleanup. Compute
  the geometric mean of per-row median ratios and report benchmark execution
  time and per-row spread alongside it.
- The first control build exposed missing DOM declarations in the recorded
  owner revisions. Both arms build against isolated copies of Input
  `fe99623` and Radiant `494680e`, with clean first-party host objects
  (D9.1.1v4, D9.1.3v2). The checked-out submodules are preserved.
- The pinned control SHA-256 is
  `c45ecf353e3da7708d18cc6efcfedbe9e523344d23fdd6b92099212a0edeec5a`.
  The 120 A/A pairs matched exactly, with a wall-time ratio of 1.003.
  Records and full build logs are under `temp/lambda-auto-tuning/`.
- The first queue change matched all 120 pairs. Caching thresholds and
  skipping the empty publication lock reduced benchmark execution by 1.1%.
  The full-set wall-time ratio was 0.998.
- Lowering the loop threshold from 10,000 to 1,024 on the control reduced
  wall time by 5.3% across 18 diagnostic rows. At 256, Levenshtein promoted
  its initialization loop and took roughly twice as long. Synchronous
  publication increased wall time by 12.4% across those rows (D8.3.2, D8.3.4).
- Background clusters with private direct calls matched all 120 pairs and
  reduced full-set wall time by 3.9%. The two Prettier rows and untyped
  matrix multiply account for most of the gain. DeltaBlue regressed by
  60–78% and emitted 94–96 bodies versus 49–50 on the control. The cluster
  needs a budget that accounts for duplicate compilation (D8.3.3).
- Guarded scalar inputs lowered untyped Mandelbrot from about 206 ms to
  26 ms. The interpreter verifies current inputs before handoff. MVP uses
  their types as initial facts and widens over subsequent stores under
  D4.3.2 and S12.2.1v3. Only the type and lane identifiers reach the worker.
- Keeping flat packed array domains and including earlier-promoted peers
  in loop images brought Levenshtein down to 24 ms untyped and 16 ms typed.
  The first full pilot matched 120 pairs. Its wall-time ratio was 0.782
  and its benchmark execution ratio was 0.614. A first-launch outlier
  widened the wall interval. DeltaBlue still took 32–46% longer.
- The candidate uses a default of 1,024 loop back edges, with up to three
  definitions per loop image and two per call image. The evaluator freezes
  the cluster and checks inputs at the loop head (D8.3.2–D8.3.4). Ordinary
  function values still come from the live module slot (S5.5.1v2). Packed
  array observations remain private to the binding and widen over writes
  and rebinds (D4.3.3, S12.2.1v3).
- The pinned final release comparison met the timing target. The
  deterministic handoff fixture, strict T0, Lambda baseline and Test262
  all passed.

## Final release timing

Nine measured pairs per workload and one warmup pair matched exact output
in both arms. Each process ran AUTO on macOS arm64. The full wall-time
geometric mean fell from **47.77 ms to 36.78 ms**, a **23.0% reduction**
(95% paired bootstrap interval **22.7–23.5%**). The sum of per-workload
medians fell from 8.96 s to 7.50 s (16.2%).

| Workload set | Rows | Exact measured pairs | Change in wall time | Change in script-reported time |
|---|---:|---:|---:|---:|
| Full set | 40 | 360 | −23.0% | — |
| Benchmarks | 28 | 252 | −31.3% | −40.4% |
| Golden fixtures | 12 | 108 | +0.5% | — |

| Workload | Control | Candidate | Change |
|---|---:|---:|---:|
| Untyped Mandelbrot | 206.35 ms | 25.64 ms | −87.6% |
| Typed Levenshtein | 98.72 ms | 16.41 ms | −83.4% |
| Untyped Levenshtein | 118.81 ms | 23.32 ms | −80.4% |
| Untyped matrix multiply | 132.31 ms | 36.62 ms | −72.3% |
| Untyped point-in-polygon | 67.98 ms | 29.61 ms | −56.4% |
| Untyped DeltaBlue | 254.12 ms | 280.34 ms | +10.3% |
| Typed DeltaBlue | 198.97 ms | 249.42 ms | +25.4% |

The final binary SHA-256 is
`dde5bbc2cc5c2d67cf4b00788a479d1b1101f7748d1da95e9544e8c93e557c28`.
`temp/lambda-auto-tuning/final.json` contains pair order, run times, output
hashes and intervals for all rows. `final.log` contains the row medians.
The host export audit passed for the pinned release binary.

## Correctness checks

- The forced-GC fixture matched in `interp`, `auto`, and `jit`. It covers a
  later loop input of a different type, assignments after handoff and a
  previously shared array (S12.2.1v3, S9.1.2). Debug traces show both
  `carry__loop` and `replace_inputs__loop` entered native code.
- The first aggregate completed 6,118 of 6,119 checks. The DOM failure
  paired a test object built against the checkout's older Input headers
  with the isolated owner archives. The focused DOM suite passed 84/84
  after rebuilding against the pinned headers. The clean Lambda baseline
  passed **6,119/6,119** after rebuilding the affected test objects.
- `make test-lambda-interp` passed **1,612/1,612**, including the 1,555
  committed golden cases that must execute without a tier fallback.
- The JavaScript exception catalog gate passed. The Test262 baseline passed
  **40,261/40,261** against the pinned release binary, with zero regressions.
