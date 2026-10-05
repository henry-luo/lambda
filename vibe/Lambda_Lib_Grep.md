# Lambda Lib Grep — a C+ grep library on RE2 (`lib/grep`)

**Date:** 2026-10-04

**Status:** Ruled and implemented (2026-10-04, worktree `lib-grep`): G-pre, G0–G5, G5A, G6 (packed pair, and Teddy per GRP29) and G7 are built; G5B waits on the caching proposal. Implementation record, decisions taken during the build, measurements and a draft `io.grep` ruling: `vibe/impl/Lambda_Impl_Lib_Grep.md`. **Ruled by the user, 2026-10-04:** GRP3 (vendor-free public API), GRP4 (results are matches, line number optional), GRP6 (one RE2 object per worker), GRP7 (internal use of `re2/regexp.h`), GRP8 (SIMD kernels in `lib/`), GRP9v2 (no RE2 patch for grep's own acceleration; one RE2 patch, the NEON + SSE2 prefix kernel, is in scope for in-memory `find`/`replace`/`split`), GRP14 (ignore defaults), GRP15 (vendor RE2 at `lib/re2/`), GRP16 (stay on RE2 `2023-03-01`), GRP17 (UTF-8 only in v1), GRP18 (`\n` and `\r\n` terminators), GRP19 (symlinks not followed), GRP20–GRP23 (line text, per-file sink control, walk filters, whole-line match, all in v1), GRP24 (inverted match and context lines, after GRP20), GRP25 (per-file and total match limits), GRP26 (`io.grep()` system function, procedural), GRP27 (file-based `find()` stays an `fn` and calls `lib/grep` underneath; its session caching is a separate proposal), GRP28 (unknown option names; ratified as S17.8.1), GRP30 (a count mode, as `grep -c`) and GRP31 (records report their line's terminator), both built the same day; and, on 2026-10-05, GRP32 (`io.grep` does not replace). The remaining decisions are proposals; §11 lists the open ones.

