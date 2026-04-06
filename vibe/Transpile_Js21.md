# JavaScript Transpiler v21: Test262 Batch Optimization (Two-Module MIR Split)

## 1. Executive Summary

This version focused on **test262 batch execution performance** — optimizing the GTest runner infrastructure rather than JS engine compliance. The main achievement is the **Two-Module MIR Split** (harness pre-compilation), which compiles the test262 harness once per batch instead of once per test.

### Baseline (v20)

| Metric | Value |
|--------|-------|
| Total test262 tests | 38,649 |
| Skipped (unsupported features) | 11,634 |
| Executed | 27,015 |
| Passed | 10,264 (38.0%) |
| Phase 2 (clean batch) | 126.7s |
| Quarantined crashers | 233 |
| MISSING tests | 0 |

### Current (v21)

| Metric | Value |
|--------|-------|
| Total test262 tests | 38,649 |
| Skipped (unsupported features) | 11,634 |
| Executed | 27,015 |
| Passed | **12,296 (45.5%)** |
| Phase 2 (clean batch) | **79.8s (-37%)** |
| Phase 2a (quarantined) | 1.9s |
| Quarantined crashers | 233 |
| MISSING tests | 0 |

Note: The passing count increase (10,264 → 12,296) includes engine improvements made between v20 and v21 runs, not solely from the batch optimization. The preamble does not change test semantics — it only changes how harness bindings are delivered.

## 2. Implementation

### 2.1 Persistent Heap Reuse (Stage 1.0)

A precursor to the full two-module split. The batch process reuses a single `EvalContext` (heap, nursery, name_pool) across all tests instead of creating/destroying one per test.

**What was implemented:**
- **`lambda/main.cpp`** (`js-test-batch` handler): When `hot_reload=true` (default), pre-initializes a persistent `EvalContext batch_context` with heap/nursery/name_pool before the test loop. Each test reuses this context. `js_batch_reset()` clears JS runtime state between tests. On timeout, the context is fully destroyed and recreated.
- **`lambda/js/js_runtime.cpp`**: Added `js_get_module_var_count()` and `js_batch_reset_to(int count)` for checkpoint/restore of module variable count.
- **`lambda/js/transpile_js_mir.cpp`**: `reusing_context` fast-path (~line 18685) — when `old_context->heap != NULL`, reuses the existing heap instead of allocating a new one.
- **`lambda/main.cpp`**: Added `--no-hot-reload` CLI flag to disable heap reuse for A/B comparison.
- **`test/test_js_test262_gtest.cpp`**: Added `g_no_hot_reload` global and pass-through of `--no-hot-reload` to child processes via posix_spawn argv.

**Measured results (A/B comparison):**
- Hot reload ON: 2:56 wall time, 10,256 passed
- Hot reload OFF: 3:02 wall time, 10,388 passed
- Heap reuse saves ~6 seconds but causes 132 fewer tests to pass due to state leakage between tests.

**Known issue:** Persistent heap causes test state to bleed across tests (132 test regressions vs normal mode). Resolved by the full two-module split in Stage 1.1–1.2.

### 2.2 Harness Pre-Compilation (Stage 1.1)

At batch process startup, the first item in each manifest is `harness:<byte_count>` followed by the concatenated sta.js + assert.js source. The batch worker:

1. Reads harness source via `fread(buf, 1, length, stdin)`
2. Calls `transpile_js_to_mir_preamble()` which:
   - Parses harness, builds AST, collects functions
   - Uses **preamble mode** (`mt->preamble_mode = true`): function declarations are registered as `MCONST_MODVAR` (with `js_set_module_var()` persistence) instead of `MCONST_FUNC` (which would be lost when `js_main` returns)
   - Generates MIR, links, compiles with JIT, executes `js_main`
   - Snapshots `module_consts` entries and `module_var_count` into `JsPreambleState`
   - Does NOT call `MIR_finish()` — keeps MIR context alive so compiled function code pointers remain valid
3. Saves checkpoint: `preamble_var_checkpoint = js_get_module_var_count()`

**Key design decision**: The original plan proposed `register_dynamic_import()` for cross-module references. The actual implementation uses a simpler approach: preamble mode forces function declarations to module variables, and subsequent test compilations pre-seed their `module_consts` hashmap from the preamble's snapshot. No dynamic import resolver needed.

