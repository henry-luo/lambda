# Lambda String Pattern Syntax — Delimited Form — Design

**Date:** 2026-08-07
**Status:** IMPLEMENTED (rev 5) — rev 1 (2026-08-07): migrate string patterns
from the bare (undelimited) form to the delimited `\(...)` form with bare
(backslash-free) character classes inside; `\...\` evaluated and rejected.
rev 2 (2026-08-07): SPO2 resolved — symbols out of the pattern *interior*;
symbol pattern types derived via a `symbol(\(...))` lift.
rev 3 (2026-08-07): **symbol-domain spelling re-decided after a five-option
sweep (§4.4): `\symbol(...)` tagged delimiter** replaces the rev-2 lift
(SP14 revised, SP15/SP16 added, SPO6 dissolved); the tag governs domain only —
the interior remains one content language, so named patterns compose across
domains.
rev 4 (2026-08-07): **SP9 reversed — `that (...)` keeps its required
parentheses.** An unparenthesized constraint would open the entire expression
grammar into type syntax space; the SPO3 boundary sweep is the recorded
evidence (SPO3 closed by reversal). SP8's grammar cleanup stands independently.
rev 5 (2026-08-07): implementation adopted. The grammar island, AST/MIR
lowering, symbol-domain checks, partial-operation rejection, literal-union
normalization, corpus migration, and the SP5 string-range overload are landed.
**Scope:** surface syntax, symbol-domain formation, and the runtime seams
required to make the adopted type values enforceable. Full-match `is`/`match`,
partial-match `find`/`replace`/`split`, and `|` as union retain their specified
semantics; partial operations accept string-domain patterns only.
**Relation to prior docs:** pattern semantics anchored by **S10.1.1** (`|` is
union everywhere, including string patterns) and **S11.2.1** (match arms,
constrained arms); constrained-type `that` per **S10.3.1** (keyword operators)
with predicate enforcement still open under **SO9**. Type values per **D3.1.1**
(`Type.kind` includes pattern and range kinds, **D3.1.1v2**); the adopted surface and
string-range rulings are now **S11.1.2** and **S11.1.3**.

---

## 1. Goal

> **Make string patterns a self-delimiting island.** Today a string pattern is
> a bare type expression that mixes freely with the general type grammar; the
> whitespace-concatenation rule it needs (`\d[3] "-" \d[3]`) is the one place
> juxtaposition is meaningful in type syntax, and it leaks ambiguity into
> everything around it. After this change, a pattern always lives inside
> `\( ... )` and the general type grammar loses juxtaposition entirely.
> (Rev 1 hoped the cascade would also unlock `T that expr` without forced
> parentheses; rev 4 reversed that — see SP9.)

---

## 2. Prior art

Four ways host languages embed a pattern language, and what each one teaches.

### 2.1 JS `/.../` — the inherited symmetric delimiter (what NOT to do)

The lineage is QED → ed → grep/sed → Perl → JavaScript. In ed, `/pattern/` was
a command *address* ("next line matching"), and the slash was the command
syntax's field separator — `g/re/p` is where grep's name comes from. Perl
borrowed the idiom wholesale; JS copied it from Perl in 1995. Nobody chose
symmetric delimiters on merit; it is a 50-year-old field separator fossilized
into syntax. It was tolerable because the delimiter (`/`) differs from the
escape char (`\`), but JS still paid twice:

1. **`/` collides with division.** `a / b / c` — two divisions or a regex?
   The lexer cannot tell without knowing whether the previous token ends an
   expression. JS's grammar is famously non-context-free at the lexical level
   because of this; every parser, minifier, and highlighter carries special
   "regex-or-divide" state, and all of them have shipped bugs from it. It is
   the canonical example of an unbalanced symmetric delimiter poisoning an
   entire grammar.
2. **No balance.** An unterminated regex swallows the line; editors cannot
   bracket-match; error recovery is poor. Perl itself later added `m{...}`,
   `m(...)`, `m[...]` — user-choosable *balanced* delimiters — because users
   kept hitting the wall of `/.../`. The ecosystem migrated toward balanced
   delimiters on its own.

### 2.2 C++ `std::regex` / Rust `regex` — patterns as opaque strings

Neither language has a regex literal; the pattern is a runtime string:

```cpp
std::regex phone("\\d{3}-\\d{4}");     // double-escaped
std::regex phone2(R"(\d{3}-\d{4})");   // C++11 raw string, added largely for this
```

```rust
let phone = Regex::new(r"\d{3}-\d{4}").unwrap();  // raw literal from day one
```

Merit: the host grammar stays simple. Cost: the compiler never sees the
pattern — zero compile-time checking (typos throw at runtime), no highlighting
inside the pattern, and the host's string-escape rules fight the pattern's own
escapes (bad enough that C++ grew raw strings for it). Lambda already does
better — patterns are parsed by the host grammar — and must not regress to
opaque strings.

### 2.3 Pomsky — the closest relative (merits to keep)

Pomsky is a standalone readable-regex DSL that compiles to regex. Its interior
design is nearly isomorphic to Lambda's string patterns:

```pomsky
let digits = [ascii_digit]+;
"v" digits "." digits "." digits            # version string
[ascii_digit]{3} "-" [ascii_digit]{3} "-" [ascii_digit]{4}
['a'-'z']+ " " ['a'-'z']+
```

Merits, all of which Lambda's interior already shares or adopts here:

- **Quoted literals** — literal text is always in quotes, never bare, so the
  metacharacter/literal boundary is unmistakable (the #1 readability failure
  of classic regex).
- **Whitespace = concatenation** — sequences read left to right with room to
  breathe; no `\x20`-style noise.
- **Named char classes** — `[ascii_digit]`, not `\d` mnemonics; the class
  namespace is open (Unicode categories fit later).
- **Named sub-patterns** (`let`) — composition by reference, mirroring
  Lambda's named `type` patterns.
- **Counted occurrence** (`{3}`, `{3,5}`) — Lambda's `[3]`, `[3, 5]`.
- **Quoted ranges** (`['a'-'z']`) — Lambda's `"a" to "z"`.

Pomsky's weakness: it is out-of-band — a separate compile step bolted onto a
regex engine, invisible to the host language's type system. Lambda embeds the
same interior design *in* the type system.

### 2.4 Melody — block syntax (the far end of the spectrum)

```melody
3 of <digit>; "-"; 3 of <digit>; "-"; 4 of <digit>;
capture year { 4 of <digit>; }
```

One term per statement: maximally unambiguous and diff-friendly, but too
verbose for inline type positions. Useful as the outer bound — Lambda should
sit between Pomsky's density and Melody's ceremony, not at either end.

### 2.5 The spectrum

| Approach | Host sees pattern? | Cost |
|----------|-------------------|------|
| C++/Rust plain strings | no — opaque text | runtime errors, escaping, no tooling |
| JS `/.../` literal | yes, via lexer hack | division ambiguity, unbalanced delimiter |
| Pomsky / Melody | fully — it *is* the language | separate compile step, out-of-band |
| **Lambda `\(...)`** | **fully, in the type system** | **one delimiter pair** |

`\(...)` takes the regex-literal *feel* with balanced-delimiter *mechanics*
and Pomsky's interior — the sweet spot none of the prior art occupies.

---

## 3. Issues with the current (bare) syntax

Current form (doc/Lambda_Type.md §String Patterns; grammar.js
`_string_type_expr` / `concat_type` / `pattern_char_class`):

```lambda
string Phone   = \d[3] "-" \d[3] "-" \d[4]
string ZipCode = \d[5] ("-" \d[4])?
string Version = "v" \d+ "." \d+ "." \d+
```

The pattern is a bare type expression; nothing marks where it starts or ends.

1. **Usage confusion.** Patterns mix freely with ordinary value/type syntax —
   a reader cannot tell at a glance whether `"v" \d+` is a pattern, a string
   followed by garbage, or a concat expression. Patterns have regex-like
   semantics but no regex-like visual identity.
2. **Whitespace-concat poisons the open grammar.** `concat_type` makes
   juxtaposition meaningful, which is unique in the type grammar and forces:
   - `prec.dynamic(-1)` on `concat_type` so `int[2+]` is not parsed as
     `concat_type(int, [2+])` (grammar.js:1255);
   - the AST builder to *reject* concat everywhere outside pattern
     definitions ("Only valid inside string/symbol pattern definitions; AST
     builder rejects elsewhere") — a semantic patch over a syntactic leak;
   - **`that` to require parentheses**: `T that (expr)` — because with
     juxtaposition live, the parser cannot tell where an unparenthesized
     constraint expression ends (S11.2.1 spells constrained arms
     `case int that (~ > 0):` for the same reason). *(Rev 4 note: the parens
     turn out to be required for a deeper reason too, and are kept even after
     juxtaposition is removed — see SP9.)*
3. **Pattern definitions are detected by content analysis, not syntax.**
   `type_stam` comments: "The AST builder detects pattern definitions by
   analyzing the type expression content" — a heuristic where a delimiter
   would make it structural.
4. **Char-class tokens leak into the general type grammar.** `\d`, `\w`,
   `\s`, `\a`, `\.` are tokens of the *whole* type grammar even though they
   are only meaningful in patterns, enlarging the surface where they can
   appear in error.

---

## 4. The two candidate designs, and the decision

Both candidates adopt the same **interior redesign**: inside the delimiter,
character classes are **bare reserved names** — `d w s a` and `.` (any char),
`...` (any string) — no backslash anywhere in the pattern operators. Quoted
literals, whitespace concat, `|`/`&`/`!`, `to`-ranges, and occurrence
suffixes are unchanged.

### 4.1 Option A: `\...\` (symmetric backslash pair)

```lambda
type Phone = \d[3] "-" d[3] "-" d[4]\      // opening \d preserves old digraph
```

The interior redesign fixes the *original* fatal flaw (delimiter == escape
char: `... \d\` needed lookahead to split "class then close" from "close then
junk"). Its one genuine merit is migration continuity — `\d[3]` becomes
`\d[3]\`, and the familiar `\d` digraph survives at the opening. But five
problems remain, one of them *created* by the interior redesign itself:

- **A1 — `\"` at the opening reads as an escaped quote.** Patterns very often
  start with a literal (`"v" d+`, `"#" (...)`, `"pre"? w+`, `\("hello"|"hi")`).
  In this form they open as `\"hello" ...\`, and `\"` is the escaped-quote
  digraph in essentially every language and highlighter. Editors, grep, and
  humans all mis-tokenize the pattern start. The continuity merit only helps
  patterns starting with a char class; patterns starting with a literal get
  this instead, which is worse.
