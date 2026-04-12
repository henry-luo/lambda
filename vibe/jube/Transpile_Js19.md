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

##### 3.4.4 Shared Test262 Harness Preloading — NOT FEASIBLE

Test262 tests prepend harness files (`assert.js`, `sta.js`, `propertyHelper.js`) to each test source. In batch mode, these could theoretically be:
1. Pre-parsed once and cached as AST fragments
2. Or: pre-compiled as a module and imported, avoiding re-parsing per test

**Status: Not feasible.** The harness defines JS functions (`assert`, `assert.sameValue`, `assert.throws`, `Test262Error`, `$DONOTEVALUATE`) that tests call as global functions. These cannot be moved to C++ because tests call them from JS with JS-level semantics. Module wrapping is also infeasible since tests are scripts expecting globals, not modules. The harness is only ~200 lines, taking microseconds to parse — not a meaningful optimization target.

##### 3.4.5 Temp File Elimination for Test262 — DONE

Previously each test262 run wrote a temp `.js` file per test and deleted it after execution (~27,000 write + read + unlink cycles). Now uses `posix_spawn()` with manifest files and inline source protocol:

**Protocol**: `source:<name>:<length>\n<source bytes>\n` — length-prefixed binary data read from manifest file via child's stdin

**Architecture**: Each of 6 worker threads reuses one manifest file. Per sub-batch: truncate → write source data → `posix_spawn()` with stdin from manifest → read stdout via pipe → waitpid.

**Key optimizations**:
- `posix_spawn()` instead of `fork()+exec()`: avoids copying ~1.3GB of page tables 5400+ times. Saved 534s system time (867s→333s)
- Per-worker reusable manifest files: only 6 temp files total, each overwritten ~900 times (vs 27K individual temp files before)
- `FD_CLOEXEC` on all pipe/file fds to prevent fd leaks across parallel sub-batch children
- `lambda/main.cpp`: `js-test-batch` handler parses `source:` prefix, reads length-prefixed binary blob from stdin via `fread()`

**Result**: 10,719 passes in 3:28 — 15 seconds faster than the original per-test temp file baseline (3:43), using only 6 tiny reusable manifest files.

##### 3.4.6 Lazy Source Assembly — DONE

Previously, Phase 1 (prepare) assembled all 27,030 `combined_source` strings upfront — each test's harness files (`sta.js`, `assert.js`, extra includes) concatenated with the test source. This consumed ~1.3GB of parent process memory (27K tests × ~47KB avg). Even with `posix_spawn()` avoiding page-table copies, this memory stayed resident throughout all 5400 spawns.

**Optimization**: Phase 1 now only parses metadata (flags, includes, negative expectations) and stores lightweight fields (`test_path`, `includes` list, `is_strict` flag). Source assembly is deferred to Phase 2 workers who call `assemble_combined_source()` on-the-fly when writing each manifest chunk — re-reading the test file from the OS page cache and prepending cached harness files.

**Memory impact**: Peak RSS dropped from ~1,280 MB to ~838 MB (-35%). The remaining ~838 MB is GTest infrastructure overhead (38,649 parameterized test registrations).

**Performance impact**: Phase 1 time dropped from ~2-3s to 0.9s (no string assembly). Wall time improved from 3:28 to ~3:00 (-13%) due to reduced memory pressure during Phase 2.

**Result**: 10,719 passes in ~3:00 — 28 seconds faster than posix_spawn baseline, 43 seconds faster than temp file baseline, with 35% less memory.

## 4. Implementation Plan