### 2.3 Test Compilation Against Harness (Stage 1.2)

For each test script (arriving as `source:<name>:<byte_count>` in the manifest — harness NOT prepended):

1. Reads test source via `fread`
2. Calls `transpile_js_to_mir_with_preamble(source, filename, &preamble)` which:
   - Parses test source only (no harness)
   - Builds AST from test parse tree
   - During `transpile_js_mir_ast`, Phase 1.1 pre-seeds `module_consts` from `preamble->entries` (harness bindings like `_js_assert → MCONST_MODVAR index=3`)
   - Phase 1.1 third pass starts `module_var_count` from `preamble->module_var_count` (not 0)
   - Phase 2: defines only test functions as MIR
   - Phase 3: emits `js_main` that references harness vars via `js_get_module_var(index)`
   - Creates a new MIR context for the test (harness MIR context stays alive)
   - Links, compiles, executes test's `js_main`
   - Destroys test's MIR context
3. Restores: `js_batch_reset_to(preamble_var_checkpoint)` — truncates module vars back to harness-only state

**Crash recovery**: When a test crashes (SIGSEGV/SIGBUS/timeout), the heap is destroyed → harness function objects are gone. Recovery: `preamble_state_destroy(&preamble)` + `has_preamble = false`. The next `harness:` line in the manifest (at the start of a new batch) re-compiles the preamble.

### 2.4 Files Modified

- **`lambda/js/transpile_js_mir.cpp`**:
  - Added `preamble_mode`, `preamble_entries`, `preamble_entry_count`, `preamble_var_count` fields to `JsMirTranspiler`
  - Phase 1.1e: In preamble mode, function declarations register as `MCONST_MODVAR` + `module_var_count++` (instead of `MCONST_FUNC`)
  - Phase 3: In preamble mode, after `jm_set_var()`, also emits `js_set_module_var(index, var_reg)` to persist function objects
  - Phase 1.1: Pre-seeds `module_consts` from preamble entries when compiling with-preamble
  - Added static globals for preamble state transfer: `g_jm_preamble_mode`, `g_jm_preamble_out`, `g_jm_preamble_in`
  - New API functions: `transpile_js_to_mir_preamble()`, `transpile_js_to_mir_with_preamble()`, `preamble_state_destroy()`
  - Conditional `js_reset_module_vars()` skip and `MIR_finish()` skip for preamble mode

- **`lambda/js/js_transpiler.hpp`**:
  - Added `JsPreambleState` struct: `void* mir_ctx`, `int module_var_count`, `JsModuleConstEntry* entries`, `int entry_count`
  - Declarations for the three preamble API functions

- **`lambda/transpiler.hpp`**: Forward declarations for `JsPreambleState` and preamble API

- **`lambda/main.cpp`** — `js-test-batch` handler:
  - Added `harness:<length>` protocol handler: reads harness source, calls `transpile_js_to_mir_preamble()`, saves checkpoint
  - Conditional dispatch: `has_preamble ? transpile_js_to_mir_with_preamble(...) : transpile_js_to_mir(...)`
  - Crash recovery: `preamble_state_destroy()` + `has_preamble = false`; next batch re-compiles preamble
  - Normal reset: `js_batch_reset_to(preamble_var_checkpoint)` when preamble active

- **`test/test_js_test262_gtest.cpp`**:
  - Added `assemble_harness_source()` — concatenates sta.js + assert.js (5,315 bytes with UTF-8 guillemets)
  - Added `assemble_test_source()` — includes + strict prefix + test body (no harness)
  - Modified `execute_t262_batch()`: writes `harness:<len>` at start of each manifest, then `source:<name>:<len>` per test
  - `assemble_combined_source()` retained for Phase 2a crasher quarantine (still uses full concatenation)

- **`lambda/js/js_runtime.cpp`** — Pre-existing helpers used:
  - `js_get_module_var_count()`: checkpoint current module var count
  - `js_batch_reset_to(int count)`: partial reset preserving vars up to checkpoint

## 3. Batch Mode Crash Recovery

Three classes of batch-mode crashes were identified and fixed, eliminating all MISSING test results from the main Phase 2 run.

