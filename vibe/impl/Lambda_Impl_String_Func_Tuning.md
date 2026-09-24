# Lambda String Function Tuning — Implementation Record

**Design:** [Lambda String Function Tuning — Proposal](../Lambda_String_Func_Tuning.md). Section references below (§2.3 C, §5.2-3, …) point into it.

**Status:** IN PROGRESS. Branch `claude/string-func-tuning`, worktree `temp/replace-find-fastpath`, based on master `4bdafbd8c`.

**Method:** every phase is measured against a release build of the base commit (`temp/ab/lambda-base-4bdafbd`, scratch). Median of interleaved runs, `LAMBDA_TIER=jit`, as §7 of the proposal requires. Semantics are checked by differential output comparison against the same base binary. The Lambda baseline must stay at 100%.

## Phase status

| Phase | Scope | Status |
|---|---|---|
| P0 | correctness: LR05-14, LR05-15 | done |
| P0 | LR09-31 (formatter size caps) | waiting on the user (proposal §8-2) |
| P1 | quadratic paths and few-line fixes: §5.2-1 to §5.2-5, §5.5-1 | done |
| P2 | shared `lib/` kernels | done |
| P3 | parser inner loops | done |
| P4 | formatter loops, plus the escape sink (§5.3-7) planned for P2 | done |
| P5a | HTML parser tag classes by id (§5.2-7) | done |
| P5b | pool bin index by leading-zero count (part of §5.2-6) | done |
| P5c | HTML parse memory and the §5.2-6 allocator round | done |
| P5d | `TypeMap` hash table out of line (A1v2) | done |
| P5 | the rest of structural: LambdaJS `+=`, UTF-8 position cache, hash choice | not started |

## P0 — correctness

- **LR05-14** (`str_rfind_byte`, `lib/str.c`). Added `_swar_has_byte_exact`, where each byte's sum stays below 0x100, so no flag leaks into a neighbour. The backward scan uses it, since the backward scan reads the highest flag.
  - Forward scans kept `_swar_has_byte`, which is safe because they read the lowest flag. P2 later moved them to `memchr`.
  - New tests in `test/lib/test_str_gtest.cpp`:
    - `RFindByteSwarNeighbour`: the filed reproducers.
    - `RFindByteMatchesNaive`: 2,000 random buffers of every length from 0 to 39, over an alphabet built around `c` and `c ^ 1`, checked against a naive backward scan.
  - Golden cases in `test/lambda/string_funcs.ls` (65, 66) and `test/js/string_methods.js`.
- **LR05-15** (`item_at`, `lambda/runtime/lambda-data-runtime.cpp`). A symbol's ASCII-ness is now decided from its bytes (`str_is_ascii`) instead of being assumed.
  - Golden case in `test/lambda/text_sequence.ls`, the S2.5.8 fixture: `['é', 'éfac', 'bé', 4, true]`.
- **Both tiers:** before, `4 2 '\xC3' '\xC3fac' 'b\xC3'`; after, `3 1 'é' 'éfac' 'bé'`.

## P1 — quadratic paths and few-line fixes

| Item | Change | Workload | Before (ms) | After (ms) | Speedup |
|---|---|---|---:|---:|---:|
| §5.2-1 | `strnlen(p, 10)` for `\u` escapes (`input-utils.hpp`, `input-toml.cpp`, `input-mark.cpp`) | 273 KiB of `\u00e9x` in one JSON string | 84.52 | 0.87 | 97× |
| §5.2-2 | `at_line_start()` instead of walking back to the line start (`input-yaml.cpp`, 4 sites) | 50 long plain-scalar lines, 977 KiB | 2366.41 | 2.86 | 826× |
| §5.2-2 | same | `big.yaml`, 10.6 MiB, short lines | 154.84 | 133.21 | 1.16× |
| §5.2-3 | `arena_owns` tests `arena->current` first (`lib/arena.c`) | Markdown parse, 9.0 MiB | 296.19 | 143.45 | 2.06× |
| §5.2-4 | float spelling probes 15, 16, 17 digits with `strtod`, and drops the redundant final `snprintf` (`lambda-decimal.cpp`) | `format(d, 'json')`, 43 MiB out | 1002.07 | 285.90 | 3.50× |
| §5.2-5 | the HTML text batch keeps bytes ≥ 0x80 (`html5_tokenizer.cpp`) | Chinese HTML, 3.1 MiB | 122.84 | 35.52 | 3.46× |
| §5.5-1 | the JS literal-regex matcher uses `str_find` (`js_runtime.cpp`) | `replace(/,/g)` on 158 KB | 8 | 5 | 1.6× |