- **A2 — string literals inside the pattern still contain `\`.** "No
  backslash in the pattern" holds for operators, not for quoted content:
  `"\n"`, `"\\"`, `"\t"` are legal literal text. `\ "a\\b" \` forces the
  delimiter scan to be fully quote-and-escape-aware (external-scanner
  territory in tree-sitter) or the literal's backslash closes the pattern
  early — exactly the complexity the redesign was meant to delete.
- **A3 — runaway on a missing terminator.** Since `\` now only ever opens
  patterns, a forgotten closing `\` runs to the *next* pattern's opening
  `\`, silently fusing two type definitions. A missing `)` is a bounded,
  well-recovered error.
- **A4 — trailing `\` reads as line continuation.** Pattern definitions
  almost always end a line (`type Phone = \...\`), and a lone `\` at EOL is
  the continuation marker in shell, C, Python, and most highlighters.
- **A5 — suffixes and unions compose badly.** `\w+\?` and `"GET" | \w+\`
  are nearly unreadable — the latter next to the `...` any-string token
  (whose literal spelling in this form is `\...\` itself).

### 4.2 Option B: `\(...)` (backslash-opened balanced pair) — **DECIDED**

```lambda
type Phone = \(d[3] "-" d[3] "-" d[4])
```

- Balanced: nests with the grouping patterns already use
  (`\(d[5] ("-" d[4])?)`), bounded error on a missing `)`, tree-sitter
  recovery, and bracket-matching / rainbow parens / auto-indent for free.
- Never abuts `\` against a quote (no A1) and is indifferent to backslashes
  inside string literals (no A2).
- Composes: `\( w+ )?`, `"GET" | \( w+ )`, multi-line patterns.
- Extends the existing family: `\d`, `\w` already established "backslash
  enters pattern-land"; `\(` reads as "pattern group". No lexer state, no
  external scanner — `\(` is a plain token, and `(` is not a class char.
- JS's two costs (§2.1) are both structurally absent: `\` has no infix
  meaning in Lambda (no division-style ambiguity), and the delimiter is
  balanced.

### 4.3 Decision ledger

| ID | Decision |
|----|----------|
| **SP1** | **String patterns are always delimited: `\( pattern )`.** The bare form is retired. `\...\` is **rejected** (A1–A5, §4.1). |
| **SP2** | **Bare char classes inside the delimiter.** `d` (digit), `w` (word), `s` (whitespace), `a` (alpha), `.` (any char), `...` (any string). The `\d \w \s \a \.` spellings are retired everywhere. |
| **SP3** | **Class names are reserved words in pattern scope.** Inside `\(...)`, `d w s a` (plus `to`, and future class names per SPO1) cannot be referenced as user pattern names. Outside the delimiter they remain ordinary identifiers. A named pattern whose name collides is still definable, just not referenceable inside a pattern. |
| **SP4** | **Interior grammar otherwise unchanged.** Quoted literals, whitespace concat, `\|` union / `&` intersection / `!` negation (S10.1.1), `"a" to "z"` ranges, `? + *` and `[n] [n+] [n, m]` occurrence, `(...)` grouping, references to named patterns (`HexDigit[3]`). Full-match `is`/`match` vs partial-match `find`/`replace`/`split` semantics untouched. |
| **SP5** | **(rev 4a, corrected)** **Char classes are delimited-only; ranges are NOT.** `\d`-style class tokens disappear from the general type grammar and live only inside `\(...)`. **`X to Y` stays purposely overloaded across all three contexts** — the overload is coherent because the denotation is one set: expr syntax = shorthand for the literal array of consecutive values (`Range` container, S4.8 successor guard); type syntax = range type, matched by membership (annotations, match arms `case 90 to 100:` per S11.2.1); pattern interior = the same set compiled as an RE2 char class (`"a" to "z"` → `[a-z]`). No grammar split; only the compilation strategy differs per context. (The pre-existing runtime gap — `fn_to` implements integer operands only, so expr-space `"a" to "z"` errors "unknown range type" — is **folded into the impl plan as P5**: single-codepoint operands, codepoint stepping, membership shared with the char-class set.) |
| **SP6** | **General type expressions admit *literals* and *delimited patterns* only** (of text-like constructs). `type HttpMethod = "GET" \| "POST"` and `type Keyword = 'if' \| 'else'` stay valid (singleton literal union types, no delimiter needed — literals are already unambiguous). Any pattern construct beyond bare literals and `\|` requires a delimiter (`\(...)` / `\symbol(...)`). |
| **SP7** | **Literal unions are the degenerate case of patterns.** `"hello" \| "hi"` and `\("hello" \| "hi")` denote the **same type** — refactoring between the forms is semantics-preserving; `is` and `match` behave identically on both. |
| **SP8** | **Whitespace concatenation exists only inside `\(...)`.** `concat_type` leaves the open grammar; the `prec.dynamic(-1)` guard and the AST builder's "reject concat elsewhere" patch are deleted; `int[2+]` parses without dynamic precedence. |
| **SP9** | **(rev 4, REVERSED)** **`that (expr)` keeps its required parentheses.** Rev 1 proposed dropping them once juxtaposition left the open grammar; the SPO3 boundary sweep showed the real cost is not any single boundary but that an unparenthesized constraint **opens the entire expression grammar into type syntax space**: every expression form becomes reachable in type position, the `\|`/`&` cutoff becomes a precedence rule users must memorize (S10.1.1 makes `\|` a value operator too, so `int that ~ > 0 \| string` is grammatical under both readings), and malformed types yield expression-grammar errors. The attractive uses — `{a: T that ..., b: T that ...}`, `fn f(a: T that ..., b: T that ...)` — are acknowledged and foregone; the parens are the wall between the two grammars. Consistent with S11.2.1's spelling `case int that (~ > 0):`. `~` scoping per S10.1.3 unchanged. |
| **SP10** | **Pattern definitions become syntactically self-evident.** `type X = \(...)` needs no content-analysis heuristic; the `type_stam` detection logic reduces to "delimiter present". The `string X = ...` / `symbol X = ...` prefix definition forms are **retired** — the delimiter (plus the `symbol(...)` lift, SP14) carries all the information the prefixes carried (rev 2, with SP13). |
| **SP11** | **Occurrence applies to a whole delimited pattern.** `\( ... )?`, `\( ... )[2]` compose like any grouped term when a pattern appears inside a larger pattern; at the top of a general type expression a delimited pattern takes no occurrence suffix (occurrence is pattern-interior structure). |
| **SP12** | **ADOPTED.** The formal semantics now record the delimited surface and domain rulings as **S11.1.2** and **S11.1.3** (per rule 17); `doc/Lambda_Type.md` §String Patterns is synchronized with the implementation. |
| **SP13** | **Symbols are out of the pattern *interior*** (resolves SPO2). The pattern language has one domain: content. Literals inside any delimiter are `"..."` strings only; `'...'` symbol literals are illegal inside. The `symbol X = pattern` and `string X = pattern` prefix definition forms are retired (SP10). The dominant symbol use — enumeration — never needed patterns: `type Keyword = 'if' \| 'else'` is a bare literal union of symbol singletons, symmetric with SP6's string literal unions. |
| **SP14** | **(rev 3, replaces the rev-2 `symbol(\(...))` lift)** **Structural symbol patterns use a tagged delimiter: `\symbol( pattern )`** — a type matching any symbol whose character content full-matches the pattern. `\symbol(` lexes as a single opening token, sibling of `\(`; the pattern literal is self-contained (its domain travels with it as a first-class type value, D3.1.1v2). `symbol()` remains purely the value-level conversion function — untouched. Five candidate spellings were evaluated; see §4.4. |
| **SP15** | **The tag governs domain only; the interior is one content language.** A named pattern referenced inside any delimiter contributes **content structure only**, regardless of the domain of the delimiter it was defined in — `\symbol(P)` means "symbols whose content matches P" even when `P` was defined as plain `\(...)`. (Semantically the tag is `symbol ∧ content(P)`, but no user writes an intersection.) One `HexDigit` serves every pattern; the cross-domain-reference error class does not exist. |
| **SP16** | **`\tag(...)` is an open tagged-delimiter family** (the shape Perl migrated to with `m{...}`, §2.1). Bare `\(...)` = string (the common case pays nothing); `\symbol(...)` = symbol; the namespace stays open for future variants (e.g. a case-insensitivity flag, a full-regex escape hatch) without touching the grammar shape again. Tag words follow S10.3.1 (words over sigils); no new tag ships without its own ruling. |

### 4.4 The symbol-domain sweep (rev 2 → rev 3, for the record)

Five candidate spellings for structural symbol patterns were evaluated across
2026-08-07 before `\symbol(...)` was adopted. Recorded in full because the
rejected options are natural re-inventions — this table is the "we already
looked at that" reference.

| # | Spelling | Example (identifier pattern) | Verdict and reason |
|---|----------|------------------------------|--------------------|
| **B1** | Quote-marked interior — `'a' 'w'` are the char classes a/w in symbol domain; bare `a w` = string domain; `"a"` = literal | `\('a' 'w'*)` | **Rejected.** (i) Quoting a letter to mean a *metacharacter class* inverts the universal convention that quoting means literal — and `'a'` already IS a literal symbol singleton in general type position (`type X = 'a' \| 'b'`); same token, opposite meanings across the delimiter. (ii) Meaning depends on length: `'a'` = class but `'ab'` = symbol literal, so single-letter symbol literals become unspellable inside patterns. (iii) Domain is inferred per-atom: forget one quote (`\('a' w*)`) and the pattern is a mixed-domain error the grammar permits but must diagnose; readers determine a pattern's type by scanning its atoms; named patterns can't be reused across domains. An earlier, weaker variant of this idea (domain marked by which *literal* quote style appears) additionally could not spell the identifier pattern at all — no literal to hang the domain on, and `''` is invalid. |
| **B2** | Intersection — pattern is a domain-free content type, narrowed by the existing `&` | `symbol & \(a w*)` | **Runner-up, folded in.** Cleanest algebra: one marker, outside, using S10.1.1 machinery; interior uniform; named patterns compose freely. Rejected as the *surface* because it bends the reading of `&` (a strict reading makes `symbol & string-pattern` empty unless bare `\(...)` is ruled textual — admitting both domains — which surprises in annotation position). Its best property — domain applied at the boundary, interior domain-free — **is retained as SP15**: the tag is sugar for exactly this intersection semantics. |
| **B3** | Conversion-function lift (the rev-2 decision) | `symbol(\(a w*))` | **Superseded.** Consistent with types-as-values (D3.1.2) but requires changing what `symbol()` means when applied to a type value — a constructor that only *looks* like application — and opened SPO6 (conversion fn on a type value vs AST-recognized spelling; dynamic vs syntactic availability). The tagged delimiter needs no change to `symbol()` at all. |
| **B4** | Keyword prefix — `symbol` juxtaposed before the pattern | `symbol \(a w*)` | **Rejected (close second).** Reads naturally, trivial to parse as a fixed two-token form — but it reintroduces juxtaposition into the open type grammar, the very thing SP8 deletes. Adjacency rules leak: `symbol` is also a standalone base type, so `let a: symbol` followed by a line starting `\(d+)` fuses (tree-sitter is newline-blind); in `symbol \(a) \| \(b)` the keyword's binding scope must be learned. Extends awkwardly to future variants. |
| **B5** | **Tagged delimiter** — domain fused into the opening token | `\symbol(a w*)` | **ADOPTED (SP14).** One token, zero interaction with the open grammar, no adjacency/newline edges; the literal is self-contained; founds the open `\tag(...)` family (SP16); `symbol()` untouched. Post-SP2 the `\` namespace is free (retired `\s`/`\d` classes), so nothing collides — `\symbol(` merely *starts* with the old `\s` digraph, a momentary flicker for migrating eyes only. |

### 4.5 The design in examples

```lambda
// before                                          // after
string Digit    = \d                               type Digit    = \(d)
string Word     = \w+                              type Word     = \(w+)
string Phone    = \d[3] "-" \d[3] "-" \d[4]        type Phone    = \(d[3] "-" d[3] "-" d[4])
string ZipCode  = \d[5] ("-" \d[4])?               type ZipCode  = \(d[5] ("-" d[4])?)
string Version  = "v" \d+ "." \d+ "." \d+          type Version  = \("v" d+ "." d+ "." d+)
string Email    = \w+ "@" \w+ "." \a[2, 6]         type Email    = \(w+ "@" w+ "." a[2, 6])
string HexDigit = "0" to "9" | "a" to "f"          type HexDigit = \("0" to "9" | "a" to "f")
string HexColor = "#" (HexDigit[3] | HexDigit[6])  type HexColor = \("#" (HexDigit[3] | HexDigit[6]))
string NotDigit = !\d                              type NotDigit = \(!d)
string Greeting = "hello" | "hi"                   type Greeting = "hello" | "hi"     // unchanged (SP6/SP7)

// symbols (SP13/SP14/SP15)
symbol Keyword  = 'if' | 'else' | 'for'            type Keyword  = 'if' | 'else' | 'for'   // bare literal union, no pattern
symbol Ident    = \a \w*                           type Ident    = \symbol(a w*)           // structural: tagged delimiter
                                                   type IdentX   = \symbol(Ident "x")      // reuse is content-only (SP15)

// constrained types: parentheses stay required (SP9, rev 4)
type Positive = int that (~ > 0)
type NonEmpty = string that (len(~) > 0)
```

---

## 5. Cascaded changes

### 5.1 Grammar (`grammar.js` → `make generate-grammar`)

- New opening tokens `\(` and `\symbol(` (the tagged-delimiter family, SP16);
  one `grouped_type`-style balanced rule for the pattern island, shared by
  both — the tag sets only the domain of the resulting type (SP15).
- `pattern_char_class` retired as spelled; bare class names tokenized only in
  pattern scope (SP2/SP3). `.` and `...` move inside the island.
- `concat_type` and its `prec.dynamic(-1)` deleted from `_type_expr` reach;
  `_string_type_expr` becomes the island's interior grammar only (SP8).
- `that_constraint` unchanged: `'that' '(' expr ')'` (SP9 rev 4 — parens
  stay).
- Expected side effect: parser-size reduction (the return-type grammar was
  already restricted for exactly this class of ambiguity —
  `return_type_pattern` comment in grammar.js).

### 5.2 AST builder (`lambda/build_ast.cpp`)

- Pattern-definition detection switches from content analysis to the
  delimiter (SP10).
- The "reject concat outside patterns" diagnostic path is deleted (SP8).
- Class-name resolution inside patterns checks the reserved set first (SP3).

### 5.3 Docs and tests

- `doc/Lambda_Type.md` §Character Classes / §Character Ranges /
  §Occurrence Modifiers / §Pattern Composition / §Complex Pattern Examples —
  all ~15 example blocks migrate; §Symbol Patterns is rewritten per SP13/SP14
  (enumerations → bare literal unions; structural patterns → `\symbol(...)`); `doc/Lambda_Reference.md`
  and `doc/Lambda_Cheatsheet.md` pattern mentions; validator guide if it shows
  pattern syntax.
- `test/lambda/*.ls` scripts using bare patterns or `\d`-classes migrate,
  with their expected `*.txt` goldens (CLAUDE.md rule 8).
- Formal-spec update per SP12.

### 5.4 Explicitly out of scope

- Pattern-matching runtime and full-vs-partial match semantics (unchanged).
- `match` arm semantics (S11.2.1) — arms referencing named patterns work as
  before; only the definition syntax changes.
- Regex-engine features (captures, backreferences, lazy quantifiers) — not
  part of this migration.

---

## 6. Open issues

| ID | Issue |
|----|-------|
| **SPO1** | **DEFERRED to future (ruling 2026-08-07).** Class-name namespace (multi-letter names, Unicode categories) is explicitly out of scope for the migration; only `d w s a` + `.` + `...` ship. The impl plan (`vibe/Lambda_Impl_String_Pattern.md` P0.3, R6) keeps the class-token rule additive so the namespace can open later without re-architecting. |
| **SPO2** | **RESOLVED (rev 2) → SP13/SP14.** `\(...)` is string-only; symbols are excluded from the pattern language. Symbol enumerations are bare literal unions; structural symbol patterns are `symbol(\(...))`. The considered alternatives — prefix keyword forms as domain markers, or a marked delimiter such as `\('...')` — are rejected: both re-introduce a dual-domain pattern language. |
| **SPO3** | **CLOSED by reversal (rev 4) — see SP9.** The boundary sweep it mandated is preserved as the evidence for keeping the parens: (i) map-field comma — `{x: int that ~ > 0, y: int}`; (ii) `fn` return position followed by the body brace — the position the grammar already restricts (`return_type_pattern`, "avoids ambiguity with map_type in `fn () T { ... }`"), where a constraint could itself contain a `{...}` map literal; (iii) the `\|`/`&` cutoff — `\|` is a value operator too (S10.1.1), so `int that ~ > 0 \| string` is grammatical under both readings and would need a memorized precedence ruling; (iv) the `let` initializer `=` and match-arm `:`. Individually patchable; collectively the tell that the constraint would open the whole expression grammar into type space. |
| **SPO4** | **RESOLVED by the impl plan** (`vibe/Lambda_Impl_String_Pattern.md` §4): **atomic cutover, no dual-parse window** — the real corpus is ~7 in-repo scripts + docs, and keeping the bare form alive reproduces the grammar entanglement SP8/SP10 delete. One PR (P0 grammar + P1 AST + P2 runtime + P3 corpus), docs trailing in the same era. |
| **SPO5** | **Enforcement interaction.** Constrained-type predicate *enforcement* is still open (SO9); nothing in this doc (least of all SP9, which now changes no spelling at all) may be read as closing SO9. |
| **SPO6** | **DISSOLVED (rev 3).** Asked what `symbol(P)` applied to a type value means (conversion fn vs AST-recognized constructor). The rev-3 tagged delimiter `\symbol(...)` (SP14/B5) needs no answer: `symbol()` stays purely the value-level conversion, and the domain is carried by the pattern literal's own opening token. |
