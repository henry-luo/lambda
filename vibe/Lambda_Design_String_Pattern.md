# Lambda String and Symbol Patterns — Design

**Revision:** 7 (2026-10-23) — consolidation of the three string-pattern records into one.
**Status:** SP1–SP24 are user-ruled and implemented on all three execution tiers (interp, jit, auto); superseded rulings are in Appendix S. `io.grep` is user-ruled and built; file-based `find(path)` and file `replace` are designed but not built (§7.3, §7.4). Open questions are in §9.
**Formal linkage:** the island surface and domains S11.1.2v4; ranges S11.1.3; value runs against text repetition S11.1.6v3; counts and wildcards S16.8.6v4; the type operators S10.1.1v4; the empty type S11.1.7; `match` S11.2.1; `split` S17.1.1; `find` positions S17.4.1; `replace` S17.6.1; case S17.7.1, S17.7.2; option maps S17.8.1; module-wide pattern names and cycles S11.4.12; open issues SO32, SO47; type values D3.1.1v5, D3.1.2.
**Spec mapping:** SP1, SP4–SP8, SP10–SP12, SP24 → S11.1.2v4; SP5 → S11.1.3; SP17–SP20 → S11.1.2v4 and S10.1.1v4; SP21 → S17.6.1; SP22 → S17.7.1; SP23 → S17.7.2; SPO14 → SO47.
**Scope:** string and symbol patterns end to end — what a pattern is as a type, its surface syntax and domains, the operations that use one, its relation to regular expressions, the matching engine and file search — with the reasons and the history of what was rejected, reversed or superseded.
**ID series:** SP# (decisions, §8) and SPO# (open questions, §9). GRP# belongs to [Lib_Grep.md](lib/Lib_Grep.md).
**Consolidates:** `Lambda_Design_String_Pattern.md` rev 1–6 (2026-08-07 to 2026-10-09, the delimited-syntax design, SP1–SP24); `Lambda_Expr_String_Pattern.md` (2026-09-27, the regular-expression compatibility audit, SP17–SP23 and SPO7–SPO14); and `Lambda_Type_String_Pattern.md` rev 2 (2026-10-23), itself the consolidation of the 2026-01-27 pattern-type proposal, the 2026-02-28 pattern-operations proposal and the 2026-02-28 file find/replace proposal. Implementation anchors, probe inventories and examples are not carried; the git history keeps them.
**Related:** [Lambda_Type_String.md](Lambda_Type_String.md) (the string type), [Lambda_Type_Pattern.md](Lambda_Type_Pattern.md) (occurrence counts, constrained types), [Lib_Grep.md](lib/Lib_Grep.md) and [Lambda_IO_Shell.md](Lambda_IO_Shell.md) (file search), user doc `doc/Lambda_String_Pattern.md`, implementation record [Lambda_Impl_String_Pattern (done)](<impl/Lambda_Impl_String_Pattern (done).md>).

---

## 1. A pattern is a type

A string pattern is a **first-class type value** that admits the strings — or, in the symbol domain, the symbols — whose whole content matches it (D3.1.1v5, D3.1.2). It goes wherever a type goes: an annotation, the right side of `is`, a `match` arm, a field type in a schema, a union arm, a constrained base, an operand of the type operators. The search functions `find`, `replace` and `split` accept one wherever they accept a literal needle.

*Why patterns are types, not a regex library.* Format validation — an email, a version string, an identifier — is a type question: is this value of this shape? Lambda already has the machinery for type questions: `is`, `match`, annotations, schema validation, union and intersection. Making a pattern a type reuses all of it, and lets the compiler parse and check the pattern instead of receiving an opaque runtime string.

## 2. Position: a readable pattern language compiled to RE2

Lambda patterns are **not a regex dialect**. They are a readable pattern language in the Pomsky lineage — quoted literals, named classes, whitespace concatenation, named sub-patterns — embedded in the type system and compiled to RE2.

### 2.1 Prior art

| Approach | Host sees the pattern? | Cost |
|---|---|---|
| C++ `std::regex`, Rust `regex`: patterns as runtime strings | no, opaque text | no compile-time checking, no highlighting inside, host escapes fight pattern escapes (C++ grew raw strings largely for this) |
| JavaScript `/…/` literal | yes, through a lexer hack | `/` collides with division, so the lexer must know whether the previous token ends an expression; an unbalanced delimiter swallows the line on error |
| Pomsky, Melody: standalone pattern languages | fully — it *is* the language | a separate compile step, invisible to the host's type system; Melody's one-term-per-statement form is too verbose for type positions |
| **Lambda `\( … )`** | **fully, in the type system** | **one delimiter pair** |

The JavaScript lineage — QED, ed, grep, sed, Perl, JavaScript — inherited `/…/` from ed's command addresses; nobody chose a symmetric delimiter on merit, and Perl itself later added the balanced `m{…}`, `m(…)`, `m[…]`. Pomsky contributed the interior Lambda keeps: literal text always quoted (so the literal/metacharacter boundary is unmistakable, the first readability failure of classic regex), whitespace as concatenation, named classes, named sub-patterns, counted occurrences and quoted ranges.

### 2.2 Two consequences

1. **Compatibility is about semantics, not spelling.** A regex must be translated (§5.1 is the table); there is no raw-regex escape hatch. The useful question is whether a Lambda construct matches what the equivalent regex construct matches.
2. **RE2 bounds the feature set.** RE2 guarantees linear-time matching, so no Lambda pattern backtracks catastrophically. The price is the non-regular features — backreferences and lookaround — and that price shapes the language (§3.4, §6).