**Root cause 1 — Concise arrow exception landing pad** (`lambda/js/transpile_js_mir.cpp`):
Concise arrow bodies used `goto finish_boxed` which skipped emitting the `func_except_label` landing pad. When `jm_emit_exc_propagate_check()` created a `BT → func_except_label` branch during `jm_transpile_box_item()`, the label was allocated but never inserted into the instruction list. During MIR's `process_inlines`, `redirect_duplicated_labels` set the branch target to NULL → SIGSEGV in `build_func_cfg`. **Fix**: emit exception landing pad before `goto finish_boxed`. Result: 12 crashers fixed (247 → 235 quarantined), 2 `assignmenttargettype` tests now pass.

**Root cause 2 — MIR compilation error exits process** (`lambda/main.cpp` + `lambda/js/transpile_js_mir.cpp`):
`new Function()` with duplicate parameters triggers MIR "Repeated reg declaration" error → default handler calls `exit(1)`, killing the batch worker. **Fix**: `batch_mir_error_handler` using `longjmp` recovery, installed via global `g_batch_mir_error_handler` after each `jit_init()` call (3 locations: main transpiler, `js_new_function_from_string`, `jm_compile_js_module`). Note: `MIR_init()` resets the error handler on each call, so installation must occur after `jit_init()`.

**Root cause 3 — Runtime SIGSEGV in JIT code** (`lambda/main.cpp`):
Some tests trigger runtime SIGSEGV/SIGBUS in JIT-compiled code (e.g., wrong method dispatch in `js_date_setter`). **Fix**: `batch_crash_handler` using `sigsetjmp/siglongjmp` with SIGSEGV/SIGBUS signal handlers installed around the batch execution loop. Signal handlers are restored after each test.

**MIR NULL-label guard** (`mac-deps/mir/mir-gen.c`):
Added NULL guard in `build_func_cfg()` to skip instructions with NULL label operands instead of crashing. Defence-in-depth against any remaining unreachable-label edge cases during MIR inlining.

**MIR -O3 restoration**: During debugging, `mir-gen.o` was temporarily compiled with `-O0`. After fixes were complete, MIR was rebuilt with `-O3`, yielding a 2× Phase 2 speedup (258s → 127s).

## 4. Other Infrastructure

- **Metadata cache** (`utils/generate_test262_metadata.py` + C++ loader): TSV-based test metadata cache for fast test discovery. Auto-generates on first run.
- **Crasher quarantine** (`test/test_js_test262_gtest.cpp`): Known crashers from `temp/_t262_crashers.txt` are run separately in small batches (Phase 2a) to avoid collateral batch failures.
- **`--no-hot-reload` flag**: CLI flag in both `lambda.exe` and test262 gtest to disable persistent heap reuse for A/B comparison.
- **`--update-baseline` flag**: Updates `test/js/test262_baseline.txt` after a full run.

## 5. Results

**Performance (debug build, -O3 MIR, 8 parallel workers):**

| Metric | v20 (before) | v21 (after) | Change |
|--------|-------------|-------------|--------|
| Phase 2 (clean batch) | 126.7s | 79.8s | **-37%** |
| Phase 2a (quarantined) | 1.9s | 1.9s | same |
| MISSING tests | 0 | 0 | same |
| Quarantined crashers | 233 | 233 | same |

**Compliance:**

| Metric | v20 (before) | v21 (after) | Change |
|--------|-------------|-------------|--------|
| Tests passing | 10,264 | 12,296 | +2,032 |
| Pass rate (of executed) | 38.0% | 45.5% | +7.5pp |
| Improvements vs baseline | — | 2,250 | — |
| Regressions vs old baseline | — | 218 | — |

Record in `test/js/test262_results.json`:
```json
{
  "date": "2026-04-06",
  "version": "v21 (two-module MIR split + preamble + crash recovery)",
  "summary": {
    "total": 38649,
    "passed": 12296,
    "failed": 14719,
    "skipped": 11634,
    "pass_rate": 45.52
  }
}
```

## 6. Future Optimization Stages

Further batch speedups are tracked in [`vibe/LambdaJS_Hot_Reload.md`](LambdaJS_Hot_Reload.md) (Stages 2–5: incremental parsing, incremental AST, incremental MIR, general hot reload).
