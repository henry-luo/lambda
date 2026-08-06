# JS Runtime Helper Profiling — Call Frequency and Time Share

**Date**: 2026-08-07  **Status**: MEASURED (findings only; no optimization landed)
**Tree anchor**: master `8585c6ffd` + uncommitted instrumentation (§6)

**Workload**: `test_js_gtest` — 327 tests, 261 batch-eligible `.js` fixtures
under `test/js/` and `test/js/props/` (real browser libraries: CodeMirror,
Tabulator, jQuery, Bootstrap, Alpine, GSAP, Tom Select, plus the language
fixture set).

Primary sources:

- Profiler: `lambda/js/js_exec_profile.{h,cpp}`
- Emitter hook: `lambda/runtime/mir_emitter_shared.hpp` (`em_emit_helper_call_count`)
- Helper call sites: `em_call_with_args` / `em_call_void_with_args`
- Profile artifacts:
  - `temp/js_exec_profile_*.tsv` (72 files) — call counts, full 327-test suite
  - `temp/batch_matched.tsv` — call counts, 261-script batch (matches the sample)
  - `temp/samples/suite_batch.txt` — 1ms `sample` of the same 261-script batch
  - `temp/suite_batch_analysis.txt` — bucket/helper attribution of the above

---

## 0. Why this pass exists

`js_exec_profile` already counted **emitted MIR call sites** per helper
(`# MIR runtime call sites by helper`). Static site counts do not rank
optimization targets: a helper emitted at 340k sites in cold module preamble
code and a helper emitted at 133 sites inside a hot loop are indistinguishable
until execution is counted. Two questions needed dynamic data:

1. What fraction of execution time is spent inside runtime helpers versus
   everything else (JIT-generated code, compilation, GC, allocator)?
2. Which helpers are called most often?

D7.4.3 already requires that "every callable runtime helper carries catalog
metadata"; this pass adds the execution-side counterpart — what those catalog
rows actually cost at run time.

## 1. Method

### 1.1 Dynamic per-helper call counts

Every JS runtime helper reaches MIR through exactly two emitter entry points
(`em_call_with_args`, `em_call_void_with_args`), both of which resolve imports
through `em_ensure_import` and announce via `em->note_mir_call`. A parallel
hook, `em->helper_call_counter`, returns a process-global `uint64_t*` slot per
helper name; when set, the emitter emits an inline `(*slot)++` (load / add /
store on an absolute address) immediately ahead of the call.

Properties that make this measurement trustworthy:

- **Exact, not sampled.** Every executed import call is counted.
- **No GC or safepoint interaction.** The increment is three plain integer
  instructions on a non-GC global; it is emitted before
  `em_before_resolved_call`, so root-slot bookkeeping and the `MAY_GC` call-site
  record are untouched (D5.3.1, D5.3.2 preserved).
- **Opt-in.** The hook is installed only under `LAMBDA_JS_EXEC_PROFILE` and only
  when `JS_EXEC_PROFILE` is truthy at emission time; ordinary debug and release
  builds emit nothing.

Counts are emitted into the existing TSV under a new section,
`# JS runtime helper dynamic calls from JIT code`.

### 1.2 Time share

Counting cannot answer question 1 — a 36M-call helper with a two-instruction
body may cost less than a 100k-call helper that walks a prototype chain. Time
share was measured separately, with no counters emitted (an uninstrumented
profile build), using macOS `sample` at 1ms over a single-process run of all
261 batch scripts:

```
./lambda-debug-profile.exe js-test-batch --timeout=60 < manifest
```

Attribution rule for the call graph: walking each stack top-down, the first
**runtime-helper** frame owns its self samples; a JIT frame (`???` / unknown
binary) sets the owner to `jit`. Plain C frames appearing *directly* under a JIT
frame are counted as helper time in a separate sub-bucket — at `-O3` the helper
frame is elided by tail calls, so e.g. `js_property_access_named_ic` appears in
the graph as its callee `js_map_get_fast`. Compile machinery is classified by
source file (`mir-gen.c`, `js_mir_*.cpp`, `parser.c`, …), which is more reliable
than symbol-prefix matching for the MIR archive.

### 1.3 Build and caveats

Measured on `debug_profile` (`-O3` + symbols + frame pointers +
`LAMBDA_JS_EXEC_PROFILE`), the configuration that exists for this purpose.
Consequences to keep in mind when reading the tables:

