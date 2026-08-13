# Unified AST Compiler Consolidation and Tuning — Detailed Implementation Plan

**Date:** 2026-08-12  
**Status:** Implementation in progress; Phase 0/1/2/3 slices are landed, and the remaining work is performance/LOC closure
**Design authority:** `doc/Lambda_Formal_Design.md` D2.4.1–D2.4.3, D3.2.3, D3.3.1, D5.3.4, D8.1.1, D8.2.1–D8.2.6, D8.4.3, D8.5.1–D8.5.3, D8.6.1, D8.6.3, D8.6.4v2
**Working design:** `vibe/Lambda_Design_Unified_AST.md` U27–U36  
**Predecessor:** `vibe/Lambda_Impl_Unified_AST (done).md` (structural convergence record; not the checklist for this continuation)

---

## 1. Outcome

Finish the Lambda/LambdaJS compiler convergence at the **process** level, not merely at the struct/enum level:

1. both front-ends feed one indexed compilation unit and one explicit pass schedule;
2. binding, traversal, analysis facts, inference, representation planning, MIR value flow, and final emission use common infrastructure;
3. language profiles retain only real semantic differences;
4. repeated scans, linear rediscovery, transient MIR values, redundant boxing, and duplicate lowering paths are removed;
5. the implementation closes only when it passes all semantic, LOC, and compiler-time gates in §2; MIR volume remains a measured diagnostic guard.

This plan does not unify the Tree-sitter grammars or erase Lambda/JavaScript semantic differences. It shares compiler mechanics and scheduling while keeping coercion, object access, errors, calls, and extension nodes profile-owned, as required by **D8.2.1–D8.2.2**.

---

## 2. Non-negotiable exit gates

Under **D8.6.4v2**, the semantic, LOC, compiler-time, and measurement gates in this section are hard. A green test suite cannot compensate for a missed LOC or performance gate, and a faster test scheduler cannot compensate for unchanged compiler work. MIR volume is retained as a required diagnostic signal, but is not an exit blocker.

| Gate | Requirement | Pass condition |
|---|---|---|
| **G0 — semantics** | Preserve behavior, error identity, inference transparency, representation/rooting invariants, and MIR budgets | All commands in §11 pass; no baseline ratchet is weakened |
| **G1 — runtime LOC** | Remove at least 2,000 net physical C/C++ lines from Lambda/JS runtime/compiler code | Candidate count ≤ **317,606**, using the fail-closed scope and anchor below |
| **G2 — Lambda compiler time** | Reduce build/transpile time of the complete `test_lambda_gtest` compilation corpus by at least 10% | `candidate_median / baseline_median ≤ 0.90` |
| **G3 — JS compiler time** | Reduce build/transpile time of the complete `test_js_gtest` compilation corpus by at least 20% | `candidate_median / baseline_median ≤ 0.80` |
| **D4 — JS MIR volume diagnostic** | Print and compare finalized MIR instructions for the frozen large-JS-library cohort; use regressions to steer lowering work | Numeric records are complete and deterministic; any growth is reported and investigated, but does not block closure |
| **G5 — measurement integrity** | Compare identical release-mode compiler work and MIR artifacts | Same timing/volume schema, sample and cohort manifests, cache classification, suite result, and machine conditions; no missing/duplicate/retried samples |

### 2.1 LOC anchor and scope

The LOC anchor is the live pre-plan commit:

```text
AST_TUNE_LOC_BASE=e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3
baseline physical LOC=319606
required candidate physical LOC<=317606
```

The count is the total physical lines of every tracked `*.c`, `*.cc`, `*.cpp`, `*.h`, and `*.hpp` file under:

```text
lambda/runtime/
lambda/js/
```

Rules:

- new shared compiler/runtime files remain inside these roots and count against the candidate;
- a source move outside these roots does not count as deletion and invalidates G1 until the moved path is added to the scope;
- deleted files count as zero in the candidate; new candidate files count from zero in the baseline;
- test, documentation, generated-build, and utility-script LOC neither helps nor hurts G1;
- whitespace reformatting, line joining, minification, or bulk comment stripping is not an acceptable deletion rationale;
- every phase maintains a deletion ledger naming the duplicate functions/paths removed. The final review must explain at least 2,000 lines as deleted implementation, not formatting.

Phase 0 adds `utils/check_ast_tune_loc.sh`. It computes the union of matching files in the anchor tree and candidate tree, fails if the anchor is unavailable, rejects scope escapes recorded by the build-source audit, and prints baseline/candidate/delta/threshold. The verifier itself is outside the counted runtime scope.

### 2.2 Compiler-time definition

For this plan, **build/transpile time** means the internally measured source-to-linked-MIR compiler interval:

```text
parse
+ AST build
+ bind
+ validate / early errors
+ index and call graph
+ capture, effect, type, representation, and function planning
+ MIR lowering
+ emitter/root/scalar/module finalization
+ MIR link
= build_transpile_us
```

It excludes:

- test discovery and GTest startup;
- process creation and batch scheduling;
- program execution;
- output comparison;
- cleanup and process teardown.

Those excluded fields remain recorded so scheduler or runtime improvements are visible, but they cannot satisfy G2/G3. Parse is included because the gate represents the complete compiler work performed for a test; phase totals also report `ast_and_analysis_us` and `mir_and_link_us` so a parser change cannot hide a builder/lowering regression.

The current cold policy may select MIR's lazy native-generation interface (or
the JS large-module interpreter policy). This deliberately measures source
through finalized/linked MIR setup, not deferred machine-code generation that
occurs only when a function is first called. The policy is part of the candidate
configuration, is disabled by `LAMBDA_LAZY_MIR=0` / `JS_LAZY_MIR=0` (and
`LAMBDA_JS_LARGE_INTERP=0` for the JS large-module policy), and must be shown
in the final ledger beside the cold rollback capture; it cannot be described as
an end-to-end execution speedup.

### 2.3 Finalized MIR-volume definition

The MIR diagnostic uses `test_js_gtest` itself; no separate synthetic MIR benchmark is introduced. With timing/volume mode enabled, the compiler prints one machine-readable record after finalizing each test's top-level MIR module:

```text
MIR_VOLUME schema=1 sample_id=<id> test_name=<escaped-name> modules=1 functions=<n> insns=<n>
```

The GTest harness strips this protocol line before expected-output comparison, validates it, and writes the values into the run TSV. Batch, direct, permission, module-profile, and DOM test paths must all emit the same record.

The measured unit is **finalized MIR instruction count**:

- count all instructions in all functions belonging to the test's finalized top-level module;
- exclude labels, local declarations, module/function headers, terminators, and diagnostic comments, matching `test/test_mir_ratchet_gtest.cpp` and `count_module_instructions()` in `test/test_mir_check_helpers.hpp`;
- do not use `MirEmitter`'s pre-finish counter, raw MIR text bytes, JIT machine-code bytes, or execution counters as the gate;
- imported modules are not charged repeatedly to an importing test. Their own compilation samples remain visible separately where applicable;
- full textual MIR dumping remains available for diagnosis, but the gate prints only the numeric size record so dump formatting and I/O cannot affect performance captures.

Phase 0 freezes two manifests from the instrumentation-only baseline:

1. **complete JS manifest:** every top-level test compiled by the unfiltered `test_js_gtest` run;
2. **large-library cohort:** every discovered test whose `test_name` begins `lib_`, plus `underscore_lib`. This includes the large Acorn, AJV, Lodash, Ramda, Yup, and other checked-in library fixtures.

The report includes these comparisons:

```text
sum(candidate finalized insns for frozen large-library cohort)
---------------------------------------------------------------- <= 0.85
sum(baseline finalized insns for frozen large-library cohort)

sum(candidate finalized insns for complete frozen JS manifest)
---------------------------------------------------------------- <= 1.00
sum(baseline finalized insns for complete frozen JS manifest)
```

Every test/library gets a before/after row with instruction delta and percentage. No sample may be added, removed, cached away, or renamed in the comparison. The instruction count must be identical across all repeated runs of the same commit; any run-to-run MIR-volume difference invalidates the volume diagnostic as nondeterministic. A volume increase is a performance warning and requires attribution in the ledger, but does not block G0–G5.

### 2.4 Baseline anchor for performance and MIR diagnostics

Current timing support is not suitable for a hard comparison:

- Lambda's `LAMBDA_PROFILE=1` writes `temp/phase_profile.txt`, is capped, and concurrent batch processes can overwrite the same file.
- JS already emits useful phase fields under `JS_TRANSPILE_TIMING=1`, including its `BATCH_END` line, but `test_js_gtest` currently discards them.
- Lambda's batch protocol reports status without compiler phases; `test_lambda_gtest` measures end-to-end elapsed time externally.

Therefore Phase 0 first lands **instrumentation only**, with no optimization or pass reordering. The commit containing that behavior-neutral instrumentation becomes `AST_TUNE_PERF_BASE`. Baseline release captures are produced from that exact commit before Phase 1 starts. G2/G3 compare compiler time; the same captures provide the finalized-MIR diagnostic. The performance anchor may differ from the LOC anchor because the instrumentation is necessary to observe the original compiler faithfully; its runtime LOC still counts against the final G1 result.

---

## 3. Live evidence and optimization priorities

### 3.1 Structural state

- `lambda/runtime/ast-core.hpp` owns the common node catalog.
- `lambda/js/js_ast.hpp` aliases 53 common node kinds and retains a small JS/TS-specific range.
- `AstNode`, `FnAnalysis`, and `MirEmitter` are shared foundations.
- `lambda/runtime/build_ast.cpp` is 12,472 lines; `lambda/js/build_js_ast.cpp` is 4,786 lines.
- Builder responsibilities remain asymmetric: Lambda performs more binding/type work during build; JS reconstructs more facts in MIR preparation and lowering.
- Core AST ownership is still encoded in many independent switches/walks.
- JS has repeated linear function/class lookup and quadratic propagation/duplicate-detection shapes that should become indexed graph operations.

### 3.2 Representative large-JS profile

Release-mode spot measurements on 2026-08-12 used the existing JS timers. They are diagnostic evidence, not the formal G3 baseline:

| Fixture | Source | AST | MIR lower | Link | Compile subtotal | MIR share |
|---|---:|---:|---:|---:|---:|---:|
| Underscore | 32 KiB | 3.0 ms | 188 ms | 22 ms | 223 ms | 84% |
| Ramda min | 53 KiB | 6.5 ms | 771 ms | 75 ms | 865 ms | 89% |
| Lodash | 78 KiB | 15.4 ms | 2,924 ms | 158 ms | 3,116 ms | 94% |
| AJV | 125 KiB | 9.9 ms | 491 ms | 52 ms | 574 ms | 86% |
| Yup | 159 KiB | 6.1 ms | 351 ms | 42 ms | 418 ms | 84% |
| Acorn | 235 KiB | 9.7 ms | 547 ms | 60 ms | 661 ms | 83% |

The first optimization order is therefore:

1. eliminate repeated analysis and linear/quadratic rediscovery;
2. eliminate duplicate MIR-lowering preparation and unnecessary MIR values/instructions;
3. delete superseded language-local walkers/lowerers immediately after migration;
4. simplify common builder mechanics;
5. consider lazy/cache work only if the phase profile still proves it necessary.

AST allocation alone is too small to produce G3. Builder consolidation is still required for structure and G1, but MIR lowering must carry most of the time reduction.

### 3.3 Regression root cause: cached capture facts had two owners

The timeout symptom was not a scheduler-only regression. The indexed/cached
capture slice added `JsFuncCollected::cached_annexb_suppressed` so Annex-B
lexical suppression could be reused while determining closure captures. The
copy helper still freed that map after copying it, although the function table
owned it and later capture passes reused the pointer. AddressSanitizer reduced
the failure to `hashmap_get -> jm_name_set_has ->
jm_copy_cached_function_locals`: a stale suppression-map read in an Annex-B
batch worker. A worker crash then looked like a transpilation timeout because
the batch controller waited for a lost result and retried the work.

The ownership invariant is now explicit and follows **D5.3.4**: cached
analysis facts live for the whole MIR-transpiler compilation and are released
only by `jm_free_scope_env_names`/transpiler cleanup; copy helpers never free
borrowed cached maps. An attempted whole-path memo was rejected: the lexical
collector's result depends on destination binding-kind state and made the
`lib_codemirror` large-library fixture stop making progress even in isolation.
Only the per-function declaration caches, whose inputs and ownership are
explicit, remain enabled. A focused three-test Annex-B batch passes 3/3 after
the ownership fix. This distinction is important for G2/G3:
first eliminate crash/retry work, then compare source-to-linked-MIR time with
identical complete manifests; a timeout reduction caused only by fewer crashed
workers is not compiler credit.

---

## 4. Target compiler process

### 4.1 `CompilationUnit`

Introduce one owner passed through every post-CST stage:

```c
struct CompilationUnit {
    Script* script;
    const LangProfile* profile;
    AstNode* root;
    AstIndex index;
    CompilationFacts facts;
    MirEmitter emitter;
    CompilerPhaseTiming timing;
    DiagnosticSink diagnostics;
};
```

Use the project `ArrayList`, `HashMap`, arena/pool, and `Str` facilities; do not introduce `std::` containers. Integrate this owner incrementally around the live `Script` and MIR contexts instead of duplicating their storage.

### 4.2 Stable IDs and index

Following **D8.2.4**, assign dense `NodeId`, `ScopeId`, `BindingId`, `FunctionId`, and `ClassId` identities. Build one `AstIndex` immediately after binding. It owns parent/owner/scope links, declaration/reference resolution, function/class direct lookup, call edges, use/def lists, nested functions, returns, and effect inputs.

Lowering never resolves a name for the second time. A reference carries or indexes its final `BindingId`; JS shadow/hoist/TDZ policy is decided during bind/validate. Function/class lookups after indexing are O(1). Graph propagation uses adjacency lists and worklists.

### 4.3 One child-enumeration contract

Add one table or function family that enumerates every owned child of each core node. All common analysis uses it. `LangProfile::visit_ext_children` handles only language-range nodes/extensions.

The traversal contract must support:

- preorder/postorder walking;
- parent/owner index construction;
- mutable and read-only visitors without copying the switch;
- explicit clause/form handling;
- a completeness test for every core node and form.

Existing language-local walkers are migrated one at a time and deleted in the same slice. Do not leave compatibility copies to be removed “later”; G1 depends on deletion as the migration proceeds.

### 4.4 Explicit facts and pass contracts

Follow **D8.2.5**, **D2.4.1**, **D3.2.3**, and **D3.3.1**:

- AST `Type*` is the source/semantic contract;
- `AstIndex` is the identity/binding authority;
- inferred/effective type is a side fact;
- representation plan is a lowering fact;
- emitted representation/provenance is `MirValue`;
- physical MIR type is only `MirValue.mir_type`.

The pass manager runs:

```text
build → bind → validate → index/call graph → capture/effect
      → type/representation inference → function plan
      → MIR lower → emitter finalize → module finalize → link
```

Each pass declares required and produced fact bits. Debug/test builds assert dependencies. Profiles return explicit typed results or `PASS_NOT_APPLICABLE`; they do not supply an alternate pass schedule or silent no-op traversal.

### 4.5 `MirValue` and demand-driven lowering

Complete **D8.2.6** and **D2.4.2–D2.4.3** on both paths. Every core expression returns `MirValue {reg, mir_type, rep, Type* contract, provenance}`. Bare-register expression APIs are transitional and must be removed before closeout.

Pass one demand into lowering:

- `VALUE_DISCARD` — preserve effects/error routing without materializing an unused result;
- `VALUE_ANY` — general fallback;
- `VALUE_REQUIRED_REP` — produce/convert once to the consumer's carrier;
- `VALUE_DEST_REG` — write directly into a known assignment/return/argument/scalar-home destination;
- `VALUE_BRANCH` — branch directly without boxing a condition result.

Profile hooks own semantic coercion; `em_require_rep()` owns carrier conversion. `MirEmitter` alone owns rootability and final root stores under **D5.3.4**. JS fallible helpers continue to use the merged Item error ABI under **D8.4.3**.

---

## 5. Phase 0 — measurement and fail-closed guardrails

**Purpose:** make the requested time reductions observable before changing compiler behavior.

### 5.1 Common timing record

- [x] Add `CompilerPhaseTiming` in the common runtime/compiler layer.
- [x] Use a monotonic clock and microsecond integer fields; timer collection is disabled by default.
- [x] Add phase scopes for parse, AST build, bind, validate, index, analysis, plan, MIR lower, emitter finalize, module finalize, link, execute, and cleanup (zero-valued fields remain explicit until their legacy pass boundaries are migrated).
- [ ] Make nested/import compilation accounting non-overlapping: the hard top-level record includes imported work once; optional module detail is parent-linked and is not re-summed into the gate.
- [ ] Keep timing code allocation-free in hot loops; record phase boundaries, not every node.
- [ ] Add a schema version so stale parsers fail rather than silently shift columns.