Unchanged, as expected (all within noise):

| Workload | Before (ms) | After (ms) |
|---|---:|---:|
| English HTML | 36.45 | 36.21 |
| JSON parse | 178.89 | 181.16 |
| XML parse | 146.28 | 147.35 |
| CSV parse | 167.40 | 167.60 |
| HTML parse | 213.12 | 211.72 |

Measurement: `abx.py`, 5 interleaved runs per binary.

**Semantics unchanged:**
- **Parsers:** every corpus parses to a byte-identical tree, serialized with `format(d, 'mark')` and hashed. The corpora are JSON, CSV, XML, YAML, Markdown, HTML, Chinese HTML, English HTML, `\u` JSON and long-line YAML.
- **Float spelling:** `format(d, 'json')` over 360k random doubles is byte-identical (the same SHA as before the change).
- **LambdaJS literal regexes:** 14 checks over `exec`/`test`/`match`/`replace`/`search`/`split`, global, sticky and `lastIndex` are identical to the base binary.

**War story: subnormals (§5.2-4).**
- **What went wrong:** the first version probed 15, 16, 17 digits for every double. The 43 MiB JSON-output check passed, because its 360k random floats contain no subnormals. The baseline then failed six tests (four Lambda, two LambdaJS), all printing `5e-324` as `4.94065645841247e-324`.
- **Why:** DBL_DIG's guarantee only holds for normal doubles. A subnormal has fewer significant bits, so its 15-digit rounding also round-trips even when a shorter spelling exists.
- **Fix:** below `DBL_MIN` the probe starts at one digit again.
- **Check:** a standalone harness (`temp/perf/floatprobe.c`, scratch) compared the old 1..17 `sscanf` probe with the new one. It ran over 19,970,618 random bit patterns, covering every exponent and about 10k subnormals, plus edge values such as `5e-324`, `DBL_MIN`, `DBL_MAX`, `0.1` and `1/3`. There were zero differences.
- **Lesson:** a random-value corpus must cover every exponent, not just "typical" values.

## P2 — shared `lib/` kernels

**Changes:**
- **§5.3-1, one search kernel.**
  - `str_find` is the `memchr` candidate scan, with a second-byte filter, bounded at the last possible start.
  - `str_find_byte` is a `memchr` wrapper.
  - `str_rfind` scans backwards for candidates using the exact `str_rfind_byte`.
  - Lambda's `literal_find` delegates its case-sensitive path to `str_find` (rule 13).
  - LambdaJS string `replace`, `replaceAll` and replace-first go through `js_str_search` (a `str_find` wrapper) instead of libc `memmem`.
- **§5.3-2, 3: block kernels.** `str_is_ascii` ORs 32-byte blocks. `str_count_byte` and `utf8_count` count in `uint8_t` lanes over blocks of at most 255 bytes. Both shapes vectorize.
- **§5.3-4, set scanners.** `str_scan_until_any`, `str_skip_chars` and their `strn_` forms build a `StrByteSet` once per call. A single stop byte uses `memchr`.
- **Cleanup:** the SWAR helpers that became dead were removed (`_swar_has_byte`, `_swar_has_zero`, `_swar_has_highbit`, `_ctz64`, `_utf_load_u64`).
- **Pin test:** `LambdaOptStrings.LiteralSplitKernelAvoidsBytewiseComparisons` now checks that `literal_find` calls `str_find` and that `str_find` is the candidate scan.

**Kernel micro-benchmark** (`temp/perf/kbench.c`, ns per call, arm64):

| Kernel | 7–31 B | 64 B | 4 KB | 1 MiB |
|---|---|---:|---:|---:|
| `str_is_ascii` (blocks) | ±0.5 ns | 1.4× | 1.8× | 1.8× |
| `str_count_byte` (u8 lanes) | equal | 2.2× | 2.3× | 2.4× |
| `utf8_count` (u8 lanes) | equal | 1.1× | 1.2× | 1.5× |

**Workloads** (release, interleaved medians, base `4bdafbd8c` vs P2):

