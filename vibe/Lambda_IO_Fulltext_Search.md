# Lambda IO Full-Text Search — ranked word search over files without an index (`io.search`)

**Date:** 2026-10-05

**Status:** Proposal; nothing implemented. FTX5 (stemmers), FTX12 (options aligned with `io.grep`), FTX13 (part-of-token terms, `word`), FTX10 (parallel execution, reused walk) and the `ignore_case` default of FTX4 were ruled by the user on 2026-10-05; every other `FTX#` decision is *proposed*, and §13 lists the questions that need the user's ruling.

**Scope:** a full-text search over a directory of text files, with the query language, tokenising and relevance ranking of a relational database's full-text search (SQLite FTS5, PostgreSQL `tsvector`/`tsquery`, MySQL `FULLTEXT`), executed the way PostgreSQL runs `to_tsvector(body) @@ query` on a column with no index: every call scans the documents, so results are always current and nothing has to be built or kept in sync. The directory walk, ignore layers, filters and literal acceleration are the ones `io.grep` already has (`lib/grep`, `vibe/Lambda_Lib_Grep.md`). The proposal covers a C+ library under `lib/fts/` and the Lambda system function `io.search()` that exposes it.

**Spec linkage:** no `S#`/`D#` ruling covers full-text search, so this doc opens the ledger series `FTX#` (FTX, not FTS, so that FTX5 is never read as SQLite FTS5). `io.search()` would be a new system function in the one registry of S17.2.1 and, once ruled and implemented, needs its own ruling in §17 of the semantics spec. Existing rulings that bind it: **S12.1.1v2** (its result depends on the file system, so it is a `pn`, as `io.grep` is under GRP26), **S17.4.1** (a text position is a code-point index into its source), **S17.7.1** (case-insensitive matching folds by Unicode simple case folding, which FTX4 uses for token normalisation), **S17.8.1** (unknown option names). The `io.grep` rulings it reuses unchanged: GRP14 (ignore defaults), GRP17 (UTF-8 only), GRP18 (`\n` and `\r\n` terminators), GRP19 (symlinks not followed), GRP22 (walk filters), GRP3 (vendor-free library API).

**Related docs:** `vibe/Lambda_Lib_Grep.md` (the walk, prefilter and kernels this builds on), `vibe/Lambda_IO_Shell.md` (`io.*` module, `io.grep` ruling), `vibe/Lambda_IO_RDB.md` (§12 item 7: FTS5 virtual tables in the RDB input, still open), `vibe/idea/Semantic Search.md` (keyword BM25 as the lexical half of a hybrid semantic search).

---

## 1. Summary

`io.grep` answers "where does this pattern occur". A full-text search answers "which documents are about these words, best first". The two differ in four ways, and this proposal adds exactly those four on top of the `io.grep` machinery:

| | `io.grep` | `io.search` (this proposal) |
|---|---|---|
| Unit of a result | a match (a span in a line) | a document (a file, or a paragraph or line of one, FTX2) |
| What is compared | a regex or literal against raw text | normalised words (tokens) against query terms (FTX3–FTX5) |
| Query | one pattern, or an alternation of patterns | a boolean combination of words, phrases and prefixes (FTX6) |
| Order | path, then position | relevance score (BM25), then path (FTX8) |
| Index | none | none: a scan per call, like PostgreSQL without a GIN index (FTX1) |

The design questions and their proposed answers:

| Question | Proposed answer | Section |
|---|---|---|
| Index or scan? | Scan. No persistent state; a later index is a separate proposal. | §3 |
| What is a document? | A file by default; a paragraph or a line by option. | §4.1 |
| What is a word? | Unicode letters, digits and marks; each CJK ideograph, kana and Hangul syllable is its own token. | §4.2 |
| Can BM25 work without an index? | Yes, exactly, if the prefilter is "any positive term" and length is measured in bytes. | §6 |
| How fast? | A literal prefilter (`lib/grep`, Teddy for term sets) skips files with none of the query's words; only candidates are tokenised. | §7 |
| Query syntax? | A web-search string (`"phrase" -not word*`), never a syntax error, as PostgreSQL's `websearch_to_tsquery`. | §5 |

---

## 2. Prior art: full-text search in relational databases

### 2.1 Features

| Feature | SQLite FTS5 | PostgreSQL | MySQL / MariaDB `FULLTEXT` | DuckDB `fts` |
|---|---|---|---|---|
| Boolean `AND`/`OR`/`NOT` | yes | `&` `\|` `!` | `+word -word` (boolean mode) | via its query macro |
| Phrase | `"a b"` | `<->`, `phraseto_tsquery` | `"a b"` (boolean mode) | no |
| Proximity | `NEAR(a b, n)` | `<n>` (exact distance) | `"a b" @n` (InnoDB) | no |
| Prefix | `pre*` | `pre:*` | `pre*` | no |
| Field filter | `col: word` | per-column `tsvector`, weights A–D | per index | per field |
| Tokeniser | `unicode61`, `ascii`, `trigram` | parser + dictionaries per language | built-in, `ngram` plugin for CJK | built-in |
| Stemming | `porter` wrapper (English) | Snowball, about 30 languages (18.6 ships Snowball 2.2.0) | none (InnoDB) | Snowball |
| Stop words | none | per configuration | built-in list | per configuration |
| Case / diacritics | folded / removed by default | folded / kept (`unaccent` extension) | per collation | folded / stripped |
| Ranking | `bm25()`, column weights | `ts_rank`, `ts_rank_cd` (cover density) | natural-language relevance | BM25 |
| Snippets / highlight | `snippet()`, `highlight()` | `ts_headline` | no | no |
| Lenient web syntax | no | `websearch_to_tsquery` | natural-language mode | no |

