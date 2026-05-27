# JS Runtime Performance Tuning Proposal — Round 2

Source data: `test/js262/results/release_run_003/` (commit `b406da25` +
working tree; release `lambda.exe`; 34,163 timed tests).
Analysis date: 2026-05-26.

This document follows on from `Transpile_Js_Tune.md` (§§1–7). That round
landed §3.1 (transient args stack), §3.3 (const-bound static dispatch),
§6.x (generator yield-spill fix), and §7.2.B (1-byte ASCII string
interning). After those changes the regular timing TSV has

> **0 tests ≥ 2 s, 1 test ≥ 1.5 s, 6 tests ≥ 1 s, 18 tests ≥ 0.5 s, 141 tests
> ≥ 0.1 s** (sum 398.8 s, avg 11.67 ms across 34 163 tests).

Two additional tests live in the partial-pass list at ~3.2 s
(`decodeURI/decodeURIComponent A2.5_T1`) — they are documented in
`test262_partial_pass.txt` and covered by `Transpile_Js_Tune.md` §7.

---

## 1. Slow-cluster inventory (release_run_003)

Sorted by elapsed time, every test ≥ 0.5 s falls into exactly **two
clusters** (the rest are ≤ 0.46 s and unremarkable):

| Cluster | Count | Sum | Avg | Range |
| --- | ---: | ---: | ---: | --- |
| **A. Unicode-identifier parsing** (`language_identifiers_start_unicode_*`) | 16 | 14.737 s | 0.921 s | 0.51–1.51 s |
| **B. RegExp literal in `eval`-loop** (`language_literals_regexp_S7_8_5_*`) | 2 | 1.295 s | 0.647 s | 0.65 s |

Plus, in `t262_partial_at_run.txt` (rerouted through Phase 4):

| Cluster | Count | Range |
| --- | ---: | --- |
| **C. URI 4-byte sweep** (`built_ins_(decode|encode)URI*/A2_5_T1`) | 2 | 3.19–3.22 s |

Cluster C was characterised at length in `Transpile_Js_Tune.md` §7 and
moved to `test262_partial_pass.txt`. This document focuses on **A and B**.

---

## 2. Cluster A — unicode-identifier tests (~14.7 s aggregate)

### 2.1 What the tests actually do

Each file parses a single top-level statement:

```js
var _ૺૻૼ… ;        // (or the un-escaped form with raw codepoints)
```

i.e. a `var` declaration whose identifier is constructed from many ID_Start /
ID_Continue codepoints from a particular Unicode version. The slowest
example, `part-unicode-8.0.0-escaped.js`, is **1918 bytes** containing
~70 `\uXXXX` and `\u{XXXXX}` escape sequences. No loop, no work to speak of
at runtime.

### 2.2 The pathology: **40× slower in batch than standalone**

Direct release timing of the slowest case
(`language_identifiers_start_unicode_8_0_0_escaped_js`):

```
./lambda.exe js ref/test262/test/language/identifiers/part-unicode-8.0.0-escaped.js
  → 0.01–0.02 s (3 runs)

# even with the full test262 preamble pre-pended:
cat sta.js assert.js nativeFunctionMatcher.js + test_body | ./lambda.exe js -
  → 0.04 s (3 runs)
```

The harness records **1.51 s** for the same test in batch (`top_slow_tests.tsv`).
This is **roughly 40× the standalone cost**, and it is *not* runtime work — the
test body has no execution to speak of. The batch elapsed window is the
`gettimeofday(&tv_end) - gettimeofday(&tv_start)` around the single
`transpile_js_to_mir_with_preamble_len` call in `lambda/main.cpp:3633`, so
the gap is inside the test-body compile path when run inside a long-lived
`js-test-batch` process with a pre-shared preamble.

The 0.04 s vs 1.51 s ratio is consistent across all 16 tests in the cluster
(absolute values scale roughly with identifier length). The shape of the
cost — “mostly absent in a fresh process, mostly present after the batch has
already linked a sizeable preamble” — points at *state that grows with batch
lifetime*, not at the parser itself.

