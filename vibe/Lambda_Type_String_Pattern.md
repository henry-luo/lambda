# Lambda Type: String Patterns — Design

**Revision:** 2 (2026-10-23) — consolidation of the string-pattern type and operations record.
**Status:** design record. The pattern surface, `is`/`match`, `find`, `replace`, `split`, case folding and `io.grep` are ratified or user-ruled and implemented; file-based `find(path)` and file `replace` are designed but not built (§5.3, §5.4).
**Formal linkage:** the island surface and domains S11.1.2v4; ranges S11.1.3; value runs against text repetition S11.1.6v3; counts and wildcards S16.8.6v4; the type operators S10.1.1v4; the empty type S11.1.7; `match` S11.2.1; `split` S17.1.1; `find` positions S17.4.1; `replace` S17.6.1; case S17.7.1, S17.7.2; option maps S17.8.1; open issues SO32, SO47; type values D3.1.1v5, D3.1.2.
**Scope:** what a string pattern is as a type, the operations that use one, the matching engine, and file search; with the reasons and the history of what was rejected or reversed. The **surface syntax** — island grammar, quoted classes, domains, operator tiers — is ruled in [Lambda_Design_String_Pattern.md](Lambda_Design_String_Pattern.md) (SP1–SP24) and measured against regular expressions in [Lambda_Expr_String_Pattern.md](Lambda_Expr_String_Pattern.md) (SP17–SP23, SPO7–SPO14); this document summarizes that surface in §2 and defers to those records for detail. The string type itself is [Lambda_Type_String.md](Lambda_Type_String.md).
**Consolidates:** `Lambda_Type_String_Pattern.md` rev 1 (2026-01-27, the original pattern-type proposal), `Lambda_Type_String_Pattern2.md` (2026-02-28, pattern-aware `find`/`replace`/`split`) and `Lambda_Type_String_Grep.md` (2026-02-28, file-based `find`/`replace`, with its 2026-10-04 updates). Superseded positions are in Appendix S; their implementation detail and examples are not carried.
**Related:** [Lambda_Type_Pattern.md](Lambda_Type_Pattern.md) (occurrence counts, constrained types), [Lambda_Lib_Grep.md](Lambda_Lib_Grep.md) (GRP1–GRP32), [Lambda_IO_Shell.md](Lambda_IO_Shell.md) (`io.grep`), user doc `doc/Lambda_String_Pattern.md`.

---

## 1. A pattern is a type

A string pattern is a **first-class type value** that admits the strings — or, in the symbol domain, the symbols — whose whole content matches it (D3.1.1v5, D3.1.2). It goes wherever a type goes: an annotation, the right side of `is`, a `match` arm, a field type in a schema, an operand of the type operators. The search functions accept one where they accept a literal needle.

*Why patterns are types, not a regex library.* Format validation (an email, a version string, an identifier) is a type question — "is this value of this shape?" — and Lambda already has the machinery for type questions: `is`, `match`, annotations, schema validation, union and intersection. Making a pattern a type reuses all of it, and lets the compiler parse and check the pattern instead of receiving an opaque runtime string. The prior-art survey ([Lambda_Design_String_Pattern.md](Lambda_Design_String_Pattern.md) Appendix S §2) places Lambda between the extremes: C++ and Rust regexes are opaque strings with no compile-time checking and double escaping; JavaScript's `/…/` literal is visible to the host but poisons its lexer with the division ambiguity; Pomsky has the readable interior Lambda adopted (quoted literals, whitespace concatenation, named classes and sub-patterns) but lives outside its host's type system.

## 2. The surface, in summary

The authority for every point here is S11.1.2v4 and SP24.