## 3. The surface

The authority for every point in this section is S11.1.2v4.

### 3.1 The island

- **One delimiter.** A pattern is an island `\( … )` (SP1). The general type grammar has no juxtaposition; whitespace concatenation exists only inside an island (SP8). A pattern definition is syntactically self-evident — `type X = \( … )` — with no content analysis (SP10).
- **Literals.** Quoted text is literal, and one quoted fragment is one operand: a suffix repeats all of it. Ordinary escapes decode once, so `"\n"` is a newline and `"\\d"` is a backslash followed by `d`.
- **Classes.** Inside an island only, the quoted escapes `"\d"`, `"\w"`, `"\s"`, `"\a"` are the digit, word, whitespace and alphabetic classes in the string domain, and `'\d'`, `'\w'`, `'\s'`, `'\a'` are their symbol-domain counterparts (SP24). Operators stay outside quotes: `"\d+"` is a digit followed by a literal plus; `"\d"+` repeats a digit. Bare `d`, `w`, `s`, `a` are ordinary pattern names.
- **Wildcards.** Bare `.` is one character and `...` any run (S16.8.6v4); both inherit the island's domain. Quoted `"."` and `"..."` are literal punctuation.
- **Ranges.** `"a" to "z"` is the set of code points between its bounds; each bound is one code point (S11.1.3). `X to Y` is purposely overloaded across value, type and pattern position because it denotes one set in all three: the literal array of consecutive values, a range type matched by membership, and a character class (SP5).
- **Operators.** Whitespace concatenates; `|` is the only binary operator; prefix `!` complements a single-character set (SP17); suffixes are `?`, `+`, `*` and the counts `{n}`, `{n+}`, `{n,m}` (S11.1.6v3). Tiers, tightest first: atom (a range is one), prefix `!`, suffix, concatenation, `|` (SP19). There is no `&` and no binary `!` inside an island (SP18, SP20).
- **Composition.** A named pattern is referenced by name and keeps its domain. Top-level pattern names bind module-wide, so islands compile in dependency order; any reference cycle is E240, since no regular pattern can recurse (S11.4.12).
- **Whole-pattern operators.** Intersecting, excluding or complementing whole patterns is the type operators between islands: `\(A) & \(B)`, `\(A) ! \(B)`, `!\(P)` (S10.1.1v4). They combine two full matches and need no regex support.
- **Inline or named.** An island may be written wherever a type or value is expected, including as a function argument.

### 3.2 One domain per island

Quotes and named patterns contribute domains, and all contributions must agree, across union branches too. Double quotes select strings, single quotes select symbols. A mixed island, or one with no domain-bearing literal or reference — `\(.)`, `\(...)`, and grouped or repeated wildcard-only forms — is a compile error; it neither defaults to strings nor admits both. A union of string and symbol patterns is written between whole islands. Matching checks the domain before the content, so a string never satisfies a symbol pattern (SP24).

*Why infer the domain from quotes (SP24, 2026-10-09).* Quoted class escapes make the two domains symmetric without spending a second delimiter: `'a'` stays the literal letter and only `'\a'` is a class, and a class-only pattern still has a domain through its quotes, so no literal prefix or empty symbol is needed (the ban on `''`, S2.2.2v2, stands). The cost is accepted openly: a reference keeps its domain instead of silently lending domain-free content, a wildcard-only island is rejected rather than inventing a third, dual-domain kind of pattern, and a domain mismatch is an error even in a branch that would never match. The symbol-domain sweep that preceded this is in Appendix S (S-4).

### 3.3 Why a delimiter at all

*The bare syntax failed (rev 1 → SP1, 2026-08-07).* The first syntax, `string Phone = \d[3] "-" \d[3] "-" \d[4]`, had nothing marking where a pattern began or ended. Readers could not tell a pattern from a concatenation or garbage. Whitespace concatenation was the one place juxtaposition meant something in type syntax, and it leaked: a dynamic-precedence guard so `int[2+]` was not read as a concatenation, an AST-builder patch rejecting concatenation everywhere else, and definitions detected by analysing content. The class tokens `\d`, `\w`, `\s`, `\a`, `\.` were tokens of the whole type grammar.

