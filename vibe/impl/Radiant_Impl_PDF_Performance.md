# PDF visual-suite runtime

Date: 2026-10-19. Scope: preserve the PDF visual/export suite while reducing
its 184-second optimized runtime toward 40 seconds. PDF generation remains in
the pure package (**S2.6.6v3**); retained resources and evaluator lifetime follow
**D4.5.1v4 / D4.1.4v5 / D5.3.3 / D5.4.1**.

## Milestone and diagnosis

Before this change, the queue runs four isolated case workers, but each GTest case starts
a fresh harness and every render starts a fresh CLI. It does not retain PDF
modules across output jobs. The full passing receipt has 314 cases and 688.84
seconds of summed case wall time; its longest case is the 60-second gradient
marker matrix. A release probe records medians of 35.86 ms PNG, 29.37 ms SVG,
49.27 ms PDF, 14.22 ms Poppler Cairo and 12.68 ms librsvg. Existing PNG batching
takes 547.40 ms for 30 jobs versus 57.16 ms for one matching fixed-viewport
single job. Existing batch and CLI defaults differ, so the batch adapter must
use the ordinary export session rather than reproduce its layout logic.

Plan:

1. Reuse the ordinary export session in format-aware batch jobs. Keep source
   loading, media, viewport, density, scripts, clipping and teardown equivalent
   to the corresponding single-file command. Retain only host/module state
   whose existing owner remains live; never retain Items across heap retirement.
2. Drive repeated export comparisons through persistent batch workers. Preserve
   every comparison, density, context mode, pixel assertion and error result.
   Keep ordinary CLI controls to detect batch-specific state leakage.
3. Profile remaining PDF encoding and independent rasterizer work; optimize
   measured shared causes, then measure the full suite with frozen release
   binaries and a fixed worker budget.

Verification budget: representative batch/single output comparisons and worker
contracts during implementation; the complete PDF suite and required Radiant
gate once the final engine changes stabilize. Package changes additionally run
the PDF goldens in interpreter and JIT. Record raw failures separately and do
not replace aggregate results with isolated controls.

Evidence: `temp/pdf_perf_profile/profile.json` and
`temp/pdf_perf_profile/temp/phase_profile.txt`.

## Implemented changes and focused controls

`render-batch --export` uses `render_export_session_begin_internal` and the
ordinary PNG/SVG/PDF encoders. The session explicitly distinguishes its owned
UI context from a borrowed batch host. Every job retains its format's viewport,
media, scaling and script behavior; the legacy PNG-only batch protocol remains.
Framed replies tolerate script stdout and preserve failed-job/process status.
Document destruction owns its JS realm teardown. The former ambient JS reset
erased the retained loader's current Lambda module slots, preventing safe PDF
module reuse; it is removed from batch cleanup.

The PDF package borrows the UI's existing loader runtime when no evaluator is
active. A newly reserved evaluator initializes its heap through the normal
package loader; live evaluator identity remains unchanged (**D5.4.1**).
POSIX test cases retain a render process for their output jobs, including
imported PDF page scripts. CLI option/rejection checks remain independent
commands. Windows keeps ordinary command execution. A new control compares
both routes through document replacement, format/media changes and 1×/2×
density, verifies visible paint, and checks failure followed by recovery.

Warm repeated Unicode-text PDF export measures 7.97 ms. Independently extracting
its embedded 384,320-byte font and deflating it with the same default setting
takes 6.29 ms. Compression now reuses the existing LRU implementation in an
optional evaluator-owned capsule: at most 64 entries / 8 MiB, including source
and compressed bytes. Native copies own all stored bytes; hashes locate an
entry and a full byte comparison proves identity. No cached Item or GC address
survives heap retirement (**D5.3.3 / D5.4.2–D5.4.4**). Uncached operation and
compression settings remain the same. Warm Unicode export falls to **1.28 ms**;
the decorated-text probe falls **23.53 → 4.58 ms**, with unchanged output sizes.
The source-key/reuse golden passes with collection every 17 allocations and
freed-memory poisoning.