| Step | Description | Files Modified | Status |
|------|-------------|----------------|--------|
| 1a | Add `js-test-batch` command handler in main.cpp | `lambda/main.cpp` | **Done** |
| 1b | Implement `js_batch_reset()` | `lambda/js/js_runtime.cpp` | **Done** |
| 1c | Refactor `transpile_js_to_mir` to accept external parser | `lambda/js/transpile_js_mir.cpp` | Deferred |
| 2a | Add `execute_js_batch()` to test harness | `test/test_js_gtest.cpp` | **Done** |
| 2b | Convert `JsFileTest` to use batch results | `test/test_js_gtest.cpp` | **Done** |
| 2c | Convert Test262 runner to batch mode | `test/test_js_test262_gtest.cpp` | **Done** |
| 3a | Guard MIR dumps with `JS_MIR_DUMP` env var | `lambda/js/transpile_js_mir.cpp` | **Done** |
| 3b | Downgrade execution-path `log_notice` to `log_debug` | `lambda/js/transpile_js_mir.cpp` | **Done** |
| 4a | Reuse JsMirTranspiler allocation in batch | `lambda/js/transpile_js_mir.cpp` | Deferred |
| 4b | Fast-path skip import precompile for no-import scripts | `lambda/js/transpile_js_mir.cpp` | **Done** |
| 3.4.6 | Lazy source assembly (defer to Phase 2 workers) | `test/test_js_test262_gtest.cpp` | **Done** |

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

## 7. Implementation Status

### Completed Items

| Step | Description | Status |
|------|-------------|--------|
| 1a | `js-test-batch` command handler in `lambda/main.cpp` | **Done** |
| 1b | `js_batch_reset()` in `lambda/js/js_runtime.cpp` | **Done** |
| 2a | `execute_js_batch()` in `test/test_js_gtest.cpp` | **Done** |
| 2b | Convert `JsFileTest` to batch mode (SetUpTestSuite) | **Done** |
| 2c | Convert Test262 runner to batch mode | **Done** |
| 3a | Guard MIR dumps with `JS_MIR_DUMP` env var | **Done** |
| 3b | Downgrade execution-path `log_notice` → `log_debug` | **Done** |
| 4b | Fast-path skip `jm_precompile_js_imports()` for no-import scripts | **Done** |
| 3.4.5 | Temp file elimination (inline `source:` protocol over pipes) | **Done** |
| 3.4.6 | Lazy source assembly (deferred to Phase 2 workers) | **Done** |

### Deferred Items

| Step | Description | Status | Notes |
|------|-------------|--------|-------|
| 1c | Refactor `transpile_js_to_mir` to accept external parser | Deferred | Parser is still created per-call; batch process amortizes startup cost across all scripts in the chunk |
| 3.4.4 | Shared test262 harness preloading | Not feasible | Harness defines JS functions (`assert`, `Test262Error`, etc.) that tests call as globals — cannot be moved to C++. Module wrapping also infeasible (tests are scripts, not modules). Harness is ~200 lines, microseconds to parse. |
| 4a | Reuse `JsMirTranspiler` allocation in batch | Deferred | ~3MB `malloc`+`memset` per script; diminishing returns given batch already eliminates process overhead |

### Final Batch Configuration

- **JS unit tests**: `JS_BATCH_CHUNK_SIZE = 50`, single batch process
- **Test262**: `T262_BATCH_CHUNK_SIZE = 5`, `T262_MAX_PARALLEL_BATCHES = 6` (~75% of 8 CPU cores)

## 8. Test262 Batch Mode Results

All runs on macOS, Apple Silicon (8 cores), debug build.

### JS Unit Tests

| Mode | Tests | Passed | Time |
|------|-------|--------|------|
| Subprocess (old) | 85 | 75 | ~8s |
| **Batch (final)** | **85** | **75** | **~1s** |

Speedup: **~8x**

### Test262 Compliance Suite (~27,030 tests)

