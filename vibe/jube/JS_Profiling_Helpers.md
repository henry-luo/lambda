# JS Runtime Helper Profiling — Call Frequency and Time Share

**Date**: 2026-08-07  **Status**: MEASURED (findings only; no optimization landed)
**Build**: `release_profile` (`NDEBUG`, `-O3 -flto=thin -march=native`, JS exec
instrumentation on). Per CLAUDE.md rule 10, performance numbers come from an
optimized release configuration; an earlier `debug_profile` pass is retained
only as a methodological note (§1.4).
**Tree anchor**: master `b9b30f4ac` + uncommitted deltas (§6)

**Workload**: the `test_js_gtest` batch — 261 `.js` fixtures under `test/js/`
and `test/js/props/` (real browser libraries: CodeMirror, Tabulator, jQuery,
Bootstrap, Alpine, GSAP, Tom Select, plus the language fixture set), executed in
one process via `js-test-batch`. This is exactly what `test_js_gtest` runs
internally from `JsFileTest::SetUpTestSuite`; driving it directly avoids
swapping a profile binary over `./lambda.exe`, and puts both halves of the
measurement on one identical workload.

Primary sources:

- Profiler: `lambda/js/js_exec_profile.{h,cpp}`
- Emitter hook: `lambda/runtime/mir_emitter_shared.hpp` (`em_emit_helper_call_count`)
- Helper call sites: `em_call_with_args` / `em_call_void_with_args`
- Profile artifacts:
  - `temp/rp_matched2.tsv` — call + emitted-site counts (release_profile)
  - `temp/samples/rp_batch2.txt` — 1ms `sample` of the same run, uninstrumented
  - `temp/rp_analysis.txt` — bucket/helper attribution of the above
  - `temp/symbol_origin.tsv` — symbol → defining object file (§1.2)

---

## 0. Why this pass exists

`js_exec_profile` already counted **emitted MIR call sites** per helper. Static
site counts do not rank optimization targets: a helper emitted at 150k sites in
cold module preamble code and one emitted at 78 sites inside a hot loop are
indistinguishable until execution is counted. Two questions needed dynamic data:

1. What fraction of execution time is spent inside runtime helpers versus
   everything else (JIT-generated code, compilation, GC, allocator)?
2. Which helpers are called most often?

D7.4.3 already requires that "every callable runtime helper carries catalog
metadata"; this pass adds the execution-side counterpart — what those catalog
rows actually cost at run time.

## 1. Method

### 1.1 Dynamic per-helper call and site counts

Every JS runtime helper reaches MIR through exactly two emitter entry points
(`em_call_with_args`, `em_call_void_with_args`), both resolving imports via
`em_ensure_import`. A hook, `em->helper_call_counter`, returns a process-global
`uint64_t*` slot per helper name; when set, the emitter emits an inline
`(*slot)++` (load / add / store on an absolute address) immediately ahead of the
call. Because the hook is consulted **exactly once per emitted call**, the same
table also yields an exact emitted-site count for free.

Properties that make this measurement trustworthy:

- **Exact, not sampled.** Every executed import call is counted.
- **No GC or safepoint interaction.** The increment is three plain integer
  instructions on a non-GC global, emitted before `em_before_resolved_call`, so
  root-slot bookkeeping and the `MAY_GC` call-site record are untouched
  (D5.3.1, D5.3.2 preserved).
- **Opt-in.** Installed only under `LAMBDA_JS_EXEC_PROFILE` and only when
  `JS_EXEC_PROFILE` is truthy at emission time.

### 1.2 Time share, and making a release build profileable

Counting cannot answer question 1 — a 32M-call helper with a two-instruction
body may cost less than a 1,200-call helper that walks a prototype chain. Time
share was measured separately with no counters emitted, using macOS `sample` at
1ms over the same 261-script run.

Attribution: walking each stack top-down, the first **runtime-helper** frame
owns its self samples; a JIT frame sets the owner to `jit`. Plain C frames
appearing *directly* under a JIT frame are counted as helper time in a separate
sub-bucket — at `-O3` with LTO the helper frame is frequently elided by tail
calls, so e.g. the promise allocator appears in the graph as its callee.

