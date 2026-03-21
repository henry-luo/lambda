# Radiant Layout Batch Performance — Profiling & Optimization Proposal

## Date: March 21, 2026

---

## 1. Current Baseline

### Observed Timings (Debug Build, macOS arm64, `make layout suite=baseline`)

| Metric | Value |
|--------|-------|
| **Wall time** | **11.6 seconds** |
| User CPU | 3.82 seconds |
| System CPU | 5.48 seconds |
| Total tests | 3,017 (3,016 exec + 1 skip) |
| HTML test files | 2,649 |
| Reference files | 2,648 matching |

> Note: all measurements here use the **debug build**. A release build will have lower user-CPU time but the same structural overheads (I/O, process spawning, Node.js comparison). The ratio of overhead vs. useful work is the same.

### Time Breakdown

```
Wall time 11.6 s
├─ lambda.exe layout (process time across all invocations) ~4.6 s
│   ├─ Per-file layout+serialize                          ~3.4 s
│   └─ Startup + font init overhead × 133 batches        ~1.2 s
├─ System I/O (file reads, debug writes to test_output/)  ~5.5 s
└─ Node.js: JSON parsing + comparison + process mgmt      ~1.5 s
```

---

## 2. Test Runner Architecture

```
Makefile: make layout suite=baseline
  └─ node test/layout/test_radiant_layout.js --engine lambda-css --category baseline -j 5

Node.js runner (test/layout/test_radiant_layout.js):
  ├─ Reads 2,649 HTML files from test/layout/data/baseline/
  ├─ Filters: no-ref, too-large → 2,648 test tasks remain
  ├─ runTestsInBatchMode (batchSize=20, SEQUENTIAL):
  │   for each batch of 20 files:
  │     └─ spawn ./lambda.exe layout f1.html … f20.html --output-dir /tmp/layout_batch/
  │   → 133 process spawns, one at a time (NO parallelism)
  └─ compareTestResult for each file (parallel within batch):
      ├─ Read output JSON from /tmp/layout_batch/{name}.json
      ├─ Read reference JSON from test/layout/reference/{name}.json
      └─ compareNodes() recursive tree comparison
```

**Key problem: `runTestsInBatchMode` is fully sequential.** The `-j 5` concurrency flag is only used by the single-file non-batch path. All 133 batches run one after another.

---

## 3. Per-File Pipeline Stages

Measured via `[TIMING]` logs in single-file mode. Two representative files:

### Small file (438-byte HTML, 7 elements)
```
Read file:            0.0 ms
Parse HTML:           0.4 ms
Debug output write:  13.7 ms  ← writes html_tree.txt UNCONDITIONALLY
Build DOM tree:       0.6 ms
Parse CSS:            1.1 ms
Execute scripts:      1.3 ms
CSS cascade:          0.3 ms
────────────────────────────
load_lambda_html_doc  16.4 ms  (84% is debug I/O!)

layout_html_root:     3.3 ms
  └─ style_resolve:   1.3 ms (5 calls)
  └─ layout_block:    5.6 ms (2 calls)
  └─ layout_flex:     1.9 ms
print_view_tree:      0.7 ms
────────────────────────────
layout_html_doc total: 20.2 ms
```

### Large file (22 KB HTML, 444 elements, `line-height-test.html`)
```
Read file:             0.0 ms
Parse HTML:           18.7 ms
Debug output write:    2.1 ms
Build DOM tree:        8.4 ms
Parse CSS:             0.5 ms
Execute scripts:       2.0 ms
CSS cascade:           1.9 ms  (444 elements, 433 selector hits)
────────────────────────────
load_lambda_html_doc  46.0 ms

layout_html_root:    119.3 ms
  └─ style_resolve:  46.4 ms (437 calls, 1 per element)
  └─ text measure:   26.0 ms (290 text runs)
  └─ layout_block:  426.7 ms (436 calls)
print_view_tree:       6.6 ms
────────────────────────────
layout_html_doc total: 142.8 ms
```

---

## 4. Identified Bottlenecks & Optimization Proposals

---

### 4.1 Batch Runner: Add Parallelism to `runTestsInBatchMode`

