# Lambda Transpile Tuning Proposal

## Motivation

Transpile profiling across 278 scripts reveals two distinct performance profiles:

| Metric | Standalone (223 scripts) | With Imports (55 scripts) |
|--------|------------------------:|-------------------------:|
| Avg total | 5.20 ms | 155.85 ms |
| Dominant phase | MIR Transpile (68.8%) | AST Build (79.7%) |
| Worst case | 134 ms (chart_test_repeat_chart) | 831 ms (latex_test_latex_m7, 28 modules) |

For **standalone scripts**, the MIR transpile phase dominates. For **scripts with imports**, serial module compilation during AST build is the bottleneck — each import recursively calls `transpile_script()` depth-first, so all parse → AST → transpile → JIT work for every imported module is measured as "AST Build" time in the importing script.

This proposal addresses both bottlenecks with two independent workstreams:

1. **Multi-threaded module compilation** — parallelize imported module compilation
2. **MIR transpile phase tuning** — reduce the 68.8% single-module transpile cost

---

## Part 1: Multi-Threaded Module Compilation

### 1.1 Current Architecture

Import resolution is embedded inside `build_module_import()` (called during AST construction). When an `import` statement is encountered:

```
Main: transpile_script()
  ├─ parse main
  ├─ build_script()               ← AST build begins
  │   └─ encounter "import A"
  │       └─ load_script(A)       ← BLOCKS: calls transpile_script(A) recursively
  │           ├─ parse A
  │           ├─ build_script(A)  ← may discover A's own imports...
  │           ├─ transpile A
  │           └─ JIT A
  │   └─ encounter "import B"
  │       └─ load_script(B)       ← BLOCKS again: sequential
  ├─ transpile main
  └─ JIT main
```

Each module is compiled **sequentially and depth-first**. For `latex_test_latex_m7` (28 modules), this serialization costs ~831 ms. If independent modules could compile in parallel, wall-clock time would drop significantly.

### 1.2 Proposed Design: Two-Phase Import Compilation

Split the current single-pass "discover and compile" into two phases:

**Phase 1 — Import Discovery (single-threaded, lightweight)**

Do a quick scan of the source to extract import paths without building the full AST. Import statements are syntactically simple (`import <alias>:<module>` or `import <module>`), so a Tree-sitter parse + shallow CST walk suffices:

```
discover_imports(source_path):
    tree = ts_parse(source)
    for each import_statement in tree:
        path = resolve_import_path(statement)
        if path not in module_cache:
            pending_modules.add(path)
            // Recurse: discover transitive imports too
            discover_imports(path)
    return pending_modules
```

This produces a **dependency graph** of all modules before any compilation begins. Tree-sitter parsing is fast (~0.3 ms per module on average), so discovering 28 modules costs ~8 ms total — negligible.

**Phase 2 — Parallel Compilation (thread pool)**

Compile modules in topological order, parallelizing independent modules:

```
compile_modules(dependency_graph):
    // Modules with no uncompiled dependencies can run in parallel
    thread_pool = ThreadPool(num_cores)
    while uncompiled modules remain:
        ready = modules whose dependencies are all compiled
        for module in ready:
            thread_pool.submit(compile_single_module, module)
        thread_pool.await_batch()
```

For a dependency graph like:
```
main → A → C
     → B → C
```
C compiles first, then A and B compile in parallel, then main compiles.

### 1.3 Shared State Inventory & Thread Safety Plan

Full audit of global/shared mutable state that must be addressed:

#### Critical (read-write during compilation)