Required per-sample TSV fields:

```text
schema_version suite run_id sample_id test_name status cache_state source_bytes
parse_us ast_build_us bind_us validate_us index_us analysis_us plan_us
mir_lower_us emitter_finalize_us module_finalize_us link_us build_transpile_us
execute_us cleanup_us mir_module_count mir_function_count mir_insn_count
```

`sample_id` must be deterministic within the suite and identify every actual script/module compilation requested by a GTest case. Tabs/newlines in names are escaped. Failed compilations still emit a status record so missing samples cannot be mistaken for speed.

### 5.2 Batch protocol and GTest integration

- [x] Add an opt-in `LAMBDA_COMPILER_TIMING=1` control understood by both Lambda and JS.
- [x] Extend Lambda `test-batch` output in `lambda/main.cpp` with the common timing record while preserving old parsers when the flag is absent.
- [x] Convert the existing JS phase counters/BATCH_END fields to the common schema rather than maintaining a second definition.
- [x] After JS top-level module finalization, count functions and finalized instructions with the MT7 definition and print the `MIR_VOLUME` record above.
- [x] Parse and validate timing records in `test/test_lambda_helpers.hpp` and `test/test_lambda_gtest.cpp`.
- [x] Parse and validate timing records in `test/test_js_gtest.cpp`, including direct/DOM subprocess cases as well as the batch path.
- [x] Strip `MIR_VOLUME` protocol lines before golden-output comparison; fail the test on a missing, duplicate, malformed, or mismatched record.
- [x] Persist one suite TSV per run under `temp/ast_tune/<label>/`; never write profiling output to `/tmp`.
- [x] Print a compact GTest property summary: sample count, compiler total, phase totals, p50, p95, and slowest ten samples (`summary.md` beside each capture).
- [ ] Keep existing program output/goldens byte-identical; timing records travel on a separately recognized protocol line and are stripped before output comparison.
- [ ] Retain `LAMBDA_PROFILE`/`JS_TRANSPILE_TIMING` as temporary ad-hoc aliases if useful, but make the common protocol the only hard-gate input.

### 5.3 Capture and compare utilities

- [x] Add `utils/capture_ast_tune_timing.sh`.
- [x] Add `utils/compare_ast_tune_timing.sh`.
- [x] Add `utils/check_ast_tune_loc.sh`.
- [ ] Add focused parser/comparator unit tests, including malformed, duplicate, missing, failed, and schema-mismatch records.
- [x] Add a focused finalized-count equivalence check: `utils/check_ast_tune_mir_volume.sh` verifies the runtime-emitted `mir_insn_count` against the finalized dump for representative LambdaJS modules.
- [ ] If a new `.cpp` is required, edit `build_lambda_config.json` and regenerate via `make`; do not edit generated Lua.

Planned capture interface after this phase lands:

```bash
make release
utils/capture_ast_tune_timing.sh --suite lambda --label baseline --warmups 1 --runs 5
utils/capture_ast_tune_timing.sh --suite js --label baseline --warmups 1 --runs 5

utils/capture_ast_tune_timing.sh --suite lambda --label candidate --warmups 1 --runs 5
utils/capture_ast_tune_timing.sh --suite js --label candidate --warmups 1 --runs 5

utils/compare_ast_tune_timing.sh --baseline temp/ast_tune/baseline --candidate temp/ast_tune/candidate
utils/check_ast_tune_loc.sh --base e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3
```

Until the common capture exists, only single-process diagnosis may use:

```bash
LAMBDA_PROFILE=1 ./lambda.exe <script.ls> --no-log
JS_TRANSPILE_TIMING=1 ./lambda.exe js <script.js> --no-log
```

The legacy Lambda profile file must not be used while running parallel GTests.

### 5.4 Reproducibility protocol

For each baseline/candidate capture:

1. use `make release`; debug builds are invalid for performance;
2. record commit, dirty-tree status, compiler/build stamp, host, CPU, OS, and timing schema;
3. use the same machine on AC power with no competing build/benchmark workload;
4. run one complete warm-up, discard it, then collect five complete suite runs;
5. require every run to pass, canonical-sort records by `sample_id`, and have the same sorted timing manifest; retain the MIR-volume manifest as a diagnostic companion;
6. use the median of each run's aggregate `build_transpile_us`;
7. report per-phase totals, p50/p95, top 20 samples, source bytes, complete/cohort MIR instruction totals, and every large library's MIR delta;
8. if `(max - min) / median > 5%`, collect four additional runs and use the median of nine; investigate persistent instability rather than widening the gate;
9. compare cache-state and frozen large-library manifests. A new cache hit, missing compilation, retry, renamed sample, or changed batch corpus invalidates the run unless explicitly reported as a separate experiment;
10. keep raw TSV, summary Markdown, and manifest under `temp/ast_tune/`; copy final gate numbers into §13 of this checked-in plan.

### 5.5 Optimization Contract Testing (Phase 0.5)

**Purpose:** make compiler/runtime tuning decisions observable as testable
contracts, not only as elapsed time or final semantic output. The ordinary JS
tests remain the semantic gate; this phase adds a small deterministic suite
that proves which cache, guard, IC, lowering demand, and fallback was selected.
This gives the debugging/recovery work a durable regression oracle without
coupling tests to MIR register names, pointer addresses, or incidental helper
layout. The phase implements the measurement intent of **D8.6.4v2** at the
decision level and preserves the common pass/lowering structure required by
**D8.2.5–D8.2.6**.

#### 5.5.1 `JsOptEvent` and `JsOptTrace` contract

Add a test-facing, C-compatible trace API in the JS runtime/compiler layer.
The exact file split may follow the existing runtime/profile modules, but the
public shape is:

```c
typedef enum JsOptEvent {
    JS_OPT_SCOPE_LOOKUP_CACHE_HIT,
    JS_OPT_SCOPE_LOOKUP_CACHE_MISS,
    JS_OPT_FACT_CACHE_HIT,
    JS_OPT_FACT_CACHE_MISS,
    JS_OPT_LOAD_IC_HIT_MONO,
    JS_OPT_LOAD_IC_HIT_POLY,
    JS_OPT_LOAD_IC_MISS,
    JS_OPT_LOAD_IC_INSTALL_MONO,
    JS_OPT_LOAD_IC_INSTALL_POLY,
    JS_OPT_STORE_IC_HIT_MONO,
    JS_OPT_STORE_IC_HIT_POLY,
    JS_OPT_STORE_IC_MISS,
    JS_OPT_STORE_IC_INSTALL_MONO,
    JS_OPT_STORE_IC_INSTALL_POLY,
    JS_OPT_REGEX_COMPILE_CACHE_HIT,
    JS_OPT_REGEX_COMPILE_CACHE_MISS,
    JS_OPT_REGEX_PERMANENT_CACHE_HIT,
    JS_OPT_REGEX_FRESH_WRAPPER,
    JS_OPT_REGEX_KEYLESS_REJECT,
    JS_OPT_REGEX_CACHE_INVALIDATE,
    JS_OPT_ARRAY_SET_FAST_HIT,
    JS_OPT_ARRAY_SET_GUARD_FAIL,
    JS_OPT_DYNAMIC_FUNCTION_FASTPATH,
    JS_OPT_DYNAMIC_FUNCTION_CACHE_HIT,
    JS_OPT_DYNAMIC_FUNCTION_CACHE_MISS,
    JS_OPT_MIR_DIRECT_DESTINATION,
    JS_OPT_MIR_DISCARD_ELISION,
    JS_OPT_MIR_BRANCH_DIRECT,
    JS_OPT_MIR_GENERIC_FALLBACK,
    JS_OPT_MIR_BOX_VALUE,
    JS_OPT_MIR_UNBOX_VALUE,
    JS_OPT_MIR_ROOT_STORE,
    JS_OPT_MODULE_CACHE_HIT,
    JS_OPT_MODULE_CACHE_MISS,
    JS_OPT_TLA_DEFERRED_BODY,
    JS_OPT_TLA_DRAIN,
    JS_OPT_URI_ERROR_CACHE_HIT,
    JS_OPT_URI_ERROR_CACHE_MISS,
    JS_OPT_EVENT_COUNT
} JsOptEvent;

typedef enum JsOptReason {
    JS_OPT_REASON_NONE,
    JS_OPT_REASON_HOLE_OR_SPARSE,
    JS_OPT_REASON_PROTOTYPE_ACCESSOR,
    JS_OPT_REASON_NOT_EXTENSIBLE,
    JS_OPT_REASON_LENGTH_NOT_WRITABLE,
    JS_OPT_REASON_CAPTURE_BEARING_SHORT_REGEX,
    JS_OPT_REASON_KEYLESS_CACHE_ENTRY,
    JS_OPT_REASON_SHAPE_CHANGED,
    JS_OPT_REASON_REPRESENTATION_MISMATCH,
    JS_OPT_REASON_TLA_PENDING,
    JS_OPT_REASON_COUNT
} JsOptReason;

typedef enum JsOptTraceOutcome {
    JS_OPT_OUTCOME_ATTEMPT,
    JS_OPT_OUTCOME_TAKEN,
    JS_OPT_OUTCOME_FALLBACK,
    JS_OPT_OUTCOME_INVALIDATED
} JsOptTraceOutcome;

typedef struct JsOptTraceCounter {
    uint64_t attempts;
    uint64_t taken;
    uint64_t fallback;
    uint64_t invalidated;
} JsOptTraceCounter;

typedef struct JsOptTraceSnapshot {
    uint32_t schema;
    uint32_t enabled;
    JsOptTraceCounter events[JS_OPT_EVENT_COUNT];
    uint64_t reason_counts[JS_OPT_REASON_COUNT];
} JsOptTraceSnapshot;

void js_opt_trace_set_enabled(int enabled);
void js_opt_trace_reset(void);
void js_opt_trace_record(JsOptEvent event, JsOptReason reason,
                         JsOptTraceOutcome outcome);
void js_opt_trace_snapshot(JsOptTraceSnapshot* out);
```