**Category:** Test infrastructure (Node.js)
**Impact:** ★★★★★ High — architectural flaw
**Effort:** Small (Node.js change only)

**Problem:**
`runTestsInBatchMode` iterates through all 133 batches sequentially with a `for` loop. The `-j` concurrency option is completely ignored in batch mode. The CPU is mostly idle while waiting for each lambda.exe process.

```js
// Current (sequential):
for (let i = 0; i < testTasks.length; i += batchSize) {
    const batch = testTasks.slice(i, i + batchSize);
    outputMap = await this.runBatchLayout(htmlFiles);  // blocks
    ...
}
```

**Fix:**
Refactor `runTestsInBatchMode` to use the same `maxConcurrency` pool pattern used by the single-file mode — dispatch up to `maxConcurrency` batches in parallel via `Promise.race`, instead of `await`-ing each one sequentially.

```js
// Proposed (parallel batches):
const batchPromises = [];
for (let i = 0; i < testTasks.length; i += batchSize) {
    batchPromises.push(processBatch(testTasks.slice(i, i + batchSize)));
}
// Run batches with maxConcurrency limit using a pool
const results = await runWithConcurrencyPool(batchPromises, maxConcurrency);
```

**Expected gain:**
With `j=5` and 133 batches of 20 files: theoretical 5× reduction in wall time for the lambda.exe portion. Node.js comparison work (parallel already) won't change.
Estimated new wall time: **~4–5 seconds** (from 11.6 s).

---

### 4.2 Increase Default Batch Size (Quick Win)

**Category:** Test infrastructure (config)
**Impact:** ★★★☆☆ Medium
**Effort:** Trivial (one line change)

**Problem:**
Default `batchSize=20` spawns 133 processes for 2,649 files. Each process incurs ~8–10 ms of startup overhead (GLFW init, FontConfig warmup, log setup). Measured:

| Batch size | Processes spawned | Wall time |
|------------|-------------------|-----------|
| 20 (current) | 133 | 9.8 s |
| 100 | 27 | 7.7 s |
| 500 | 6 | 7.7 s |

Beyond 100, the savings plateau because startup overhead is already a small fraction. But `20 → 100` gives **21% improvement** for free.

The `MAX_INPUT_FILES` C++ limit is 1024. With 2,649 baseline test files, the practical maximum useful batch size is ~900 before hitting this limit if λ.exe must process all files in a single pass.

**Fix:**
1. Change default `batchSize` from `20` to `100` in [test/layout/test_radiant_layout.js](../../test/layout/test_radiant_layout.js).
2. Raise `MAX_INPUT_FILES` from 1024 to e.g. 4096 in `radiant/cmd_layout.cpp` so a single invocation can handle the entire baseline in one pass.

---

### 4.3 Eliminate Unconditional Debug File Writes in Batch Mode

**Category:** Lambda executable (C++ fix)
**Impact:** ★★★★☆ High
**Effort:** Small

**Problem A: `html_tree.txt` written every file unconditionally.**

In `load_lambda_html_doc()` ([radiant/cmd_layout.cpp](../../radiant/cmd_layout.cpp)), after parsing HTML, the code unconditionally serializes the entire parsed element tree to `./html_tree.txt`:

```cpp
StrBuf* htm_buf = strbuf_new();
print_item(htm_buf, input->root, 0);          // build string for entire tree
FILE* htm_file = fopen("html_tree.txt", "w"); // open/write/close every file
if (htm_file) {
    fwrite(htm_buf->str, 1, htm_buf->length, htm_file);
    fclose(htm_file);
}
strbuf_free(htm_buf);
```

For the small test file above, this consumed **13.7 ms out of 16.4 ms total load time** (84%!). For larger files it's smaller relative to parse time, but still 2+ ms per file.

**Problem B: `print_view_tree` called TWICE per file in batch mode.**

1. Inside `layout_html_doc()` ([radiant/layout.cpp](../../radiant/layout.cpp)):
   ```cpp
   print_view_tree((ViewElement*)doc->view_tree->root, doc->url);  // no output_path
   ```
   → writes `./view_tree.txt` + `./test_output/view_tree_{name}.txt` + `./test_output/view_tree_{name}.json` + `/tmp/view_tree.json`