Profiling a release build required fixing two things that silently corrupt the
result, both of which would have been invisible in the output:

- **`-Wl,-x` stripped local symbols.** Static functions symbolicated as bare
  `???`, leaving **50.4% of frames unresolvable**. Worse, they carried
  `(in lambda-profile.exe)` while genuine JIT frames carry
  `(in <unknown binary>)`, so an analyzer keying on `???` alone attributes
  ordinary runtime C to the JIT — which is precisely what happened on the first
  pass (it reported "JIT code = 31.9%"). `release_profile` now keeps local
  symbols (§6); resolution went to 79.6% named / 0% unresolved, matching a debug
  build's 80.1% / 0%.
- **No `-g` means no `file:line`**, which is how compile-vs-runtime frames were
  classified. Name heuristics cannot tell MIR's `gvn_modify`, `rewrite_insn`,
  `find_rd_by_reg`, `gap_lr_spill_cost` from runtime C. Replaced with an exact
  **symbol → defining object file** map built from the build's own `.o` files
  and `libmir.a` members (`temp/build_symbol_map.py`). This moved 4.5 points of
  CPU out of "other runtime C" and into "compile", where it belongs.

### 1.3 Caveats

- Counts cover **import-path calls emitted from JIT code**. Helper-to-helper C
  calls and IC fast paths reached without an emitted call are not counted
  separately; they appear in the time profile as the tail-called sub-bucket.
  §2.1 therefore reports a JIT-caller share per helper and withholds `ns/call`
  where that share is low — several of the most expensive helpers are reached
  almost exclusively from C, so their execution count is *not* their emitted-call
  count.
- The `stdout_and_logging` bucket (4.8%) is **not** debug logging — `log_debug`/
  `log_info` compile out under `NDEBUG`. It is the batch protocol's own
  `printf`/`fwrite` traffic plus script output, i.e. harness overhead that a
  non-batch workload would not pay.
- Compilation share is workload-specific: 261 short scripts, each compiled once
  and run briefly. It would fall sharply on a long-running script.
- Wall time: 34.4s uninstrumented, 38.8s under the sampler, ~44s with counters
  emitted. Time and counts come from separate runs of the identical manifest.

Correctness gate: instrumented script output is byte-identical to the goldens,
and the batch reports all 261 scripts complete.

### 1.4 Why the earlier `debug_profile` pass was discarded

An initial pass used `debug_profile` (`-O3` but `DEBUG`, no LTO, no
`-march=native`). It is optimized, so it was not a debug build in the `-Og`+ASan
sense — but `DEBUG` rather than `NDEBUG` distorted exactly what this report
measures:

- `js_debug_assert_exception_clear` (5.05% of all calls) and
  `js_debug_check_callee` (1.35%) are `#ifndef NDEBUG` — **~6.4% of the
  frequency table was phantom**.
- `lambda_restore_number_frame_top` ran a `0xA5` poison `memset` over the
  released extent on every one of its 20.6M calls.

Two further reasons its numbers are not directly comparable: LTO and
`-march=native` change which helper frames survive inlining, and the source tree
itself advanced between the two passes (`js_property_access_named_ic` was
renamed to `js_property_access_key_ic` in `d0e12787a`). Notably the run is
deterministic — that helper reports byte-identical 555,859 calls / 19,101 sites
under both names.

The headline helper share proved robust to all of this (43.8% → 43.5%). The
per-helper detail did not, which is the reason for the rule.

## 2. Result 1 — helpers are ~44% of working CPU

29,072 working samples (~29.1s); 148,838 samples were parked threads.

| Bucket | Samples | % of work |
|---|---:|---:|
| Compile: parse → AST → lowering → MIR codegen | 9,656 | 33.2% |
| **Runtime helpers (intact frames)** | 6,400 | **22.0%** |
| **Runtime helpers (tail-called, frame elided)** | 6,237 | **21.5%** |
| System libs (allocator/memcpy) | 2,558 | 8.8% |
| Other runtime C (boot, module init, name pool) | 1,696 | 5.8% |
| Batch-protocol stdout (harness overhead) | 1,386 | 4.8% |
| dyld lazy link / symbolication | 608 | 2.1% |
| Lazy codegen during execution | 371 | 1.3% |
| **JIT-generated code (self)** | 160 | **0.6%** |
| **Helpers, total** | **12,637** | **43.5%** |