The two large bundled matrices become 53 FO/native fixture cases and 25 gradient
kind cases. Every original physical page, source pair, effect and density still
runs. The shared queue now defaults to eight CPU-capped workers, configurable
with `LAMBDA_PDF_TEST_JOBS`. GTest cases retain process isolation; module reuse
is within each case's render-job batch.

The remaining reader-process startup cost is removed where the installed
Poppler GLib/Cairo and librsvg libraries are available. Optional in-process
adapters use these independent engines through their public C interfaces;
missing libraries retain the CLI route. A reader failure remains a failure.
The direct-library/CLI control checks exact decoded pixels for text, Unicode,
gradients, opacity, rounded clipping, transforms and two densities. Across the
saved full-suite outputs, **6,085 comparisons / 1,247,121,390 pixels** from
identical source inputs have **zero differences**. Another 29 candidate pairs
have different or missing inputs and are excluded from that equivalence audit.

## Release measurements

Frozen optimized host/harness binaries run through the existing shared queue:

| Implementation | Workers | Passed / total | Wall time |
|---|---:|---:|---:|
| Merged tree before this change | 4 | 314 / 314 | 179.60 s |
| Persistent export jobs | 4 | 315 / 315 | 90.82 s |
| Compression reuse and split matrices | 8 | 391 / 391 | 58.91 s |
| In-process independent readers, no saved scheduling timings | 8 | 392 / 392 | 46.13 s |
| Same binaries, repeat with saved scheduling timings | 8 | 392 / 392 | **39.23 s** |

Every full PDF run has zero skips. The 314 original scenarios remain: two
bundled cases become 53 and 25 parameterized cases, and two independent
controls are added. No golden, tolerance, assertion or fixture is relaxed.
The existing duration-based scheduler starts the long PDF-import comparison
earlier on repeats. That case still takes 24.47 seconds and is the next
significant bottleneck. Its generated script pages still own an evaluator per
document, so the batch saves process/UI startup without sharing those script
module instances. PDF-export package modules do remain cached by the UI owner.
The approximately 40-second result applies to this repeat run, not a guaranteed
first-run bound. OS caches were not flushed, and
these are paired observations on macOS ARM64 rather than statistical estimates.
Summed child CPU falls from 610.61 to 295.01 seconds despite doubling the worker
limit; the gain includes less work as well as better overlap.

Receipts: `temp/pdf_perf_{before,batch4,after,readers8}_timing.json`,
`temp/pdf_perf_readers8_cold_timing.json`,
`temp/pdf_perf_profile/warm_profile{,_cache}.json` and
`temp/pdf_perf_oracle_equivalence.json`. Binaries, package snapshots and
per-case outputs remain under `temp/pdf_perf_*`; timing receipts include the
exact host and harness SHA-256 hashes.

## Qualification

- All **97 PDF package goldens** pass in both interpreter and JIT.
- All **59 shared runner contracts**, the Radiant integer-cast lint and the
  module boundary check pass (134 deferred imports against baseline 165).
- Scoped AddressSanitizer checks pass for the compression-reuse golden in
  both backends and **72 mixed PNG/SVG/PDF jobs**, with precise collection every
  17 allocations and freed-memory poisoning. Leak detection is disabled;
  these checks make no leak-free or exhaustive sanitizer claim.
- The required Radiant aggregate records 5,895 passed, 157 accepted partial,
  one skipped and **one failed** across 6,054 results. All **500 paged-media
  cases** pass; layout reports 3,141 passed / 153 accepted partial with no
  regressions. The sole failure is the previously observed 15-second timeout
  in `PageLoad/PageLoadTest.LoadWithoutCrash/bootstrap_5_kitchen_sink_`.
  Three isolated executions of its exact page command, with the same aggregate
  host and unchanged 15-second deadline, pass in **3.63 / 3.51 / 3.66 seconds**.
  They supplement the raw aggregate failure; they do not replace it.