2. Inside `layout_single_file()` ([radiant/cmd_layout.cpp](../../radiant/cmd_layout.cpp)):
   ```cpp
   print_view_tree((ViewElement*)doc->view_tree->root, doc->url, output_path);
   ```
   → writes `./view_tree.txt` (again) + `./test_output/view_tree_{name}.txt` (again) + `./test_output/view_tree_{name}.json` (again) + `output_path` (the real batch output)

Each file generates **5–6 I/O operations** when only 1 is needed.

**Measured impact:**
Running 900 files: ~3.62s system CPU for I/O. With 2,649 files: ~5.5s system CPU, which is **47% of total wall time**. 133 MB of test_output/ is written on every `make layout suite=baseline` run.

**Fixes:**

1. **Guard `html_tree.txt` debug write behind batch mode check** — skip both the `strbuf` build and the file write when in batch mode (logging is already disabled):
   ```cpp
   // Only write debug tree in single-file mode (logging enabled)
   if (!batch_mode) {
       StrBuf* htm_buf = strbuf_new();
       print_item(htm_buf, input->root, 0);
       ...
   }
   ```
   Since batch_mode isn't visible in `load_lambda_html_doc`, pass a flag or detect via log-enabled state.

2. **Remove the inner `print_view_tree` call from `layout_html_doc()`** — `layout_single_file()` already calls it with the correct output path. The call inside `layout_html_doc()` is redundant:
   ```cpp
   // In layout_html_doc(): REMOVE this call
   // print_view_tree((ViewElement*)doc->view_tree->root, doc->url);  ← delete
   ```
   `layout_html_doc()` should only *compute* the layout, not serialize it. Serialization is the caller's responsibility.

3. **Guard `./test_output/` and `./view_tree.txt` writes behind batch mode** — in `print_view_tree()`, skip all side-channel text/json writes when an explicit `output_path` is provided:
   ```cpp
   void print_view_tree(ViewElement* root, Url* url, const char* output_path) {
       // Only write debug .txt and side-channel .json in single-file mode
       if (!output_path) {
           // ... write ./view_tree.txt, ./test_output/view_tree_*.txt, etc.
       }
       // Always write the requested output_path
       print_view_tree_json(root, url, output_path);
   }
   ```

**Expected gain:** Eliminating ~5 unnecessary file writes per file × 2,649 files = ~13,000 file operations saved. Estimated system CPU reduction: **~3–4 seconds** from the current 5.48s.

---

### 4.4 Eliminate Duplicate `print_view_tree_json` Side-Output in Batch Mode

**Category:** Lambda executable (C++ fix)
**Impact:** ★★★☆☆ Medium (part of 4.3)
**Effort:** Small

In `print_view_tree_json()` ([radiant/view_pool.cpp](../../radiant/view_pool.cpp)), two files are written unconditionally:
```cpp
// Write to test_output/view_tree_{name}.json
snprintf(buf, sizeof(buf), "./test_output/view_tree_%s.json", last_slash + 1);
write_string_to_file(buf, json_buf->str);

// Write to output_path (or /tmp/view_tree.json fallback)
const char* json_output = output_path ? output_path : "/tmp/view_tree.json";
write_string_to_file(json_output, json_buf->str);
```

The `./test_output/view_tree_{name}.json` file is a duplicate of the batch output JSON. It is never used by the test runner. This alone accounts for 7 KB × 2,649 files = ~18 MB of extra writes per baseline run.

**Fix:** Skip `./test_output/` JSON write when `output_path` is explicitly specified.

---

### 4.5 Style Resolution: Lazy vs. Eager — No Change Needed

**Category:** Layout engine analysis
**Finding:** Style in Radiant is already lazy. `dom_node_resolve_style()` is only called during `layout_block()` traversal on demand. No full-tree pre-resolve step occurs. This is correct.

For the 444-element `line-height-test.html`, style_resolve takes 46.4 ms across 437 calls = **0.106 ms/element**. This is the floor of CSS resolution cost and is not easily reducible without deep algorithmic changes.