- **One delimiter.** A pattern is an island `\( … )` (SP1). Outside an island the type grammar has no juxtaposition; the island is self-delimiting, balanced, and never confused with an escape (SP1 weighed and rejected `\…\` for five reasons).
- **One domain per island, from its quotes.** `"…"` literals make a string pattern, `'…'` literals a symbol pattern; named patterns keep their domain; mixed or absent domains are compile errors, and a union of the two domains is written between whole islands (SP24).
- **Literals, classes, wildcards, ranges.** Quoted text is literal. Inside an island only, the quoted escapes `"\d"`, `"\w"`, `"\s"`, `"\a"` (and `'\d'` … for symbols) are the digit, word, whitespace and alphabetic classes. Bare `.` is one character and `...` any run; both inherit the island's domain. `"a" to "z"` is a code-point range, one atom (S11.1.3).
- **Operators.** Whitespace concatenates; `|` is the island's only binary operator; prefix `!` complements a single-character set (SP17); the suffixes are `? + *` and the counts `{n}`, `{n+}`, `{n,m}` (S11.1.6v3, S16.8.6v4). Tiers, tightest first: atom, prefix `!`, suffix, concatenation, `|` (SP19). There is no `&` and no binary `!` inside an island (SP18, SP20); intersecting or excluding whole patterns is the type operators between islands, `\(A) & \(B)`, `\(A) ! \(B)`, `!\(P)` (S10.1.1v4).
- **Composition by name.** `type Digits = \("\d"+)` then `\("ID-" Digits)`.
- **Inline or named.** An island may be written inline wherever a type or value is expected, including as a function argument.

### 2.1 String enums and string patterns are one family

A literal union such as `type Method = "GET" | "POST"` and the island `\("GET" | "POST")` denote **the same type** (SP7); a literal-only island is represented as its literal union (S11.1.2v4). A pattern is what a literal union becomes once it needs repetition, classes or concatenation. Use a literal union for a finite set of exact values and an island for a format.

*History.* Rev 1 drew the opposite line: `type T = "a" | "b"` was an exact-value enum while `string S = "a" | "b"` was regex alternation, so `|` meant different things in the two forms. That contradicted `|` meaning union everywhere (S10.1.1v4) and is retired; the two readings coincide because a literal alternation *is* a union of singleton types.

### 2.2 Text repetition is not a value run

Outside an island an occurrence counts **separate values**: `"a"*` as a type is a run of string items. Inside an island it repeats **text**: `\("a"*)` is one string of `a`s (S11.1.6v3). The delimiter is what keeps the two apart, and it is the decisive reason a delimiter-free design failed (SP24 §2).

## 3. Operations

### 3.1 Full match: `is` and `match`

`s is P` and a `match` arm `case P:` test the **whole** value: the string must match the pattern from its first character to its last (S11.2.1). Arms are tried in order and the first match wins, so arm order matters when patterns overlap. Matching checks the domain before the content: a string never satisfies a symbol pattern (S11.1.2v4).

*Why full match.* It is what a type test means everywhere else: `"123" is int` asks whether the value is an int, not whether it contains one. There is no separate `matches(s, p)` function; `is` is the spelling.

### 3.2 Search: `find`, `replace`, `split`

These three **search** inside the subject (unanchored matching), as their counterparts do in every mainstream language. Each takes either a pattern or a plain string as its needle, with one rule set for both. They operate on string-domain patterns: a structural symbol-domain pattern is a domain error rather than a content match. A symbol subject is searched by its characters, and positions are code points in either kind (S17.4.1).

### 3.3 `find`

`find(s, needle, options?)` returns an **array of match maps** `{value, index}`, in position order, `[]` when nothing matches (S7.10.1v3). `value` is the matched text; `index` is the zero-based start in code points, the same domain as `index_of`, subscripts and `slice` (S17.4.1). The options are `limit` (first *n* matches), `last` (last *n*) and `ignore_case` (§3.6).

*Why match maps.* Python, JavaScript, Ruby and Go each ship a strings-only primary find plus a second function for positions; C# and Rust return one match object carrying both. Lambda took the latter: one function, no `find_at`, and `|> ~.value` recovers the strings. A `find_first` was considered and is covered by `{limit: 1}`.

### 3.4 `replace`

`replace(s, needle, replacement, options?)` steps through the matches as ECMAScript `String.prototype.replaceAll` does and inserts the replacement **literally** (S17.6.1, SP21). A match consumes its span; after an empty match the search advances one code point, so an empty match right after a non-empty one is replaced too, and an empty subject holds one empty match. `$&`, `\0` and every other substitution form mean nothing, since patterns have no captures (SO32). `limit`, `last` and `ignore_case` choose among the same matches and never change how they are found; every match `replace` replaces is one `find` returns.

*Why.* One call, one meaning; `split` and `find` already followed ECMAScript; and a substitution template has nothing to name without captures. The plain call had used RE2's `GlobalReplace`, which skipped an empty match after a non-empty one and rewrote `\0`–`\9` — colliding with Lambda's own string escapes.

### 3.5 `split`

`split(s, delimiter, keep?)` follows ECMAScript `String.prototype.split` for strings and patterns alike (S17.1.1): leading and trailing delimiters yield empty segments, a subject with no match yields itself as the one segment, a zero-width match on the current segment's start only advances the search (which suppresses a zero-width delimiter's outer empties), and the empty subject yields `[]` or `[""]` by whether the delimiter matches the empty string. With `keep` true the delimiters are interleaved in the result. `split(s, null)` keeps the Python-shaped whitespace form, which has no ECMAScript analogue.

### 3.6 Case-insensitive matching

`ignore_case: true` folds by **Unicode simple case folding**: one code point to one, the same in every locale, on every path — a literal needle, a literal-only pattern and any other pattern (S17.7.1, SP22). `ẞ` matches `ß` and the Kelvin sign matches `k`; `ß` does not match `ss`. A match keeps its length, so its span is defined. Folding does not normalize: precomposed `é` and decomposed `é` stay different. *Why simple folding:* it is what RE2 and JavaScript's `u` flag implement, and full folding changes lengths, which no regex engine supports. The case-*mapping* functions `upper`/`lower` use full mapping instead (S17.7.2, SP23) — mapping and folding answer different questions. How to spell a case-insensitive `is` or `match` is open (SPO12).

## 4. The matching engine

### 4.1 RE2

Patterns compile to RE2. The original survey weighed RE2, PCRE2, `std::regex`, Oniguruma and Hyperscan; RE2 won on a linear-time guarantee (no catastrophic backtracking on hostile input), bounded memory, UTF-8 support, a clean API and a BSD licence. Its costs were accepted: no backreferences and no lookaround.

Those costs shaped the language, deliberately. A construct belongs inside an island only if a regular-expression engine compiles it; anything else is a whole-pattern type operation, which combines two full matches and needs no regex support. That is why island `!` stops at one character (complementing a longer pattern needs a deterministic automaton), why there is no island `&` (RE2 cannot intersect; the old lookahead translation silently matched nothing), and why there is no binary `!` (SP17, SP18, SP20). The same reasoning closes rev 1's question about lookahead and lookbehind.

### 4.2 Anchored and unanchored forms

A compiled pattern holds a full-match regex for `is` and `match`; the unanchored form for searching is derived lazily on first use, because most patterns are only ever tested with `is`. Patterns are compiled once, at compile time, and stored with the script's types — which answers rev 1's question of whether patterns should be cached or interned.

### 4.3 Literals bypass the regex

A literal needle and a literal-only pattern take the shared literal scan kernels in `lib/`, not RE2 (T28-5.1). RE2 is vendored at `lib/re2/` with one patch, a NEON/SSE2 prefix-acceleration kernel used by in-memory `find`/`replace`/`split` (GRP15, GRP9v2).

## 5. Search beyond strings: files

### 5.1 The dimension

Rev 2's grep proposal gave `find` and `replace` a file dimension: when the first argument is a file path, the operation targets the file's text — built-in grep and sed. Two principles from that proposal stand: **a path type is required** (a string argument is always content, never a file name, so there is no ambiguity), and **file mutation is procedural**.

### 5.2 `io.grep` — built

`io.grep(source, pattern, options?)` is the full-option surface of `lib/grep` and is a procedure: its result depends on the file system, and an `fn` must be deterministic (S12.1.1v2; GRP26). It searches line by line — a match never spans a line terminator — and returns `{value, index}` match maps, where `index` counts code points from the start of the file as in-memory `find` does; options add `file`, a 1-based `line`, `byte_offset`, the line `text`, `line_ending`, context lines, or a per-file count. A directory honours ignore files and skips hidden and dependency directories (GRP14). It **does not replace** (GRP32): like `grep` and ripgrep it never changes a file; changing text is `input()`, `replace()`, `output()`. The ruling text is in [Lambda_IO_Shell.md](Lambda_IO_Shell.md) and the engine in [Lambda_Lib_Grep.md](Lambda_Lib_Grep.md); it is not yet ratified into the formal spec.

### 5.3 File-based `find(path, …)` — designed, not built

`find(path, pattern, options?)` stays an `fn` and runs on `lib/grep`, so it is line-oriented like `io.grep` (GRP27). Its result shape stays uniform with string `find`: a single file gives `{value, index}`, a directory or wildcard adds `file`, and `{line: true}` / `{byte_offset: true}` add those fields on request (revising the proposal's original "no extra fields"). Its determinism is to come from caching for the evaluation session, which a separate, not yet written proposal will design together with `input()`, `exists()` and `path#`. Blocked on that proposal ([LR09-34](Lambda_Issue_Ledger.md#lr09-34)).

### 5.4 File `replace(path, …)` — proposed, not built

The 2026-02-28 proposal registered a procedural `pn replace(path, pattern, replacement, options?)` beside the pure string `replace`, selected by the first argument's type and returning `null`; there was to be no pure file-replace mode, since `input(file) |> replace(~, …)` already gives the text. It was never enabled: its `("replace", 3)` registry key collides with the pure row ([LR09-1](Lambda_Issue_Ledger.md#lr09-1)). Since GRP32 names `input`/`replace`/`output` as the way to change file text, whether the dedicated form is still wanted is unruled.

## 6. Rejected, deferred and open

### 6.1 Rejected

| Proposal | Why it lost |
|---|---|
| A `/…/` regex literal | the division ambiguity and an unbalanced delimiter ([Design_String_Pattern](Lambda_Design_String_Pattern.md) Appendix S §2.1) |
| Patterns as opaque runtime strings | no compile-time checking, double escaping |
| The `\…\` delimiter | A1–A5: `\"` reads as an escaped quote, literal backslashes end it, runaway on a missing close, a trailing `\` reads as line continuation, poor composition (SP1) |
| Undelimited patterns, `string X = …` / `symbol X = …` definitions | juxtaposition leaked into the whole type grammar; definitions were detected by content analysis (SP1, SP8, SP10) |
| Bare reserved class names `d w s a` | superseded by quoted classes, which make both domains symmetric (SP2, SP3 → SP24) |
| `\symbol( … )` tagged delimiter and domain-free reuse of named patterns | superseded by quote-determined domains (SP14, SP15 → SP24) |
| `&` inside an island; binary `!`; multi-character `!` | regex engines cannot compile them; the whole-pattern operators already work (SP17, SP18, SP20) |
| The regex open count `T{n,}` | `T{n+}` keeps `+` meaning "or more" (S16.8.6v4) |
| `find_at` returning position pairs | `find`'s match maps carry both |
| A `matches(s, p)` function | `is` is the spelling |
| RE2 `GlobalReplace` semantics and its backslash rewrite | replaced by `replaceAll` stepping and literal replacement (SP21) |

### 6.2 Deferred and open

| Item | State | Home |
|---|---|---|
| Captures (`@name:` in rev 1), capture binding in match arms, a `capture()` function | deferred; gates substitution templates in `replace` | SO32 |
| Function-valued replacement `replace(s, p, f)` | deferred since rev 2; today an error | — |
| Whether an empty literal needle matches at every boundary | open by the user's decision | SO47, SPO14 |
| Spelling of a case-insensitive `is` / `match` | open | SPO12 ([LR05-17](Lambda_Issue_Ledger.md#lr05-17)) |
| What `.`, `...` and `"\s"` match across line terminators (`.` stops at a newline today, `...` does not) | open | SPO10 ([LR05-17](Lambda_Issue_Ledger.md#lr05-17)) |
| Precedence of the type operators in value expressions | open (the island half is SP19) | SPO7 ([LR05-17](Lambda_Issue_Ledger.md#lr05-17)) |
| Multi-letter and Unicode class names (`\p{Letter}`, `\p{Han}`) | deferred | SPO1 ([LR05-17](Lambda_Issue_Ledger.md#lr05-17)) |
| Recursive patterns; how detailed a mismatch diagnostic should be | rev 1 open questions, never ruled | [LR05-17](Lambda_Issue_Ledger.md#lr05-17) |
| A structured `validate(s, p)` result (matched prefix, failure position, expectation) | kept in view, not built | — |
| Pattern debugging (`pattern_to_regex`, a match trace) | kept in view, not built | — |
| Pattern-based overloading of functions | kept in view, not built | — |
| Symbol patterns constraining a map's or element's keys | proposed in rev 1, not built | — |
| A pattern type as a parameter or declaration annotation is rejected statically | open defect | [LR03-34](Lambda_Issue_Ledger.md#lr03-34) |
| A pattern named from another module resolves against the importer | open defect | [LR07-48](Lambda_Issue_Ledger.md#lr07-48) |
| Implicit validation when a string is assigned to a pattern type, and `as` | follows the general boundary rules (S11.4.1v3); `as` is SO33 | SO33 |

---

## Appendix S — Superseded positions

- **S-1.** ~~(Rev 1.) A pattern is defined by `string name = pattern` or `symbol name = pattern`, undelimited, with backslash classes `\d \w \s \a`, `\.` for any character, and counts `[n]`, `[n, m]`, `[n+]`.~~ **Replaced** by the island `\( … )` (SP1, 2026-08-07), brace counts (S11.1.6v2, 2026-09-22/23) and quoted classes (SP24, 2026-10-09).
- **S-2.** ~~(Rev 1.) `&` intersects patterns inside a pattern and `!` negates any pattern; `!` is compiled through a lookahead workaround.~~ **Replaced by SP17, SP18, SP20 (2026-09-27).**
- **S-3.** ~~(Rev 1.) `|` means exact-value alternation in `type T = …` and regex alternation in `string S = …`.~~ **Replaced by SP7 and S10.1.1v4:** `|` is union everywhere, and a literal-only island equals its literal union.
- **S-4.** ~~(Rev 1.) Symbol patterns use symbol literals and symbol ranges (`'a to 'z`); then (SP13) symbols are banned from the interior; then (SP14) `\symbol( … )` tags the domain and named patterns compose across domains (SP15).~~ **Replaced by SP24:** one delimiter, domain from quotes, domain-preserving references.
- **S-5.** ~~(Rev 1.) `find(s, p)` returns `[string]`; `find_at(s, p)` returns `[(start, end)]`; `replace` takes a string or a function; `match(s, p)` and `capture(s, p)` are functions.~~ **Replaced** by match-map `find` (rev 2) and `is`; function replacement and captures deferred (§6.2).
- **S-6.** ~~(Rev 2, design decision 3.) Inline patterns are not supported in expression context — `\d+` would be confused with escapes and `+` with arithmetic; patterns must be named.~~ **Reversed by SP1:** the delimiter removed the ambiguity, so an island is legal inline, including as an argument.
- **S-7.** ~~(Rev 2.) `replace` with a pattern uses RE2 `GlobalReplace`; `{limit: 1}` maps to `RE2::Replace`.~~ **Replaced by S17.6.1 (SP21).**
- **S-8.** ~~(Rev 2.) Empty-pattern matches follow RE2's default, advancing past zero-length matches.~~ **Replaced** by the ECMAScript rules of S17.1.1 and S17.6.1.
- **S-9.** ~~(Rev 2.) `match` with pattern arms needs a fix in the C transpiler.~~ The fix landed on 2026-02-28; the C2MIR back end has since been removed (MIR Direct is the only back end), so the issue is moot.
- **S-10.** ~~(Rev 1.) An empty string cannot be matched: `""` is null, so a pattern match on it returns an error.~~ **Replaced by S2.2.1:** `""` is a string, and a pattern that admits the empty string matches it.
- **S-11.** ~~(Grep proposal.) File `find` returns `{value, index}` only, with no `line` field; its options are `{limit, ignore_case}`; it searches whole file contents.~~ **Revised 2026-10-04** (GRP4, GRP27): line-oriented on `lib/grep`, with opt-in `line` and `byte_offset`.
- **S-12.** ~~(Rev 1.) The `/i` modifier, or a tagged `\tag( … )` island (SP16), spells case-insensitivity.~~ SP16's tag family is superseded by SP24, which introduces no tagged form; the spelling is open (SPO12), while the folding rule is S17.7.1.

## Appendix A — Implementation history

| Date | Change |
|---|---|
| 2026-01-27/28 | Rev 1: patterns in the grammar, AST nodes, RE2 compilation, `is` via full match; `TypePattern` type kind. |
| 2026-02-03/04 | Occurrence counts; string patterns integrated with type patterns. |
| 2026-02-28 | Rev 2: lazily compiled unanchored regex; pattern-aware `replace` and `split`, `split` keep-delimiters, `find` returning match maps; `match` pattern arms fixed. Found on the way: a crash replacing with `""` (then `ITEM_NULL`), a match map storing a `char*` where a `String*` belonged, and a thread-local `context` declared without `__thread`. |
| 2026-07-01 | `find` and `replace` options `limit`, `last`, `ignore_case`. |
| 2026-08-07 | SP1–SP16: the `\( … )` island, bare classes, `\symbol( … )`; atomic corpus migration; string ranges at run time ([impl record](<impl/Lambda_Impl_String_Pattern (done).md>)). |
| 2026-09-22/23 | Brace counts `{n}`, `{n+}`, `{n,m}` in islands as everywhere. |
| 2026-09-24 | T28-5.1: literal `replace`/`find` on the shared scan kernel. |
| 2026-09-27 | Regex-compatibility audit: ten conformance defects fixed; SP17–SP23 ruled and built; `ignore_case` folds alike on every path. |
| 2026-10-04/05 | `lib/grep`, vendored RE2 with the SIMD prefix patch, Teddy literal sets, `io.grep` with count and line-ending modes (GRP1–GRP32). |
| 2026-10-09 | SP24: quoted classes, one domain per island, `\symbol` retired. |

Key files: `lambda/runtime/parse_type_pattern.cpp` (island parsing and domain resolution), `lambda/runtime/re2_wrapper.cpp` (lowering to RE2, anchored and unanchored forms, search primitives), `lambda/runtime/lambda-eval.cpp` (`fn_find*`, `fn_replace*`, `fn_split*`), `lambda/runtime/io_grep.cpp` and `lib/grep/` (file search), `lib/re2/` (vendored engine).