Two readings matter more than the headline:

- **JIT-generated code accounts for 0.6% of working time.** Generated code is
  almost pure glue between helper calls; essentially all semantic work happens
  in C. This also bounds caller-side call overhead (spills, reloads, call
  sequences) at the same 0.6% — so the headroom is inside helper *bodies*, not
  in the calls or the code around them. §2.1 and §4.1 develop this; it is the
  single most important number in this document.
- **Compilation is a third of the run**, reflecting 261 short scripts rather
  than a codegen problem. The helper ratio is the portable number.

### 2.1 Top 20 helpers by run time

Attribution per §1.2: a helper's samples are its own frame plus non-helper C
callees reached under it, excluding nested helpers and anything under a JIT
frame it calls back into. `JIT %` is the share of that helper's sampled frames
whose immediate parent is JIT-generated code. `ns/call` is shown only where the
JIT is the majority caller; where a helper is reached chiefly from C, dividing
its time by JIT-emitted calls has no meaning and the cell is `—`.

| # | Helper | Samples | % work | % helper | JIT % | JIT calls | ns/call |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | `js_get_prototype_of` | 2,034 | 7.00% | 16.10% | 0% | 1,216 | — |
| 2 | `js_new_from_class_object` | 1,623 | 5.58% | 12.84% | 60% | 7,695 | 210,916 |
| 3 | `js_property_set` | 823 | 2.83% | 6.51% | 49% | 1,280,717 | — |
| 4 | `js_to_string` | 407 | 1.40% | 3.22% | 0% | 573 | — |
| 5 | `js_create_regex` | 317 | 1.09% | 2.51% | 46% | 77,342 | — |
| 6 | `js_property_get` | 190 | 0.65% | 1.50% | 0% | 2,065 | — |
| 7 | `js_typeof` | 124 | 0.43% | 0.98% | 100% | 2,038,634 | 61 |
| 8 | `js_to_property_key` | 111 | 0.38% | 0.88% | 0% | 7,451 | — |
| 9 | `js_string_method` | 91 | 0.31% | 0.72% | 100% | 3,164,596 | 29 |
| 10 | `js_get_global_this` | 85 | 0.29% | 0.67% | 3% | 109 | — |
| 11 | `js_property_access` | 42 | 0.14% | 0.33% | 49% | 4,728,201 | — |
| 12 | `js_strict_equal` | 41 | 0.14% | 0.32% | 73% | 6,848,233 | 6 |
| 13 | `js_compare` | 37 | 0.13% | 0.29% | 100% | 3,891,516 | 10 |
| 14 | `js_object_get_own_property_descriptor` | 36 | 0.12% | 0.28% | 3% | 2,460 | — |
| 15 | `it2s` | 35 | 0.12% | 0.28% | 0% | 573 | — |
| 16 | `js_object_define_property` | 33 | 0.11% | 0.26% | 5% | 1,523 | — |
| 17 | `js_array_new` | 32 | 0.11% | 0.25% | 97% | 176,383 | 181 |
| 18 | `js_check_exception` | 31 | 0.11% | 0.25% | 100% | 32,608,419 | **1** |
| 19 | `js_add` | 31 | 0.11% | 0.25% | 100% | 4,096,958 | 8 |
| 20 | `js_in` | 31 | 0.11% | 0.25% | 2% | 3,828 | — |

Top 20 = 6,154 samples, 49% of all helper time; the rest is spread across the
tail-called bucket and ~335 further helpers.

- **Time concentrates in three helpers.** Ranks 1–3 are 4,480 samples = 15.4% of
  working CPU and **35.5% of all helper time** — prototype-chain traversal,
  class instantiation, and property stores.
- **The single heaviest helper is never called from JIT code.**
  `js_get_prototype_of` has a 0% JIT-caller share; every sampled invocation is
  reached from C, chiefly `js_has_property` → `js_get_prototype_of` →
  `js_get_prototype` → `js_map_get_fast`. Lowering-layer optimizations cannot
  reach it.