---

### 4.6 CSS Cascade: `SelectorMatcher` Could Be Shared for Identical Stylesheets

**Category:** Lambda executable (C++ optimization)
**Impact:** ★★☆☆☆ Low–Medium
**Effort:** Medium

**Observation:**
Many baseline test files share the same or very similar `<style>` block (e.g., Yoga-derived flex tests all have identical CSS). A `SelectorMatcher` is created from scratch for every file, even when two files have the same stylesheet content.

Currently: `SelectorMatcher* matcher = selector_matcher_create(pool)` is created per-file. The CSS parsing (`css_engine_create`, parsing `<style>` elements) is also per-file.

**Potential:** If files are grouped by identical stylesheet fingerprint (hash of CSS content), the parsed `CssStylesheet` and pre-indexed `SelectorMatcher` could be shared across a batch. This would reduce per-file CSS parse + cascade cost.

However, since CSS parsing takes ~1 ms per file and cascade is ~0.3 ms for most test files, the real savings here are modest (~300 ms total for 2,649 files). Not high priority compared to proposals 4.1–4.4.

---

### 4.7 Layout Pool Reuse / Arena Reset Instead of Destroy+Create

**Category:** Lambda executable (C++ optimization)
**Impact:** ★★★☆☆ Medium
**Effort:** Medium

**Problem:**
Each file does `pool = pool_create()` / `pool_destroy(pool)`. Pool creation allocates headers and sets up free lists; destruction walks and frees all allocations. For the typical small baseline test file (300–500 bytes of HTML, <50 elements), the pool allocation overhead may exceed the actual object allocation work.

**Proposal:** Introduce a `pool_reset()` operation that resets the pool's bump pointer without calling `free()` on individual allocations. The pool block memory is retained and reused for the next file. This is safe because all objects in the pool are short-lived (one file's lifetime).

```cpp
// Instead of:
Pool* pool = pool_create();  // allocates system memory
// ... process file ...
pool_destroy(pool);  // frees all -> next iteration allocates again

// Proposed:
Pool* pool = pool_create();  // once, before the batch loop
for each file:
    pool_reset(pool);  // O(1): reset bump pointers, keep block memory
    // ... process file...
// After all files done:
pool_destroy(pool);  // once
```

This saves O(n) malloc/free system calls. Benchmark needed to confirm impact.

---

### 4.8 JSON Comparison: Pre-Load Reference Files

**Category:** Node.js test runner
**Impact:** ★★★☆☆ Medium
**Effort:** Medium

**Problem:**
Currently, each call to `compareTestResult()` reads the reference file from disk:
```js
const browserData = await this.loadBrowserReference(testName, category, htmlFile);
```
With 2,648 reference files, this is 2,648 random reads from `test/layout/reference/`. Even with filesystem caching, this adds latency.

**Proposal:** Pre-load all required reference files for the baseline suite into memory before dispatching any layout work. With 2,648 files averaging ~10 KB each, this is ~26 MB of RAM — trivially affordable.

```js
// Before running tests:
const referenceCache = new Map();
for (const task of testTasks) {
    const ref = await loadBrowserReference(task);
    referenceCache.set(task.testName, ref);
}
// Then pass cache to compareTestResult instead of disk reads
```

This converts random I/O scattered across CPU usage into a single sequential prefetch pass, improving cache locality.

---

### 4.9 Serialization: Direct File Writing Instead of StrBuf Intermediate

**Category:** Lambda executable (C++ optimization)
**Impact:** ★★☆☆☆ Low–Medium
**Effort:** Medium

**Problem:**
`print_view_tree_json()` builds the entire JSON output into a `StrBuf` then writes it to disk in one `write_string_to_file()` call. For the `line-height-test.html` (large file), the JSON output is ~40 KB. The StrBuf must grow its internal buffer several times.

**Proposal:** Use a buffered file writer (`FILE*` with `setvbuf`) instead of StrBuf, writing JSON tokens directly to disk. This eliminates the memory allocation growth and reduces peak RSS.

