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

#### 2.4.A — Length-bypass for very long names in the name pool — *Retracted (implemented + measured, regressed)*

> **Status.** Implemented as a 64-byte length bypass in
> `name_pool_create_strview` (threshold set above `NAME_POOL_SYMBOL_LIMIT`
> so symbol pooling was untouched), built release, and ran the full js262
> baseline twice against two clean baseline runs of the same HEAD. On the
> 58 unicode-identifier tests common to all four runs, the bypass version
> consistently came out **~+18 % slower** (28.4 s and 28.9 s vs 24.4 s and
> 24.1 s baseline). The pattern was robust across runs and across the
> aggregate; the proposal's prediction (14.7 s → ~2 s aggregate) didn't
> materialise.
>
> **Why hypothesis #1 doesn't hold against the actual binary.** The
> name-pool hashmap is O(1)-amortised, and `hashmap_sip` (SipHash-2-4)
> distributes long UTF-8 byte sequences just fine — there's no collision
> degradation to win back. Meanwhile the bypass actively *costs* per call:
> every reference to a long identifier (e.g. across parsing + scope
> binding + codegen) reallocates and re-copies the full byte range instead
> of returning the existing `String*`. Reverted in commit-pending state.

**Change (original proposal, retained for context).** In `name_pool_create_strview`, short-circuit interning for names
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

#### 2.4.B — Hash the *length-prefixed* key, not the bytes alone — *Retracted (bundled with 2.4.A, same measurement)*

> **Status.** Implemented in the same PR as §2.4.A — replaced the
> `HASHMAP_DEFINE_LENSTRKEY` emit with a hand-written hash that folded
> length into the final mix via a splitmix64-style step. Measured
> together with §2.4.A (the two share an entry path); the combined change
> regressed, and there is no reason to suspect this half on its own
> (SipHash-2-4 already includes the length implicitly in the finalisation
> step, so adding an explicit `len` mix is a no-op in practice for the
> distribution argument). Reverted alongside §2.4.A.

**Change (original proposal, retained for context).** The name pool's hash function is content-only. For mixed short
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

#### 2.4.E — Spec-faithful ID_Start / ID_Continue classification via a three-stage compressed trie (~2 KiB `.rodata`) — *Retracted (implemented + measured, no viable form)*

