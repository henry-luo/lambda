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
| P4 | formatter loops | next |
| P5 | structural | planned |

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

## Findings not yet filed

These were found while measuring; each predates the branch (the base binary behaves the same). The two bugs go to the LambdaJS ledger once another session's edits to `vibe/JS_Issue_Ledger.md` have landed.

- **LambdaJS `console.log` truncation:** a single call silently truncates its output at 4,095 bytes (`console.log("x".repeat(5000))` prints 4,095 characters). The base binary does the same. It hid most of the first JS fuzz run. To be filed in the LambdaJS ledger.
- **Regex `split` cost:** LambdaJS regex `split` costs about 2.5 µs per input position (396 ms on 158 KB where Node takes under 1 ms), because it runs `exec` at every position (proposal §5.5-6). The literal-matcher change cannot help it; a bulk split path is needed.
- **Pre-existing bug:** `"a/b".split(/\//)` does not split in LambdaJS, while Node splits it. The base binary behaves the same, so this predates the branch. To be filed in the LambdaJS ledger.
- **JSON error messages print a literal `%s`.** `json_consume_comma_or_recover` reports `addError(loc, "%s", expected_message)`, and the output reads `error: %s`.
- **`input()` stops at the first NUL byte of a file.** It reads the file as a NUL-terminated string, so an HTML document with a NUL in body text loses everything after it.
- **CSV errors report a position of line 1, column 1.** The CSV parser does not advance the shared tracker.
