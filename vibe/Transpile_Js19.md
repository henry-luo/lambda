# JavaScript Transpiler v19: Batch Mode and Test Performance Proposal

## 1. Executive Summary

LambdaJS tests currently run **each script as a separate subprocess** (`popen("./lambda.exe js file.js")`), incurring full process startup, tree-sitter parser creation, runtime initialization, and teardown for every single test. Lambda Script solved this years ago with a `test-batch` command that streams multiple scripts through one process, reusing the parser and resetting only the heap between runs.

This proposal introduces an equivalent **`js-test-batch`** command for LambdaJS, along with targeted optimizations to reduce debug overhead and improve test throughput.

### Estimated Impact

| Area | Current | Proposed | Speedup |
|------|---------|----------|---------|
| JS unit tests (~85 files) | ~85 subprocess spawns | ~2 batch processes (50/batch) | ~5-8x |
| Test262 suite (~26K executed) | ~26K subprocess spawns | ~520 batch processes (50/batch) | ~10-20x |
| Debug build MIR dump | Writes `temp/js_mir_dump.txt` per run | Skip in batch mode | Eliminates I/O |
| Parser lifecycle | Created + destroyed per subprocess | Created once, reused across batch | ~2-3ms saved/test |

## 2. Current Architecture (Problem)

### 2.1 JS Unit Tests (`test/test_js_gtest.cpp`)

Each test invokes a **fresh subprocess**:
```cpp
char* execute_js_script(const char* script_path) {
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --no-log", script_path);
    FILE* pipe = popen(command, "r");
    // ... read output, check exit code
}
```

Per-subprocess cost breakdown:
1. **Process fork+exec**: ~1-2ms (OS overhead)
2. **Runtime init** (`runtime_init`): memory pool, parser creation
3. **Tree-sitter JS parser creation** (`ts_parser_new` + `ts_parser_set_language` for JS grammar): ~1-3ms
4. **MIR JIT context init** (`jit_init`): ~0.5ms
5. **JsMirTranspiler alloc** (~3 MB struct): `malloc` + `memset`
6. **Cleanup**: parser destroy, MIR finish, runtime cleanup
7. **Process teardown**: file descriptor close, memory unmap

For 85 tests, this is ~85 process spawn/teardown cycles. Each cycle costs ~5-10ms in overhead alone.

### 2.2 Test262 Runner (`test/test_js_test262_gtest.cpp`)

Uses a **parallel thread pool** (N = hardware concurrency), but each thread still spawns subprocesses:
```cpp
snprintf(command, sizeof(command),
    "timeout 10 ./lambda.exe js \"%s\" --no-log 2>&1", temp_path.c_str());
FILE* pipe = popen(command, "r");
```

With ~26,000 executed tests, this is ~26,000 subprocess spawns — even with N threads running in parallel, the subprocess overhead dominates. Each test also writes a temp `.js` file to disk and deletes it after.

### 2.3 Lambda Script Batch Mode (Reference Implementation)

Lambda Script's `test-batch` command (`main.cpp:2439-2515`) demonstrates the pattern:
- Reads script paths from **stdin** (one per line)
- Brackets output with **`\x01BATCH_START`** / **`\x01BATCH_END`** markers
- Reuses the **tree-sitter parser** across all scripts
- Calls `runtime_reset_heap()` between scripts to reset GC heap, nursery, and name pool
- Cleans up Script objects (AST, syntax tree, pool, JIT context) after each run
- Supports **per-script timeout** via `SIGALRM` (Unix) with configurable `--timeout=N`

Test harness side (`test/test_lambda_helpers.hpp`):
- `execute_lambda_batch()` splits scripts into sub-batches of 50 (`BATCH_CHUNK_SIZE`)
- Sub-batches run in **parallel threads**, each piping a manifest file to `test-batch`
- Results parsed from stdout markers and merged

## 3. Proposed Changes

### Phase 1: `js-test-batch` CLI Command

Add a new command to `lambda/main.cpp` that mirrors `test-batch` but for JavaScript:

```
./lambda.exe js-test-batch [--no-log] [--timeout=N]
```

**Stdin protocol** (same as Lambda Script batch):
```
path/to/test1.js
path/to/test2.js
source:inline_js_code_here
...
```

**Stdout protocol**:
```
\x01BATCH_START path/to/test1.js
<test output>
\x01BATCH_END 0
\x01BATCH_START path/to/test2.js
<test output>
\x01BATCH_END 1
```