- `js_debug_assert_exception_clear` and `js_debug_check_callee` are emitted
  under `#ifndef NDEBUG` only (`js_mir_completion.cpp:177`,
  `js_mir_expression_lowering.cpp:106`) — they vanish in release.
- The 4.4% `logging` bucket is a debug-build artifact; `log_debug`/`log_info`
  compile out under `NDEBUG`.
- Counts cover **import-path calls emitted from JIT code**. Helper-to-helper C
  calls and IC fast paths reached without an emitted call are not counted
  separately; they appear in the time profile as the tail-called sub-bucket.
  This is why §2.1 reports a JIT-caller share per helper and withholds
  `ns/call` where that share is low — several of the most expensive helpers are
  reached almost exclusively from C, so their execution count is *not* their
  emitted-call count.
- **§2.1's two columns come from two runs of the same 261-script manifest** —
  time from an uninstrumented sample, calls from an instrumented one. The §3
  frequency table instead covers the full 327-test suite (72 processes), so its
  absolute counts are larger. Ratios are quoted within a run, never across.
- Frame-pointer-based sampling under `-O3`: tail-called frames are elided, which
  §1.2's sub-bucket handles explicitly rather than silently dropping.
- Suite wall time: 67s uninstrumented, 123s with counters emitted — the counter
  overhead is real and is why time share is measured on a separate run.

Correctness gate: the full suite passes 327/327 with the instrumented binary,
and instrumented script output is byte-identical to the goldens.

## 2. Result 1 — helpers are ~44% of working CPU

Sampled batch run: 195,046 total samples, 162,608 in parked/idle threads,
**32,438 samples of actual work**.

| Bucket | Samples | % of work |
|---|---:|---:|
| **Runtime helpers (intact frames)** | 8,631 | **26.6%** |
| **Runtime helpers (tail-called, frame elided)** | 5,576 | **17.2%** |
| Compile: parse → AST → lowering → MIR codegen | 10,223 | 31.5% |
| System libs (allocator/memcpy under compile) | 2,787 | 8.6% |
| Other runtime C (boot, module init, name pool) | 2,178 | 6.7% |
| Logging (debug-build only) | 1,430 | 4.4% |
| dyld lazy link / symbolication | 1,121 | 3.5% |
| Lazy codegen during execution | 392 | 1.2% |
| **JIT-generated code (self)** | 100 | **0.3%** |
| **Helpers, total** | **14,207** | **43.8%** |

Two readings matter more than the headline number:

- **JIT-generated code accounts for 0.3% of working time.** Generated code is
  almost pure glue between helper calls; essentially all semantic work happens
  in C. Improving the *quality* of generated instruction sequences has close to
  no headroom on this workload. Note this also bounds caller-side call overhead
  (spills, reload, call sequence) at the same 0.3% — so the headroom is inside
  the helper *bodies*, not in the calls or the code around them. §2.1 and §4.1
  develop this; it is the single most important number in this document.
- **Compilation is a third of the run.** This workload is compile-heavy (261
  scripts, several of them large minified libraries, each compiled once and run
  briefly), so the ratio is workload-specific and would fall sharply on a
  long-running script. It is not evidence of a codegen problem; it is the shape
  of a test suite.

### 2.1 Top 20 helpers by run time

Attribution per §1.2: a helper's samples are its own frame plus any non-helper C
callees reached under it, excluding nested helpers and anything under a JIT
frame it calls back into. Percentages are of the 32,438 working samples and of
the 14,207-sample helper total. `JIT %` is the share of this helper's sampled
frames whose immediate parent is JIT-generated code, measured from the same
call graph — the rest are reached from C.

Call counts come from an instrumented run of the **same** 261-script manifest
(139,539,070 helper calls), so the two columns describe one workload. `ns/call`
is shown only where the JIT is the majority caller; where a helper is reached
chiefly from C, dividing its time by JIT-emitted calls has no meaning and the
cell is `—`.

