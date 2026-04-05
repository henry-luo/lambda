# LambdaJS Hot Reload Design

Incremental compilation for the JS engine, enabling near-instant re-execution on code changes. Primary application: test262 batch mode (27k tests sharing common harness). Secondary application: REPL and live development.

## Status: PROPOSAL

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

**Goal**: Compile harness MIR once, compile each test's MIR separately, link via dynamic imports.

This is the highest-value, lowest-risk change. No incremental parsing needed yet.

### 1.1 Harness Pre-Compilation

At batch process startup (before reading any test from the manifest):

```
1. Concatenate sta.js + assert.js into harness_source
2. transpile_js_to_mir(harness_source)
   → Parse, build AST, collect functions, generate MIR module "harness"
   → MIR_link + MIR_gen: compiles all harness functions to native code
3. Register all harness functions via register_dynamic_import():
   - "assert_sameValue" → native ptr
   - "assert_throws" → native ptr  
   - "Test262Error_ctor" → native ptr
   - etc.
4. Execute harness js_main to initialize globals:
   - Populates js_module_vars[] with assert object, Test262Error, etc.
5. Save checkpoint:
   - harness_module_var_count = current module var count
   - harness_nursery_pos = nursery bump pointer position
   - harness_mir_ctx stays alive (compiled code must persist)
```

### 1.2 Test Compilation Against Harness

For each test script:

```
1. Parse test source only (no harness prepended)
2. Build AST from test parse tree
3. During MIR transpilation (transpile_js_mir_ast):
   - jm_find_var("_js_assert") → finds harness module var index
   - jm_collect_functions → only finds test functions
   - Phase 2: define only test functions as MIR
   - Phase 3: emit js_main that references harness vars via js_get_module_var()
4. MIR_link resolves test's imports against harness via import_resolver
5. MIR_gen compiles only the test module (smaller, faster)
6. Execute test's js_main
7. Restore:
   - Truncate js_module_vars[] to harness_module_var_count
   - Reset nursery to harness_nursery_pos (or full destroy + recreate)
   - Destroy test's MIR context (harness MIR context stays)
```

### 1.3 Implementation Details

**Files to modify:**

- **`lambda/main.cpp`** — `js-test-batch` handler:
  - New manifest protocol: `harness:<length>\n<bytes>\n` before `source:` entries
  - Harness compilation at startup, checkpoint save
  - Restore after each test instead of full `runtime_reset_heap`

- **`lambda/js/transpile_js_mir.cpp`** — New function:
  ```cpp
  // Transpile and execute a test script against an existing harness environment.
  // The harness module vars, functions, and compiled code are already present.
  // Only the test's new code is parsed, transpiled, compiled, and executed.
  Item transpile_js_test_against_harness(
      Runtime* runtime,
      const char* test_source,
      const char* filename,
      int harness_module_var_count);
  ```
  Inside, `transpile_js_mir_ast` already iterates `program->body` for module vars and function hoisting. The test's top-level code references harness vars via `jm_find_var()` which searches `module_consts` and `var_scopes` — these would either:
  - (a) Pre-populate `module_consts` with harness bindings (e.g. `_js_assert → MCONST_MODVAR, index=3`), or
  - (b) Generate `js_get_module_var(index)` calls for unresolved identifiers that exist in harness scope

- **`lambda/js/js_runtime.cpp`** — New functions:
  ```cpp
  int js_get_module_var_count(void);               // checkpoint: how many vars exist
  void js_truncate_module_vars(int count);          // restore: discard vars beyond count
  void js_batch_reset_partial(int harness_count);   // reset test state, keep harness
  ```

- **`lambda/mir.c`** — No changes needed. The existing `register_dynamic_import` + `import_resolver` already supports cross-module references.

- **`test/test_js_test262_gtest.cpp`** — `assemble_combined_source()` stops prepending sta.js/assert.js. Manifest format changes.

**Risk**: Low. Two-module compilation is already proven for ES module imports. The harness is essentially an "imported module" that provides globals.

**Expected savings**: ~30-40% of total batch time. Eliminates redundant harness compilation (parse + AST + MIR gen + MIR compile) for 27k tests. MIR JIT of the test module is faster because it's smaller (no harness functions).

## Stage 2: Incremental Tree-sitter Parsing

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

## Implementation Order

```
Stage 1: Two-Module MIR Split              ← highest value, lowest risk
  └── modified js-test-batch, new transpile_js_test_against_harness()
  └── test262 batch: ~30-40% reduction

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
| Current | baseline | ~17/sec |
| After Stage 1 | 30-40% | ~24/sec |
| After Stage 1+2 | 35-45% | ~27/sec |
| After Stage 1-4 | 55-65% | ~40/sec |