`JsOptReason` is a stable enum for guard/fallback causes, including
`HOLE_OR_SPARSE`, `PROTOTYPE_ACCESSOR`, `NOT_EXTENSIBLE`,
`LENGTH_NOT_WRITABLE`, `CAPTURE_BEARING_SHORT_REGEX`, `KEYLESS_CACHE_ENTRY`,
`SHAPE_CHANGED`, `REPRESENTATION_MISMATCH`, and `TLA_PENDING`. The event and
reason values are schema-versioned; adding a value is compatible, renumbering
one is not.

The first implementation stores the snapshot in the profile-enabled child
process. This is safe for the isolated fixture harness (one child is one
runtime/compilation sample); move ownership into `JsRuntimeState` before any
in-process parallel contract runner is enabled. A test begins with reset, runs
one isolated fixture, snapshots the counters, and then disables the trace. The
existing `JS_EXEC_PROFILE` implementation remains the
broad runtime profiler and its IC/shape counters should be reused rather than
duplicated; `JsOptTrace` adds deterministic decision/fallback contracts that
the broad profiler does not provide.

Rules for the implementation:

- normal/release execution has no allocation, clock read, file I/O, or string
  formatting on the disabled path;
- contract tests use a profile-enabled test binary or explicit test mode;
  release timing captures remain instrumentation-free so G2/G3 are not
  contaminated;
- counters are per runtime/compilation sample, not process-global accumulators
  shared by parallel GTests;
- counters record decisions and reason IDs, never MIR register numbers,
  addresses, hash-table iteration order, or helper symbol spelling;
- an optional bounded event ring is available only for state-machine tests
  (for example TLA drain ordering); bulk corpus runs use counters only;
- trace records are emitted as a validated machine-readable protocol line and
  are stripped before golden-output comparison, like `MIR_VOLUME`.

#### 5.5.2 Optimization contract matrix

Every optimization must have one row in the implementation ledger with these
columns: `optimization_id`, precondition, guard, fast action, fallback,
`JsOptEvent`/reason values, fixture, semantic invariant, and MIR/timing metric.
The first required rows are:

| Optimization | Positive fixture assertion | Required guard/fallback assertion |
|---|---|---|
| Regex compile/permanent cache | repeated large literal and a medium capture-free literal in a hot loop compile once and then hit; each evaluation still receives a fresh RegExp object/`lastIndex` | short capture-bearing literal creates a fresh wrapper; no keyless entry is stored in the compile cache; invalidation releases only its own entry |
| Dense array indexed store | dense contiguous array takes the direct store path | hole/sparse, prototype setter, non-extensible array, or non-writable length records the reason and uses the generic path |
| Named load/store IC | first access installs mono/polymorphic state and later accesses hit it | shape/key/type change records a miss and invalidation/transition |
| Dynamic `Function` fast path | simple eligible body records the parser fast path | comments, ASI-sensitive, reserved/complex syntax records the normal-parser fallback |
| Demand-driven MIR lowering | discard, direct branch, and direct destination avoid unnecessary value materialization | representation mismatch records generic `VALUE_ANY` fallback; boxing/unboxing and root stores remain explicit under **D2.4.1–D2.4.3** and **D5.3.4** |
| Module/TLA settlement | deferred body is drained before namespace resolution | pending async module is not resolved early and a module is not drained twice |
| Scope/fact indexes | repeated lookups/fact requests hit the indexed cache | mutation or unavailable fact records a miss and rebuild/fallback |
| URI error cache | repeated malformed URI decoding records a miss followed by rooted cache hits | absent runtime/root registration uses the uncached rooted constructor and cannot report a hit (**D5.3.3**) |
| MIR branch-join carrier | logical and conditional joins publish the merged `Item` destination to the post-join ERROR-lane test | neither short-circuit nor alternate edges may expose a path-local helper register (**D8.4.3**) |

Each positive case must also assert the ordinary result. Each negative case
must assert the result/error and the reasoned fallback. This is important for
**D8.4.3**: an optimization may not hide or replace the merged JS `Item` error
lane merely to make a fast-path counter increase.

#### 5.5.3 Contract-test harness

- [x] Add `test/test_js_opt_gtest.cpp`; its inline fixture sources are written
  to `temp/js_opt_contract/` so each decision remains child-isolated without a
  second source-of-truth fixture tree.
- [x] Run each fixture in an isolated runtime/child process, enable the trace,
  parse exactly one `JS_OPT_TRACE schema=1 ...` record, and fail on missing,
  duplicate, malformed, unknown-schema, or unknown-event records.
- [x] Assert semantic output/error plus the expected `attempts`, `taken`,
  `fallback`, `invalidated`, and reason counters. Use inequalities only where
  repeated execution legitimately changes counts.
- [x] Add trace-on/trace-off differential checks for semantic output and
  fail-closed trace-file creation. Both child runs keep the same profile mode,
  toggle only `JS_OPT_TRACE`, and compare their finalized `LAMBDA_MIR_DUMP_PATH`
  artifacts after canonicalizing process-local MIR addresses.
- [ ] Add a bounded event-sequence assertion for TLA/module settlement and any
  future cache ownership state machine; do not use sequence assertions for
  ordinary hot-loop counts.
- [x] Add parser tests for duplicated, schema-mismatched, unknown-event, and
  truncated trace records, reusing the fail-closed timing/MIR protocol policy
  from §5.2–§5.3.
- [ ] Run a single trace-enabled smoke sample for each frozen large JS library
  (`lib_*` plus `underscore_lib`) to prove the intended optimization is
  exercised. Do not run the full Test262 corpus with tracing enabled by
  default.
- [ ] Keep `test_js_gtest` and `test262-baseline` as semantic gates; contract
  tests are additional internal-path gates, not replacements for them.

#### 5.5.4 Phase 0.5 exit

- [ ] Every optimization row has one positive and one guard/fallback fixture.
- [x] The contract executable passes with tracing enabled and disabled, with
  identical semantic results and equivalent finalized MIR for every fixture in
  the current matrix. The comparison is structural after removing only
  process-local addresses; register names, helper calls, control flow, and
  constants remain checked.
- [ ] All trace records are deterministic, per-sample, schema-validated, and
  isolated across sequential and parallel test execution.
- [ ] Existing `JS_EXEC_PROFILE`, timing, and MIR-volume diagnostics continue
  to parse without schema drift.
- [ ] Release timing captures contain no contract instrumentation and remain
  eligible for G2/G3.
- [ ] Each tuning change updates the contract matrix before changing the
  optimization or cache policy; a missing contract row blocks that slice.

### 5.6 Phase 0 exit

