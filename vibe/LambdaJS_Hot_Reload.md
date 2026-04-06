# LambdaJS Hot Reload Design — Future Stages

Incremental compilation for the JS engine, enabling near-instant re-execution on code changes. Primary application: test262 batch mode (27k tests sharing common harness). Secondary application: REPL and live development.

## Completed Work

**Stage 1 (Two-Module MIR Split)** is fully implemented. See [`vibe/Transpile_Js21.md`](Transpile_Js21.md) for details.

| Stage | Status | Result |
|-------|--------|--------|
| 1.0 Persistent heap reuse | **DONE** | ~6s faster, state leakage issue |
| 1.1 Harness pre-compilation | **DONE** | Preamble mode, harness compiled once per batch |
| 1.2 Test compilation against harness | **DONE** | Pre-seeds module_consts from preamble snapshot |
| Crash recovery | **DONE** | 3 crash root causes fixed, 0 MISSING tests |

**Current performance**: Phase 2 clean batch in **79.8s** (was 126.7s, **-37%**), 12,296 tests passing (45.5% pass rate).

## Remaining Design: Incremental Append Model

The next stages build on the two-module split by incrementalizing the per-test compilation pipeline further. Instead of full parse → AST → MIR for each test, the system will reuse harness artifacts and only process the appended test portion.

```
┌─────────────────────────────────────────────────────────┐
│ Already done (Stage 1):                                 │
│   parse(harness) → AST(harness) → MIR(harness) →       │
│   compile(harness) → preamble checkpoint                │
├─────────────────────────────────────────────────────────┤
│ Current per-test (Stages 2-4 will optimize):            │
│   parse(test_only)        ← Stage 2: incremental parse  │
│   build_ast(test_only)    ← Stage 3: incremental AST    │
│   MIR gen(test_only)      ← Stage 4: incremental MIR    │
│   MIR_link + MIR_gen (test module)                      │
│   execute js_main                                       │
│   restore to preamble checkpoint                        │
└─────────────────────────────────────────────────────────┘
```

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
| 1.0–1.2 | Two-Module MIR Split | **DONE** | See [Transpile_Js21.md](Transpile_Js21.md) |
| 2 | Incremental Tree-sitter parsing | NOT STARTED | Low risk, ~5-10% additional reduction |
| 3 | Incremental AST building | NOT STARTED | Medium risk, ~3-5% additional reduction |
| 4 | Incremental MIR generation | NOT STARTED | Medium-high risk, ~15-20% additional reduction |
| 5 | General hot reload (REPL/dev) | NOT STARTED | Highest complexity |

## Roadmap

```
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

Stages 2-3 can be developed independently. Stage 4 builds on 2-3. Stage 5 builds on all prior stages.

## Estimated Cumulative Savings (test262)

| Stage | Phase 2 time (est.) | Cumulative reduction |
|-------|-------------------|---------------------|
| After Stage 1 (current) | 79.8s | baseline |
| After Stage 2 | ~72s | ~10% |
| After Stage 2+3 | ~69s | ~14% |
| After Stage 2-4 | ~55s | ~31% |