> **Status: implemented in full, measured, and reverted.** Details below.
> Measurement context: release `lambda.exe` at current `master` (well past
> the `release_run_003` commit `b406da25`); baselines and variants compared
> via `./test/test_js_test262_gtest.exe --baseline-only --batch-only`
> (34 240 timed tests), two runs each to bound the ~15 % run-to-run noise.
>
> **What was built (works end-to-end).**
> - `utils/gen_id_class_trie.py` — offline generator. Reads the regex
>   engine's spec-faithful ranges (`js_regex_generated_ranges_70_id_start`,
>   `…_69_id_continue` in `lambda/js/js_regex_generated_property_tables.inc`),
>   materialises the BMP bitmap, dedups it into a three-stage trie, and emits
>   `id_class_trie.inc`. Includes an **exhaustive self-check** that walks every
>   codepoint `0..0x10FFFF` and asserts the trie matches the source ranges
>   byte-for-byte. Ran clean.
> - `id_class_trie.inc` — the generated `.rodata`. Layout exactly as proposed
>   (stage1 32×u16, stage2, stage3 16-byte leaves packing ID_Start‖ID_Continue,
>   astral fallback ranges).
> - `scanner.c` — `is_id_start`/`is_id_continue` trie lookups (with ASCII
>   short-circuit + astral binary-search fallback), a `scan_identifier` that
>   drives the lexer and decodes `\uXXXX` / `\u{…}` identifier escapes inline,
>   and dispatch from `tree_sitter_javascript_external_scanner_scan`.
> - `grammar.js` — `$.identifier` added to `externals`; `parser.c` regenerated
>   (`tree-sitter generate`, +1 external token, ~172 k-line table diff).
>
> **Actual results — trie size.** ~**8.4 KiB**, not the predicted ~2.1 KiB
> (159 unique BMP leaves + 664 astral ranges vs the estimated ~55 / ~340).
> Still small in absolute terms, but 4× the estimate.
>
> **Actual results — two integration forms tried, neither viable:**
>
> 1. **Both-paths** (external token *and* the internal `identifier` regex
>    retained — the literal "+1 line in `externals`" the proposal described).
>    - *Correctness:* builds; **0 js262 exit-code changes across 34 240 tests**
>      (verified by joining per-test exit codes against baseline — not one
>      test flipped pass↔fail).
>    - *Performance:* a small but **consistent ~+4 % regression on the
>      unicode-identifier cluster** — 24.6 / 25.9 s (two runs) vs 24.1 / 24.4 s
>      baseline. Every unicode test was slower or equal; the regression is
>      unidirectional, not noise. (An earlier draft that called `mark_end`
>      per-codepoint was far worse, +25 %; moving to one `mark_end` per token
>      recovered most of it, but it never beat the internal lexer.)
>    - *Conformance:* **not achieved.** Verified directly that
>      `eval('var \u{1F600}=2')` (emoji, not ID_Start) and a digit-start escape
>      are **still accepted** — see the blocking issue below.
>
> 2. **External-authoritative** (internal `identifier` body replaced with an
>    unmatchable sentinel token so the external scanner is the *sole* producer
>    of `sym_identifier`; regenerated `parser.c` confirmed the
>    `sym_identifier_character_set_*` tables were gone).
>    - *Conformance:* would be achieved (the coarse fallback is gone).
>    - *Correctness:* **catastrophic regression** — even `var __filename = …`
>      fails with a syntax error. See blocking issue below.
>
> **Blocking issues.**
> - *(B1) tree-sitter keeps the internal lexer as a fallback.* When a token is
>   declared in `externals` *and* still has an internal grammar rule, the
>   external scanner is **consultative, not authoritative**: if it returns
>   false the parser falls back to the internal lexer. The internal lexer's
>   auto-generated `sym_identifier_character_set_*` ranges are a coarse
>   super-set of the spec (e.g. they accept all of `[0xff00..0x10ffff]`), so
>   anything my scanner rejected was re-accepted by the fallback → no
>   conformance gain in the both-paths form.
> - *(B2) the `word` directive requires an internally-lexable identifier.*
>   Removing the internal rule (to make the scanner authoritative, fixing B1)
>   collides with `word: $ => $.identifier`. tree-sitter's keyword extraction
>   lexes the `word` token with the *internal* lexer and then checks it against
>   the keyword set; with no internal identifier token, that machinery breaks
>   and ordinary identifiers after a keyword (`var __filename`) no longer
>   tokenize. Making the scanner authoritative would therefore also require
>   dropping `word` and re-expressing every JS keyword as an explicit token — a
>   large, high-risk grammar rewrite, the opposite of the advertised one-line
>   change.
> - *(B3) no performance headroom.* Independent of conformance, the external
>   scanner does **not** beat tree-sitter's highly-optimised internal
>   state-machine lexer; the trie lookup is fast but the per-token
>   external-scanner dispatch + `advance`/`mark_end` boundary costs leave it
>   neutral-to-slightly-slower. The proposal's predicted speedup did not appear.
>
> **Why it was reverted.** Against the keep-bar ("no regression in correctness
> *and* performance"), the both-paths form fails on performance (~+4 % unicode
> cluster) while delivering none of its headline value (no conformance, no
> speedup), and the authoritative form fails on correctness (breaks all
> parsing) unless paired with a major grammar rewrite that was explicitly out
> of scope. Neither form is keepable. Reverted in full: `grammar.js`,
> `scanner.c`, `grammar.json` restored from git; `parser.c` regenerated and
> verified **byte-identical** to its committed state; `id_class_trie.inc` and
> `utils/gen_id_class_trie.py` deleted; release rebuilt and JS parsing
> re-smoke-tested (keywords, identifiers, `\u` escapes) green.
>
> **Salvageable.** The generator + trie + `is_id_start`/`is_id_continue` are
> correct and self-verified. If a *different* consumer ever needs a fast
> spec-faithful classifier (e.g. replacing the binary-search
> `js_regex_cp_is_id_*` in `js_runtime.cpp:13729`), the trie can be reused
> there with none of the tree-sitter coupling above — but that is a runtime
> optimisation, not the parser-conformance win §2.4.E set out to deliver.

**Mechanism today.** The auto-generated parser
(`lambda/tree-sitter-javascript/src/parser.c:4445-4453`) classifies each
identifier codepoint via `set_contains(sym_identifier_character_set_*,
14|15, lookahead)` defined in
`lambda/tree-sitter-javascript/src/tree_sitter/parser.h:154`. That helper
is a 4-iteration binary search over 14–15 `TSCharacterRange` entries with
several branchy `lookahead >= range->start && lookahead <= range->end`
compares — roughly 8–12 conditionally-predicted operations per codepoint
on a hot path that runs on every identifier character of every parse.