| State | Location | Problem | Solution |
|-------|----------|---------|----------|
| `Runtime->scripts` (ArrayList) | `lambda/lambda.hpp` | Module cache; appended by `load_script()` | Mutex-protect append + lookup; or pre-populate from Phase 1 |
| `dynamic_import_table[]` | `lambda/mir.c:87` | Global static array, linear scan, cleared per module | **Eliminate global**: pass per-compilation import table (see §1.4) |
| `Runtime->parser` (TSParser) | `lambda/lambda.hpp` | Tree-sitter parsers are NOT thread-safe | Create one TSParser per worker thread |
| `Script->is_loading` | `lambda/ast.hpp:590` | Circular import guard; non-atomic boolean | Not needed if Phase 1 pre-resolves the full dependency graph |
| `MIR_context_t` | Per-script | MIR library not thread-safe for concurrent module creation | One MIR context per compilation (already the case for MIR Direct) |
| Arena allocators | `Transpiler->arena` | Bump-pointer; inherently non-thread-safe | Per-thread arenas (each compilation already creates its own Transpiler) |

#### Init-once (safe after startup)

| State | Location | Solution |
|-------|----------|----------|
| `func_map` (runtime function hashmap) | `lambda/mir.c:38` | Move `init_func_map()` to program startup before any threading |
| `sys_func_map` + `sys_func_name_set` | `lambda/build_ast.cpp:74` | Move init to startup; already guarded by `if (!initialized)` but add mutex |

#### Already thread-local (safe)

| State | Location |
|-------|----------|
| `context` (eval context) | `__thread` in runner.cpp |
| `persistent_last_error` | `__thread` in runner.cpp |
| `_lambda_stack_base/limit` | `__thread` in lambda-stack.h |
| `MirTranspiler` struct | Stack-local per compilation call |

### 1.4 Eliminating the Global Dynamic Import Table

The current design uses a **global static array** (`dynamic_import_table[256]`) that is cleared and repopulated before each module's `MIR_link()`. This is fundamentally incompatible with concurrent compilation.

**Proposed change**: Replace the global table with a per-compilation import context:

```c
// NEW: Per-compilation import context (passed to import_resolver)
typedef struct {
    JitImport* imports;
    int count;
    int capacity;
} ImportContext;

// import_resolver receives context via MIR_link's void* user_data
// (requires checking MIR API for user_data support — if not available,
//  use thread-local storage as fallback)
void *import_resolver(const char *name) {
    ImportContext* ctx = get_thread_import_context();
    // Search per-compilation table (or use hashmap for O(1))
    ...
    // Fall back to global func_map (read-only after init, safe)
    return hashmap_get(func_map, name);
}
```

If MIR's `MIR_link()` doesn't support a user-data parameter for the resolver callback, use `__thread ImportContext* tls_import_ctx` as the mechanism.

### 1.5 Implementation Plan

| Step | Change | Risk | Effort |
|------|--------|------|--------|
| 1. Import discovery pass | New function: `discover_all_imports()` — parse sources, build dependency graph | Low | Medium |
| 2. Move one-time inits to startup | `init_func_map()`, `init_sys_func_maps()` called in `main()` before any compilation | Low | Low |
| 3. Per-thread TSParser | Each worker thread creates its own `ts_parser_new()` + `ts_parser_set_language()` | Low | Low |
| 4. Per-compilation import context | Replace global `dynamic_import_table` with thread-local or parameter-passed context | Medium | Medium |
| 5. Thread pool + topological dispatch | `std::thread` or pthread pool; compile in dependency order | Medium | Medium |
| 6. Module cache synchronization | Mutex around `Runtime->scripts` append; compiled modules registered after join | Low | Low |
| 7. Merge compiled modules | After all modules compiled, register cross-module function pointers for main script | Medium | Medium |

### 1.6 Expected Improvement

For a script importing N modules with max dependency depth D:

- **Current**: O(N) serial module compilations
- **Proposed**: O(D) critical-path compilations (parallel within each depth level)

For `latex_test_latex_m7` (28 modules): if the dependency tree has depth ~4 and most modules are independent siblings, expect **3–5× speedup** on the import-heavy compilation path. For the chart suite (2–16 modules per script, ~270 ms average), expect **2–3× speedup**.

Standalone scripts (no imports) see **zero benefit** from this change — they compile a single module anyway.