- **Where ns/call is defined, it is tiny.** `js_check_exception` costs **1ns per
  call**, `js_strict_equal` 6ns, `js_add` 8ns, `js_compare` 10ns.

### 2.2 The largest single cost is not in the helper table

The biggest tail-called internal is `heap_register_gc_root_range`: **2,054 self
samples = 7.1% of working CPU** (3,668 inclusive, 12.6%). Its caller
distribution is not mixed —

> **100% of its samples come from `js_alloc_promise`.**

Every promise allocation registers a GC root range. In the discarded debug pass
this cost surfaced under a different name (`js_promise_resolve`, 5.78%); under
LTO the outer frame is inlined and the true cost lands on the root-range
registration. Either way promise machinery is a top-two cost centre, and the
release build localizes it to a specific, addressable call.

The rest of the tail-called bucket is name/shape lookup, spread across property
access, builtin dispatch, and prototype walking rather than concentrated in one
entry point: `name_pool_create_strview` (838), `well_known_name_id` (505),
`js_map_get_fast` (347), `js_has_own_property` (318), `hashmap_sip` (251),
`js_builtin_catalog_find` (198), `typemap_hash_lookup_*` (286) — **2,425
samples = 8.3% of working CPU** combined.

## 3. Result 2 — top helpers by call frequency

**130,847,135 dynamic helper calls** over 355 helpers. Site counts are now exact
(§1.1); the older `note_mir_call` tally shares a 1024-slot table with
per-function direct-call names and **saturates**, which is why `js_unary_minus`
(78 real sites) was missing from it entirely.

| # | Helper | Calls | % | Sites | Calls/site |
|---:|---|---:|---:|---:|---:|
| 1 | `js_check_exception` | 32,608,419 | 24.92% | 153,725 | 212 |
| 2 | `lambda_restore_number_frame_top` | 20,591,472 | 15.74% | 91,339 | 225 |
| 3 | `lambda_item_adopt_scalar_home` | 12,716,738 | 9.72% | 82,965 | 153 |
| 4 | `js_strict_equal` | 6,848,233 | 5.23% | 4,561 | 1,501 |
| 5 | `js_subtract` | 6,513,823 | 4.98% | 1,112 | 5,858 |
| 6 | `js_is_truthy` | 5,871,876 | 4.49% | 11,142 | 527 |
| 7 | `js_property_access` | 4,728,201 | 3.61% | 22,154 | 213 |
| 8 | `js_add` | 4,096,958 | 3.13% | 6,561 | 624 |
| 9 | `js_compare` | 3,891,516 | 2.97% | 1,285 | 3,028 |
| 10 | `js_cmp_raw` | 3,566,397 | 2.73% | 1,572 | 2,269 |
| 11 | `js_require_object_coercible` | 3,483,462 | 2.66% | 16,608 | 210 |
| 12 | `item_type_id` | 3,470,250 | 2.65% | 15,648 | 222 |
| 13 | `js_string_method` | 3,164,596 | 2.42% | 15,979 | 198 |
| 14 | `js_typeof` | 2,038,634 | 1.56% | 941 | 2,166 |
| 15 | `js_profiled_push_d` † | 2,000,019 | 1.53% | 1,922 | 1,041 |
| 16 | `js_get_length_item` | 1,927,532 | 1.47% | 2,744 | 702 |
| 17 | `js_call_function_prerooted_args_into` | 1,707,181 | 1.30% | 21,140 | 81 |
| 18 | `js_to_numeric` | 1,366,370 | 1.04% | 1,238 | 1,104 |
| 19 | `js_increment` | 1,364,439 | 1.04% | 1,069 | 1,276 |
| 20 | `js_check_tdz` | 1,289,507 | 0.99% | 18,701 | 69 |

† profile-build alias for `push_d` (float boxing).

Note the debug-only `js_debug_assert_exception_clear` (rank 4 previously) and
`js_debug_check_callee` (rank 17) are correctly **absent** here.

### 3.1 Sites and calls diverge by 157×