**Scope:** a line-oriented text search library under `./lib/grep`, built on the RE2 that Lambda already links, and the Lambda system function `io.grep()` that exposes it to scripts (§9B). The library covers pattern compilation, literal acceleration, single-buffer and streaming search, and parallel directory search. Other consumers (file-based `find(path, pattern)`, a CLI command, the editor's find panel) keep their own design docs.

**Spec linkage:** no `S#`/`D#` ruling covers a grep library, so this doc opens the ledger series `GRP#`. `io.grep()` is a new system function: it joins the one registry of S17.2.1 and, once implemented, needs its own ruling in §17 of the semantics spec. GRP28 is ratified as **S17.8.1** (spec 56.0.0). Two existing rulings constrain the library: S17.7.1 (case-insensitive matching folds by Unicode simple case folding) binds the `ignore_case` option and the literal prefilter (§5.4), and S17.6.1 binds any later replace feature built on this library. CLAUDE.md rule 16 governs every RE2 patch discussed in §6 and §7.

**Prior art documented in §3:** ripgrep (the architecture this design follows), Hyperscan / Vectorscan (streaming and multi-pattern reference), Nushell (the surface model: one `find` over values).

**Related docs:** `vibe/Lambda_Type_String_Grep.md` (file-based `find()`/`replace()`, the first intended consumer), `vibe/Lambda_Type_String_Pattern2.md` (in-memory pattern functions), `vibe/lib/Lib_Enhance5.md` (the `lib/str` scanner tier this library extends).

---

## 1. Summary

The library follows ripgrep's architecture: search is line-oriented, a literal prefilter finds candidate lines at memory speed, and the regex engine runs only on those candidates. RE2 stays the only regex engine and is not modified in the first version.

The three questions in the brief have these answers:

| Question | Answer | Section |
|---|---|---|
| Follow ripgrep's overall design? | Yes. The matcher / searcher / sink / walker split maps cleanly onto C+ and onto helpers `lib/` already has. | §4 |
| Does RE2 need a patch for literal acceleration? | **Not for grep.** The acceleration that matters is done outside RE2, before it is called. RE2's own prefix acceleration is weak on Lambda's builds; the line-oriented design makes that irrelevant to grep, but it does slow in-memory `find`/`replace`/`split`. By the user's ruling, one RE2 patch (a NEON prefix kernel) is part of this proposal to fix that. | §6 |
| Can it stream across chunks? | **Yes for line-oriented search, with no RE2 change**: this is how ripgrep streams. Matches that span lines across a chunk boundary need either a bounded overlap window or a patch to RE2's DFA; both are deferred. | §7 |
| Backreferences, lookaround | Out of scope. The compile call reports "unsupported" and the caller routes to the engine it already has. | §8 |

---

## 2. Current state in Lambda

Verified against the tree on 2026-10-04.

**RE2 is pinned and not vendored in-tree.** The setup scripts clone tag `2023-03-01` (commit `3a8436ac`, the last line before RE2 required Abseil) into `build_temp/re2-noabsl`, and the Makefile builds `libre2.a` there with CMake. No `-mavx2` or `-march` flag is passed. Lambda has no RE2 patches today; `patches/` holds MIR, ThorVG, woff2 and WPT deltas only.

**RE2 is reached through two wrappers.** `lib/re2_glue.hpp` owns allocation (`re2_glue_compile`, `re2_glue_release`) and default options. `lambda/runtime/re2_wrapper.hpp` builds Lambda patterns on top (`pattern_find_all_options`, `pattern_replace_all_options`, `pattern_split`). Neither uses `RE2::Set`, `FilteredRE2`, or any internal RE2 header.

**The existing search helpers in `lib/`:**

| Helper | What it gives | Gap for grep |
|---|---|---|
| `str_find` (`lib/str.c`, "the one literal-search kernel", T28-5) | `memchr` on the needle's first byte, second-byte filter, `memcmp` | Always anchors on the first byte, even when it is a common one. No multi-needle or case-insensitive form. |
| `LineFramer` (`lib/line_framer.h`) | Append bytes, peek complete data, consume a prefix | Exactly the rolling buffer a streaming searcher needs. |
| `dir_walk`, `dir_list`, `file_glob`, `file_find` (`lib/file_utils.h`) | Serial recursive walk with a callback; `fnmatch` for names | Serial. No `.gitignore` handling, no `**`. |
| `ThreadPool` (`lib/thread_pool.h`) | `tp_submit`, `tp_wait_all`, priorities | Sufficient for a parallel walker. |
| `file_read_all`, `read_binary_file` (`lib/file.h`) | Whole-file reads | No memory-mapped read. `mem_vm` reserves anonymous regions, not file mappings. |

**No SIMD intrinsics exist in `lib/`.** Fast scans rely on libc `memchr` (vectorized on all three platforms) and SWAR.

**The backtracking engine** for constructs RE2 cannot do is `lambda/js/js_bt_regex`, selected by `js_regex_scanner_needs_backtrack()`. Lambda's own string patterns never need it: an island `&` is rejected outright (S11.1.2v3) rather than lowered to a lookahead.

---

## 3. Prior art

### 3.1 ripgrep

ripgrep is a Rust command-line tool; its parts are published as separate crates, and the split is the design this proposal copies.

| Crate | Role | Lambda counterpart (proposed) |
|---|---|---|
| `grep-matcher` | An abstract `Matcher` interface so the searcher is engine-agnostic | Not copied. One engine (RE2), so the matcher is a concrete struct. |
| `grep-regex` | Compiles the pattern, strips line terminators from what it can match, extracts literals | `grep_matcher.cpp`, `grep_literal.cpp` |
| `grep-searcher` | Reads input, finds lines, handles binary detection, context lines, encodings | `grep_searcher.cpp`, `grep_stream.cpp` |
| `grep-printer` | Formats results (standard, JSON, summary) | A `GrepSink` callback table; formatting belongs to the consumer. |
| `ignore` | Parallel directory walk, `.gitignore`, hidden files, file types | `grep_walk.c`, `grep_ignore.c` |
| `memchr`, `aho-corasick` | SIMD byte, substring and multi-substring search (including Teddy) | An extended kernel in `lib/str.c`, later an optional SIMD tier |

The ideas that produce the speed, in order of payoff:

1. **Skip files.** Ignore rules, hidden-file and binary-file detection mean most bytes on disk are never read.
2. **Line-oriented search with a pattern that cannot match a line terminator.** If no match can contain `\n`, a buffer holding many lines can be searched in one call, and every match lies inside one line.
3. **Inner-literal candidates.** From the pattern, extract a set of literals such that every match contains at least one. Scan the buffer for those literals with SIMD; for each hit, find the enclosing line and run the regex on that line alone. The literal can come from anywhere in the pattern, not just the front, because the line supplies the bounds.
4. **Pure-literal patterns never reach the regex engine.** A pattern that is a literal, or an alternation of literals, is answered by substring search directly.
5. **Rare-byte selection.** The substring searcher does not anchor on the needle's first byte. It ranks the needle's bytes by how common they are in typical text and scans for the rarest, so the scan seldom stops.
6. **Parallel walk.** Directory traversal and file search share a work-stealing pool.
7. **Reading strategy.** A reused fixed buffer for many small files; memory maps for a few large ones.

Streaming in ripgrep is a consequence of item 2, not a regex feature: the searcher fills a buffer, searches up to the last complete line, moves the partial tail to the front and refills. A multi-line pattern turns this off and the whole file is read or mapped.

### 3.2 Hyperscan and Vectorscan

Hyperscan is Intel's regex library for network intrusion detection; Vectorscan is the BSD-licensed fork that adds ARM NEON/SVE and POWER back ends. Its design differs from RE2's in ways worth recording, because two of them (true streaming and multi-pattern matching) are the things this library cannot get from RE2.

| Aspect | Hyperscan / Vectorscan | Consequence for this proposal |
|---|---|---|
| Pattern decomposition | Each pattern is split into literal factors and small automata, coordinated by a component called Rose. Literal matching (FDR, Teddy) drives everything. | The same idea as ripgrep's inner literals, taken further. §5 adopts the idea at the line level only. |
| Multi-pattern | Thousands of patterns in one pass, each match tagged with a pattern ID | Not a goal. Several `-e` patterns are joined into one alternation. |
| Streaming | `hs_open_stream` / `hs_scan_stream` keep fixed-size state per stream, so a match can span chunk boundaries with no buffering | The model for a "true streaming" RE2 patch, and the reason that patch reports match ends only (§7.3). |
| Match semantics | Reports every match **end**; start-of-match is opt-in, costs extra and has a bounded horizon | Shows the cost is inherent: finding a match start needs text that a stream has already discarded. |
| Captures | None | RE2 keeps this advantage. |
| Compile cost | Slow compile, large database; built to compile once and scan for a long time | Wrong shape for an interactive `find()` that compiles a fresh pattern per call. |
| Platforms | Linux, macOS, BSD; Windows support in Vectorscan is weak or absent (from memory, not re-checked) | Rules it out as Lambda's engine: Lambda ships on Windows. |
| Build | CMake, Boost headers, Ragel | A heavy dependency next to what Lambda vendors today. |

Conclusion: Vectorscan is recorded as prior art and as the reference design for streaming, not as a dependency. Adopting it would mean two matching paths with different semantics and no Windows build.

### 3.3 Nushell

Nushell has no grep. Read from its source (`crates/nu-command/src/filters/find.rs`) and documentation on 2026-10-04:

| Aspect | Nushell | Consequence for this proposal |
|---|---|---|
| The command | `find` is a pipeline filter over data already in the shell: strings, lists, records, tables and byte streams. It takes plain terms or `--regex`, with `--ignore-case`, `--invert`, `--multiline`, `--dotall`, `--columns` and `--rfind`. | The same shape as Lambda's in-memory `find(str, pattern)`: a function over values, not a file searcher. |
| Engine | The `fancy_regex` crate: the Rust `regex` engine with a backtracking layer for backreferences and lookaround. | The same split GRP12 makes, but inside one crate. Lambda keeps the two engines separate. |
| Lines | A multi-line string is split into lines and each line is tested, unless `--multiline`. A byte stream is processed line by line and cannot be combined with `--multiline`. | Line-oriented by default, as GRP1. Its streaming is the same line-at-a-time idea as §7.1, without a prefilter. |
| Files and directories | It does not read files or walk directories. Users compose it: `open f \| lines \| find pat`, or `ls **/*.rs \| each { ... }`. For real recursive search the Nushell book and community point to calling `rg`. | This is the gap Lambda's `find(path, pattern)` fills natively. |
| Ignore rules, parallelism, literal acceleration | None in `find`. | Nothing to borrow for performance. |
| Output | Matching items, with matches highlighted by ANSI codes unless `--no-highlight`. Structured data stays structured: a table row matches if any (or a named) column matches. | Worth borrowing at the Lambda surface later: searching the fields of maps and elements is a natural extension of in-memory `find`, and is outside this library. |

Conclusion: Nushell is prior art for the surface (one `find` over values, options as flags, structured results), not for the engine.

### 3.4 What RE2 itself provides

Read from the pinned source (`build_temp/re2-noabsl/re2/`).

| Capability | Where | Notes |
|---|---|---|
| Prefix acceleration | `Regexp::RequiredPrefixForAccel` (`regexp.cc:729`), `Prog::ConfigurePrefixAccel` (`prog.cc:1014`) | Only a literal at the **very start** of the pattern, seen through capture groups. No inner or suffix literals, no alternation of literals. |
| Prefix scan kernels | `Prog::PrefixAccel_FrontAndBack`, `PrefixAccel_ShiftDFA` (`prog.cc`) | The front-and-back kernel is compiled only under `__AVX2__`. Lambda's build does not define it, so on every Lambda platform the kernel is `memchr` on the prefix's first byte. A case-insensitive prefix uses a scalar shift-DFA. |
| Earliest-match DFA | `Prog::SearchDFA` (`dfa.cc:1859`) | With zero submatches requested, the DFA stops at the first match and the reverse DFA never runs. This is the cheap "does this line match" call. |
| `never_nl` option | `parse.cc:404`, `413`, `1926` | Removes `\n` from literals, classes and `.` at parse time. This is the line-terminator stripping of §3.1 item 2, already built in. |
| Parsed pattern access | `RE2::Regexp()` (`re2.h:331`), type in `re2/regexp.h` | A public accessor returning the internal AST. The header ships in the source tree Lambda already uses as its include path. |
| Atom extraction | `FilteredRE2`, `re2/prefilter.h` | Public, but returns atoms through `std::vector<std::string>`, lowercases them, and hides the AND/OR structure. |
| Pattern sets | `RE2::Set` | Reports which patterns matched, through `std::vector<int>`. |
| State snapshot across cache resets | `DFA::StateSaver` (`dfa.cc`) | Internal. It already saves a DFA state as its instruction list and restores it after the cache is flushed, which is the hard part of a resumable search. |
| Streaming API | none | A search call takes the whole text; no state survives it. |

---

## 4. Design overview

### 4.1 Components

| Component | File (proposed) | Responsibility |
|---|---|---|
| Public API | `lib/grep/grep.h` | C-callable declarations (`extern "C"`), option structs, the sink table |
| Matcher | `lib/grep/grep_matcher.cpp` | Wraps the user pattern, compiles it through `re2_glue_compile`, owns the literal plan. Immutable after construction. |
| Literal extraction | `lib/grep/grep_literal.cpp` | Walks RE2's parsed pattern and produces the required-literal set (§5.2) |
| Scan kernels | `lib/str.c` (extended), `lib/str_simd.c` | Prepared-needle substring search with rare-byte selection (§5.3) |
| Searcher | `lib/grep/grep_searcher.cpp` | The line-oriented engine over one contiguous buffer: candidate → line bounds → confirm → enumerate matches → report. Optional line numbers, binary detection. |
| Stream | `lib/grep/grep_stream.cpp` | Push interface over `LineFramer` (§7.1) |
| Walker | `lib/grep/grep_walk.c`, `lib/grep/grep_ignore.c` | Parallel directory traversal on `ThreadPool`, ignore rules, per-file dispatch (§9) |

### 4.2 Data flow

1. The caller compiles a `GrepMatcher` from a pattern and options. This runs once per search.
2. Each worker thread owns a `GrepSearcher`: its buffers, counters and configuration. Searchers are never shared.
3. For each input, the searcher gets bytes (a whole buffer, a file, or pushed chunks), runs the tiered search of §5.1 and calls the `GrepSink` once per match.
4. For directory search, the walker feeds file paths to searchers through the thread pool and merges sink output.

### 4.3 Decisions

**GRP1. Search is line-oriented.** The unit of searching is the line: matches are found inside lines and never span them (what is reported is the match, GRP4). The pattern is compiled so that it cannot match `\n`. This is the property that makes both the literal prefilter (§5) and streaming (§7.1) sound, so it is the default and the only mode in v1.

**GRP2. RE2 is the only engine and is used through its existing boundary.** Compilation goes through `lib/re2_glue.hpp`. The library adds no second allocation path for RE2 objects.

**GRP3 (USER, 2026-10-04). `lib/grep` exports its own well-defined API and exposes no vendor type.** `lib/grep/grep.h` is C-callable, includes no RE2 header, and uses only `lib/` types and opaque handles. A caller passes a pattern as text and never sees or supplies an `re2::RE2`, a `re2::Regexp` or a `StringPiece`. RE2 could be replaced behind this API without touching a caller. Inside the library, RE2 entry points that traffic in `std::vector` or `std::string` (`RE2::Set::Match`, `FilteredRE2::Compile`) are not used, which CLAUDE.md rule 3 would otherwise put in tension with the library.

**GRP4 (USER, 2026-10-04). The unit of a result is the match, and the line number is optional.** The sink receives one record per match: its byte offset in the input, its length, and its text. When the caller asks for line numbers, the record also carries the line number; line numbers are 1-based, as in `grep -n`, ripgrep and editors, while byte offsets are 0-based, as in `grep -b`; when it does not, the searcher never counts lines, which saves a pass over the bytes between matches. This is the shape the Lambda surface needs: `find(path, pattern)` returns `{value, index}` per match, and `find(path, pattern, {line: true})` returns `{value, index, line}` (with `file` added for a directory search, per `vibe/Lambda_Type_String_Grep.md`). **Positions (USER, 2026-10-04): a file match's `index` is in code points, counted from the start of the file, the same unit in-memory `find` uses.** The library computes it, because only the library sees the bytes between matches: a consumer of a streamed or parallel search receives match records, not the skipped text, and could not count across it. So each record carries the byte offset always (it is free) and the code-point offset when the caller asks for it, exactly as the line number is optional. The count uses the same function in-memory `find` uses (`str_utf8_count`), so the two agree by construction, including on invalid UTF-8. Counting is a pass over the bytes before the last match; it shares that pass with line counting and gets a SIMD kernel in §5.3. **Byte offset at the Lambda surface (USER, 2026-10-04): opt-in, like `line`.** `find(path, pattern, {byte_offset: true})` adds a `byte_offset` field; the default stays `{value, index}`. The sink callback's return value controls the search (GRP21); match limits are built in (GRP25).

Lines remain the unit of *searching* (GRP1): matches are found inside lines and never span them. They are no longer the unit of *reporting*.

**GRP5. Pattern preparation.** The matcher wraps the user's pattern as `(?m:…)` so `^` and `$` mean line boundaries, sets `never_nl`, sets `ignore_case` through RE2's case-folding (which is Unicode simple case folding, as S17.7.1 requires), and joins several patterns into one alternation. A fixed-string option escapes the pattern and marks it pure-literal.

**GRP6 (USER, 2026-10-04). One RE2 object per worker thread.** An RE2 object is thread-safe, but its lazy DFA sits behind a reader/writer lock taken on every search call. The tiered search of §5.1 makes one short call per candidate line, so several threads sharing one object would contend on that lock constantly. Compiling the pattern once per worker costs compile time and DFA memory times the thread count, and needs no RE2 change.

**GRP17 (USER, 2026-10-04). v1 searches UTF-8 only.** Input bytes are treated as UTF-8 and never transcoded. A UTF-16 file is not detected as text: its NUL bytes make it binary under §7.1's rule. Byte-order-mark detection and transcoding are out of scope for v1.

**GRP18 (USER, 2026-10-04). A line ends at `\n` or `\r\n`.** The `\r` of a `\r\n` pair belongs to the terminator, never to the line: it is not in the reported text, a match cannot include it, and `$` matches before it. A lone `\r` is ordinary line content. The mechanics are in §5.5.

---

## 5. Literal acceleration

### 5.1 The three tiers

| Tier | When | How a buffer is searched |
|---|---|---|
| **0: a literal set** | The pattern is one case-sensitive literal, or a pure set of them (an alternation, also as RE2 factors it) | Substring scan only — the packed pair for one literal, Teddy for a set (GRP29). Each hit is a match; for a set, the first literal in pattern order that occurs at the leftmost hit is RE2's leftmost-first match. RE2 is never called, and line bounds are never computed unless line numbers are requested. |
| **1: required literals** | Every match must contain at least one literal from a small extracted set | Scan for the literals. For each hit, find the enclosing line with `memrchr`/`memchr`, then ask RE2 whether that line matches (zero submatches, so the earliest-match DFA runs). If it does, enumerate the matches in that line. After a line is done, resume scanning after it. |
| **2: no literal** | Nothing usable was extracted (for example `\w+\s+\d+`) | RE2 searches the buffer directly and each match is reported. RE2's own prefix acceleration applies where it can. |

Enumerating the matches of a confirmed line is a loop of RE2 calls on that line with one submatch requested, which is where match starts are computed. It runs only on lines already known to match, so the cheap zero-submatch call still rejects the false candidates.

~~An alternation of literals is tier 1, not tier 0: RE2 picks the leftmost match and, among alternatives starting at the same place, the first one written (`ab|abc` matches `ab`), and reproducing that outside the engine is not worth the risk of disagreeing with it.~~ **Changed during implementation (2026-10-04, for the user's review):** a pure set of case-sensitive literals is tier 0. The planner expands the parse tree into its finite language in RE2's preference order (alternation branches in order, concatenation as a cross product with earlier pieces varying slowest), so the first literal of the list occurring at the leftmost hit is exactly RE2's leftmost-first match; folded literals, classes, anchors, repetition or an empty alternative leave the pattern in tier 1. It is tested against the per-line RE2 reference on priority-sensitive sets (`abc|ab`, `(?:a|ab)(?:c|bcd)`, `b|ab|abb`, a factored `mem_alloc|mem_free|mem_calloc`). Without it an 8-literal set took 97 ms where ripgrep takes 78.

### 5.2 Extracting literals

**GRP7 (USER, 2026-10-04: internal use of the header is fine). Literals are extracted by walking RE2's parsed pattern, obtained through `RE2::Regexp()`, with `re2/regexp.h` included from the pinned source tree.** The walk computes, for each node, a set of literals of which at least one must appear in any match of that node:

- a literal string yields itself;
- a concatenation yields the best set among its children (the one whose shortest literal is longest, with ties broken by rarity);
- an alternation yields the union of its branches' sets, and yields nothing if any branch yields nothing;
- a repetition with a minimum of zero yields nothing; with a minimum of one or more it yields its child's set;
- a capture group passes through; a character class, `.`, an anchor or an assertion yields nothing.

The plan is rejected (falling to tier 2) when the set is empty, has more literals than the kernel handles, or its shortest literal is under a minimum length. A set of one-byte literals is still useful if the bytes are rare.

Why this route and not the alternatives:

| Alternative | Why not |
|---|---|
| `FilteredRE2` (public API) | Returns `std::vector<std::string>`; atoms are forced to lowercase, so the scan must be case-insensitive even for a case-sensitive pattern; the AND/OR structure is hidden. |
| A separate regex parser in `lib/grep` | Duplicates RE2's parser and will disagree with it on corner cases. A prefilter that disagrees with the engine is unsound. |
| Extraction from Lambda's pattern AST | `lib/` cannot depend on `lambda/`, and JS or CLI patterns arrive as regex text, not as a Lambda AST. |
| Patching RE2 to export literals | Unnecessary: the accessor is already public. |

**What using `re2/regexp.h` costs.** None of these blocks the design; they are the things to get right.

| Issue | Detail | Handling |
|---|---|---|
| Not part of RE2's API contract | RE2's CMake install publishes four headers (`re2.h`, `set.h`, `filtered_re2.h`, `stringpiece.h`). `regexp.h` is not among them, and the accessor's own comment says "not for general use". Upstream may change the type freely. | Moot once RE2 is vendored at a fixed commit (GRP15, GRP16). A future RE2 upgrade has to re-check the walker; a test per node kind pins it. |
| Static linking only | A shared `libre2` exports only the symbols in `libre2.symbols`, and no `Regexp` symbol is listed. | Lambda links `libre2.a`, where every symbol is available. This becomes a constraint: RE2 cannot later be switched to a system shared library. |
| Header pollution | `regexp.h` pulls in `util/logging.h`, which defines unprefixed macros (`LOG`, `CHECK`, `DCHECK`, `LOG_INFO`, `LOG_ERROR`), plus `<map>`, `<set>` and `<string>`. None collides with `lib/log.h` today. | Include it in exactly one file, `grep_literal.cpp`, and expose only a plain-C literal plan from that file. |
| No `std::` at the call sites | The walk needs only `op()`, `nsub()`, `sub()`, `rune()`, `runes()`, `nrunes()`, `min()`, `max()` and `parse_flags()`, all plain accessors. RE2's own `Walker` template uses `std::stack`. | Write a direct recursive walk under `lib/recursion_guard.hpp` and do not use `Walker`. CLAUDE.md rule 3 is kept. |
| It is the unsimplified tree | `RE2::Regexp()` returns the pattern as parsed, before simplification, so counted repetitions and every other node kind appear as written. | Every node kind the walk does not explicitly understand yields "no literals", which is always sound. |
| Borrowed pointer | The tree is owned by the RE2 object. | The walk runs inside matcher construction and keeps no pointer into the tree. |

If the header ever becomes unavailable, the library degrades to tier 2 and stays correct.

### 5.3 Scan kernels

**GRP8. The substring kernel is an extension of `str_find` in `lib/str.c`, not a second kernel.** `str_find` is documented as the one literal-search kernel, so the rare-byte improvement belongs there (CLAUDE.md rule 13). The proposal adds a prepared-needle form: preparation picks the needle's rarest byte from a static byte-frequency table and records its offset; the scan runs `memchr` on that byte and verifies around the hit. `str_find` keeps its signature and can adopt the same selection for long needles.

Kernel coverage by phase:

| Literal set | v1 kernel | Later |
|---|---|---|
| One literal | Rare-byte `memchr` + verify | A packed-pair SIMD kernel (two rare bytes at a fixed distance), NEON and SSE2 |
| Two or three literals | A two- or three-byte scan over each literal's rare byte + verify | Teddy |
| More | Falls to tier 2 | Teddy, or Aho-Corasick for large sets |

v1 uses no intrinsics, matching the rest of `lib/`. The SIMD tier is a separate phase with its own measurement gate.

**GRP8 addendum (USER, 2026-10-04). SIMD kernels are part of this proposal and live in `lib/`, not in `lib/grep`.** They sit beside `str_find` (proposed file `lib/str_simd.c`, declared in `lib/str.h`), so the in-memory string functions get them too: `find`, `replace` and `split` with a literal needle go through `str_find` and pick up the packed-pair kernel with no change at their call sites. What the tier covers (phase G6). libc `memchr` is already vectorized, so the gain is not in finding one byte. It is in the scans libc has no function for:

| Kernel | What it replaces | Why it helps |
|---|---|---|
| Packed pair | Rare-byte `memchr` + verify | Tests two bytes of the needle at a fixed distance in one pass, so far fewer false stops than one byte gives |
| Two- and three-byte search | Several `memchr` passes, or a byte loop | Needed for case-insensitive literals (upper and lower form) and small literal sets |
| Teddy | One `str_needle_find` per literal, and RE2 for sets past 8 | Searches a set of 2–64 short literals in one pass by nibble-table lookups; built (GRP29) |
| Newline counting | A byte loop or repeated `memchr` | Line numbers (GRP4) need the count of `\n` between matches |
| Code-point counting | The scalar/SWAR loop in `str_utf8_count` | The code-point `index` (GRP4) needs the count of non-continuation bytes between matches; in-memory `find` uses the same function and benefits too |
| Reverse byte search | The SWAR `str_rfind_byte` | Finding a line start; macOS and Windows have no `memrchr` |

Each kernel exists in three forms: NEON for ARM64, SSE2 for x86-64 (always present there, so no runtime dispatch), and the portable v1 code as the fallback and the test oracle. Every SIMD form is tested against the portable one on the same inputs, including every alignment and every length around the vector width. AVX2 would need runtime CPU detection and is not proposed.

**GRP29 (USER, 2026-10-04). Teddy is built for literal sets, with a one-time runtime CPU check on x86-64.** Measured after G7, a set of literals scanned once per literal fell behind ripgrep from about four literals on (8: 148 vs 78 ms; over the 8-literal cap, RE2 took over: 12: 468 vs 86 ms, §10.1). Teddy searches the whole set in one pass:

- the literals are split into up to 8 buckets, and each one's first 1–4 bytes are its fingerprint (3 while every bucket holds one literal, 4 once buckets share: in lowercase text the high nibbles barely differ, so a shared bucket needs the extra byte); for every 16 input bytes, each byte's low and high nibble index two 16-entry tables of bucket masks (`pshufb` / `tbl`), the masks of the fingerprint positions are ANDed, and only positions with a surviving bucket are verified against that bucket's literals; in fold mode a letter's tables carry both cases;
- it lives in `lib/` beside the other kernels (`lib/str_teddy.c`, `StrTeddy` in `lib/str.h`), for sets of 2 to 64 literals; the literal cap of a plan rises from 8 to 64 (`GREP_MAX_LITERALS`);
- NEON on ARM64 (`tbl` is always present); on x86-64 the kernel needs SSSE3 `pshufb`, which the SSE2 baseline does not guarantee, so it is compiled for SSSE3 with a function target attribute and chosen by one CPUID check per process; any other CPU takes the portable form, which is also the test oracle.

This amends GRP8's no-runtime-dispatch rule for this kernel only; the other kernels stay on their baselines.

### 5.4 Soundness

A prefilter may report lines that do not match; it must never skip one that does. Three rules follow.

**Case-insensitive literals.** Under Unicode simple case folding (S17.7.1), ASCII `k` also matches U+212A (Kelvin sign) and `s` matches U+017F (long s), which do not contain the bytes `k`, `K`, `s` or `S`. So for a case-insensitive literal, the scan byte is chosen only among bytes whose fold set is ASCII-only: letters other than `k` and `s` (scanned as a two-byte upper/lower pair) and caseless ASCII. A literal with no such byte, or with non-ASCII letters, contributes no prefilter and the plan falls to tier 2. RE2's parser already helps here: under case folding it turns a letter whose fold set has more than two members into a character class (`Regexp::ParseState::PushLiteral`, `parse.cc`), which the walk treats as yielding nothing, so `k` and `s` break a literal run on their own. The walk still applies the rule explicitly and does not depend on that behavior.

**Invalid UTF-8.** Literals are compared as bytes, and RE2 in UTF-8 mode simply fails to match across invalid sequences, so the prefilter is a superset of the engine on any input.

**The confirming call sees the same context the full search would.** RE2 is called with the line as the text. Because the pattern cannot match `\n` and `^`/`$` are in multi-line mode, a line searched alone gives the same answer as that line inside the buffer. Word-boundary assertions at the ends of a line agree as well, since `\n` is not a word character.

### 5.5 Line terminators (GRP18)

RE2's multi-line `$` matches before `\n` only, and `.` matches `\r`, so a buffer of `\r\n` lines searched whole gives wrong answers in two ways: `foo$` misses `foo\r\n`, and `.+` matches the empty line `\r\n`. No RE2 option fixes this, and rewriting the pattern (ripgrep's opt-in CRLF mode rewrites `$`) leaves the `\r` inside matches.

The design avoids both by making the per-line call the judge:

- **Line bounds.** When the searcher cuts a line out of the buffer, a `\r` immediately before the `\n` is dropped from the line. RE2 then sees the line as its whole text, so `$` matches at its end and the `\r` cannot be matched.
- **Tiers 0 and 1** already confirm each candidate on its own line, so they are correct with no further work.
- **Tier 2** searches the buffer whole to find candidates. If the buffer contains a `\r\n`, each candidate line is re-confirmed by a per-line call, which removes matches that existed only because of a `\r`. If the pattern also contains an end-of-line or end-of-text assertion (known from the §5.2 walk), the whole-buffer search could miss lines, so tier 2 searches such inputs line by line.

The cost falls only on tier-2 patterns over CRLF input. An input with no `\r\n` takes the same path as before.

---

## 6. Does RE2 need to be patched for literal acceleration?

**GRP9v2 (USER, 2026-10-04). Grep's own acceleration needs no RE2 patch; one RE2 patch, the NEON prefix kernel, is in scope for the in-memory string functions.** This replaces the first ruling of the same day ("no patch in v1"; see Appendix S).

Why grep itself needs none:

1. RE2's acceleration is limited to a literal prefix, and on Lambda's builds its only kernel is `memchr` on the prefix's first byte (§3.4). That is a real weakness of RE2 taken alone.
2. In tier 0 and tier 1, RE2 is never asked to scan a large buffer. It is called on one candidate line at a time, where its prefix acceleration contributes nothing measurable.
3. Tier 2 patterns are those from which no literal could be extracted. A pattern with a literal prefix normally yields a tier-1 plan (the prefix is itself a required literal; the exceptions are a prefix under the minimum length or one §5.4 rules out), so RE2's prefix kernel is rarely what a tier-2 search waits on.
4. Everything the prefilter needs from RE2 (the parsed pattern, `never_nl`, the earliest-match call) is reachable today.

### 6.1 The SIMD prefix kernel (RE2 patch)

**What it fixes.** In-memory `find`, `replace` and `split` with a regex pattern call RE2 on the whole string. They are not line-oriented (a match may span newlines), so grep's inner-literal prefilter is not sound for them, and they depend on RE2's own prefix acceleration. For a pattern such as `hello\w+`, RE2 skips to places where `hello` could start. Its fast kernel, `Prog::PrefixAccel_FrontAndBack`, tests the prefix's first and last byte together, 32 bytes at a time, but is compiled only under `__AVX2__`. Lambda's build never defines that, and ARM64 has no AVX2, so on every Lambda platform the skip is `memchr` on the first byte alone, which stops at every `h`.

**The patch (USER, 2026-10-04: NEON and SSE2).** Two more implementations of the same front-and-back test inside `Prog::PrefixAccel_FrontAndBack` (`lib/re2/re2/prog.cc` after GRP15), each 16 bytes at a time: NEON under `__ARM_NEON` for ARM64, and SSE2 under `__SSE2__` for x86-64, where it is always available and so needs no runtime detection. The existing AVX2 path stays first in the chain for any build that enables it. Roughly 30 lines per path; the scalar tail and the function's contract are unchanged. Recorded as `patches/re2-simd-prefix-accel.patch`, listed in `lib/re2/VENDOR.md`, checked by `make verify-re2-patches`.

**Reach.** Every RE2 search in Lambda whose pattern starts with a case-sensitive literal of two or more bytes: Lambda string patterns, the JS RegExp path, the Python and Ruby hosts. A case-insensitive prefix uses a different kernel (the shift-DFA) and is not affected.

**Alternative considered: no patch.** A prefix prefilter can be run outside RE2: find the prefix with `lib/`'s own SIMD kernel, then call RE2 anchored at each candidate. That keeps RE2 unmodified, but pays RE2's per-call setup at every candidate instead of resuming inside the DFA loop, and has to be wired into each caller. The patch is smaller and covers every caller at once.

**Gate.** Benchmarks of in-memory `find`/`replace`/`split` on long strings, release build, before and after; plus RE2's own test for the function ported to `test/lib`, comparing the NEON and SSE2 paths against `memchr`-based results on every alignment.

### 6.2 Vendoring RE2 in-tree

**GRP15 (USER, 2026-10-04). RE2 moves in-tree, vendored the way MIR is, so that a patch has somewhere auditable to land.** Today the three setup scripts clone it into `build_temp/re2-noabsl`, which leaves no tracked source to patch. The MIR pattern (`lambda/mir/VENDOR.md`) carries over directly:

- the source lives in a tracked directory with a `VENDOR.md` naming the upstream URL, the commit and the list of local patches;
- patches live under `patches/re2-*.patch` and are already applied to the tracked source, not applied at build time;
- a `make verify-re2-patches` target clones pristine upstream at the recorded commit, applies the patches and diffs the result against the tracked directory;
- only what `libre2.a` needs is kept: `re2/*.cc` and `re2/*.h`, the parts of `util/` the library compiles, `LICENSE` and the upstream README. `re2/testing`, `re2/fuzzing`, the benchmarks, Bazel files and Python bindings are dropped. The kept source is about 0.8 MB.

Location (USER, 2026-10-04): `lib/re2/`, beside the other vendored code `lib/` already holds (`lib/sqlite`, `lib/font/woff2`), because `lib/re2_glue.hpp` and `lib/grep` both depend on it and `lib/` may not depend on `lambda/`. The build change is in `build_lambda_config.json` (include path and library entry), the Makefile's `RE2_LIB` rule, and the clone steps of the three setup scripts, which are removed. CLAUDE.md rule 16's list of vendored directories gains the new path.

Vendoring is phase G-pre in §10. It changes no RE2 source: the first vendored tree is byte-identical to the pinned tag, and the patch list starts empty.

### 6.3 Which RE2 version to vendor

**GRP16 (USER, 2026-10-04). Vendor the currently pinned tag, `2023-03-01`, and do not upgrade.**

The pinned tag is the last release line before RE2 began requiring Abseil; the directory name `re2-noabsl` records that this was deliberate. Upstream is still maintained (latest release tag `2025-11-05`). What changed in `re2/` after the pinned tag, from the upstream commit list as read on 2026-10-04:

| Change | Date | Matters to Lambda? |
|---|---|---|
| Abseil becomes a required dependency | mid-2023 | The cost of upgrading. Abseil is a large multi-target library that would have to be vendored and built on all three platforms. |
| `(?<name>expr)` named-group spelling accepted | 2023-08 | Minor. The JS regex path may already rewrite this spelling. |
| Unicode data 15.0 → 15.1 | 2023-10 | Minor. 15.1 added one block of CJK ideographs and no case-folding changes relevant to S17.7.1. |
| Counted repetitions of empty-width operators no longer expanded | 2023-07 | A compile-size fix for unusual patterns. |
| DFA inner loop: redundant checks removed | 2023-11 | A small speed-up, size unstated upstream. |
| Fixes for "ancient bugs" in Latin-1 handling and prefix factoring | 2023-12, 2024-02 | Correctness fixes for the parser's factoring of alternations. Worth reading individually. |
| Clearer error for look-behind | 2024-01 | Diagnostic only. |
| BitState micro-optimization | 2025-11 | Small. |

None of these is a matching-engine redesign, and none adds the literal acceleration or streaming this proposal needs. Weighed against taking on Abseil, an upgrade is not worth it for grep.

Two caveats on that reading. It comes from commit titles, not from reading the diffs or the release notes, and it was not checked for security advisories. And the factoring fixes are real bugs that the pinned version still has; if one of them is shown to affect Lambda, the fix can be backported as the first entry under `patches/re2-*.patch`, which is exactly what GRP15 prepares for.

---

## 7. Streaming across chunks

### 7.1 Level A: line-oriented streaming (v1)

**GRP10. Chunked input is supported for line-oriented search with no RE2 change.** Because no match can span a line terminator (GRP1), a chunk boundary only ever splits a line, never a match that the searcher would otherwise have found.

The mechanism is `LineFramer`: the caller pushes chunks of any size; after each push the stream takes the data up to the last line terminator, searches it with the ordinary buffer searcher, and consumes it, leaving the partial last line in the framer for the next push. A finish call searches whatever remains as a final unterminated line.

Details the design has to pin:

- **Offsets and line numbers** are absolute across the whole stream; the stream carries a running byte offset and line count.
- **Context lines** (GRP24, phase G5A) require the framer to keep the last N lines unconsumed; context after a match is delivered as later chunks arrive.
- **A line longer than the buffer** grows the framer up to a configured cap. Past the cap the stream reports an error for that input and does not truncate silently.
- **Binary detection** runs on the first chunk (a NUL byte, as ripgrep does), with the policy (skip, or search as text) set by an option.
- **File search uses the same path**: reading a file in fixed-size chunks through the stream bounds memory for arbitrarily large files. Whole-buffer search remains for inputs already in memory.

### 7.2 Level B: multi-line patterns (deferred)

A multi-line mode drops GRP1 for a search. Two ways to support it without touching RE2:

| Approach | Works when | Cost |
|---|---|---|
| Overlap window | The pattern's maximum match length L is finite (no `*`, `+` or open-ended count), computed by the same parsed-pattern walk as §5.2 | Keep the last L−1 bytes across the boundary; report only matches ending past it |
| Whole-input buffering | Always | Memory proportional to the input. This is what ripgrep does. |

### 7.3 Level C: true streaming in RE2 (not recommended; needs approval)

Carrying matcher state across chunks, as Hyperscan does, cannot be done from outside RE2: a search call owns the DFA state for its duration and returns only a result.

It is feasible as a patch. The lazy DFA's states are invalidated when its cache is flushed, but `DFA::StateSaver` already snapshots a state as its instruction list and rebuilds it afterwards, so a resumable handle is a `StateSaver`-shaped object returned to the caller. The estimate is a few hundred lines across `dfa.cc`, `prog.h` and `re2.h`; that figure is a reading of the code, not a prototype.

Its limits are the same ones Hyperscan has:

- **Match ends only.** RE2 finds a match's start by running a reverse DFA backwards from the end, over text a stream has already discarded.
- **No captures**, for the same reason.
- **Different reporting semantics.** A resumable forward DFA naturally reports every position where a match ends, not RE2's leftmost-first match.

**GRP11. Level C is not built until a consumer needs multi-line matching over an unbounded stream.** If that day comes, CLAUDE.md rule 16 applies: stop, explain the root cause, get approval, and record the delta under `patches/`.

---

## 8. Out of scope: backreferences and lookaround

**GRP12. The library does not match backreferences or lookaround and does not emulate them.** RE2 rejects these at compile time. The matcher's compile call returns a distinct "unsupported construct" status, separate from a syntax error, and the caller decides:

- a JS-originated pattern keeps its current route through `js_regex_scanner_needs_backtrack()` to `lambda/js/js_bt_regex`, searching without this library;
- a Lambda string pattern cannot contain these constructs (S11.1.2v3), so the status is unreachable from Lambda source and would indicate a lowering bug.

The library also stays out of replacement. A sed-like `replace` over files (the second half of `vibe/Lambda_Type_String_Grep.md`) can use the searcher to find matching lines and then apply `pattern_replace_all_options`, keeping S17.6.1's stepping in one place.

---

## 9. Directory search

**GRP13. Traversal and search share one `ThreadPool`.** A directory job lists its children with `dir_list`, submits a job per subdirectory and a job per file, and returns. This gives ripgrep's parallel walk without a dedicated work-stealing queue. Output order across files is nondeterministic unless the consumer asks for sorted results, in which case results are buffered per file and emitted in path order.

**GRP14 (USER, 2026-10-04). Directory search honours `.gitignore` by default, and also skips well-known dependency, cache and temp directories.** `grep_ignore` applies three layers, each of which an option can turn off:

| Layer | Default | Rule |
|---|---|---|
| Ignore files | on | `.gitignore` and `.ignore` files are read during the descent and kept as a stack of rule sets, with git's last-match-wins rule and `!` negation. |
| Hidden entries | on | Names starting with `.` are skipped, which covers `.git`, `.venv`, `.cache`, `.mypy_cache`, `.pytest_cache`, `.gradle`, `.next`, `.idea` and the like without naming them. |
| Built-in directory list | on | A fixed list of non-hidden directory names that are dependencies or caches in every ecosystem that uses them: `node_modules`, `bower_components`, `__pycache__`, `venv`, `site-packages`. |

Two rules keep the defaults from hiding real results:

- **A path the caller names explicitly is always searched.** `find(/proj.node_modules.foo, pat)` searches that directory even though a walk from `/proj` would skip it. The layers filter what the walk discovers, never its roots. This is ripgrep's rule as well.
- **Ambiguous names are never skipped by name (USER, 2026-10-04).** `build`, `dist`, `out`, `target`, `bin`, `tmp` and `temp` are output directories in some projects and source directories in others. They are skipped only when an ignore file says so, which is the usual case in a repository.

A walk outside any git repository still applies ignore files it finds, as ripgrep does with `.ignore` and, optionally, `.gitignore`.

The glob matcher needs `**` and directory-only patterns, which `fnmatch` (used by `file_find` today) does not provide portably; a small wildmatch function is added once and `file_find` can move to it.

**GRP19 (USER, 2026-10-04). The walk does not follow symbolic links in v1.** A symlink found during the descent is skipped, whether it points to a file or a directory, which also rules out link cycles. A symlink the caller names explicitly as a root is resolved and searched, by the same rule that exempts explicit paths from the ignore layers.

Reading strategy in v1 is the chunked stream of §7.1 for every file. Memory-mapped reads are a later optimization: `lib/` has no file-mapping helper today, and ripgrep itself only maps when searching a few large files.

---

## 9A. Feature coverage against grep and ripgrep

The design is not a full grep. This table states what v1 covers and what it leaves out, so the omissions are decisions and not accidents. "Consumer" means the library gives the consumer what it needs and the feature is a few lines there.

| Feature (grep / ripgrep flag) | v1 | Notes |
|---|---|---|
| Regex search, RE2 syntax | yes | |
| Fixed strings (`-F`), ignore case (`-i`), whole word (`-w`) | yes | `GrepOptions` |
| Several patterns (`-e`, `-f`) | yes | Joined into one alternation (GRP5); reading a pattern file is the consumer's |
| Only the matching part (`-o`) | yes | This is the default result (GRP4) |
| Line number (`-n`), byte offset (`-b`), file name (`-H`) | yes | Plus the code-point offset, which grep does not have |
| Recursive search (`-r`), ignore files, hidden files (`--hidden`, `--no-ignore`) | yes | GRP14 |
| Binary files skipped or searched as text (`-a`) | yes | §7.1 |
| Stop after the first match (`-q`) | yes | The sink returns stop, or a total limit of 1 |
| Total match limit, per-file limit (`-m`) | yes | GRP25 |
| Sorted output (`--sort path`) | yes | §9 |
| Counts of matching lines (`-c`) | yes | GRP30: lines are counted, not enumerated; with `-v`, the other lines |
| Color, JSON, column (`--column`), smart case (`-S`) | consumer | All derivable from the match record |
| The text of the matching line | yes | Opt-in (GRP20) |
| Each line's terminator, `\n` or `\r\n` | yes | GRP31; grep and ripgrep have no such field (ripgrep's `--crlf` only changes what `$` matches) |
| Files with matches (`-l`), files without (`-L`) | yes | Three-way sink result and a per-file end callback (GRP21) |
| File-name filters (`--include`, `--exclude`, `-g`), depth limit (`--max-depth`), size limit | yes | GRP22. Named file types (`-t rust`) are a consumer-side table of globs. |
| Whole-line match (`-x`) | yes | GRP23 |
| Inverted match (`-v`) | after GRP20 | GRP24, phase G5A |
| Context lines (`-A`, `-B`, `-C`) | after GRP20 | GRP24, phase G5A |
| Multi-line matches (`-U`) | deferred | §7.2 |
| Backreferences, lookaround (`-P`) | out of scope | GRP12 |
| Other encodings (`-E`), UTF-16 | out of scope for v1 | GRP17 |
| Follow symlinks (`-L` in ripgrep) | out of scope for v1 | GRP19 |
| Replacement (`--replace`) | out of scope | §8; belongs to the `replace` consumer, and `io.grep` does not replace (GRP32) |
| NUL-separated records (`-z`), compressed files (`-z` in ripgrep), POSIX basic/extended syntax (`-G`, `-E`) | no | No consumer needs them |

