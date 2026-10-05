# Lambda Impl — Full-Text Search (`lib/fts`, `io.search`)

**Date:** 2026-10-05

**Status:** Phases F0–F6 built on branch `fts`, then optimised (§3); F7 (English stemming and stop-word lists) and F8 (stemmer Jube module) not started. Design and rulings: `vibe/Lambda_IO_Fulltext_Search.md` (FTX1–FTX13).

---

## 1. What was built

| Phase | Content | Where |
|---|---|---|
| F0 | The walk promoted to `grep_walk_paths` with worker slots; `grep_search_paths` is now a visitor on it, behaviour unchanged. `grep_path_order` and `grep_input_is_binary` exported for reuse. | `lib/grep/grep.h`, `lib/grep/grep_walk.cpp`, `lib/grep/grep_searcher.cpp` |
| F1 | Tokeniser and normal form: letters, digits, marks; Han, kana and Hangul unigrams; S17.7.1 folding through RE2's fold table; `unaccent` as NFD, drop Mn, NFC | `lib/fts/fts_token.cpp` |
| F2 | Query parser and compiler: words, phrases, `or`, `-`, grouping, `pre*`/`*suf`/`*mid*`, `word`, stop words as phrase positions; never fails | `lib/fts/fts_query.cpp` |
| F3 | Evaluation per document; BM25, tf and unranked order; per-file and total limits; `io.search` | `lib/fts/fts_search.cpp`, `lambda/runtime/io_search.cpp` |
| F4 | Literal prefilter over each file's buffer | `lib/fts/fts_query.cpp` (`build_prefilter`), `fts_search.cpp` |
| F5 | `matches` and `snippet`, from a second read of the returned documents' files | `fts_document_matches`, `fts_document_snippet`, `io_search.cpp` |
| F6 | `unit: "paragraph"` and `"line"`, with `line_ending` and context lines | `fts_search.cpp` (`for_each_document`), `io_search.cpp` |

`io.grep` and `io.search` share option reading, sources and result values through `lambda/runtime/io_file_search.{hpp,cpp}`, moved out of `io_grep.cpp` (CLAUDE.md rule 13); `io_grep.cpp` keeps only what is grep's.

## 2. Decisions taken during the build

1. **Canonical fold.** A token's case-folded form is the smallest member of its S17.7.1 orbit, lowered with `utf8proc_tolower`: K, k and KELVIN SIGN give `k`; S, s and LONG S give `s`; Σ, σ and ς give `σ`. The orbit comes from RE2's `unicode_casefold` table (`LookupCaseFold`/`ApplyFold` from `re2/unicode_casefold.h`, an internal header as GRP7 allows for `regexp.h`), so a token equals exactly the spellings RE2's `(?i)` matches. That is what makes the prefilter sound: its literal is the folded token, and RE2 finds every spelling of it.
2. **Prefilter literals.** A file is skipped when it holds none of the leaves' literals and the query is false with every leaf absent. Literals are taken from every leaf, negated ones included, so a doubly negated leaf cannot be missed. A phrase's literal is its longest token; with `unaccent` or a language there is no prefilter (§7.2).
3. **Binary files** are skipped before the prefilter and count in no statistic, as `io.grep` skips them; the rule (`grep_input_is_binary`) is `lib/grep`'s, exported rather than copied.
4. **Statistics only when ranking needs them.** `documents`, `length_sum` and `df` are kept for BM25 only; `tf` and unranked searches skip counting a skipped file's paragraphs or lines.
5. **Reading.** The scan streams each file in windows (§3, item 5); a second read, on the calling thread, fetches the returned documents' text, context, matches and snippet, each file once. A file that changed between the reads gives clamped text, never a crash.
6. **Unranked early stop per file** (FTX9 as corrected): under `rank: false` a file stops being evaluated after `min(limit, limit_per_file)` documents; `files: true` stops each file at its first.
7. **`stopwords: true`** is an error until F7 brings Snowball's lists; an explicit array works. **`language`** is an error naming the language until F7 (`FTS_ERR_LANGUAGE`).
8. **Worker slots.** `grep_walk_paths` offers `threads + 1` slots when it has a pool (a job runs inline on the submitting thread when `tp_submit` fails), one otherwise; `begin` gives the count before any visit.

## 3. Optimisation round (2026-10-05)