| Workload | Before | After | Speedup |
|---|---:|---:|---:|
| split/replace/find/index_of/len micro | 36.1 ms | 33.7 ms | 1.07× |
| CSV parse (set scanner) | 162.6 ms | 152.0 ms | 1.07× |
| LambdaJS string `replace`/`replaceAll`/`indexOf` | 118 ms | 113 ms | 1.04× (Node: 61) |

Everything else was flat. On this machine the SWAR kernels were already close to `memchr`. The value of P2 is one kernel, wider scans on x86-64, and less code, more than speed.

**Semantics unchanged:**
- **Parsers:** the ten corpora parse to byte-identical trees.
- **Lambda builtins:** the 4,000-case `replace`/`find`/`split` fuzz is identical to the base binary.
- **LambdaJS:** the literal-regex checks are identical. A new 3,000-case string-method fuzz (`indexOf`/`lastIndexOf`/`includes`/`replace`/`replaceAll`/`split`) matches Node exactly on both the base and P2 binaries.
- **Unit tests:** the kernels are checked against naive references in 5 new randomized tests (`StrKernelTest.*`), 244/244.

## P3 — parser inner loops

**Changes:**
- **§5.6 shared: lazy `SourceTracker`.**
  - `advance()` is inline and only moves the pointer.
  - `location()`, `line()` and `column()` catch up from the last synced offset. They count newlines with `memchr` and columns as non-continuation bytes, so they return the same values the per-byte loop produced, and a backward move recounts from the start.
  - The line index is built only on demand (error context), with `memchr`, instead of one push per newline during parsing. The up-front reservation of one slot per 40 source bytes (6.6 MB for a 33 MB file) is gone.
- **JSON strings.**
  - `parse_string_raw` returns a slice of the source when the string has no escapes, so there is one copy at creation. Otherwise it appends runs between escapes in bulk.
  - Object keys go from that slice straight to the name pool, without the throwaway arena `String`.
- **Markdown.** The inline prefilter is a 256-byte lookup table instead of `strpbrk`: 24% of the parse at P2. macOS `strpbrk` compares every byte with every set member. The same scan also yields the text length.
- **YAML.** Scalars and keys take their `StrBuf` from a small per-parser stack (acquire/release) instead of a malloc/free pair per scalar (~30% of the parse at P2). The parse guard frees the stack.
- **HTML.** `html5_insert_text` inserts a character token's text as runs, with one parent lookup and one append. It is used in the two hot paths (in body, and text mode for `script`/`style`/`title`), which keep the first-character newline skip and the per-NUL skip-with-error exactly.
- **XML.**
  - Attribute values and element text are appended in runs between entity references.
  - The next `<` is found with `strchr`.
  - Comment and CDATA ends use a `strchr` candidate scan for the three-byte terminator (`xml_find_terminator`) instead of `strncmp` at every offset.
- **CSV.**
  - Unquoted fields are created straight from the source slice (the scratch-buffer copy is gone).
  - Quoted fields append runs up to each quote.
- **PDF.** `endstream` and `endobj` are found with `str_find`, which is binary-safe like the `strncmp`-at-every-byte loops it replaced.

**Results** (release, interleaved medians of 7):

| Parser | P2 (ms) | P3 (ms) | P3 speedup | Since base `4bdafbd8c` |
|---|---:|---:|---:|---:|
| JSON, 33 MiB | 176.4 | 102.7 | 1.72× | 1.74× |
| YAML, 11 MiB | 130.4 | 101.5 | 1.28× | 1.52× |
| Markdown, 9 MiB | 147.1 | 115.4 | 1.27× | 2.57× |
| Chinese HTML, 3.1 MiB | 35.7 | 29.9 | 1.19× | 4.11× |
| CSV, 37 MiB | 151.0 | 129.7 | 1.16× | 1.29× |
| HTML, 13 MiB | 204.6 | 181.9 | 1.12× | 1.17× |
| XML, 23 MiB | 142.9 | 128.4 | 1.11× | 1.14× |

JSON now parses at 322 MiB/s. Node's `JSON.parse` does 449 MiB/s on the same file, so the gap has narrowed from 2.5× to 1.4×.