**GRP20 (USER, 2026-10-04). Optional line text.** A `line_text` option fills two more fields of the match record with the enclosing line's span, without its terminator (GRP18). Tier 1 already has the bounds; tier 0 and tier 2 compute them only when asked. Several matches in one line report the same line span.

**GRP21 (USER, 2026-10-04). A three-way sink result and a per-file end callback.** The match callback returns continue, skip the rest of this file, or stop everything. An optional callback fires when a file is finished, with its path and match count, which is what "files without a match" needs. Together they give "which files contain a match" without reading files to the end, and let a consumer apply its own stopping rule. The two common rules, a per-file and a total match limit, are built in (GRP25).

**GRP25 (USER, 2026-10-04). The library supports both a per-file and a total match limit.** Both are options on the search, not something each consumer rebuilds from the sink result, and they can be set together:

- **Per-file limit.** A searcher stops reading an input once it has reported that many matches from it. This is local to one worker and costs nothing.
- **Total limit.** One counter is shared by all workers of a directory search. A worker claims a slot from it before reporting each match; when the counter is exhausted the walk stops submitting jobs and running searches end at their next match.
- **Which matches a total limit keeps.** In an unsorted parallel search the first *n* matches to arrive depend on thread timing. When the caller asks for sorted results, the limit applies in path order, so the result is deterministic: files are still searched in parallel, each capped at *n* matches, and the merge takes the first *n* in order. `io.grep` uses the sorted form (§9B.3).
- With inverted match (GRP24), the limits count reported lines; when counting (GRP30), counted lines.

