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
| P2 | shared `lib/` kernels | next |
| P3 | parser inner loops | planned |
| P4 | formatter loops | planned |
| P5 | structural | planned |

## P0 — correctness

- **LR05-14** (`str_rfind_byte`, `lib/str.c`). Added `_swar_has_byte_exact`, where each byte's sum stays below 0x100, so no flag leaks into a neighbour. The backward scan uses it, since the backward scan reads the highest flag.
  - Forward scans keep `_swar_has_byte`: they read the lowest flag, which is always a real match.
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
| §5.2-1 | `strnlen(p, 10)` for `\u` escapes (`input-utils.hpp`, `input-toml.cpp`, `input-mark.cpp`) | 273 KiB of `éx` in one JSON string | 84.52 | 0.87 | 97× |
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

**Findings along the way:**
- **Regex `split` cost:** LambdaJS regex `split` costs about 2.5 µs per input position (396 ms on 158 KB where Node takes under 1 ms), because it runs `exec` at every position (proposal §5.5-6). The literal-matcher change cannot help it; a bulk split path is needed.
- **Pre-existing bug:** `"a/b".split(/\//)` does not split in LambdaJS, while Node splits it. The base binary behaves the same, so this predates the branch. To be filed in the LambdaJS ledger.