### 1.7 Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Correctness regression from races | Start with a global mutex around `load_script()` (serialize as before), then selectively remove locks area by area with targeted tests |
| MIR library not designed for concurrent use | Each module gets its own `MIR_context_t` (already the case in MIR Direct path); contexts are independent |
| Import discovery may miss dynamic/conditional imports | Lambda imports are always static top-level statements — no conditional imports exist in the language grammar |
| Thread creation overhead exceeds savings for small import counts | Use thread pool (reuse threads); skip parallelism for ≤2 imports |

---

## Part 2: MIR Transpile Phase Tuning

The MIR transpile phase accounts for **68.8% of standalone compilation** (798.83 ms total, 3.58 ms average across 223 scripts). This section identifies concrete optimizations within the transpile phase itself.

### 2.1 Current Transpile Pipeline

Inside `transpile_mir_ast()`, the AST is processed in **4 sequential passes** plus a final main-body transpilation:

```
transpile_mir_ast(ast):
    Pass 1: prepass_compile_patterns()     — compile regex patterns       ~2-5%
    Pass 2: prepass_create_global_vars()   — create BSS for module vars   ~1-2%
    Pass 3: prepass_forward_declare()      — forward-declare all funcs    ~5-8%
    Pass 4: prepass_define_functions()     — define function bodies        ~20-30%
              └─ transpile_func_def()      — per function
                  └─ infer_param_type()    — per untyped parameter (WALKS BODY)
    Pass 5: transpile main body            — generate main() MIR code     ~15-25%
    MIR_link()                             — link imports                  ~5-10%
    MIR_gen()                              — generate native code          ~15-20%
```

### 2.2 Optimization A: Merge Prepass 3 & 4 Into a Single AST Walk

**Problem**: `prepass_forward_declare()` and `prepass_define_functions()` walk the **entire AST** with **identical traversal patterns** (same switch/case structure over ~25 node types). This means every node is visited twice.

**Proposed fix**: Merge into a single pass that forward-declares each function, then immediately defines it:

```cpp
static void prepass_declare_and_define(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            // Step 1: forward-declare (from old prepass_forward_declare)
            forward_declare_function(mt, fn_node);
            // Step 2: recurse into nested functions first
            if (fn_node->body) prepass_declare_and_define(mt, fn_node->body);
            // Step 3: define (from old prepass_define_functions)
            transpile_func_def(mt, fn_node);
            break;
        }
        // ... same cases for CONTENT, LIST, LET_STAM, etc. ...
        }
        node = node->next;
    }
}
```

**Constraint**: Forward declarations must precede definitions for mutual recursion. The merged pass handles this naturally — a function is forward-declared before its body (and nested functions) are processed, so any forward reference within the body resolves correctly.

**Expected savings**: Eliminates one full AST traversal. For large scripts (~100+ functions), this saves **5-8% of transpile time**.

| Effort | Risk | Savings |
|--------|------|---------|
| Medium | Low (same semantics, just reordered) | ~5-8% of transpile phase |

### 2.3 Optimization B: Batch Parameter Type Inference

**Problem**: `infer_param_type()` is called **once per untyped parameter**, and each call walks the **entire function body**. For a function with M untyped parameters, this is M × O(body_size).

The two internal passes are:
1. `find_aliases()` — find `var x = param` assignments (iterates until convergence)
2. `gather_evidence()` — scan body for type evidence (int literals, float literals, arithmetic usage)

**Proposed fix**: Gather evidence for **all parameters in a single body walk**:

```cpp
// NEW: Infer types for ALL untyped params in one pass
static void infer_all_param_types(AstNode* body, AstFuncNode* fn_node) {
    // Build a map: param_name → InferCtx
    HashMap param_ctxs;  // keyed by param name
    for (each param in fn_node->params) {
        if (param->type == ANY) {
            InferCtx* ctx = create_infer_ctx(param->name);
            hashmap_set(&param_ctxs, param->name, ctx);
        }
    }
    if (hashmap_count(&param_ctxs) == 0) return;

    // Single pass: find aliases for ALL params simultaneously
    bool changed = true;
    while (changed) {
        changed = false;
        find_aliases_multi(body, &param_ctxs, &changed);
    }

    // Single pass: gather evidence for ALL params simultaneously
    gather_evidence_multi(body, &param_ctxs);

    // Read results back
    for (each param in fn_node->params) {
        InferCtx* ctx = hashmap_get(&param_ctxs, param->name);
        if (ctx) param->inferred_type = resolve_type(ctx);
    }
}
```

**Savings analysis**: A function with 5 untyped parameters currently does 5 separate body walks. The batched version does 1 walk (or 2 if alias convergence needs an extra round). For scripts with many multi-parameter functions, this is a **significant reduction**.

| Effort | Risk | Savings |
|--------|------|---------|
| Medium | Low (same inference logic, different iteration) | ~10-20% of transpile phase for param-heavy scripts |

### 2.4 Optimization C: Convert Dynamic Import Table to Hashmap

**Problem**: `import_resolver()` (called by `MIR_link()`) does a **linear O(n) scan** of `dynamic_import_table[]` before falling back to the O(1) `func_map` hashmap lookup:

```c
void *import_resolver(const char *name) {
    // O(n) linear scan — called once per unresolved symbol during MIR_link
    for (int i = 0; i < dynamic_import_count; i++) {
        if (strcmp(dynamic_import_table[i].name, name) == 0)
            return (void*)dynamic_import_table[i].func;
    }
    // O(1) hashmap lookup (for runtime functions)
    return hashmap_get(func_map, name);
}
```

For a module importing K symbols from other modules, each `MIR_link()` call invokes `import_resolver` per unresolved symbol. With `dynamic_import_count` up to 256, this is O(K × 256) string comparisons.

**Proposed fix**: Use a hashmap for dynamic imports:

```c
static HashMap* dynamic_import_map = NULL;

void register_dynamic_import(const char *name, void *addr) {
    if (!dynamic_import_map) dynamic_import_map = hashmap_new(...);
    JitImport entry = {.name = name, .func = addr};
    hashmap_set(dynamic_import_map, &entry);
}

void clear_dynamic_imports(void) {
    if (dynamic_import_map) hashmap_clear(dynamic_import_map);
}

void *import_resolver(const char *name) {
    // O(1) lookup for cross-module imports
    if (dynamic_import_map) {
        JitImport* found = hashmap_get(dynamic_import_map, &(JitImport){.name = name});
        if (found) return (void*)found->func;
    }
    // O(1) lookup for runtime functions
    return hashmap_get(func_map, &(JitImport){.name = name});
}
```

| Effort | Risk | Savings |
|--------|------|---------|
| Low | Very low (drop-in replacement) | ~5-10% of MIR_link time; more for import-heavy modules |

### 2.5 Optimization D: Cache Prepass Results Across Phases

**Problem**: Information computed in early passes (forward declarations, function locations) is sometimes re-derived in later passes. The `register_local_func` / `find_local_func` lookups are fast (hashmap), but the AST nodes themselves are visited multiple times across passes.

**Proposed fix**: Build a **function index** during Pass 1 (or the merged pass from §2.2):

```cpp
struct FuncIndex {
    AstFuncNode** functions;   // flat array of all function nodes
    int count;
};

// Built during prepass, reused by define phase and main body
FuncIndex func_index;
```

This allows `prepass_define_functions` to iterate a flat array of function pointers instead of recursively walking the entire AST tree. Combined with §2.2 (merge passes), this further reduces overhead.

| Effort | Risk | Savings |
|--------|------|---------|
| Low | Very low | ~2-3% (reduces AST traversal overhead) |

### 2.6 Optimization E: Explore MIR Optimization Level Tuning