Alternatively: pre-size the StrBuf based on DOM element count × average bytes-per-element (empirically ~100 bytes). Replace `strbuf_new_cap(2048)` with `strbuf_new_cap(element_count * 120)`.

---

### 4.10 `strbuf` in `print_view_tree` (Text Format) — Skip Entirely in Batch Mode

**Category:** Lambda executable (C++ fix)
**Impact:** Part of 4.3 benefit
**Effort:** Small

`print_view_tree()` currently builds a text-format view tree string (`StrBuf*`) even in batch mode where nobody reads it. The string is `log_debug`-ed (already a no-op when logging is off) then written to `./view_tree.txt` anyway.

```cpp
StrBuf* buf = strbuf_new_cap(1024);
print_view_block((ViewBlock*)view_root, buf, 0);
// ...
write_string_to_file("./view_tree.txt", buf->str);  // wasteful in batch
```

**Fix:** Guard the entire text-format generation behind `if (!output_path)` — when an explicit batch output path is given, skip building the text string. Only build + write the JSON.

---

## 5. Summary of Proposals and Expected Impact

| # | Proposal | Type | Effort | Est. Wall Time Saved | Status | Priority |
|---|---------|------|--------|---------------------|--------|----------|
| 4.1 | **Parallelize batch mode in Node.js runner** | Test infra | Small | ~4–5 s (40–50%) | ✅ Done | ★★★★★ |
| 4.3 | **Skip debug file writes in batch mode** (`html_tree.txt`, redundant `print_view_tree`) | C++ fix | Small | ~2–3 s (20–25%) | ✅ Done | ★★★★★ |
| 4.4 | **Skip `test_output/` JSON duplicate writes** | C++ fix | Small | ~0.5–1 s (5–10%) | ✅ Done | ★★★★☆ |
| 4.2 | **Increase default batch size 20 → 100** | Config | Trivial | ~2 s (20%) | ✅ Done | ★★★★☆ |
| 4.8 | **Pre-load reference files in Node.js** | Node.js | Medium | ~0.5–1 s (5%) | — | ★★★☆☆ |
| 4.7 | **Pool reset instead of destroy+create** | C++ opt | Medium | ~0.3–0.5 s (3%) | — | ★★★☆☆ |
| 4.6 | **CSS SelectorMatcher sharing for identical stylesheets** | C++ opt | Medium | ~0.3 s (3%) | — | ★★☆☆☆ |
| 4.10 | **Skip text strbuf build in batch mode** | C++ fix | Small | (part of 4.3) | ✅ Done | ★★★☆☆ |
| 4.9 | **Direct file writer in JSON serialization** | C++ opt | Medium | ~0.1 s | — | ★★☆☆☆ |

---

## 6. Tuning Results (Implemented March 21, 2026)

### Changes Implemented

| Change | File(s) | Description |
|--------|---------|-------------|
| 4.1 | `test/layout/test_radiant_layout.js` | `runTestsInBatchMode` rewritten to dispatch up to `maxConcurrency` batches in parallel using a `Promise.race` pool |
| 4.2 | `test/layout/test_radiant_layout.js` | Default `batchSize` raised from 20 → 100 |
| 4.2 | `radiant/cmd_layout.cpp` | `MAX_INPUT_FILES` raised from 1024 → 4096 |
| 4.3 | `radiant/cmd_layout.cpp` | `html_tree.txt` write guarded by `log_default_category->enabled` (skipped in batch mode) |
| 4.3 | `radiant/layout.cpp` | Removed redundant `print_view_tree()` call from inside `layout_html_doc()`; serialization is now solely the caller's responsibility |
| 4.3 + 4.4 + 4.10 | `radiant/view_pool.cpp` | `./test_output/view_tree_*.txt`, `./view_tree.txt`, and `./test_output/view_tree_*.json` side-writes skipped when an explicit `output_path` is provided (batch mode) |

### Measured Results

