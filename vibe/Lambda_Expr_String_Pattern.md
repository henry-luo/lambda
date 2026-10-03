# Lambda String Patterns and Regular Expressions — Compatibility Analysis

> **Status:** analysis, 2026-09-27, with seven rulings, all made by the user on
> 2026-09-27 and implemented the same day. **SP17** (§10) limits island `!` to
> single-character sets; it resolves SPO9 and is formal as S11.1.2v3.
> **SP18** (§11) keeps `!` prefix-only inside an island; exclusion between
> patterns is the type operator, `\(A) ! \(B)` (formal as S10.1.1v3 and
> S11.1.2v3). **SP19** (§12) fixes the island's tiers, tightest first: atom
> (a range is one), prefix `!`, suffix, concatenation, `|`. It resolves SPO8
> and the island half of SPO7. **SP20** (§12) removes `&` from the island,
> leaving `|` as its only binary operator; it resolves SPO13. Both are formal
> in S11.1.2v3 and S10.1.1v3. The conformance defects in §7 need no new
> ruling, because each contradicts a ruling or a user doc already in force.
> The other open questions (SPO7's value-expression tiers, SPO10, SPO12's
> spelling, SPO14, §8)
> each carry a recommendation and await the user's ruling. **SP21** (§13),
> ruled the same day, resolves SPO11: `replace` steps through matches as
> ECMAScript `replaceAll` does and inserts its replacement literally (formal
> as S17.6.1). **SP22** and **SP23** (§15) settle case: case-insensitive
> matching folds by Unicode simple case folding on every path (SPO12's folding
> half), and `lower`/`upper` use Unicode full case mapping, as JS does (formal
> as S17.7.1 and S17.7.2). All ten §7 defects were fixed on 2026-09-27.
> SP17–SP23 and the SPO entries extend the series of
> [Lambda_Design_String_Pattern.md](Lambda_Design_String_Pattern.md) §4.3 and
> §6. The documentation drift listed in §9 was corrected on 2026-09-27.
>
> **Scope:** the pattern islands `\( … )` and `\symbol( … )` measured against
> common regular-expression dialects, ECMAScript `RegExp` first. Covers the
> construct mapping, matching semantics, feature coverage and operator
> precedence, plus the defects found while measuring.
>
> **Formal linkage:** island surface and `!` (§10)
> [S11.1.2v3](../doc/Lambda_Formal_Semantics.md#s111-types-compose-like-values);
> ranges S11.1.3; counts S11.1.6v3 and
> [S16.8.6v3](../doc/Lambda_Formal_Semantics.md#s168-lexical-forms); the type
> operators [S10.1.1v3](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter)
> and S11.1.7; full-match `match` arms
> [S11.2.1](../doc/Lambda_Formal_Semantics.md#s112-match); `split`
> [S17.1.1](../doc/Lambda_Formal_Semantics.md#s171-string-splitting); `find`
> positions [S17.4.1](../doc/Lambda_Formal_Semantics.md#s174-string-search-positions);
> the type-value representation
> [D3.1.1v4](../doc/Lambda_Formal_Design.md#d31-first-class-type-values).
> Design record: [Lambda_Design_String_Pattern.md](Lambda_Design_String_Pattern.md)
> (SP1–SP16, SPO1–SPO6).
>
> The measurements in §1–§9 describe the tree before SP17–SP20 were
> implemented; §6 and §7 mark what they fixed.
>
> **Evidence:** every Lambda claim was measured with the debug `lambda.exe` of
> 2026-09-27 (tree at 3fb512d91; the pattern sources are unchanged since
> 2026-09-26). The probe scripts are in `temp/pattern_probe/` (scratch, not
> committed). The interpreter and the MIR JIT (`LAMBDA_EXEC_BACKEND=jit`) agree on
> every probe. JS results come from Node 22.13.0, Python results from CPython
> 3.14. Reference-grammar results come from `tree-sitter parse` on
> `grammar.js`.

---

## 1. Summary

- **Syntax: not compatible, by design.** Lambda patterns are a readable
  pattern language in the Pomsky lineage: quoted literals, bare class names
  and whitespace concatenation (SP1–SP4), compiled to RE2. A JS regex has to be
  translated, and §3 is the translation table. There is no raw-regex escape
  hatch; SP16 leaves room for one as a future `\tag( … )`.
- **Semantics: the JS `u`-flag subset, with three exceptions.** Within the
  supported subset Lambda matches the way JS does with the `u` flag.
  Alternation is leftmost-first, quantifiers are greedy, `d` and `w` are JS's
  ASCII `\d` and `\w`, and `.` consumes one code point. The exceptions are
  that `s` is ASCII-only, that `.` stops only at `\n`, and that `find` reports
  code-point indices (S17.4.1). A fourth, `...` stopping at `\n`, agrees with
  JS but contradicts Lambda's own ruling (§7 #7).
- **Coverage: the regular core only.** Lambda has no captures,
  backreferences, lookaround, anchors, lazy quantifiers, bracket sets, Unicode
  properties or flags; the one exception is an `ignore_case` option on `find`
  and `replace`. Negation worked only on the four named classes; since SP17
  (§10) `!` complements any single-character set, which covers `[^…]`. `&`
  did not compile inside an island, and since SP20 (§12) it is not an island
  operator.
- **Precedence: regex's core order.** Atoms bind tightest, then quantifiers,
  then concatenation, then alternation, with Lambda-specific atoms (a quoted
  string is one atom). The C parser and `grammar.js` disagreed on the island's
  `|`/`&` tier and on `!d+`; SP19 and SP20 (§12) ruled the island's tiers and
  both front ends now follow them. The C expression parser still puts
  `| & ! to` on one flat tier, which breaks S10.1.1v3 (§6.4).
- **Defects: ten.** One is a crash, four are silent miscompiles or silent
  failures (§7). Two of them, #2 and #4, were fixed with SP17. #9 turned up
  later, while checking the user docs' examples, and #10 while weighing range
  spellings. All ten are fixed as of 2026-09-27.

---

## 2. Position: a pattern language compiled to RE2

Lambda patterns are not a regex dialect. The delimited design
([Lambda_Design_String_Pattern.md](Lambda_Design_String_Pattern.md), rev 1–5)
chose the Pomsky interior on purpose:

- literal text is always quoted (SP4);
- character classes are bare words reserved only inside the island (SP2,
  SP3);
- whitespace is concatenation (SP4, SP8);
- counts use the type families' braces (S11.1.6v3);
- a pattern is a first-class type value (S11.1.2v3, D3.1.1v4), which `is`,
  `match`, `find`, `replace` and `split` all accept.

The host grammar parses the island and lowers it to an RE2 regular expression
(Appendix A).

Two consequences frame the rest of this document:

1. **Compatibility is about semantics, not spelling.** The useful question is
   whether a Lambda construct matches what the equivalent JS construct
   matches. §3 maps the constructs, and §4 lists where the equivalents
   diverge.
2. **RE2 bounds the feature set.** RE2 guarantees linear-time matching, so a
   Lambda pattern can never backtrack catastrophically, which JS cannot
   promise. The price is the non-regular features, backreferences and
   lookaround (§5.1). RE2 also caps a counted repetition at 1000.

---

## 3. Construct mapping

| Lambda (inside `\( … )`) | JS `RegExp` | Notes |
|---|---|---|
| `"abc"` | `abc` | Quoted text is always literal, so metacharacters need no escaping. A quoted string is one atom (§6.2). |
| `d` | `\d` | `[0-9]`, the same as JS |
| `w` | `\w` | `[a-zA-Z0-9_]`, the same as JS |
| `s` | `\s` | **Differs:** `[\t\n\f\r ]` only (§4.1) |
| `a` | `[a-zA-Z]` | No JS shorthand; ASCII only |
| `.` | `.` | **Differs** at `\r`, U+2028 and U+2029 (§4.2). Consumes one code point, as with `u`. |
| `...` | `.*` | Also stops at `\n` (§4.2) |
| `!d`, `!w`, `!s`, `!a` | `\D`, `\W`, `\S`, `[^a-zA-Z]` | One character outside the class. `!s` is the complement of Lambda's narrower `s`. |
| `!("a" \| "b")`, `!">"`, `!("a" to "f")` | `[^ab]`, `[^>]`, `[^a-f]` | The complement of any single-character set (SP17, §10) |
| `"a" to "z"` | `[a-z]` | Any code points since SP17; ASCII bounds only before (§7 #4) |
| `x \| y` | `x\|y` | |
| `x y` | `xy` | Word atoms need whitespace between them: `dw` is a name (§6.2) |
| `( … )` | `(?: … )` | Groups never capture |
| `x?`, `x*`, `x+` | the same | Greedy only |
| `x{n}`, `x{n,m}` | the same | |
| `x{n+}` | `x{n,}` | Lambda's open count (S11.1.6v3) |
| `Name` | none | Reference to a named pattern, inlined; no recursion |
| `\symbol( … )` | none | The same content language in the symbol domain (SP14, SP15) |
| `s is P`, `case P:` | `/^(?: … )$/.test(s)` | Always a full match |
| `find(s, P)` | `[...s.matchAll(/ … /g)]` | Yields `{value, index}`; the index counts code points (S17.4.1) |
| `replace(s, P, r)` | `s.replaceAll(/ … /g, r)` | `r` is literal, with a leak (§4.6) |
| `split(s, P)`, `split(s, P, true)` | `s.split(/ … /)`, `s.split(/( … )/)` | S17.1.1 adopts ECMAScript's rules |

---

## 4. Matching semantics

### 4.1 Character classes

| Probe | Lambda | JS |
|---|---|---|
| `"\u000B"` (vertical tab) is `s` | false | true |
| `"\u00A0"` (no-break space) is `s` | false | true |
| `"\u2003"` (em space) is `s` | false | true |
| `"\u2028"` (line separator) is `s` | false | true |
| `"\uFEFF"` (BOM) is `s` | false | true |
| `"\n"`, `"\t"`, `"\u000C"` is `s` | true | true |
| `"é"` is `w` | false | false |
| `"\u0663"` (Arabic-Indic three) is `d` | false | false |
| `"\n"` is `!d` | true | true (`\D`) |

`d` and `w` agree with JS exactly; JS keeps both ASCII even under `u`.

`s` is RE2's Perl class, `[\t\n\f\r ]`. It misses the vertical tab and every
Unicode space that JS `\s` covers, which is
`[\t\n\v\f\r \u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000\ufeff]`.
RE2 (and Go) are the outliers here: PCRE2, Perl 5.18 and later, Python, Java,
.NET and POSIX `[[:space:]]` all include the vertical tab.

`a` has no JS counterpart. It is ASCII, so it is not "alphabetic" in the
Unicode sense: `"é" is \(a)` is false. Unicode-aware classes belong to the
deferred class namespace (SPO1). The open question is SPO10.

### 4.2 Line terminators: `.` and `...`

| Probe | Lambda | JS |
|---|---|---|
| `"\n"` is `.` | false | false |
| `"\r"` is `.` | true | false |
| `"\u2028"` is `.` | true | false |
| `"a\nb"` is `...` | false | false (`/^.*$/`) |
| `find("a\nb", \(...))` | `a`, empty, `b`, empty | the same |

RE2's `.` excludes only `\n`, which is also how Perl, PCRE, Python and Go read
it. ECMAScript (and Java) exclude all four line terminators: `\n`, `\r`,
U+2028 and U+2029.

`...` compiled to `.*`, so it also stopped at `\n`: `"a\nb" is \(...)` was
false. That agrees with JS `.*`, but it contradicts Lambda's own definition.
S16.8.6v3 makes `...` the elided run, with `...` ≡ `any*`, and the user doc
calls `\(...)` "any string". **Fixed 2026-09-27 (§7 #7):** `...` now lowers to
`(?s:.*)` and matches newlines. `.` is unchanged until SPO10 rules on it.

### 4.3 Unicode: code points, not UTF-16 units

Lambda strings are UTF-8 and RE2 runs in UTF-8 mode, so `.` and `!d` each
consume one code point. `"😀" is \(.)` is true and `"😀" is \(. .)` is false.
JS agrees only under the `u` flag; without it, `.` sees two UTF-16
surrogates.

`find` reports code-point indices (S17.4.1): `find("x😀y", \(.))` gives the
indices 0, 1 and 2, where JS gives 0, 1 and 3. Zero-width advances step one
code point (S17.1.1), as JS does under `u`.

### 4.4 Match discipline

- **Full match.** `is` and `match` arms match the whole string (S11.2.1). The
  compiled regex is anchored `^…$`, and RE2's `$` means end of text (as JS's
  does without `m`), so a trailing newline gets no slack.
- **Search.** `find`, `replace` and `split` search the string. RE2 runs
  leftmost-first with greedy quantifiers, which is the Perl and JS semantics,
  not POSIX leftmost-longest: `find("abc", \("a" | "ab"))` yields `"a"`, as JS
  does.
- **Zero-width matches.** `find("abc", \(d*))` yields four empty matches at
  indices 0 to 3. `find("aab", \("a"*))` yields `"aa"` at 0 and empty matches
  at 2 and 3. Both are identical to `matchAll`.
- **Split.** `split` follows ECMAScript by ruling (S17.1.1).
  `split("aab", \("a"*))` is `["", "b"]`, and with delimiters kept it is
  `["", "aa", "b"]`, matching JS `split(/a*/)` and `split(/(a*)/)`.
- **Replace is the exception.** Its no-options path drops an empty match that
  touches the end of the previous match (RE2 `GlobalReplace`, which is Go's
  rule): `replace("aab", \("a"*), "-")` is `"-b-"`. JS, Python and the same
  call with a `limit`, `last` or `ignore_case` option all give `"--b-"`
  (§7 #6, SPO11).

### 4.5 Case-insensitive matching

Only `find` and `replace` take `{ignore_case: true}`. `is`, `match` and
`split` have no case-insensitive form, and there is no inline flag.

The folding also depends on the path:

- Through RE2 the folding is Unicode:
  `find("É1", \("é" d), {ignore_case: true})` matches.
- A literal string goes through the literal search, which folds ASCII only.
  So does a literal-only island, which S11.1.2v3 makes identical to a literal
  union. `find("É", "é", {ignore_case: true})` and
  `find("É", \("é"), {ignore_case: true})` are both `[]`, while JS `/é/i`
  matches.

See §7 #8 and SPO12. **Fixed 2026-09-27 (SP22, §15):** a literal needle now
folds through RE2 as a pattern does, so both calls above match.

### 4.6 Replacement text

With no capture groups there are no substitution templates. JS's `$&`, `$1`
and `$<name>` stay literal in Lambda: `replace("abc", \("b"), "[$&]")` is
`"a[$&]c"`, where JS gives `"a[b]c"`.

The plain `replace` path, however, handed the replacement to RE2's rewriter.
It was taken whenever no `limit`, `last` or `ignore_case` option was set. The
rewriter reads `\0`–`\9` as submatch references and `\\` as one backslash, and
it silently drops any other backslash sequence. The options path appends the
replacement literally. **SP21 (§13) made the options path the only one:** every
row below now reads as the options column.

| Call (replacement text as characters) | Plain path | Options path | JS |
|---|---|---|---|
| `replace("a1b", \(d), r)`, `r` = `<\0\0>` | `a<11>b` | `a<\0\0>b` | `a<\0\0>b` |
| `replace("a1b", \(d), r)`, `r` = `C:\x` | `aC:b` | `aC:\xb` | `aC:\xb` |
| `replace("a1b", \(d), r)`, `r` = `\\` | `a\b` | `a\\b` | `a\\b` |

See §7 #6 and SPO11.

---

## 5. Feature coverage

### 5.1 JS features Lambda lacks

| JS feature | In Lambda | Why, or where it stands |
|---|---|---|
| Capture groups `( )`, named groups `(?<n> )` | None: groups never capture, and `find` yields only `{value, index}` | A `capture()` function was proposed in [Lambda_Type_String_Pattern.md](Lambda_Type_String_Pattern.md) §4.1; capture binding in `match` arms is SO32 |
| Backreferences `\1`, `\k<n>` | None | Not regular, so RE2 cannot |
| Lookaround `(?= )`, `(?! )`, `(?<= )`, `(?<! )` | None | RE2 cannot, which is also why island `&` failed (§7 #3) and was removed (SP20, §12) |
| Anchors `^` and `$`, word boundaries `\b` and `\B` | None (E103) | `is` and `match` are always anchored, and a search cannot be anchored. RE2 supports all four, so this is a design gap, not an engine limit. |
| Lazy quantifiers `*?`, `+?`, `??`, `{n,m}?` | None (E103) | RE2 supports them. They don't matter for a full match, but they do for `find` and `replace`: `<.*?>` has no spelling. |
| Bracket sets `[abc]`, `[^abc]`, `[a-z0-9_]` | Unions of literals and ranges (`"a" \| "b"`, `"a" to "z" \| d`); since SP17, `!` complements any such set, so `[^>]` is `!">"` | Before SP17 only `d`, `w`, `s`, `a` could be complemented (§7 #2) |
| `v`-flag set algebra (`[\p{L}--[a-z]]`, `&&`) | None | No `&` inside an island (SP20, §12): spell a set difference or intersection out as a union, or combine whole patterns with `&` and `!` |
| Unicode properties `\p{L}`, `\p{Script=Greek}` | None | The class namespace is deferred (SPO1) |
| Escapes `\t`, `\n`, `\uXXXX`, `\u{…}`, `\xHH` | Written inside quoted literals (`"\t"`, `"\u00A0"`) | Equivalent |
| Flag `i` | The `ignore_case` option on `find` and `replace` only | SPO12; SP16 anticipates a `\tag( … )` for this |
| Flags `m`, `s`, `y`, `g`, `d` | None; `find` and `replace` are always global | `s` matters if `.` stays newline-blind (SPO10) |
| Counted repetition above 1000 | RE2's limit: `d{1001}` fails to compile, silently (§7 #3) | JS has no practical limit |
| Replacement templates `$&`, `$1` | Literal, apart from the RE2 rewrite leak (§4.6) | SPO11 |

### 5.2 Lambda features JS lacks

- **Named, composable sub-patterns.** `type HexDigit = \("0" to "9" | "a" to "f")`
  and then `\("#" HexDigit{6})`. A reference inlines the definition, like
  Pomsky's `let`, and contributes content only in either domain (SP15).
  Self-reference is not supported (§7 #3).
- **A domain tag.** `\symbol( … )` matches symbols and never strings
  (S11.1.2v3).
- **Patterns are type values.** A pattern can be an annotation, a `match` arm,
  a union arm or a constrained base (the S11.4.6 conformance row, LR03-26). It
  also composes with the type operators *between* islands, through the type
  algebra rather than the regex: `"abc" is (\(a+) & \(w+))` is true and
  `"12" is (\(w+) ! \(d+))` is false today.
- **One denotation for ranges.** `"a" to "z"` means the same set in value, type
  and pattern position (SP5, S11.1.3), with the §7 #4 caveat.
- **Linear-time matching** (RE2).

---

## 6. Operator precedence

### 6.1 The tiers side by side

| Tier (tightest first) | JS regex | Lambda island, C parser | Lambda island, `grammar.js` |
|---|---|---|---|
| Atom | character, escape, class, `[…]`, `(…)` | quoted string, class word, `.`, `...`, `"a" to "z"`, `( … )`, name | the same |
| Prefix | none | `!` on an atom or group | `!` on an atom; `!( … )` as a group form |
| Postfix | `? * + {n,m}`, each optionally lazy | exactly one of `? * + {n} {n,m} {n+}`, touching its operand | the same, but not after `!` (§6.3) |
| Concatenation | juxtaposition | whitespace juxtaposition | the same |
| Binary | `\|` | `\|` and `&` on one tier, left-associative | `&` above `!` above `\|` (the type-operator tiers) |

**Ruled 2026-09-27 (SP19, SP20, §12).** The table is the measured state
before the rulings. The island's tiers are now, tightest first: atom (a range
is one), prefix `!`, suffix, concatenation, `|`, and `|` is its only binary
operator. Both front ends follow them: `grammar.js` takes `!` under a suffix,
and neither accepts an island `&`.

The core order agrees with every mainstream dialect: quantifier binds tighter
than concatenation, which binds tighter than alternation.
`\("a" | "b" "c")` matches `a` and `bc` but not `ac`, and `\(d | "a"+)` matches
`aaa`.

### 6.2 Binding rules that differ from regex

- **A quoted string is one atom,** so a suffix repeats all of it:
  `\("ab"+)` matches `abab` but not `abb`. JS `ab+` does the reverse. Pomsky
  reads it the same way Lambda does.
- **Prefix `!` binds before a suffix:** `\(!d+)` is `(!d)+`, which is JS
  `\D+`. Regex never has to rank these, because it has no negation operator:
  negation is spelled inside the atom (`\D`, `\W`, `\S`, `[^0-9]`), and a
  quantifier always repeats the atom before it. Lambda makes negation a
  separate prefix operator, so `!d+` has two readings. `(!d)+` is one or more
  non-digit characters. `!(d+)` is the complement, any string that is not a
  run of digits; regex needs a lookahead for that (`^(?!\d+$)`), which RE2
  lacks. The C parser applies the prefix first, which reproduces the regex
  reading. It is the reverse of the usual programming-language convention,
  where postfix binds tighter (`-x++` is `-(x++)`). `grammar.js` rejected
  `\(!d+)` and required the group `\((!d)+)` until SP19 (§12) made the C
  parser's reading the ruling.
- **`to` binds inside the atom:** `\("a" to "c"+)` is `[a-c]+`.
- **A suffix must touch its operand, and only one is allowed:** `d +`, `d+?`
  and `d{2}+` are all E103.
- **Word atoms need whitespace between them.** `dw` is a reference to a
  pattern named `dw`, not `d w`; it is unresolved and silently false (§7 #3).
  Punctuation atoms don't need spaces: `\(d"-"d)` and `\(d.)` parse.
- **Infix `!` is not exclusion inside an island.** Both front ends read
  `\(w+ ! d)` as `w+` followed by `!d`, so `"ab1"` fails. A type reads
  `A ! B` as exclusion (S10.1.1v3), so binary exclusion has no island
  spelling. SP18 (§11) makes this the ruling: exclusion is written between
  whole patterns, `\(A) ! \(B)`.

### 6.3 Where the front ends disagree

By Doc_Convention §2, `grammar.js` outranks the C parser, and a divergence is
an implementation bug unless a ruling says otherwise. Measured results:

| Input | C parser (`lambda.exe`) | `grammar.js` (`tree-sitter parse`) |
|---|---|---|
| `\("a" \| "b" & "c")` | `("a" \| "b") & "c"` | `"a" \| ("b" & "c")` |
| `\(!d+)` | `(!d)+` | ERROR; the group form `\((!d)+)` is needed |
| `\(d{2,})` | accepted as regex's open count | ERROR (S11.1.6v3) |
| `\(d{,5})`, `\(d{a})` | accepted, as the literal text `{,5}` and `{a}` | ERROR |
| `\(d[3])`, `\(d[])` | E103 (a retired spelling) | accepted, because the island's `occurrence` admits `array_count` |
| `let x = "a" \| "b" & "c"` | `("a" \| "b") & "c"` | `"a" \| ("b" & "c")` |
| `let r = 5 \| 1 to 3` | `(5 \| 1) to 3`, a runtime error | `5 \| (1 to 3)` |

The first two rows are settled since 2026-09-27: both front ends reject an
island `&` (SP20) and read `\(!d+)` as `(!d)+` (SP19). The count rows are
§7 #5; the value-expression rows remain open (SPO7, §6.4).

Other dialects also disagree on `{,m}`. Python and Perl 5.34 or later read it
as `{0,m}`, JS without `u` and RE2 read literal text, and JS with `u` rejects
it. That spread is why the island must validate its own counts rather than
pass them through (§7 #5).

### 6.4 The same operators in value expressions

S10.1.1v3 makes `|`, `&` and `!` the type operators "everywhere: type
expressions, match or-patterns, string patterns and value expressions
alike". It also promises that a type operation has "the same meaning wherever
it is written".

The C type-pattern parser and `grammar.js` rank them `to` above `&` above `!`
above `|`. The C expression parser instead gives all four one binding power
(`LAMBDA_BP_SET`), left-associative. So one spelling denotes two different
types:

```lambda
type T = "a" | "b" & "c"     // "a" | ("b" & "c"), which is "a"
let x = "a" | "b" & "c"      // ("a" | "b") & "c", which is none
("a" is T)                   // true
("a" is x)                   // false
```

`let r = 5 | 1 to 3` also fails at run time ("range bounds must be exact
integers or single-codepoint strings"), while `type R = 5 | 1 to 3` admits 5
and 1 to 3. No S-point fixes these tiers; the formal spec is silent (SPO7).

### 6.5 The user-doc precedence tables

Before 2026-09-27, the operator-precedence tables in `doc/Lambda_Expr_Stam.md`
and `doc/Lambda_Cheatsheet.md` described neither parser:

- They left out binary `|`, `&` and `!`.
- They ranked `and` and `or` above `to` and `is`/`in`. Both front ends bind
  the other way: `(1 is int and 2 is int)` is `true` and `(false or 1 to 3)`
  is `[1, 2, 3]`.
- They still listed `not` and a value-position unary `!` in the tight unary
  tier, against S16.8.1 and S16.8.2.

Both tables now follow S16.8.1, S16.8.2 and the `grammar.js` tiers (§9). The
C expression parser's single tier for `| & ! to` remains a bug (§6.4).

---

## 7. Conformance defects found by the audit

None of these needs a new ruling; each contradicts a ruling or a user doc
already in force. They are recorded here as the audit's evidence. Status
tracking belongs to the central ledger
([Lambda_Issue_Ledger.md](Lambda_Issue_Ledger.md), Doc_Convention §6), where
they are **not yet filed** as of 2026-09-27. Code anchors are in Appendix A.

| # | Defect | Measured | Required by |
|---|---|---|---|
| 1 | **Fixed 2026-09-27.** **Crash.** Printing a pattern value segfaults, in both tiers. | `"x"; \(d+)`, `[P]` and `\symbol(a+)` each exit with status 139 | D3.1.1v4: a pattern is a `Type*` under `LMD_TYPE_TYPE`. The printer assumes every such value is a `TypeType` wrapper. |
| 2 | **Fixed 2026-09-27 (SP17, §10).** **Silent miscompile.** `!` on anything except `d`, `w`, `s`, `a` compiles to nothing. | `"1" is \("1" !("a" to "z"))` is true and `"11"` is false; `"" is \(!"x")` is true; `"x" is \(!(d))` is false; `find("<a><b>", \("<" (!">")* ">"))` is `[]` | S11.1.2v2 applies negation in islands, and `grammar.js` admits these operands (`char_negation_type`) |
| 3 | **Fixed 2026-09-27.** **Silent failure.** Errors found while building the regex are only logged: `is` answers false, `find` returns an error value, and the exit status is 0. | RE2 rejections: `d{5,2}` and `d{1001}`; every island `&` also failed this way ("invalid perl operator: (?=") until SP20 made it a parse error. Unresolved names: `\(dw)`, `\(N)` with `type N = int`, and self-reference. | A malformed pattern must be a compile-time diagnostic, as parse-stage errors already are (E103) |
| 4 | **Fixed 2026-09-27 (§10.2).** Non-ASCII range bounds break, and multi-code-point bounds are accepted. | `"β" is \("α" to "ω")` is false (RE2: "invalid UTF-8"), while `"β" is ("α" to "ω")` is true. `\("ab" to "z")` is accepted as `[a-z]`. | S11.1.3; SP5's single denotation |
| 5 | **Fixed 2026-09-27.** Island counts are not validated. | `\(d{2,})` is accepted, where type position gives E103. `"1{,5}" is \(d{,5})` and `"1{a}" is \(d{a})` are both true. | S11.1.6v3 and S16.8.6v3. Its Appendix A row lists the islands among the conforming front ends. |
| 6 | **Fixed 2026-09-27 (SP21, §13).** `replace` has two semantics, chosen by whether a `limit`, `last` or `ignore_case` option is set. | The plain call uses RE2 `GlobalReplace`: `"aab"` with `"a"*` gives `"-b-"`, `\0` is substituted and `\x` is dropped (§4.6). With an option it gives `"--b-"` and treats the replacement literally. | One call, one meaning. Plain `replace` also skips the empty match at 2 that `find` reports. SPO11 decides the target semantics. |
| 7 | **Fixed 2026-09-27.** `...` stops at `\n`. | `"a\nb" is \(...)` is false | S16.8.6v3 (`...` ≡ `any*`) and the user doc's "any string". SPO10 decides `.`. |
| 8 | **Fixed 2026-09-27 (SP22, §15).** `ignore_case` folds only ASCII on the literal path but full Unicode on the regex path. | `find("É", \("é"), {ignore_case: true})` is `[]`, while `find("É1", \("é" d), …)` matches | S11.1.2v3: a literal-only island is the literal union, so its meaning must not depend on the path. SPO12 decides the folding rule. |
| 9 | **Found and fixed 2026-09-27, while checking doc examples.** A pattern cannot annotate a `let` binding or a parameter: the static boundary reads it as a type value. | `let code: \(a{3}) = "abc"`, and `let x: A = "abc"` with `type A = \(a+)`, give E201 ("cannot initialize … of type type with string"). `fn f(x: \(d+))` called with `"12"` gives E207 ("argument 1 expected type, got string"). The parameter form worked on a 2026-09-12 build (451b17f4c) and fails on 2026-09-22 (fc3153b3b); the `let` form fails on both. | S11.1.2v3: a pattern is a type value, usable wherever a type is. `doc/Lambda_Type.md` §Inline Patterns and §Using Patterns as Types show both forms. |
| 10 | **Found and fixed 2026-09-27.** An island cannot name a character range type. | With `type Lower = "a" to "z"`, `\(Lower+)` logs "unresolved pattern reference" and matches only the empty string, and `\(!Lower)` is E200; `"q" is Lower` is true. Named literal unions worked (§10.2). | SP5 and S11.1.3: `X to Y` denotes one set in type position and in an island. S11.1.2v3 lets an island reuse named patterns. |

---

## 8. Open questions (SPO7–SPO14)

**SPO7 — Precedence of the type operators.** *Island half RESOLVED
2026-09-27 → SP19 and SP20 (§12); the value-expression tiers stay open.*
What are the tiers of `|`, `&`,
binary `!` and `to`, and do they hold in type position, value expressions and
islands alike? The evidence is in §6.3 and §6.4. **Recommendation:** make the
`grammar.js` order, `to` above `&` above `!` above `|`, a clause of S10.1.1v3.
It is already the type-position order in both front ends. Then align the C
expression parser (split `LAMBDA_BP_SET`) with it; the island has no `&`
since SP20, so its body needs no change. The
value-expression side is the smaller migration, because type declarations
already use these tiers.

**SPO8 — RESOLVED 2026-09-27 → SP19 (§12).** Prefix `!` against a
suffix. Does island `!` bind tighter than a
suffix, so that `\(!d+)` means `(!d)+`? Regex never faces the question,
because its negation lives inside the atom (§6.2). The C parser says yes;
`grammar.js` rejects the form. **Recommendation:** yes. `!d+` ≡ `(!d)+` is the
reading regex users bring from `\D+`, and the other reading, `!(d+)`, is a
string complement that SP17 rules out anyway. Let `grammar.js`'s
`char_occurrence_type` take a negated operand. Adopted as recommended.

**SPO9 — RESOLVED 2026-09-27 → SP17 (§10).** What may island `!` negate,
and what does it mean? The user doc said island `!` "negates character
classes", meaning one character outside the class. That is not the
type-level `!T` ≡ `any ! T` (`doc/Lambda_Type.md` §Negation), and S11.1.2v2
said only that "negation … rules apply". The recommendation was adopted as
recorded: island `!` is the complement of a single-character set.

**SPO10 — What `.`, `...` and `s` match.** The options are (a) ECMAScript,
where `.` excludes the four line terminators and `s` is JS `\s`, or (b)
"any", where `.` is any code point and `...` any string. **Recommendation:**

- `...` should match any string including newlines. S16.8.6v3 already
  requires this (§7 #7).
- `.` should match any single code point (dotall), so that `...` ≡ `.*` keeps
  holding and the user doc's "any single character" is true. That departs
  from JS's default `.`, but it equals JS with the `s` flag.
- `s` should be the ECMAScript `\s` set (WhiteSpace plus LineTerminator), so
  whitespace means what users expect.
- `d` and `w` stay ASCII, as in JS. `a` stays ASCII too; Unicode letters wait
  for SPO1's class namespace.

**SPO11 — RESOLVED 2026-09-27 → SP21 (§13).** `replace` semantics. What
does `replace` do with the replacement
text and with empty matches? **Recommendation:** follow ECMAScript
`String.prototype.replaceAll` for empty matches, as S17.1.1 already does for
`split` and as `find` already behaves. Treat the replacement as literal text
until captures exist (SO32), and never use RE2's backslash rewrite, which
collides with Lambda's own string escapes (`"\\0"`). In the implementation
that means routing the plain call through the literal loop the options path
already uses (§7 #6).

**SPO12 — Case-insensitive matching.** *Folding rule RESOLVED 2026-09-27 →
SP22 (§15); the spelling for a case-insensitive `is`/`match` stays open.*
There should be one folding rule for
every path, and a spelling for case-insensitive `is` and `match`.
**Recommendation:** Unicode simple case folding everywhere, which is what RE2
and JS with `u` do; this closes §7 #8. SP16 reserves the `\tag( … )` family
for a case-insensitive island; choose the tag word when a use case asks for
it.

**SPO13 — RESOLVED 2026-09-27 → SP20 (§12), not as recommended.** `&`
inside an island. S10.1.1v3 put `&` in string patterns, but
RE2 cannot intersect, so the compiler's lookahead is rejected (§7 #3). At the
top level, an island intersection is the same as intersecting two islands,
which already works through the type algebra
(`"abc" is (\(a+) & \(w+))` is true). **Recommendation:** admit `&` inside an
island only when both operands are single-code-point sets, and compile it to a
class intersection, like the `v` flag's `&&`. Everywhere else, reject it with
a diagnostic that points to `\(A) & \(B)`. The ruling went further: no `&`
inside an island at all, so the diagnostic covers every island `&`.

**SPO14 — An empty literal needle.** Opened 2026-09-27 with SP21 (USER: keep
it open); formal SO47. Does an empty literal needle match at every code-point
boundary? Today it matches nowhere in `replace` and `find`, but everywhere in
`split`:

| Call | Lambda | JS | Python |
|---|---|---|---|
| `replace("abc", "", "-")` | `"abc"` | `"-a-b-c-"` (`replaceAll`) | `"-a-b-c-"` |
| `replace("abc", "", "-", {limit: 1})` | `"abc"` | `"-abc"` (`replace`) | `"-abc"` (count 1) |
| `find("abc", "")` | `[]` | indices 0, 1, 2, 3 (`matchAll`) | n/a |
| `split("abc", "")` | `["a", "b", "c"]` (S17.1.1) | the same | error |

Two things tie it to the patterns. `\("")` is the value `""` (SP7), so it takes
the literal path and replaces nothing, while `\(d*)`, which also matches the
empty string everywhere, gives `"-a-b-c-"` under S17.6.1. And S17.6.1 promises
that `find` and `replace` see the same matches, which holds for both readings.
**The options:** (a) match at every boundary, as JS and Python do, which makes
`replace`, `find` and `split` agree and `\("")` agree with `\(d*)`; or (b)
keep matching nowhere, which guards against a needle that is unexpectedly
`""`, and state the exception to S17.6.1 and SP7 explicitly. Open by the
user's decision; the evidence points to (a).

---

## 9. Documentation drift (fixed 2026-09-27)

The audit found these docs stale. All were corrected on 2026-09-27.

- **Quantifier spelling.** `doc/Lambda_Type.md` §Occurrence Modifiers listed
  the retired `[n]`, `[n+]` and `[n, m]`. It now gives `{n}`, `{n+}` and
  `{n,m}`, says the open count is not regex's `{n,}`, and notes that a quoted
  string is one repeated element. `doc/Lambda_Cheatsheet.md` §String Patterns
  had the same spelling in its quantifier line and got the same fix.
- **Pattern-composition note.** The "Not yet supported" note in
  `doc/Lambda_Type.md` §Pattern Composition said pattern intersection "does
  not compile". In fact it compiles with no diagnostic and answers false
  (§7 #3). It also said the binary `&` and `!` type operators were
  unimplemented, which has been false since S10.1.1v2 landed on 2026-09-26.
  The note now says three things:
  - intersection *inside* an island is not yet supported;
  - `&` and `!` between whole islands work;
  - island `!` negates only `d`, `w`, `s` and `a` for now.
- **Precedence tables.** The tables in `doc/Lambda_Expr_Stam.md` and
  `doc/Lambda_Cheatsheet.md` (§6.5) now follow S16.8.1, S16.8.2 and the
  `grammar.js` tiers, with the type operators, `at` and `<:` added.
- **Formal spec, Appendix A** (spec 48.0.3, a PATCH):
  - The row "S11.1.1v3, S11.1.2v2, S11.1.6v3, S16.8.6v3" listed the islands
    among the conforming front ends. It now records the island residue (§7 #2,
    #5, #7).
  - The S10.1.1v2 row now records the residue for expression precedence and
    island `&` (§6.4, §7 #3).
  - In-code `|` characters in the S10.1.1v2 and S11.1.7 rows are now escaped.
    GFM had been dropping everything after the first bare pipe, which would
    have hidden the new residue.
- **Validator design doc.** `doc/dev/lambda/LR_13_Schema_Validator.md` listed
  the retired `[n]` count among the validator's occurrence operators.

**Deliberately unchanged:** `doc/Lambda_Type.md` §Character Classes still
reads "`.` any single character", "`...` any characters" and "`s`
whitespace". A user doc states the ruling, and implementation behavior never
earns a documented exception (Doc_Convention §2):

- For `...`, the text matches S16.8.6v3; the implementation is what is wrong
  (§7 #7).
- For `.` and `s`, no ruling exists yet (SPO10).

---

## 10. SP17: `!` negates single-character sets (ruled 2026-09-27)

**Ruling (USER, 2026-09-27; formal text S11.1.2v3).** Inside a pattern
island, prefix `!` complements a *single-character set*: it matches exactly
one character that is not in the set. A single-character set is:

- a named class: `d`, `w`, `s`, `a` or `.`;
- a range, such as `"a" to "z"`;
- a one-character string, such as `"x"`;
- a union, group, negation or named pattern built only from these.

So `\(!("a" | "b" | "c"))` is regex `[^abc]`, `\(!">")` is `[^>]`, and
`\(!(d | "-"))` is `[^0-9-]`. Any other operand is a compile error (E200),
for example `!"ab"`, `!(d+)`, `!(d d)` and `!...`. To exclude a whole
pattern, use the type operators outside the island: `x is !\(d+)`, or
`\(a w*) ! ("if" | "else")`.

This resolves SPO9 and fixes §7 #2.

### 10.1 Why `!` stops at one character

1. **It is the negation regular expressions have, so an existing regex engine
   compiles it.** Every mainstream dialect negates exactly one character:
   `[^abc]`, `\D`, `\W`, `\S`. Keeping island `!` within that family lets
   Lambda go on compiling patterns to RE2, or to any other standard engine,
   without a matcher of its own. A negation lowers to one bracket class.
2. **Complementing a longer pattern needs a deterministic automaton.**
   Engines match either by simulating a nondeterministic automaton (RE2, Go,
   Rust) or by backtracking (JS, PCRE, Python, Java), and neither can
   complement. The automaton must first be made deterministic, which can grow
   it exponentially, and each nested `!` can grow it exponentially again. The
   tools that do offer full complement are built on deterministic automata
   or derivatives for that reason: Ragel, dk.brics.automaton and Lucene, RE#.
3. **It does not fit backtracking either.** A backtracking engine searches
   for one way to match; "no way matches" means exhausting every way at every
   length. Those engines offer negative lookahead `(?!P)` instead, a
   zero-width test. RE2 drops even that, to guarantee linear-time matching.
4. **The natural reading is usually wrong.** "Not `abc`" usually means "does
   not contain `abc`", but the complement of `abc` is every string except
   exactly `abc`, `xabcx` included. In a search (`find`, `replace`, `split`),
   a bare complement also matches the empty string at almost every position.
5. **Captures and first-match priority mean nothing under complement.** Regex
   search rests on both: which alternative wins, and what a group captured. A
   complement is a pure set operation with neither, so it would not compose
   with captures when they arrive (SO32).
6. **The whole-string cases already work.** For `is` and `match` the whole
   string must match, so complementing or intersecting whole patterns is a
   yes/no combination that the type operators already perform, with no
   automaton: `x is !\(d+)`, `\(a w*) ! ("if" | "else" | "for")`,
   `\(a+) & \(w+)`.
7. **One character covers the everyday need.** The common task is "any
   character except …", as in `"<" (!">")* ">"` (regex `<[^>]*>`), and the
   single-character form spells it. What stays out is "a run that does not
   contain X", the C-comment case. Admitting it would mean replacing RE2 with
   a matcher that can complement (§5.1).

### 10.2 Consequences

- A set is built as code-point intervals, so union and nested `!` compose
  (`!(!d)` is `d`) and sets beyond ASCII work, as in `!("α" to "ω")`.
- The same code now lowers every range. A range's bounds are code points
  rather than their first byte, and a bound of more than one character is an
  error (E200, S11.1.3). This fixes §7 #4.
- An island can now name a literal union. With `type Vowel = "a" | "e"`, both
  `\(Vowel+)` and `\(!Vowel)` work. That reference used to fail silently,
  although S11.1.2v3 makes a literal-only island identical to the literal
  union. A character range type followed the same day (§7 #10): with
  `type Lower = "a" to "z"`, `\(Lower+)` and `\(!Lower)` work, and so do unions
  of ranges and literals. The fix is in the one helper that renders a literal
  type as a pattern (`append_literal_type`), so `find`, `split` and `replace`
  also take `Lower` as a pattern, as they already took a literal union. An
  integer range is not a character set, so `\(!Digits)` with
  `type Digits = 1 to 9` stays E200.
- A symbol literal used as a range bound is now caught by the content-only
  diagnostic (E103), like any other symbol literal in an island.
- Island `!` and type-level `!` stay different operators. The first
  complements a set of characters; the second is the set complement in the
  type lattice (S11.1.7).
- Fixtures: `test/lambda/pattern_negation.ls` (the interpreter, the JIT and
  the auto tier give identical output), and the negative fixtures
  `string_pattern_negation_non_set.ls` and `string_pattern_range_bound.ls`
  under `test/lambda/negative/semantic/`.

---

## 11. SP18: no binary `!` inside a pattern (ruled 2026-09-27)

**Ruling (USER, 2026-09-27; formal text S10.1.1v3 and S11.1.2v3).** Inside a
string or symbol pattern, `!` is prefix-only, and the SP17 rule governs it.
There is no binary `!` there: `\(w ! d)` is a word character followed by a
non-digit. Excluding one pattern from another is the type operator between
whole patterns, `\(A) ! \(B)`, and it already works:
`"ab" is (\(w+) ! \(d+))` is true and `"12"` is false.

### 11.1 What regular expressions offer

Regex has binary exclusion only between character sets, and only in some
dialects. No mainstream dialect can exclude one multi-character pattern from
another.

| Dialect | Character-set difference |
|---|---|
| JS, `v` flag (ES2024) | `[\w--\d]` |
| .NET, XML Schema | `[a-z-[aeiou]]` |
| Java | `[a-z&&[^aeiou]]` |
| Perl 5.18+ (experimental) | `(?[ [a-z] - [aeiou] ])` |
| RE2, Go, Python `re` | none |

To express "matches A but not B" for longer patterns, regex users fall back
on a negative lookahead, `^(?!\d+$)\w+$`, which RE2 does not support.

### 11.2 Why there is no binary `!` inside a pattern

1. **It collides with concatenation.** Inside an island whitespace joins
   atoms and `!` is a prefix, so `w ! d` already means two characters, `w`
   then `!d`. A binary reading would silently change working patterns, or
   make the meaning depend on spacing, which the delimited design removed
   (SP8).
2. **Between longer patterns it is pattern difference, which SP17 rules
   out.** `A ! B` is `A` intersected with the complement of `B`, and
   complementing a multi-character pattern needs a deterministic automaton
   or a matcher of its own (§10.1). RE2 cannot.
3. **The whole-pattern case already works.** For `is` and `match` the whole
   string must match, so `\(A) ! \(B)` is a yes/no combination of two full
   matches, done by the type operators with no regex support.
4. **The regex feature it would mirror is narrow.** Only character-set
   difference exists in regex, and not in RE2. A set difference is spelled
   out as a union of what remains; SP20 (§12) later ruled out an island `&`,
   so `A & !B` is not a spelling either.

### 11.3 Consequences

- S10.1.1v3 qualifies "everywhere … string patterns": inside a pattern `|` is
  alternation and `&` intersection, but `!` is prefix-only. (SP20 removed the
  island `&` the same day, §12.) The spec is
  49.0.0, a MAJOR revision, because an existing ruling changed meaning
  (Doc_Convention §3).
- The C parser already read `!` this way, so it did not change. The
  reference grammar did: `grammar.js` no longer offers `!` among the island's
  binary operators (`char_binary_type`). The S16 harnesses pass on both
  front ends (reference grammar 358/358, C parser 372/372).
- `test/lambda/pattern_negation.ls` §7 pins both the concatenation reading
  and the type-level exclusion.

---

## 12. SP19 and SP20: the island's operator tiers (ruled 2026-09-27)

**Rulings (USER, 2026-09-27; formal text S11.1.2v3 and S10.1.1v3).**

- **SP19.** Inside a string or symbol pattern the tiers are, tightest first:
  atom, where a range `"a" to "z"` is one atom; prefix `!`; suffix;
  concatenation; alternation `|`.
- **SP20.** `&` is removed from string and symbol patterns, and `|` is the
  island's only binary operator. Intersecting whole patterns stays the type
  operator, `\(A) & \(B)`.

Together with SP18, an island's operators are regex's, plus prefix `!`.

### 12.1 Why these tiers

1. **They are regex's order wherever the two share a tier.** In every
   mainstream dialect a suffix binds before concatenation, which binds before
   alternation. So `\("a" "b"+)` is `ab+` and `\("a" | "b" "c")` is `a|bc`.
2. **A range is one atom, as in regex.** `"a" to "z"` is the class `[a-z]`,
   one atom. So `\("a" to "c"+)` is `[a-c]+` and `\(!"a" to "z")` is
   `[^a-z]`. Both front ends already parsed it this way, and it matches `to`
   in value and type position (S11.1.3).
3. **Prefix before suffix reproduces regex's negated classes.** Regex spells
   negation inside the atom (`\D`, `[^>]`), so a quantifier always repeats
   the negated class: `\D+`, `[^>]*`. Binding `!` first gives `!d+` that
   reading, `(!d)+`. The other reading, `!(d+)`, complements a
   multi-character pattern, which SP17 rejects, so no legal program loses a
   meaning. It is the reverse of the usual programming-language convention,
   where postfix binds tighter (`-x++` is `-(x++)`).
4. **One binary tier leaves nothing to memorize.** With binary `!` gone
   (SP18) and `&` gone (SP20), no ranking of `&` against `|` is needed. JS's
   `v` flag, the one regex dialect with set operators inside a class, refuses
   to rank them at all: `[a&&b--c]` is a SyntaxError.

### 12.2 Why there is no `&` inside a pattern

1. **Regex engines cannot intersect.** Intersecting two multi-character
   patterns needs a product automaton or lookahead, and RE2 has neither. The
   island `&` compiled to `(?=A)B`, which RE2 rejects, so every island `&`
   silently matched nothing (§7 #3).
2. **The whole-pattern form already works.** For `is` and `match` the whole
   string must match, so `\(A) & \(B)` combines two full matches with the
   type operators and needs no regex support: `"abc" is (\(a+) & \(w+))` is
   true.
3. **What regex does offer is narrow.** Regex has only character-set
   intersection (`&&` under JS's `v` flag, and in Java), and RE2 lacks even
   that. The overlap of two single-character sets can be written directly as
   a set or a union of ranges. SPO13 recommended admitting `&` between
   single-character sets; SP20 chooses the smaller language.

### 12.3 Consequences

- Formal text: S11.1.2v3 gains the tier clause and "`|` is the island's only
  binary operator", and S10.1.1v3's island sentence drops `&`. The committed
  spec is still 48.0.2, so both rulings amend the uncommitted v3 clauses and
  ship under the same 49.0.0 (Doc_Convention §3).
- The C parser's `parse_island_body` rejects an island `&` with E103, naming
  `\(A) & \(B)`. The regex lowering's lookahead branch is removed, and so are
  the surface printer's binary `&` and `!`, since no island can contain them.
- In `grammar.js`, `char_occurrence_type` takes a negated operand, so
  `\(!d+)` parses as `(!d)+`. `char_binary_type` is `|` alone, and
  `type_operators` is back to its committed form.
- Fixtures: `test/lambda/pattern_precedence.ls` pins each tier; the
  interpreter, the JIT and the auto tier give identical output.
  `negative/semantic/string_pattern_intersection.ls` pins the diagnostic. Both
  S16 harnesses gain an island section (reference grammar 366/366, C parser
  380/380).

---

## 13. SP21: `replace` follows ECMAScript `replaceAll` (ruled 2026-09-27)

**Ruling (USER, 2026-09-27; formal text S17.6.1).** `replace` handles empty
matches the way ECMAScript `String.prototype.replaceAll` does, and inserts its
replacement as literal text: the SPO11 recommendation, adopted. The options
`limit`, `last` and `ignore_case` choose among the same matches and change
nothing else.

| Call | Before | After (= JS) |
|---|---|---|
| `replace("aab", \("a"*), "-")` | `"-b-"` | `"--b-"` |
| `replace("ab", \(d*), "-")` | `"-a-b-"` | `"-a-b-"` |
| `replace("", \(d*), "-")` | `""` | `"-"` |
| `replace("a1b", \(d), "<\\0>")` (the text `<\0>`) | `"a<1>b"` | `"a<\0>b"` |

### 13.1 Why

1. **One call, one meaning.** Whether an option is present must not change how
   matches are found or what replaces them (§7 #6).
2. **It completes a family.** `split` already follows ECMAScript (S17.1.1),
   and `find` already steps over empty matches the same way (§4.4). With
   `replace` following them, every match `replace` replaces is one that
   `find` reports.
3. **Literal text until captures exist.** With no capture groups (SO32) there
   is nothing for `$1` or `\1` to name. RE2's rewriter also collided with
   Lambda's own string escapes (`"\\0"`), and it silently dropped every other
   backslash sequence (§4.6).

### 13.2 Consequences

- Plain `replace` uses the loop the options path already used; RE2's
  `GlobalReplace` path is gone (`pattern_replace_all`).
- `find` and pattern `replace` search an empty subject too, since a pattern can
  match `""`: `find("", \(d*))` is one empty match at 0, where it was `[]`,
  and `replace("", \(d*), "-")` is `"-"`. `split` is unchanged (S17.1.1
  already rules its empty subject).
- An empty options map sets no option. It was rejected, so
  `replace("aab", "a", "-", {})` and `find("aab", "a", {})` returned errors.
- Fixture: `test/lambda/pattern_replace.ls`, identical on the interpreter,
  the JIT and the auto tier.
- **Left open (SPO14, formal SO47):** an empty *literal* needle still matches
  nothing: `replace("abc", "", "-")` is `"abc"` and `find("abc", "")` is `[]`,
  where JS `replaceAll("", "-")` gives `"-a-b-c-"`. SP21 speaks to pattern
  matches; the user kept the literal case open.
- S17.6.1 is a new ruling that changes what plain `replace` calls return, so
  the spec moves to 50.0.0 (MAJOR); 49.0.0 had been committed with SP17–SP20.

---

## 14. Defect fixes (2026-09-27)

The §7 defects that needed no ruling, fixed the same day. Each fix is pinned by
a golden run on all three tiers or by a negative fixture in
`test_lambda_errors_gtest`.

- **#1, printing a pattern.** The printer cast every type value to the
  `TypeType` wrapper, but a pattern is a bare `TypePattern` under the same tag
  (D3.1.1v4). A pattern now prints as its canonical source, `\((d)+)`, and a
  contract diagnostic names a pattern the same way instead of as `type`.
- **#3, silent failures.** The lowering now returns a reason instead of
  logging and emitting nothing, and the resolver reports it:
  - an unknown or later-defined name is E204 ("`B` is not defined before this
    pattern"), with a hint when the name is class letters run together
    (`dw`);
  - a name that is no pattern, literal union or character range is E200;
  - a pattern RE2 refuses is E200 ("this pattern cannot be compiled: …");
  - a self-reference is caught as an undefined name, and a depth guard stops
    any expansion that would recurse.
- **#5, counts.** A count inside a pattern is checked by the scanner type
  position uses: `{n}`, `{n,m}` or `{n+}`, blanks allowed as in the reference
  grammar. `{n,}` names `{n+}`; `{,5}` and `{a}` are E103. The regex engine's
  own bounds are checked in the pattern parser: `n ≤ m`, at most 1000.
- **#7, `...`.** It lowers to `(?s:.*)`, so it matches newlines.
- **#9, pattern annotations.** A pattern contract now proves only its tag's
  domain (`string` or `symbol`) and leaves membership to the runtime check, as
  a range contract does. So `let code: \(a{3}) = "abc"`, `fn f(x: \(d+))` and a
  named alias all work, a non-matching string fails at run time, and an `int`
  or a symbol is rejected at compile time. A range- or pattern-typed source
  bound to a `type` slot defers to the runtime check: there the source may be
  the type value itself (`let t: type = \(d+)`). That also fixed
  `let w: type = R` for a range type alias, which had been E201.

Found while fixing, not fixed: `let u: type = int` is E201. The static check
strips the type-value wrapper, so it compares `int` with `type`. The cause
lies outside patterns.

---

## 15. SP22 and SP23: case folding and case mapping (ruled 2026-09-27)

**Rulings (USER, 2026-09-27; formal text S17.7.1 and S17.7.2).**

- **SP22.** Case-insensitive matching folds by Unicode simple case folding:
  one code point to one, the same in every locale, on every path. This is
  SPO12's recommendation, part 1; the spelling for a case-insensitive
  `is`/`match` stays open.
- **SP23.** `lower` and `upper` use Unicode full case mapping, the same in
  every locale, as ECMAScript's `toLowerCase`/`toUpperCase` do.

### 15.1 Why simple folding

| Case | JS `/…/iu` | RE2 (the pattern path) |
|---|---|---|
| `ẞ` matches `ß` | yes | yes |
| Kelvin sign matches `k` | yes | yes |
| `ς` matches `σ` | yes | yes |
| `ß` matches `ss` | no | no |
| `İ` matches `i` | no | no |

1. It is what the regex engines implement: RE2 already folded the pattern path
   this way, and so does JS with the `u` flag.
2. A match keeps its length in code points, so `find`'s `index` and `value`
   stay the source's (S17.4.1). Full folding (`ß` ↔ `ss`) changes lengths;
   no regex engine does it.
3. It is the same in every locale: no Turkish `İ`/`i` special case, as in JS.

### 15.2 Why full mapping for `lower` and `upper`

Mapping is a different operation from folding: it produces text for people to
read, so `upper("straße")` must be `"STRASSE"`, not `"STRAßE"`. The user doc
already promised Unicode behaviour (`upper("café")` is `"CAFÉ"`), while the
implementation mapped ASCII letters only. Full mapping is also what JS gives,
so Lambda and LambdaJS agree.

### 15.3 Implementation

- **One case mapping for both languages.** LambdaJS already implemented full
  mapping (`js_string_simple_case_map`, despite its name): utf8proc's simple
  mappings, hand-coded multi-character cases, a decomposition trick for the
  rest, and the Final_Sigma context. The trick split characters that have a
  one-to-one mapping: `ǅ` upper-cased to `D` + `Ž` instead of `Ǆ`. The code
  moved to `utf8_case_map` in `lambda/core/utf_string.cpp`. The trick is
  replaced by the 103 unconditional entries of `SpecialCasing.txt` (16.0.0),
  generated by `utils/generate_special_casing.py`. LambdaJS's
  `toLowerCase`/`toUpperCase` and Lambda's `lower`/`upper` both call it.
  Lambda keeps its ASCII byte loop, since no special case reaches ASCII.
- **One folding for both paths.** A case-insensitive literal search compiles
  the escaped needle as a transient pattern and uses the pattern search loops
  (`literal_find_all_ignore_case`, `literal_replace_all_ignore_case`), so it
  folds exactly as RE2 does. The literal scanner lost its ASCII-folding
  branch.
- **Verification.** Lambda's `lower`/`upper` and LambdaJS match Node on 20
  special cases (`ß`, `ﬁ`, `ŉ`, `ǰ`, `ΐ`, `ᾳ`, `ǅ`, `İ`, final sigma, the
  Kelvin sign, among others). All 110 Test262 tests for
  `String.prototype.to[Locale]{Upper,Lower}Case` pass. Fixtures:
  `test/lambda/string_case.ls` and `test/lambda/string_ignore_case.ls`, with
  the same output on all three tiers.

---

## Appendix A — Implementation map

Anchors are `file:line` plus the symbol, verified against the working tree
of 2026-09-27 with SP17–SP23 and the §14 fixes applied.

**Island parser.** In `lambda/runtime/parse_type_pattern.cpp`:

- `parse_island` (535) and `parse_island_body` (509), which has only the
  `|` tier and rejects an `&` (SP20).
- `parse_island_concat` (484) implements whitespace concatenation.
- `parse_island_unary` (425) handles prefix `!` and one suffix. A count goes
  through `scan_occurrence_count` (943) and `occurrence_count_problem` (919),
  shared with type position, then the engine bounds.
- `parse_island_primary` (359) and `island_char_class` (337).

**Resolver checks (SP17).** In `resolve_type_pattern`
(`lambda/runtime/parse_type_pattern.cpp`):

- `LSF_TP_PATTERN_REF` (1447) rejects a name the pattern cannot use (E204,
  E200), and `LSF_TP_ISLAND` (1505) reports a compile failure nothing else
  reported.
- `LSF_TP_ISLAND_UNARY` (1484) rejects a `!` operand that is not a
  single-character set (E200).
- `LSF_TP_PATTERN_RANGE` (1467) rejects a range bound that is not a single
  character (E200, S11.1.3).
- Both leave a symbol literal to the island's content-only diagnostic.
  `pattern_ast_has_symbol_literal` (`lambda/runtime/build_ast.cpp:4877`) now
  looks inside range bounds too.

**Regex lowering.** In `lambda/runtime/re2_wrapper.cpp`:

- `compile_pattern_ast` (664) anchors `^…$` and returns the lowering's reason.
- `compile_pattern_to_regex` (425) and `lower_pattern` (429), which fail with
  a reason (`pattern_lowering_failed`, 393); `pattern_can_name` (401).
- `char_set_add_node` (256) builds a single-character set as code-point
  intervals, and fails on anything else. `pattern_is_char_set` (319) is the
  resolver's entry to it; `char_range_type_bounds` (224) reads a character
  range type (§7 #10).
- `char_set_add_class` (181) is the one definition of each named class.
- `compile_char_set` (356) lowers a set, or its complement, to one RE2 class.
  It serves `!` and every range.
- `compile_char_class` (365) takes `d`, `w`, `s` and `a` from the class
  table. `.` stays RE2's `.`, and `...` is `(?s:.*)`.
- `append_literal_pattern` (382) lets an island name a literal union (SP7).
- `|` is the only binary operator it lowers; the `(?=…)` lookahead for `&`,
  which RE2 rejected, was removed with SP20.
- `convert_occurrence_to_regex` (45) copies the checked count text, turning
  `{n+}` into `{n,}`.
- `escape_regex_literal` (59) and `append_literal_type` (825).

**RE2 options.** `lib/re2_glue.hpp` `re2_glue_default_options`: UTF-8
encoding, Perl syntax, leftmost-first, case-sensitive, `dot_nl` off.

**Search functions.** In `lambda/runtime/re2_wrapper.cpp`:

- `pattern_find_all_options` (1118).
- `pattern_replace_all_options` (1184), the one replace loop (S17.6.1).

`fn_replace_impl` (`lambda/runtime/lambda-eval.cpp:8085`) calls that loop
with or without options (§7 #6); `parse_find_replace_options` (7947) takes an
empty map as no options.

**Printer and contract names.** `lambda/core/print.cpp:671`,
`PrintItemVisitor::operator()(ItemOf<LMD_TYPE_TYPE>)`, prints a bare
`TypePattern` as its source (§7 #1); `lambda_type_format_name_inner`
(`lambda/runtime/type_contract.cpp:1610`) names a pattern contract the same
way.

**Pattern contracts (§7 #9).** `lambda_pattern_type_domain` and
`lambda_member_test_type_domain` (`lambda/lambda-data.hpp:1292`, 1299) give a
pattern's or range's carrier domain; `static_boundary_relation`
(`lambda/runtime/build_ast.cpp:1683`, 1687) uses it for targets and sources.

**Case (§15).** `utf8_case_map` (`lambda/core/utf_string.cpp:293`) with the
`SpecialCasing.txt` table and `utf8_special_case` (237), generated by
`utils/generate_special_casing.py`. Lambda's `text_case_map`
(`lambda/runtime/lambda-eval.cpp:7203`) serves `lower`/`upper`, and LambdaJS's
`js_string_case_map` (`lambda/js/js_runtime.cpp:22679`) serves
`toLowerCase`/`toUpperCase`. A case-insensitive literal search leaves
`fn_replace_impl` (8103) and `fn_find` (8259) for
`literal_replace_all_ignore_case` and `literal_find_all_ignore_case`
(`lambda/runtime/re2_wrapper.cpp:1268`, 1258).

**Expression precedence.** `lambda/runtime/parser/lambda_parser.c:1804`,
`infix_bp`: `|`, `&`, `!` and `to` all map to `LAMBDA_BP_SET` (§6.4).

**Reference grammar.** In `lambda/tree-sitter-lambda/grammar.js`:

- `char_pattern_island` (1490), `char_occurrence_type` (1500),
  `char_negation_type` (1504) and `char_binary_type` (1518).
- `char_occurrence_type` takes a negated operand (SP19), and
  `char_binary_type` is `|` alone (SP18, SP20).
- `_uncounted_occurrence` (1272), which admits `array_count`.
- `occurrence_count` (1286).
- The `precedences` list (402) orders `range_to` above `set_intersect` above
  `set_exclude` above `set_union`.

## Appendix B — Probe inventory

These live in `temp/pattern_probe/`, which is scratch and not committed.

| Probes | What they cover |
|---|---|
| `p1_classes.ls` | classes and line terminators (§4.1, §4.2) |
| `p2_ranges.ls` | ranges (§7 #4) |
| `p3_neg.ls` | negation (§7 #2) |
| `p4_counts.ls`, `p4b_named_bad.ls` | counts and how errors surface (§7 #3, #5) |
| `p5_prec.ls`, `p6_valprec.ls`, `p13.ls` | precedence (§6) |
| `p7_find.ls`, `p9.ls`–`p12.ls`, `p15.ls` | search functions (§4.4, §4.6) |
| `p8_icase.ls` | case folding (§4.5) |
| `p14.ls`, `p19.ls` | type-level `&` and `!` between islands (§5.2, §10.1, SPO13) |
| `p20_refs.ls`, `p21_sp17.ls` | SP17 behaviour and named references (§10) |
| `n1.ls`–`n9.ls` | SP17 and range-bound diagnostics (§10) |
| `p22_*.ls`, `p23_let_plus.ls` | patterns as `let` and parameter annotations (§7 #9) |
| `s1.ls`–`s6.ls`, `c1.ls`–`c6.ls` | printer crash (§7 #1) |
| `e1.ls`–`e11.ls` | diagnostics |
| `ts1.ls`, `ts2.ls` | reference-grammar parses |
| `js_probe.js` | the JS side |