### 2.3 Likely root causes (ranked by my prior on the code shape)

| # | Hypothesis | Evidence / mechanism |
| --- | --- | --- |
| 1 | **Name-pool intern is O(pool_size) per long-identifier insert.** `heap_create_name` → `name_pool_create_strview` does a `find_string_by_content` lookup *and* a parent-pool lookup before allocating (`lambda/name_pool.cpp:100–122`). Hash quality on long Unicode UTF-8 strings degrades; collision walks may become linear. After a fresh process the pool is tiny; after the preamble + N previous tests, the pool holds tens of thousands of names. A single long-Unicode identifier on a fat pool is plausibly tens of ms — multiplied by both strict and non-strict compile passes, and the cost compounds. | `lambda/name_pool.cpp` |
| 2 | **MIR module / linker symbol table grows monotonically across batch tests.** Each test body produces MIR items with externally-visible names. If the symbol-table lookup is linear or hash-degraded against a multi-MB linker symbol set built up from 50 prior compiles, a long Unicode-named declaration's lookups dominate the test's elapsed window. | `lambda/mir.c`, `lambda/transpile-mir.cpp` |
| 3 | **Per-codepoint ID_Continue classification.** The lexer needs ID_Continue for each codepoint. If the lookup is a linear range scan (typical of generated tree-sitter scanners) rather than a flat 64-KiB BMP bitmap or a sorted-range binary search, scanning a 70-codepoint identifier costs O(70 × range_count). The standalone-vs-batch gap argues against this being *the* cause (a fresh process pays the same cost) but it likely adds a fixed slice. | `lambda/tree-sitter-javascript/src/scanner.c` (and any unicode_property tables) |

A short investigative spike (a few hours: add per-phase timers to `transpile_js_to_mir_with_preamble_len`, then re-run the slow test in batch vs standalone) would tell us which of #1/#2/#3 dominates. The optimization proposals below cover all three.

### 2.4 Proposed general optimizations

Each item below is gated, generally beneficial, and independently
landable. None is specific to "unicode identifier tests" — they all target
mechanisms that the broader engine pays for too.

#### 2.4.A — Length-bypass for very long names in the name pool

**Change.** In `name_pool_create_strview`, short-circuit interning for names
longer than a threshold (e.g. 32 bytes): allocate a fresh `String` directly
without doing the `find_string_by_content` + parent-pool lookup. Long names
are dominated by *unique* user identifiers; the pool's de-dup value is
mostly for short fixed strings ("constructor", "length", "toString" etc.).