**Semantics unchanged:**
- **Corpora:** the ten corpora parse to byte-identical trees.
- **Repo test inputs:** 99 files across 16 formats, including CSV, HTML and subdirectories, produce identical output and diagnostics, Mermaid source spans included.
- **JSON error reports:** twelve malformed JSON files (errors on later lines, after UTF-8, inside strings and escapes) report identical messages, lines and columns.
- **Adversarial inputs:** 13 files aimed at the changed branches are identical:
  - XML comment/CDATA terminator near-misses and unterminated forms;
  - CSV `""` escapes, unclosed quotes and CRLF;
  - HTML leading-newline skips;
  - YAML nested deeper than the scratch stack;
  - Markdown text with no markup.

**Near-miss.** The scripted swap of `strbuf_free(sb)` for the YAML release call also rewrote the fallback inside the new `yaml_scratch_release`, making it call itself. It would only have fired with more than eight scratch buffers live, which tests would rarely reach. Counting replacements against allocations caught it: five frees against four allocations.

## P4 — formatter loops

**Changes:**
- **§5.3-7, one clean-run copy for every escaper** (planned for P2, landed here).
  - `lib/escape.c` has a length-aware sink. Every escaper copies the bytes that need no escaping through one helper, `escape_append_run`, and handles only the byte that stopped the run. That covers JSON, the rule-table escaper (YAML, TOML/INI/properties, JSX, graph, Radiant SVG) and the quoted escaper (Mark strings, `%q`).
  - Formatters that write a `StringBuf` directly use `escape_append_run_stringbuf`: HTML/XML, Markdown/RST/Wiki.
  - Each stop set holds exactly the bytes its escaper rewrites:
    - JSON: static, with and without 0xED, the only lead byte of a UTF-8 surrogate.
    - HTML/XML markup: eight precomputed sets, one per combination of the quote, apostrophe and non-ASCII flags.
    - Rule tables and quote options: built per call.
  - The public callback entry points (`escape_append_json_to`, `escape_append_quoted_to`) still write to their caller's sink a byte at a time.
- **§5.3-4, one byte-set kernel.**
  - `str_find_byteset`, `str_find_not_byteset` and the bounded set scanners `strn_scan_until_any` and `strn_skip_chars` share `_byteset_scan`.
  - It tests the first eight bytes one at a time, then eight bytes per step without branching, with one branch per block.
  - In isolation it scans long runs 1.66× faster (5.4 against 3.2 GB/s).
  - The byte-wise head keeps dense stops at the old cost. Without it, XML output of Chinese text, where every byte is a stop, ran at 0.78×.
  - `str_byteset_clear`, `str_byteset_add` and `str_icmp` are now inline. `lib/binsearch.h` stays header-only; `test_binsearch_gtest` links no `.c` file.
- **§5.7-2, escaping.**
  - Every text path now copies clean runs: HTML/XML text and attributes, Markdown/RST/Wiki text, JSON strings, YAML/TOML/INI/JSX/graph values and Mark strings.
  - XML writes each non-ASCII byte's numeric reference with `str_hex_encode` instead of an `snprintf`, which cost three calls per CJK character.
- **§5.7-3, integers.**
  - `print_int_value_chars` writes an int's digits into a stack buffer.
  - `format_number_impl` no longer allocates a `StrBuf` and runs `vsnprintf` twice per int.
- **§5.7-4, tag dispatch.**
  - The void and raw-text checks read the element's `name_id` (`ElementReader::tagId()`). MarkBuilder sets it from the name pool, which resolves every well-known spelling to its `MARKUP_NAME_*` record, Input pools included.
  - A table built once from the string tables answers those IDs, so an ID gives exactly the answer its spelling would.
  - Names without a markup ID (custom or uppercase) fall back to the string tables. Their binary search now folds ASCII (`str_icmp`) instead of calling `strncasecmp`.
  - Before, the per-element binary searches were 27% of HTML output.