- [ ] Instrumentation-on versus instrumentation-off wall time differs by ≤1% on both suites.
- [x] Instrumentation changes no AST dump, MIR budget, output, or test result (`utils/check_ast_tune_instrumentation.sh`).
- [ ] Every compiled test in both GTests produces exactly one validated top-level timing sample.
- [x] Every JS test produces exactly one finalized `MIR_VOLUME` record, and the Lodash count agrees with an artifact-dump count in `utils/check_ast_tune_mir_volume.sh`.
- [ ] Baseline raw captures and summaries exist for five valid runs.
- [ ] `AST_TUNE_PERF_BASE` is recorded in §13.
- [ ] Complete-JS and large-library cohort manifests and baseline instruction totals are recorded in §13.
- [x] LOC verifier reports 319,606 for the fixed G1 anchor and 316,657 for the current tree (`utils/check_ast_tune_loc.sh`).

No optimization work starts until this exit is green.

---

## 6. Phase 1 — authoritative traversal, binding, and `AstIndex`

**Purpose:** turn repeated rediscovery into one linear indexing step and establish the common substrate for all later deletions.

### 6.1 Stable identity

- [ ] Add compact ID types with an explicit invalid value; assign IDs deterministically in source/preorder order.
- [ ] Assign node IDs through the common allocation helper used by both builders.
- [ ] Assign scope/binding/function/class IDs during bind/index without changing source semantics.
- [ ] Add dump support that prints stable IDs only in an opt-in debug form so existing goldens remain stable.
- [ ] Assert one owner compilation unit per ID and reject cross-unit accidental reuse.

### 6.2 Common child enumeration

- [ ] Inventory every common core node/form and every owned AST child.
- [ ] Implement `visit_core_children()` once in the common layer.
- [ ] Implement JS/Lambda extension-child callbacks only for language-range nodes/clauses.
- [ ] Add the catalog completeness GTest: every owned child exactly once, no borrowed/back-reference treated as a child.
- [ ] Port parent assignment, function collection, class collection, return/yield/await discovery, and generic validation walks to it.
- [ ] Delete each superseded walker/switch in the same commit that migrates its last caller.

### 6.3 Binding and index

- [ ] Define `AstIndex` dense arrays and adjacency lists using project containers/arena allocation.
- [ ] Store parent, lexical scope, owning function/class, declaration/reference binding, child functions/classes, calls, returns, yields/awaits, and use/def edges.
- [ ] Make Lambda's existing builder bindings populate the shared binding IDs without semantic change.
- [ ] Make JS hoist/block/TDZ binding authoritative before MIR analysis; validation remains profile-specific.
- [ ] Replace lowering-time JS shadow repair and repeated scope-name lookup with resolved binding IDs.
- [ ] Replace linear `jm_find_collected_func`/`jm_find_class` shapes with direct indexed lookup.
- [ ] Replace duplicate-function discovery with one index-build hash/set check.
- [ ] Replace strict/effect propagation full rescans with adjacency-list worklists.
- [ ] Add debug verification comparing old lookup results with indexed results during migration; delete the old path once equivalence is green.

### 6.4 Phase 1 tests and checkpoint

- [ ] Binding tests cover nested shadowing, JS `var` hoist, `let/const` TDZ, duplicate declarations, methods/classes, Lambda captures, and cross-module names.
- [ ] Index tests cover deeply nested functions/classes and adversarial broad call graphs.
- [ ] Complexity microtest demonstrates linear index construction and no quadratic growth at 2×/4× synthetic functions.
- [ ] Both full GTest compiler-time captures remain no slower than the Phase 0 baseline by more than 2%.
- [ ] Record cumulative LOC delta and deleted-walker ledger.

Planning checkpoint, not a substitute for final gates: aim for ≥350 cumulative LOC removed, ≥2% Lambda compiler reduction, and ≥4% JS reduction.

---

## 7. Phase 2 — fact separation, typed passes, and shared worklists

**Purpose:** make the compiler stages explicit and remove the duplicated Lambda/JS analysis process.

### 7.1 Fact tables

- [ ] Add `NodeFacts`, `BindingFacts`, and `FunctionFacts` side tables keyed by stable IDs.
- [ ] Inventory every mutation/read of `AstNode.type` and classify it as declared contract, inferred type, narrowed flow fact, or representation plan.
- [ ] Keep declared/source contracts on AST/bindings; migrate inferred/effective values into side tables.
- [ ] Add assertions preventing an inferred fact from overwriting a declared contract.
- [ ] Add a “facts erased” boxed mode for **D3.3.1** differential testing.
- [ ] Keep representation/provenance out of `TypeId` shortcuts under **D2.4.1**.

### 7.2 Typed pass manager

- [ ] Define pass IDs and required/produced fact masks.
- [ ] Wrap current passes in the common order without changing their internals first.
- [ ] Make missing prerequisites a debug/test failure with the pass and fact name.
- [ ] Move per-phase timing boundaries to the pass manager.
- [ ] Replace `void`/silent profile hooks with typed status/result hooks.
- [ ] Require an explicit `PASS_NOT_APPLICABLE` result for a language that does not use a stage.
- [ ] Centralize diagnostic collection/order so moving a pass does not reorder user-facing errors unintentionally.

### 7.3 Shared graph worklists and inference

- [ ] Generalize call/capture/effect iteration over `AstIndex` adjacency lists.
- [ ] Merge the common evidence vocabulary into one `ParamEvidence`/binding evidence record.
- [ ] Keep Lambda and JS evidence resolution in typed profile policies.
- [ ] Merge call-site and body evidence through the same worklist; converge only affected functions.
- [ ] Preserve JS Number semantics and Lambda numeric-lane semantics; inference changes implementation only.
- [ ] Migrate one language at a time behind an equivalence assertion, then delete its old collector/cache/walk.
- [ ] Ensure invalidation is explicit even if incremental recompilation is not yet enabled.

### 7.4 Phase 2 tests and checkpoint

- [ ] Pass-order tests deliberately invoke a pass without its prerequisite and assert failure.
- [ ] Declared-versus-inferred tests cover annotations, aliases, comparisons, reassignment, containers, division, and cross-call evidence.
- [ ] Boxed-versus-specialized differential tests pass for both languages.
- [ ] Diagnostics and early-error snapshots are unchanged.
- [ ] Re-profile both suites and attribute gains/losses by phase.
- [ ] Record cumulative LOC delta and deleted-analysis ledger.

Planning checkpoint: aim for ≥900 cumulative LOC removed, ≥5% Lambda compiler reduction, and ≥10% JS reduction.

---

## 8. Phase 3 — demand-driven `MirValue` lowering

**Purpose:** attack the dominant MIR-lowering time by avoiding work, not merely moving it.

### 8.1 Complete the expression contract

- [ ] Inventory every Lambda and JS expression-lowering entry/return type.
- [ ] Extend common `MirValue` to carry the full `Type*` contract and provenance required by **D2.4.2**.
- [ ] Convert core expression handlers from bare register/`TypeId` results to `MirValue`.
- [ ] Ban `MIR_reg_type()` as a semantic/representation oracle in expression lowering; keep only named physical-layer helpers permitted by **D2.4.1**.
- [ ] Route every carrier change through fail-closed `em_require_rep()` under **D2.4.3**.
- [ ] Keep JS semantic coercions in JS profile hooks and Lambda conversions in Lambda profile hooks.

### 8.2 Add demand, in low-risk slices

Implement and verify in this order:

1. [ ] `VALUE_DISCARD` for expression statements and unused completion values;
2. [ ] `VALUE_BRANCH` for `if`, loops, logical operators, and conditional expressions;
3. [ ] `VALUE_DEST_REG` for local assignment and returns;
4. [ ] required representation for fixed call arguments and scalar homes;
5. [ ] direct destination emission for literals, identifiers, and simple unary/binary operations;
6. [ ] destructuring destinations and aggregate element construction;
7. [ ] extension nodes only where the profile can prove the same invariants.

Every handler may fall back to `VALUE_ANY`. Correctness never depends on satisfying the demand.

### 8.3 Emitter ownership and emitted-work reduction

- [ ] Consolidate final root stores, scalar homes, and module data finalization in `MirEmitter` only (**D5.3.4**).
- [ ] Delete language-local final-store/rooting copies as each caller migrates.
- [ ] Count MIR instructions, calls, boxing/unboxing conversions, temporaries, and root stores per timed sample.
- [ ] For each large JS fixture, compare compiler phase time and emitted instruction categories before/after.
- [ ] Keep finalized MIR instruction counts deterministic across all repeated runs; investigate any variation before using a capture.
- [ ] Preserve JS error Item identity and try/finally routing under **D8.4.3**.
- [ ] Reject any speedup that depends on weakening error checks or precise rooting.

