# Lambda Lib Grep — implementation record

**Design:** `vibe/Lambda_Lib_Grep.md` (GRP1–GRP31). **Started and landed in worktree `lib-grep`:** 2026-10-04. **Status:** phases G-pre, G0–G5, G5A, G6 (packed pair, Teddy) and G7 built and tested on macOS arm64, and after them the count mode (GRP30) and line endings (GRP31); G5B waits on the separate caching proposal; G8 not started.

## 1. What was built

| Phase | Content | Where |
|---|---|---|
| G-pre | RE2 `2023-03-01` vendored in-tree, built out of tree with its own CMake; `make verify-re2-patches`; setup scripts no longer clone it | `lib/re2/`, `lib/re2/VENDOR.md`, `Makefile`, `setup-*-deps.sh`, `build_lambda_config.json` |
| G0 | Rare-byte prepared needle; `str_find` now anchors on the needle's rarest byte (measured byte-frequency table) | `lib/str.c` (`StrNeedle`, `str_needle_init`, `str_needle_find`, `str_byte_rank`) |
| G1–G3 | Matcher (pattern assembly, never_nl, multi-line anchors, word/whole-line wrapping, unsupported-construct status), literal planner (tiers 0/1/2 from RE2's parse tree), line engine (CRLF, line numbers, code-point offsets, line text, invert, context, per-file limit, sink actions), streaming over `LineFramer` | `lib/grep/grep.h`, `grep_matcher.cpp`, `grep_literal.cpp`, `grep_searcher.cpp` |
| G4 | Parallel walk on `ThreadPool` (dir and file jobs, searcher pool = one RE2 per worker), ignore layers (`.gitignore`/`.ignore` stack incl. the enclosing repository's, hidden entries, built-in directory list), caller filters, symlinks skipped, sorted delivery with deterministic total limit | `grep_walk.cpp`, `grep_ignore.cpp` |
| G5, G5A | `io.grep(source, pattern, options?)`, procedural, both tiers; S17.8.1 compile-time check (registry option table) and run-time warning | `lambda/runtime/io_grep.cpp`, `sys_func_registry.{c,h}`, `build_ast.cpp`, `lambda.h` |
| G6 | Packed-pair SIMD kernel (NEON, SSE2, scalar), used by `str_needle_find` and so by `str_find`; Teddy for literal sets of 2–64 (GRP29): NEON on ARM64, SSSE3 on x86-64 chosen by one CPUID check per process, portable elsewhere; a plan's literal cap rose from 8 to 64 | `lib/str_simd.c`, `lib/str_teddy.c` (`StrTeddy`) |
| G7 | SSE2 and NEON twins of RE2's front-and-back prefix kernel | `lib/re2/re2/prog.cc`, `patches/re2-simd-prefix-accel.patch` |
| GRP30, GRP31 | Count mode: `GrepOptions.count_lines`, the count handed to `file_done`, counted lines cut by the total limit in sorted and parallel delivery, bulk counting of inverted gaps and of patterns that match every line. `GrepLineEnding` on every record, kept through sorted buffering. `io.grep` options `count` and `line_ending` | `grep.h`, `grep_searcher.cpp`, `grep_walk.cpp`, `grep_matcher.cpp`, `io_grep.cpp`, `sys_func_registry.c` |

Shared-code changes made on the way (CLAUDE.md rule 13): `runtime_result_shape` extracted from `create_match_map` and reused by `io.grep`; `pattern_unanchored_source` replaces two copies of the "strip `^…$`" logic; `runtime_pattern_from_type` and `tp_hardware_threads` promoted from `static`; `line_framer_data` added.

## 2. Decisions taken during implementation (for review)

These are not covered by a GRP ruling, or interpret one. Each is a reading the user may overturn.

1. **Word mode** wraps the pattern as `(?:^|[^\pL\pN_])(PAT)(?:[^\pL\pN_]|$)` and reports group 1, resuming after the group so a shared separator can lead the next match. The boundary is Unicode letters/digits/`_`, not RE2's ASCII `\b` (so `caf` does not match inside `café`).
2. **file_done after a total limit.** The file in which a total limit is reached still gets its `file_done` (with the matches delivered from it), in sorted and in parallel mode; other files do not. `io.grep` emits per file, so without this its last match was lost (found by the fixture).
3. **Case-insensitive literals** keep only ASCII runs: a non-ASCII rune under folding cuts the literal (§5.4, conservative). RE2 already turns `k`/`s` into classes.
4. **Ancestor ignore files** apply only inside a git repository: from the walk root up to the nearest directory holding `.git`. Outside a repository nothing above the root counts.
5. **`.git` is not in the built-in list**: the hidden layer covers it, and with `hidden: true` it is searched (ripgrep behaves the same).
6. **`io.grep` sources.** A path source reports `file` as a Lambda path (the source path plus segments); a text source reports text joined from the given text, not the absolutized path. A trailing `*` sets depth 1, `**` is recursive; the ignore layers still apply below the source.
7. **`io.grep` context arrays** hold the neighbouring lines' text whether or not those lines match; `before`/`after` override `context`. Line numbers and line text are turned on internally for this.
8. **The run-time S17.8.1 warning** is `log_warn` — the codebase's warning channel (S12.3.7's shadow warning uses it too). It reaches the console in debug builds and `log.txt` always.
9. **Binary inputs**: a NUL in the first 8 KiB; a stream waits for the whole window before deciding, so results never depend on chunking. Skipped binary files get no `file_done`.
10. **Defaults**: stream line cap 64 MiB (streams only); threads = cores, at most 8; file chunk 256 KiB.
11. **Pure literal sets run in tier 0** (Teddy, no RE2). This reverses a note in the proposal's §5.1, which kept alternations of literals in tier 1 as "not worth the risk" of disagreeing with RE2's choice among alternatives at one position. The planner (`lit_expand`) expands the parse tree into its finite language in RE2's preference order — alternation branches in order, concatenation as a cross product with earlier pieces varying slowest, which also covers RE2's factoring of `mem_alloc|mem_free` into `mem_(?:alloc|free)` — and the first literal of that list occurring at the leftmost hit is RE2's leftmost-first match. Folding, classes, anchors, repetition and empty alternatives stay in tier 1. Tested against the per-line RE2 reference on `abc|ab`, `(?:a|ab)(?:c|bcd)`, `b|ab|abb` and a factored set. Gain: 8 literals 97 → 88 ms.
12. **Teddy's fingerprint is 3 bytes while each bucket holds one literal (≤ 8), 4 bytes beyond.** A 4th byte cut 24 literals from 206 to 98 ms (kernel alone) but slowed 3 literals from 40 to 52 ms, where 3 bytes already left ~0.2% false candidates. A first-byte check precedes each verify's `memcmp`.
13. **No confirming RE2 call without context or invert.** Enumerating a candidate line already says whether it matches; the separate zero-submatch call is kept only when context or invert must know before enumeration. (Measured as neutral; kept as the simpler path.) Counting (GRP30) also confirms, since nothing enumerates a counted line.
14. **A count record always names its file** (`{file, count}`), even for a single-file source, as a `files` result is always a path; a file with no selected line gets no record, as in `rg -c`.
15. **The limits count lines when counting.** `limit_per_file` caps each file's count and `limit` caps the sum, cutting the count of the file it ends in — GRP25's rule in the new unit. A cut count is partial, as a cut file's matches are.
16. **`count` with `files` is an error** (E312): both say what a result is. The options that add fields to match maps (`line`, `byte_offset`, `text`, `line_ending`, context) do not apply to a count and are ignored, as they are under `files`; `line_ending` with `count` is the user's ruling (GRP31).
17. **Counting without matching where the answer is known.** An unwrapped empty pattern matches every line (`GrepMatcher.every_line`), so its count is the block's newlines plus an unterminated last line; under invert, the lines between two matching lines are counted the same way instead of being visited. Both are tested against the general path, whole and chunked. Other patterns that match every line (`x*`, `a|`) take the general path. Gain on 265 MB: `""` 331–357 → 44 ms, `-v the` 175–177 → 99–100 ms.
18. **The searcher enforces a per-input cap.** The walker passes the total limit as a cap on each file (`grep_search_file_as(…, cap)`) and the searcher stops at the smaller of it and `max_matches_per_file`. This replaced the sorted buffer's own cap check: a count delivers no records for a sink to check, so the searcher has to stop itself. At the library level `GrepMatch.line_ending` is always filled, since it costs one subtraction; `io.grep` adds the field only on request.

## 3. Verification

- `test/lib/test_grep_gtest.cpp` (25 tests): every corpus pattern through its planned tier and forced to tier 2 agrees with an independent per-line RE2 reference, in `\n` and `\r\n` lines, with case folding; stream results equal buffer results for every chunk size 1–40 with and without context; invert/context/limits/sink actions; glob semantics; walk layers, filters, explicit roots, ancestor ignores, deterministic total limit for 1–8 threads (100× repeated); the RE2 prefix kernel against a naive search at every alignment.
- `test/lib/test_str_gtest.cpp`: rare-byte and packed-pair scans against a naive search (random inputs, every alignment around the vector width).
- `test/lambda/proc/io_grep.ls` (20 cases, identical on interp, jit and auto) and `test/lambda/negative/semantic/io_grep_unknown_option.ls` (registered in `test_lambda_errors_gtest`).
- Count mode and line endings (GRP30, GRP31; 30 tests now): every corpus pattern's count, in its planned tier and forced to tier 2, with and without folding and invert, equals the number of distinct lines the per-line reference's matches fall on (or the rest, inverted); counts equal for every chunk size; the empty-pattern and inverted-gap bulk counts equal the general path, whole, chunked and capped; line endings on match, context and inverted records and through the sorted walk; sorted counts with a total limit identical for 1–8 threads, and an unsorted count's sum capped. `io_grep.ls` gained T21–T27 (counts per file, invert, both limits, `line_ending` incl. `null`, the empty pattern, `files` with `count` an error), identical on interp, jit and auto; new fixture `test/input/grep_tree/eol.txt` (`-text` in `.gitattributes`).
- Guard Malloc (`libgmalloc`) and malloc scribbling over the grep and str tests: clean.
- Both SSE2 paths were run as x86_64 builds under Rosetta: the RE2 prefix kernel (120,000 checks against a naive search) and `lib/str_simd.c`'s packed pair (200,000 checks, case-sensitive and folded).
- Teddy: `StrSearchTest.Teddy*` (random sets of 1–64 literals with mixed folding, against a naive search and against the portable form); the tier check now includes sets past the old cap and priority-sensitive pure sets. The SSSE3 kernel and its CPUID dispatch ran as an x86_64 build under Rosetta (`kernel: ssse3`, 100,000 random sets), NEON natively (100,000). Guard Malloc with `MALLOC_STRICT_SIZE=1` over exactly-sized heap haystacks found no read past the end in Teddy (100,000) or the packed pair (200,000).
- `make test-lambda-baseline`: 6206/6206 with RE2 vendored and nothing else changed. After the whole change one test failed: `LambdaOptStrings.LiteralSplitKernelAvoidsBytewiseComparisons` pins the source text of `str_find`'s old first-byte kernel to stop a regression to a comparison at every byte. The kernel changed by design (GRP8), still verifies only at candidates, and the test now pins the new structure (`str_needle_find`, its `memchr` on the rarest byte, and the pair kernel's candidate-only verify).

