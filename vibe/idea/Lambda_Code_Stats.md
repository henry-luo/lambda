# Code statistics for Lambda: an scc-style line counter (idea)

- **Status:** Idea, 2026-10-04; §5.7 and CNT13 added 2026-10-05. Nothing here is ruled; the decisions it needs are CNT1–CNT13 (§9). Phase L0's walker interface has since been built for `io.search`: `grep_walk_paths` (`vibe/Lambda_IO_Fulltext_Search.md`, FTX10).
- **Origin:** after `count: true` landed in `io.grep` (GRP30), the user asked whether the grep library could count lines while ignoring blank lines and C comment lines, then asked what tokei and scc offer, and then whether grep could do what `wc` does (§2.1, §5.6).
- **Authorities:** `doc/Lambda_Formal_Semantics.md` (S#) and `doc/Lambda_Formal_Design.md` (D#). It builds on the grep library (`vibe/Lambda_Lib_Grep.md`, GRP#) and the `io` module (`vibe/Lambda_IO_Shell.md`).
- **Prior art read for it (2026-10-04):** scc's README, `processor/workers.go`, `processor/structs.go` and `languages.json` (github.com/boyter/scc), its author's design write-up (boyter.org/posts/sloc-cloc-code), and tokei's README (github.com/XAMPPRocky/tokei).

## 1. Summary

Lambda can already count lines: `io.grep(source, pattern, {count: true})` returns how many lines of each file match (GRP30), an empty pattern counts every line, and line-local patterns leave out blank lines and `//` comment lines. What it cannot do is tell code from comments. Whether a line sits inside a `/* … */` block depends on the lines before it, and a `/*` inside a string opens nothing; both need a small lexer that carries state from line to line. This doc proposes that tool, modelled on scc and tokei: a comment-aware line counter on lib/grep's walker and streaming, exposed as `io.loc(source, options?)` (a working name, CNT1), returning `{file, language, lines, code, comment, blank}` for each file. Since it reads every byte once, it can also give `wc`'s counts — words, characters, bytes and the longest line — for any text file, in the same pass (§5.6). The same classifier could later limit `io.grep` to code, comments or strings (§5.5).

## 2. What `io.grep` covers today, and the gap

```c
 1  // header comment
 2  #include <stdio.h>
 3
 4  /* block
 5   * comment
 6   */
 7  int main(void) {   // trailing comment
 8      /* inline */ return 0;
 9
10      char* s = "/* not a comment";
11  }
```

Line 9 holds spaces and a tab. With today's options:

```lambda
io.grep(src, \(!s), {count: true})^                                                     // non-blank lines: 9
io.grep(src, [\(s*), \(s* "//" ...)], {whole_line: true, invert: true, count: true})^   // neither blank nor //: 8
```

The real code count is 5 (lines 2, 7, 8, 10 and 11): lines 4–6 are inside a block comment. No per-line pattern can know that, and guesses such as "a line starting with `*`" also catch code like `*p = 0;`.

Multi-line matching (the grep design's deferred Level B, §7.2) does not close the gap. It would let a regex match a whole `/* … */`, but the scan would still have to skip strings and `//` comments first, or line 10 opens a comment that is not there, and the matched spans would still have to become per-line verdicts — line 8 holds a comment and code, so it is code. That is a lexer written as a regex. A counter does it in one pass, exactly, at the cost of reading the bytes once.

### 2.1 `wc` today

`wc` counts lines (`-l`), words (`-w`), characters (`-m`) and bytes (`-c`); GNU `wc` adds the longest line (`-L`). Each has a Lambda spelling today, but only lines are a good fit for grep. Checked on 2026-10-04 against `wc` on five files — the grep fixtures `notes.txt`, `unicode.txt`, `crlf.txt` and `eol.txt` (no final newline) and the C sample of §2 — and on a 265 MB file of source code:

| `wc` | Lambda today | Same as `wc` | Cost |
|---|---|---|---|
| `-l` lines | `io.grep(f, "", {count: true})` | not on a file whose last line has no newline: it counts that line, as `grep -c ''` does, where `wc -l` counts newline characters (`eol.txt`: 3 against 2) | fast: 44 ms against `wc -l`'s 243 ms on the 265 MB file |
| `-w` words | `len(io.grep(f, \(!s+)))` | yes on all five files; but whitespace is ASCII only (RE2's `\s`), so on the 265 MB file it finds 28,250,740 words, as `wc -w` does in the C locale, and 126 fewer than `wc -w` in a UTF-8 locale | slow: one result map per word, and even the library alone, counting matches without building results, takes 2.9 s against `wc -w`'s 0.36 s — one RE2 call per word |
| `-m` characters | `len(input(f, 'text'))`, not grep | yes, code points | reads the whole file into memory |
| `-c` bytes | `p.size`, a path property, not grep | yes | one stat |
| `-L` longest line | none | — | — |

So grep is the right tool for counting lines and a poor one for words, and characters and bytes are not grep's business. `io.grep` walks a directory with the ignore rules in one call; characters and bytes need a loop over files.

## 3. Prior art

### 3.1 scc

scc ("Sloc Cloc and Code", Go, by Ben Boyter) is the closest model: a small, exact counting core with many optional extras around it.

**Counting core.** One byte-by-byte pass per file through a state machine, with no regex (`countLoopGeneric`). When a line ends, its state decides what it counts as:

| State at the end of a line | Counted as |
|---|---|
| `SCode`, `SString`, `SCommentCode` (code, then `//`), `SMulticommentCode` (code before `/*` or after `*/`) | code |
| `SComment`, `SMulticomment`, `SMulticommentBlank` (a line inside an open block comment) | comment |
| `SBlank` | blank |

So a blank line inside `/* … */` counts as comment, a line inside a multi-line string counts as code, and a line holding both code and a comment counts as code. Block-comment closers go on a stack (`endComments`), so comments nest only in languages marked nestable. Inside a string, a closing quote is escaped when an odd number of escape characters precede it; raw strings skip escape handling.

**Fast paths.** A 256-entry table (`TokenFirst`) marks which bytes can start any token of the language; every other byte is skipped without a lookup, and the rest are matched against a prefix tree (`Trie`) of the language's tokens. Runs of spaces, tabs and `\r` are skipped in one step, and inside a `//` comment the scan jumps to the end of the line.

**Language table.** `languages.json` defines each language as data; the README counts 322 languages. The C entry, abridged:

```json
"C": { "extensions": ["c", "ec", "pgc"], "line_comment": ["//"], "multi_line": [["/*", "*/"]],
       "quotes": [{"start": "\"", "end": "\""}], "linesplice": true,
       "complexitychecks": ["for ", "for(", "if ", "if(", "switch ", "while ", "else ", "|| ", "&& ", "!= ", "== "] }
```

The other fields: `nestedmultiline`; per-quote `ignoreEscape` (raw strings such as Go's backticks), `delimited` (C++ `R"x(…)x"`) and `docString` (Python docstrings count as comment); `linesplice` (a `//` comment ending in `\` continues onto the next line, as in C); `filenames` and `shebangs` for detection; `heuristics`, regexes that settle shared extensions such as `.h` or `.m`; `caseinsensitive` and `commentisword`.

**Extras.**

| Feature | How it is computed | Cost |
|---|---|---|
| Complexity | counts the language's complexity tokens (`if `, `for(`, `&& `, even `== `) met in code, not in strings or comments; comparable only within one language | "almost free"; `-c` turns it off |
| Cognitive complexity | each such token weighted by nesting depth, guessed from indentation | small |
| Bytes | the file's size | free |
| Duplicate files (`-d`) | BLAKE2b-256 of the whole file, in a shared map behind a lock | about 20% slower |
| Unique lines, DRYness | a set of every line's text; DRYness is unique lines over all lines | can double the run time |
| Minified | average line length of 255 bytes or more | free |
| Generated | a marker such as "do not edit" in the first 1,000 bytes | free |
| Large files | skipped above 1 MB or 40,000 lines with `--no-large` | free |

Beyond counting: COCOMO estimates (cost, schedule, team size), an experimental LOCOMO estimate (the cost of regenerating the code with an LLM), git-history reports (hotspots, files that change together, per-author ownership, timelines), about a dozen output formats (table, JSON, CSV, cloc-compatible YAML, HTML, SQL, OpenMetrics), an HTML report, badges, a config file and an MCP server mode.

**Pipeline and speed.** Walk, read, count and summarise run as stages joined by channels, with 8 workers per stage by default and a parallel directory walk. From the author's write-up: early versions spent about 95% of their time walking directories; memory-mapping files only won from about 6 MB, against a typical 18 KB source file; and switching off Go's garbage collector took the Linux kernel from 1.7 s to 0.74 s. The README's March 2026 benchmark (32 cores) puts scc ahead of tokei: Linux kernel 0.91 vs 1.42 s, CPython 81 vs 450 ms, Valkey 28 vs 74 ms.

**Library.** The Go package counts one in-memory file and calls back per line with blank, code or comment. With `ClassifyContent` it also tags every byte as blank, code, comment or string, and `FilterContentByType` blanks out every byte not of the chosen kinds, keeping newlines.

**Limits.** Complexity is a keyword count, not cyclomatic complexity. The C and C++ entries list only `"` as a quote — probably because of digit separators such as `1'000` in C++14 and C23 — so the char literal `'"'` opens a string that is not there and misreads the lines up to the next `"`. There is no handling of code embedded in another language beyond experimental per-language scanners, and constructs such as JavaScript regex literals can fool a byte scanner; the author accepts "99.999%" accuracy in exchange for speed.

### 3.2 tokei

tokei (Rust) has the same core — lines, code, comments and blanks per language, with block comments, nested comments and comment markers inside strings handled correctly — and claims "over 150" languages. It counts code embedded in other languages: code blocks in Markdown, and Markdown in Rust doc comments. It honours `.gitignore`, `.ignore` and `.tokeignore`, with `--hidden` and `--no-ignore*` switches; filters by language (`-t`) and glob (`-e`); prints a table, JSON, YAML or CBOR, with `--files` for per-file rows; merges earlier runs (`--input`); reads `tokei.toml`; and is a Rust library.

### 3.3 Others

cloc (Perl) strips comments with regular expressions. It is the tool the others measure themselves against, and its YAML output is what scc's `cloc-yaml` format imitates; in the scc author's early benchmark it took 1.49 s on Redis against scc's 37 ms. scc's benchmarks also include polyglot, loc and gocloc.

## 4. Feature inventory

| Feature | scc | Proposal | Why |
|---|---|---|---|
| Lines, code, comment, blank per file | yes | v1 | the point of the tool |
| Exact comment and string handling: nesting, raw strings, docstrings, line splices | yes | v1 | §5.2 |
| Language detection by extension, file name and shebang | yes | v1 | §5.3 |
| Heuristics for shared extensions (`.h`, `.m`) | yes | later | v1 maps each extension to one language |
| Ignore files, hidden entries, include and exclude globs, depth and size limits | yes | v1 | lib/grep's walker as it is (GRP14, GRP19, GRP22) |
| Per-language totals | yes | Lambda | a few lines over the per-file records (CNT2) |
| Bytes | yes | v1 | free |
| Words, characters, longest line (`wc -w`, `-m`, `-L`) | longest and mean line length only (`--character`) | v1, on request | one vectorized pass each over blocks already in cache (§5.6, CNT11) |
| Minified and generated flags | yes | v1 or later | free (CNT6) |
| Complexity | yes | later | cheap, but needs a keyword list per language (CNT6) |
| Cognitive complexity | yes | no | an indentation guess |
| Duplicate files | yes | later | a hash per file (CNT6) |
| Unique lines, DRYness | yes | no | doubles the cost for a niche figure |
| Code embedded in other languages | experimental | later | Markdown code blocks, HTML `<script>` and `<style>`; tokei does it |
| Per-byte classification | library | later | the basis for scoped search (§5.5) |
| COCOMO, LOCOMO | yes | no | a formula over the counts, a few lines of Lambda if wanted |
| Git-history reports | yes | no | another tool's job |
| Output formats, HTML report, badges | yes | no | the result is Lambda data; `format()` and `output()` render it |
| Config files | yes | no | an options map |
| MCP server mode | yes | no | out of scope |

## 5. Design sketch

### 5.1 Where it lives

The walker in `lib/grep/grep_walk.cpp` already does what a counter needs — ignore layers, caller filters, symlink rules, parallel jobs, sorted delivery — but its file job is hard-wired to a `GrepSearcher`. The smallest change is to give the walk a per-file job interface (open an input, feed it blocks, finish it) that the searcher and a counter both implement, and to put the counter beside the searcher in `lib/grep` (for example `grep_loc.cpp`), reusing the stream framing that already yields blocks of complete lines, and the binary sniff. A separate `lib/loc/` over an extracted walker is the alternative (CNT9).

A C API in the style of `grep.h` (illustrative):

```c
typedef struct LocCounts {
    uint64_t lines, code, comment, blank, bytes;
    bool minified, generated;
} LocCounts;

const LocLanguage* loc_language_for(const char* path, const char* head, size_t head_len);
LocState* loc_begin(const LocLanguage* lang);                  // one input
void loc_feed(LocState* st, const char* data, size_t len);     // blocks of complete lines (§5.2)
void loc_finish(LocState* st, LocCounts* out);                 // the unterminated last line, then the counts
GrepStatus loc_count_paths(const char* const* paths, size_t count, const GrepWalkOptions* walk,
                           const LocSink* sink);               // per-file counts, in path order when sorted
```

### 5.2 The engine

The states and the end-of-line rule follow scc (§3.1). For each language the table compiles into a first-byte table and a short list (or a trie) of tokens: line-comment starts, block-comment pairs, quote kinds. The hot loop jumps to the next byte that can matter — a token's first byte, a `\n`, an escape inside a string — and compares tokens only there.

- **Skipping.** In code, the bytes worth stopping at are few (`/`, `"`, `'`, `\` and `\n` for C), so finding the next one is a byte-class scan. `str_find_byteset` does this today as a scalar test of eight bytes at a time. A SIMD byte-class scan — Hyperscan calls the technique Shufti, and it is Teddy's nibble lookup (`lib/str_teddy.c`) with a one-byte fingerprint — would serve the counter and every other byte-set user in lib/str. Inside a `//` comment the scan jumps to the `\n` with `str_find_byte`; inside a block comment it looks for the first byte of the closer.
- **Strings.** An escape counts when an odd number of escape characters precede the quote; raw strings ignore escapes; delimited raw strings (C++ `R"x(…)x"`, Rust `r#"…"#`) close only on their own delimiter; a docstring kind counts as comment on lines that hold nothing else.
- **Nesting.** A depth counter for languages whose comments nest (Rust, Swift); a closer stack only where one language has several block-comment pairs.
- **Line ends.** By GRP18 the `\r` of a `\r\n` belongs to the terminator, so a line of spaces ending in `\r\n` is blank. With `linesplice`, a `//` comment ending in `\` carries the comment state onto the next line.
- **Streaming.** The state is a few bytes (state, nesting depth, open string kind), carried from block to block. Blocks end at `\n` and no token contains one, so a token never straddles a block boundary, and counts do not depend on chunking — the grep library's G3 gate again.
- **Inputs.** Binary files are skipped as in grep (a NUL in the first 8 KiB), and a UTF-8 byte-order mark is skipped before counting.

### 5.3 The language table

The schema can stay close to scc's, so entries can be checked against it or imported (CNT4; scc is MIT-licensed, so imported entries carry its notice). Whether the table is compiled in (a static C array, perhaps generated from a JSON file at build time) or loaded at run time from a data file under `lmd/` is open; compiled in is simpler and costs nothing at start-up.

A first version could cover C, C++, Objective-C, C#, Java, JavaScript, TypeScript, Go, Rust, Swift, Kotlin, Lambda (`//` and `/* */` comments, strings in `"`, symbols in `'`), Python, Ruby, shell, Perl, Lua, SQL, CSS, HTML and XML, YAML, TOML, Makefile and CMake (CNT4).

Where it can, the table should be more exact than scc's:

- **`'` in C and C++.** Treat `'` as opening a char literal unless it sits inside a number — a run that began with a digit — where it is a digit separator (`1'000`). Prefixed literals such as `u8'a'` and `L'a'` begin with a letter, so they stay char literals (CNT7).
- **Python string prefixes** (`r`, `b`, `f` and their combinations) and triple quotes, as raw and docstring kinds.
- **Lua long brackets** of any level (`[==[ … ]==]`), matched by level rather than listed level by level.
- **JavaScript regex literals** — a `/` that follows an operator or an opening bracket — if a fixture shows the need.

### 5.4 The Lambda surface

`io.loc(source, options?)` (CNT1) would sit beside `io.grep` and follow it: a procedure, since its result depends on the file system and `fn` must be deterministic (S12.1.1v2, as GRP26 ruled for `io.grep`); a row in the system-function registry (S17.2.1); unknown option names handled by S17.8.1; the same source forms (a path, a wildcard path, a string naming a path, or an array of these); and results in path order, as GRP25 makes them for `io.grep`.

```lambda
io.loc(/.src)^
// [{file: /.src.'main.c', language: "C", lines: 11, code: 5, comment: 4, blank: 2}, ...]
```

The options would be `io.grep`'s walk options unchanged (`include`, `exclude`, `max_depth`, `max_size`, `hidden`, `ignore`, `binary`), plus counter options: `language` to force one, `unknown` to count unrecognised files as plain text (lines and blanks only; scc's `--count-unsupported`), `wc` to add `wc`'s counts (§5.6, CNT11), and later `complexity` (CNT6). Files in no known language are left out by default (CNT5).

Two alternatives were considered and set aside. An option on `io.grep` (say `count: 'code'`) would bend a pattern search into something that takes no pattern and returns a different shape. Multi-line matching (§2) would still need the lexer and the line arithmetic.

### 5.5 Scoped search (later)

The classifier knows, for each byte, whether it is code, comment or string; scc's library exposes exactly that. With it, `io.grep` could take an option such as `{in: 'comment'}`: find `TODO` only in comments, a name only in code, a message only in string literals (CNT8). The cost is that the classifier must read every byte of a file, so a scoped search runs at the counter's speed, not at the literal scan's.

### 5.6 Plain counts, as `wc`

`wc`'s counts need no language table: they are the part of the counter that applies to any text file. The counter already holds each block of complete lines in cache, so each count is one more pass over it, and none needs the token matching:

| Count | Definition | Pass |
|---|---|---|
| `lines` | as the rest of the counter: an unterminated last line counts, as with `grep -c ''` and scc; `wc -l` counts newline characters instead (CNT12) | the counter's own |
| `bytes` | the file's size | free |
| `chars` | code points, counted by `str_utf8_count` — the function behind `io.grep`'s `index` and in-memory `find` (GRP4), so the three agree, including on invalid UTF-8 | a loop the compiler already vectorizes |
| `words` | maximal runs of non-whitespace, as `wc -w`; which characters are whitespace is CNT12 | a whitespace mask per 16 bytes, words counted at each space-to-non-space step, with a slow path only at the lead bytes of multi-byte spaces if whitespace is Unicode |
| `max_line` | the longest line, in code points, without its terminator; GNU `wc -L` measures display width instead (tabs to the next multiple of 8, wide characters as 2), which a counter has no business guessing | the counter stops at every `\n` anyway, so line lengths in bytes are free; a line can only beat the maximum if its byte length does, so code points are counted for those lines alone |

Nothing carries from block to block: blocks end at `\n`, so no word and no character is split between them. With `{wc: true}` the record gains `words`, `chars` and `max_line` next to the counter's own fields; `lines` and `bytes` are always there. A file in no known language, counted with `unknown: true`, gets the plain counts and no code or comment split. The alternative is a separate `io.wc(source, options?)` on the same engine, returning `{file, lines, words, chars, bytes, max_line}` for every text file with no language detection at all, for scripts that think in shell commands (CNT11).

```lambda
io.loc(/.src, {wc: true})^
// for the sample of §2:
// [{file: /.src.'main.c', language: "C", lines: 11, code: 5, comment: 4, blank: 2,
//   bytes: 170, words: 29, chars: 170, max_line: 38}, ...]
```

Done in C in one sweep over cached blocks, these counts should run at `wc`'s speed or better — against the 2.9 s that counting words through grep takes on the 265 MB file (§2.1), and `wc -w`'s 0.36 s.

### 5.7 What a word is: `wc`, `io.search`, and Unicode word boundaries

Three definitions of a word are in play, and they disagree:

- **`wc -w`**: a maximal run of non-whitespace (§5.6). Simple and checkable against `wc` itself, but punctuation sticks to words (`search,` and `(search)` are words, and so is a lone `—`), and text written without spaces is one word per run (`全文检索很快` is 1).
- **`io.search`'s token** (FTX3): a run of letters, digits and marks, with each Han, kana or Hangul character a token of its own. It is built for matching and splits on purpose — `don't` is `don` + `t`, `3.14` is `3` + `14`, `snake_case_name` is three tokens, a URL five — because the query is split the same way, so `"don't"` still matches. As a count it inflates prose with contractions and code with identifiers, and it should not be reused for counting: making it count well (keeping `don't` whole) would make search worse (`don` would stop finding `don't`).
- **Unicode word boundaries (UAX #29)**, what word processors use: `don't`, `3.14` and `e.g.` stay whole, punctuation-only segments are not words, `_` joins (`snake_case_name` is one word), a Hangul word is one word. Ideographs still split one per character, as in `io.search`; real counts for Chinese and Japanese need dictionary segmentation, which nothing here proposes.

| Text | `wc -w` | `io.search` tokens | UAX #29 words |
|---|---|---|---|
| `don't stop` | 2 | 3 | 2 |
| `pi is 3.14` | 3 | 4 | 3 |
| `e.g. this` | 2 | 3 | 2 |
| `snake_case_name` | 1 | 3 | 1 |
| `see — this` | 3 | 2 | 2 |
| `학교에서 공부` | 2 | 6 | 2 |
| `全文检索很快` | 1 | 6 | 6 |

The proposal (CNT13) is two counts: `words` exactly as `wc -w`, so a result can be checked against the tool, and a UAX #29 word count as a separate option for prose.

**What UAX #29 would cost** (an estimate from the specification and from comparable implementations, 2026-10-05; nothing written):

| Part | Lines | Notes |
|---|---|---|
| Boundary algorithm | 250–350 | the ~20 word rules WB1–WB999: no break in `\r\n`; marks, format characters and ZWJ ignored inside a word (WB4); `don't`, `3.14`, `e.g.` held by one code point of look-ahead (WB6/7, WB11/12); emoji ZWJ sequences (WB3c); regional-indicator pairs (WB15/16) |
| Word_Break property table | 1,000–1,500, generated | ranges from Unicode's `WordBreakProperty.txt` (~19 values, ALetter most of them) plus Extended_Pictographic from `emoji-data.txt`, one line per range |
| Table generator | 80–120 | Python, so a Unicode upgrade is a rerun |
| Conformance test | 80–100 + data | Unicode's `WordBreakTest.txt` (~1,800 cases) |
| Word count on top | 30–50 | segments holding a letter or digit, ignoring space- and punctuation-only segments |

About 400–550 hand-written lines and a generated table. For comparison, Rust's `unicode-segmentation` implements the word rules in about 700–800 lines, with its generated tables in a separate, larger file; a C version needing only word boundaries and a forward iterator is smaller.

The table cannot be avoided: nothing in the tree has the Word_Break property. `utf8proc` has general categories and grapheme-cluster breaks only, RE2 has none, and there is no ICU. Deriving it from general categories gets letters and digits roughly right but misclassifies the characters the rules turn on (apostrophes, middle dots, Hebrew letters, Katakana, emoji), and the conformance test would fail. So the table comes from Unicode's data files (`WordBreakProperty.txt`, `emoji-data.txt`, `WordBreakTest.txt`, a few hundred KB under the Unicode licence), which have to be downloaded — the user's call, as for `libstemmer_c` (FTX5). The code would sit in `lib/` beside `lib/fts`'s tokeniser, with an ASCII fast path (letters, digits, `'`, `.` and spaces decided by a small table, as `str_ascii_alnum_span` does for `io.search`), so only non-ASCII text pays for the table, and it would serve the counter here and any later word segmentation in Lambda.

## 6. Performance expectations

A directory count should be bound by walking and opening files, as directory grep is: lib/grep's walker is level with ripgrep's on this repository (about 300 ms for 24,000 files, `vibe/Lambda_Lib_Grep.md` §10.1), and counting every line of the repository with lib/grep's count mode already takes 335 ms against ripgrep's 463 ms. Per byte, the engine does what scc does — a table lookup per byte, with token comparisons only at candidates — and in C, with a SIMD byte-class skip, it should match or beat scc's single-core rate. A gate in the grep library's style: counts equal to scc's and tokei's on a fixture corpus, except documented improvements (§5.3), and wall time within 1.5× of scc's on this repository and on a large tree such as the Linux kernel (CNT10, not ruled).

## 7. Verification

- Fixtures per language with hand-checked counts, including the traps: comment markers inside strings, strings inside comments, nested comments, raw and delimited strings, line splices, `'` digit separators, CRLF files, a missing final newline, a byte-order mark.
- Differential runs against scc and tokei over this repository and a large public tree, with every difference explained or fixed (installing the two tools is the user's call).
- Chunk-size independence, as in the grep library's G3 gate, and equal counts for the LF and CRLF forms of every fixture.
- `wc` counts against `wc` itself in the C and UTF-8 locales, with each documented difference (an unterminated last line, display width against code points, the whitespace set of CNT12) shown by a fixture; `chars` against `str_utf8_count` on invalid UTF-8.
- A slow, obviously correct reference lexer, fuzzed against the fast engine.

## 8. Phases

| Phase | Content | Gate |
|---|---|---|
| L0 | per-file job interface in the walker; grep moved onto it — done for `io.search` as `grep_walk_paths` (FTX10) | grep tests unchanged |
| L1 | engine and C-family table; C API | fixtures; chunk-size and CRLF independence; fuzzing |
| L2 | `io.loc` | script tests on all three tiers; differential run against scc and tokei |
| L3 | the rest of the v1 table; minified and generated flags; `wc` counts | fixtures per language; counts against `wc` |
| L4 | SIMD byte-class scan in lib/str | byte-identical to the scalar form; throughput |
| L5 | per-byte classification; `io.grep`'s `in:` option | scoped-search tests |
| L6 | complexity; embedded code (Markdown code blocks, HTML `<script>` and `<style>`) | fixtures |

## 9. Open decisions

| ID | Decision | Proposal |
|---|---|---|
| CNT1 | Name and home | `io.loc(source, options?)`, separate from `io.grep` |
| CNT2 | Result shape | one `{file, language, lines, code, comment, blank}` per file; per-language totals left to Lambda |
| CNT3 | Counting rules | scc's: a line with code is code; a blank line inside a block comment is comment; a line inside a multi-line string is code; a line holding only a docstring is comment |
| CNT4 | Language table: source, form, first coverage | our own table on scc's schema, compiled in; the list in §5.3 |
| CNT5 | Files in no known language | left out; `unknown: true` counts them as plain text |
| CNT6 | Extras in v1 | bytes, minified and generated flags; complexity and duplicate files later |
| CNT7 | `'` in C and C++ | a char literal, except inside a number |
| CNT8 | Scoped search | an `in:` option on `io.grep`, after the counter |
| CNT9 | Where the code lives | in `lib/grep`, behind a per-file job interface in the walker |
| CNT10 | Performance gate | counts equal to scc's and tokei's except documented improvements; wall time within 1.5× of scc's |
| CNT11 | `wc` counts: where and when | an option `{wc: true}` on `io.loc` that adds `words`, `chars` and `max_line` (`lines` and `bytes` always present); a separate `io.wc` on the same engine is the alternative |
| CNT12 | `wc` definitions | `lines` counts an unterminated last line (as `grep -c ''` and scc, not `wc -l`); `chars` are code points by `str_utf8_count`; `max_line` in code points, not display width; whitespace for `words`: Unicode White_Space, to match `wc -w` in a UTF-8 locale, or ASCII as RE2's `\s` and the C locale |
| CNT13 | Word definitions for counting | `words` exactly as `wc -w` (runs of non-whitespace; whitespace per CNT12), and a separate UAX #29 word count (an option, e.g. `uwords`) on a generated Word_Break table, checked against `WordBreakTest.txt` (§5.7); `io.search`'s tokens are not used for counting |