**GRP22 (USER, 2026-10-04). Caller filters on the walk.** Include globs, exclude globs, a maximum depth and a maximum file size, applied after the ignore layers of GRP14 and using the same glob matcher. A file must pass the ignore layers, match an include glob if any are given, and match no exclude glob. As with the ignore layers, a path named explicitly as a root is exempt.

**GRP23 (USER, 2026-10-04). Whole-line option.** The matcher wraps the pattern as `^(?:…)$`, inside the multi-line wrapping of GRP5.

**GRP24 (USER, 2026-10-04). Inverted match and context lines come after GRP20.** Both report lines, so both build on the line span:

- **Inverted match** reports each line that contains no match. The record has no match, so its match fields are empty and its line fields are filled; `line_text` is implied. All three tiers still apply: the searcher finds matching lines as usual and reports the lines between them.
- **Context lines** report up to N lines before and after each matching line, through a separate callback so a consumer can tell context from matches. Overlapping contexts are reported once. In a stream, the framer keeps the last N lines unconsumed (§7.1).

**GRP30 (USER, 2026-10-04). A count mode, as `grep -c`.** The user asked whether the library could count lines; it could only hand over match records, so a consumer had to de-duplicate records it did not need. Ruled: a `count` option with `rg -c`'s meaning — one `{file, count}` per file that has lines with a match, `count` being how many, or with inverted match how many lines have none. As built (implementation record, decisions 14–18): the library's `count_lines` option skips the match and context callbacks and hands the count to the per-file end callback (GRP21). A selected line is confirmed and counted, never enumerated, so no offsets or line numbers are computed; under inverted match the lines between two matching lines are counted by their newlines without being visited; a pattern that matches every line, such as an empty one, gives the line count (`grep -c ''`) from newlines alone. The limits (GRP25) count lines in this mode: the per-file limit caps each count and a total limit caps their sum, cutting the count of the file it ends in. In `io.grep` a count record names its file even for a single-file source, and `count` with `files` is an error.