## 4. Measurements (release flags, Apple M-series, warm cache)

ripgrep 14.1.1 as the reference; times are lib/grep's own wall time against `rg` including its ~10–15 ms start-up.

| Corpus | Pattern | lib/grep | rg |
|---|---|---|---|
| test262, 349 MB, 54k files | any of 6 patterns | 0.83–0.87 s | 0.85–0.93 s |
| one 265 MB file | `TODO` | 44 ms | 55 ms |
| | `the` (693k matches) | 97 ms | 102 ms |
| | `Symbol.asyncIterator` | 38 ms | 57 ms |
| | `log_error\("[a-z_]+:` | 42 ms | 54 ms |
| | `-i todo` | 41 ms | 66 ms |
| | `(?:mem_alloc|mem_free|mem_calloc)\(` | 40 ms | 71 ms |
| | `\w+\s+=\s+\d+;` (no usable literal) | 491 ms | 117 ms |

**The Lambda repo itself** (24,299 files after ignore rules; 1,043 Markdown, 1,606 C/C++), 10 threads, include globs as `rg -g`. Every query below gave the same total and the **same set of matched files** as `rg` (`rg -l` diffed against lib/grep's sorted file list), so the walk semantics — `.gitignore`, hidden entries, binary files, symlinks, globs — agree as well as the matching:

| Files | Queries | lib/grep search | lib/grep wall | rg wall |
|---|---|---|---|---|
| `*.md` | `TODO`, `Lambda` (18,543), `S17\.[0-9]+\.[0-9]+`, `-i ripgrep`, `^#+ ` (32,539), `-F io.grep(` | 48–56 ms | 74–82 ms | 69–90 ms |
| `*.c *.h *.cpp *.hpp` | `log_error`, `TODO\|FIXME`, `static inline`, `mem_alloc\(`, `struct [A-Za-z_]+ \{`, `-i kelvin`, `[A-Za-z_]+ = NULL;` | 53–56 ms | 77–83 ms | 72–76 ms |
| everything | `RootFrame`, `TODO` | 300–306 ms | 326–334 ms | 319–326 ms |

The one difference is by design: whole-repo `TODO` finds 2 files fewer than `rg`, both under this work's own fixture `test/input/grep_tree/{bower_components,venv}/`, which the built-in directory list skips (GRP14) and ripgrep has no list for. Sorted delivery (what `io.grep` uses) costs 1–18 ms on top. `io.grep` from Lambda returns the same counts over the repo; four repo-wide searches took 0.59 s in the debug build.

Literal sets (265 MB, `a|b|c…`, all counts equal to ripgrep's). Before Teddy: 3 literals 76 vs 77 ms; 8 literals 148 vs 78 ms; 12 literals 468 vs 86 ms; 24 literals 487 vs 101 ms. With Teddy and pure sets in tier 0 (two warm runs each): 3 literals 75–79 vs 72–77 ms; 8 literals 87–88 vs 77–80 ms; 12 literals 95–98 vs 86–94 ms; 24 literals 92–104 vs 88–100 ms. The kernel alone (every occurrence, `teddy_bench`): 3 literals 38 ms, 8 literals 49 ms, 24 literals 108 ms. The 8-literal set has 40,096 matches; what remains is per-match work, not the scan.

The packed pair took `-i todo` from 121 to 41 ms and the alternation from 82 to 40 ms. Tier 2 stays RE2-bound, as §10 predicted. Match counts agree with ripgrep except where RE2's classes differ: `\w` is ASCII and `\s` has no `\v` in RE2, Unicode and with `\v` in ripgrep; with `(?-u)` and an explicit class the counts are equal.

`str_find` itself (G0 gate; 265 MB, every occurrence, against a copy of the old first-byte kernel): a needle with a rare first byte is unchanged (`Zebra` 5.0 → 5.1 ms, `QuickSort` 6.8 → 6.8 ms); a common first byte gains 10–13× (`the end` 169 → 13 ms, `error` 230 → 21 ms, `return NULL;` 161 → 16 ms), `TODO` 27 → 14 ms. In-memory `find`/`replace`/`split`/`contains` with a literal needle all go through it.

RE2 prefix kernel (64 MB, no match): `hello\w+` 0.55 → 12–13.6 GB/s, `the zebra` 0.97 → 5.8–6.2 GB/s, `hz[0-9]` 0.61 → 40–46 GB/s; `qq\d` (first byte absent) 62 → 43–46 GB/s.

**Counting (GRP30)** against `rg -c`, 265 MB / 6.06 M lines, two warm runs, wall time of both processes; every count equal to ripgrep's:

| Pattern | count | same matches reported | `rg -c` |
|---|---|---|---|
| `TODO` (672 lines) | 55–56 ms | 54–55 ms | 47 ms |
| `the` (474,649 lines) | 83–88 ms | 112–115 ms | 73–75 ms |
| `-i todo` | 69–72 ms | 69–73 ms | 69–70 ms |
| `\w+\s+=\s+\d+` | 481–490 ms | 510–513 ms | 192–193 ms |
| `-v the` (5,590,151 lines) | 99–100 ms | 203–211 ms | 133–140 ms |
| `""` (every line) | 44 ms | — | 177–178 ms (`wc -l` 243 ms) |

Whole repo, per-file counts: `the` 196,901 lines in 5,896 files and `""` 4,661,162 lines, both equal to `rg -c` except the two fixture files the built-in directory list skips (1 line each; `""` 335 vs 463 ms).

## 5. Not done, and why

- **G5B, file-based `find(path)`**: waits on the separate caching proposal (GRP27).
- **Teddy**: built on 2026-10-04 (GRP29); see §1 and decisions 11–13.
- **Newline and code-point counting kernels**: `str_count_byte` and `utf8_count` are block loops the compiler vectorizes (their own comments measure it); no intrinsics were needed. **Reverse byte search** stays the SWAR `str_rfind_byte`.
- **G8 multi-line matching**: optional, no consumer.
- **S17.8.1 migration of existing functions**: the mechanism is generic (`sys_func_option_names`), but only `io.grep` is registered; `find`/`replace` and the rest still accept unknown names silently (design Appendix C).
- **Linux and Windows**: the build and setup-script changes are untested there.

## 6. Draft semantics ruling for `io.grep` (not ratified)

GRP26's gate asks for a draft. On 2026-10-04 the user had it recorded as the `io.grep` ruling in the `io` module's design doc, `vibe/Lambda_IO_Shell.md` (renamed from `Lambda_Shell.md`); `doc/Lambda_Formal_Semantics.md` is unchanged until it is ratified there.

> **S17.9.1*** **`io.grep(source, pattern, options?)` searches files line by line and is a procedure.** A match never spans a line terminator; a line ends at `"\n"` or `"\r\n"`, whose `"\r"` is never matched. Results are match maps `{value, index}` in a stable order — sources as given, files in path order, matches in position order — where `index` counts code points from the start of the file as in-memory `find` does (S17.4.1); options add `file`, `line` (1-based), `byte_offset`, `text`, `line_ending`, `before`/`after`, or make the result one `{file, count}` per file, as `grep -c`. A directory honours ignore files and skips hidden entries and dependency directories; a source named explicitly is always searched. *Why: its result depends on the file system, which `fn` may not (S12.1.1v2); a line-oriented search is what makes it fast and is what grep means.* [S12.1.1v2, S17.4.1, S17.8.1; GRP1, GRP4, GRP14, GRP26, GRP30, GRP31]