| # | Helper | Samples | % work | % helper | JIT % | JIT calls | ns/call |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | `js_get_prototype_of` | 2,249 | 6.93% | 15.83% | 0% | 1,216 | — |
| 2 | `js_promise_resolve` | 1,874 | 5.78% | 13.19% | 0% | 0 | — |
| 3 | `js_new_from_class_object` | 1,794 | 5.53% | 12.63% | 68% | 7,695 | 233,138 |
| 4 | `js_property_set` | 740 | 2.28% | 5.21% | 30% | 1,280,717 | — |
| 5 | `js_create_regex` | 332 | 1.02% | 2.34% | 48% | 77,342 | — |
| 6 | `js_check_exception` | 244 | 0.75% | 1.72% | 33% | 32,651,693 | ≤7 |
| 7 | `js_to_string` | 210 | 0.65% | 1.48% | 1% | 573 | — |
| 8 | `js_property_get` | 189 | 0.58% | 1.33% | 0% | 2,065 | — |
| 9 | `js_to_property_key` | 167 | 0.51% | 1.18% | 0% | 7,451 | — |
| 10 | `js_typeof` | 95 | 0.29% | 0.67% | 100% | 2,038,634 | 47 |
| 11 | `js_get_global_this` | 68 | 0.21% | 0.48% | 0% | 109 | — |
| 12 | `js_string_method` | 56 | 0.17% | 0.39% | 100% | 3,164,596 | 18 |
| 13 | `it2s` | 50 | 0.15% | 0.35% | 0% | 573 | — |
| 14 | `js_is_truthy` | 50 | 0.15% | 0.35% | 28% | 5,871,873 | ≤9 |
| 15 | `lambda_restore_number_frame_top` | 46 | 0.14% | 0.32% | 90% | 20,591,471 | 2 |
| 16 | `js_object_define_property` | 44 | 0.14% | 0.31% | 7% | 1,523 | — |
| 17 | `lambda_item_adopt_scalar_home` | 36 | 0.11% | 0.25% | 83% | 12,716,738 | 3 |
| 18 | `js_array_new` | 29 | 0.09% | 0.20% | 69% | 176,383 | 164 |
| 19 | `js_object_get_own_property_descriptor` | 28 | 0.09% | 0.20% | 4% | 2,460 | — |
| 20 | `js_in` | 25 | 0.08% | 0.18% | 3% | 3,828 | — |

Top 20 = 8,326 samples, **59% of all helper time**; the remaining 41% is spread
across the unattributed tail-called bucket and ~340 further helpers.

Three structural facts fall out of this table:

- **Time concentrates in three helpers.** Ranks 1–3 are 5,917 samples = 18.2% of
  working CPU and **41.6% of all helper time**. All three are semantically
  heavy: prototype-chain traversal, microtask/job-queue resolution, and class
  instantiation.
- **The heaviest helpers are not called from JIT code at all.**
  `js_get_prototype_of` has a 0% JIT-caller share — every sampled invocation is
  reached from C, chiefly `js_has_property` → `js_get_prototype_of` →
  `js_get_prototype` → `js_map_get_fast`. `js_promise_resolve` is driven by the
  event loop. Optimizations expressed at the lowering layer cannot reach these;
  they are C-side algorithmic work.
- **Where ns/call is defined, it is tiny.** Every JIT-dominated helper measures
  in the single-digit-to-tens of nanoseconds: `lambda_restore_number_frame_top`
  2ns, `lambda_item_adopt_scalar_home` 3ns, `js_check_exception` ≤7ns,
  `js_string_method` 18ns, `js_typeof` 47ns. (Values marked `≤` also carry
  C-originated invocations, so the true per-JIT-call cost is at or below the
  figure shown.)

The tail-called sub-bucket is dominated by lookup infrastructure rather than by
any single semantic helper: `hashmap_get` (1114 samples), `well_known_name_id`
(579), `gc_register_root_range` (426), `js_map_get_fast` (394), `hashmap_sip`
(336), `js_builtin_catalog_find` (247), `typemap_hash_lookup_*` (349 combined).
**Name and shape lookup is the single largest cross-helper cost** — it is spread
across property access, builtin dispatch, and prototype walking rather than
concentrated in one entry point, which is why it does not appear as a line item
above.

## 3. Result 2 — top helpers by call frequency

**153,937,848 dynamic helper calls** across the suite, over 380 distinct
helpers. Sites = emitted MIR call sites, shown for contrast.