*Why `\( … )` and not `\…\` (SP1).* The symmetric backslash pair kept the familiar `\d` digraph at the opening, but: a pattern opening with a literal reads as an escaped quote (`\"v" …`); a backslash inside a quoted literal would end the pattern unless the scan is quote-aware; a forgotten terminator runs to the next pattern and fuses two definitions; a trailing `\` reads as line continuation; and suffixes and unions compose badly. `\( … )` is balanced (bounded errors, editor bracket matching, multi-line patterns), never abuts a quote, extends the "backslash enters pattern-land" family, and needs no lexer state. JavaScript's two costs are absent: `\` has no infix meaning, and the delimiter balances.

*Why the delimiter survives (SP24 §2).* An attempted delimiter-free redesign collided with value occurrences: outside an island `"a"*` is a run of separate string values, while `\("a"*)` is one string whose contents repeat (S11.1.6v3). The island is what keeps text repetition apart from value runs, and character complement apart from whole-type complement.

*Why `that ( … )` keeps its parentheses (SP9, reversed in rev 4).* Rev 1 hoped that removing juxtaposition would let a constraint drop its parentheses. The boundary sweep (SPO3) showed that an unparenthesized constraint opens the whole expression grammar into type positions: the map-field comma, a return type followed by a body brace, `|` being a value operator too (so `int that ~ > 0 | string` parses both ways), the `let` initializer and the match-arm colon. The parentheses are the wall between the two grammars, consistent with S11.2.1's `case int that (~ > 0):`.

### 3.4 Why the island's operators are regex's (SP17–SP20, 2026-09-27)

Together, SP17–SP20 give an island exactly regex's operators plus a prefix `!`, and each rejected construct is one a regular-expression engine cannot compile.

*SP17 — `!` complements one character.* Its operand must be a class, a range, a one-character literal, or a union, group, negation or named pattern built only from these, so `!("a" | "b" | "c")` is `[^abc]`; anything else is a compile error. Reasons: it is the negation every regex dialect has (`[^…]`, `\D`), so RE2 compiles it as one bracket class; complementing a longer pattern needs a deterministic automaton, which can grow exponentially per nested `!` (the tools that offer it — Ragel, dk.brics.automaton, Lucene, RE# — are built on deterministic automata or derivatives for that reason); backtracking engines cannot complement either and offer zero-width lookahead instead, which RE2 drops; the natural reading of "not `abc`" is usually "does not contain `abc`", not the complement, which includes `xabcx`, and a bare complement matches the empty string almost everywhere in a search; captures and first-match priority mean nothing under complement; and the whole-string cases already work through the type operators. One character covers the everyday need, as in `"<" (!">")* ">"`; what stays out is "a run not containing X".

*SP18 — no binary `!` inside a pattern.* `\("\w" !"\d")` is a word character followed by a non-digit. A binary reading would collide with concatenation, silently changing working patterns or making meaning depend on spacing; between longer patterns it is pattern difference, which SP17 rules out; the whole-pattern form `\(A) ! \(B)` works; and regex offers only character-set difference, in a few dialects (JS `v`, .NET, Java, Perl's experimental form) and not in RE2.

*SP19 — the tiers.* Atom, prefix `!`, suffix, concatenation, `|`. These are regex's tiers wherever the two share one (`\("a" "b"+)` is `ab+`; `\("a" | "b" "c")` is `a|bc`). A range is one atom, as `[a-z]` is. Prefix before suffix reproduces regex's negated classes — `!"\d"+` is `(!"\d")+`, regex `\D+` — and the other reading complements a multi-character pattern, which SP17 rejects, so no legal program loses a meaning. It is the reverse of the usual programming-language convention, where postfix binds tighter. With one binary operator nothing else needs ranking; JS's `v` flag refuses to rank its set operators at all.

*SP20 — no `&` inside a pattern.* The island `&` compiled to a lookahead that RE2 rejects, so every island `&` silently matched nothing; the whole-pattern form `\(A) & \(B)` works; and regex offers only character-set intersection, not in RE2. SPO13 had recommended admitting `&` between single-character sets; the ruling chose the smaller language.

### 3.5 String enums and string patterns are one family

A literal union such as `type Method = "GET" | "POST"` and the island `\("GET" | "POST")` denote **the same type**; a literal-only island is represented as its literal union, groups included (SP6, SP7, S11.1.2v4). General type expressions admit literals and literal unions without a delimiter; anything beyond literals and `|` needs one. Use a literal union for a finite set of exact values and an island for a format.

*History.* The 2026-01 proposal drew the opposite line: `type T = "a" | "b"` was an exact-value enum while `string S = "a" | "b"` was regex alternation, so `|` meant two things. That contradicted `|` meaning union everywhere (S10.1.1v4); a literal alternation *is* a union of singleton types, so the readings coincide.

## 4. Operations

### 4.1 Full match: `is` and `match`

`s is P` and a `match` arm `case P:` test the **whole** value (S11.2.1). The compiled regex is anchored at the start and end of the text, so a trailing newline gets no slack. Arms are tried in order and the first match wins. There is no separate `matches(s, p)` function. *Why full match:* it is what a type test means everywhere else — `"123" is int` asks whether the value is an int, not whether it contains one.

### 4.2 Search: `find`, `replace`, `split`

These **search** inside the subject (unanchored matching), leftmost-first with greedy suffixes — the Perl and JavaScript discipline, not POSIX leftmost-longest. Each takes a pattern or a plain string as its needle, under one rule set. They operate on string-domain patterns: a structural symbol-domain pattern is a domain error rather than a content match. A symbol subject is searched by its characters, and every reported position is in code points in either kind (S17.4.1). Zero-width advances step one code point, never a byte.

### 4.3 `find`

`find(s, needle, options?)` returns an **array of match maps** `{value, index}` in position order, `[]` when nothing matches (S7.10.1v3). `index` is the zero-based code-point start, the same domain as `index_of`, subscripts and `slice` (S17.4.1). The options are `limit` (first *n*), `last` (last *n*) and `ignore_case`. Empty matches are reported as JavaScript's `matchAll` reports them, and an empty subject is searched too, since a pattern can match `""`.

*Why match maps (2026-02-28).* Python, JavaScript, Ruby and Go each ship a strings-only primary find plus a second function for positions; C# and Rust return one match object carrying both. Lambda took the latter: one function, no `find_at`. A `find_first` is `{limit: 1}`.

### 4.4 `replace`

`replace(s, needle, replacement, options?)` steps through the matches as ECMAScript `String.prototype.replaceAll` does and inserts the replacement **literally** (SP21, S17.6.1). A match consumes its span; after an empty match the search advances one code point, so an empty match right after a non-empty one is replaced too, and an empty subject holds one empty match. `$&`, `$1`, `\0` and every other substitution form are literal text, since patterns have no captures (SO32). `limit`, `last` and `ignore_case` choose among the same matches and never change how they are found; every match `replace` replaces is one `find` returns.

*Why (SP21, 2026-09-27).* One call, one meaning: the plain call had used RE2's `GlobalReplace` — which skips an empty match touching the previous match (Go's rule) and rewrites `\0`–`\9` while silently dropping other backslash sequences — whereas any option switched to a literal loop, so merely passing an option changed the result. `split` and `find` already followed ECMAScript; and a substitution template has nothing to name without captures, while RE2's rewriter collided with Lambda's own string escapes.

### 4.5 `split`

`split(s, delimiter, keep?)` follows ECMAScript `String.prototype.split` for strings and patterns alike (S17.1.1): leading and trailing delimiters yield empty segments; no match yields the subject as one segment; a match ending on the current segment's start only advances the search, which suppresses a zero-width delimiter's outer empties; the empty subject yields `[]` or `[""]` by whether the delimiter matches the empty string. With `keep` true the delimiters are interleaved, as with a capturing group in JavaScript. `split(s, null)` keeps the Python-shaped whitespace form, which has no ECMAScript analogue.

### 4.6 Case-insensitive matching and case mapping

`ignore_case: true` on `find` and `replace` folds by **Unicode simple case folding**: one code point to one, the same in every locale, on every path — a literal needle, a literal-only pattern and any other pattern (SP22, S17.7.1). `ẞ` matches `ß`, the Kelvin sign matches `k`, `ς` matches `σ`; `ß` does not match `ss` and `İ` does not match `i`. A match keeps its length, so its span is defined. Folding does not normalize: precomposed and decomposed `é` stay different.

*Why simple folding (SP22).* It is what RE2 and JavaScript's `u` flag implement; full folding (`ß` ↔ `ss`) changes lengths, which no regex engine supports and which would leave a match's span undefined; and it needs no Turkish special case. Before the ruling the literal path folded ASCII only while the regex path folded Unicode, so a literal-only island's meaning depended on the path, against SP7.

*Why full mapping for `lower`/`upper` (SP23).* Mapping produces text for people to read, so `upper("straße")` must be `"STRASSE"`, and a final capital sigma lowercases to `ς`. It is what JavaScript's `toUpperCase`/`toLowerCase` do, so Lambda and LambdaJS share one implementation (S17.7.2). Mapping is not folding: case-insensitive comparison is not `lower(a) == lower(b)`.

`is`, `match` and `split` have no case-insensitive form, and there is no inline flag; the spelling is open (SPO12).

## 5. Compatibility with regular expressions

Measured against ECMAScript `RegExp` first, with Python and RE2 where they differ. The audit of 2026-09-27 found ten conformance defects; all were fixed that day (Appendix A.2). This section states the current behavior.

### 5.1 Construct mapping

| Lambda (inside `\( … )`) | JS `RegExp` | Notes |
|---|---|---|
| `"abc"` | `abc` | always literal; one atom, so `"ab"+` is JS `(?:ab)+` |
| `"\d"`, `"\w"` | `\d`, `\w` | ASCII, the same as JS |
| `"\s"` | `\s` | **differs:** `[\t\n\f\r ]` only (§5.2) |
| `"\a"` | `[a-zA-Z]` | no JS shorthand; ASCII |
| `.` | `.` with `u` | **differs** at `\r`, U+2028, U+2029 (§5.2) |
| `...` | `[^]*`, or `.*` with `s` | matches newlines (S16.8.6v4) |
| `!"\d"`, `!("a" \| "b")`, `!">"`, `!("a" to "f")` | `\D`, `[^ab]`, `[^>]`, `[^a-f]` | any single-character set (SP17) |
| `"a" to "z"` | `[a-z]` | any code points |
| `x \| y`, `x y`, `( … )` | `x\|y`, `xy`, `(?: … )` | groups never capture |
| `x?`, `x*`, `x+`, `x{n}`, `x{n,m}` | the same | greedy only |
| `x{n+}` | `x{n,}` | Lambda's open count; `{n,}` is rejected by name |
| `Name` | none | a named pattern, inlined; no recursion |
| `'…'` literals | none | the same language in the symbol domain |
| `s is P`, `case P:` | `/^(?:…)$/u.test(s)` | always a full match |
| `find(s, P)` | `[...s.matchAll(/…/gu)]` | `{value, index}`, code-point index |
| `replace(s, P, r)` | `s.replaceAll(/…/gu, r)` | `r` literal |
| `split(s, P)`, `split(s, P, true)` | `s.split(/…/u)`, `s.split(/(…)/u)` | S17.1.1 |

### 5.2 Semantic divergences

- **Within the supported subset Lambda matches as JavaScript does with the `u` flag:** leftmost-first alternation, greedy quantifiers, ASCII `\d` and `\w`, one code point per `.`.
- **`"\s"` is narrower.** It is RE2's Perl class, `[\t\n\f\r ]`; it misses the vertical tab and every Unicode space JS `\s` covers (no-break space, em space, U+2028, BOM). RE2 and Go are the outliers: PCRE2, Perl 5.18+, Python, Java, .NET and POSIX `[[:space:]]` all include the vertical tab. Open as SPO10.
- **`.` stops only at `\n`.** RE2, Perl, PCRE, Python and Go exclude only `\n`; ECMAScript and Java exclude all four line terminators. Open as SPO10. `...` matched newlines only after the audit: it had compiled to a newline-blind `.*`, contradicting S16.8.6v3's `...` ≡ `any*`.
- **`"\a"` is ASCII.** `"é"` is not alphabetic in this sense; Unicode classes wait for SPO1.
- **Code points, not UTF-16 units.** `"😀"` is one `.`, as in JS only under `u`, and `find` reports code-point indices where JS reports UTF-16 indices.
- **Counts are capped at 1000**, RE2's limit; a larger count is a compile error. JS has no practical limit.

### 5.3 Regex features Lambda lacks

| Feature | State |
|---|---|
| Capture groups, named groups | none; groups never capture. Captures, capture binding in `match` arms and substitution templates are SO32 |
| Backreferences | none — not regular, so RE2 cannot |
| Lookaround | none — RE2 cannot; this is also why island `&` failed and was removed (SP20) |
| Anchors `^ $`, word boundaries `\b \B` | none (E103). `is`/`match` are always anchored and a search cannot be. RE2 supports all four, so this is a **design gap, not an engine limit** |
| Lazy quantifiers | none (E103). Irrelevant to a full match but not to search: `<.*?>` has no spelling. RE2 supports them; a design gap |
| Bracket sets | unions of literals and ranges, complemented by `!` (SP17) |
| `v`-flag set algebra | none; spell the remaining set as a union, or combine whole patterns |
| Unicode properties | none; SPO1 |
| Escapes `\t`, `\uXXXX`, `\xHH` | written inside quoted literals |
| Flag `i` | `ignore_case` on `find` and `replace` only (SPO12) |
| Flags `m`, `s`, `y`, `g`, `d` | none; search is always global. `s` matters while `.` stays newline-blind (SPO10) |

### 5.4 Lambda features regex lacks

- Named, composable sub-patterns, inlined like Pomsky's `let`.
- A domain: symbol patterns match symbols and never strings.
- Patterns are type values: annotations, `match` arms, union arms, constrained bases, schema fields, and whole-pattern `&`/`!` through the type algebra.
- One denotation for ranges across value, type and pattern position (SP5, S11.1.3).
- Linear-time matching.

### 5.5 The same operators in value expressions

S10.1.1v4 makes `|`, `&` and `!` the type operators in type expressions, match or-patterns, string patterns and value expressions alike, and promises a type operation the same meaning wherever it is written. The type-pattern parser and the reference grammar rank them `to` above `&` above `!` above `|`; the C expression parser gives all four one binding power, left-associative. So `type T = "a" | "b" & "c"` admits `"a"` while `let x = "a" | "b" & "c"` admits nothing, and `let r = 5 | 1 to 3` fails at run time while the same type admits 5 and 1 to 3. No ruling fixes the value-expression tiers (SPO7); the divergence is [LR02-37](Lambda_Issue_Ledger.md#lr02-37). The recommendation is to make the reference-grammar order a clause of S10.1.1 and align the C expression parser with it.

## 6. The matching engine

*Why RE2 (2026-01-27).* The survey weighed RE2, PCRE2, `std::regex`, Oniguruma and Hyperscan. RE2 won on a linear-time guarantee (no catastrophic backtracking on hostile input), bounded memory, UTF-8 support, a clean API and a BSD licence; PCRE2's full Perl syntax carries an exponential worst case, `std::regex` is slow and inconsistent, and Hyperscan is complex and x86-only. Its costs — no backreferences, no lookaround — were accepted, and they became a design rule: **a construct belongs inside an island only if a regular-expression engine compiles it; anything else is a whole-pattern type operation**, which combines full matches and needs no regex support (SP17, SP18, SP20). The same rule answers the 2026-01 question of lookahead and lookbehind.

- **Anchored and unanchored forms.** A compiled pattern holds a full-match regex for `is` and `match`; the unanchored search form is derived lazily on first use, because most patterns are only ever tested with `is` (2026-02-28).
- **Compiled once.** Patterns compile where they resolve, at compile time, and live with the script's types — which answers the 2026-01 question of whether patterns should be cached or interned. A pattern RE2 refuses is a compile-time diagnostic, never a silently false match.
- **Literals bypass the regex.** A literal needle and a literal-only pattern take the shared literal scan kernels in `lib/`; a case-insensitive literal compiles as a transient pattern so it folds exactly as RE2 does.
- **Vendored engine.** RE2 is vendored at `lib/re2/` with one patch, a NEON/SSE2 prefix-acceleration kernel used by in-memory search (GRP15, GRP9v2).

## 7. Search beyond strings: files

### 7.1 Principles

The 2026-02-28 grep proposal gave `find` and `replace` a file dimension — built-in grep and sed. Two of its principles stand: **a path type is required**, because a string argument is always content and never a file name; and **file mutation is procedural**.

### 7.2 `io.grep` — built

`io.grep(source, pattern, options?)` exposes `lib/grep` directly and is a procedure: its result depends on the file system, and an `fn` must be deterministic (S12.1.1v2; GRP26). It searches line by line — a match never spans a line terminator — and returns `{value, index}` match maps, `index` counting code points from the start of the file; options add `file`, a 1-based `line`, `byte_offset`, the line `text`, `line_ending`, context lines, or a per-file count. A directory honours ignore files and skips hidden and dependency directories (GRP14). It **does not replace** (GRP32): like `grep` and ripgrep it never changes a file, and changing text is `input()`, `replace()`, `output()`. Ruling text in [Lambda_IO_Shell.md](Lambda_IO_Shell.md), engine in [Lib_Grep.md](lib/Lib_Grep.md); not yet ratified into the formal spec.

### 7.3 File-based `find(path, …)` — designed, not built

`find(path, pattern, options?)` stays an `fn` and runs on `lib/grep`, so it is line-oriented like `io.grep` (GRP27). Its shape stays uniform with string `find`: a single file gives `{value, index}`, a directory or wildcard adds `file`, and `{line: true}` / `{byte_offset: true}` add those fields on request — revising the proposal's original "no extra fields". Its determinism is to come from caching for the evaluation session, which a separate, not yet written proposal will design together with `input()`, `exists()` and `path#`. Blocked on that proposal ([LR09-34](Lambda_Issue_Ledger.md#lr09-34)).