Qualification receipts: `temp/pdf_perf_qualification.json`,
`temp/pdf_perf_goldens_{interp,jit}.xml`, `temp/pdf_perf_asan_checks.json`,
`temp/pdf_perf_radiant_results/test_summary.json` and
`temp/pdf_perf_bootstrap_isolated.json`.

## PDF-import comparison milestone

The next milestone removes the corpus case's serial tail without changing
PDF interpretation or the independent Splash oracle. An isolated optimized
receipt compares 44 pages from 14 PDFs in 15.09 seconds; the same case took
24.47 seconds alongside the full suite. Phase profiling shows imported ASTs
already reused across script runtimes, so module recompilation is not the
dominant cause. Each fixture and reference page nevertheless runs serially.

Each discovered PDF now registers a separate case in the existing bounded
queue. Its page jobs retain their batch process, and one Poppler invocation
rasterizes all selected reference pages. The source page limit, pixel delta
threshold, mismatch budgets and independent Splash renderer remain unchanged.
Baseline readers share a file lock; partial writers read, merge and replace
under an exclusive lock, preserving other fixtures. Explicit `--update-baseline`
retains the single whole-corpus case and its regression check before writing.

The compiler's default eight-thread pool previously multiplied across case
workers. The harness now defaults `LAMBDA_MODULE_AST_THREADS` to the inherited
case CPU budget; an explicit user setting remains authoritative. This changes
the test environment only. Production sources and the optimized host hash are
identical to the preceding qualification (**D8.1.1v13 / D8.5.1v7**).

Final observations with eight case workers and frozen optimized binaries:

| Check | Result |
|---|---|
| Original isolated corpus case, with phase profiling | 44 pages in 15.09 s |
| Final import selection, including concurrent-baseline control | 15 / 15 passed in **4.88 s** |
| Longest import fixture during the full repeat run | **6.51 s**, previously one 24.47 s bundled case |
| Full suite without saved scheduling timings | **406 / 406 passed, 42.36 s**, zero skips |
| Full repeat suite with saved scheduling timings | **406 / 406 passed, 41.50 s**, zero skips |

The serial import tail is removed. The longest remaining full-suite case is
the paged row-span comparison at 10.50 seconds. These measurements establish
the shorter import selection; they do not establish an overall repeat-suite
speedup against the earlier 39.23-second observation. A split-only intermediate
also passed the full suite in 39.06 seconds without saved scheduling timings.
The recorded runs retain their load and CPU measurements; OS caches were not
flushed and no statistical runtime bound is claimed.

All **44 original pages** retain exact reference, Lambda and diff pixels:
**132 images / 62,308,800 pixels**, zero differences. The eight-process baseline
control preserves the seed and every fixture. Private-corpus controls also
verify whole-corpus refresh, refusal to write after a regression, and automatic
addition of a new fixture without changing existing budgets. The companion
baseline remains byte-identical. All **59 runner contracts** pass. The 406-case
inventory is the prior 392 cases with its bundled import case replaced by 14
fixture cases and one concurrent-baseline control; every other identity remains.

Receipts: `temp/pdf_import_qualification.json`,
`temp/pdf_import_{before_profile,final_cold_timing,final_warm_timing,final_focus_timing}.json`,
`temp/pdf_import_pixel_equivalence.json`,
`temp/pdf_import_baseline_controls.json` and
`temp/pdf_import_runner_contracts.log`. Prior engine, Radiant, package and
sanitizer qualification remains applicable because production code is unchanged;
the previously recorded aggregate Bootstrap timeout remains a qualification
limit. Windows baseline locking is implemented but not runtime-qualified here.

## Coverage audit and 15-second milestone

Audit the 406 cases by behavior and retained assertions, removing only proven
duplicate work. Acceptance keeps every distinct input family, backend-specific
structural check, independent pixel control and negative result. The immediate
causes are unused legacy captures before four decoration matrices, a remaining
per-image SVG CLI path, repeated per-page Poppler loading in paged comparisons,
and oversized blank canvases around small fixed-geometry fixtures. Narrow only
fixtures without viewport-dependent behavior, then compare retained pixels and
verify the excluded old columns contain no distinct paint. Imported PDF pages,
paged sheet geometry, device densities and comparison thresholds stay fixed.