### 8.4 Phase 3 tests and checkpoint

- [ ] Add focused MIR-shape tests for discard, direct branch, direct destination, representation fallback, errors, and GC-visible values.
- [ ] Run `test_mir_emission_gtest`, `test_js_mir_emission_gtest`, `test_mir_ratchet_gtest`, and `test_mir_gc_stress_gtest` after each demand family.
- [ ] Preserve **D8.6.1** zero-slack budgets; decreases tighten normally, increases require explicit same-change review and are not justified by this plan alone.
- [ ] Run **D8.6.3** forced-GC/poison oracles for every change to root/store placement.
- [ ] Run boxed-demand differential mode for both languages.
- [ ] Re-profile large JS fixtures and the two full GTest corpora.
- [ ] Record cumulative LOC delta and deleted-lowering ledger.

Planning checkpoint: aim for ≥1,500 cumulative LOC removed, ≥8% Lambda compiler reduction, and ≥17% JS reduction.

---

## 9. Phase 4 — builder/lowering consolidation and deletion

**Purpose:** finish common-process adoption, reach the LOC gate, and remove compatibility paths.

### 9.1 Builder mechanics

- [ ] Inventory duplicate node allocation, source-span, intrusive-list, diagnostic, identifier, literal, parameter, declarator/pattern, and scope helpers.
- [ ] Search for an existing common helper before extracting another; at the third near-identical case, parameterize the common shape.
- [ ] Extract typed core-node construction helpers without accepting grammar-specific `TSNode` interpretation.
- [ ] Keep Lambda and JS CST switches in their builders; move only mechanics and common validation/fact publication.
- [ ] Make both builders return/populate the same `CompilationUnit` state and phase boundaries.
- [ ] Delete superseded helpers immediately.

### 9.2 Common lowering skeleton

- [ ] Inventory core node cases still independently lowered by Lambda and JS.
- [ ] Move only structural control flow, demand propagation, common call plumbing, binding lookup, and emitter operations into the shared driver.
- [ ] Keep truthiness, coercion, member access/ICs, iteration protocol, error semantics, calling convention decisions, and language-range nodes in typed profiles.
- [ ] Remove pass-through profile hooks and replace them with direct common behavior or an explicit semantic hook.
- [ ] Delete compatibility wrappers after their final caller migrates.
- [ ] Keep `transpile.cpp`/C2MIR frozen under U11 and project rule 14; it is not extended to JS and is not a source of shared code.

### 9.3 Remaining measured hotspots

Only after the common migrations above, use the new phase and sample reports to address remaining direct costs:

- [ ] replace any remaining repeated function/class/binding scans with index lookups;
- [ ] reserve lists/maps from indexed counts to avoid growth/re-hash churn;
- [ ] avoid reconstructing signatures, type keys, or helper imports inside node loops;
- [ ] intern/reuse immutable lowering descriptors owned by the compilation unit;
- [ ] collapse repeated finalize/link walks where the emitter can produce the final table once;
- [ ] inspect the slowest 20 samples, especially Lodash/Ramda and large libraries, rather than tuning only micro-scripts;
- [ ] separately improve fixed-size alphabetical batch scheduling if desired (weighted/slowest-first bounded workers), but report that only as wall-time improvement, never G2/G3 credit.

### 9.4 Phase 4 exit

- [ ] No semantic pass has a private core-child traversal.
- [ ] No core expression boundary returns a bare MIR register.
- [ ] No lowering path repeats binding/function/class discovery.
- [ ] No language lowerer inserts final roots/stores outside `MirEmitter`.
- [ ] Old analysis/lowering compatibility paths are deleted, not disabled.
- [ ] `utils/check_ast_tune_loc.sh` reports candidate ≤317,606 and an audited delta ≤−2,000.
- [ ] Lambda candidate ratio is ≤0.90.
- [ ] JS candidate ratio is ≤0.80.
- [ ] Frozen large-library and complete-JS finalized-MIR ratios are reported with deterministic counts; any increase has an attribution note, but these ratios are not exit gates.

If all Phase 4 exit items pass, skip Phase 5.

---

## 10. Phase 5 — measured contingency only

**Entry condition:** G2 or G3 still fails after Phase 4, or the diagnostic MIR report identifies a large avoidable emitted-work regression and phase/sample attribution identifies its cause. Do not enter based on intuition.

Allowed investigations, in priority order:

1. [ ] eliminate the top remaining repeated analysis/finalization operation proven by counters;
2. [ ] specialize the common driver dispatch only if profiles show dispatch itself material;
3. [ ] add function-level lazy MIR lowering for unreachable/unused functions if the full sample record remains present and the top-level `build_transpile_us` honestly reports the skipped work;
4. [ ] evaluate D8.5-approved lazy/cache work only with explicit hit/miss counters and the required cache correctness rules.

Restrictions:

- persistent cache warm hits are not accepted as the primary G2/G3 proof;
- a test/module sample may not disappear from the manifest because it was cached or made lazy;
- do not implement a new AST interpreter (**D8.1.1**);
- do not patch MIR/vendor code;
- do not weaken optimization level, validation, early errors, rooting, or link correctness;
- do not add source-level parallelism until profiler data proves independent work and determinism/diagnostics are preserved.

Every contingency change must include a before/after phase profile showing that its target was material and its reduction survives the full-suite median.

---

## 11. Regression and closeout matrix

Run focused tests after each slice and the complete matrix before declaring the plan done.

### 11.1 Build and focused compiler tests

```bash
make release
make build-test
./test/test_lambda_gtest.exe
./test/test_js_gtest.exe
./test/test_js_opt_gtest.exe
./test/test_mir_emission_gtest.exe
./test/test_js_mir_emission_gtest.exe
./test/test_mir_ratchet_gtest.exe
./test/test_mir_gc_stress_gtest.exe
```

Use the real executable names produced by `make build-test`; if a target is filtered during development, the unfiltered executable remains the closeout gate.

### 11.2 Runtime baselines

```bash
make test-lambda-baseline
make test262-baseline
make test-js-opt
```

`make node-baseline` is not part of this plan's default closeout. Run it only if a slice intentionally changes Node compatibility or the user separately requests that gate.

### 11.3 Required semantic invariants

- **D3.3.1:** boxed/facts-erased and specialized execution are observationally identical.
- **D2.4.1–D2.4.3:** no representation decision is recovered from a MIR register class; conversions fail closed.
- **D5.3.4:** precise root policy/final stores exist only in `MirEmitter`.
- **D8.4.3:** JS fallible helpers preserve the merged error Item and its identity through routing.
- **D8.6.1:** no silent MIR budget slack.
- **D8.6.3:** forced-GC/poison runs match unstressed runs.

### 11.4 Performance closeout

```bash
make release
utils/capture_ast_tune_timing.sh --suite lambda --label candidate --warmups 1 --runs 5
utils/capture_ast_tune_timing.sh --suite js --label candidate --warmups 1 --runs 5
utils/compare_ast_tune_timing.sh --baseline temp/ast_tune/baseline --candidate temp/ast_tune/candidate
utils/check_ast_tune_loc.sh --base e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3
```

The comparison must print and the final plan update must record:

- baseline/candidate commits and dirty state;
- schema and sample-manifest hashes;
- five (or nine) per-run aggregate values;
- baseline/candidate medians and ratios;
- phase totals and changes;
- p50/p95/top 20 samples;
- source-byte totals, complete/cohort finalized-MIR totals, and every large library's before/after instruction count;
- LOC baseline/candidate/delta;
- all suite results.

---

## 12. Risk controls and rollback rules