| # | Helper | Calls | % | Sites |
|---:|---|---:|---:|---:|
| 1 | `js_check_exception` | 36,829,216 | 23.92% | 340,350 |
| 2 | `lambda_restore_number_frame_top` | 22,202,259 | 14.42% | 193,328 |
| 3 | `lambda_item_adopt_scalar_home` | 14,270,193 | 9.27% | 173,513 |
| 4 | `js_debug_assert_exception_clear` † | 7,769,190 | 5.05% | 119,823 |
| 5 | `js_strict_equal` | 6,860,839 | 4.46% | 8,174 |
| 6 | `js_subtract` | 6,604,194 | 4.29% | 3,040 |
| 7 | `js_is_truthy` | 5,949,860 | 3.87% | 26,440 |
| 8 | `js_property_access` | 4,776,903 | 3.10% | 46,725 |
| 9 | `js_add` | 4,188,531 | 2.72% | 9,852 |
| 10 | `js_require_object_coercible` | 3,966,762 | 2.58% | 36,901 |
| 11 | `item_type_id` | 3,940,647 | 2.56% | 32,546 |
| 12 | `js_compare` | 3,899,197 | 2.53% | 3,189 |
| 13 | `js_cmp_raw` | 3,645,653 | 2.37% | 2,558 |
| 14 | `js_string_method` | 3,510,478 | 2.28% | 32,958 |
| 15 | `js_check_tdz` | 2,331,922 | 1.51% | 42,746 |
| 16 | `js_get_length_item` | 2,171,857 | 1.41% | 5,062 |
| 17 | `js_debug_check_callee` † | 2,072,431 | 1.35% | 48,475 |
| 18 | `js_typeof` | 2,042,383 | 1.33% | 1,640 |
| 19 | `js_call_function_prerooted_args_into` | 2,005,627 | 1.30% | 41,561 |
| 20 | `js_profiled_push_d` ‡ | 2,000,019 | 1.30% | 4,989 |

† debug-only (`#ifndef NDEBUG`). ‡ profile-build alias for `push_d` (float boxing).

Ranks 21–30, for completeness: `js_to_numeric` (1.43M), `js_increment` (1.43M),
`js_property_set` (1.29M), `js_resolve_unresolved_binding` (892k),
`js_unary_minus` (847k), `js_check_unresolved_capture` (821k),
`js_property_access_named_ic` (640k), `js_env_rehome_scalars` (497k),
`js_eq_raw` (483k), `js_logical_not` (454k).

### 3.1 Sites versus calls diverge sharply

The two orderings disagree in both directions, which is the point of measuring:

- `js_unary_minus`: **133 sites → 847k calls** (6,367 calls/site). Deep inside
  hot loops.
- `js_subtract`: 3,040 sites → 6.6M calls (2,172 per site).
- `js_property_access_named_ic`: **50,967 sites → 640k calls** (13 per site).
  The IC helper is emitted at *more* sites than the non-IC `js_property_access`
  (46,725 sites) yet executes 7.5× less often (640k vs 4.78M calls, 102 per
  site). Sites emitted in cold library-definition code dominate the static count
  while the hot path runs through the non-IC helper. Any future ranking of IC
  coverage by site count would be reading exactly the wrong signal.

## 4. Call frequency is dominated by ABI overhead — which costs ~1% of time

Ranks 1–3 by *frequency* are not JavaScript semantics. They are the ABI:

**`js_check_exception` — 36.8M calls (1 in 4 of all helper calls).**
Body (`js_runtime_state.cpp:1095`):

```c
extern "C" int js_check_exception(void) {
    AutoAssertNoGC no_gc;
    return js_exception_pending ? 1 : 0;
}
```

`js_exception_pending` expands to `(*js_active_runtime_state).exception.pending`
— one TLS load, one deref, one byte load. This is an out-of-line C call, with
full call-site bookkeeping, executed 36.8 million times to read a flag.

Note this is the residual **after** the online exception-poll tracker
(OE1–OE10, `vibe/Lambda_Impl_Online_Exception (done).md`) is already active:
`jm_emit_exception_test` (`js_mir_completion.cpp:257`) folds `CLEAN`, `SET`, and
`UNREACHABLE` states at emission and only falls through to the call in
`JS_EXC_UNKNOWN`. 36.8M residual calls says the tracker reaches `UNKNOWN` at the
overwhelming majority of executed polls — expected, since every
`JIT_EXCEPTION_MAY_SET` call resets it and most catalog rows default to
`MAY_SET`. Two independent levers follow: **tighten the effect metadata** so
more calls are `PRESERVES` (fewer `UNKNOWN` transitions), and **inline the poll
itself** so the residual ones cost two loads instead of a call. `EvalContext`
already carries `js_state` (`lambda-data.hpp:131`) and the JIT frame already
materializes the `Context*` in a register (`em->frame.runtime`), so the poll is
reachable as two loads off an existing register with no TLS access.

DO15 flags a doc-status conflict on this work (the impl doc is named `(done)`
but reads PLANNED). The code shows the tracker landed; the measurement above
gives the follow-up its target.