Verification budget: focused affected families while editing, exact image
audits against the frozen 406-case host/harness, then the complete release gate
and runner contracts. Cross-case batching is an experiment rather than an
acceptance shortcut: its first probe takes about 36 seconds and exposes the
WebGL fixture's process-environment boundary, so it does not justify changing
case isolation by itself. Production engine changes, if measurements require
them, also require the Radiant and relevant lifetime gates.


### Audit outcome and retained coverage

The 406-case inventory contains 307 export parity cases, three dedicated PDF
visual controls, 14 import fixtures, 53 FO/native fixtures, 25 gradient marker
variants and four WebGL context modes. A source-body audit finds no identical
normalized case bodies. Similar family names generally distinguish native
raster paint from vector lowering, parser/computed-value behavior from physical
page geometry, or different resource/lifetime contracts; deleting those cases
would remove distinct coverage.

One pair is consolidated: `SvgExportCssFilterUsesRasterFallbackImage` and
`PdfExportCssFilterUsesRasterFallbackImage` use the same grayscale fixture and
viewport. `CssFilterExportsUseRasterFallbackImages` retains both SVG structural
markers and the PDF image extent. Four decoration cases lose eight unused
legacy PNG/SVG captures; their subsequent three-format geometry comparisons
remain. The count becomes 405 before adding
`PdfReferenceCacheTracksBytesAndDensity`, which checks byte identity across
paths, replacement at the same path, both densities, eviction, corrupt/missing
sources and a destination write failure. The final inventory remains 406,
with every other original case identity retained.

Twelve fixed-geometry fixture families now declare a 96-, 128- or 160-pixel root
width. These fixtures do not use viewport-relative units or responsive query
conditions. The audit compares 3,428 resized image pairs: all 114,734,080
retained-area pixels are exact, and the excluded old columns contain only
background/padding. Another 3,805 unchanged-image comparisons retain exact RGBA
across 702,549,390 pixels. All 44 imported pages retain exact reference, actual
and diff pixels: 132 images / 62,308,800 pixels. Device densities, source/effect
matrices, physical sheets and mismatch budgets are unchanged.

### Repeated work removed

Viewport-only fixture options now use the existing batch protocol's dimensions;
previously `-vw`/`-vh` forced a fresh process and PDF package load per export.
The ordinary CLI/batch control now covers explicit dimensions as well as
format, media, source replacement and density. Unsupported option forms keep
the CLI route and its integer argument semantics.

The shared runner supports opt-in `case_batch_size`/`case_batch_size_env`.
The PDF gate balances batches of at most 16 cases across eight CPU-capped
workers. This supersedes the preceding milestone's process-per-case choice.
The exported documents and their JS realms still retire after each render job;
only loader/module caches span jobs (**D5.4.1**). The batch client restarts its
child when the presence of `LAMBDA_HEADLESS_GLFW_WINDOW` changes, matching the
UI host's initialization contract. `LAMBDA_PDF_TEST_BATCH_SIZE=1` restores
process isolation for diagnosis. Other suites retain their one-case default.

Each assigned case must appear exactly once in a valid GTest report. Assertion
failures remain attached to their cases; crashes, timeouts, unexpected exits,
missing/duplicate/extra cases and invalid reports fail the affected batch.
The merged report retains discovery order and each case's workspace. All 74
runner contracts pass, including 15 new batching/result-integrity controls.