#### Implementation Details

##### 3.1.1 Command Handler (`main.cpp`)

Add a new block after the existing `test-batch` handler:

```cpp
if (argc >= 2 && strcmp(argv[1], "js-test-batch") == 0) {
    Runtime runtime;
    runtime_init(&runtime);
    lambda_stack_init();

    // Create persistent JS parser (reused across all scripts)
    TSParser* js_parser = ts_parser_new();
    ts_parser_set_language(js_parser, tree_sitter_javascript());

    int batch_timeout = 60;
    // parse --timeout=N from argv...

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        // trim trailing whitespace
        // skip empty lines

        printf("\x01" "BATCH_START %s\n", script_path);
        fflush(stdout);

        // Read JS source or use inline source
        char* js_source = NULL;
        if (strncmp(line, "source:", 7) == 0) {
            js_source = strdup(line + 7);
        } else {
            js_source = read_text_file(script_path);
        }

        // Execute with timeout (SIGALRM on Unix)
        int result = run_js_batch_entry(&runtime, js_parser, js_source, script_path, batch_timeout);

        fflush(stdout);
        printf("\x01" "BATCH_END %d\n", result);
        fflush(stdout);

        free(js_source);

        // Reset state for next script
        js_batch_reset_state(&runtime);
    }

    ts_parser_delete(js_parser);
    runtime_cleanup(&runtime);
}
```

##### 3.1.2 Per-Script Execution (`run_js_batch_entry`)

New function that reuses the persistent parser and runtime:

```cpp
static int run_js_batch_entry(Runtime* runtime, TSParser* parser,
                              const char* js_source, const char* filename,
                              int timeout_sec) {
    // Parse with shared parser
    TSTree* tree = ts_parser_parse_string(parser, NULL, js_source, strlen(js_source));
    if (!tree) return 1;

    // Build AST
    JsTranspiler* tp = js_transpiler_create_with_parser(runtime, parser);
    // ... (use existing transpile_js_to_mir logic but with shared parser)

    // Execute
    Item result = transpile_js_to_mir_internal(runtime, tp, tree, js_source, filename);

    ts_tree_delete(tree);  // tree per-script, parser persists
    // transpiler cleanup per-script

    return (result.item == ITEM_ERROR) ? 1 : 0;
}
```

##### 3.1.3 State Reset Between Scripts (`js_batch_reset_state`)

Critical: must clean all mutable global state so scripts don't leak into each other.

| State | Location | Reset Action |
|-------|----------|-------------|
| GC heap + nursery | `runtime->heap`, `runtime->nursery` | `runtime_reset_heap(&runtime)` |
| Name pool | `runtime->name_pool` | Released by `runtime_reset_heap` |
| Module vars | `js_module_vars[2048]` in `js_runtime.cpp` | `js_reset_module_vars()` |
| Exception state | `js_exception_pending`, `js_exception_value` | `js_clear_all_exceptions()` (new) |
| Event loop | Event queue, timers | `js_event_loop_cleanup()` (new if needed) |
| Deferred MIR contexts | `module_mir_contexts[]` | `jm_cleanup_deferred_mir()` |
| DOM document | `runtime->dom_doc`, `_js_current_document` | Set to `NULL` |
| Input context | `js_input` global | Set to `NULL` |
| Current `this` binding | `js_current_this` | Reset to `{0}` |
| `js_source_runtime` | Static in `transpile_js_mir.cpp` | Already set per-run |
| Dynamic func counter | `js_dynamic_func_counter` | Reset to 0 |
| Fetch state | `response_body_count`, `pending_fetch_work` | Reset to 0 / NULL |
| Array method `this` | `js_array_method_real_this` | Reset to `{0}` |

Implement as a single reset function:
```cpp
extern "C" void js_batch_reset_state(Runtime* runtime) {
    js_reset_module_vars();
    js_clear_all_exceptions();
    js_event_loop_cleanup();
    jm_cleanup_deferred_mir();
    runtime->dom_doc = NULL;
    js_runtime_set_input(NULL);
    runtime_reset_heap(runtime);
}
```

### Phase 2: Test Harness Integration

##### 3.2.1 `execute_js_batch()` in `test/test_js_gtest.cpp`

Modeled on `execute_lambda_batch()` from `test_lambda_helpers.hpp`:

```cpp
static const size_t JS_BATCH_CHUNK_SIZE = 50;

static std::unordered_map<std::string, BatchResult> execute_js_batch(
    const std::vector<std::string>& scripts,
    size_t chunk_size = JS_BATCH_CHUNK_SIZE)
{
    // Split into sub-batches, write manifests to ./temp/
    // Run each sub-batch in parallel threads via:
    //   ./lambda.exe js-test-batch --no-log --timeout=10 < manifest.txt
    // Parse \x01 markers from stdout
    // Merge results
}
```

##### 3.2.2 Convert `JsFileTest` to Batch Mode

```cpp
class JsFileTest : public testing::TestWithParam<JsTestParam> {
    static std::unordered_map<std::string, BatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;
        auto all = discover_all_js_tests();
        std::vector<std::string> scripts;
        for (auto& t : all) scripts.push_back(t.script_path);
        batch_results = execute_js_batch(scripts);
        batch_executed = true;
    }
};

TEST_P(JsFileTest, Run) {
    auto it = batch_results.find(GetParam().script_path);
    ASSERT_TRUE(it != batch_results.end());
    // Compare it->second.output against expected .txt file
}
```

##### 3.2.3 Convert Test262 to Batch Mode

The Test262 runner needs more work because:
1. Tests include **harness preambles** (`assert.js`, `sta.js`, etc.) prepended to test source
2. Tests may be wrapped in strict mode (`"use strict";\n`)
3. Negative tests expect error exit codes

Approach: use `source:` prefix to send assembled test source inline on stdin:
```
source:<assembled_test_source_base64_or_escaped>
```

Or, better: continue writing temp files but batch them via manifest:
```
./temp/_test262_batch_0.js
./temp/_test262_batch_1.js
...
```

The parallel thread pool model can be retained, but instead of N threads each spawning subprocesses, use N batch processes each handling ~50 tests.

**Expected speedup for test262**: From ~26,000 subprocess spawns → ~520 batch processes (50/batch), run across N parallel threads. With N=12 threads: ~44 batch rounds × batch overhead ≈ major wall-clock reduction.

### Phase 3: Eliminate Unnecessary Debug Overhead

##### 3.3.1 MIR Dump in Debug Builds

In `transpile_js_mir.cpp`, two `#ifndef NDEBUG` blocks write full MIR dumps to disk:

| Location | Output File | Trigger |
|----------|-------------|---------|
| Line ~18564 | `temp/ts_mir_dump.txt` | Every TypeScript transpilation |
| Line ~18783 | `temp/js_mir_dump.txt` | Every JavaScript transpilation |

These iterate all MIR instructions, check for NULL labels, and write the entire module. In batch mode this would fire once per script.

**Fix**: Guard with an additional `JS_MIR_DUMP` environment variable or skip entirely in batch mode:
```cpp
#ifndef NDEBUG
    if (!batch_mode && getenv("JS_MIR_DUMP")) {
        // existing dump logic
    }
#endif
```

##### 3.3.2 High-Frequency Logging in DOM Path

`lambda/js/js_dom.cpp` contains **42 `log_debug()` calls** that fire on every property get/set. While `--no-log` disables output, the function call overhead (argument evaluation, format string processing) still occurs on each invocation.

**Fix**: Wrap hot-path DOM logging with a macro that short-circuits argument evaluation:
```cpp
#define JS_DOM_LOG(fmt, ...) \
    do { if (log_level_enabled(LOG_DEBUG)) log_debug(fmt, ##__VA_ARGS__); } while(0)
```

Or, since `--no-log` is always used in tests, verify that `log_debug` is truly a no-op (no argument evaluation) when logging is disabled. If the current implementation evaluates arguments before checking the log level, this is wasted work across thousands of property accesses.

##### 3.3.3 `log_notice()` Calls in Execution Path

Three `log_notice()` calls in the hot execution path (`transpile_js_mir.cpp:18843-18859`):
```
"js-mir: executing JIT compiled code"
"js-mir: JIT execution returned (type=%d)"
"js-mir: event loop drained"
```

These fire for every script. In batch mode with --no-log they're no-ops, but they should be downgraded to `log_debug()` regardless since they're not user-facing.

### Phase 4: Additional Speed Optimizations

##### 3.4.1 Reuse JsMirTranspiler Allocation

`JsMirTranspiler` is a **~3 MB struct** (`malloc` + `memset` to zero each time). In batch mode, allocate once and reuse with a targeted reset of only the fields that change between scripts (counters, scope depth, hashmaps).