### 2.2 Index or scan

Three of the four require an index before a query can run, and maintain it on every write: FTS5 stores the index as the virtual table itself, MySQL refuses `MATCH … AGAINST` without a `FULLTEXT` index, and DuckDB builds a snapshot index that later writes do not update. **PostgreSQL alone can run a full-text query with no index**: `to_tsvector(body) @@ to_tsquery(…)` tokenises every row at query time. That works because PostgreSQL's ranking functions (`ts_rank`, `ts_rank_cd`) use only what is inside one document: term frequencies, positions and weights, never corpus statistics such as document frequency. BM25 does use corpus statistics, which is one reason FTS5 needs its index. §6 shows how a scan can compute those statistics exactly in the same pass.

For files the scan model fits better than the index model: a directory changes under the user, there is no write path to hook an index update onto, and `io.grep` has already shown that a parallel, prefiltered scan of a source tree is fast.

---

## 3. Execution model

**FTX1 (proposed). `io.search` scans; it builds and keeps no index.** Each call walks the source, reads candidate files and evaluates the query on them, so the result always reflects the files as they are, and there is nothing to create, refresh or invalidate. Repeated searches over a large, unchanging tree would benefit from an index; that is deferred to a separate proposal (§12), alongside the session caching GRP27 already defers for file-based `find`.

---

## 4. Documents and tokens

### 4.1 Documents

**FTX2 (proposed). A document is a file by default; `unit: "paragraph"` or `unit: "line"` makes each paragraph or line of a file a document.** A paragraph is a maximal run of lines that are not blank (a blank line is empty or only white space); a line is as in GRP18. A sub-file document carries its file, the code-point `index` of its first character (S17.4.1) and its 1-based first line number.

The file unit is what a database row usually is: one record. The sub-file units are what make the scan useful on long files, such as a book or a log, where "the file mentions both words" says little and "this paragraph does" says a lot. Ranking (§6) treats every unit the same way.

Binary files are skipped by the rule `io.grep` uses (a NUL in the first 8 KiB), and input is UTF-8 only (GRP17). Markup files are searched as plain text in v1: the words of `<title>` in an HTML file are words, and so is `title`. Extracting the text content of HTML, Markdown or PDF through Lambda's input parsers is open (§13, Q7).

### 4.2 Tokens

**FTX3 (proposed; revised 2026-10-05 to add Hangul). A token is a maximal run of letters, digits and combining marks; every other code point separates tokens, except that each CJK ideograph, hiragana, katakana and Hangul syllable is a token of its own (a unigram).** Classification uses `utf8proc_category` (`utf8proc` is already linked): categories `L*`, `N*`, `Mn`, `Mc` and `Me` are token characters. `utf8proc` exposes no Script property, so the unigram scripts are a small range table in `lib/fts` (the Han blocks and their extensions, Hiragana, Katakana and its extensions, Hangul Syllables).

With unigrams, a phrase query over consecutive characters finds a word without a dictionary: `"全文检索"` is the four-token phrase `全 文 检 索`. FTS5's `unicode61` instead makes a whole run of ideographs one token, which makes CJK text unsearchable below sentence length.