The current ranges are *also* a coarse super-set of the Unicode spec
(e.g. they accept all of `[0xa1..0x167f]` wholesale). The engine's regex
side already ships the precise spec values
(`js_regex_generated_ranges_69_id_continue`,
`js_regex_generated_ranges_70_id_start` in
`lambda/js/js_regex_generated_property_tables.inc:3090, :3252` — 1414
and 1416 sorted ranges respectively). We want the *scanner* to use the
same precise values, but without inflating either the binary or the
hot-path code.

**Change.** Replace the parser-level classifier with a **three-stage
compressed trie** built from the spec ID_Start / ID_Continue tables,
shipped as ~2 KiB of `.rodata`. Three lookups per codepoint, all L1
hits, branch-free in the hot path. Architecture:

1. Declare `identifier` as an **external token** in `grammar.js`'s
   `externals` array (one line).
2. Implement an `IDENTIFIER` case in the hand-written `scanner.c` —
   never touching the auto-generated `parser.c`.
3. Pre-compute the three-stage trie offline (small Python generator,
   ~80 lines, run on Unicode-version bumps) and commit the result as
   `id_class_trie.inc`. No runtime init; lookups go straight through
   `.rodata`.

**Why three-stage.** A flat 16 KiB bitmap or even a two-stage 32-byte-
block layout (~3.2 KiB) wastes space because BMP ID_Continue has
massive structural repetition — the entire CJK Unified Ideographs block
U+4E00..U+9FFF (16,384 codepoints) is one solid run of "set", Private
Use and unassigned areas are solid runs of "unset", and Latin / Cyrillic
/ Greek / Hangul each form long contiguous strides. The first stage
deduplicates *blocks-of-blocks*; the second stage deduplicates blocks
of 64-codepoint chunks; the third stage holds the actual bit data.
Standard ICU/V8/SpiderMonkey pattern.

**Layout.**

```
BMP codepoint cp (16 bits):
  ┌──────────┬──────────┬──────────────┐
  │ cp[15:11]│ cp[10: 6]│   cp[5:0]    │
  │  5 bits  │  5 bits  │    6 bits    │
  │  ↓       │  ↓       │  ↓           │
  │ stage1   │ stage2   │  bit         │
  │ index    │ index    │  position    │
  │ (32)     │ (32 per  │  (64 per     │
  │          │  group)  │   leaf)      │
  └──────────┴──────────┴──────────────┘
```

```c
// id_class_trie.inc (generated; committed alongside parser.c)

// Stage 1: 32 entries, each a base offset into stage2.
// (uint16_t suffices because the max stage2 size is ≤ 1024.)
static const uint16_t id_class_stage1[32];                   // 64 B

// Stage 2: groups of 32 entries pointed to by stage1.
// After dedup over groups, ~12 distinct groups × 32 entries × 1 byte
// = ~384 B. Each entry indexes into stage3 (≤ 255 unique leaves).
static const uint8_t  id_class_stage2[ID_S2_LEN];            // ~384 B

// Stage 3: deduplicated 64-codepoint leaves. Each leaf packs both
// classifications (8 bytes for ID_Start, 8 bytes for ID_Continue).
// Empirically ~55 unique leaves over the BMP.
static const uint8_t  id_class_stage3[ID_S3_LEN * 16];       // ~880 B

// Astral plane (U+10000..U+10FFFF) kept as raw range fallback —
// rarely consulted on real workloads.
static const int32_t  id_start_astral[][2];                  // ~340 B
static const int32_t  id_continue_astral[][2];               // ~470 B
```

Totals across both classifications: **~64 + 384 + 880 + 340 + 470 ≈
2.1 KiB** of `.rodata`. The "+200 B" wiggle covers a tiny header with
the lengths (`ID_S2_LEN`, `ID_S3_LEN`, astral counts) so the C side
doesn't need preprocessor magic.

**Lookup.**