**GRP31 (USER, 2026-10-04). Records report their line's terminator.** GRP18 makes the `\r` of a `\r\n` part of the terminator, so no pattern can see it, and a consumer could not tell a CRLF line from an LF one; ripgrep can (`rg -c '\r$'`) only because its default terminator is a bare `\n`. Each record now carries how its line ended — `\n`, `\r\n`, or nothing for an input's last line when it is unterminated — and the consumer decides what to do with it. The library fills it on every record, including inverted and context lines; it costs one subtraction. In `io.grep`, `line_ending: true` adds a `line_ending` field holding `"\n"`, `"\r\n"` or `null`. The name follows `line`, `byte_offset` and `text`, each named after the field it adds. With `count: true` it has no effect, since a count has no line to describe.

---

---

## 9B. The Lambda surface: `io.grep()`

**GRP26 (USER, 2026-10-04). Lambda gets a system function `io.grep()` that exposes the library directly, and it is procedural (`pn`).** The function's ruling, with its parameters and options, is recorded with the rest of the `io` module in `vibe/Lambda_IO_Shell.md` (`io.grep`). It only reads, but its result depends on the state of the file system, so it is not deterministic, and S12.1.1v2 requires `fn` to be pure and deterministic. It is therefore callable from `pn` only, like every other member of `io`. It is one more row in the system-function registry (S17.2.1), so `io.grep(…)` and the qualified `lambda.io.grep(…)` both reach it. The signature, result shape and option names below were ruled the same day.