##### 3.4.2 Skip `jm_precompile_js_imports()` for Simple Tests

Most unit tests have zero imports. The parallel import precompiler scans source for `import` statements and may spawn threads. Add a fast-path check:
```cpp
if (strstr(js_source, "import ") == NULL && strstr(js_source, "import{") == NULL) {
    // skip precompile step entirely
}
```

##### 3.4.3 Tree-sitter Parser Reuse Detail

In the current per-subprocess model, `js_transpiler_create()` calls:
- `ts_parser_new()` — allocates internal parser state
- `tree_sitter_javascript()` — loads grammar tables
- `ts_parser_set_language()` — validates and binds grammar

In batch mode, only `ts_parser_parse_string()` is called per script (O(source_length)), while parser creation is amortized to once per batch process.

##### 3.4.4 Shared Test262 Harness Preloading

Test262 tests prepend harness files (`assert.js`, `sta.js`, `propertyHelper.js`) to each test source. In batch mode, these could be:
1. Pre-parsed once and cached as AST fragments
2. Or: pre-compiled as a module and imported, avoiding re-parsing per test

This eliminates redundant parsing of ~200-500 lines of harness code × 26,000 tests.

##### 3.4.5 Temp File Elimination for Test262

Currently each test262 run writes a temp `.js` file and deletes it:
```cpp
std::string temp_path = "temp/_test262_run_" + std::to_string(my_id % 256) + ".js";
std::ofstream out(temp_path);
out << combined;
// ... execute, then unlink
```

With the `source:` stdin protocol, the assembled source can be passed inline, eliminating ~26,000 file write + read + unlink cycles. For large sources, use a pipe or memory-mapped approach.

## 4. Implementation Plan

| Step | Description | Files Modified | Effort |
|------|-------------|----------------|--------|
| 1a | Add `js-test-batch` command handler in main.cpp | `lambda/main.cpp` | Medium |
| 1b | Implement `js_batch_reset_state()` | `lambda/js/js_runtime.cpp`, `transpile_js_mir.cpp` | Medium |
| 1c | Refactor `transpile_js_to_mir` to accept external parser | `lambda/js/transpile_js_mir.cpp` | Medium |
| 2a | Add `execute_js_batch()` to test harness | `test/test_js_gtest.cpp` | Small |
| 2b | Convert `JsFileTest` to use batch results | `test/test_js_gtest.cpp` | Small |
| 2c | Convert Test262 runner to batch mode | `test/test_js_test262_gtest.cpp` | Medium |
| 3a | Guard MIR dumps with env var / batch flag | `lambda/js/transpile_js_mir.cpp` | Small |
| 3b | Downgrade execution-path `log_notice` to `log_debug` | `lambda/js/transpile_js_mir.cpp` | Trivial |
| 4a | Reuse JsMirTranspiler allocation in batch | `lambda/js/transpile_js_mir.cpp` | Small |
| 4b | Fast-path skip import precompile for no-import scripts | `lambda/js/transpile_js_mir.cpp` | Trivial |

### Priority Order

1. **Steps 1a-1c + 2a-2b**: JS batch mode + unit test integration (highest ROI)
2. **Step 2c**: Test262 batch mode (biggest absolute time savings)
3. **Steps 3a-3b**: Debug overhead cleanup (quick wins)
4. **Steps 4a-4b**: Allocation and import optimizations (diminishing returns)

## 5. Risk Assessment

| Risk | Mitigation |
|------|-----------|
| State leak between batch scripts | Comprehensive reset function with explicit audit of all static/global vars in `lambda/js/` |
| Crash in one script kills batch | Per-script timeout via SIGALRM; chunk size of 50 limits blast radius |
| Output interleaving | Mutex on stdout, or rely on single-threaded batch process |
| Memory accumulation over 50 scripts | Chunk size tunable; `runtime_reset_heap` is proven reliable from Lambda batch mode |
| Test262 harness compatibility | Maintain subprocess fallback path for DOM tests and edge cases |

## 6. Verification

- All 85 JS unit tests must produce identical results in batch vs subprocess mode
- Test262 pass/fail/skip counts must be identical before and after
- No ASan/UBSan warnings in debug batch runs (except existing)
- Batch mode should show measurable wall-clock speedup (target: 5x+ for unit tests, 10x+ for test262)