Calls-per-site ranges from 69 (`js_check_tdz`) to 10,857 (`js_unary_minus`, 78
sites → 846,845 calls). Ranking work by emitted sites would invert the priority
order: `js_check_tdz` is emitted at 240× more sites than `js_unary_minus` and
executes 1.5× as often. Site counts measure how much *code* a construct
generates; only call counts measure how much *work* it does.

## 4. Call frequency is dominated by ABI overhead — which costs 0.2% of time

Ranks 1–3 by *frequency* are not JavaScript semantics. They are the ABI:

**`js_check_exception` — 32.6M calls, 1 in 4 of all helper calls.** Body:

```c
extern "C" int js_check_exception(void) {
    AutoAssertNoGC no_gc;
    return js_exception_pending ? 1 : 0;
}
```

This is the residual **after** the online exception-poll tracker (OE1–OE10)
folds `CLEAN`/`SET`/`UNREACHABLE` states at emission
(`jm_emit_exception_test`, `js_mir_completion.cpp:257`); 32.6M residual calls
means the tracker reaches `UNKNOWN` at most executed polls, expected since every
`JIT_EXCEPTION_MAY_SET` call resets it and most catalog rows default to
`MAY_SET`.

**`lambda_restore_number_frame_top` (20.6M)** and
**`lambda_item_adopt_scalar_home` (12.7M)** are the scalar-home epilogue and
adoption steps (D5.2.1, D5.2.2).

Together: **50.4% of all helper calls.**

### 4.1 Frequency does not predict cost — and release makes this emphatic

Those same three helpers total **65 samples = 0.22% of working CPU** and 0.51%
of helper time. `js_check_exception` measures **1 ns/call**: with `NDEBUG` and
LTO it is a predicted call returning a hot flag.

The debug pass put this trio at 1.00% of work; release puts it at 0.22%. The
optimization that a hypothetical inlining project would deliver has a **ceiling
of roughly two tenths of one percent** — and less in practice, since an inline
form still performs the load. The caller side does not hide the cost either:
JIT-generated code's own self time is 0.6%.

The helpers that actually cost time sit at the opposite corner of the table:
thousands of calls, hundreds of microseconds each, and for the heaviest, **not
called from JIT code at all**.

The design implication is *not* that D5.2's caller-donated-home protocol needs a
cheaper lowering. It bounds scalar space by peak liveness rather than call
count, which is the right trade, and the measured price of its per-call C-call
form is under 0.1% of runtime. **The levers are promise-allocation GC root
registration (7.1%), prototype lookup (7.0%), class instantiation (5.6%), and
the name/shape lookup infrastructure (8.3%).**

## 5. Candidate follow-ups

Ordered by measured time. The ordering deliberately inverts the frequency
ranking, for the reasons in §4.1. None of this is implemented.

1. **GC root-range registration in `js_alloc_promise` — 7.1% of working CPU,
   100% from one caller.** Every promise allocation registers a root range. D5.3.1
   requires root stores be "proportional to dirty live homes at `MAY_GC`
   boundaries — not to instructions"; a per-allocation range registration is
   exactly the shape that ruling wants bounded. Highest-value item here and the
   most sharply localized.
2. **Name/shape lookup as one cost rather than per-call-site — 8.3%.** Reached
   from property access, builtin dispatch, and prototype walking alike. D4.6
   (name identity) and DO16's dynamic-intern growth bound are the relevant
   rulings. `name_pool_create_strview` at 838 samples is the largest single
   contributor and suggests lookups are re-deriving `StrView`s that could be
   interned once.
3. **`js_get_prototype_of` — 7.0%, 0% JIT-called.** The sampled path is
   `js_has_property` → `js_get_prototype_of` → `js_get_prototype` →
   `js_map_get_fast`. A per-shape prototype-slot cache would target it. No
   lowering-layer change can reach it.
4. **`js_new_from_class_object` — 5.6%, 60% JIT-called at ~211µs per call.** The
   one heavyweight helper the lowering layer *can* reach; shape/metadata reuse
   across instantiations is the obvious question (adjacent to Tune12 P2).
5. **Tighten `exception_effect` catalog rows.** Removes polls rather than
   cheapening them. Worth ≤0.11% of time, but it is metadata work with no
   codegen risk, and D7.4.3 already requires accurate exception behavior in
   catalog rows — enforcement of an existing ruling.