### 9B.1 Signature and results

```lambda
io.grep(source, pattern)             // [{value, index}, ...]^E
io.grep(source, pattern, options)
```

| Argument | Accepts | Meaning |
|---|---|---|
| `source` | a path to a file, a path to a directory, a wildcard path, or an array of paths | What to search. A directory is walked with the defaults of GRP14. |
| `pattern` | a string, a string pattern, or an array of either | A string is literal text and a pattern is a pattern, exactly as in-memory `find` reads them. An array matches any of its members. |
| `options` | a map | See §9B.2. |

The result is an array of match maps, in path order and, within a file, in position order.

| Field | Present | Value |
|---|---|---|
| `value` | always | The matched text |
| `index` | always | Code-point offset of the match from the start of its file, 0-based (GRP4) |
| `file` | when the source is a directory, a wildcard or an array | The path of the file |
| `line` | with `{line: true}` | 1-based line number |
| `byte_offset` | with `{byte_offset: true}` | 0-based byte offset of the match |
| `text` | with `{text: true}` | The whole line containing the match, without its terminator (GRP20) |
| `line_ending` | with `{line_ending: true}` | `"\n"`, `"\r\n"`, or `null` for a last line with no terminator (GRP31) |
| `before`, `after` | with `{context: n}` or `{before: n, after: m}` | Arrays of the neighbouring lines' text (GRP24) |

With `{count: true}` the result is instead one `{file, count}` per file with a selected line, in path order (GRP30); with `{files: true}`, the paths of the files with a match (GRP21).

```lambda
string todo = "TODO" ":" \s* \w+

io.grep(/src.'main.ls', "Lambda")^
// [{value: "Lambda", index: 0}, {value: "Lambda", index: 312}]

io.grep(/src, todo, {line: true, text: true})^
// [{file: /src.'main.ls', value: "TODO: refactor", index: 45, line: 3, text: "  // TODO: refactor this"}, ...]

io.grep(/src, "fixme", {ignore_case: true, include: ["*.ls", "*.md"], limit: 20})^
```

### 9B.2 Options

Each option maps onto a library option already ruled. The names are ruled (USER, 2026-10-04), `limit`, `limit_per_file`, `text` and `files` explicitly.