Five changes, results unchanged (a 3.8 MB dump of nine queries in three modes over the whole repository is byte-identical before and after):

1. **ASCII word runs 16 bytes at a time.** `str_ascii_alnum_span` and `str_ascii_nonalnum_span` in `lib/str_simd.c` (NEON, SSE2, scalar elsewhere; beside the packed-pair kernel, as GRP8 places SIMD kernels), tested against a byte-at-a-time reference at every length and alignment. The tokeniser takes whole ASCII runs per call and decodes UTF-8 only for bytes >= 0x80.
2. **No copy for a token already in normal form.** A token of lowercase ASCII (or any token when neither folding nor unaccent applies) points into the text (`FtsToken.in_source`, read through `fts_token_text`); an ASCII token with capitals is lowered byte by byte; only non-ASCII tokens go through decode, fold and encode.
3. **Exact atoms by hash.** One open-addressing probe per token (djb2), behind a bitmask of the exact atoms' lengths that rejects most tokens before hashing; only `pre*`/`*suf`/`*mid*` atoms are scanned.
4. **Tokenise only near literals.** For paragraphs and lines, the prefilter now reports every literal in the window and only documents holding one are tokenised; the rest still count for BM25 (documents and bytes). Sound for the same reason the file-level prefilter is (§7.2): a document without any leaf's literal can neither match nor hold a scored term. A whole-file document is tokenised in full when the file holds a literal, since phrases and negations need all of its tokens.
5. **Streaming.** Files are read in windows (256 KiB, the slot's buffer, kept across files) cut after the last newline, or after the last blank line for paragraphs, so no token, line or paragraph is split; the window grows only for a longer line or paragraph. A whole-file document is scanned for a literal first and read again only when it holds one; its tokens accumulate across windows (`fts_eval_begin`/`fts_eval_add`/`fts_eval_finish`). Line numbers and code-point offsets carry across windows (tested on a 1.3 MB file with a 350 KB last line).

**Measurements** (release `lambda-cli.exe`, M3, 8 cores, the repository's ~24,300 selected files, about 257 MB tracked). The machine carried a load average of 45–95 from other builds throughout, so wall time is not reported; CPU time (user + system) per search, the mean of two runs of three searches each:

| Query | Before (CPU s) | After (CPU s) | |
|---|---|---|---|
| `io.grep "tokenizer"` files only (same walk, for reference) | 2.5 | 2.3 | |
| `tokenizer` | 2.27 | 2.22 | 1.0x |
| `garbage collection` | 2.68 | 2.33 | 1.15x |
| `"garbage collection"` | 2.82 | 2.57 | 1.1x |
| `the` | 3.52 | 2.82 | 1.25x |
| `pars*` | 2.96 | 2.48 | 1.2x |
| `*alloc*` | 3.30 | 2.88 | 1.15x |
| `unit: "line"` | 2.94 | 2.22 | 1.3x |
| `unit: "paragraph"` | 2.90 | 2.52 | 1.15x |
| `unaccent` (every file tokenised) | 5.60 | 2.94 | 1.9x |
| `-zzqq…` (every file tokenised) | 3.80 | 2.76 | 1.4x |

Every query now costs within about 25% of the walk and reads it shares with `io.grep`; the remaining time is there, not in tokenising.

## 4. Tests

- `test/lib/test_fts_gtest.cpp`: tokens (separators, unigrams, fold orbits, marks, `unaccent`, offsets against `str_utf8_count`), queries (boolean, phrase, grouping, part-of-token, `word`, leniency, stop words, language), BM25 against the formula over byte lengths, paragraph and line documents, limits, prefilter soundness under KELVIN SIGN, prefilter off when absence can match, 1 vs 8 threads bit-identical, missing root, matches and snippet.
- `test/lib/test_grep_gtest.cpp`: `WalkVisitsEachSelectedFileOnce` (every selected file once, slots exclusive).
- `test/lambda/proc/io_search.ls` with fixture `test/input/fts_tree`; `test/lambda/negative/semantic/io_search_unknown_option.ls`.

## 5. Open

- F7: vendor the English subset of `libstemmer_c` under `lib/snowball/` (FTX5) and the English stop-word list; needs the upstream release (the copy in PostgreSQL's tree has a modified runtime).
- F8: the stemmer Jube module (§13 Q10).
- Phase 2: format extractors (§13 Q7).