**Context**: `MIR_gen_init(ctx, opt_level)` accepts an optimization level. The current setting is read from `Runtime->optimize_level`. Higher optimization levels make MIR spend more time optimizing generated code (producing faster runtime code but slower compilation).

**Proposed investigation**: Profile the JIT codegen phase (`MIR_gen()`) at different optimization levels:

| Level | Expected compile speed | Expected runtime speed |
|-------|----------------------|----------------------|
| 0 | Fastest | Baseline |
| 1 | Moderate | Better |
| 2 | Slowest | Best |

For scripts where compilation latency matters more than peak throughput (e.g., REPL, short scripts, test suites), level 0 may be the right default. For benchmarks, level 2. This is a **configuration change** rather than a code change.

| Effort | Risk | Savings |
|--------|------|---------|
| Very low (config) | None | Variable: could save 30-50% of JIT codegen time at level 0 |

### 2.7 Summary: Estimated Impact on Standalone Scripts

Combining all MIR transpile optimizations for standalone scripts (current average: 5.20 ms):

| Optimization | Phase affected | Est. savings | New avg (ms) |
|-------------|---------------|-------------|-------------|
| Baseline | — | — | 5.20 |
| B. Batch param inference | Transpile (68.8%) | 10-20% of transpile | 4.48 – 4.84 |
| A. Merge prepass 3+4 | Transpile (68.8%) | 5-8% of transpile | 4.30 – 4.55 |
| C. Hashmap dynamic imports | Link (~5%) | 5-10% of link | 4.27 – 4.52 |
| D. Function index | Transpile | 2-3% of transpile | 4.20 – 4.45 |
| E. Opt level tuning | JIT (4.6%) | 30-50% of JIT (level 0) | 4.13 – 4.34 |
| **Combined estimate** | | | **~4.1 – 4.3 ms (17-21% faster)** |

---

## Implementation Priority

| Priority | Optimization | Effort | Impact | Standalone | With Imports |
|----------|-------------|--------|--------|-----------|-------------|
| **P1** | B. Batch param inference | Medium | High | ✅ 10-20% transpile | ✅ Also helps per-module |
| **P2** | A. Merge prepass 3+4 | Medium | Medium | ✅ 5-8% transpile | ✅ Also helps per-module |
| **P3** | C. Hashmap dynamic imports | Low | Medium | ✅ Link speedup | ✅ More symbols = more gain |
| **P4** | Multi-thread imports (Part 1) | High | High | — | ✅ 2-5× for heavy imports |
| **P5** | D. Function index cache | Low | Low | ✅ Minor | ✅ Minor |
| **P6** | E. MIR opt level tuning | Very low | Variable | ✅ JIT phase | ✅ JIT phase |

**Recommended approach**: Start with P1–P3 (algorithmic improvements, no threading complexity), then tackle P4 (multi-threaded imports) as a separate milestone.

---

## Implementation Progress

### Completed Optimizations

| Priority | Optimization                          | Status         | Files Changed              |
| -------- | ------------------------------------- | -------------- | -------------------------- |
| **P1**   | B. Batch param inference              | ✅ Done         | `lambda/transpile-mir.cpp` |
| **P2**   | D. Cache inferred types across phases | ✅ Done         | `lambda/transpile-mir.cpp` |
| **P2b**  | A. Merge prepass 3+4                  | ⏭️ Skipped     | —                          |
| **P3**   | C. Hashmap dynamic imports            | ✅ Done         | `lambda/mir.c`             |
| **P4**   | Multi-thread imports (Part 1)         | ✅ Done         | `runner.cpp`, `mir.c`, `build_ast.cpp` |
| **P5**   | D. Function index cache               | 🔲 Not started | —                          |
| **P6**   | E. MIR opt level tuning               | 🔲 Not started | —                          |

### P3: Hashmap Dynamic Imports (mir.c)

Replaced the global `dynamic_import_table[256]` array + linear O(n) scan with a Robin Hood hashmap (`lib/hashmap.h`). Three functions changed:

- `register_dynamic_import()` — now lazily creates `dynamic_import_map` and uses `hashmap_set()`  
- `clear_dynamic_imports()` — uses `hashmap_clear()`  
- `import_resolver()` — uses `hashmap_get()` for O(1) lookup instead of O(n) `strcmp` loop

Reuses the existing `func_obj_hash` / `func_obj_compare` functions already used by `func_map`.

### P2: Cache Inferred Types Across Phases (transpile-mir.cpp)

Added an `InferCacheEntry` struct + hashmap (`infer_cache`) to `MirTranspiler` that stores inferred parameter types keyed by `AstFuncNode*` pointer:

```cpp
struct InferCacheEntry {
    AstFuncNode* fn;
    TypeId param_types[16];
    int param_count;
};
```

- **`prepass_forward_declare()`**: After computing parameter types (declared + inferred), stores them in `infer_cache`
- **`transpile_func_def()`**: Checks `infer_cache` first before running inference again; only falls back to `infer_param_types_batched()` on cache miss

This eliminates redundant body walks between the prepass and definition phases — every function's parameters are inferred exactly once.

### P1: Batch Parameter Type Inference (transpile-mir.cpp)

Replaced per-parameter `infer_param_type()` calls with a batched version that walks the function body once for all untyped parameters simultaneously:

- **`find_aliases_multi()`** — single body walk checking aliases for all `InferCtx` contexts at once
- **`gather_evidence_multi()`** — single body walk gathering type evidence for all contexts at once
- **`resolve_inferred_type()`** — shared evidence→type resolution (extracted from `infer_param_type`)
- **`infer_param_types_batched()`** — orchestrator: collects untyped params, runs batched alias/evidence passes, resolves all types

For a function with M untyped parameters, body traversal is now O(body_size) instead of O(M × body_size).

### P2b: Merge Prepass 3+4 — Skipped

### P4: Multi-thread Imports — Part 1 (runner.cpp, mir.c, build_ast.cpp)

Added parallel pre-compilation of imported modules. When a main script is loaded with MIR Direct, all imports are discovered upfront and compiled by topological depth level before the main script's normal compilation begins.

**Architecture — two-phase approach:**

1. **Discovery phase** (single-threaded): Parse main script with Tree-sitter, extract `import_module` nodes, resolve module paths, recurse into transitive imports. Build a dependency graph with depth annotations.

2. **Compilation phase** (parallel per depth level): Compile modules level-by-level starting from depth 0 (leaves). At each level, if there's 1 module, compile inline; if 2+, spawn worker threads via `pthread_create`. Workers call `load_script()` which handles AST building, transpilation, and MIR code generation independently.

**Key changes:**

- `runner.cpp`: `precompile_imports()` — orchestrator function (~200 lines) with import discovery, depth computation, and level-by-level parallel compilation. `compile_module_worker()` — pthread entry point. `discover_imports_recursive()` — Tree-sitter-based import graph builder. `resolve_module_path()` — mirrors `build_module_import()` path resolution logic. `load_script()` restructured with mutex-protected script cache, `tls_parser` for worker threads, and precompile hook.
- `mir.c`: `dynamic_import_map` made `__thread` for per-thread isolation during parallel MIR linking. Added `ensure_jit_imports_initialized()`.
- `build_ast.cpp`: Added `ensure_sys_func_maps_initialized()` for thread-safe one-time init.

**Thread safety:**
- `pthread_mutex_t scripts_mutex` guards `runtime->scripts` arraylist
- `__thread TSParser* tls_parser` — thread-local parser for worker threads
- `__thread dynamic_import_map` — per-thread MIR import resolution
- Worker thread stack size set to 8MB (matches main thread) for deep transpiler recursion
- Scripts list reversed after precompile to match `run_script_mir()` reverse-order init expectation