| Risk | Control | Rollback rule |
|---|---|---|
| binding/TDZ/hoist behavior changes while indexing | dual-run old/new resolver assertions; targeted early-error tests | revert the current binding slice; do not retain a shadow-repair workaround |
| declared type polluted by inference | separate facts + facts-erased differential mode | revert the mutating migration and classify the fact correctly |
| MIR speedup changes semantics | per-demand slice tests, boxed differential, MIR shape tests | disable/revert only that demand producer; keep safe `VALUE_ANY` fallback |
| root liveness regression | emitter-only ownership + forced-GC/poison | revert the store/root consolidation slice; never restore conservative native-stack scanning |
| JS IC/native specialization cliff | per-sample phase and instruction counters on large libs | preserve profile-owned specialization; do not force generic common lowering |
| diagnostic order drift | central diagnostic sink and snapshot comparison | preserve source-order stable sort before deleting old pass |
| timing noise | internal timers, release mode, manifest validation, 5/9-run median | invalidate capture and repeat; never relax percentages |
| LOC gate gamed by file moves/formatting | anchor-tree union counter + source-scope audit + deletion ledger | add moved path back to scope or reject the claimed reduction |
| cache/lazy path hides work | cache-state/sample manifest, explicit lazy/interpreter policy, and cold rollback comparison | report deferred native generation separately; only source-to-linked-MIR work contributes to G2/G3 |
| optimization trace changes the selected path or adds hot-loop cost | trace-on/trace-off differential fixtures, disabled fast path, separate profile-enabled contract binary | disable the trace at the runtime boundary; keep release timing captures instrumentation-free and revert only the affected event hook |
| internal contract overfits implementation details | stable event/reason IDs, no register/pointer/helper-name assertions, positive plus guard/fallback fixtures | replace the assertion with a semantic decision contract or remove the event; do not freeze incidental MIR layout |

Every implementation slice is independently revertible and leaves one authoritative path. Do not keep two production implementations behind permanent flags. Temporary equivalence flags/assertions are removed in the same phase that deletes the old path.

---

## 13. Progress and evidence ledger

This section is updated as implementation proceeds. A checkbox is changed only with the named evidence.

### 13.1 Anchors

| Item | Value |
|---|---|
| LOC base commit | `e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3` |
| LOC baseline | 319,606 |
| LOC hard maximum | 317,606 |
| Performance base commit | remote `bd54f11c9` release baseline capture; candidate remains dirty-tree diagnostic until final source slice |
| Timing schema version | `1` (common control records + GTest TSV) |
| Lambda sample-manifest hash | baseline `16782a500ee471b21a3632ac14a9c00f8587b03097e86556d2778bc53b66cd46`; candidate `0997303bcedcd0504fa9f21e96687f18d395856a08419ac3abf7fa7c8d339bd6` |
| JS sample-manifest hash | baseline `fa13051f73cdc88744cc103671db2f9ddf7f5dafd1a36a7424ddb892488be2ab`; candidate `56bee0991e833c2265a1d9f64b444b89dbeb008b537a9a685aa5a485640f0ca6` |
| JS large-library cohort hash | derived from the identical JS run-0 manifest; cohort predicate is `lib_*` plus `underscore_lib` |
| JS complete baseline MIR instructions (diagnostic) | run 0: `7,187,862` (capture under `temp/ast_tune/baseline/js`) |
| JS library-cohort baseline MIR instructions (diagnostic) | run 0: `5,743,247` (`lib_*` + `underscore_lib` cohort) |

### 13.1.1 Current diagnostic evidence (not a hard-gate capture)

The first indexed-lowering slice was measured on the debug Lodash fixture only
to validate attribution (release captures remain required for G3). The same
working tree/fixture moved from approximately **38.0 s** compiler subtotal
before the pointer-index lookup to **7.94 s** after it; finalized MIR remained
**843,772 instructions**. This confirms repeated function discovery was a
material time hotspot while also showing that the MIR-volume diagnostic needs
a separate demand/emission slice. A later release-linked one-pass probe reached
about **2.00 s**, but the collector contract proved that `AstIndex` callable
counts are an upper bound rather than an exact semantic count, so the unsafe
one-pass allocation was removed; the index remains the identity source while
the collector's count pass remains authoritative. The values are deliberately
not entered as a hard-gate result.

The demand/emission slices now include block-local immediate-constant reuse,
immutable helper-result metadata, module-name/IC caching, and removal of
redundant discard-value MIR calls. A current release Lodash diagnostic reports
**798,235** finalized instructions; `utils/check_ast_tune_mir_volume.sh`
reconciles this count with the finalized MIR artifact. The frozen large-library
cohort remains a diagnostic only, so any count movement is recorded and
attributed without turning MIR volume into an exit gate.

A post-finalization copy-propagation experiment was rejected and removed because
MIR's post-finish operand mutation contract is not established for all
backends. The safe fallback is retained until a demand producer can prove the
full **D8.4.3** error/root contract.

### 13.1.2 Test262 instability root-cause closeout (2026-08-13)

The 356 batch-unstable/slow results were runtime/compiler defects, not a runner
policy problem. No Test262 test, harness, manifest, timeout, batching rule, or
retry rule was changed. Five coupled root causes were fixed:

1. Prototype-reset snapshots kept raw Map data-zone pointers and untraced
   accessor Items. Precise GC could relocate the live data and later restore a
   discarded nursery buffer; descriptor mutations could also retag/enlarge the
   pristine `TypeMap` itself. Snapshots now use rooted GC shadow Maps, restore
   the complete `data_cap`, detach snapshot shapes before type/new-property
   mutation, and publish both hidden and public typed-array constructor
   `prototype` slots. This protects the realm-isolation invariant under
   **D4.3.1**, **D5.4.3**, and **D6.2.2v2**.
2. Ordinary top-level `var` declarations were lowered into repeated
   `js_set_module_var`, global-property, and binding-registration MIR calls even
   though the runtime already had a bulk instantiation helper. A table-driven
   **D8.4.3** lowering now emits one bulk call for ordinary script/module vars
   while eval/Annex-B cases retain their specialized lanes. On
   `language/identifiers/start-unicode-10.0.0-escaped.js`, normal-release time
   fell from about 5.30–5.40 s to 0.340–0.364 s; the profile's top-level runtime
   call-site count fell from about 25,004 to 18.
3. Capture-free regex literals between 9 and 1023 bytes were excluded from the
   compile cache. The native-function-source validator evaluated its medium
   Unicode classes about 25,452 times, repeating canonicalization and RE2
   compilation. Capture-free matcher data is now cached while every evaluation
   still creates a distinct RegExp object with fresh `lastIndex` (ECMA-262
   §22.2.3.1). The isolated release-profile test fell from about 2.76 s to
   0.35–0.36 s. `JsOpt.RegexMediumCaptureFreeLoopReusesCompiledMatcher` locks both
   the cache-hit decision and fresh-object state; the capture-bearing fallback
   contract remains green.
4. Logical and conditional MIR joins merged their semantic `Item` result but
   left `last_call_result_reg` naming a path-local RHS/branch register. The
   post-join **D8.4.3** error test could therefore read an undefined stale
   register on the short-circuit edge; in release builds that register
   intermittently contained the bare `ItemError` bit pattern. Logical joins now
   publish the merged result as the carrier, and conditional branches both
   start from the pre-split carrier and publish their merged result at the join.
   `JsOpt.MirLogicalJoinPublishesMergedCarrier` and
   `JsOpt.MirConditionalJoinPublishesMergedCarrier` check the ordinary results
   and finalized-MIR dataflow without pinning register numbers.
5. The context-local URI/ASCII cache had an exact `JsRootRange` descriptor but
   never registered it before publishing cached Items. Under a long hot batch,
   GC could reclaim or relocate the cached `URIError` and later dereference a
   stale pointer. The URI error slow path also created name and message strings
   as unrooted locals across consecutive allocations. The cache now registers
   its range on first use before storing an Item, and both URI decoders use the
   canonical rooted error constructor, enforcing **D5.3.3**. Before this fix,
   the decodeURIComponent stress batch produced ten `CRASH_139` results, killed
   the worker after 69/100 tests, and peaked at 1,371.5 MB RSS; afterwards the
   same batch completed 100/100 on every stability run.

The runtime ownership fixes now have named **D5.3.3** fixtures in
`test_mir_gc_stress_gtest`: global URI/character caches, `for...in` key/result
construction, and RegExp named-group/indices result construction. Each fixture
must match its unstressed output under collect-every plus poison, deterministic
randomized GC plus poison, and collect-every through the MIR interpreter. The
same source fixtures participate in `test_js_mir_emission_gtest`; an additional
**D8.4.3** fixture requires exactly one
`js_init_module_vars_undefined_bulk` call so the batch-lowering path cannot be
silently disabled again.