The independent Poppler reader spent substantial CPU decompressing embedded
fonts repeatedly. Generated self-contained PDF exports now reuse rasterized
results by complete source bytes and density. The existing LRU holds at most
32 entries / 32 MiB of source/PNG payload and entry data; hashes only locate
candidates, followed by full byte comparison. Export generation always runs.
`LAMBDA_PDF_RASTER_CACHE=0` disables raster reuse. SVG resources remain uncached.
Identical encoded PNGs require one successful decode before equality can pass.
Both the harness and independent readers use the shared fast lossless PNG
writer for diagnostics, avoiding adaptive filter searches; Cairo ARGB32 pixels
are converted with the existing straight-alpha helper. CLI-reader equivalence
and the image audits retain exact RGBA, including transparency.

Performance receipts and final qualification follow below. The 15-second full
suite target remains open; the measured improvements do not establish it.


### Release observations and qualification

All observations use macOS on the ten-core Apple M4 and the frozen optimized
host `1ee7b4acd04fdfc5e5d5bed256983423895dff4c86b90e935bed61fb8b16c4e6`.
The final optimized harness is
`b393adbe29d819a2777cbbd2d748e2361341b75c75568d3132c6bf94b410696f`.
No production engine code changes belong to this audit milestone; the preceding
engine qualification remains evidence for that frozen host, not for a future
renderer rebuild.

| Observation | Result |
|---|---|
| Previous 406-case qualification, repeat | 406 passed, **41.50 s** |
| 64-case batches, no saved timings / repeat | 406 passed, **35.22 / 26.96 s** |
| 16-case batch trial, no saved timings | 406 passed, **27.45 s** |
| Selected 16-case default, first run during a competing compile | 406 passed, **32.46 s** |
| Selected default, repeat after that compile finished | 406 passed, **28.06 s**, zero skips |
| Nine-worker trial, 16-case batches | 406 passed, **29.73 s**; retain eight workers |

The smaller batches bound first-run scheduling imbalance while retaining module
reuse. The selected repeat reduces elapsed time by about 32% against the prior
repeat. These are observations, not a runtime bound: load is recorded in each
receipt, OS caches were not flushed, and the competing compiler was observed
using approximately eight CPU cores during the slower first run.

A separate diagnostic PNG read-all experiment passed all 406 cases in 27.35 s,
but used 208.53 descendant CPU seconds against the selected repeat's 202.20;
it demonstrated no CPU saving and was reverted. Its first attempt stopped with
ENOSPC before producing a suite result. Cleaning 72 intermediate generated
workspaces freed about 4.3 GiB; final resized-image audit artifacts and the old
reference workspaces remain available. The failed attempt is not a passing
qualification receipt.

All 74 runner contracts pass. A separately ASan-instrumented parent harness
passes 24 shuffled executions covering cache identity, density and eviction,
reader/CLI equivalence, transparent paint, document replacement, and GPU-host
environment transitions. Static dependencies and the frozen release child are
not instrumented in that parent check; leak detection is disabled for retained
system-library state. Earlier production-child sanitizer evidence is recorded
in the preceding milestone. The companion PDF baseline remains byte-identical
(`ced461ca27c7a5455b8b450f8a87b71f02e5d51555d3d2cb94ce5919f6880d64`).

**The 15-second target is not met.** The selected run still consumes 202.20
CPU seconds, exceeding the ideal 150 CPU seconds that ten logical CPUs could
supply in 15 seconds. Further reductions require removing actual conversion or
reader work as well as balancing scheduling. The remaining identified costs
include repeated embedded-font decoding in byte-different PDF exports, fresh
process/module initialization for paged-option exports, and the retained
Canvas/WebGL output matrices. No additional duplicate case bodies were proven;
those distinct inputs and assertions remain covered.

Receipts: `temp/pdf15/qualification.json`, `coverage_audit.json`,
`case_identity_equivalence.json`, `narrowed_pixel_equivalence.json`,
`unchanged_pixel_equivalence.json`, `import_pixel_equivalence.json`,
`delivery_timing.json`, `delivery_contended_timing.json`, `chunk16_timing.json`,
`nine_timing.json`, `runner_contracts.json`, `asan_receipt.json` and
`cleanup_receipt.json`, all under `temp/pdf15/` unless already fully qualified.