**`lambda_restore_number_frame_top` — 22.2M calls.** The scalar-home epilogue
(D5.2.1). Release body is a bounds check plus one store of the watermark; the
debug build adds a `0xA5` poison memset over the released extent.

**`lambda_item_adopt_scalar_home` — 14.3M calls.** The scalar-home adoption
step (D5.2.2): a `get_type_id` switch, one word copy, one retag.

These three are **47.6% of all helper calls** (47.3% on the matched batch run),
and all three have bodies small enough to be emitted inline as a handful of MIR
instructions. Adding rank 4 (debug-only) brings the pure-overhead share to
52.7%. By contrast, the semantic helpers — `js_strict_equal`, `js_subtract`,
`js_is_truthy`, `js_add`, `js_compare` — together account for roughly 18%.

### 4.1 But frequency does not predict cost

The §2.1 time table refutes the natural reading of the frequency ranking. Those
same three helpers total **326 samples = 1.00% of working CPU** and 2.29% of
helper time, at 2–7ns per call. The 66 million ABI calls are individually so
cheap, and so well predicted, that eliminating *all* of them by inlining would
recover at most ~1% of this workload — and realistically less, since the inline
form still performs the load and the store.

The caller side does not hide the cost either: JIT-generated code's own self
time is 100 samples (0.3%), which bounds the spill/reload and call-sequence
overhead in the caller at the same order of magnitude.

The three helpers that actually cost time (§2.1 ranks 1–3, 41.6% of helper
time) sit at the opposite corner of the table: thousands of calls, hundreds of
microseconds each, and — for two of the three — **not called from JIT code at
all**.

The design implication is therefore *not* that D5.2's caller-donated-home
protocol needs a cheaper lowering. It bounds scalar space by peak liveness
rather than call count, which is the right trade, and the measured price of its
per-call C-call form is ~0.25% of runtime. Inlining it is a legitimate but
minor cleanup, not a performance lever. **The lever is the C-side cost of
prototype lookup, promise resolution, class instantiation, and the name/shape
lookup infrastructure underneath them.**

## 5. Candidate follow-ups

Ordered by **measured time**, not by call count and not by ease. None of this is
implemented. The ordering deliberately inverts the frequency ranking, for the
reasons in §4.1.

1. **Name/shape lookup, as one cost rather than per-call-site.** The tail-called
   bucket is `hashmap_get` + `well_known_name_id` + `hashmap_sip` +
   `typemap_hash_lookup_*` + `js_builtin_catalog_find` = 2,625 samples = **8.1%
   of working CPU** — larger than any single named helper, and reached from
   property access, builtin dispatch, and prototype walking alike. D4.6 (name
   identity) and DO16's dynamic-intern growth bound are the relevant rulings.
2. **`js_get_prototype_of` — 6.93% of working CPU, 0% JIT-called.** The sampled
   path is `js_has_property` → `js_get_prototype_of` → `js_get_prototype` →
   `js_map_get_fast`/`js_proto_shape_entry`. This is a C-side chain walk with a
   map lookup per level; a per-shape prototype-slot cache would target it. Note
   that no lowering-layer change can reach it — it is not called from JIT code.
3. **`js_promise_resolve` — 5.78%, driven entirely by the event loop.** Worth a
   dedicated look at the job-queue drain path before assuming the cost is
   inherent to the microtask semantics.