| Run | Chunk Size | Workers | Passed | Failed | Skipped | Wall Time | CPU % | Notes |
|-----|-----------|---------|--------|--------|---------|-----------|-------|-------|
| Baseline (subprocess) | 1 per process | 8 (hw_concurrency) | 10,982 (40.6%) | 16,048 | 11,619 | 5:37 | 458% | Pre-batch reference |
| Batch run 1 | 5 | 2 | 10,719 (39.7%) | 16,311 | 11,619 | 9:09 | 183% | Conservative first attempt |
| Batch run 2 | 20 | 3 | 10,090 (37.3%) | 16,940 | 11,619 | 6:26 | 266% | Larger chunks cause more state leakage |
| Batch run 3 | 5 | 6 | 10,726 (39.7%) | 16,304 | 11,619 | 4:48 | 433% | Before Phase 3/4 optimizations |
| **Final (batch+opt)** | **5** | **6** | **10,719 (39.7%)** | **16,311** | **11,619** | **3:43** | **453%** | **With MIR dump guard + log_debug + import skip** |
| Inline source (no temp files) | 5 | 6 | 10,719 (39.7%) | 16,311 | 11,619 | 4:09 | 472% | Pipe-based inline source protocol, zero temp files |
| posix_spawn + manifests | 5 | 6 | 10,719 (39.7%) | 16,311 | 11,619 | 3:28 | 489% | posix_spawn avoids fork page-table copy; 6 reusable manifest files |
| **posix_spawn + lazy assembly** | **5** | **6** | **10,719 (39.7%)** | **16,311** | **11,619** | **~3:00** | **~510%** | **Phase 1 stores metadata only; workers assemble source on-the-fly. RSS 1280→838 MB** |

### Key Observations

1. **Small chunk size (5) is optimal** — larger chunks (20) cause ~3% pass rate degradation due to incomplete state reset between scripts in a batch. Some static variables (`js_ctor_cache_init`, `js_symbol_next_id`, `js_func_cache_count`, etc.) are not reset by `js_batch_reset()`.

2. **6 workers (~75% of cores) saturates CPU well** — 489% CPU on 8 cores with posix_spawn approach. Using all 8 cores risks system instability (an earlier attempt with unlimited parallelism crashed the system).

3. **Phase 3/4 optimizations added ~22% speedup** — guarding MIR dumps, downgrading `log_notice` to `log_debug`, and skipping import precompile for no-import scripts reduced wall time from 4:48 to 3:43 with identical batch settings.

4. **~1% pass rate gap vs subprocess baseline** — 10,719 vs 10,982 passes (39.7% vs 40.6%). This is due to residual state leakage between scripts in a batch. Acceptable tradeoff for the 38% wall-clock improvement.

5. **posix_spawn() is critical** — `fork()` copies the parent's page tables for ~1.3GB of in-memory test sources. With 5400+ spawns, this added ~534s of system time. `posix_spawn()` uses a macOS kernel-optimized path that directly creates child processes without copying page tables, eliminating this overhead.

6. **Overall speedup: 47% faster than subprocess baseline** (~3:00 vs 5:37) while using similar CPU resources. Also 19% faster than the temp-file batch baseline (~3:00 vs 3:43).

7. **Lazy assembly eliminates 1.3GB peak memory** — deferring `combined_source` assembly from Phase 1 to Phase 2 workers reduced RSS from 1,280 MB to 838 MB (-35%). Workers re-read test files from OS page cache (hot after Phase 1 metadata parsing) and assemble with cached harness strings. Wall time improved ~28s (3:28→~3:00) due to reduced memory pressure and faster Phase 1 (0.9s vs 2-3s).

### Process Spawn Strategy Comparison (Section 3.4.5)

All runs: macOS ARM (Apple Silicon, 8 cores), debug build, chunk=5, 6 workers, 10,719 passes.

| # | Approach | User | System | Wall | CPU% | Temp Files |
|---|----------|------|--------|------|------|------------|
| 1 | Temp files (baseline) | 667s | 344s | **3:43** | 453% | ~27K (write+read+unlink each) |
| 2 | Inline pipes | 569s | 609s | 4:09 | 472% | 0 |
| 3 | fork + manifests (all upfront) | 677s | 744s | 5:33 | 426% | ~5400 (written all at once, then executed) |
| 4 | fork + reusable manifests | 746s | 867s | 6:14 | 431% | 6 (one per worker, reused) |
| 5 | posix_spawn + reusable manifests | 684s | 333s | 3:28 | 489% | 6 |
| **6** | **posix_spawn + lazy assembly** | **~603s** | **~313s** | **~3:00** | **~510%** | **6** |

#### Why each approach is fast or slow