- **Implementation surface.** ~20 lines in `lambda/name_pool.cpp`.
- **Per-test win.** Removes one O(pool_size) hashmap+content compare per
  long-identifier insertion. On the 16 unicode tests, plausibly ~0.7–1.0 s
  saved each (if hypothesis #1 holds) → ~12 s off the aggregate.
- **General benefit.** Any code that creates long identifiers, large
  property names, or long literal strings (computed keys, URL strings used
  as keys, etc.) stops paying the dedup tax. No risk on short names where
  interning is the right policy.
- **Risk.** Low. The pool's identity-by-content semantics are still
  preserved for short names. Long-name de-dup was statistically rare
  anyway.

#### 2.4.B — Hash the *length-prefixed* key, not the bytes alone

**Change.** The name pool's hash function is content-only. For mixed short
+ very long unicode strings, two long IDs sharing a common UTF-8 prefix
land in adjacent buckets, increasing collision walks. Adding `len` into the
hash (`hash = mix(hash, len)`) buys back distribution at zero cost.

- **Implementation surface.** ~5 lines in `lambda/name_pool.cpp`'s hash
  function (likely already a small inline helper).
- **Per-test win.** Stand-alone hard to predict; if hypothesis #1 holds it
  removes another constant from the pool insert cost.
- **General benefit.** Faster name-pool lookups for any program with a wide
  spread of name lengths. Essentially free.
- **Risk.** Negligible — the hash result changes but is still well-mixed;
  the pool's correctness is content-equality, not hash-identity.

#### 2.4.C — *Retracted.*

An earlier draft proposed "skip dual-mode (strict + sloppy) compile when
the test body has no strict-sensitive construct". That proposal rested on
a false premise: this harness does **not** dual-compile. Per
`test/test_js_test262_gtest.cpp:858, 963, 1011, 1080`, each test carries a
single `bool is_strict` that controls whether `"use strict";` is prepended
once before compile; the runner compiles each test exactly once. The
test262 spec asks for both modes when neither `onlyStrict` nor `noStrict`
is present, but this harness has chosen single-mode coverage there
(probably for total-batch-time reasons). So there is no duplicate compile
to elide; the proposal is moot. Adding the missing strict-mode pass
would be a *coverage* improvement that doubles work, the opposite of
what §2 is trying to do.

#### 2.4.D — Per-batch heap-epoch reset of the JIT linker symbol set

**Change.** If hypothesis #2 holds, the MIR module/symbol table grows
unboundedly across a 50-test batch. Add a periodic "soft reset" after every
K tests (or at the natural heap-epoch boundary) that drops symbols that
won't be referenced by future tests — the pre-shared preamble's exports
stay; the test-private symbols go away.

- **Implementation surface.** New entry point in `lambda/mir.c` /
  `lambda/transpile-mir.cpp`; a hook call in `lambda/main.cpp`'s
  `js-test-batch` loop.
- **Per-test win.** Eliminates O(N²) symbol-table growth across the batch
  if it currently exists.
- **General benefit.** Server-style long-lived JS runners (any embedding
  that repeatedly compiles short scripts) benefits.
- **Risk.** Medium — the test-private symbol set needs careful
  characterisation; touching MIR's linker tables is non-trivial.

#### 2.4.E — Flat-bitmap ID_Start / ID_Continue classification (external scanner + runtime init)

**Mechanism today.** The auto-generated parser
(`lambda/tree-sitter-javascript/src/parser.c:4445-4453`) classifies each
identifier codepoint via `set_contains(sym_identifier_character_set_*,
14|15, lookahead)` defined in
`lambda/tree-sitter-javascript/src/tree_sitter/parser.h:154`. That helper
is a 4-iteration binary search over 14–15 `TSCharacterRange` entries with
several branchy `lookahead >= range->start && lookahead <= range->end`
compares — roughly 8–12 conditionally-predicted operations per codepoint
on a hot path that runs on every identifier character of every parse.

**Change.** Replace that classifier with a flat BMP bitmap (1 bit per
codepoint in U+0000–U+FFFF) for the JS identifier rule, accessed in three
instructions (shift + mask + bit test). Do this *without* touching the
auto-generated `parser.c` — instead:

1. Declare `identifier` as an **external token** by adding one line to
   `grammar.js`'s `externals` array. Tree-sitter will then call our
   hand-written scanner first whenever an identifier might be valid.
2. Implement the classifier and an `IDENTIFIER` case in
   `scanner.c` (a file we already own and edit by hand).
3. **Generate the two 8 KiB bitmaps in BSS at runtime**, on the first
   call to `tree_sitter_javascript_external_scanner_create`. The data
   source is a small self-contained sorted `int32_t ranges[][2]` table
   (~3 KiB of `.rodata`, hand-maintained on Unicode version bumps; mirrors
   the precise tables already shipped at
   `lambda/js/js_runtime.cpp:13729` and used by the regex engine).

```c
// scanner.c

static const int32_t id_start_ranges[][2]    = { /* … ~330 pairs … */ };
static const int32_t id_continue_ranges[][2] = { /* … ~480 pairs … */ };

// BSS — 16 KiB of zero at link time, no disk bytes.
static uint8_t id_start_bmp[8192];
static uint8_t id_continue_bmp[8192];
static bool    id_bmp_inited = false;

static void fill_bmp(uint8_t bmp[8192], const int32_t r[][2], size_t n) {
    for (size_t i = 0; i < n; i++) {
        int32_t lo = r[i][0], hi = r[i][1];
        if (lo >= 0x10000) continue;            // astral handled separately
        if (hi >= 0x10000) hi = 0xFFFF;
        for (int32_t cp = lo; cp <= hi; cp++)
            bmp[cp >> 3] |= (uint8_t)(1u << (cp & 7));
    }
}

static void init_id_bmps_once(void) {           // ~3 µs, once per process
    if (id_bmp_inited) return;                  // see "Concurrency" below
    fill_bmp(id_start_bmp,    id_start_ranges,    N_ID_START);
    fill_bmp(id_continue_bmp, id_continue_ranges, N_ID_CONT);
    id_bmp_inited = true;
}

void *tree_sitter_javascript_external_scanner_create(void) {
    init_id_bmps_once();
    return NULL;
}

static inline bool is_id_start(int32_t cp) {
    if ((uint32_t)cp < 0x10000)
        return (id_start_bmp[(uint32_t)cp >> 3] >> ((uint32_t)cp & 7)) & 1;
    return astral_lookup(id_start_astral_ranges, N_ID_START_AST, cp);
}
static inline bool is_id_continue(int32_t cp) { /* … */ }
```

The `IDENTIFIER` case in
`tree_sitter_javascript_external_scanner_scan` then drives the lexer
straight-line: classify lookahead, advance, repeat — including a small
inline decoder for `\uXXXX` / `\u{XXXXXX}` escapes that calls the same
classifier on the decoded codepoint. The original `identifier:` grammar
rule stays in `grammar.js` as the formal definition (used by static-
analysis tooling that doesn't link scanner.c); when scanner.c IS linked
the external scanner wins.

**Why not a committed `.inc` file?** An earlier draft proposed generating
the bitmaps at build time via a Python script and committing a ~16 KiB
`.inc` file. Compared to runtime initialisation, that approach:

- adds ~16 KiB to `lambda.exe` on disk (the bitmaps land in `.rodata`),
- introduces a build-time Python generator and a `make generate-grammar`
  hookup,
- commits a generated data file that must be regenerated whenever the
  source ranges change.

The runtime-init design above pays a one-time ~3 µs startup cost
(unobservable in any real workload) in exchange for zero of those costs.
The 16 KiB BSS bitmaps still cost the same resident memory either way —
the only real difference is that compile-time `.rodata` would let the OS
share pages between concurrent `lambda.exe` processes, which at this
size (16 KiB × ~32 max workers = ~512 KiB private vs 16 KiB shared) is
academically interesting but not measurable.

**Concurrency.** `tree_sitter_javascript_external_scanner_create` runs
once per parser instance and may run concurrently in different threads.
The simplest safe option for this exact data is the "racy-read,
idempotent-fill" pattern shown above: concurrent fills write the same
bits in any order and the trailing `id_bmp_inited = true` store is the
only ordering edge that matters. If that bothers TSAN, swap the guard
for `pthread_once_t` (POSIX) or `std::call_once` (C++) — five extra
lines, no algorithmic change.

**Implementation surface.**

| File | Change | Lines |
| --- | --- | ---: |
| `lambda/tree-sitter-javascript/grammar.js` | Add `$.identifier` to the `externals: $ => [...]` array | +1 |
| `lambda/tree-sitter-javascript/src/scanner.c` | New `IDENTIFIER` token, `init_id_bmps_once`, `is_id_start`/`is_id_continue`, `scan_identifier`, escape decoder, dispatch in `external_scanner_scan` | ~120 |
| same — embedded range data | Sorted `int32_t id_start_ranges[][2]` and `id_continue_ranges[][2]` (BMP + astral) | ~3 KiB of `.rodata` literals |
| `Makefile` / build glue | None | 0 |

**Tests.** A standalone correctness test that, for every codepoint
0..0x10FFFF, asserts `is_id_start(cp)` and `is_id_continue(cp)` match
the existing classifier (`set_contains` on the parser's range tables
*or* `js_regex_cp_is_id_*` in `js_runtime.cpp`). Mechanical equivalence
check — no false negatives possible at build time.

**Honest sizing.** This is a *small general* win. The slow unicode
cluster's 40× batch slowdown is dominated by name-pool / linker-state
growth (§2.4.A/B/D), not by the classifier — even eliminating the
classifier entirely would save only a few milliseconds per test. What
this item buys is: a constant per-codepoint speedup on every JS parse
forever after, with the right architecture (external scanner, no
generated-code patching, no committed data file, no extra build target).
Land it when convenient.

- **Per-test win.** Tens of µs per long-identifier test, ms-scale per
  parse for production codebases with non-ASCII identifiers (minified
  bundles with Unicode mangling, internationalised codebases).
- **General benefit.** Every JS parse on the system gets faster. The
  external-scanner shape also makes future tweaks (e.g. accepting newer
  Unicode versions) a one-file edit, not a regex-grammar refactor.
- **Risk.** Very low. The `identifier` rule's formal definition in
  `grammar.js` is unchanged; external scanners are tree-sitter's
  prescribed extension point; the byte-comparison test rules out
  classifier drift. CLAUDE.md rule #5 (no manual `parser.c` edits) is
  respected.

### 2.5 What I would land first

`2.4.A` + `2.4.B` together: ~25 lines of code, near-zero risk, and they
target hypothesis #1, which is the most plausible single cause given the
batch-state-dependent shape. Expected: drives the 16-test cluster from
14.7 s aggregate to **~2 s aggregate** (~0.1 s/test) if hypothesis #1
holds. If it does *not* hold, the test still records the savings (small
constant on the pool) and the experiment cheaply rules the hypothesis out
so we can move to `2.4.D` / `2.4.E`.

---

## 3. Cluster B — RegExp-literal `eval` loop (~1.3 s aggregate)

### 3.1 What the tests do

Both files (`S7.8.5_A2.4_T2`, `S7.8.5_A2.1_T2`) run

```js
for (var cu = 0; cu <= 0xffff; ++cu) {
  …filter out a few code points…
  var xx = "a\\" + String.fromCharCode(cu);
  try {
    var pattern = eval("/" + xx + "/");   // ← compile a tiny regex literal
  } catch (e) {
    eval("var _" + String.fromCharCode(cu));   // ← compile a tiny var-decl
    …
  }
  assert.sameValue(pattern.source, xx, …);
}
```

→ ~65 000 `eval()` calls per test, each compiling a one-token JS source
(`/regex/` or `var _<char>`). The hot work is **eval-bound compilation
overhead**: every iteration goes through tree-sitter parsing, AST build,
MIR generation, JIT compile, execute.

### 3.2 Proposed general optimization — `eval()` compile cache

**Change.** Add a small LRU keyed on `(source, strict_mode)` (e.g. 256
entries, ~8 KiB) inside `js_builtin_eval` (`lambda/js/js_runtime.h:165`).
On hit, dispatch the cached compiled item directly; on miss, compile and
insert. Reset on heap-epoch change so the cache never holds stale MIR.

- **Implementation surface.** ~80 lines in `lambda/js/js_globals.cpp` (or
  wherever `js_builtin_eval` lives). The MIR module pointer is the cache
  value; the source `String*` (already pool-interned for short eval
  bodies) is the key.
- **Per-test win.** Cuts ~65 000 compiles per test to ≤ 256 unique
  compiles after the first iteration's worth of misses. Expected: each
  test drops from 0.65 s to ≤ 0.1 s.
- **General benefit.** Any JS program that builds short regex / call /
  expression strings and `eval`s them in a tight loop benefits — common
  in code generation, dynamic dispatch trampolines, JSON-Schema
  validators, etc.
- **Risk.** Low. eval-cache is a standard engine feature in production
  JS engines; the only correctness subtlety is invalidation on global
  redefinition of builtins (Test262Error, RegExp, …). Tying the cache
  lifetime to the heap epoch (which already increments on the rare
  redefinition paths) handles this automatically.

### 3.3 An aside: pre-compile recognised regex-literal eval

A simpler subset of 3.2 that we could land even before the full eval
cache: detect `eval(stringExpr)` where the resulting string is statically
known to be a regex literal (`"/" + simpleChars + "/"`) and emit a direct
`js_create_regex(...)` call. This is closer to the test-shaped fused
calls in `js_test262_*` and would cover these two tests exactly. Smaller
win, smaller scope. Not recommended over 3.2 unless 3.2 is rejected.

---

## 4. Implementation plan

In suggested order; each step is independently revertible.

1. **§2.4.A + §2.4.B** (name-pool length-bypass + length-prefixed hash). 1
   PR, ~30 lines, < 1 day. Measure: re-run gtest baseline; aggregate
   `sum` of per-test elapsed should drop noticeably; the 16-test unicode
   cluster's individual times should approach the standalone 0.04 s.
2. **§3.2** (eval LRU cache). 1 PR, ~80 lines + a small invalidation
   test, 1–2 days. Measure: targeted timing of the two S7.8.5 tests
   should drop ≥ 5×.
3. If §2.4.A/B did not close the unicode-cluster gap → run the §2.3
   investigative spike (timers in the batch compile path), then pursue
   §2.4.D (per-batch JIT linker symbol-set reset) if hypothesis #2 is
   confirmed.
4. §2.4.E (external-scanner BMP bitmap, runtime-initialised) — small,
   mechanical, do it whenever convenient even if the unicode cluster is
   already fast. Helps every JS parse on the system; no build-time
   generator, no committed data file, `parser.c` untouched.

## 5. Verification plan

For each landed change:

1. **Microbench** sized to the targeted mechanism (name-pool insert
   throughput, eval-with-cache hit rate, etc.).
2. **Targeted regression**: re-run `./test/test_js_test262_gtest.exe
   --baseline-only --batch-only` and confirm:
   - Compliance summary: **34 245 / 34 245 fully passing, 0 regressions**.
   - `top_slow_tests.tsv` aggregate `sum` drops; the targeted cluster
     individual times drop into the < 0.1 s range.
3. **Memory**: peak RSS and largest single-test growth in
   `top_memory_delta_mb.tsv` must not increase materially.
4. **Capture** the post-change results into a new
   `test/js262/results/release_run_004/` snapshot to preserve the
   measurement chain.

## 6. Summary

`release_run_003` has **zero tests ≥ 2 s** in the regular timing TSV. The
remaining slow tail decomposes cleanly into two clusters: 16
unicode-identifier tests at ~0.9 s each (likely a name-pool / linker
state-growth pathology — they run in 0.04 s standalone, **40× slower in
batch**), and 2 `eval`-in-tight-loop regex tests at ~0.65 s each.

Both clusters yield to *general* engine optimizations:

- A length-prefixed hash + a length-bypass intern in the name pool
  (§2.4.A/B) likely closes most of the unicode-identifier gap, while
  speeding up any program with long identifiers or computed string keys.
- A small `eval` compile cache (§3.2) — a standard JS-engine feature —
  collapses the regex-literal-in-loop tests and any production code
  that dynamically generates short scripts.

Combined expected impact on the next `release_run_004`: aggregate
per-test sum drops from 398.8 s toward ~370 s, and the slow tail above
0.5 s shrinks from 18 tests to single digits — without any test-specific
tuning, without touching the partial-pass list, and with broad benefit
to real JS workloads.