4. **`js_new_from_class_object` — 5.53%, 68% JIT-called at 233µs per call.** The
   one heavyweight helper the lowering layer *can* reach. Class instantiation
   cost per call is high enough that shape/metadata reuse across instantiations
   is the obvious question (adjacent to Tune12's P2 constructor-shape work).
5. **Tighten `exception_effect` catalog rows.** Every helper that cannot set the
   pending flag but is registered `JIT_EXCEPTION_MAY_SET` (the conservative
   default) forces the tracker to `UNKNOWN` and materializes a poll. Worth ≤0.75%
   of time, but it is metadata work with no codegen risk, and D7.4.3 already
   requires accurate exception behavior in catalog rows — enforcement of an
   existing ruling rather than a new mechanism.
6. **Inline the exception poll and the scalar-home epilogue/adoption.** Two loads
   off `em->frame.runtime` replacing a call, at 36.8M executions; similarly for
   the type-id switch in `lambda_item_adopt_scalar_home` when the emitter knows
   the lane statically. **Ceiling is ~1% of this workload** (§4.1) — do it as
   code-size and instruction-count hygiene, not as a performance project, and
   keep the helpers for the unknown-type and debug paths.
7. **Re-measure on a long-running workload before acting on the compile share.**
   31.5% in compilation reflects 261 short scripts. The helper ratio is the
   portable number; the compile ratio is not. A long-running benchmark would
   also re-rank §2.1, where per-script setup currently carries weight.

## 6. Instrumentation delta

Uncommitted at the time of writing. Four files:

- `lambda/runtime/mir_emitter_shared.hpp` — `MirEmitter::helper_call_counter`
  hook and `em_emit_helper_call_count()`; called from `em_call_with_args` and
  `em_call_void_with_args` ahead of `em_before_resolved_call`.
- `lambda/js/js_exec_profile.{h,cpp}` — `js_exec_profile_helper_call_counter()`
  slot allocator (own 2048-entry table, keyed by a profiler-owned copy of the
  label since import names are transpiler-pool-owned and the JIT'd stores
  outlive that pool) and the new TSV section.
- `lambda/js/js_mir_hashmap_scope_utils.cpp` — hook installation, `#if
  JS_EXEC_PROFILE_ENABLED`.
- `lambda/main.cpp` — `js_exec_profile_dump()` before the `js-test-batch`
  `_exit(0)` path.

Two profiler bugs were fixed to make this measurable at all, both pre-existing:

- **`js-test-batch` recorded nothing under `JS_EXEC_PROFILE`.** The batch host
  exits via `_exit(0)` to avoid re-entering allocator-backed cleanup after a
  recovered crash, which skips both `atexit` and `runtime_cleanup` — the two
  places the dump was wired. Since the gtest harness routes ~260 of 327 tests
  through this path, essentially the whole suite was invisible.
- **The dump was once-only** (`g_js_exec_profile_dumped`), so a process hosting
  several runtimes kept only the first one's data. It is now a cumulative
  rewrite, with `atexit` retained as the final fallback.

Reproduction:

```bash
make -C build/premake config=debug_profile_native lambda test_js_gtest -j10
JS_EXEC_PROFILE=1 ./test/test_js_gtest.exe        # after installing lambda-debug-profile.exe as lambda.exe
python3 temp/aggregate_helper_calls.py 'temp/js_exec_profile_*.tsv' 25
```

For the matched time/calls pair behind §2.1 — one sampled run and one counted
run of the same manifest, driving the batch host directly:

```bash
sample lambda-debug-profile -wait 300 1 -mayDie -file temp/samples/suite_batch.txt & \
  ./lambda-debug-profile.exe js-test-batch --timeout=60 < temp/js_batch_manifest.txt >/dev/null
```

```bash
JS_EXEC_PROFILE=1 JS_EXEC_PROFILE_OUT=temp/batch_matched.tsv ./lambda-debug-profile.exe js-test-batch --timeout=60 < temp/js_batch_manifest.txt >/dev/null
```

`test_js_gtest` invokes `./lambda.exe` by a hardcoded path, so the profile
binary must be installed in its place for a harness run; the batch host can be
driven directly for the sampling pass.

## 7. Open questions

1. Is a per-shape prototype-slot cache the right answer to §5's item 2, and can
   it be invalidated correctly against `js_intrinsic_note_property_mutation`?
   `js_get_prototype_of` at 6.93% is the largest single named cost in the
   profile and it is entirely C-side, so this is the highest-value open
   question here.
2. Given §4.1, is the ABI-inlining work (exception poll, scalar-home
   adopt/restore) worth doing at all at a ~1% ceiling — and if it is, should
   the inline poll read through `EvalContext::js_state` (two loads off the
   existing frame register) or should the pending flag be relocated into
   `Context` so it is one load? The latter touches the state capsule's
   ownership boundary for a fraction of a percent.
2. Is there a principled way to make the emitter's default exception effect
   `PRESERVES` for helpers that provably cannot throw, rather than auditing
   catalog rows by hand? D6.1.3 sets the polarity rule for *missing* analyses
   (missing must mean defect-capable), which argues against flipping the
   default and for mechanical verification instead.
3. Should scalar-home adopt/restore be expressible as an emitter primitive with
   a helper fallback, or does that duplicate the protocol in two places and
   risk the D5.2.2 invariants drifting apart?
4. Should dynamic helper-call counting graduate from a profile-build hook into a
   catalog-driven report (counts attributable per call site, not just per
   helper), so the site-vs-call divergence in §3.1 is queryable rather than
   requiring a separate analysis pass?