```c
static inline bool is_id_classified(int32_t cp, int byte_offset) {
    // byte_offset = 0 → ID_Start; byte_offset = 8 → ID_Continue.
    if ((uint32_t)cp >= 0x10000) {
        const int32_t (*ast)[2] = (byte_offset == 0) ? id_start_astral : id_continue_astral;
        size_t n = (byte_offset == 0) ? ID_START_AST_N : ID_CONT_AST_N;
        return astral_range_contains(ast, n, cp);
    }
    uint32_t c   = (uint32_t)cp;
    uint16_t g   = id_class_stage1[c >> 11];                       // load 1
    uint8_t  leaf= id_class_stage2[g + ((c >> 6) & 31)];           // load 2
    const uint8_t *p = &id_class_stage3[(unsigned)leaf * 16 + byte_offset];
    uint32_t bit = c & 63;
    return (p[bit >> 3] >> (bit & 7)) & 1;                         // load 3
}

static inline bool is_id_start   (int32_t cp) { return is_id_classified(cp, 0); }
static inline bool is_id_continue(int32_t cp) { return is_id_classified(cp, 8); }
```

Three L1 loads + a few shifts and masks. No branches in the hot path
(the BMP guard branch is taken on virtually every codepoint of real
JS). The two classifications share stage 1 and stage 2 — looking up
ID_Continue right after ID_Start reuses the same stage-1 and stage-2
cache lines.

**Cost vs the earlier flat-bitmap and two-stage proposals.**

| | Flat 16 KiB bitmap | Two-stage 32-cp blocks | **Three-stage (this)** |
| --- | ---: | ---: | ---: |
| `.rodata` total | 16 KiB | ~3.2 KiB | **~2.1 KiB** |
| `.bss` total | 0 | 0 | 0 |
| Memory loads per lookup | 1 | 2 | 3 |
| Shifts / masks per lookup | 2 | 4 | 6 |
| Branches in hot path | 0 | 0 | 0 |
| Spec-faithful | only if generated from spec data | yes | **yes** |
| Cache footprint of a single identifier scan | ~64 B of bmp | ~32 B + ~16 B | ~64 B + ~32 B + ~64 B (still ≤ 2 cache lines hot) |

The extra load and the two extra shifts are sub-nanosecond on modern
hardware. For a 70-codepoint identifier the three-stage path adds maybe
~70 ns over the flat bitmap — utterly invisible against any real test
or program. In exchange you save ~14 KiB of binary size and gain spec
conformance.

**The `IDENTIFIER` case** in
`tree_sitter_javascript_external_scanner_scan` then drives the lexer
straight-line: classify lookahead via `is_id_start`/`is_id_continue`,
advance, repeat. The case also handles JavaScript's `\uXXXX` and
`\u{XXXXXX}` identifier escapes inline (decode → classify the resulting
codepoint → advance past the escape on success). The `identifier:` rule
in `grammar.js` stays as the formal definition (used by static-analysis
tooling that doesn't link `scanner.c`); when `scanner.c` is linked the
external scanner wins.

**Generator.** A small offline Python script
(`utils/gen_id_class_trie.py`, ~80 lines) does the heavy lifting:

```
1. Read DerivedCoreProperties.txt for the target Unicode version
   (or extract ID_Start / ID_Continue from
   lambda/js/js_regex_generated_property_tables.inc — same data).
2. Materialise the raw 16 KiB BMP bitmap (one bit per cp × 2 properties).
3. Slice into 32 groups of 32 leaves of 64 codepoints each.
4. Deduplicate leaves (across both classifications, packed as 16-byte
   blocks). Build the stage-3 array + a leaf-index map.
5. Deduplicate groups. Build the stage-2 array + a group-index map.
6. Build stage-1 from the stage-2 group indices.
7. Emit id_class_trie.inc with the four arrays + the two astral lists.
8. As a self-check, walk every codepoint 0..0x10FFFF and assert
   is_id_classified(cp) matches the source ranges byte-for-byte.
```

The output is regenerated only when the target Unicode version changes
(rare — every few years). Committing it keeps the build hermetic.

**No runtime init, no `.bss`.** Because the trie is precomputed and
sits in `.rodata`, there is nothing to initialise at scanner-create
time and no concurrency to worry about. `tree_sitter_javascript_
external_scanner_create` stays as the existing one-liner that returns
NULL. The cross-process `.rodata` sharing the OS already does for
`lambda.exe`'s code segments now also covers the trie.

**Implementation surface.**

| File | Change | Lines |
| --- | --- | ---: |
| `lambda/tree-sitter-javascript/grammar.js` | Add `$.identifier` to the `externals: $ => [...]` array | +1 |
| `lambda/tree-sitter-javascript/src/scanner.c` | New `IDENTIFIER` token, `is_id_start` / `is_id_continue`, `scan_identifier`, `\uXXXX` decoder, dispatch in `external_scanner_scan`, `#include "id_class_trie.inc"` | ~120 |
| `lambda/tree-sitter-javascript/src/id_class_trie.inc` | Generated trie data (4 arrays + 2 astral range lists) | **~2.1 KiB** `.rodata` |
| `utils/gen_id_class_trie.py` | Offline generator script | ~80 |
| `Makefile` / build glue | Optional: a `make generate-id-trie` target. Not required for normal builds (the `.inc` is committed). | 0–5 |

**Tests.** A standalone correctness test that, for every codepoint
`0..0x10FFFF`, asserts `is_id_start(cp)` and `is_id_continue(cp)` match
the engine's existing spec-faithful classifier (`js_regex_cp_is_id_*`
in `lambda/js/js_runtime.cpp:13729-13744`). Mechanical equivalence
check — no false negatives possible at build time. Run automatically
under the `make build-test` target.