Nine consecutive post-fix release `make test262-baseline` runs collected all
40,261 baseline results, each with **40,261 fully passing, 0
non-fully-passing, 0 failed, no retry phase, no crash exit, and no killed
batch**. The final minimized-source run completed in **164.2 s** at **677.9 MB**
peak RSS; the immediately preceding three exact trace-hook candidate runs took
160.8 s, 163.9 s, and 163.5 s at 676.6 MB, 678.8 MB, and 678.5 MB. After adding
the focused regression GTests, the ninth validation run completed in 195.6 s
at 676.9 MB peak RSS. The current source passed `make test-lambda-baseline` at
3,713/3,713: JS MIR emission 20/20 and MIR forced-GC stress 66/66 include the
new fixtures. The optimization-contract executable passed 12/12. This closes
the instability regression without weakening a gate. The standalone
`make test-gc-rooting-core` gate also passed: all dynamic root oracles, 45
`NO_GC` imports over 81 call-graph nodes, 14,945 native-function hazard checks,
and the 66/66 corpus sweep. The broader G1/G2/G3 Unified-AST closeout remains
open.

### 13.2 Phase status

| Phase | Status | Evidence |
|---|---|---|
| 0 — measurement/guardrails | completed | Common timing/MIR protocol, GTest parsers, TSV capture summaries, clean five-run Lambda/JS manifests, instrumentation equivalence, and finalized-artifact equivalence are recorded |
| 0.5 — optimization contract testing | core implementation landed; matrix expansion remains | `test_js_opt_gtest` passes 12/12 with profile tracing, including the medium capture-free regex cache/fresh-object contract, D8.4.3 logical and conditional join-carrier contracts, the D5.3.3 rooted URI cache contract, trace-off semantic/finalized-MIR differential, and fail-closed parser checks; `test_mir_gc_stress_gtest` passes 66/66 with named fixtures for all repaired ownership paths; runtime ownership migration and the remaining optimization rows remain |
| 1 — traversal/index/binding | in progress | `AstIndex`, dense node/function identities, common core child visitor, JS function pointer index, and pass-manager prerequisite harness landed; extension catalog/binding migration remain |
| 2 — facts/pass manager | in progress | Typed fact bits/pass manager and `MirValue` demand/contract fields landed; production pass wrapping remains |
| 3 — demand-driven `MirValue` | in progress | Immediate boxed-number reuse is live for indexed JS function/module scopes; full demand propagation and common expression boundaries remain |
| 4 — consolidation/deletion | in progress | Common index/cache/emitter paths and safe lazy policy are live; source-scope deletion audit and baseline gates remain |
| 5 — measured contingency | completed for current profile | Lazy/interpreter policy entered only after phase profiles showed MIR/link dominance; rollback switches remain documented |
| closeout | in progress | Test262 instability is closed at zero non-fully-passing tests and the Lambda baseline is green; final G2/G3 recapture and audited deletion ledger remain |

### 13.3 Gate and diagnostic results

| Gate | Baseline | Candidate | Required | Status |
|---|---:|---:|---:|---|
| G1 runtime LOC | 319,606 | 318,358 (working tree after rejecting unsafe lexical-path memo) | ≤317,606 | open; 752 additional audited runtime/compiler LOC must be deleted before closeout |
| G2 Lambda median `build_transpile_us` | 24,134,804 | 18,685,648 (historical candidate_lazy; 5 complete runs) | candidate/base ≤0.90 | provisional evidence invalidated for closeout; recapture after unsafe lexical-path memo removal |
| G3 JS median `build_transpile_us` | 192,832,974 | 120,444,046 (historical `candidate_final_js`; 5 complete runs) | candidate/base ≤0.80 | provisional evidence invalidated for closeout; recapture after unsafe lexical-path memo removal |
| D4 JS large-library finalized MIR diagnostic | 5,743,247 | 5,008,331 (`candidate_final_js` run 0) | deterministic report; investigate growth | diagnostic |
| D4 JS complete-corpus finalized MIR diagnostic | 7,187,862 | 6,135,408 (`candidate_final_js` run 0) | deterministic report; investigate growth | diagnostic |
| G5 sample/timing integrity | 698 Lambda rows / 324 JS rows, identical sorted manifests | historical captures retained for diagnosis only; incomplete captures are rejected | exact timing manifest | open until post-rejection recapture |
| G0 regressions | current baselines | `make test-lambda-baseline`: input 2104/2104 plus Lambda runtime 1609/1609 (3713/3713 total), including JS MIR 20/20 and forced-GC 66/66; nine consecutive post-fix `make test262-baseline` runs: 40261/40261 fully passing, 0 non-fully-passing, 0 failed, 0 retries/crash exits/killed batches; `test_js_opt_gtest`: 12/12 | Lambda and Test262 baselines green | verified 2026-08-13 after adding the focused GTests; latest Test262 run 195.6 s at 676.9 MB peak RSS; `test/js262/t262_partial.txt` is empty and no Test262 test/runner source was changed |

### 13.4 Deletion ledger

| Phase | Deleted implementation | Removed LOC | Replacement |
|---|---|---:|---|
| 1 | MIR expression-lowering consolidation in the fixed anchor delta | 2,740 net physical runtime LOC | indexed/common lowering helpers |
| 1 | MIR statement-lowering consolidation in the fixed anchor delta | 1,357 net physical runtime LOC | shared statement lowering |
| 1 | JS global/runtime helper consolidation in the fixed anchor delta | 570 + 212 net physical runtime LOC | common global/builtin registry paths |
| 2 | common `AstIndex` visitor, dense node/facts storage, JS function identity index | measured after source audit; new tables count against the candidate | common visitor/index |
| 3 | repeated immediate-number materialization in indexed JS MIR scopes | 522 MIR instructions on Lodash diagnostic artifact (not LOC) | block-local boxed-number cache |
| 4 | remaining anchor-scope cleanup, including DOM/runtime balancing edits | offsets the above deletions; included in the fail-closed counter | common builder/lowering mechanics |
| **Audited net through candidate** | — | **1,248 net deleted since LOC anchor; current counter is 318,358 (delta −1,248)** | hard requirement ≥2,000; 752 audited deletions remain |

---

## 14. Definition of done

This plan is complete only when all statements are true:

- [ ] Lambda and JS compile through one explicit pass manager over one indexed compilation unit (**D8.2.4–D8.2.5**).
- [ ] Core traversal, binding identity, function/class lookup, facts, and graph scheduling each have one authority (**D8.2.4–D8.2.5**).
- [ ] Declared contracts and inferred/effective facts are separate (**D3.2.3**).
- [ ] Both expression pipelines use full-contract `MirValue`; demand-driven lowering has a safe generic fallback (**D8.2.6**, **D2.4.1–D2.4.3**).
- [ ] Root/final-store policy exists only in `MirEmitter` (**D5.3.4**).
- [ ] Language profiles contain semantic differences, not duplicate pass schedules or core walks (**D8.2.1–D8.2.5**).
- [ ] Old walkers, linear rediscovery, quadratic propagation, bare-register core APIs, and compatibility lowering paths are deleted.
- [ ] G1 reports at least 2,000 net physical runtime/compiler LOC removed (**D8.6.4v2**).
- [ ] G2 reports at least 10% lower Lambda GTest compiler time (**D8.6.4v2**).
- [ ] G3 reports at least 20% lower JS GTest compiler time (**D8.6.4v2**).
- [ ] D4 reports deterministic finalized-MIR volume for the frozen JS large-library cohort and complete corpus; investigate material growth (**D8.6.4v2**).
- [ ] G5 proves identical, complete, deterministic release-mode timing manifests; MIR manifests remain attached as diagnostics (**D8.6.4v2**).
- [ ] Optimization Contract Testing passes: each material tuning path has a positive and guard/fallback fixture with deterministic `JsOptTrace` events; trace-on/trace-off output, errors, and finalized MIR are identical (**D8.2.5–D8.2.6**, **D8.6.4v2**).
- [ ] G0 and the entire §11 matrix are green with no weakened ratchets. Current evidence has `make test-lambda-baseline` green at 3713/3713 (input 2104/2104 plus Lambda runtime 1609/1609, including JS MIR 20/20 and forced-GC 66/66), nine consecutive post-fix `make test262-baseline` runs green at 40261/40261 with zero non-fully-passing tests, retries, crash exits, or killed batches, and `test_js_opt_gtest` green at 12/12; the remaining §11 commands still require final closeout execution.
- [ ] §13 contains the final commits, raw-capture locations, medians, phase attribution, LOC ledger, and verified test results.

Until every item is checked, the unified-AST tuning continuation remains open.