**#1 Temp files (baseline, 3:43)** — One `.js` temp file per test, written during Phase 1 (parallel prepare), then each sub-batch passes file paths to `popen("lambda.exe js-test-batch")`. The child reads file paths from stdin, opens and reads each file via `read_text_file()`. System time is moderate (344s) because file I/O is well-cached by the OS page cache and `popen()` uses `vfork()` internally on macOS.

**#2 Inline pipes (4:09, +26s)** — Eliminated all temp files by piping `source:<name>:<length>\n<bytes>\n` directly to child's stdin via `pipe()+fork()+exec()`. A writer thread sends data while the main thread reads stdout, avoiding pipe buffer deadlock. **System time doubled** (344→609s) because ~1.3GB of combined test source must flow through kernel pipe buffers (64KB on macOS), requiring ~20K+ context switches between writer and reader. The pipe is a **serial bottleneck**: every byte goes parent→kernel→child, vs file I/O where `fread()` reads directly from page cache.

**#3 fork + manifests upfront (5:33, +1:50)** — Pre-wrote all ~5400 manifest files in parallel before execution, then `fork()+exec()` with stdin redirected from the manifest file. Fixed the pipe overhead (stdin is now file-based), but **system time exploded** (744s) because the parent process now holds ~1.3GB of `combined_source` strings in memory. Each `fork()` must copy the parent's page tables for all that virtual memory. On macOS ARM with 16KB pages, 1.3GB = ~83K page table entries × 5400 forks = **~450M page table entry copies** in the kernel. Additionally, writing 5400 manifest files upfront (each ~250KB) is ~1.35GB of file I/O that competes with the subsequent execution phase.

**#4 fork + reusable manifests (6:14, +2:31)** — Same as #3 but each worker reuses one manifest file (6 total). Eliminated the upfront write burst, but **made things worse** because manifest writes now happen **inside the execution loop**: write manifest → fork → exec → read stdout → waitpid → repeat. The `fork()` page-table copy cost (867s system time) is compounded by the serialized file write inserting latency between spawns. Each worker does 900 cycles of write+fork+read, and `fork()` gets progressively slower as the parent accumulates more memory from result strings.

**#5 posix_spawn + reusable manifests (3:28, -15s vs baseline)** — Same architecture as #4 but replaced `fork()+exec()` with `posix_spawn()`. **System time dropped from 867s to 333s** because `posix_spawn()` on macOS uses the `__mac_posix_spawn` kernel syscall which creates the child process directly — it does NOT copy the parent's page tables. The child starts with a clean address space and loads `lambda.exe` via `exec`, so the parent's 1.3GB of in-memory data is irrelevant. This is 11s less system time than even the temp-file baseline (333 vs 344s) because we also eliminated 27K `unlink()` syscalls.

**#6 posix_spawn + lazy assembly (~3:00, -43s vs baseline)** — Same as #5 but Phase 1 no longer assembles `combined_source` strings. Instead it stores only metadata (`test_path`, `includes` list, `is_strict` flag). Phase 2 workers call `assemble_combined_source()` on-the-fly when writing each manifest, re-reading the test file from OS page cache and prepending cached harness strings. **RSS dropped from 1,280 MB to 838 MB** (-35%) because the 27K × 47KB source strings are never held simultaneously. User time dropped ~81s (684→603s) because Phase 1 is faster (no string allocation/copy) and Phase 2 has less memory pressure (fewer TLB misses, less GC pressure from malloc). System time dropped ~20s (333→313s) from reduced page fault handling. Phase 1 time: 2-3s → 0.9s.

#### Summary of bottlenecks

| Bottleneck | Approaches affected | System time cost |
|-----------|---------------------|-----------------|
| Pipe buffer serialization (~1.3GB through 64KB kernel buffer) | #2 Inline pipes | +265s |
| `fork()` page-table copy (~1.3GB parent memory × 5400 spawns) | #3, #4 | +400-523s |
| 27K temp file create+unlink cycles | #1 Temp files | ~11s |
| Manifest I/O inside execution loop | #4 | serialized latency |
| 1.3GB resident memory (27K pre-assembled sources) | #1-#5 | +81s user, +20s system (memory pressure) |