- **§5.7-5, `format_contains_complex`.** This deviates from the proposal, which folds the check into the formatting walk; folding would make every formatter handle complex values. Instead:
  - `complex_new`, the only constructor of complex values (both tiers' literals call it), sets the process-wide flag `g_complex_value_created`.
  - `format_data` skips the whole-tree pre-walk while that flag is clear.
  - The refusal itself is unchanged, and a script that builds a complex still gets the walk.
  - A new golden case in `test/lambda/complex.ls` pins the refusal on both tiers.
- **§5.7-6, smaller items.**
  - `write_indent` appends the whole indent at once.
  - `stringbuf_emit` gained `%.*s`, with printf's semantics (it stops at a NUL). That replaced 11 `stringbuf_append_format` calls in the HTML and XML formatters, each of which ran `vsnprintf` twice.
  - YAML quote detection:
    - one byte-set pass replaces 16 `strchr` passes;
    - the reserved-word `strcmp`s run only for strings of at most five bytes;
    - `strtol`/`strtod` run only when the first byte could start a number.

**Results** (release, interleaved medians of 7; `format(d, fmt)` on the parsed corpus):

| Output | P3 (ms) | P4 (ms) | P4 speedup | Since base `4bdafbd8c` |
|---|---:|---:|---:|---:|
| HTML, from 13 MiB HTML | 104.8 | 54.7 | 1.92× | 1.93× |
| XML, from 19 MiB XML | 99.2 | 56.5 | 1.76× | 1.76× |
| Markdown, from 12 MiB Markdown | 47.5 | 24.3 | 1.96× | 1.99× |
| JSON, from 31 MiB JSON | 293.3 | 250.0 | 1.17× | 4.11× |
| YAML, same input | 297.0 | 251.9 | 1.18× | 4.10× |
| TOML, same input | 283.3 | 241.7 | 1.17× | 4.24× |
| Mark, same input | 331.4 | 312.3 | 1.06× | 3.42× |
| XML, from 3.1 MiB Chinese HTML | 93.4 | 14.7 | 6.34× | 6.77× |
| JSON and XML, 15 MiB of long paragraphs | 73.2 | 16.6 | 4.42× | 4.54× |
| LambdaJS `JSON.stringify` | 1944 | 1892 | 1.03× | — |

Node's `JSON.stringify` takes 180 ms on the same workload, so the LambdaJS gap there lies outside the escaper.

**Where output time goes now:**
- **HTML:** the escaper's scan is 31% of samples, but it is bound by memory latency on short strings: the block kernel is neutral here and 1.21× on long strings.
- **JSON, YAML, TOML and Mark:** float formatting dominates; libc `dtoa` is about 40% of YAML output. What remains is the Ryu decision (§8).

**Semantics unchanged:**
- **Formatter outputs:** 108 outputs (9 corpora × 12 formats) are byte-identical to P3.
- **Adversarial escapes:** 126 outputs are identical to P3 (6 fixtures × 20 formats plus `print`). The fixtures cover:
  - every control byte;
  - quotes, backslashes, entities and near-entities;
  - non-ASCII text;
  - YAML reserved words and number-like strings;
  - strings led by NUL.
- **LambdaJS:** `JSON.stringify` output is identical to Node and to P3.
- **Parsers:** the set scanners also serve CSV, kv, vcf, ics, eml and YAML parsing. 10 corpora, 99 test inputs and 13 adversarial files parse identically to P3.
- **Unit tests:** the new `StrKernelTest.ByteSetFindMatchesNaive` runs 20,000 randomized rounds (sparse and dense members, high bytes and NUL, lengths up to 199). `test_str_gtest` passes 245/245 and `test_binsearch_gtest` 11/11.
- **Baselines:** Lambda 5,862/5,862; test262 40,261/40,261 with no regressions; Radiant all suites pass (DOM UI Integration 125/125, layout 2,934 with no failures).

**Build breaks caught by the Radiant baseline.** Its DOM UI Integration suite runs the full `build-test`, which the Lambda baseline does not, and it failed twice:
- The first version of the `binsearch.h` change called the out-of-line `str_icmp`, but `test_binsearch_gtest` links no library. Moving `str_icmp` inline fixed it and keeps the header self-contained.
- `print.cpp` never included `print.h`; it got the header through `ast.hpp`, which the `LAMBDA_PRINT_VALUE_ONLY` build of `lambda-boundary-core` leaves out, so the new `PRINT_INT_VALUE_CHARS_CAP` was undeclared there. `print.cpp` now includes its own header.

## P5a — HTML parser tag classes by id (§5.2-7)

**Changes:**
- **The tag lists of the tree-construction rules become class bits.** Scope markers, implied end tags, formatting, special, head content, headings, list-item walk stops, and SVG entry and exit are each one `Html5TagClass` bit. The lists move verbatim to `html5_parser.cpp`, as the single source.
- **`html5_tag_classes(tag_id, name)`** answers a well-known markup name id from a table and matches any other name against the lists.
  - The table is built once by running the list classifier over every markup record, so an id answers exactly what its spelling would.
  - The builder, `MarkupNameClassTable` in `lambda/core/markup_name_classes.hpp`, is shared with P4's void and raw-text table in `html-defs.cpp`.
- **Where the ids come from.**
  - Open elements carry `TypeElmt::name_id`: MarkBuilder takes it from the name pool, which resolves every well-known spelling to its catalog record. D4.6.1v3 says generated names retain catalog IDs.
  - Tokens cache theirs on first use (`html5_token_tag_id`, one catalog lookup per tag token). `html5_token_set_tag_name` clears the cache.
- **Equality.** `html5_same_tag` compares ids only when both are markup ids, and spellings otherwise. An element whose type lost its id, as in a MarkEditor copy, still compares by bytes.
- **Call sites.**
  - The scope checks, implied-end-tag generation, pop helpers and the li/dd/dt closing walk take `(id, spelling)` targets: `HTML5_TAG(P)` expands to the catalog constant and its spelling, and token targets pass `html5_token_tag_id`.
  - 270 `strcmp(tag, "…")` tests of a token's tag in the insertion-mode dispatch became `html5_token_is(token, MARKUP_NAME_…)`. A script converted them after two checks: `tag` must be defined from `token->tag_name->chars` in the same function, and the literal must be a catalog spelling exactly. Comparisons on element tags and on `search`, which is not in the catalog, still compare strings.

**Results:**
- HTML parse, 13 MiB: 188.8 → 167.8 ms (1.12×). English and Chinese pages, 3.1 MiB each: 1.06×.
- `strcmp` fell from 25% of HTML parse to under 1%.
- HTML parse is now 54% memory management: `pool_take_block`, committed-range appends and `mprotect` from the pool, plus arena allocation and teardown. That is §5.2-6, which needs its own design round.

**Semantics unchanged:**
- **Every HTML file in the test data** — 21,602 files, WPT and real pages — produces an identical Mark tree, as do the 10 corpora, 99 test inputs and 13 adversarial files.
- **Near-miss the file differential caught.** `<image>` becomes `<img>` by assigning the token a new tag name. The first version cached the token id before that rename, so the renamed `<img>` was not treated as void. The corpora and inputs passed; two of the 21,602 files differed. All tag-name writes now go through `html5_token_set_tag_name`.
- **Baselines:**
  - Lambda: 5,862/5,862. Radiant: all suites pass.
  - One Radiant run failed the page snapshot (`zengarden`, `nojs`) while other sessions kept the load average near 7. The suite settles 200 ms after load. The same binary passed the suite on its own, and the release view trees of both pages are byte-identical between P4 and P5a.

## P5b — pool bin index by leading-zero count (part of §5.2-6)

**Change.**
- `pool_bin_index` (`lib/mempool.c`) was a shift loop computing `min(floor(log2(span)), 31)`. It ran three times per allocation: for the request, the taken block and the split remainder, about 20 steps each for the large tail block.
- It now counts leading zeros with `math_clz64`: the same bins, so the allocator picks exactly the same blocks.
- `math_clz64` is `lib/str.c`'s private `_clz64` promoted to `lib/math_utils.h` (rule 13). Its portable fallback returned 63 for 0; it now returns 64. A new `MathUtilsTest.Clz64` covers it.

**Results** (release, interleaved medians of 7, P5a → P5b):

| Parse | P5a (ms) | P5b (ms) | Speedup |
|---|---:|---:|---:|
| HTML, 13 MiB | 169.2 | 148.8 | 1.14× |
| XML, 19 MiB | 133.0 | 123.6 | 1.08× |
| Markdown | 120.7 | 117.1 | 1.03× |
| CSV | 133.8 | 130.4 | 1.03× |
| JSON, YAML | — | — | 1.01× |

**Memory regression from P5a, fixed.** P5a's cached token id grew `Html5Token` from 96 to 104 bytes. A token is allocated per character run and kept until the parse ends, so the larger pool blocks cost 16.5 MB, 2.9% more peak RSS on the 13 MiB HTML parse: 562.7 → 579.3 MB. The id now sits in the padding after `self_closing`. The token is 96 bytes again, RSS and page reclaims match P3 exactly, and HTML parse gained another 1.03×.

**Semantics unchanged:** the 10 corpora and all 21,602 HTML files parse identically. `test_mempool_gtest` passes 55/55 and `test_arena_gtest` 91/91. Lambda passes 5,862/5,862, Radiant all suites, and test262 40,261/40,261.

## P5c — HTML parse memory and the §5.2-6 allocator round

Branch `claude/html-parse-memory` from master `293b7a175`.

**Where the 562 MB went** (13 MiB HTML, temporary probe on the Input's pool and arena):

| Owner | What | Size |
|---|---|---:|
| Pool, tokens | 1.04M `Html5Token`s (320K start, 320K end, 400K text), never freed | 166 MB with headers |
| Pool, types | 320K `TypeElmt`, one private type per element, 424 B each (256 B of it the A1 inline hash table) | 156 MB with headers |
| Pool, headers | 64-byte `PoolBlock` on each of 2.2M allocations | 141 MB (overlaps the two rows above) |
| Arena | token text and names, attribute names, DOM strings | 80 MB |

Tokens, their strings and their attribute maps are garbage once the tree builder returns: it copies names and text into the document and keeps only attribute values' Items.

**Changes:**
- **Tokens.**
  - A token, its attribute array and its strings come from the parser's scratch arena. The arena is reset once it passes 64 KiB with no token in flight and destroyed when `html5_parse`/`html5_parse_ex` returns.
  - Attributes are a plain `(name, value)` array instead of a Lambda `Map`.
  - Fragment parsers and synthesized tokens keep the Input arena.
- **Pool header, 64 → 32 bytes.** Free-list links move into the free block's payload (`POOL_MIN_PAYLOAD` holds them). `requested` becomes 32 bits (`SIZE_LIMIT` fits) and doubles as the live flag, since `pool_alloc` refuses size 0. Magic, span, prev_span and extent keep the registry-free validation.
- **Pool search:** a mask of non-empty bins skips the empty ones. Bins and block choice are unchanged.
- **Pool commits:** a VM extent's step grows with its committed size up to 1 MiB, instead of one `mprotect` per 16 KiB page. Committed pages stay non-resident until touched, so RSS does not move.
- **Arena:** `arena_alloc` skips the free-list search while the arena holds no free bytes. `free_bytes` is zero exactly when every bin is empty, so this is exact.
- **Not taken: TLSF second-level classes.** With the mask, the remaining pool time is mostly the first touch of fresh memory, not list walks.

**Results** (release, 15 interleaved runs, base `293b7a175`):

| Workload | Base | After | |
|---|---:|---:|---:|
| HTML parse, 13 MiB: peak RSS | 562 MB | 315 MB | −44% |
| HTML parse: page reclaims | 35.5K | 20.4K | −42% |
| HTML parse | 156.5 ms | 113.0 ms | 1.39× |
| Chinese HTML page | 28.2 ms | 20.1 ms | 1.41× |
| XML parse | 133.6 ms | 109.3 ms | 1.22× |
| JSON / Markdown / YAML / CSV parse | | | 1.06× / 1.06× / 1.04× / 1.08× |
| XML / JSON / Markdown / CSV peak RSS | | | −5% / −4% / −5% / −5% |

**Semantics unchanged:**
- All 21,602 test-data HTML files, the 10 corpora, 99 test inputs and 108 formatter outputs match the base binary.
- `test_mempool_gtest` passes 55/55 and `test_arena_gtest` 91/91. Lambda passes 5,867/5,867, Radiant all suites, and test262 40,261/40,261.
- One pool test changed: `SplitAndCoalesceAdjacentFreeBlocks` requested exactly the merged span of two 64-byte-header blocks. It now requests 256 bytes, which only the merge can serve at either header size.

**The per-element `TypeElmt`** was 146 MB, 46% of the remaining peak; the user chose option 1 (P5d below).

## P5d — `TypeMap` hash table out of line (A1v2)

Branch `claude/typemap-hash-out-of-line` from master `cec71a8f4`; user-approved revision of A1.

**Change.**
- `TypeMap`'s open-addressing property table was 32 inline slots, 256 bytes in every map, element and object type. It is now a pool-owned `ShapeEntry** field_index`, allocated when first populated.
- The table is sized to the shape: a power of two at least twice the fields, at least 8 slots. A table used to fill up past 32 fields and fall back to the chain until the next rebuild; now it grows with the shape.
- `field_index_dynamic` is gone.
- A struct copy shares the pointer, so every copy path rebuilds through `typemap_hash_prepare`, which resets it first. The audit found that all of them already do.
- `typemap_hash_prepare` leaves the previous table to the pool rather than freeing it, since a copy may still use it.

**Why:** JS shapes are shared by many objects, so they amortized the inline table. Every element of a parsed document has a private type, so each paid the full 256 bytes.

**Results** (decimal MB, peak RSS):

| Parse | Before P5c | After P5c | After P5d |
|---|---:|---:|---:|
| HTML, 13 MiB | 562 MB | 315 MB | 233 MB |
| XML, 19 MiB | 505 MB | 480 MB | 351 MB |
| Markdown | 315 MB | 300 MB | 224 MB |
| JSON | 199 MB | 191 MB | 191 MB |

The P5c section above labels MiB values as MB. The first column here was re-measured as MB.

- **Parse speed:** HTML 1.03×, XML 1.10×, Markdown 1.04×; JSON and YAML unchanged (21 runs).
- **LambdaJS:** the AWFY bundles (deltablue, json, richards, bounce, towers, storage) are within ±3%. `bench_property.js` gets and sets are about 1% slower: one more pointer load per hashed lookup.
- **Lambda AWFY:** richards 295.5 → 293.3 ms and deltablue 1.06×, over 21 runs.

**Checks:**
- All 21,602 HTML files, the 10 corpora, 99 inputs and 108 formatter outputs are identical.
- Lambda passes 5,868/5,868, Radiant all suites, and test262 40,261/40,261.
- The four `TypeMapHash*` unit tests assumed a stack `TypeMap` had slots without a pool. They now prepare their table through a pool. They are joined by `TypeMapHashIsAbsentUntilPopulated` and `TypeMapHashOwnedInsertGrowsWithTheShape`.
- The design descriptions were updated in `doc/dev/lambda/LR_03_Value_and_Type_Model.md` and `vibe/Lambda_Design_Runtime_Structs.md`. No `D#` ruling covered the inline table.

## Findings not yet filed

These were found while measuring; each predates the branch (the base binary behaves the same). The two bugs go to the LambdaJS ledger once another session's edits to `vibe/JS_Issue_Ledger.md` have landed.

- **LambdaJS `console.log` truncation:** a single call silently truncates its output at 4,095 bytes (`console.log("x".repeat(5000))` prints 4,095 characters). The base binary does the same. It hid most of the first JS fuzz run. To be filed in the LambdaJS ledger.
- **Regex `split` cost:** LambdaJS regex `split` costs about 2.5 µs per input position (396 ms on 158 KB where Node takes under 1 ms), because it runs `exec` at every position (proposal §5.5-6). The literal-matcher change cannot help it; a bulk split path is needed.
- **Pre-existing bug:** `"a/b".split(/\//)` does not split in LambdaJS, while Node splits it. The base binary behaves the same, so this predates the branch. To be filed in the LambdaJS ledger.
- **JSON error messages print a literal `%s`.** `json_consume_comma_or_recover` reports `addError(loc, "%s", expected_message)`, and the output reads `error: %s`.
- **`input()` stops at the first NUL byte of a file.** It reads the file as a NUL-terminated string, so an HTML document with a NUL in body text loses everything after it.
- **CSV errors report a position of line 1, column 1.** The CSV parser does not advance the shared tracker.
- **A failed `format()` is `null` under the JIT but `error` under the interpreter.** `fn_format2` returns a null `String*`. The interpreter's `eval_sys_call` deliberately maps a null `String*` to `ItemError` ("the error carrier"), while the MIR lowering boxes it as `null`. Found while pinning the complex refusal; the golden case uses `or`, which rescues both (S3.1, S7.5.3).
- **XML output writes each UTF-8 byte of non-ASCII text as its own numeric reference.** `café` becomes `caf&#xc3;&#xa9;`, which any XML reader, Lambda's included, decodes as `cafÃ©`; CJK text is mangled the same way. This is deliberate: the code comment and `test/lambda/pdf/phase28_winansi_encoding.ls` pin the byte-wise form for decoded PDF text. Changing it needs a decision.
- **The parser's "special" element list lacks `search`,** which WHATWG added; the tree builder's own list-item stop list has it. Kept as is, since changing either list changes tree construction.
- **YAML output leaves `True`, `TRUE`, `Null` and similar strings unquoted.** YAML 1.2's core schema reads them as booleans and null, so the round trip changes their type. The reserved-word list only has the lower-case spellings.