- **Chinese and Japanese** are written without spaces; per-character tokens are what Unicode word segmentation (UAX #29) and Elasticsearch's standard tokeniser give for ideographs and kana.
- **Korean** is written with spaces, but each space-separated unit (*eojeol*) is a stem with particles and endings attached: `학교에서` is `학교` ("school") plus `에서` ("at"). As one token, as UAX #29 would make it, `학교에서` would not be found by `학교`. Per-syllable tokens fix that: `학교` is the phrase `학 교`, which occurs inside `학 교 에 서`. This departs from UAX #29, which treats Hangul as ordinary letters. Snowball has no Korean stemmer, and the alternative is a morphological analyser (§13, Q9).
- **Hangul and `unaccent`.** Canonical decomposition splits a Hangul syllable into two or three conjoining jamo, which are letters, not marks. So `unaccent` (FTX4) recomposes after it drops the marks (NFD, drop `Mn`, NFC), and a syllable stays one token.

Unigrams find every word but rank coarsely: each character is a common term, so BM25 (FTX7) separates documents less well than with whole words. Overlapping bigrams are the usual remedy (§13, Q9).

Each token records its code-point offset and its length, so match positions (§9) can be reported in the unit S17.4.1 requires. Apostrophes and hyphens separate tokens (`don't` is `don` and `t`, `full-text` is `full` and `text`), as in FTS5. Because query text goes through the same tokeniser (FTX6), the phrase `"full-text"` still finds `full-text`, `full text` and `full, text`.

### 4.3 Normalisation

**FTX4 (proposed; `ignore_case` default USER, 2026-10-05). Tokens are compared after Unicode simple case folding (S17.7.1), and nothing else by default. Folding is the `ignore_case` option, as in `io.grep`, but it defaults to true.** Full-text search is case-insensitive in every engine of §2, so `io.search` folds unless asked not to; `{ignore_case: false}` compares tokens exactly, and the prefilter of §7 then searches case-sensitively. The folding is the one every case-insensitive search in Lambda uses, so `io.search` and `io.grep {ignore_case: true}` agree on which words are equal; it also keeps the prefilter sound, because `lib/grep` folds the same way. Like S17.7.1, folding does not normalise: a precomposed `é` and `e` followed by a combining accent are different tokens unless `unaccent` is on.

Three options change normalisation:

| Option | Effect | Prior art |
|---|---|---|
| `unaccent: true` | Decompose (NFD) and drop combining marks before folding, then recompose (NFC): `café`, `cafe` and a decomposed `café` become one token, and a Hangul syllable stays whole (FTX3) | FTS5 `remove_diacritics`, PostgreSQL `unaccent` |
| `language: "english"` | Stem tokens (FTX5) | FTS5 `porter`, PostgreSQL `english` |
| `stopwords: true` or an array of strings | Drop listed words from documents and queries, keeping their positions so phrases still count them | PostgreSQL configurations, MySQL stop-word list |

**FTX5 (USER, 2026-10-05). Stemming is off by default. The engine has one stemmer: English (Porter2), UTF-8. No other language, CJK included, has a stemmer in the engine; the full Snowball stemmer set, with the other languages, is a Jube module.** Without stemming, `run` does not find `running`, which is FTS5's default and what a user expecting `grep`-like exactness gets. With `language: "english"`, document tokens and query terms are both stemmed. *Why: all 30 UTF-8 languages are too big for the engine (§4.4), while English alone costs about 15 KB.*

CJK has no stemmer anywhere, Snowball included: Chinese has no inflection, and Japanese and Korean need morphological analysis rather than suffix rules, which FTX3's per-character tokens approximate (§13, Q9).

### 4.4 Stemmer size: the measurements behind FTX5

Snowball is a string-processing language plus a compiler, and `libstemmer_c` is its C distribution: the generated stemmers and a small runtime. Porter2 *is* Snowball's `english` stemmer, so a Porter2 taken from `libstemmer_c` gives the same stems as PostgreSQL's `english` configuration for the same Snowball release, and a hand port could only match it, not improve on it.

The figures below were measured on 2026-10-05 on the copy of `libstemmer_c` that PostgreSQL 18.6 ships (`mac-deps/rdb/build/postgresql-18.6/src/backend/snowball/libstemmer/` and `src/include/snowball/libstemmer/`, generated by Snowball 2.2.0). Newer Snowball releases add a few languages, so the full set is a lower bound.

**Source.**

| Part | Files | Lines | Bytes |
|---|---|---|---|
| English stemmer (Porter2), `stem_UTF_8_english.c` | 1 | 1,080 | 32,601 |
| Original Porter, `stem_UTF_8_porter.c` (what SQLite FTS5's `porter` implements; for comparison) | 1 | 724 | 20,829 |
| Runtime: `api.c`, `utilities.c` | 2 | 564 | 14,989 |
| Runtime headers: `api.h`, `header.h` | 2 | 93 | 3,386 |
| **English + runtime** | **6** (with `stem_UTF_8_english.h`) | **about 1,750** | **about 51,000** |
| All 30 UTF-8 stemmers (`stem_UTF_8_*.c`) | 30 | 35,262 | 1,263,287 |
| Everything PostgreSQL ships (adds Latin-1, Latin-2 and KOI8-R variants; Lambda is UTF-8 only, GRP17) | 49 `.c` | 49,777 | 1,721,406 |

The 30 UTF-8 languages: Arabic, Armenian, Basque, Catalan, Danish, Dutch, English, Estonian, Finnish, French, German, Greek, Hindi, Hungarian, Indonesian, Irish, Italian, Lithuanian, Nepali, Norwegian, Porter (original English), Portuguese, Romanian, Russian, Serbian, Spanish, Swedish, Tamil, Turkish, Yiddish.

**Compiled size** (`clang -O2`, arm64, Apple clang; text plus data as reported by `size`; PostgreSQL's runtime calls its own allocator, so the two runtime files were compiled against plain `malloc`, as upstream `libstemmer_c` uses):

| Part | Text | Data | Total |
|---|---|---|---|
| English stemmer | 4,713 | 3,520 | 8,521 (8.5 KB) |
| Runtime (`utilities.c` 5,592 + `api.c` 556) | | | 6,148 (6.1 KB) |
| **English + runtime** | | | **about 15 KB** |
| All 30 UTF-8 stemmers + runtime | | | **386,517 (about 387 KB)** |

Against a release `lambda.exe` of about 8 MB, English adds about 0.2%; the 30 languages would add about 5% for stemmers most users never call. Upstream's language dispatcher (`libstemmer.c`, which PostgreSQL replaces with its own `dict_snowball.c`) is not in these figures; it is a few hundred bytes.

What the engine vendors follows from this, under CLAUDE.md rule 16: the English stemmer and the runtime, unmodified, under `lib/snowball/` with a `VENDOR.md` that pins the upstream release and lists the subset (the MIR pattern, `lambda/mir/VENDOR.md`). How the Jube module registers the other languages with `lib/fts` is open (§13, Q10).

---

## 5. Queries

**FTX6 (proposed). The query is a string in web-search syntax, it never fails to parse, and its words go through the document tokeniser.** The syntax is PostgreSQL's `websearch_to_tsquery` plus FTS5's prefix and grouping, and suffix and infix terms (FTX13):

| Query | Meaning |
|---|---|
| `full text search` | all three words (implicit AND) |
| `"full text"` | the phrase: the words at consecutive token positions |
| `lambda or scheme` | either word (`or`/`OR`, as a separate word) |
| `-draft`, `-"to do"` | documents without the word or phrase |
| `pars*` | any token starting with `pars` |
| `*port` | any token ending with `port` (FTX13) |
| `*port*` | any token containing `port` (FTX13) |
| `(lambda or scheme) jit` | grouping |

Anything the grammar does not recognise is read as words: an unbalanced quote closes at the end, a stray `)` or `-` is a separator, an empty query matches nothing. *Why: the common source of a query is a search box, and a user's text should never raise an error (PostgreSQL made the same choice for `websearch_to_tsquery`).*

Query text is tokenised and normalised exactly as documents are, so a quoted phrase may become more tokens than it shows (`"don't"` is the phrase `don t`) and a stop word inside a phrase still takes its position. A term that becomes no token at all (only punctuation, or only stop words) is dropped. A query that has only negated terms matches every document without them, as in PostgreSQL.

**FTX13 (USER, 2026-10-05). A term can match part of a token: `pre*` a start, `*suf` an end, `*mid*` any part; and `io.grep`'s `word` option, default true, decides what a bare term matches.** With `word: true` a bare term `port` matches the token `port` only; with `word: false` it matches any token containing `port`, as `*port*` does, so `io.search {word: false}` finds what `io.grep` finds by default, word by word. A term written with `*` means the same under either setting. Matched tokens are folded first when `ignore_case` is on (FTX4).

- **Why a scan can afford it.** An index engine stores whole tokens in sorted order, so a prefix is a range lookup but a suffix or infix means visiting every token in the vocabulary; that is why PostgreSQL needs `pg_trgm` and FTS5 a `trigram` tokeniser for `LIKE '%mid%'`. A scan tokenises each candidate document anyway, and testing a token for a suffix or a substring costs about what testing it for equality does.
- **No stemming.** A term with `*`, and a bare term under `word: false`, is matched unstemmed, also when `language` is set: a stemmed affix has no stable meaning (`*ing` would never match a stemmed token).
- **Inside phrases.** Each word of a phrase may carry `*`: `"*port scan*"` matches `import scanner`. Under `word: false`, every word of a phrase matches as a part.
- **Terms that become several tokens.** A leading `*` applies to the first token and a trailing `*` to the last: `*don't*` is the phrase `*don t*`. A `*` with no token beside it (`*`, `**`, `* *`) is dropped, by the leniency rule above.
- **Ranking.** Every token a part-term matches counts as an occurrence of that term (`tf`), and `df` counts documents with any such token. BM25 is unchanged, but short part-terms match widely (`*at*` matches `data`, `pattern`, `that`), so they rank coarsely.
- **CJK.** Unchanged: each ideograph, kana or Hangul syllable is already a token of its own (FTX3), so a part of one is the whole of it.

Proximity (`NEAR(a b, n)` in FTS5) and per-field filters (`path: word`, §13 Q6) are not in v1. A structured query form built from Lambda values, instead of a string, is §13, Q2.

---

## 6. Ranking

**FTX7 (proposed). Documents are ranked by BM25 with FTS5's constants, `k1 = 1.2` and `b = 0.75`:**

```
score(D) = Σ over positive query terms t:
           idf(t) · tf(t,D) · (k1 + 1) / (tf(t,D) + k1 · (1 − b + b · len(D) / avglen))

idf(t)   = ln(1 + (N − df(t) + 0.5) / (df(t) + 0.5))
```

`tf(t,D)` is the number of occurrences of `t` in `D` (a phrase counts its occurrences as a phrase), `N` the number of documents searched, `df(t)` the number of documents containing `t`, `len(D)` the document's length. Negated terms filter and do not score. The score is a positive number, larger is better (FTS5 negates its `bm25()` so that `ORDER BY` sorts ascending; a Lambda result has no such reason).

**FTX8 (proposed). The statistics are computed exactly, in the scan itself, over the documents the call searched.** A scan has every document in front of it, so it needs no stored statistics, provided two things hold:

- **`df(t)` needs every document that contains any query term.** The prefilter (§7) therefore admits a file if it contains *any* positive term, never "all of them": a file with only one of two ANDed words is tokenised, is not a result, but adds to that word's `df`. A file the prefilter skips contains no query term, so it contributes `tf = 0` everywhere and only enters `N` and `avglen`.
- **`len(D)` is measured in bytes, not tokens.** BM25 uses length only in the ratio `len(D) / avglen`, so any consistent unit works; bytes are known for a skipped file from its size, at no cost, while a token count would force the scan to tokenise every file and give up the prefilter. For `unit: "line"` and `"paragraph"`, `N` and the lengths of skipped units still need the units' boundaries, which costs a newline scan of each skipped file (the SIMD newline count GRP30 uses), still without tokenising it.

Two consequences, which the function's documentation has to state:

1. **A score is relative to the documents searched.** The same file scores differently when it is searched within a different directory, because `N`, `df` and `avglen` change. FTS5 scores are relative to the whole index in the same way; a scan's corpus is simply the source of the call.
2. **A ranked search reads its whole source before returning anything**, because `idf` is only known at the end. `limit` caps the result, not the work. (No `io.search` call stops its walk early, ranked or not: FTX9.)

**FTX9 (proposed). `rank` chooses the order: `"bm25"` (the default), `"tf"`, or `false`.** `"tf"` is the BM25 formula with `idf = 1`, the per-document scoring PostgreSQL's `ts_rank` stands for: it needs no corpus statistics, so the prefilter may require all ANDed terms, which is faster on large trees. `false` returns matching documents in path order with no `score`. With `limit`, it does what `io.grep` does under GRP25 (corrected 2026-10-05; an earlier draft said the walk stops early): every file is still visited, each file stops evaluating after `limit` matching documents, since no file can contribute more, and the merge keeps the first `limit` in path order. The saving is within files, and only for `unit: "paragraph"` or `"line"`. Stopping the walk itself once enough documents are found is §13, Q11.

**Order and determinism.** Results are sorted by score descending, ties by path and then by position in the file. Scores are computed after the walk from per-document counts, in a fixed order, so neither the thread count nor timing changes a result (the stable-order rule of `io.grep`, §9B.3 of `vibe/Lambda_Lib_Grep.md`).

---

## 7. Execution

### 7.1 Pipeline

```
walk (io.grep's walk via grep_walk_paths: GRP14, GRP19, GRP22, one ThreadPool, FTX10)
  └─ per file, on a worker:
       prefilter  ── no positive term's literal occurs ──▶ count N, bytes; done
          │ hit
       tokenise + normalise (FTX3, FTX4, FTX5)
       look each token up in the query's term table; keep positions of query terms only
       segment into units (FTX2); evaluate the boolean query per unit
       record per unit: tf per term, length, start index/line; per term: df
  └─ after the walk, on the calling thread: merge per-file results in path order,
     sum N, df, lengths; idf, scores, sort, limit                     (FTX8, FTX10)
  └─ for the returned documents only: re-read for `matches` and `snippet` (§9)
```

Only positions of query terms are kept, never a document's vocabulary, so memory is proportional to the matching documents times the number of query terms. Match spans and snippets are computed in a second pass over just the documents that are returned, as FTS5 computes `snippet()` only for the rows a query outputs.

### 7.2 The literal prefilter and its soundness

The prefilter is a `lib/grep` matcher, built with `fixed_string` and `ignore_case`, over the set of literals that some positive query term requires; a set of up to 64 literals is one Teddy pass (GRP29). It must never skip a file that contains a token the query would match:

| Term kind | Literal | Sound because |
|---|---|---|
| word, no stemming | the folded word | `lib/grep` folds by S17.7.1, the same folding as FTX4 |
| `pre*`, `*suf`, `*mid*`, or a bare term under `word: false` | the term's text: `pre`, `suf`, `mid` | every token the term matches contains that text |
| phrase | its rarest word | every word of the phrase occurs |
| stemmed word | the stem's longest prefix the stemmer guarantees to keep (§13, Q4); none if that is empty | a stemmer may rewrite a word's end (`happy` → `happi`) |
| any term under `unaccent` | none | `café` does not contain the bytes of `cafe`; the file is tokenised |

When any positive term has no literal, the prefilter is off and every file is tokenised. A query of only negated terms has no prefilter either.

### 7.3 Parallel execution and library shape

**FTX10 (USER, 2026-10-05; library split proposed). `io.search` runs in parallel the way `io.grep` does, for both the walk and the search, and the walk is `io.grep`'s own, reused, not copied.** What `lib/grep` does today (`lib/grep/grep_walk.cpp`), and so what `io.search` does:

- **One thread pool for walking and searching** (GRP13). A directory job lists its entries and submits a job per subdirectory and a job per file, so traversal and search overlap with no separate work queue.
- **Threads:** one per core, at most 8 by default (`GREP_DEFAULT_MAX_THREADS`); a pool only when there is a directory or more than one path, otherwise the calling thread does the work.
- **The unit of parallel work is a file.** A file is prefiltered, tokenised and evaluated by one worker, start to end; files run concurrently. A single large file is not split across workers.
- **Per-worker state, no shared locks on the hot path.** A worker borrows one prefilter searcher (one RE2 per worker, GRP6) and one tokeniser, and returns them when the file is done.
- **Results buffered per file, merged on the calling thread.** A worker records its file's units (FTX8: `tf` per term, length, position) and its contribution to `N`, `df` and total length. After the pool drains, the calling thread sorts the per-file results by root index and path order (the same comparison `io.grep`'s sorted mode uses), sums the statistics in that order, computes `idf` and scores, and ranks. Floating-point sums in a fixed order make scores identical for any thread count.
- **One read per file.** The worker reads the file once into its buffer, runs the prefilter over that buffer (`grep_search_buffer`), and tokenises the same bytes on a hit.
- **Sources one after another.** An array of sources is walked one root at a time, each root in parallel, as `io.grep` does; a `limit` spans every source.

**The library split.** `lib/fts` (new, C+, vendor-free in its API as GRP3 requires of `lib/grep`) provides the tokeniser, the query parser and compiler, the per-document evaluator, the scorer and the snippet builder; `utf8proc` stays behind it. Today the walk is reachable only through `grep_search_paths`, which needs a regex matcher and drives grep's own per-file job. Under CLAUDE.md rule 13 the walk is promoted to its own entry point in `lib/grep/grep.h`, for example

```c
// called on a worker thread for every file the walk selects; `worker` is a
// stable slot in 0..threads-1 for per-worker state (searcher, tokeniser)
typedef GrepAction (*GrepFileVisitor)(void* user_data, int worker, const char* open_path,
                                      const char* label, size_t root_index);
GrepStatus grep_walk_paths(const char* const* paths, size_t count, const GrepWalkOptions* walk,
                           GrepFileVisitor visit, void* user_data);
```

`grep_search_paths` is re-expressed on it, its per-file job and sorted delivery unchanged, so `io.grep` keeps its behaviour and its tests; `lib/fts` supplies its own per-file job and merge. The ignore layers (GRP14), filters (GRP22), symlink rule (GRP19), root handling and thread pool stay in one place. The names are illustrative and settled in F0.

---

## 8. The Lambda surface: `io.search()`

**FTX11 (proposed). `io.search(source, query, options?)` is a procedure (`pn`).** It only reads, but its result depends on the file system, so S12.1.1v2 keeps it out of `fn`, exactly as GRP26 rules for `io.grep`. A missing source raises E401, as `io.grep` does.

```lambda
io.search(source, query)             // [{file, score}, ...]^E
io.search(source, query, options)
```

| Argument | Accepts | Meaning |
|---|---|---|
| `source` | a path to a file or directory, a wildcard path, or an array of paths | What to search; a directory is walked with the defaults of GRP14 |
| `query` | a string | Web-search syntax (FTX6) |
| `options` | a map | Below |

### 8.1 Options

**FTX12 (USER, 2026-10-05). `io.search` takes `io.grep`'s options wherever they apply, with the same names, types and meaning; three defaults differ, `ignore_case`, `text` and `word`, all true.** A user who knows one function knows the other's walk, limits, output fields and result shapes. The differences follow what full-text search is: it is case-insensitive in every engine of §2 (FTX4), it matches words rather than substrings (FTX13), and a paragraph or line result is read for its text. `limit` keeps its name although it selects differently: `io.grep` keeps the first matches in path order, `io.search` the highest-ranked documents (the first in path order under `rank: false`), and both mean "at most this many".

Options shared with `io.grep` (§9B.2 of `vibe/Lambda_Lib_Grep.md`):

| Option | Type | Default | Meaning in `io.search` | `io.grep` |
|---|---|---|---|---|
| `ignore_case` | bool | **true** | fold case by S17.7.1 (FTX4) | same, default false |
| `word` | bool | **true** | a bare term matches whole tokens; `false`: any part of a token, as `*term*` (FTX13) | a match must not touch a letter, digit or `_`; default false |
| `text` | bool | **true** | add `text`, the paragraph's or line's text without its final terminator; no effect with `unit: "file"` | same field for the matched line, default false |
| `line` | bool | false | add `line`, the 1-based first line of a paragraph or line, and to each of `matches` | same |
| `byte_offset` | bool | false | add `byte_offset`, the 0-based byte offset of a paragraph or line, and to each of `matches` | same |
| `line_ending` | bool | false | `unit: "line"` only: add `line_ending`, `"\n"`, `"\r\n"` or `null` (GRP31) | same |
| `context`, `before`, `after` | int | 0 | `unit: "line"` only: add `before`/`after`, the neighbouring lines' text (GRP24) | same |
| `limit` | int | none | at most this many documents, the highest-ranked | at most this many matches, the first |
| `limit_per_file` | int | none | at most this many documents per file, the highest-ranked in each; no effect with `unit: "file"` | at most this many matches per file |
| `files` | bool | false | return the paths of the files with a matching document instead of document maps (GRP21) | same |
| `count` | bool | false | return one `{file, count}` per file, `count` being its matching documents (GRP30); not with `files` | same, counting lines |
| `include`, `exclude` | string or array | none | gitignore-style globs (GRP22) | same |
| `max_depth`, `max_size` | int | none | walk depth and file-size limits (GRP22) | same |
| `hidden` | bool | false | also search names starting with `.` (GRP14) | same |
| `ignore` | bool | true | honour ignore files and skip dependency directories (GRP14) | same |
| `binary` | bool | false | search files with a NUL in their first 8 KiB instead of skipping them | same |

`io.grep` options that do not apply, and so are unknown names to `io.search` (S17.8.1): `whole_line` (no pattern to anchor) and `invert` (a `-term` in the query excludes).

Options of `io.search` only:

| Option | Type | Default | Meaning |
|---|---|---|---|
| `unit` | `"file"`, `"paragraph"`, `"line"` | `"file"` | FTX2 |
| `rank` | `"bm25"`, `"tf"`, `false` | `"bm25"` | FTX9 |
| `language` | string | none | stemming (FTX5): `"english"` is built in; other Snowball languages need the stemmer Jube module |
| `stopwords` | bool or array of strings | false | FTX4; `true` is the list for `language`, built in for English |
| `unaccent` | bool | false | FTX4 |
| `matches` | bool | false | add `matches` (§9) |
| `snippet` | int | 0 | add `snippet`, a window of about this many tokens (§9) |

An option name it does not define is a compile-time error in a literal map and a warning in a map value (S17.8.1).

---

## 9. Results

The result is an array of document maps, best first. With `files: true` it is instead the paths of the files with a matching document, and with `count: true` one `{file, count}` per such file, both in path order as in `io.grep` (GRP21, GRP30).

| Field | Present | Value |
|---|---|---|
| `file` | always | the file's path |
| `score` | unless `rank: false` | BM25 or tf score, a float |
| `index` | `unit` is `"paragraph"` or `"line"` | code-point offset of the unit in its file (S17.4.1) |
| `line` | with `line: true` and a sub-file unit | 1-based first line of the unit |
| `byte_offset` | with `byte_offset: true` and a sub-file unit | 0-based byte offset of the unit in its file |
| `text` | a sub-file unit, unless `text: false` | the unit's text, without its final terminator |
| `line_ending` | with `line_ending: true` and `unit: "line"` | `"\n"`, `"\r\n"`, or `null` for a last line with no terminator |
| `before`, `after` | with `context`, `before` or `after` and `unit: "line"` | arrays of the neighbouring lines' text |
| `matches` | with `matches: true` | `[{value, index}]` for every token occurrence that satisfied a positive term, in position order; `index` counts from the start of the file; `line` and `byte_offset` added with their options |
| `snippet` | with `snippet: n` | the text window of about `n` tokens that holds the most distinct query terms, with `…` where it cuts the document |

Highlighting is not built in: `matches` gives the spans, and marking them up (HTML `<mark>`, terminal colour) is the caller's choice. This is `ts_headline`/`highlight()` split into its data and its presentation.

```lambda
pn main() {
    // the ten files most about memory ownership
    let hits = io.search(/.doc, "memory ownership -draft", {limit: 10})^
    for (h in hits) print(h.file, " ", h.score, "\n")

    // paragraphs in the design docs, with a short excerpt
    let paras = io.search(/.vibe, "\"line delimiter\" or \"line join\"",
                          {unit: "paragraph", line: true, snippet: 24, include: "*.md"})^
    for (p in paras) print(p.file, ":", p.line, "  ", p.snippet, "\n")

    // stemmed English: finds "parsing", "parsed", "parser" — and every file that has one
    let any = io.search(/.src, "parse", {language: "english", rank: false})^
}
```

---

## 10. Coverage against the database engines and `io.grep`

| Feature | FTS5 | PostgreSQL | `io.search` v1 | `io.grep` |
|---|---|---|---|---|
| Works with no index | no | yes | **yes** | yes |
| Boolean, phrase, prefix | yes | yes | yes | alternation only |
| Suffix and infix within a word | `trigram` tokeniser (a separate index) | `pg_trgm` (a separate index) | yes, `*suf`, `*mid*`, `word: false` (FTX13) | yes, any pattern |
| Proximity | yes | yes | later (§13) | no |
| Relevance ranking | BM25 | `ts_rank` | BM25 or tf | no |
| Unicode tokenising, CJK | `unicode61` (CJK runs), `trigram` | parser (no CJK without extensions such as `zhparser`, `pg_bigm`) | yes, per-character CJK unigrams | n/a |
| Stemming, stop words | English (original Porter) | about 30 languages | English built in; ~30 languages as a Jube module | no |
| Diacritic folding | yes | `unaccent` | `unaccent` | no |
| Snippets, highlight spans | yes | yes | yes | line text, context lines |
| Field weights | columns | A–D labels | no (Q6) | no |
| Regex | no | `pg_trgm` + `~` | no | yes |
| Always current | on write | yes | yes | yes |

The one thing a database does that this proposal does not is answer quickly from an index when the corpus is large and the query is repeated (§12).

---

## 11. Performance expectations

The cost of a call is the walk, plus the prefilter over every file, plus tokenising the candidates. For a selective query (rare words) almost all files are rejected by the prefilter, and the call runs at close to `io.grep {files: true}` speed. For an unselective query (a common word, or `unaccent`, or stemming without a usable literal) every file is tokenised, and tokenising is the cost to measure: classifying each code point is a table lookup, but it is far slower than a SIMD literal scan. Phase F1 has to establish that throughput on the repository's own `doc/` and `vibe/` trees against `io.grep` before the prefilter's value can be stated; no figure is claimed here.

Tokenising in the classifier's ASCII fast path (bytes below `0x80` decided by a 128-entry table, `utf8proc` only for the rest) is the obvious first optimisation and is part of F1.

---

## 12. Later: an index

When the same large tree is searched often, a scan repeats work an index would keep. Two routes, both out of scope here:

- **Write an SQLite FTS5 database** for the tree and query it, reusing SQLite's index, ranking and snippets. This needs `SQLITE_ENABLE_FTS5` in the build (it is not set today; `lib/sqlite/sqlite3.c` contains the source) and would also answer `vibe/Lambda_IO_RDB.md` §12 item 7 for the RDB input. Its tokeniser differs from FTX3 (CJK), so results would differ between the indexed and the scanning path unless FTX3 is registered as a custom FTS5 tokeniser.
- **An own segment index** under a cache directory, keyed by path and modification time, updated incrementally on the next call. This is the session caching GRP27 defers for file-based `find`, extended to postings.

Either way the scan stays the reference behaviour: an index is an acceleration that must return what the scan returns.

---

## 13. Open questions for the user

1. **Name.** `io.search`, or `io.fts`, or `io.find_text`? `search` is the plain word but is generic; it would also be the natural name for a later semantic search (`vibe/idea/Semantic Search.md`).
2. **Query form.** FTX6 proposes a web-search string only. Should there also be a structured form built from Lambda values (for example an array is AND, a map `{or: [...]}`, `{not: …}`, `{phrase: "…"}`), so a program that builds a query never escapes quotes? `io.grep` precedent: no regex syntax appears at the Lambda surface, and patterns are Lambda syntax.
3. ~~**Stemmer.**~~ Ruled as FTX5 (USER, 2026-10-05): only English (Porter2, UTF-8) is in the engine, vendored from `libstemmer_c`; the other languages are a Jube module. Measurements in §4.4.
4. **Prefilter for stemmed terms.** Accept "the stem minus its last character" as the guaranteed literal for Porter2 (to be verified per suffix rule, with a test per rule, as §5.4 of `vibe/Lambda_Lib_Grep.md` does for literal extraction), or turn the prefilter off whenever `language` is set?
5. **Length unit.** FTX8 measures length in bytes so the prefilter can skip files. The alternative, tokens, is FTS5's definition and makes scores closer to SQLite's, at the cost of tokenising every file. Bytes are proposed.
6. **Fields.** Should the file's path be a second, weighted field (a word in the file name ranks higher, `path: word` filters), the closest thing a file has to FTS5 columns or PostgreSQL's A–D weights?
7. **Markup.** Search HTML, Markdown and PDF by their text content through Lambda's input parsers (slower; positions then refer to the extracted text, not the file), or keep v1 plain-text only?
8. **Proximity.** Add `NEAR(a b, n)` in v1, or later?
9. **CJK bigrams.** FTX3 makes each Han, kana and Hangul character a token (unigrams). Should v1 (or an option such as `cjk: "bigram"`) index overlapping pairs instead, as MySQL's `ngram` parser (default size 2) and Lucene's `CJKAnalyzer` do? Bigrams rank much better, because a pair is a far rarer term than a character, and a two-character word, the most common length in Chinese and Korean, becomes one exact term. The costs: about twice the terms per CJK text; a one-character query needs a prefix-style lookup over the bigrams; a match can straddle a word boundary (`검색` inside `전문검색엔진` is right, but a pair across two words is noise); and `index` and phrase positions must be defined for overlapping tokens. Dictionary-based analysers (jieba for Chinese, MeCab or Sudachi for Japanese, Nori or mecab-ko for Korean) give the best results but bring dictionaries of tens of MB and a new vendored dependency each; they are not proposed.
10. **Stemmer module registration.** How does the Jube module that carries the other Snowball languages (FTX5) provide them to `lib/fts`? The `rdb-drivers` proposal (`vibe/Lambda_IO_RDB.md` §13, RDB2–RDB4, not yet ruled) is the nearest model: a host-subsystem provider that registers versioned driver tables, discovered lazily from its manifest. What should `io.search` do when `language` names a language that no installed module provides: raise an error, or search unstemmed with a warning?
11. **Early stop under `rank: false`.** With `limit`, `io.search` (like `io.grep`) visits every file and keeps the first `limit` results in path order (FTX9). Stopping the walk once enough are found needs the walk to know that no unvisited file sorts before the ones already done, which `lib/grep`'s parallel walk cannot say today. Worth adding to the shared walk, for both functions, or not?

---

## 14. Phases

| Phase | Content | Done when |
|---|---|---|
| F0 | Promote the walk to `grep_walk_paths` with per-worker slots; re-express `grep_search_paths` on it (FTX10) | `lib/grep` and `io.grep` tests unchanged |
| F1 | Tokeniser and normaliser (FTX3, FTX4 folding, `unaccent`), ASCII fast path | unit tests on Latin, Greek, Cyrillic, Chinese, Japanese, Korean (eojeol with particles), combining marks, Hangul under `unaccent`; throughput measured against `io.grep` |
| F2 | Query parser and compiler (FTX6), never failing | a parse table of every construct and malformed input |
| F3 | Evaluator, BM25 and tf ranking, `rank: false` with early stop, file unit; `io.search` with the walk options | gtest corpus with hand-computed scores; Lambda `.ls` tests with expected `.txt` |
| F4 | Literal prefilter (§7.2) | same results with the prefilter on and off over the corpus; speed-up measured |
| F5 | `matches`, `snippet` (second pass over returned documents) | tests on spans and snippet windows |
| F6 | `unit: "paragraph"` and `"line"` | tests on unit boundaries, `index`, `line` |
| F7 | Vendor the English subset of `libstemmer_c` (`lib/snowball/`, FTX5); English stemming and stop words (after Q4) | Snowball English test vectors |
| F8 | Stemmer Jube module with the other Snowball languages (after Q10) | test vectors per language; `io.search` stems a non-English language only with the module installed |

---

## Ledger

| ID | Decision | Status |
|---|---|---|
| FTX1 | Scan per call; no index | proposed |
| FTX2 | Document unit: file, paragraph, line | proposed |
| FTX3 | Tokens: Unicode letters, digits, marks; per-character unigrams for Han, kana and Hangul | proposed (Q9) |
| FTX4 | Normalisation: simple case folding (S17.7.1) under `ignore_case`, default true; `unaccent`, `stopwords` options | proposed; `ignore_case` default USER, 2026-10-05 |
| FTX5 | Stemming off by default; only English (Porter2, UTF-8) in the engine; other languages a Jube module; no CJK stemmer | USER, 2026-10-05 |
| FTX6 | Web-search query string, never a parse error, tokenised like documents | proposed (Q2) |
| FTX7 | BM25, `k1 = 1.2`, `b = 0.75`, higher is better | proposed |
| FTX8 | Exact corpus statistics in the scan: any-term prefilter, byte lengths | proposed (Q5) |
| FTX9 | `rank`: `"bm25"`, `"tf"`, `false`; `limit` caps each file and the merge, never stops the walk | proposed (Q11) |
| FTX10 | Parallel like `io.grep`: one pool for walk and search, a file per job, per-worker state, per-file results merged in path order; walk reused through `grep_walk_paths`; `lib/fts` vendor-free | USER, 2026-10-05 (split proposed) |
| FTX11 | `io.search` is a `pn`; E401 on a missing source | proposed (Q1) |
| FTX12 | `io.grep`'s options where they apply, same names and meaning; `ignore_case`, `text` and `word` default to true; `limit` keeps the highest-ranked | USER, 2026-10-05 |
| FTX13 | Part-of-token terms `pre*`, `*suf`, `*mid*`; `word` option, default true, `false` makes a bare term match any part; unstemmed | USER, 2026-10-05 |