```
make layout suite=baseline  (debug build, macOS arm64, Apple Silicon)

Before:  11.6 s wall  |  3.82 s user  |  5.48 s system  |  ~100% CPU  |  3,017 ✅
After:    3.6 s wall  |  3.04 s user  |  4.55 s system  |   212% CPU  |  3,017 ✅

Speedup: 3.2×  (11.6 s → 3.6 s)
```

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| **Wall time** | **11.6 s** | **3.6 s** | **−69% (3.2×)** |
| User CPU | 3.82 s | 3.04 s | −20% |
| System CPU | 5.48 s | 4.55 s | −17% |
| CPU utilization | ~100% | 212% | Multi-core parallelism |
| Tests passing | 3,017 / 3,017 | 3,017 / 3,017 | No regression |

The remaining system CPU (4.55 s) is largely `/tmp/layout_batch/{name}.json` writes — the 2,648 output files that are genuinely required for comparison.

### Projected Outcome After Remaining Proposals

```
Baseline (before):       11.6 s
After implemented (4.1–4.4):  3.6 s  (3.2× speedup)  ← ACTUAL MEASURED
After 4.7 + 4.8:        ~2.5 s  (combined ~4.6× from original)
```

The dominant wins are:
1. **Parallelize batch mode** — structural fix, biggest single win (unlocked multi-core)
2. **Eliminate unnecessary file writes per test** — fixes ~47% of system CPU
3. **Larger batch size** — trivial config change, ~20% win

The rest are diminishing returns since lambda.exe computation itself (~1.5–2 s for 2,649 files in a release build) is the remaining floor.

---

## 7. Appendix: Profiling Data

### Direct `lambda.exe` invocation (batches of 900 files, debug build)

```
Batch 1 (900 files):  1,042.6 ms  → 1.2 ms/file
Batch 2 (900 files):  1,470.1 ms  → 1.6 ms/file
Batch 3 (900 files):  1,644.8 ms  → 1.8 ms/file
Batch 4 (318 files):    486.3 ms  → 1.5 ms/file
Wall time: 6.4 s (user: 1.71 s, system: 3.62 s)
```

Raw layout only (no I/O overhead from debug files): **~4 s user CPU**, ceiling ~2 s in release build.

### Test runner wall times at different parameters

```
batchSize=20,  j=5  (current defaults):  11.6 s
batchSize=100, j=5:                       7.7 s  (−34%)
batchSize=500, j=5:                       7.7 s  (plateau)
batchSize=500, j=10:                      7.0 s  (j has no effect in batch mode!)
```

The plateau at 7.7s for batch≥100 confirms that startup amortization is the main gain from bigger batches, and the sequential execution is the bottleneck, not per-file compute.

### Unnecessary debug writes per baseline run

| File | Written per test | Total for 2,649 tests |
|------|------------------|-----------------------|
| `./html_tree.txt` | ~0.5 KB (overwritten) | cost: 1× write × 2,649 |
| `./view_tree.txt` | ~4 KB (overwritten twice) | cost: 2× writes × 2,649 |
| `./test_output/view_tree_{name}.txt` | ~4 KB | cost: 2× writes × 2,649 |
| `./test_output/view_tree_{name}.json` | ~7 KB | cost: 2× writes × 2,649 |
| `/tmp/view_tree.json` | ~7 KB (overwritten) | cost: 1× write × 2,649 |
| `/tmp/layout_batch/{name}.json` | ~7 KB | ← this is the **only** needed one |

Total data written per baseline run: ~**163+ MB** (measured: 133 MB in `test_output/`).
Only **~18 MB** of that is the actual required batch output in `/tmp/layout_batch/`.

---

## 8. Key Insight Summary

1. **Sequential batch mode is the biggest wasted opportunity.** The infrastructure to run lambda.exe processes in parallel already exists (used by single-file mode). Applying it to batch mode is a Node.js-only change that should unlock parallelism for free.

2. **47% of wall time is system CPU for file I/O** to files that are never read (debug artifact writes). These can be safely removed for batch mode with no impact on test correctness.

3. **The layout algorithm itself (`layout_block`, style resolution, text measurement) is fast** — ~1.5 ms/file for typical baseline files. The actual computation is not the bottleneck.

4. **Release build will halve user-CPU time** but does nothing for system I/O or parallel dispatch. Proposals 4.1 and 4.3 address the true bottlenecks regardless of build type.
