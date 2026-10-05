# Lambda Impl — Full-Text Search (`lib/fts`, `io.search`)

**Date:** 2026-10-05

**Status:** Phases F0–F6 built on branch `fts`; F7 (English stemming and stop-word lists) and F8 (stemmer Jube module) not started. Design and rulings: `vibe/Lambda_IO_Fulltext_Search.md` (FTX1–FTX13).

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
5. **One read per file in the scan**, with `file_read_all`; a second read, on the calling thread, for the returned documents' text, context, matches and snippet, each file once. A file that changed between the reads gives clamped text, never a crash.
6. **Unranked early stop per file** (FTX9 as corrected): under `rank: false` a file stops being evaluated after `min(limit, limit_per_file)` documents; `files: true` stops each file at its first.
7. **`stopwords: true`** is an error until F7 brings Snowball's lists; an explicit array works. **`language`** is an error naming the language until F7 (`FTS_ERR_LANGUAGE`).
8. **Worker slots.** `grep_walk_paths` offers `threads + 1` slots when it has a pool (a job runs inline on the submitting thread when `tp_submit` fails), one otherwise; `begin` gives the count before any visit.

## 3. Tests

- `test/lib/test_fts_gtest.cpp`: tokens (separators, unigrams, fold orbits, marks, `unaccent`, offsets against `str_utf8_count`), queries (boolean, phrase, grouping, part-of-token, `word`, leniency, stop words, language), BM25 against the formula over byte lengths, paragraph and line documents, limits, prefilter soundness under KELVIN SIGN, prefilter off when absence can match, 1 vs 8 threads bit-identical, missing root, matches and snippet.
- `test/lib/test_grep_gtest.cpp`: `WalkVisitsEachSelectedFileOnce` (every selected file once, slots exclusive).
- `test/lambda/proc/io_search.ls` with fixture `test/input/fts_tree`; `test/lambda/negative/semantic/io_search_unknown_option.ls`.

## 4. Open

- F7: vendor the English subset of `libstemmer_c` under `lib/snowball/` (FTX5) and the English stop-word list; needs the upstream release (the copy in PostgreSQL's tree has a modified runtime).
- F8: the stemmer Jube module (§13 Q10).
- Phase 2: format extractors (§13 Q7).