### 7.4 File `replace(path, …)` — proposed, not built

The 2026-02-28 proposal registered a procedural `pn replace(path, pattern, replacement, options?)` beside the pure string `replace`, selected by the first argument's type and returning `null`, with no pure file-replace mode since `input(file) |> replace(~, …)` already gives the text. It was never enabled: its registry key collides with the pure row ([LR09-1](Lambda_Issue_Ledger.md#lr09-1)). Since GRP32 names `input`/`replace`/`output` as the way to change file text, whether the dedicated form is still wanted is unruled.

## 8. Decision ledger

| ID | Decision (current) |
|---|---|
| **SP1** | String patterns are always delimited, `\( … )`; the bare form is retired and `\…\` is rejected (§3.3). |
| **SP2**, **SP3** | *Superseded by SP24* — bare reserved class names (Appendix S, S-2). |
| **SP4** | The island interior keeps quoted literals, whitespace concatenation, `|`, ranges, suffixes and counts (brace counts since S11.1.6v2), grouping and named references; full-match and search semantics are untouched. *Its island `&` and binary `!` are superseded by SP18 and SP20.* |
| **SP5** | Classes are island-only; ranges are not: `X to Y` denotes one set in value, type and pattern position (S11.1.3). |
| **SP6** | General type expressions admit text literals, literal unions and delimited patterns; anything beyond literals and `|` needs a delimiter. |
| **SP7** | A literal union and the literal-only island are the same type. |
| **SP8** | Whitespace concatenation exists only inside an island. |
| **SP9** | (Rev 4, reversed.) `that ( … )` keeps its parentheses (§3.3). |
| **SP10** | Pattern definitions are syntactically self-evident; the `string X =` / `symbol X =` definition forms are retired. |
| **SP11** | Occurrence applies to a whole delimited pattern inside a larger pattern; at the top of a general type expression an island takes no occurrence suffix. |
| **SP12** | Adopted into the formal spec (now S11.1.2v4, S11.1.3). |
| **SP13**–**SP16** | *Superseded by SP24* — symbol-domain spellings and the `\tag( … )` family (Appendix S, S-4). |
| **SP17** | Island `!` complements a single-character set (§3.4). |
| **SP18** | No binary `!` inside a pattern (§3.4). |
| **SP19** | Island tiers: atom, prefix `!`, suffix, concatenation, `|` (§3.4). |
| **SP20** | No `&` inside a pattern; `|` is the only binary operator (§3.4). |
| **SP21** | `replace` follows ECMAScript `replaceAll` and inserts its replacement literally (§4.4). |
| **SP22** | Case-insensitive matching folds by Unicode simple case folding on every path (§4.6). |
| **SP23** | `lower` and `upper` use Unicode full case mapping (§4.6). |
| **SP24** | Quoted class escapes; one domain per island from quotes and references; `\symbol( … )` retired; wildcard-only islands rejected (§3.1, §3.2). |

Decisions on the operations predate the SP series and carry no ID: full match for `is`/`match` (2026-01-27); search semantics, match-map `find` and `split`'s keep-delimiter argument (2026-02-28); `find`/`replace` options (2026-07-01); the path-type requirement and procedural file mutation (2026-02-28).

## 9. Open issues (SPO1–SPO14)

| ID | Question | State |
|---|---|---|
| **SPO1** | Multi-letter and Unicode class names (`\p{Letter}`, `\p{Han}`) | deferred (2026-08-07); the class-token rule is kept additive so the namespace can open later |
| **SPO2** | Symbols in the pattern language | resolved, finally by SP24 |
| **SPO3** | Unparenthesized `that` constraints | closed by the SP9 reversal |
| **SPO4** | Migration strategy | resolved: atomic cutover, no dual-parse window |
| **SPO5** | Constrained-type predicate enforcement | outside this design; SO9 |
| **SPO6** | Meaning of `symbol(P)` on a type value | dissolved; `symbol()` stays a value conversion |
| **SPO7** | Precedence of the type operators in value expressions | **open**; the island half is SP19. Recommendation in §5.5; divergence [LR02-37](Lambda_Issue_Ledger.md#lr02-37) |
| **SPO8** | Prefix `!` against a suffix | resolved by SP19 |
| **SPO9** | What island `!` may negate | resolved by SP17 |
| **SPO10** | What `.`, `...` and `"\s"` match | **open** for `.` and `"\s"` (`...` matches newlines). Recommendation: a dotall `.`, so `...` ≡ `.*` keeps holding and "any single character" is true (JS with the `s` flag); `"\s"` as the ECMAScript set; `"\d"`, `"\w"` and `"\a"` stay ASCII |
| **SPO11** | `replace` semantics | resolved by SP21 |
| **SPO12** | Case-insensitive matching | folding resolved by SP22; **open:** a spelling for case-insensitive `is`/`match`. The earlier recommendation leaned on SP16's `\tag( … )` family, which SP24 superseded |
| **SPO13** | `&` inside an island | resolved by SP20, against its recommendation |
| **SPO14** | Whether an empty literal needle matches at every code-point boundary | **open by the user's decision** (SO47). Today `replace("abc", "", "-")` is `"abc"` and `find("abc", "")` is `[]`, while `split("abc", "")` splits at every boundary and an empty-matching pattern such as `\("\d"*)` replaces there; JS and Python give `"-a-b-c-"`. Option (a), match at every boundary, makes the three functions agree and `\("")` agree with `\("\d"*)`; option (b) keeps matching nowhere, guarding against an unexpectedly empty needle, and must state the exception to S17.6.1 and SP7. The evidence points to (a) |

Other deferred and open items:

| Item | State | Home |
|---|---|---|
| Captures (`@name:` in the 2026-01 proposal), a `capture()` function, capture binding in `match` arms | deferred; gates substitution templates | SO32 |
| Function-valued replacement `replace(s, p, f)` | deferred since 2026-02-28; an error today | — |
| Anchors, word boundaries, lazy quantifiers | design gaps RE2 could support (§5.3) | — |
| Recursive patterns; how detailed a mismatch diagnostic should be | 2026-01 questions never ruled; S11.4.12 now rejects reference cycles | [LR05-17](Lambda_Issue_Ledger.md#lr05-17) |
| A structured `validate(s, p)` result; pattern debugging (regex dump, match trace); pattern-based overloading; symbol patterns constraining a map's keys | kept in view, not built | — |
| A pattern named from another module resolves against the importer | open defect | [LR07-48](Lambda_Issue_Ledger.md#lr07-48) |
| `as` and implicit conversion to a pattern type | follow the general boundary rules (S11.4.1v3); `as` is SO33 | SO33 |

### 9.1 Rejected

| Proposal | Why it lost |
|---|---|
| A `/…/` regex literal | the division ambiguity and an unbalanced delimiter (§2.1) |
| Patterns as opaque runtime strings | no compile-time checking, double escaping |
| The `\…\` delimiter; undelimited patterns | §3.3 |
| Bare reserved class names; the `\symbol( … )` tag; domain-free reuse of named patterns | superseded by SP24 (Appendix S) |
| `&` and binary `!` inside an island; multi-character `!` | regex engines cannot compile them (§3.4) |
| The regex open count `{n,}` | `{n+}` keeps `+` meaning "or more" (S16.8.6v4) |
| `find_at` returning position pairs; a `matches(s, p)` function | match maps carry both; `is` is the spelling |
| RE2 `GlobalReplace` semantics and its backslash rewrite | SP21 |

---

## Appendix S — Superseded designs and rulings

- **S-1. The bare syntax (2026-01-27).** ~~A pattern is defined by `string name = pattern` or `symbol name = pattern`, undelimited, with backslash classes `\d \w \s \a`, `\.` for any character, `...` for any run, and counts `[n]`, `[n, m]`, `[n+]`; `&` intersects inside a pattern and `!` negates any operand through a lookahead.~~ **Replaced** by the island (SP1, 2026-08-07), brace counts (S11.1.6v2, 2026-09-22/23), SP17–SP20 (2026-09-27) and quoted classes (SP24).
- **S-2. SP2, SP3 (2026-08-07).** ~~Inside the delimiter, classes are the bare reserved names `d w s a`, with `.` and `...`; the backslash spellings are retired everywhere; inside an island `d w s a` cannot be referenced as user pattern names.~~ **Replaced by SP24:** quoted class escapes; bare letters are ordinary names. The bare names also forced whitespace between word atoms (`dw` was a name, not `d w`).
- **S-3. SP4's island `&` and binary `!`.** ~~`|` union, `&` intersection and `!` negation apply inside the island.~~ **Replaced by SP17, SP18, SP20.**
- **S-4. The symbol domain, four times.** ~~(2026-01) symbol patterns use symbol literals and ranges (`'a to 'z`); (SP13, rev 2) symbols are banned from the interior and lifted with `symbol(\( … ))`; (SP14–SP16, rev 3) a tagged delimiter `\symbol( … )` carries the domain, named patterns contribute content regardless of domain, and `\tag( … )` is an open family for future variants (case-insensitive, raw regex).~~ **Replaced by SP24.** The rev-3 sweep weighed five spellings: quoting a letter to mean a class (`\('a' 'w'*)`, rejected — quoting means literal everywhere else, meaning would depend on length, and forgetting one quote silently mixed domains); intersection with the base type (`symbol & \( … )`, runner-up — it bent the reading of `&`); the conversion lift (`symbol(\( … ))` — it changed what `symbol()` means on a type value); a keyword prefix (`symbol \( … )` — it reintroduced juxtaposition, with newline-fusion hazards); and the tagged delimiter (adopted). SP24 revived a corrected form of the first option: only the class *escapes* `'\a'` are classes, so `'a'` stays a literal.
- **S-5. The 2026-01 operation set.** ~~`find(s, p)` returns `[string]`; `find_at(s, p)` returns `[(start, end)]`; `replace` takes a string or a function; `match(s, p)` and `capture(s, p)` are functions; `validate(s, p)` returns a detailed result.~~ **Replaced** by match-map `find` (2026-02-28) and `is`; the rest deferred (§9).
- **S-6. No inline patterns (2026-02-28, decision 3).** ~~Patterns must be named; inline `\d+` in an argument would be confused with escapes and arithmetic.~~ **Reversed by SP1:** the delimiter removed the ambiguity.
- **S-7. RE2 replacement and empty matches (2026-02-28).** ~~`replace` uses `RE2::GlobalReplace` (`RE2::Replace` for one match); empty matches follow RE2's default.~~ **Replaced by SP21, S17.1.1 and S17.6.1.**
- **S-8. `match` arms in the C transpiler (2026-02-28).** ~~Pattern arms need a fix in the C-text back end.~~ Fixed then; the back end has since been removed, so the item is moot.
- **S-9. The empty string (2026-01-27).** ~~`""` is null, so a pattern match on it returns an error.~~ **Replaced by S2.2.1:** `""` is a string, and a pattern admitting the empty string matches it.
- **S-10. File `find` without line numbers (2026-02-28).** ~~`find(path, p)` returns `{value, index}` only, with options `{limit, ignore_case}`, searching whole file contents.~~ **Revised 2026-10-04** (GRP4, GRP27): line-oriented on `lib/grep`, opt-in `line` and `byte_offset`.
- **S-11. Case-insensitive spelling (2026-01-27).** ~~A `/i` modifier, or later a `\tag( … )` island (SP16), spells case-insensitivity.~~ SP24 introduces no tagged form; the spelling is open (SPO12), while folding is S17.7.1.
- **S-12. `|` as two operators (2026-01-27).** ~~`|` is exact-value alternation in `type T = …` and regex alternation in `string S = …`.~~ **Replaced by SP7 and S10.1.1v4** (§3.5).
- **S-13. S11.1.2v3 (2026-09-27 to 2026-10-09).** ~~`\( … )` is a string-domain pattern and `\symbol( … )` a symbol-domain one; inside, `d w s a . ...` are reserved atoms; named structural patterns may be reused as content in either domain.~~ **Replaced by S11.1.2v4 (SP24).** Its tier, `!` and `&` clauses carried over unchanged.

## Appendix A — Implementation history

### A.1 Timeline

| Date | Change |
|---|---|
| 2026-01-27/28 | Patterns in the grammar, RE2 compilation, `is` by full match, symbol patterns; a pattern type kind. |
| 2026-02-03/04 | Occurrence counts; string patterns integrated with type patterns. |
| 2026-02-28 | Lazily compiled unanchored regex; pattern-aware `replace` and `split`, `split` keep-delimiters, match-map `find`; `match` pattern arms fixed. Found on the way: a crash replacing with an empty string, a match map storing a raw character pointer, and a thread-local context declared without thread storage. |
| 2026-07-01 | `find`/`replace` options `limit`, `last`, `ignore_case`. |
| 2026-08-07 | SP1–SP16: the island, bare classes, `\symbol( … )`; atomic migration of the corpus and docs; string ranges at run time ([impl record](<impl/Lambda_Impl_String_Pattern (done).md>)). |
| 2026-09-22/23 | Brace counts in islands as everywhere. |
| 2026-09-24 | Literal `replace`/`find` on the shared scan kernel (T28-5.1). |
| 2026-09-27 | Regex-compatibility audit; SP17–SP23 ruled and built (spec 49.0.0 and 50.0.0, both MAJOR); the ten defects of A.2 fixed; the user-doc precedence and quantifier tables corrected. |
| 2026-10-04/05 | `lib/grep`, vendored RE2 with the SIMD prefix patch, Teddy literal sets, `io.grep` with count and line-ending modes (GRP1–GRP32). |
| 2026-10-09 | SP24: quoted classes, one domain per island, `\symbol` retired; syntax harnesses C 411/411, reference grammar 397/397. |
| 2026-10-21 | S11.4.12: module-wide pattern names; reference cycles are E240. |

### A.2 The 2026-09-27 audit defects

All ten contradicted a ruling or user doc already in force, so none needed a new ruling; all were fixed the same day.

| # | Defect | Fix |
|---|---|---|
| 1 | Printing a pattern value crashed on both tiers | a pattern prints as its canonical source |
| 2 | `!` on anything but the four classes compiled to nothing | SP17 |
| 3 | Errors found while building the regex were only logged: `is` answered false, and island `&`, oversized counts, unresolved names and self-reference all failed silently | compile-time diagnostics (E200, E204; cycles are E240 since S11.4.12) |
| 4 | Non-ASCII range bounds broke; multi-code-point bounds were accepted | code-point sets; one-code-point bounds (S11.1.3) |
| 5 | Island counts were not validated (`{2,}`, `{,5}`, `{a}`) | the shared count scanner, plus RE2's bounds |
| 6 | `replace` had two semantics, chosen by whether an option was present | SP21 |
| 7 | `...` stopped at `\n` | matches newlines (S16.8.6v3) |
| 8 | `ignore_case` folded ASCII on the literal path and Unicode on the regex path | SP22 |
| 9 | A pattern could not annotate a `let` binding or a parameter | a pattern contract proves its domain statically and membership at run time |
| 10 | An island could not name a character range type | named ranges and unions of ranges work |

Key files: `lambda/runtime/parse_type_pattern.cpp` (island parsing, domain resolution), `lambda/runtime/re2_wrapper.cpp` (lowering, anchored and unanchored forms, search loops), `lambda/runtime/lambda-eval.cpp` (`find`, `replace`, `split`), `lambda/core/utf_string.cpp` (`utf8_case_map`), `lambda/runtime/io_grep.cpp` and `lib/grep/` (file search), `lib/re2/` (vendored engine).
