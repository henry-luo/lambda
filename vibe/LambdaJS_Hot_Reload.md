# LambdaJS Hot Reload Design

Incremental compilation for the JS engine, enabling near-instant re-execution on code changes. Primary application: test262 batch mode (27k tests sharing common harness). Secondary application: REPL and live development.

## Status: PARTIALLY IMPLEMENTED

See [Implementation Status](#implementation-status) below for per-stage breakdown.

## Problem

The current `js-test-batch` pipeline repeats the full compilation for every test:

```
For each of 27k tests:
  concat(sta.js + assert.js + includes + test.js)   ~5-17KB
  → Tree-sitter parse (full)                         ~0.2ms
  → build_js_ast (full)                               ~0.1ms
  → jm_collect_functions (full)                        ~0.1ms
  → Phase 1.1-1.3: module const/var scan (full)        ~0.05ms
  → Phase 2: define all functions to MIR (full)        ~0.3ms
  → Phase 3: emit js_main to MIR (full)               ~0.2ms
  → MIR_link + MIR_gen (full JIT)                      ~0.5ms
  → execute js_main                                    varies
  → destroy MIR context + reset heap                   ~0.1ms
```

The harness (sta.js + assert.js) defines `Test262Error`, `assert`, `assert.sameValue`, `assert.notSameValue`, `assert.throws`, `$DONOTEVALUATE`, etc. These are identical across all tests but get recompiled 27k times.

## Design: Incremental Append Model

The core idea: treat test execution like **hot reloading a script that grows**. The common harness is the base document. Each test appends to it. The system incrementally processes only the appended portion at each stage.

```
┌─────────────────────────────────────────────────────────┐
│ Once (batch startup):                                   │
│   parse(harness) → AST(harness) → MIR(harness) →       │
│   compile(harness) → register_dynamic_import(...)       │
│   checkpoint: save parse tree, AST, compilation state   │
├─────────────────────────────────────────────────────────┤
│ Per test:                                               │
│   append(test_source) to source buffer                  │
│   ts_tree_edit + incremental re-parse (only new tokens) │
│   incremental AST build (only new statements)           │
│   incremental MIR gen (only new functions + js_main)    │
│   MIR_link + MIR_gen (test module only, imports harness)│
│   execute js_main                                       │
│   restore checkpoint                                    │
└─────────────────────────────────────────────────────────┘
```

## Stage 1: Two-Module MIR Split

**Status: IMPLEMENTED** — Persistent heap reuse (1.0) and two-module MIR split (1.1–1.2) are both implemented.

**Goal**: Compile harness MIR once, compile each test's MIR separately, link via dynamic imports.

This is the highest-value, lowest-risk change. No incremental parsing needed yet.

### 1.0 Persistent Heap Reuse (IMPLEMENTED)

A simpler precursor to the full two-module split. The batch process reuses a single `EvalContext` (heap, nursery, name_pool) across all tests instead of creating/destroying one per test.

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

**Known issue:** Persistent heap causes test state to bleed across tests (132 test regressions vs normal mode). A full fix requires either better state isolation or the proper two-module split from Stage 1.1–1.2.

### 1.1 Harness Pre-Compilation (IMPLEMENTED)

At batch process startup, the first item in each manifest is `harness:<byte_count>` followed by the concatenated sta.js + assert.js source. The batch worker:

1. Reads harness source via `fread(buf, 1, length, stdin)`
2. Calls `transpile_js_to_mir_preamble()` which:
   - Parses harness, builds AST, collects functions
   - Uses **preamble mode** (`mt->preamble_mode = true`): function declarations are registered as `MCONST_MODVAR` (with `js_set_module_var()` persistence) instead of `MCONST_FUNC` (which would be lost when `js_main` returns)
   - Generates MIR, links, compiles with JIT, executes `js_main`
   - Snapshots `module_consts` entries and `module_var_count` into `JsPreambleState`
   - Does NOT call `MIR_finish()` — keeps MIR context alive so compiled function code pointers remain valid
3. Saves checkpoint: `preamble_var_checkpoint = js_get_module_var_count()`

**Key design decision**: The original Stage 1.1 plan proposed `register_dynamic_import()` for cross-module references. The actual implementation uses a simpler approach: preamble mode forces function declarations to module variables, and subsequent test compilations pre-seed their `module_consts` hashmap from the preamble's snapshot. No dynamic import resolver needed.

### 1.2 Test Compilation Against Harness (IMPLEMENTED)

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

### 1.3 Implementation Details (IMPLEMENTED)

**Files modified:**

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

**Measured results (debug build, -O3 MIR, 8 parallel workers):**

| Metric | Before (Stage 1.0 only) | After (Stage 1.1-1.2) | Change |
|--------|------------------------|----------------------|--------|
| Phase 2 (clean batch) | 126.7s | 79.8s | **-37%** |
| Tests passing | 10,264 | 12,296 | +2,032 |
| Improvements vs baseline | 1,345 | 2,250 | +905 |
| Regressions vs old baseline | 1,644 | 218 | -1,426 |
| Phase 2a (quarantined) | 1.9s | 1.9s | same |
| MISSING tests | 0 | 0 | same |
| Quarantined crashers | 233 | 233 | same |

Note: The passing count increase (10,264 → 12,296) includes engine improvements made between runs, not solely from the preamble change. The preamble itself does not change test semantics — it only changes how harness bindings are delivered (pre-compiled module vars vs per-test concatenation). The 218 regressions vs old baseline are pre-existing engine regressions confirmed by A/B testing individual tests in both modes.

## Stage 2: Incremental Tree-sitter Parsing

**Status: NOT IMPLEMENTED**

**Goal**: Re-parse only the appended test portion instead of the full source.

**Prerequisite**: Stage 1 complete.

### 2.1 How Tree-sitter Incremental Parsing Works

Tree-sitter supports editing a parse tree and re-parsing incrementally:

```c
// 1. Parse harness once:
TSTree* tree = ts_parser_parse_string(parser, NULL, harness_source, harness_len);

// 2. For each test, "append" by editing the tree:
TSInputEdit edit = {
    .start_byte     = harness_len,
    .old_end_byte   = harness_len,     // nothing was there before
    .new_end_byte   = harness_len + test_len,  // now test is there
    .start_point    = harness_end_point,
    .old_end_point  = harness_end_point,
    .new_end_point  = new_end_point,
};
ts_tree_edit(tree, &edit);

// 3. Re-parse with new full source + old tree:
// combined_source = harness_source + test_source
TSTree* new_tree = ts_parser_parse_string(parser, tree, combined_source, total_len);

// Tree-sitter reuses all harness nodes, only parses the new test portion.
// 4. After test: restore tree to harness-only state for next test.
```

### 2.2 Source Buffer Management

Maintain a single buffer:

```
┌──────────────────┬──────────────────┐
│  harness source  │  test source     │
│  (fixed prefix)  │  (swapped each)  │
└──────────────────┴──────────────────┘
0             harness_len        harness_len + test_len
```

For each test:
1. Overwrite bytes from `harness_len` onward with new test source
2. Call `ts_tree_edit` with the appropriate edit range
3. Re-parse with old tree → Tree-sitter incrementally processes only the appended region

### 2.3 Implementation

- **`lambda/js/js_scope.cpp`** — New function:
  ```cpp
  // Incremental re-parse: appends new source at offset, returns updated tree.
  // Reuses existing tree nodes for the unchanged prefix.
  bool js_transpiler_parse_incremental(
      JsTranspiler* tp,
      const char* full_source,
      size_t full_length,
      size_t unchanged_prefix_length,
      TSTree* old_tree);
  ```

- **Existing** `js_transpiler_parse` passes `NULL` for `old_tree`. The incremental version passes the harness tree.

**Risk**: Low. Tree-sitter's incremental parsing is a core feature, well-tested. Append-at-end is the simplest edit pattern (no rebalancing of existing nodes).

**Expected savings**: Additional ~5-10% beyond Stage 1. Parsing is fast to begin with (~0.2ms), but eliminating 27k × 5KB of redundant harness tokenization adds up.

## Stage 3: Incremental AST Building

**Status: NOT IMPLEMENTED**

**Goal**: Build AST nodes only for the new test statements, reuse harness AST.

### 3.1 How It Works

The current `build_js_program()` iterates all `ts_node_named_child_count()` children of the Program node. With incremental parsing, Tree-sitter marks which CST nodes changed. We can:

1. Save harness AST: `harness_last_stmt` (tail of the statement linked list)
2. For each test:
   - Incremental parse produces updated Program node
   - Count total children; harness children haven't changed
   - Start building AST from child index `harness_child_count` onward
   - Append new AST statements to `harness_last_stmt->next`
3. After test:
   - Set `harness_last_stmt->next = NULL` (detach test AST)
   - Free test AST nodes

### 3.2 Implementation

- **`lambda/js/build_js_ast.cpp`** — New function:
  ```cpp
  // Build AST incrementally: only process Program children starting from
  // child_offset. Appends new statements to prev_tail->next.
  // Returns the new tail of the statement list.
  JsAstNode* build_js_program_incremental(
      JsTranspiler* tp,
      TSNode program_node,
      uint32_t child_offset,      // skip this many children (harness)
      JsAstNode* prev_tail);      // append new statements here
  ```

- The allocator for AST nodes (`alloc_js_ast_node`) uses the transpiler's pool. Need a checkpoint/restore on this pool to free test-only AST nodes between tests.

**Risk**: Medium. AST nodes are allocated from a pool in `JsTranspiler`. Need to ensure the pool supports rollback. Also need to verify that no test AST nodes are referenced after restore.

**Expected savings**: Additional ~3-5% beyond Stage 2.

## Stage 4: Incremental MIR Generation

**Status: NOT IMPLEMENTED**

**Goal**: Skip MIR generation for harness functions; only generate MIR for the test's functions and a new `js_main`.

### 4.1 How It Works

The MIR transpiler (`transpile_js_mir_ast`) has four phases:

| Phase | What it does | Incremental approach |
|-------|-------------|---------------------|
| Phase 1: `jm_collect_functions` | Walk full AST, collect inner functions | Skip harness subtrees, only collect from new test statements |
| Phase 1.1: Module consts scan | Scan top-level const declarations | Skip harness consts (already in saved `module_consts` map) |
| Phase 2: `jm_define_function` | Emit MIR for each collected function | Only define test functions (harness functions already compiled in Stage 1) |
| Phase 3: Emit `js_main` | Walk top-level statements, emit MIR | Only walk test statements; harness side-effects already executed |

### 4.2 Implementation

- **`lambda/js/transpile_js_mir.cpp`** — New function:
  ```cpp
  // Incremental MIR generation: processes only the test portion of the AST.
  // harness_state contains the saved function collection, module consts,
  // and variable scope from the harness compilation.
  void transpile_js_mir_ast_incremental(
      JsMirTranspiler* mt,
      JsAstNode* first_test_stmt,   // first statement after harness
      const JsMirHarnessState* harness_state);
  ```

- **`JsMirHarnessState`** captures:
  ```cpp
  struct JsMirHarnessState {
      int func_count;                     // harness function count in func_entries[]
      int class_count;                    // harness class count
      struct hashmap* module_consts;      // harness const bindings (shared, not cloned)
      int module_var_count;               // harness module var count
      // var_scopes[0] with harness global bindings
      struct hashmap* global_scope_snapshot;
  };
  ```

**Risk**: Medium-high. The MIR transpiler has deep interdependencies between phases (e.g., Phase 1 function collection builds `func_entries[]` used by Phase 2 and Phase 3 variable resolution). Incrementalizing Phase 1 requires careful offset management.

**Expected savings**: Additional ~15-20% beyond Stage 3. This is where the big gains are after Stage 1 — avoiding MIR emission for harness functions (assert.sameValue alone generates dozens of MIR instructions).

## Stage 5: General Hot Reload (REPL / Dev Mode)

**Status: NOT IMPLEMENTED**

**Goal**: Apply the incremental pipeline to interactive development.

### 5.1 REPL

Each REPL line is an "append" to the running program:

```
λ> const x = 42              → append + incremental compile + execute
λ> function greet(n) { ... } → append + incremental compile + execute
λ> greet("world")            → append + incremental compile + execute
```

The entire session history is the "harness" (checkpoint), and each new line is the "test" (appended, executed, then merged into checkpoint).

**Difference from test262**: In REPL, each successful execution advances the checkpoint (the new bindings become permanent). In test262, the checkpoint rewinds after each test.

### 5.2 Live File Editing

For `lambda.exe view` mode with live file watching:

```
1. Initial load: parse + compile + render
2. File change detected:
   - Compute edit delta (new bytes, edit range)
   - ts_tree_edit(old_tree, &edit)
   - Incremental re-parse
   - Diff old vs new AST to find changed functions/statements
   - Re-transpile only changed functions
   - Re-link MIR (changed functions replace old ones)
   - Re-execute
```

This is significantly more complex than append-only because edits can occur anywhere in the document. The Tree-sitter incremental parser handles this, but AST diffing and selective MIR regeneration require a "function-level granularity" approach where each function is an independently compilable unit.

### 5.3 Implementation Sketch

- **`lambda/js/js_hot_reload.hpp`** — New module:
  ```cpp
  struct HotReloadSession {
      TSParser* parser;
      TSTree* tree;                 // current parse tree
      JsAstNode* ast;              // current full AST
      JsMirTranspiler* mt;         // persistent transpiler state
      MIR_context_t harness_ctx;   // long-lived MIR context
      
      // Checkpoint for rollback (test262 mode)
      HeapCheckpoint checkpoint;
      int module_var_checkpoint;
      JsAstNode* checkpoint_last_stmt;
      uint32_t checkpoint_child_count;
      
      // Methods
      bool init(Runtime* runtime, const char* base_source, size_t base_len);
      Item execute_appended(const char* new_source, size_t new_len);
      void rollback();    // test262: rewind to checkpoint
      void advance();     // REPL: merge into checkpoint
  };
  ```

## Implementation Status

| Stage | Description | Status | Notes |
|-------|------------|--------|-------|
| 1.0 | Persistent heap reuse | **DONE** | ~6s faster but 132 test regressions from state leakage |
| 1.1 | Harness pre-compilation | **DONE** | Preamble mode compiles harness once per batch |
| 1.2 | Test compilation against harness | **DONE** | Pre-seeds module_consts from preamble snapshot |
| 2 | Incremental Tree-sitter parsing | NOT STARTED | Depends on Stage 1 |
| 3 | Incremental AST building | NOT STARTED | Depends on Stage 2 |
| 4 | Incremental MIR generation | NOT STARTED | Depends on Stage 3 |
| 5 | General hot reload (REPL/dev) | NOT STARTED | Depends on Stages 1-4 |

### Other Infrastructure Implemented

- **Metadata cache** (`utils/generate_test262_metadata.py` + C++ loader): TSV-based test metadata cache for fast test discovery. Auto-generates on first run.
- **Crasher quarantine** (`test/test_js_test262_gtest.cpp`): Known crashers from `temp/_t262_crashers.txt` are run separately in small batches (Phase 2a) to avoid collateral batch failures. Reduces Phase 2b retry from ~68s to ~7s.
- **`--no-hot-reload` flag**: CLI flag in both `lambda.exe` and test262 gtest to disable persistent heap reuse for A/B comparison.

### Batch Mode Crash Recovery (IMPLEMENTED)

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

**Results (debug build, -O3 MIR, 8 parallel workers):**

| Metric | Before fixes | After fixes |
|--------|-------------|-------------|
| Phase 2 (clean batch) | 258.2s (26,768 tests) | 126.7s (26,782 tests) |
| Phase 2a (quarantined) | 4.2s (247 crashers) | 1.9s (233 crashers) |
| MISSING tests (Phase 2) | hundreds | **0** |
| Quarantined crashers | 247 | 233 |
| Tests passing | 10,264 | 10,264 |
| Improvements vs old baseline | 1,245 | 1,353 |

## Implementation Order

```
Stage 1.0: Persistent Heap Reuse           ← DONE (minimal speedup, state leakage issue)
Stage 1.1-1.2: Two-Module MIR Split        ← DONE (Phase 2: 126.7s → 79.8s, -37%)
  └── transpile_js_to_mir_preamble() + transpile_js_to_mir_with_preamble()
  └── test262 batch: 37% reduction achieved

Stage 2: Incremental Tree-sitter Parse     ← low risk, leverages existing API
  └── js_transpiler_parse_incremental()
  └── additional ~5-10% reduction

Stage 3: Incremental AST Build             ← medium risk, pool checkpoint needed
  └── build_js_program_incremental()
  └── additional ~3-5% reduction

Stage 4: Incremental MIR Generation        ← medium-high risk, deep transpiler changes
  └── transpile_js_mir_ast_incremental(), JsMirHarnessState
  └── additional ~15-20% reduction

Stage 5: General Hot Reload                ← highest complexity, broadest impact
  └── HotReloadSession, REPL integration, file watching
  └── sub-second reload for REPL and live dev
```

Stages 1-2 can be developed and shipped independently. Stages 3-4 build on 1-2. Stage 5 builds on all prior stages.

## Combined Savings Estimate (test262)

| Stage | Cumulative reduction | Tests/sec (est.) |
|-------|---------------------|-------------------|
| Current (Stage 1.0 only) | baseline (126.7s) | ~211/sec |
| After Stage 1.1-1.2 | **37%** | ~335/sec |
| After Stage 1+2 | ~42% (est.) | ~365/sec |
| After Stage 1-4 | ~55-65% (est.) | ~470/sec |