| Option | Type | Default | Library |
|---|---|---|---|
| `ignore_case` | bool | false | S17.7.1 folding |
| `word` | bool | false | whole-word match |
| `whole_line` | bool | false | GRP23 |
| `invert` | bool | false | GRP24. Results are lines: `value` is the line's text. |
| `line`, `byte_offset`, `text` | bool | false | GRP4, GRP20 |
| `line_ending` | bool | false | GRP31 |
| `context`, `before`, `after` | int | 0 | GRP24 |
| `limit` | int | none | total limit, GRP25 |
| `limit_per_file` | int | none | per-file limit, GRP25 |
| `files` | bool | false | Return the paths of files with a match instead of match maps (GRP21) |
| `count` | bool | false | Return one `{file, count}` per file instead of match maps (GRP30); not with `files` |
| `include`, `exclude` | string or array of strings | none | globs, GRP22 |
| `max_depth` | int | none | GRP22 |
| `max_size` | int | none | bytes, GRP22 |
| `hidden` | bool | false | search hidden entries (GRP14) |
| `ignore` | bool | true | honour ignore files and the built-in directory list (GRP14) |
| `binary` | bool | false | search binary files as text instead of skipping them |

**GRP28 (USER, 2026-10-04; ratified as S17.8.1). An unknown option name is a compile-time error where the compiler can see it, and a run-time warning otherwise.** In a map literal written at the call, an unknown name is an error. In an options map that arrives as a value, it is ignored and a warning is issued. The user ruled this as the convention for every system function that takes an options map, and existing implementations are to be migrated to it; Appendix C records where they stand. The user's first wording the same day (ignored with a warning in all cases) is in Appendix S.

### 9B.3 Semantics the function has to state

**It is line-oriented by default (USER, 2026-10-04), and that differs from in-memory `find`.** Other modes, such as multi-line matching (§7.2), can be added later as options if a need appears. A match never spans a line terminator (GRP1). A Lambda pattern that can match a newline, such as one using `...` (which matches newlines, S16.8.6v3) or a literal `"\n"`, matches only within a line here: `...` stops at the end of the line and a literal newline never matches. In-memory `find(str, pattern)` has no such limit. This is what grep means, and it is what makes the search fast, but it must be written into the function's documentation and its eventual ruling.

**A string pattern is lowered to regex text by the existing path.** `io.grep` reuses the lowering that in-memory `find` uses (`compile_pattern_to_regex`, `lambda/runtime/re2_wrapper.hpp`) and hands the text to the library, which is what GRP3's text-only API expects. A string argument sets the library's fixed-string option. No regex syntax appears at the Lambda surface, and no Lambda pattern can produce a backreference or lookaround (S11.1.2v3), so the "unsupported" status of GRP12 is unreachable.

**Results are in a stable order.** The function asks the library for sorted results, so an unchanged tree gives the same array on every run and thread count, with or without a limit (GRP25). As a `pn` it is not required to be deterministic, but a result order that depends on thread timing would make scripts and tests flaky for no benefit.

**GRP32 (USER, 2026-10-05). It does not replace.** Like the `grep` command, which has no replace option, and ripgrep, whose `--replace` rewrites only its printed output ("Neither this flag nor any other ripgrep flag will modify your files"), `io.grep` only searches and never changes a file. Changing text stays with the in-memory `replace()` (S17.6.1): read the file with `input()`, apply `replace()`, write it back with `output()`.

**Errors.** The function returns an error value in the `T^E` style of `input()`: a source that does not exist or cannot be read, a line over the size cap, a wrongly typed option value. An unreadable file met *during* a directory walk is skipped and logged, as grep does, and is not an error of the call.

### 9B.4 Relation to file-based `find()`

**Current state.** `find` is a pure function today: `SYSFUNC_FIND` and `SYSFUNC_FIND3` are registered as non-procedural (`lambda/runtime/sys_func_registry.c:718`, `721`) and take a string or symbol. The file-based form proposed in `vibe/Lambda_Type_String_Grep.md` is not implemented.

**GRP27 (USER, 2026-10-04). File-based `find(path, pattern, options?)` stays an `fn` and calls `lib/grep` underneath.** An `fn` must be deterministic (S12.1.1v2), and a file search taken alone is not. What makes it deterministic is caching for the script evaluation session, and **that caching is out of scope here**: it will be worked out in a separate proposal covering `find(path)`, `input()`, `exists()` and `path#` together. This doc designs no cache.

So the two functions divide as follows:

| | `find(path, pattern, options?)` | `io.grep(source, pattern, options?)` |
|---|---|---|
| Kind | `fn` | `pn` |
| Engine | `lib/grep` | `lib/grep` |
| Determinism | From the session cache of the separate proposal | Not required |
| Options | The small set ruled earlier: `ignore_case`, `limit`, `line`, `byte_offset` | The full set of §9B.2 |
| Result | `{value, index}`, plus `file` for a directory, plus the opt-in fields | The same shape, plus `text`, context and the rest |

What this library owes `find`: the same search `io.grep` gets, with sorted results so that the value to be cached does not depend on thread timing. Nothing in `lib/grep` knows about sessions or caches; the cache sits above it, in whatever the separate proposal defines.

**One consequence to carry into that proposal.** Because `find(path, …)` runs on `lib/grep`, it is line-oriented like `io.grep` (§9B.3): a pattern cannot match across a line boundary in a file, although the same pattern can in a string passed to in-memory `find`. The surface doc for `find` has to state this.

---

---

## 10. Phases

Each phase ends with its tests green and, from G2 on, a release-build measurement (CLAUDE.md rule 10) against `rg` and `grep` on a fixed corpus, with scratch output under `./temp/`.

| Phase | Content | Gate |
|---|---|---|
| **G-pre** | Vendor RE2 in-tree at the pinned tag (GRP15): tracked source, `VENDOR.md`, `make verify-re2-patches`, setup scripts and build config updated | `libre2.a` builds from the tracked tree on all three platforms; `make test-lambda-baseline` unchanged; verify target passes with an empty patch list |
| **G0** | Prepared-needle, rare-byte kernel in `lib/str.c`; unit tests | `str_find` results unchanged; kernel no slower than today on a first-byte-rare needle |
| **G1** | `GrepMatcher` (GRP5, whole-line option GRP23), tier-2 buffer searcher, match sink with three-way result (GRP21), optional line numbers, offsets and line text (GRP20), per-file limit (GRP25) | Matches identical to `grep -o -b -n` on the corpus for a pattern list. Includes a test that `never_nl` plus `(?m:…)` gives per-line answers. |
| **G2** | Literal extraction (§5.2), tiers 0 and 1, soundness tests of §5.4 | Tier 0/1 results byte-identical to tier 2 on every corpus pattern, including the case-folding cases; the corpus run in `\n` and `\r\n` forms gives the same lines (§5.5) |
| **G3** | Streaming (§7.1): `LineFramer` integration, binary detection, long-line cap | Results independent of chunk size, tested from one byte per chunk upward, including a chunk boundary between `\r` and `\n` |
| **G4** | Walker, ignore rules, caller filters (GRP22), per-file end callback (GRP21), parallel search, per-thread matchers (GRP6), total limit across workers (GRP25) | File set identical to `rg --files` on a test tree, with and without globs and depth limits; a sorted search with a total limit returns the same matches on every run and thread count; thread scaling measured |
| **G5** | `io.grep()` (GRP26): procedural registry row, argument and option decoding, result maps, both execution tiers |
| **G5B** | File-based `find()` (GRP27) on the same engine | Depends on the separate caching proposal; not gated by this doc | `test/lambda/*.ls` fixtures with expected `.txt` for each option; `doc/Lambda_Sys_Func.md` entry; semantics-spec ruling drafted for the user |
| **G5A** | Inverted match and context lines (GRP24), with their `io.grep` options | Output identical to `grep -v` and `grep -A/-B/-C` on the corpus, in whole-buffer and streamed form |
| **G6** | SIMD kernels in `lib/str_simd.c` (GRP8): packed pair, two- and three-byte search, newline count, reverse byte search; then Teddy (built, GRP29: `lib/str_teddy.c`, NEON / SSSE3 by CPUID / portable) | Each SIMD form byte-identical to its portable form; release-build measurements for grep and for literal `find`/`replace`/`split` |
| **G7** | RE2 NEON + SSE2 prefix kernel patch (§6.1), the first entry under `patches/re2-*.patch` | `make verify-re2-patches` passes; regex `find`/`replace`/`split` benchmarks before and after; baseline tests unchanged |
| **G8** (optional) | Multi-line Level B (§7.2) | A consumer that needs it |

Tests live in `test/lib/test_grep_gtest.cpp`, registered in `build_lambda_config.json`.

No performance figure is claimed here. The goal is to be within a small factor of ripgrep on literal and literal-bearing patterns; tier 2 will be slower than ripgrep, because RE2's lazy DFA with a `memchr` prefix scan is slower than the Rust regex engine on patterns with no literal.

### 10.1 Measured: directory search against ripgrep (2026-10-04)