6. **Do *not* prioritize inlining the ABI helpers.** Ceiling is ~0.2% (§4.1).
   Worth doing only as code-size/instruction-count hygiene.
7. **Re-measure on a long-running workload.** 33.2% in compilation and 4.8% in
   batch-protocol stdout are both artifacts of 261 short scripts.

## 6. Instrumentation delta

Committed in `d0e12787a`/`b9b30f4ac`:

- `lambda/runtime/mir_emitter_shared.hpp` — `MirEmitter::helper_call_counter`
  hook and `em_emit_helper_call_count()`, called from `em_call_with_args` and
  `em_call_void_with_args` ahead of `em_before_resolved_call`.
- `lambda/js/js_mir_hashmap_scope_utils.cpp` — hook installation.
- `lambda/main.cpp` — `js_exec_profile_dump()` before the `js-test-batch`
  `_exit(0)` path.

Uncommitted:

- `lambda/js/js_exec_profile.cpp` — per-helper slot table (2048 entries, keyed
  by a profiler-owned copy of the label since import names are
  transpiler-pool-owned and the JIT'd stores outlive that pool), plus the exact
  emitted-site count and the new TSV section.
- `utils/generate_premake.py` — `add_release_link_options(strip_locals=...)`;
  `release_profile` now keeps local symbols (macOS `-Wl,-x`, Linux
  `--strip-all`, Windows `-s`). `release` is unchanged. Codegen is identical
  either way — only the retained symbol table differs — and without it a
  release profile cannot name static functions at all (§1.2).

Three profiler defects were fixed to make this measurable, all pre-existing:

- **`js-test-batch` recorded nothing under `JS_EXEC_PROFILE`.** The batch host
  exits via `_exit(0)` to avoid re-entering allocator-backed cleanup after a
  recovered crash, skipping both `atexit` and `runtime_cleanup` — the only
  places the dump was wired. Since the harness routes ~260 of 327 tests through
  this path, essentially the whole suite was invisible.
- **The dump was once-only**, so a process hosting several runtimes kept only
  the first one's data. Now a cumulative rewrite, `atexit` retained as fallback.
- **The site tally saturates** at its 1024-slot table, which it shares with
  per-function direct-call names. Superseded by the exact count in §1.1; the old
  section is left in place for compatibility but should not be used.

Reproduction:

```bash
make -C build/premake config=release_profile_native lambda -j10
```

```bash
sample lambda-profile -wait 300 1 -mayDie -file temp/samples/rp_batch2.txt & ./lambda-profile.exe js-test-batch --timeout=60 < temp/js_batch_manifest.txt >/dev/null
```

```bash
JS_EXEC_PROFILE=1 JS_EXEC_PROFILE_OUT=temp/rp_matched2.tsv ./lambda-profile.exe js-test-batch --timeout=60 < temp/js_batch_manifest.txt >/dev/null
```

```bash
python3 temp/build_symbol_map.py && python3 temp/parse_sample2.py temp/samples/rp_batch2.txt temp/rp_helper_names.txt
```

## 7. Open questions

1. Why does `js_alloc_promise` register a GC root range per allocation, and can
   promise slots live in an existing rooted structure instead? At 7.1% of
   working CPU with a single caller, this is the highest-value open question
   here (D5.3.1).
2. Is a per-shape prototype-slot cache the right answer to §5 item 3, and can it
   be invalidated correctly against `js_intrinsic_note_property_mutation`?
3. Is there a principled way to make the emitter's default exception effect
   `PRESERVES` for helpers that provably cannot throw, rather than auditing
   catalog rows by hand? D6.1.3 sets the polarity rule for *missing* analyses
   (missing must mean defect-capable), which argues for mechanical verification
   rather than flipping the default.
4. Should `release_profile` also carry `-g`? It would restore `file:line` and
   make the symbol-origin map unnecessary, at the cost of binary size. The map
   works, but it is an extra artifact that must be rebuilt whenever the binary is.
5. Should dynamic helper-call counting graduate into a catalog-driven report
   (counts attributable per call site, not just per helper), so the
   sites-vs-calls divergence in §3.1 is queryable rather than requiring a
   separate analysis pass?