**Honest sizing.** This is still a *small general* win. The slow
unicode cluster's 40× batch slowdown is dominated by name-pool /
linker-state growth (§2.4.A/B/D), not by the classifier — even
eliminating the classifier entirely would save only a few milliseconds
per test. What this item buys is:

- **Per-test win.** Tens of µs per long-identifier test; ms-scale per
  parse for production codebases with non-ASCII identifiers (minified
  bundles with Unicode mangling, internationalised codebases).
- **General benefit.** Every JS parse on the system gets faster, *and*
  becomes spec-faithful (today's parser is coarsely permissive). The
  external-scanner shape also makes future tweaks (Unicode version
  bumps, identifier-rule refinements) a one-file regenerate, not a
  regex-grammar refactor.
- **Risk.** Very low. The `identifier` rule's formal definition in
  `grammar.js` is unchanged; external scanners are tree-sitter's
  prescribed extension point; the codepoint-equivalence test rules
  out classifier drift. CLAUDE.md rule #5 (no manual `parser.c` edits)
  is respected.

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

### 3.2 Proposed general optimization — `eval()` compile cache — *Retracted (implemented + measured, no signal)*

> **Status.** Implemented as a direct-mapped 256-slot cache keyed on
> `(source bytes, source length)` and gated to the "simple" Phase A
> path (indirect eval, non-strict, source ≤ 256 bytes). The cache was
> invalidated on `js_heap_epoch` change. Measured across two runs of the
> baseline and two runs of the cache build; on the targeted S7.8.5
> cluster the result was 3.05 s + 2.49 s (cache) vs 3.14 s + 2.92 s
> (baseline) — squarely inside the 15 %+ run-to-run noise band.
>
> **Why §3.2 cannot fire on the targeted tests.** Both `eval` calls in
> `S7.8.5_A2.{1,4}_T2` route around Phase A by design:
>
> 1. `eval("/a\\<char>/")` is intercepted by the existing
>    regex-literal fast path (`lambda/js/js_mir_eval_lowering.cpp:844`),
>    which calls `js_create_regexp_from_source` and returns *before* the
>    cache lookup runs.
> 2. `eval("var _<char>")` carries a `var` keyword, which trips the
>    `skip_expr_form = true` decision (`lambda/js/js_mir_eval_lowering.cpp:921`)
>    and routes the call to Phase C (full top-level script compile,
>    uncached).
>
> The proposal's predicted "65 000 compiles → ≤ 256 unique compiles"
> rests on those evals reaching a Phase A compile, which they don't.
> Reverted; no demonstrable benefit, only a maintenance surface.
>
> Salvageable future work: caching the regex-literal fast path would
> require sharing mutable RegExp objects across calls (spec-observable
> identity), so it isn't a drop-in. Phase C caching is correctness-heavy
> (direct-eval scope effects, `is_eval_direct`, var hoisting into the
> outer scope) and would need its own design pass.

**Change (original proposal, retained for context).** Add a small LRU keyed on `(source, strict_mode)` (e.g. 256
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
4. §2.4.E (external-scanner three-stage compressed trie, ~2 KiB
   `.rodata`) — small, mechanical, do it whenever convenient even if the
   unicode cluster is already fast. Helps every JS parse on the system
   *and* upgrades the parser from coarsely-permissive to spec-faithful
   ID_Start / ID_Continue; `parser.c` untouched.

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