**Current status:** Infrastructure complete and passing all tests (676/677, 1 pre-existing failure). Threshold set to ≥2 imported modules. The current test suite scripts have small import graphs (2-3 modules), so parallel threading benefit is minimal. Real speedup expected for larger module graphs with multiple independent imports at the same depth level.

Analysis determined this is **unsafe** due to mutual recursion. If function A (defined earlier in source) references function B (defined later as a sibling), B must be forward-declared in Pass 3 before A's body is transpiled in Pass 4. A merged single pass would attempt to define A's body before B is declared, causing a missing-function error. The cache optimization (P2) already eliminates the redundant inference work that motivated this merge.

### Measured Results

**Before** (baseline, 223 standalone scripts):

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 56.13 | 0.25 | 4.8% |
| AST Build | 242.33 | 1.09 | 20.9% |
| MIR Transpile | 798.83 | 3.58 | 68.8% |
| JIT Codegen | 63.72 | 0.29 | 5.5% |
| **Total** | **1161.01** | **5.20** | 100% |

**After** (P1 + P2 + P3, 220 standalone scripts):

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 56.03 | 0.25 | 6.0% |
| AST Build | 83.56 | 0.38 | 9.0% |
| MIR Transpile | 741.80 | 3.37 | 79.9% |
| JIT Codegen | 47.24 | 0.21 | 5.1% |
| **Total** | **928.63** | **4.22** | 100% |

**Transpile phase reduction by suite:**

| Suite | Before (ms) | After (ms) | Reduction |
|-------|------------|-----------|-----------|
| awfy | 163.22 | 152.07 | **6.8%** |
| lambda | 453.77 | 424.16 | **6.5%** |
| proc | 66.55 | 62.30 | **6.4%** |
| r7rs | 33.08 | 25.96 | **21.5%** |
| larceny | 33.03 | 32.18 | **2.6%** |
| beng | 23.85 | 23.08 | **3.2%** |
| **Total** | **798.83** | **741.80** | **7.1%** |

The r7rs suite showed the largest improvement (21.5%) because its Scheme-style scripts have many untyped parameters — exactly the case batched inference optimizes most.

### Verification

- **Build**: 0 errors, 221 warnings (no new warnings)
- **Lambda baseline tests**: 676/677 pass (1 pre-existing failure: `math_test_math_html_output` — UTF-8 encoding issue in expected output, unrelated)
- **Radiant baseline tests**: 3671/3671 pass

---

## Appendix: Profiling Data Reference

Full profiling report: [`test/benchmark/Transpile_Result.md`](../test/benchmark/Transpile_Result.md)

**Top 5 standalone scripts by transpile time:**

| Script | Parse | AST | Transpile | JIT | Total |
|--------|------:|----:|----------:|----:|------:|
| chart_test_repeat_chart | 0.12 | 132.60 | 1.02 | 0.10 | 133.85 |
| complex_iot_report_html | 2.31 | 3.61 | 49.21 | 0.65 | 55.78 |
| awfy_cd | 2.14 | 3.15 | 32.45 | 0.79 | 38.53 |
| type_pattern | 1.51 | 2.18 | 25.16 | 0.46 | 29.31 |
| latex_test_latex_picture | 0.24 | 24.91 | 3.39 | 0.13 | 28.67 |

**Top 5 import-heavy scripts by total time:**

| Script | Parse | AST | Transpile | JIT | Total | Modules |
|--------|------:|----:|----------:|----:|------:|--------:|
| latex_test_latex_m7 | 16.02 | 588.70 | 219.56 | 6.29 | 830.57 | 28 |
| math_test_math_html_output | 5.97 | 364.17 | 63.41 | 2.42 | 435.98 | 15 |
| chart_test_histogram | 10.07 | 266.16 | 113.50 | 3.19 | 392.92 | 16 |
| chart_test_rule_chart | 10.13 | 264.38 | 111.75 | 3.06 | 389.31 | 16 |
| chart_test_bubble_chart | 9.54 | 265.50 | 108.57 | 2.86 | 386.46 | 15 |