After G-pre to G7, on Apple M-series, release flags, warm cache, 10 threads, against ripgrep 14.1.1. Every query below gave the same match total as `rg`, and the repo queries also gave the **same set of matched files** (`rg -l` diffed against the library's sorted list), so the walk — `.gitignore`/`.ignore`, hidden entries, binary files, symlinks, include globs — agrees with ripgrep's as well as the matching does. Wall times include process start-up for both tools.

| Corpus | Queries | lib/grep | rg |
|---|---|---|---|
| Lambda repo, `*.md` (1,043 files) | 6: `TODO`, `Lambda` (18,543 matches), `S17\.[0-9]+\.[0-9]+`, `-i ripgrep`, `^#+ ` (32,539), `-F io.grep(` | 74–82 ms | 69–90 ms |
| Lambda repo, `*.c *.h *.cpp *.hpp` (1,606 files) | 7: `log_error`, `TODO\|FIXME`, `static inline`, `mem_alloc\(`, `struct [A-Za-z_]+ \{`, `-i kelvin`, `[A-Za-z_]+ = NULL;` | 77–83 ms | 72–76 ms |
| Lambda repo, every file (24,299) | `RootFrame`, `TODO` | 326–334 ms | 319–326 ms |
| test262 (349 MB, 54k files) | 6, literal to no-literal | 0.83–0.87 s | 0.85–0.93 s |

Directory search is bound by walking and opening files, so on this hardware the two tools are level. The one file-set difference is by design: whole-repo `TODO` finds two files fewer, both under `test/input/grep_tree/{bower_components,venv}/`, which the built-in directory list skips (GRP14) and ripgrep has no list for. Sorted delivery, which `io.grep` uses, adds 1–18 ms. `io.grep` returns the same counts over the repo.

Single-file throughput (265 MB) is where the engines differ: literal, folded-literal and literal-bearing patterns run level with or ahead of ripgrep, and a pattern with no usable literal is about 4× slower (RE2's DFA, as predicted above). Literal sets lost ground before Teddy — 8 literals 148 vs 78 ms (one scan per literal), 12 literals 468 vs 86 ms (over the then cap of 8, so RE2 searched). With Teddy and pure sets in tier 0 (GRP29), warm runs measure 3 literals 75–79 vs 72–77 ms, 8 literals 87–88 vs 77–80 ms (40,096 matches: the remaining gap is per-match work), 12 literals 95–98 vs 86–94 ms, 24 literals 92–104 vs 88–100 ms. Full numbers: `vibe/impl/Lambda_Impl_Lib_Grep.md` §4.

Counting (GRP30) against `rg -c` on the same 265 MB file, 6.06 million lines, every count equal: literal patterns level (`TODO` 55 vs 47 ms, `-i todo` 69–72 vs 69–70 ms), `the` 83–88 vs 73–75 ms (112–115 ms when the same matches are reported instead of counted), inverted `-v the` 99–100 vs 133–140 ms, and every line (`""`) 44 vs 177–178 ms, where `wc -l` takes 243 ms. Over the whole repo the per-file counts equal ripgrep's except for the two fixture files the built-in directory list skips.

---

## 11. Open questions for the user

None. Every decision GRP1 to GRP32 is ruled or an unchallenged part of the design those rulings build on.

Work this doc creates outside its own phases: migrating existing system functions to S17.8.1 (Appendix C), and the separate caching proposal that file-based `find` waits on (GRP27).

---

## Appendix A. API sketch

Illustrative only; names and fields are settled during G1.

```c
// lib/grep/grep.h
typedef struct GrepMatcher GrepMatcher;     // compiled pattern + literal plan, immutable
typedef struct GrepSearcher GrepSearcher;   // per-thread buffers and settings
typedef struct GrepStream GrepStream;       // push interface over one input

typedef enum GrepStatus {
    GREP_OK = 0,
    GREP_ERR_SYNTAX,        // pattern does not parse
    GREP_ERR_UNSUPPORTED,   // backreference or lookaround: caller falls back (GRP12)
    GREP_ERR_LINE_TOO_LONG,
    GREP_ERR_IO,
} GrepStatus;

typedef struct GrepOptions {
    bool ignore_case;       // Unicode simple case folding (S17.7.1)
    bool fixed_string;      // pattern is literal text
    bool word;              // match whole words
    bool line_numbers;      // fill GrepMatch.line_number (GRP4); off by default
    bool char_offsets;      // fill GrepMatch.char_offset (GRP4); off by default
    bool line_text;         // fill GrepMatch.line / line_length (GRP20); off by default
    bool whole_line;        // the pattern must match an entire line (GRP23)
    uint64_t max_matches_per_file;   // 0 = unlimited (GRP25)
    uint64_t max_matches_total;      // 0 = unlimited (GRP25)
    bool sorted;            // report files in path order; makes a total limit deterministic
    bool invert;            // report lines with no match (GRP24); implies line_text
    int before_context, after_context;   // GRP24
    size_t max_line_bytes;
} GrepOptions;

typedef struct GrepMatch {
    const char* path;       // NULL for buffer or stream input
    const char* text;       // the matched bytes; valid during the callback only
    size_t length;
    uint64_t byte_offset;   // of the match start, absolute within the input, 0-based; always filled
    uint64_t char_offset;   // the same position in code points, 0-based; valid when char_offsets is on
    uint64_t line_number;   // 1-based; 0 when line_numbers is off
    const char* line;       // the enclosing line without its terminator; NULL when line_text is off
    size_t line_length;
} GrepMatch;

typedef enum GrepAction {
    GREP_CONTINUE = 0,
    GREP_SKIP_FILE,         // stop searching the current input, go on to the next (GRP21)
    GREP_STOP,              // stop the whole search
} GrepAction;

typedef struct GrepSink {
    void* user_data;
    GrepAction (*matched)(void* user_data, const GrepMatch* match);
    GrepAction (*context)(void* user_data, const GrepMatch* line);          // optional; a context line (GRP24)
    GrepAction (*file_done)(void* user_data, const char* path, uint64_t match_count);   // optional (GRP21)
} GrepSink;

GrepStatus grep_matcher_create(const char* pattern, size_t pattern_len,
                               const GrepOptions* options, GrepMatcher** out,
                               char* error_buf, size_t error_buf_len);
void grep_matcher_destroy(GrepMatcher* matcher);

GrepSearcher* grep_searcher_create(const GrepMatcher* matcher);
void grep_searcher_destroy(GrepSearcher* searcher);
GrepStatus grep_search_buffer(GrepSearcher* searcher, const char* data, size_t length,
                              const GrepSink* sink);
GrepStatus grep_search_file(GrepSearcher* searcher, const char* path, const GrepSink* sink);

GrepStream* grep_stream_open(GrepSearcher* searcher, const GrepSink* sink);
GrepStatus grep_stream_feed(GrepStream* stream, const char* data, size_t length);
GrepStatus grep_stream_finish(GrepStream* stream);   // searches the final unterminated line, then frees
```

## Appendix B. RE2 facts this design relies on

Each should be pinned by a test in G1, since they are read from the source and not yet exercised.

| Fact | Source |
|---|---|
| `never_nl` removes `\n` from literals, classes and `.` at parse time | `re2/parse.cc:404`, `413`, `456`, `1926` |
| With no submatch requested and an unanchored search, the DFA stops at the earliest match | `re2/dfa.cc:1859–1866` |
| Prefix acceleration covers only a leading literal, seen through captures | `re2/regexp.cc:729–752` |
| The front-and-back prefix kernel is AVX2-only; otherwise `memchr` on the first byte | `re2/prog.cc:1130–1170` |
| The parsed pattern is reachable through a public accessor | `re2/re2.h:331` |
| Under case folding, a letter with more than two fold variants parses to a character class, not a literal | `Regexp::ParseState::PushLiteral`, `re2/parse.cc` |
| Multi-line `$` matches before `\n` only, and `.` matches `\r` | to be confirmed by test (§5.5) |
| Memory budget is split two thirds forward program, one third reverse | `re2/re2.cc:253`, `279` |
| A DFA state can be saved and restored across a cache flush | `DFA::StateSaver`, `re2/dfa.cc` |

## Appendix C. Unknown option names: current state

Checked on 2026-10-04, as the baseline for migrating to S17.8.1 (GRP28).

| Where | Behavior today | Against S17.8.1 |
|---|---|---|
| `start(target, args, options)` (S13.1.1v2; `lambda/runtime/build_ast.cpp:2913`) | Options must be a map literal, and an unknown name is a compile-time error | Conforms: it is the compile-time arm |
| `find`, `replace` options (`parse_find_replace_options`, `lambda/runtime/lambda-eval.cpp`) | Looks up `limit`, `last` and `ignore_case`; any other name is ignored silently | Missing both arms: no error for a literal, no warning for a value |
| `input`, `output`, `fetch`, `cmd` and other functions with an options map | Not audited | Unknown |
| `io.grep` (this doc) | Literal: compile-time error through the generic registry table `sys_func_option_names`; value: `log_warn` and ignored | Conforms (2026-10-04) |

## Appendix S. Superseded rulings

- ~~**GRP9 (USER, 2026-10-04). No patch in v1.**~~ Replaced the same day by GRP9v2: the reasoning that grep needs no RE2 patch stands, but the prefix kernel (NEON, then SSE2 as well), first listed as "one optional patch, not recommended now", was brought into scope by the user to speed up in-memory `find`/`replace`/`split`.
- ~~**Unknown option names (USER, 2026-10-04): ignored, with a warning.**~~ Refined the same day into GRP28 / S17.8.1: an error at compile time where the name is visible in a literal, a warning at run time otherwise.
