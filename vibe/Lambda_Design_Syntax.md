# Lambda Design: Syntax — Line Delimiters and Expression Continuation

**Status**: RATIFIED 2026-08-21 as `S16 Surface Syntax`
(`doc/Lambda_Formal_Semantics.md`), amended through **spec v10.0.0**
(2026-08-22). Partially implemented — most S16 rulings still carry the `*`
mark; see Appendix A for the per-ruling conformance list. §7 audit rulings
(points 19–31, 33) decided 2026-08-21; §7.13 fully resolved; §7.14
(closed-tail juxtaposition) and §7.15 (relative path `\.a.b`) decided.
2026-08-27: handler-arm brace placement reaffirmed with rationale (§5.9,
ledger 16; spec v15.2.1 records it in S16.4.3).

> **The body states current rulings only.** Superseded wording has been
> moved out to **Appendix S — Superseded Rulings**, struck through and
> annotated with what replaced it. Nothing there is normative. Currently
> held: S.1 the blanket dual-role rule (§3.2/§3.3, replaced by §7.14) and
> S.2 the `{ ... }` Options 1 and 2 (§5.9, replaced by v3).
>
> **Ratification erratum (2026-08-22).** §5.9's rejected **Option 2** was
> ratified into the spec as S16.4.1 by mistake, making `if (c) {a: 1}` a
> syntax error and prescribing `if (c) ({a: 1})` as its repair. §5.9 **v3**
> was and remains the ruling: **interior decides, and grouping parens never
> flip the reading**. Corrected in spec v10.0.0 as S16.4.1v2 + S16.4.2 +
> S16.4.3. The C parser still implements the erratum for the paren-head
> spelling (Appendix A).

**Scope**: Statement separation, the role of the line break, and cross-line
expression continuation in Lambda surface syntax.
**Spec linkage**: This document is the decision record; **`S16` is the
authority.** Cite `S16.#` in discussion and downstream docs, not this
document's section numbers. Map: §3.1 → S16.1.1, §3.2 → S16.1.2–S16.1.3v2 +
S16.2.1, §3.3 → S16.2.2v2–S16.2.3v2 + S16.2.5, §3.4 → S16.2.4v2, §3.6 →
S16.2.6, §3.8 → S16.3.1, §5.9 → S16.4.1v2–S16.4.3, §5.10 → S16.5.1,
§5.1–§5.6 → S16.6.1–S16.6.5, §7.1–§7.2 → S16.8.1–S16.8.2, §7.3–§7.5 →
S16.8.3, §7.9 → S16.8.4, §7.12 → S16.8.5, §7.10 → S16.8.6, §7.8 → S16.8.7,
§7.13 → S16.8.8, §7.6 → S16.9.1, §7.7 → S16.9.2, §7.11 → S16.9.3, §7.15 →
S16.9.4, §7.22 → S16.9.5, §7.14 → S16.1.3v2/S16.2.3v2, §7.23 → S16.7,
§7.24 → S16.10, §7.25 → S12.3.7.
**Not ratified into S16 (process, not syntax):** ledger 18 (authority order)
and 32 (two parsers, §4.4) stay here. A future formal syntax document is
tracked as `SO35`.

---

## 1. Prior Art

How existing languages decide where one statement ends and the next begins,
and what goes wrong in each model.

### 1.1 C / C++ / Java / Rust — explicit `;`, free-form whitespace

Every statement is terminated by `;`; newlines carry no meaning. The model is
fully predictable and whitespace-insensitive.

**Issues.** Verbosity (`;` on every line). A *forgotten* `;` is usually a
parse error, but not always: `a = b` followed on the next line by `(f)(x)`
merges into `a = b(f)(x)` — silently, if the types happen to work. C survives
mostly because merged forms are usually type errors; a dynamically-flavored
language cannot rely on that. Rust adds a second trap: `;` is a *terminator*
that discards the value, so `{ a; b }` and `{ a; b; }` differ (`b` vs unit) —
a persistent source of confusion in an expression-oriented language.

### 1.2 JavaScript — Automatic Semicolon Insertion (ASI)

Newlines are usually insignificant, but the parser *inserts* a semicolon when
the next token cannot continue the current statement, plus special "restricted
production" rules.

**Issues.** The two failure modes are both silent:

- **The `return` trap** (restricted production): a newline directly after
  `return` inserts a semicolon, so
  ```js
  return
      value;
  ```
  returns `undefined` and `value` is dead code.
- **The line-start `(` / `[` / `` ` `` / `+` / `-` / `/` hazard**: a line
  beginning with a token that *can* continue the previous expression is merged
  into it — `a = b` then `(f)(x)` becomes a call. Semicolon-free JS style
  survives only because its users memorize the hazardous leading tokens and
  write defensive leading semicolons.

JS's *optional* `;` with automatic insertion is the worst point in the design
space: the delimiter is sometimes required but the language never says when,
so the user must simulate the parser's insertion rules to know — and both
failure modes (split and merge) are silent. This assessment directly shapes
the rejections in §2.3.

Notably, leading-dot method chains (`promise\n.then(...)`) are the one
juxtaposition JS handles well — `.` cannot start an expression in JS, so a
leading dot is always unambiguous continuation.

### 1.3 Python — newline terminates; explicit and bracket continuation

Newline is a hard statement terminator. Continuation requires either a
trailing `\` or an unclosed bracket (`(`, `[`, `{`), inside which newlines are
ignored.

**Issues.** The `\` continuation is fragile (trailing whitespace after `\` is
a syntax error) and considered poor style; the recommended workaround is to
introduce otherwise-meaningless parentheses. The model is simple and loud —
mistakes are syntax errors, not wrong answers — but it buys that by making
newlines deeply significant, and it needs the bracket exception to be livable.

### 1.4 Go — lexer-level semicolon insertion

The lexer inserts `;` at each newline whose *last token* could end a
statement (identifier, literal, `)`, `}`, `++`, etc.). Effectively:
"a complete-looking line is a statement."

**Issues.** The rule is simple and mechanical, but it silently *splits*:
`return` on its own line becomes a bare return. Go converts that from silent
bug to loud error only via a second, unrelated check (unreachable-code
analysis) — and because a bare expression is not a legal Go statement, most
accidental splits fail to compile. Go also forces brace style (`{` must not
open a line) as a side effect of the insertion rule. An expression-oriented
language cannot copy this model: bare expressions *are* legal statements, so
accidental splits would be legal, silent, and wrong.

### 1.5 Lua — fully free-form, no newline significance

Statements are self-delimiting; `;` is optional decoration. Works because
expressions are not statements (only calls and assignments are).

**Issues.** The famous ambiguity: `a = b` followed by `(f)(x)` — Lua declares
this a syntax error ("ambiguous syntax near '('") rather than pick a side.
Even with its restricted statement grammar, free-form juxtaposition could not
be made unambiguous; Lua chose loudness, which is the right instinct.

### 1.6 Ruby / Julia / Kotlin / Swift — the complete-expression rule

Newline terminates a statement *only if* the expression is syntactically
complete; a trailing operator or unclosed bracket continues to the next line.
Kotlin and Swift additionally special-case leading-dot chains
(`list\n.filter { }\n.map { }`) as continuation.

**Issues.** This is the modern mainstream, and the least surprising model in
practice. Its residual traps: a *leading* binary operator silently starts a
new statement (`a` then `+ b` is two statements — Ruby warns, Julia doesn't),
and the user must internalize "complete vs incomplete" to predict behavior.
The meaning of a program still depends on where its newlines fall.

### 1.7 Summary of failure modes

| Model | Forgotten/omitted delimiter | Newlines change meaning? |
|---|---|---|
| C-family explicit `;` | Usually loud, occasionally silent merge | No |
| JS ASI | Silent split (`return`) and silent merge (`(`-lines) | Yes |
| Python | Loud | Yes (by design) |
| Go | Silent split (masked by statement restrictions) | Yes |
| Lua | Loud (ambiguity error) | No |
| Ruby/Julia/Kotlin/Swift | Silent split on leading operator | Yes |

No mainstream model achieves both "newlines never change meaning" and "all
mistakes are loud" for an expression-oriented language. That combination is
the goal of the design in §3.

---

## 2. Lambda Today

### 2.1 Current behavior

Lambda currently implements the Ruby/Julia-style complete-expression rule,
via the `'statement_end'` precedence tier and an explicit high-precedence
linebreak token in `grammar-common.js` (`_statement`:
`prec.right('statement_end', seq($._content_expr, choice(token(prec(10, /\r\n|\n/)), ';')))`),
racing against `/\s/` in `extras`.

Verified behavior (probes run 2026-08-21 on master):

| Input | Result | Reading |
|---|---|---|
| `1 +` ⏎ `2` | `3` | incomplete expr continues across the newline |
| `let b = 1` ⏎ `+ 2` | `2` | complete expr terminates; `+2` is a new (final) statement |
| `let a = f` ⏎ `(1 + 2)` | `3` | complete expr terminates; `(1+2)` is a new statement |
| `pn main() { return` ⏎ `42 }` | returns null | **the JS `return` trap exists in Lambda today** |
| `"a" <b "hi">` (one line) | element parses | string/map/element content juxtaposition (`repeat1` class) |
| `x <b "hi">` (one line) | **parse error** | identifier + `<` goes down the relational path and dies |
| `x` ⏎ `<b "hi">` | element parses | the newline is what rescues expr-then-element |

### 2.2 Issues

1. **The `return` trap** — silent null return, dead value line (probe above).
2. **Silent splits** — a leading `(`, `[`, `-`, `+` line starts a new
   statement; in an expression-oriented language where the last expression is
   the block's value, a split silently changes the *value*, not just effects.
3. **Grammar fragility** — the `statement_end` tier plus the
   `token(prec(10, /\r\n|\n/))`-vs-`extras` race is the most delicate
   machinery in the grammar; the element rule needs its own
   `optional(choice(linebreak, ';'))` seam.
4. **Meaning depends on newlines** — the same token stream parses differently
   depending on line breaks (`x <b "hi">` vs `x` ⏎ `<b "hi">`), which
   complicates generated code, embedding, and reasoning about programs.

### 2.3 Alternatives explored and rejected

Two other directions were worked through in the 2026-08-21 design discussion
before settling on §3. Both achieve full whitespace-insensitivity at the
parser level; both were rejected for the same root defect — **silent
behavior** — and both compile to *the same greedy parser*, differing only in
doctrine.

**Rejected A — drop the newline delimiter; `;` optional, greedy
continuation.** `{ expr expr expr }` parses by maximal munch: a statement
extends as long as the next token can continue it, and the user writes `;`
"when there is ambiguity". Rejected because:

- The user must simulate the parser to know where `;` is needed — the JS
  optional-`;` problem (§1.2) reproduced exactly, with the same memorized
  hazard list (`(` `[` `.` `-` `+` `*` `!` `^` `/` `<` and the word
  operators).
- Omission at a hazard point is a **silent merge**: `let a = f` ⏎ `(1 + 2)`
  becomes `f(1+2)`, and in an expression-oriented language the block's
  *value* changes silently, not just its effects.
- The `<` collision loses its newline shield: `title` ⏎ `<div ...>` — the
  bread-and-butter template shape — is forced into the relational reading
  (a parse error at best, a silent comparison at worst; see the §2.1 probe).
- Migration of the existing corpus would be a *silent* meaning-change sweep,
  unauditable without semantic diffing.

**Rejected B — C-style mandatory `;`, relaxed by safe juxtaposition.** `;`
required between statements even across lines; then omitted where the next
statement's leading token cannot continue an expression (keyword-led
statements, literals, identifiers). Better doctrine than A — "`;` is the
rule" is teachable, and `return` ⏎ `expr` inverts to `return expr`, fixing
the JS trap. Rejected because:

- The mandatory-`;` rule is **unenforceable exactly where it matters**: with
  whitespace fully insignificant, a forgotten `;` before a line-start `(`,
  `[`, `-`, … cannot produce an error — the parser has no newline left to
  see, so it silently merges, same as A. The rule exists only on paper.
- Since the safe-juxtaposition class is precisely the complement of the
  ambiguous class, the resulting grammar is byte-identical to A's. The
  choice between A and B was never between two parsers — only between two
  documentation stances over the same silent failure mode.
- Same `<`/element and migration problems as A.

The lesson both rejections teach: whitespace-insensitivity is achievable, but
*silence is not acceptable as its price*. The adopted design (§3) keeps the
whitespace-insensitivity goal and pays for it with loud errors instead —
newlines retain exactly one job, arming an ambiguity check, and lose all
meaning-carrying roles.

---

## 3. New Design: Newline as Continuation Barrier

### 3.1 Design goals

1. **Whitespace insensitivity.** Line breaks carry no meaning of their own.
   The acceptance test: **replacing every line break in a valid Lambda script
   with a space must yield a script with the same semantics.** (The converse
   direction may reject — inserting a line break can turn a valid program
   into a syntax error, per Rule 3 — but it can never change what an
   accepted program means.)
2. **Natural multi-line expressions.** An expression can span multiple lines
   without ambiguity and without pitfalls — no JS `return` trap, no
   line-start hazard list to memorize, no Python-style `\` continuation.
   Where a line break is illegal mid-expression, the parser says so and names
   the fix; it never guesses.
3. **Minimal `;`.** Semicolons are required only where genuine ambiguity
   exists; everywhere else, statements juxtapose freely (keyword-led
   statements, literal content, identifier-led expressions). Typical code
   needs no `;` at all.

Two enforcement properties realize these goals — the combination no
prior-art model (§1.7) achieves:

- **Loud, never silent**: a forgotten or misplaced delimiter is always a
  syntax error — never a silent merge (C's failure, rejected option B's
  failure) and never a silent split (JS/Go's failure, and Lambda's today).
- **Refuse to guess**: where a line-start token has two readings, neither
  wins by default (the Lua instinct, §1.5, generalized).

### 3.2 Core rules

**Rule 1 (strict separator).** `;` is a statement *separator*, and the
separator discipline is total and uniform for `;` and `,` alike: **a
separator sits between two items — it never dangles and never marks an
empty slot.** (Probes 2026-08-21 confirm the `,` half is already current
behavior; the `;` half tightens today's terminator-style tolerance.)

No trailing separators:

```
{ a; b; }           // SYNTAX ERROR — trailing ';'   → { a; b }
[1, 2,]             // SYNTAX ERROR — trailing ','   → [1, 2]
{a: 1,}             // SYNTAX ERROR — trailing ','   → {a: 1}
f(a, b,)            // SYNTAX ERROR — trailing ','   → f(a, b)
```

No interior empty slots:

```
{ a; ; b }          // SYNTAX ERROR — empty statement slot
[1, , 2]            // SYNTAX ERROR — empty element slot
{a: 1, , b: 2}      // SYNTAX ERROR — empty map-item slot
f(a, , b)           // SYNTAX ERROR — empty argument slot
```

Rationale. An interior empty slot is always evidence of a mistake or an
attempt at elision/argument-skipping, and both deserve loudness: JS array
elision (`[1, , 2]` as a sparse array) is the cautionary tale, and silently
collapsing `f(a, , b)` to `f(a, b)` would rebind positional arguments — a
silent meaning change, which this design forbids everywhere. Trailing
separators then fall to the same doctrine rather than earning an exception:
under this design `;` is a *disambiguator*, not a terminator, and before a
`}` there is nothing to disambiguate — a trailing `;` is pure noise, and
banning it in the middle while allowing it at the end would be the
inconsistency. Since separator-as-terminator semantics were already
rejected (the Rust model, where a trailing `;` silently discards the block
value), the trailing `;` had no semantic function to lose.

The cost is C-family muscle memory (`{ a; b; }`, and the diff-friendly
trailing comma in multi-line literals). It is mild here: idiomatic Lambda
code under this design contains almost no semicolons at all — statements
juxtapose (Rule 3), so the terminator habit is rarely triggered — and the
trailing-comma ban is today's status quo, not a regression. The error is
loud with a one-character fix, and the Erlang-style reordering friction
that strict separators are known for applies mainly to `,` lists, where it
already exists today.

Uniformity bonus: `;` never attaches to any statement form, so bare
`return` needs no special spelling. `return; stmt` mid-block is an ordinary
bare return followed by a separator and the next statement; at block end,
bare return is simply `return }` — and `{ return; }` is the ordinary
trailing-separator error. No carve-out exists (contrast `apply;`, which
remains a single token by prior design).

**The separator landscape.** Every separator context in the language, under
the one rule:

| Context | Banned form | Write instead |
|---|---|---|
| statement block | `{ a; b; }` | `{ a; b }` |
| array | `[1, 2,]` | `[1, 2]` |
| map | `{a: 1, b: 2,}` | `{a: 1, b: 2}` |
| call arguments | `f(a, b,)` | `f(a, b)` |
| fn/pn parameters | `fn f(a, b,)` | `fn f(a, b)` |
| arrow-fn head | `(a, b,) => e` | `(a, b) => e` |
| let/var list | `let a = 1, b = 2,` | `let a = 1, b = 2` |
| element attributes | `<div a:1, b:2,>` | `<div a:1, b:2>` |
| import list | `import a, b,` | `import a, b` |
| interior slot (any of the above) | `{ a; ; b }`, `[1, , 2]`, `f(a, , b)` | remove the empty slot |

Prior art on the same landscape — the choices other languages made, and
what each costs:

| Language | Trailing separator | Interior empty slot |
|---|---|---|
| JS | `,` allowed | `[1, , 2]` = **sparse array** (elision) — the silent-meaning trap |
| JSON | banned | banned — strict, hand-editing friction is the known complaint |
| C | **split rule**: allowed in initializers/enums, banned in calls | banned |
| Python / Rust / Swift | allowed everywhere | banned |
| Go | **required** (multiline) | banned |
| Erlang | banned (strict separators) | banned — the famous reorder-the-last-clause friction |
| **Lambda (this design)** | **banned** | **banned** |

Lambda lands on the JSON/Erlang corner deliberately: the reorder-the-last-
item friction (touching the previous line's separator) is the accepted,
known cost, paid for total uniformity. The two alternatives were each
considered and rejected in design: trailing-`;`-only (an unprincipled
exception once interior slots are banned) and a closer-shape rule allowing
trailing `,` before `]`/`}` but not `)` (C's split, generalized — rejected
for carrying two rules where one suffices). Note that C itself demonstrates
the split rule is livable but never clean: fifty years on, its users still
look up which contexts take a trailing comma.

**Rule 2 (incomplete expressions always continue).** If the expression before
a newline is syntactically incomplete — it ends with an infix operator, an
unclosed bracket, a keyword awaiting its form (`match x` before `{`,
`if cond` before the body) — the next line continues it unconditionally.
This is current behavior (`1 +` ⏎ `2` → `3`) and is unchanged.

**Rule 3 (line-start classification after a complete expression).** When the
expression before a newline is complete, the first token of the next line is
classified into exactly one of three classes:

- **Continuation tokens** — tokens that can *only* continue an expression and
  can never start one. The expression continues; this is legal multi-line
  style.
- **Start tokens** — tokens that can *only* start a new expression/statement
  and can never continue one. A new statement begins; no `;` is needed
  (safe juxtaposition).
- **Dual-role tokens** — tokens that could do either. **Syntax error, but
  only expression → expression.** After a *closed-tail* statement — a
  brace-closed declaration, `import`, and the rest of the closed set — a
  dual-role-led expression juxtaposes with no `;` (§7.14). Where the error
  does apply, the user must either write `;` (to start a new statement) or
  move the token to the end of the previous line (to continue). There is no
  default. *(The original blanket form of this rule is in Appendix S.1.)*

Rule 3 is the heart of the design: ambiguity is resolved by *refusing to
guess*, which is what makes whitespace insensitivity (goal 1) and loud-never-
silent enforcement simultaneously achievable.

### 3.3 Token classification

The token *classes* below are normative. Their **application** is scoped by
§7.14: a line-start dual-role token is an error only expression → expression,
never after a closed-tail statement.

**Continuation tokens (allowed at line start; expression continues):**

```
|>  |  &  ?  .?  **  ++  %
and  or  to  in  is  at  div  that
eq  ne  lt  le  ge  gt
==  !=  <=  >=  >  =
```

Rationale: none of these can begin an expression, so a leading occurrence is
unambiguous continuation. Two entries carry a note:

- **`>`** is a continuation token outside element scope; inside an
  element, the symbol relationals are not operators at all (§5.10), so
  `>` there is unconditionally the **element terminator** — which
  sanctions the closing-`>`-on-its-own-line style (analogous to `}` on
  its own line):

  ```
  <div class: "body";
      "content here"
  >
  ```

  Outside elements, `a` ⏎ `> b` continues the comparison. (`>=` is a
  distinct token; as a comparison it exists only outside element scope,
  where it continues like the rest of the pure-infix set.)
- **`=`** is admitted so an assignment may break after its target
  (`target` ⏎ `= value`). A line-start `=` after a non-assignable
  expression fails loudly at the invalid-target check.

This sanctions the natural multi-line pipe style:

```
data
|> normalize(~)
|> validate(~)
that ~.score gt 0
```

**Start tokens (allowed at line start; new statement, no `;` needed):**

identifiers, numeric literals, strings, symbols, datetime/binary literals,
`{` (map), and all statement keywords (`let`, `var`, `if`, `for`, `while`,
`fn`, `pn`, `type`, `view`, `edit`, `match`, `return`, `raise`, `break`,
`continue`, `import`, `pub`). None of these can continue a complete
expression (Lambda has no juxtaposition application), so a leading occurrence
is unambiguously a new statement:

```
{ let x = 1 let y = 2 x + y }     // legal, no ';' needed
```

**Dual-role tokens (error at line start after a complete *expression* — see
§7.14 for the closed-tail exemption):**

| Token | Continuation reading | Start reading |
|---|---|---|
| `(` | call arguments | parenthesized expr / arrow-fn head |
| `[` | index | array literal |
| `-` `+` | binary arithmetic | unary sign |
| `*` | multiplication | unary spread |
| `!` | set exclusion | unary not |
| `^` | postfix propagation / handler | current-error atom (in handler bodies) |
| `/` | division | rooted path `/.seg` |
| `<` | relational | element literal (§3.5) |
| `.` | member access | path expr / float literal (§3.4) |

The error message must name both repairs, e.g.:

> `'(' at the start of line N cannot continue the expression on line N-1.`
> `Write ';' before it to start a new statement, or move '(' to the end of line N-1 to continue the call.`

**Consequences worth stating explicitly:**

- `let a = f` ⏎ `(1 + 2)` — **error** (today: silent split; C-model: silent
  merge). This single case is the clearest demonstration of the design.
- `return` ⏎ `42` — now means `return 42`: literals are start tokens, but
  `return` with an optional operand consumes the following expression
  greedily (Rule 2 territory — `return` awaiting an optional value binds a
  following start-token line as its value). A bare return is `return`
  followed by a separator or the closing brace: `return; stmt` mid-block,
  `return }` at block end (`{ return; }` is the Rule 1 trailing-separator
  error). The JS trap is fixed *by inversion*: users who intend a value get
  the value; intending no value costs one explicit `;` only when another
  statement follows.
- Relational chains must break trailing: `a <` ⏎ `b` is legal;
  `a` ⏎ `< b` is an error (`<` is dual-role).

### 3.4 Special handling: `.`

`.` has three line-start readings: member access (continuation), a path
expression (`.seg` — start), and a float literal (`.5` — start). It gets a
finer split, decided by lookahead past the dot:

- **`.ident(` — member *call* continuation. Allowed.** Path bodies have no
  call syntax (paths are dotted names, wildcards, and indices — parsed by
  `parse_path_expr.cpp`), so a dot followed by an identifier followed by `(`
  cannot be a path expression and cannot be a float. It is unambiguously a
  method-style call on the previous expression. This sanctions the fluent
  chain style, which is common enough to deserve first-class support:

  ```
  data
  .filter(fn (x) => x > 0)
  .map(fn (x) => x * 2)
  ```

- **`.ident` (no following `(`) — syntax error.** Ambiguous between member
  access (`expr.name`) and a new path-expression statement (`.name`). Paths
  are a core Lambda feature and must remain writable at statement start, so
  neither reading may win silently. Write `;` ⏎ `.name` for the path, or
  `expr.` ⏎ `name` (trailing dot) — or keep it on one line — for the member.

- **`.digit` — syntax error.** Ambiguous between float literal (`.123` is a
  number) and integer member access (`expr.123` is legal member syntax).
  Same repair options.

- **`.?` — continuation.** Already in the continuation set; cannot start an
  expression.

The lookahead is two tokens past the dot (`ident` then `(`), with same-line
adjacency required for the `(` — `.func` ⏎ `(x)` does not qualify (the `(`
falls under the dual-role rule on its own line).

### 3.5 Special handling: `<`

`<` at line start after a complete expression is a **syntax error**, never a
relational continuation and never an element. This is the load-bearing case
for Lambda's document idiom:

```
let title = compute()
title;              // ';' required: '<' below cannot follow a complete expr
<div class: "body"; content >
```

Without the error, `title` ⏎ `<div ...>` would be forced into the relational
reading (which is exactly what happens on one line today — probe §2.1 —
where `x <b "hi">` is a parse error via the relational path). The `;` tax
falls on every expr-then-element and declaration-then-element boundary
(a `let` initializer is a greedy trailing expression, so
`let title = "Hi"` ⏎ `<div>` also needs the `;`). In practice content
elements mostly follow strings, elements, or maps — all start tokens with no
relational reading — so the tax is confined to boundaries where an
identifier-ish expression precedes markup.

At genuine statement-start positions (block start, after `;`, after a
keyword-led statement, inside element content after another element/string),
`<` begins an element as today.

### 3.6 Special handling: `^` and `{`

- A handler's brace must open **on the same line as its `^`**:
  `expr ^ { ... }`. A line-start `{` never continues a complete expression
  (a new statement — map or block expression by interior, §5.9), and a
  trailing `^` followed by a line-start `{` is the propagate/handler
  ambiguity — resolved as an error by the same-line requirement.
- Line-start `^` after a complete expression is dual-role (postfix
  propagation vs the current-error atom inside handler bodies) and therefore
  an error; propagation binds tightly and reads naturally on one line
  (`f(x)^`), so nothing of value is lost.

### 3.7 What this deletes

- The `'statement_end'` precedence tier and the
  `token(prec(10, /\r\n|\n/))`-vs-`extras` race in `grammar-common.js`.
- The `optional(choice(linebreak, ';'))` seam in the `element` rule and the
  linebreak alternative in `_import_stam` sequencing (both become plain
  optional `;`).
- The entire class of silent split/merge behaviors documented in §2.2.

Newlines remain *significant* in exactly one way: they arm the Rule 3
classification. They never carry meaning of their own — replacing every
newline in an accepted program with a space yields the same parse
(dual-role errors simply become the one-line reading, which by construction
is what the multi-line program already meant, or was already an error).

### 3.8 Fate of expression juxtaposition: general form dead, reduced form alive

This design permanently settles what `expr expr expr` means in Lambda.

**The general form is dead.** A juxtaposition *construct* — one where
adjacent expressions combine into a computed result, as in ML/Haskell
function application (`f x y`), unit suffixes (`3 px`), or any future
delimiter-free operator form — is **no longer possible** in Lambda. Rule 3
assigns the `expr expr` parse to statement separation: a start token after a
complete expression begins a *new statement*, unconditionally. That claim on
the syntax space is total and irreversible — retrofitting a combining
juxtaposition later would change the meaning of every existing block, so the
door is closed by this design, not merely left unopened. (This also
retroactively answers the question that opened the 2026-08-21 discussion:
the proposed general `expr expr expr` construct was already unviable against
the pre-existing grammar — dual-role tokens, the `<`/element collision, and
content juxtaposition all conflicted — and this design converts that
de-facto unviability into a de-jure ruling.)

**The reduced form is alive — juxtaposition as *sequencing*.** Adjacent
expressions with no delimiter remain legal wherever they read as a sequence
of separate items rather than a combined value:

- **Statement juxtaposition** (Rule 3 start tokens): consecutive statements
  need no `;` when each begins with a token that cannot continue its
  predecessor — `{ let x = 1 let y = 2 x + y }`, `f(1) g(2)`.
- **Content juxtaposition**: document content composes adjacent items
  freely — `"Hello " name "!"`, strings/maps/elements in element bodies
  (today's `repeat1(choice(string, map, element))` class in
  `grammar-common.js`, which Rule 3 generalizes to all start-token-led
  items).

The dividing line is semantic: juxtaposition may *sequence* (each expression
evaluated as its own statement or content item) but may never *combine*
(no juxtaposed form denotes a value computed from its neighbors). Any future
construct needing operand adjacency must use an introducer keyword, a sigil,
or explicit delimiters instead.

---

## 4. Implementation Notes

### 4.1 Grammar / scanner

The Rule 3 check needs to know whether a newline precedes the current token —
information that `/\s/` in `extras` currently discards. The standard
Tree-sitter technique (used by the Swift and Julia grammars) is an external
scanner that tracks newline-crossing and exposes zero-width guard tokens:

- a `_same_line` guard gating the dual-role continuations: `call_expr`'s
  `(`, `index_expr`'s `[`, `member_expr`'s `.` (except the `.ident(` form),
  `propagate_expr`/`handler_expr`'s `^`, binary `- + * ! / <`;
- a `_member_call_dot` token for the §3.4 fluent-chain form, emitted when the
  scanner sees `.` `ident` `(` with the required adjacency (this sits
  naturally in the existing scanner seam — `src/scanner.c` already owns the
  dot-adjacent `path_body_token`, so `.`-form disambiguation logic is
  already concentrated there);
- the continuation-token set needs no guards at all (they are unambiguous in
  any position).

The production grammar already carries a seven-token external scanner
(`grammar.js` externals), so this extends existing architecture rather than
introducing it. Both `grammar.js` and the reference `grammar-lambda.js` must
gain the guards; `make grammar-sync-check` keeps them aligned.

**Authority order (decided, revised 2026-08-21): spec/design doc →
Tree-sitter grammar → C parser.** Each artifact is downstream of the one
before it and is never a source of rulings. Where an implementation
behaves differently from this document, the implementation is wrong and
gets fixed; its current behavior is never evidence for what the language
should do, and never earns a documented exception. The same order governs
the reverse direction: a rule stated here must land in `grammar.js` before
the C parser is asked to enforce it. Note that *authority* order and
*execution* order are deliberately opposite — the production parser sits
last in authority and first in execution (§4.4).

### 4.2 Diagnostics

The design's usability lives in its error messages. Each dual-role rejection
must report: the offending token, the line whose expression it cannot
continue, and *both* repairs (insert `;` / move token up). The `<`-element
case should additionally recognize element-shaped lookahead
(`<ident` + attr/content shape) and phrase the message in element terms.

A `build_ast` dead-code check for statements after `return` in the same block
remains worth adding independently (belt-and-braces for the `return;` form).

### 4.3 Migration

Because every behavioral difference from the current syntax surfaces as a
syntax error (loud-never-silent, §3.1), migration of the `.ls` corpus is
mechanical: parse
everything, fix each reported line. There is **no silent breakage class** —
the decisive advantage over both the greedy no-delimiter model and the
C-style mandatory-`;` model, either of which would have changed the meaning
of existing programs without a diagnostic. Expected dominant fixes:

1. `;` before content elements that follow an expression or declaration
   (§3.5) — the most common edit.
2. Rehanging line-start `(`/`[`/`-` continuations or splits to the trailing
   position, or inserting `;`.
3. Removing trailing `;` before `}` — today `;` is a statement terminator,
   so `{ ...; }` shapes are common in the corpus; each becomes a loud Rule 1
   error with a one-character fix.
4. `return` ⏎ `value` sites: today these return null; after migration they
   return the value. Any script that *depended* on the trap gets a behavior
   change — but such sites are bugs by construction, and the corpus sweep
   will enumerate them (they now parse as `return value`, so they must be
   found by review of the migration diff, the one place where care is needed).

### 4.4 The two parsers: production C, reference Tree-sitter

Lambda now has **two parsers, with different jobs**, and S16 must be
implemented in both.

**The C parser is production.** `lambda/runtime/parser/` — a hand-written
lexer (`lambda_lexer.c`) plus recursive-descent/Pratt parser
(`lambda_parser.c`) behind a C ABI — parses source **directly to AST**
through a reduction sink (`lambda_rd_parse_source` →
`direct_ast_reduce` in `build_ast.cpp`), with no CST in between. It is the
default path; every script the shipped binary runs goes through it.

**The Tree-sitter grammar is the official grammar and reference
implementation.** Its jobs are (a) to state the surface syntax normatively
in one readable artifact, and (b) to serve as the cross-checking oracle for
the C parser — the `compare_parser` lane in `runner.cpp` already parses the
same source with both and reports any source the C parser accepts and
Tree-sitter rejects. Tree-sitter also still backs the S-expression emitter
and REPL paths.

**Consequences for this implementation:**

- **Back to a single `grammar.js`.** The production/reference grammar split
  (`grammar.js` + `grammar-lambda.js` + `grammar-common.js`, guarded by
  `make grammar-sync-check`) existed to keep the *shipped* parse tables
  small. With Tree-sitter no longer in the production path, parser size and
  speed stop being constraints: the grammar collapses back to one file that
  states the whole language, and the sync check retires with it.
- **The sub-language extraction externals retire too.** The seven scanner
  tokens (`type_pattern_token`, `primary_type_pattern_token`,
  `pattern_island_token`, `content_type_token`, `return_type_token`,
  `view_pattern_token`, `path_body_token`) exist for the same size reason —
  they hand whole sub-languages to Lambda-side parsers. Type patterns,
  view patterns, and path bodies become ordinary Tree-sitter rules again,
  which is also what makes the grammar readable as a normative statement.
  This moots most of O3: the scanner's private continuation set for type
  patterns disappears along with the token, though **the same conformance
  question moves to the C lexer**, which must apply S16.2.2/S16.2.3
  uniformly across type and value space.
- **But a small external scanner must remain.** S16's line-start
  classification needs to know whether a newline preceded a token, and a
  pure `grammar.js` cannot see that: `/\s/` lives in `extras` and is
  invisible to grammar rules. So the scanner survives with an entirely
  different job — no longer sub-language extraction, but the S16 guards:
  `_same_line`, `_stmt_boundary`, `_not_paren` (§5.2), and the `{`
  pre-classification tokens (`_map_open` / `_block_open` / `_empty_braces`,
  §5.9). That is a much smaller scanner than today's, and its logic is
  mechanical lookahead rather than sub-language scanning.
- **The C parser has the easier side of S16.** Its lexer already emits an
  explicit `LAMBDA_TOK_NEWLINE`, so "did a newline precede this token" is
  directly available — the guard machinery Tree-sitter needs a scanner for
  is a token-stream test there. The two implementations will therefore
  *look* different while implementing identical rules; the `compare_parser`
  lane is what keeps them honest.
- **Cross-check strengthens into the conformance test.** Since the grammar
  is the reference, the compare lane should run over the whole `.ls` corpus
  during the S16 migration and be extended to flag the reverse direction
  too (source Tree-sitter accepts but the C parser rejects), not just the
  direction it checks today.

---

### 4.5 Implementation status (2026-08-21)

**Tree-sitter grammar: implemented and verified.** The three-file split is
collapsed into one `grammar.js` stating the whole language, and
`src/scanner.c` is rewritten from sub-language extraction to the S16 guards.
A 50-case conformance harness (accept *and* reject cases across S16.1–S16.6
and §7.1–§7.11) passes 50/50.

*Design change forced by the implementation.* The guards were first written
as a single zero-width `_join` marker, as §4.4 anticipated. That fails: a
zero-width token pushes the precedence-deciding operator two symbols out of
lookahead range and breaks LR(1) resolution for the whole operator tier
(`a ++ b • _join …` cannot choose between reduce and shift). The working
design makes each guarded operator its own **consumed** external token —
`_bin_plus`, `_bin_minus`, `_bin_star`, `_bin_slash`, `_bin_lt`,
`_call_lparen`, `_index_lbracket`, `_member_dot`, `_postfix_caret` — emitted
only when the operator shares a line with its left operand, plus the
zero-width `_stmt_boundary` and `_not_paren`. Precedence then resolves at
one-token lookahead as usual. The disjointness argument survives intact: a
guarded operator token and `_stmt_boundary` are never both emissible, so a
line-start dual-role token yields neither and the parse fails loudly.

*Second implementation finding.* `type` had to leave value position. It is
both a base-type keyword (`type(x)`) and a declaration introducer, so under
S16.1.3 juxtaposition `type E { … }` also read as three adjacent statements
— type value, identifier, map — and that reading won. `base_type` keeps
`type` for the type language; value position uses the base-type keywords
without it, and `type(x)` is reinstated as an explicit call form, separated
from a declaration by one token of lookahead (`(` vs an identifier).

*Post-implementation note: the §7.14 closed-tail refinement was decided
AFTER the 50/50 milestone below; both parsers and both harnesses still
implement the blanket statement-boundary rule and need re-tuning.*

**C parser: implemented and verified.** `test/c_s16_conformance.sh` mirrors
the reference harness case for case and passes 50/50, so both front ends
accept and reject the same language. The central move matches S16.1.1
exactly: **line breaks no longer reach the parser at all.**
`parser_next_significant` collapses every NEWLINE run into an `nl_before`
flag on the following token, and that flag is the only thing the S16 rules
consult. Everything else follows from it — the Pratt loop and
`parse_postfix` break on a dual-role token carrying the flag,
`parse_content` implements strict `;` plus juxtaposition, and three
ad-hoc newline heuristics (`newline_starts_root_path`, `newlines_lead_to`,
`assignment_statement_starts`) were deleted outright because the rule now
covers what they approximated. `parse_type_slot`'s annotation boundary
moved to the same flag, which is O3 discharged on the C side: type space
now shares the S16.2.2 continuation set instead of keeping a private one.

**Build integration: done.** `ts-enum.h` regenerates cleanly; the C++ CST
path was migrated as scoped above (retired extraction tokens and their four
now-dead builders removed, `SYM_IF_STAM`/`SYM_FOR_STAM`/`SYM_RAISE_STAM`
dropped, `SYM_WHILE_STAM` remapped to `sym_while_expr`). The Makefile drops
`GRAMMAR_COMMON_JS` for `GRAMMAR_SCANNER_C`, retires `grammar-sync-check`
and `generate-grammar-full` with the two grammar files and their `utils/`
scripts, and gains `make test-grammar-s16`, which runs both harnesses.

**Corpus migration scale.** 21 of the first 60 `test/lambda/*.ls` files
already parse clean; the other 39 exhibit exactly the predicted S16 classes
— most commonly a line-start `[` or `(` after a complete statement
(`fn a() { … }` ⏎ `[pick(2)]`), which is the S16.2.3 error and takes a `;`.
As designed, every one surfaces as a parse error rather than a silent
reinterpretation. A front-end cross-check over the same corpus found the
two parsers agreeing everywhere; the only apparent divergences were the C
parser resolving *imports* into un-migrated library sources — e.g.
`lambda/package/graph/transform/paint.ls:154` still spells the retired
element divider (`else <g;`), which §7.11 replaced with the boundary comma.
The package library is therefore part of the migration surface, not just
`test/`.

**2026-08-24 conformance sweep** (details moved here from the spec's
Appendix A; the spec rows now carry status + pointer only):

- *S16.1–S16.6 harness*: `test/c_s16_conformance.sh` 123/123 against the
  production C parser, `test/ts_s16_conformance.sh` 118/118 against the
  reference grammar. Both defects the spec row previously named are fixed:
  `return` ⏎ *value* now returns the value (S16.2.5), and the type scanners
  no longer continue on a line-start `!` (S16.2.3). The harness is a case
  sample, not a proof, so the `*` marks stand. Residue tracked as O3 (the
  `!` fix for the four sibling Tree-sitter scanners) and §7.17 (the comment
  vs line-start guard, Tree-sitter only, benign); the O4 user-facing doc
  sweep found 60 of 172 `lambda` code blocks in `Lambda_Expr_Stam.md`,
  `Lambda_Reference.md`, `Lambda_Cheatsheet.md`, and `Lambda_Func.md` no
  longer parsing.
- *Points 35–36 (S16.6.6/7) enforcement*: C parser rejects every unbraced
  control-statement body with a repair-naming diagnostic;
  `parse_function_declaration` rejects `pn ... =>` outright (closing a §4.4
  divergence where the C parser was the outlier — 1 doc site migrated, 0
  test/std uses). A misfiring `runner.cpp` heuristic that rewrote
  arrow-body errors into the element-ambiguity diagnosis (the `>` of `=>`
  matched its relation walk-back) was guarded. Verified C 140/140,
  Tree-sitter 135/135 on identical case sets; the 700-file `.ls` corpus
  cross-check showed zero movement (76 pre-existing failures before and
  after).
- *Points 37–38 (S16.6.8/9) enforcement*: `build_ast` semantic analysis
  per §5.6, reported as `E312` — `reject_procedural_block_operand` guards
  tuple/list/array elements, call arguments, binary operands and `=>`
  bodies; `validate_match_branch_homogeneity` /
  `validate_if_branch_homogeneity` enforce point 38. Classification is the
  shared three-way recursive `ast_branch_kind` (see the point-38 addendum
  in §6 for the two shapes a top-level-only first cut over-rejected).
  Migration cost as predicted: 1 doc example (`default: null` beside
  control arms became `default { }`), 0 test/corpus changes. Verified C
  152/152 (E312 detection added — semantic, not parse, rejections),
  Tree-sitter 135/135, `make test-lambda-baseline` 3868/3868; C-suite only
  by design, the reference grammar has no semantic tier.
- *§7.22 (S16.9.5) `a?: T` marker*: the 2026-08-22 spot-check that first
  cleared the ruling was wrong — the marker parsed only on parameters, and
  both a map-type field and an element attribute were rejected with
  `error[E103]`. Both now parse (`parse_type_pattern.cpp`, one shared
  marker helper for the map-field and element-attribute sites), the
  validator honours the marker, and `test/validator_test_data/maps.ls` no
  longer errors against itself; covered by
  `test/lambda/optional_field_marker.ls`. Residue stays in the spec row.

---

---

## 5. Unified `if` / `for` / `while` Forms

The expr/stam split in today's control constructs (`if_expr` vs `if_stam`,
`for_expr` vs `for_stam`) exists largely *because of* the old line-end
delimiter: statement forms needed the `statement_end` machinery to terminate
without `;`, while expression forms had to be greedy expressions — hence the
required `else`, the mandatory parenthesized condition, and the divergent
else-chain rules. With newlines stripped of their delimiting role (§3),
nothing remains for a separate statement form to do. This section unifies
each construct to one node with two interchangeable spellings.

### 5.1 The two spellings

```
if (cond) expr [else expr]      // paren head → body is any expression
if cond { ... } [else ...]      // bare head  → body must be braced

for (decls clauses) expr        // paren head → body is any expression
for decls clauses { ... }       // bare head  → body must be braced

while (cond) expr               // while joins the same pattern
while cond { ... }
```

Both spellings produce the same AST node. The expr/stam distinction moves
out of the grammar entirely (§5.6).

### 5.2 Form commitment: `(` after the keyword

Immediately after `if` / `while`, a `(` **commits to the paren form**: the
parentheses are the construct's delimiters, never the first token of a bare
condition. This gives every program exactly one parse:

- `if (c) x + 1 else y` — paren form; the body expression is fully general
  (`-1`, `(x)`, a call — anything, including tokens that would be dual-role
  at a line start; inside the committed form there is no ambiguity to arm).
- `if x == 0 { ... }` — bare form; the condition runs greedily to the `{`.
- `if (c) { ... }` — paren form with a braced body; identical in meaning to
  the bare-form intuition, so the overlap is harmless.

**Consequence (decided): a bare-form condition must not start with `(`.**

```
if ((a+b)*2) { ... }      // the intended spelling: paren form, braced body
if (a+b)*2 { ... }        // NOT an error — see the correction below
```

*Correction (2026-08-21, from the Tree-sitter implementation).* An earlier
draft of this section claimed `if (a+b)*2 { … }` is a syntax error because
`* 2` "cannot begin a body expression." That reasoning predates §7.10 and
§7.12, which keep `*` (spread) and `+` (identity/coercion) as PREFIX
operators — so `*2` is a perfectly good body expression, and the line
parses as paren-form `if (a+b) *2` followed by a juxtaposed `{ … }` block.
Verified against the generated parser. The commitment rule itself stands
and is implemented (`_not_paren`): `(` after `if`/`while` selects the paren
form, so a bare head may not begin with `(`, and a body that opens with a
continuation-only token — `if (1) == 2 { … }` — is still a loud error.
What cannot be enforced grammatically is the *reader's* misgrouping, since
banning prefix-operator bodies would also ban the legitimate
`if (c) -1 else 1`. This belongs to the diagnostics backlog (§7.13): warn
when a paren-form body opens with a prefix operator on the same line, and
suggest wrapping the condition. This is the mirror image of Go's
composite-literal-in-condition restriction, and it is the entire price of
having two spellings. `for` pays nothing: loop declarations must begin with
an identifier, so `(` after `for` is never ambiguous. `while`'s bare-form
condition carries the same restriction as `if`'s.

### 5.3 The bare-form body boundary: `{` opens the body

Go's quirk exists because its `T{...}` composite literal lets `{` continue
an identifier. Lambda has no expression form in which `{` continues a
*complete* expression (§3.6), so in a bare head, the first `{` after a
complete head expression **deterministically opens the body** — even with
map literals inside the condition:

```
if x == {a: 1} { ... }    // map continues the incomplete '=='; the second
                          // '{' follows a complete cond and opens the body
```

The sole exception is the `^` handler, the one construct where `{` follows a
complete expression: in `if x ^ { h } { body }` the handler binds to the
condition (greedy head, matching existing handler precedence); a user who
wants propagate-then-body writes `if (x^) { body }`.

### 5.4 `else`: optional, continuation-classified, nearest-binding

- **`else` is optional in both spellings** (decided). An absent else yields
  `null` in value position and contributes nothing in content position
  (natural for templates). This retires the ternary-style required-else of
  today's `if_expr`; it is the unification's one *semantic* change and joins
  the S-ruling to be added on ratification (§6-O4).
- **`else` (and `default`, `case`) classify as continuation-only keywords**
  under Rule 3: they cannot start a statement, so a line-start `else` is
  unambiguous continuation — `if (c) x ⏎ else y` is legal, and after a
  now-complete `if a { }` a following line-start `else` still attaches.
- **Dangling else binds to the nearest `if`** (the universal convention).
  It is reachable only in the paren form (`if (a) if (b) x else y`); the
  bare form's mandatory braces structurally prevent it.
- Else-if chains need no special grammar in either spelling: `else`'s
  operand is any expression, and an `if` is an expression.

### 5.5 `match` stays single-form (decided)

`match expr { case ... }` already instantiates the unified bare form — bare
head, braced body — so it is *already consistent* with this design. A paren
form (`match (e) case a: x case b: y`) is rejected:

- The paren form's payoff for `if`/`for`/`while` is a non-braced body. A
  match "body" is an arm *list*, not an expression; the braces are the arm
  list's delimiters. Without them the construct has no terminator, and
  nested matches acquire a dangling-`case` problem — the dangling-else
  hazard multiplied by arbitrarily many arms.
- Adding the paren form would import the §5.2 leading-`(` restriction into
  match scrutinees for zero benefit. As is, `match (a+b)*2 { ... }` stays
  legal, because with no paren form there is no commitment to make.

### 5.6 The expr/stam distinction moves to semantic analysis

Grammar no longer distinguishes expression and statement control forms; the
distinction relocates to `build_ast`, keyed by the existing fn/pn context:

- `break` / `continue` inside a loop body: legal only in procedural (`pn`)
  context; a `build_ast` error in functional context.
- A `for` in functional context is a comprehension yielding content; in
  procedural context with its value unused, an effect loop.
- `while` remains procedural-only — a `build_ast` check, not a grammar rule.

This mirrors how Lambda already treats the fn/pn boundary as semantic rather
than syntactic.

### 5.7 Line-break interplay — the payoff

- `if x ⏎ { ... }` — legal: the head is incomplete, Rule 2 continues.
- `for x in list where p ⏎ { ... }` — same.
- `if (c) x ⏎ else y` — legal via `else`-as-continuation (§5.4).
- `if c { } ⏎ (x)` — line-start `(` after a complete `if`: the ordinary
  Rule 3 dual-role error. No control-flow special cases exist; an `if` is
  just an expression.

### 5.8 Grammar effect

Deleted: `if_stam`, `for_stam`, `while_stam` as separate rules (and their
`_statement` entries); the required-else branch of `if_expr`; the divergent
else-chain alternatives. Retained: the `prec.dynamic` map-vs-block
arbitration in braced bodies (§5.9 v3 interior-differentiation relies on
it; an interim Option-2 draft had deleted it). `match_expr` is untouched.

### 5.9 `{ ... }`: interior decides; context breaks the empty tie; parens never matter (decided, v3)

`{ ... }` is both the map literal and the block form. This ruling reached
its final shape in three steps on 2026-08-21.

Two earlier options were considered and rejected; **Option 2 was ratified
into the spec by mistake** and is the source of the `if (c) ({a: 1})`
erratum. Both are struck out in **Appendix S.2** — do not implement or
ratify them. The three rules below are the ruling.

**The three rules:**

1. **Interior decides, wherever braces are an expression.** A map needs
   `key ':'` at the front, and `ident ':'` occurs nowhere in statement
   space (Lambda has no labels), so the two interiors are *disjoint
   grammars* — `{a: 1}` map, `{let x = 1 x}` block, `{f(1)}` block — a
   decidable two-token choice, not a guess. This applies in every
   expression position **and in control-form bodies of both spellings**:
   `()` never flips the interpretation of `{...}` — `if c {a: 1}` and
   `if (c) {a: 1}` are identical, as grouping parens should be.
2. **Empty `{}` resolves by context, only where a tie exists.** Value
   position (initializers, call args, operands — in `fn` *and* `pn`):
   empty map. Content position: empty map item (meaningful — it
   serializes). `if`/`for` bodies: **fn context → empty map, pn context →
   empty block** — which aligns with value use, since fn control bodies
   produce values and pn control bodies discard them. Bare statement
   position in `pn`: **syntax error** — a bare `{}` statement is dead code
   under either reading (no-op block or discarded map), and this design
   answers meaningless input with a loud error, which also eliminates the
   context rule at statement position entirely.
3. **Declaration braces are structural and never read as maps**: `fn`,
   `pn`, `view`, and `on` bodies, braced match arms (`case T { ... }`),
   handler arms (`^ { ... }`, `~ { ... }`), and `while` bodies — `while`
   belongs here by the same value-use principle: it is procedural-only,
   its body value is always discarded, so a map body is dead by
   construction. `{}` in all of these is the empty body. Each declaration
   has its expression escape where a value-body is wanted
   (`fn f() => {a: 1}`, `case int: {a: 1}`). `match`'s outer braces
   delimit the arm list (S16.6.4); `type` bodies have their own interior
   (§7.11); map-*type* patterns `{a: int}` live in scanner-owned type
   space, untouched.

**Why handler arms are rule 3, not rule 1 (reaffirmed 2026-08-27, user).**
The question was raised whether `expr ^ { ... } ~ { ... }` should move to
the interior-decides group, since semantically it branches exactly like
`if`/`else` and its arms produce values. Ruled no — the line between
rule 1 and rule 3 is a decidable test, not taste:

- **Rule 1 positions all have a bare-expression second form** —
  `if (c) expr else expr`, `case int: expr`, `() => expr`. The body slot
  is a general expression position; braces written there are ordinary
  expressions, so interior must decide.
- **Handler arms have no such form and cannot get one**: `^` is postfix
  propagation, and under §3.3/§3.6 an expression may continue after it —
  `f()^ + 1` propagates, then adds. An unbraced arm `expr ^ arm_expr` is
  therefore grammatically unreachable; the same-line `{` (§3.6, S16.2.6)
  is the *sole* discriminator between propagation and handling. A brace
  that carries that grammar burden is structural by construction, like
  `fn`'s body brace and `case T { ... }`'s arm brace — not like `if`'s.
- The `while` value-use argument does **not** apply here — handler arms
  do produce values (`let v = expr ^ { null }`; the `~` arm is the value
  branch). The placement rests on the structural argument alone.
- Cost accepted: a map-valued arm needs the generic block-expression
  escape, `expr ^ { {retries: 3} }`. The handler is the one rule-3 entry
  with no dedicated expression escape; if map/expression-valued arms ever
  prove common, the consistent fix is to add one in the family of `fn`'s
  `=>` — not to flip the brace reading, which would also drag `^ {}`
  (the natural acknowledge-and-swallow statement handler) into rule 2's
  context-tie machinery.

**Consequences ruled in:**

- **Block expressions exist** (Rust-style): `{statements}` is legal in any
  expression position, its value is its last expression, and its `let`s
  are block-scoped. `let x = { let y = 1 y + 1 }` is legal. This is what
  gives arrow functions block bodies — `(x) => { let y = x + 1 y }` —
  resolving the last §7 audit item with no JS `({...})` quirk.
- **Arrow bodies are fn context by definition, even inside a `pn`**:
  `() => {}` mid-procedure still yields the empty map — the arrow is a
  functional value regardless of where it is written.
- **`for (x in l) {}` in fn context yields one empty map per iteration** —
  legal by rule, almost never intent: a lint warns on an empty-map
  comprehension body.

**The full classification:**

| Braces | Reading |
|---|---|
| value/expression position, call args, initializers (fn **and** pn) | map or block by interior; `{}` = map |
| `if`/`for` bodies (both spellings), `else`, colon-form match arms, arrow bodies | map or block by interior; `{}` by fn/pn context (arrows: always fn) |
| `fn` `pn` `view` `on` bodies, braced match arms, handler arms, `while` bodies | always block; `{}` = empty body |
| `match` outer braces | arm list (structural) |
| `type` bodies | fields/constraint/methods interior (§7.11) |
| content position | map item (`{}` = empty map item, meaningful) |
| pn statement position, bare | **error** (dead either way) |
| type-annotation `{...}` | type space, unaffected |

Grammar note: the `prec.dynamic`/GLR map-vs-block arbitration returns
(reversing the §5.8 deletion claimed under Option 2) — for non-empty
braces it is a bounded-lookahead disambiguation over disjoint interiors,
and the empty-brace tie can be resolved in `build_ast` from a neutral CST
node, where the fn/pn boundary already lives (S16.6.5).

### 5.10 `>` in element scope: the angle brackets are not operators (decided)

The last collision: comparisons inside an element, in attribute values and
bare content expressions.

```
<elmt attr: a > b > ...
```

Every `>` in that line is a candidate element terminator *and* a candidate
comparison. Three options were evaluated (2026-08-21):

**Option 1 — comparison takes priority.** Greedy relational: `attr: (a > b)`
then close. Rejected: in `attr: a > b > c > ...` *every* `>` is a candidate
closer, so the parser must speculate over all split points — unbounded
GLR fan-out, and the surviving parse shifts as the user edits. The design's
worst enemy (the guess) reintroduced at its core idiom.

**Option 2 — element close takes priority.** `attr: a` then close, with
`b > ...` spilling into the parent. Rejected as the worst of the three:
the spill is often *legal-looking content*, so a user who wrote a
comparison gets a silently restructured document — a silent meaning
change, with comparisons still nominally legal. Prioritizing between two
legal readings is still guessing; it is just guessing consistently.

**Option 3 — inside element scope, `<` `>` `<=` `>=` are not operators at
all (ADOPTED).** Not "comparison loses" but "comparison in this scope does
not exist": `>` always closes the current element, `<` always opens a child
element, and the four symbol relationals are simply absent from the
operator set within `< ... >` (attribute values and bare content
expressions alike). With one reading removed rather than deprioritized,
the close-priority question never arises — the same move the design makes
everywhere else (§3.1, refuse to guess by construction).

Escapes, where a comparison is genuinely wanted inside an element:

- **Parentheses are grammar islands**: inside `( )` the full expression
  grammar returns, symbol relationals included — `attr: (a > b)`. This is
  the general escape and matches current attribute behavior.
- **The word operators `lt gt le ge` (and `eq ne`) remain available bare**
  — but note they are **not synonyms** for the symbol forms. Confirmed in
  the implementation and by probe (2026-08-21): the word operators map to
  `OPERATOR_ELEM_*` and are **element-wise/vectorized** (`[1, 2, 3] gt 2`
  → `[false, false, true]`, via `vec_cmp`), degrading to the scalar result
  on scalar operands (`3 gt 2` → `true`), while the symbol forms are
  scalar-only (`[1, 2, 3] > 2` is an error). This is the **opposite** of
  XQuery's assignment (where `gt` is the singleton value comparison and
  the symbol forms are the general/existential ones). So: for scalar
  operands `attr: a gt b` reads naturally and agrees with `(a > b)`; for
  collection operands the two families genuinely differ, and parentheses
  are the escape that preserves symbol semantics.

Cost accounting versus the status quo: attributes already work this way —
`binary_expr($, in_attr)` excludes the symbol relationals, and this
collision is *why* that flag exists — so the only tightening is extending
the same exclusion to bare content expressions (today they use full
`_expr`). The ruling also **simplifies decided point 13**: the
trailing-style carve-out for relational `>` inside element content
(`a >` ⏎ `b`) assumed bare comparisons exist there; they no longer do, so
inside element scope `>` is the terminator unconditionally, and the §3.3
continuation role applies only outside elements. The rule got shorter.

Residual caveat, recorded honestly: `attr: a > b` still parses — as
`attr: a`, close, with `b > ...` spilled into the parent. The spill fails
loudly at top level (the trailing `>` has no element to close) but can be
silently legal when nested; this residual is inherent to markup syntax
(HTML/XML share it exactly). Mitigation is diagnostic, not grammatical:
when a `>` closes an element and the following tokens have the shape
`operand >`, warn "did you mean a comparison? use `(a > b)`, or `a gt b`
for scalars".

---

## 6. Decided Points and Open Issues

**Decided (this document):**

1. `;` is a strict separator — between two statements only, never trailing,
   never terminator semantics (§3.2 Rule 1; revised 2026-08-21 from an
   earlier trailing-`;`-allowed draft).
2. Dual-role line-start tokens are syntax errors, not defaults (§3.2 Rule 3).
3. Continuation set: `|> | & ? .? ** ++ % and or to in is at div that eq ne
   lt le ge gt == != <= >=` (§3.3).
4. Banned dual-role set: `( [ - + * ! ^ / < .` with the `.ident(` carve-out
   (§3.3–§3.6).
5. `return` ⏎ `expr` means `return expr`; bare return is `return` followed
   by a separator (mid-block) or the closing `}` (block end) (§3.3).
6. Handler `^ {` is same-line only (§3.6).
7. `expr expr expr` in its general (combining) form is dead — no
   juxtaposition construct is possible in Lambda, permanently. Its reduced
   (sequencing) form is alive: statement juxtaposition via start tokens and
   content-item juxtaposition. See §3.8 for the full ruling and the
   sequence-vs-combine dividing line.
8. `if` / `for` / `while` unify to one node with two spellings: paren head
   with any-expression body, or bare head with braced body (§5.1); `(` after
   the keyword commits to the paren form (§5.2).
9. `(` after `if`/`while` commits to the paren form, so a bare-form
   condition must not start with `(` (§5.2). Corrected 2026-08-21: the
   example `if (a+b)*2 { }` is NOT an error — `*` is a prefix operator
   (§7.10), so it parses as paren-form with body `*2` plus a juxtaposed
   block. A body opening with a continuation-only token still errors; the
   misgrouping risk is a lint, since banning prefix bodies would ban
   `if (c) -1`.
10. `else` is optional in both spellings; absent else yields `null` in value
    position, nothing in content position; dangling else binds nearest;
    `else`/`case`/`default` are continuation-only keywords under Rule 3
    (§5.4).
11. `match` keeps its single braced form — it already instantiates the
    unified bare form; no paren form is added (§5.5).
12. The expr/stam distinction relocates from grammar to `build_ast`
    semantic checks keyed by fn/pn context (`break`/`continue`, `while`
    procedural-only, comprehension vs effect loop) (§5.6).
13. *(resolves O1; simplified by 17)* `>` is a continuation token outside
    element scope (line-start `> b` continues a comparison); inside an
    element it is unconditionally the terminator — preserving the
    closing-`>`-on-its-own-line formatting style (like `}` on its own
    line). Banning `>` would have ruled out a major formatting style for
    markup-heavy scripts. (An earlier draft carried a trailing-style
    carve-out for relational `>` inside element content; ruling 17 removed
    bare symbol relationals from element scope, so the carve-out is gone.)
    (§3.3)
14. *(resolves O2)* `=` is a continuation token: an assignment may break
    after its target (`target` ⏎ `= value`). Invalid targets fail loudly
    at the existing assignment-target check (§3.3).
15. Total separator strictness (revised 2026-08-21, superseding an earlier
    trailing-`;` allowance and a considered-and-rejected closer-shape rule
    permitting trailing `,` before `]`/`}`): a separator sits between two
    items only. Interior empty slots AND trailing separators are banned for
    both `;` and `,` — `{ a; b; }`, `[1, 2,]`, `{a:1,}`, `f(a, b,)`,
    `{ a; ; b }`, `[1, , 2]`, `f(a, , b)` are all syntax errors
    (§3.2 Rule 1). The known cost — JSON/Erlang-style reorder-the-last-item
    friction — is accepted deliberately for uniformity.
16. (v3, twice revised) `{ ... }`: **interior decides; context breaks the
    empty tie; parens never matter.** Non-empty braces are map or block by
    their disjoint interiors, in every expression position and control
    body of either spelling (`if c {a: 1}` ≡ `if (c) {a: 1}`). Block
    expressions exist (Rust-style, last-expression value, block-scoped
    `let`s) — giving arrows block bodies with no `({...})` quirk. Empty
    `{}`: value/content position → empty map (fn and pn alike); `if`/`for`
    bodies → fn: empty map, pn: empty block; bare pn statement → error
    (dead either way). Always-block (structural): fn/pn/view/on bodies,
    braced match arms, handler arms, `while` bodies (pn-only, value
    discarded). Arrows are fn context even inside pn. Lint:
    `for (x in l) {}` empty-map comprehension. Earlier forms — ~~Option 1
    (interior-only, ambiguous `{}`)~~ and ~~Option 2 (position-only, ugly
    escapes, no arrow blocks)~~ — **OBSOLETE**. Option 2 was ratified into
    the spec by mistake as S16.4.1 and corrected on 2026-08-22 to
    S16.4.1v2 + S16.4.2 + S16.4.3 (spec v10.0.0); see the erratum in the
    header and §5.9. Reaffirmed 2026-08-27: handler arms
    (`^ { ... }`, `~ { ... }`) stay always-block — considered and
    rejected moving them to interior-decides. The dividing test: interior
    decides where a bare-expression second form exists; the brace is
    structural where it is mandatory. Handlers have no unbraced form —
    `expr ^ expr` is already propagate-then-continue (`f()^ + 1`), so the
    same-line `{` is the sole propagation-vs-handler discriminator (§5.9
    rationale block; spec v15.2.1).
17. Inside element scope (attribute values and bare content expressions),
    `<` `>` `<=` `>=` are not operators: `>` always terminates, `<` always
    opens a child element. Comparisons there use parenthesized islands
    (`(a > b)`, full grammar returns inside `( )`) or the word operators —
    noting `lt gt le ge` / `eq ne` are element-wise/vectorized
    (`OPERATOR_ELEM_*`, opposite of XQuery's assignment), agreeing with the
    symbol forms on scalars only. Options "comparison priority" and
    "close priority" rejected — remove a reading, don't rank readings
    (§5.10).
18. (revised) Authority order: spec/design doc → Tree-sitter grammar →
    C parser. Implementation behavior is never evidence for a ruling and
    never earns a documented exception; a divergence from this document is
    an implementation bug to fix (§4.1). Authority order is deliberately
    the reverse of execution order (§4.4). Applied to O3: the scanner's
    line-start `!` type continuation was a bug, not a type-space
    carve-out.
19. Unary `!` is removed from value expressions — `not` is the one logical
    negation; `!` keeps its type-level roles (infix exclusion, complement).
    `!` becomes a pure infix token and moves from the S16.2.3 banned set to
    the S16.2.2 continuation set (§7.1).
20. `not` re-tiers loose: below comparisons and `is`/`in`/`at`, above
    `and`/`or` — `not a == b` ≡ `not (a == b)`, the Python placement
    (§7.2).
21. Sized floats accept integer spellings: `1f32` is a valid f32 literal,
    symmetric with `1i32`; the silent two-token misparse is fixed (§7.3).
22. Numeric separators: `_` between digits in every numeric literal family,
    hex included; spelling only, standard placement constraints (§7.4).
23. Hex is the only radix prefix — `0b`/`0o` considered and rejected
    (§7.5).
24. `pub` is a uniform prefix modifier: `pub let` / `pub fn` / `pub type`;
    bare `pub x = 1` removed; `pub var` stays illegal by non-composition
    (§7.6).
25. The `apply;` fused token is retired: bare `apply` is the keyword
    statement, disambiguated from `apply(...)` by the S16.2.5 return
    pattern; `;` near it is ordinary separation (§7.7).
26. Affirmed by design: comma decomposition (`let a, b = expr`, bracket
    patterns rejected) and single-quote symbols (§7.8).
27. String interpolation is KIV with its direction fixed: if built, it is a
    quoted-DSL mechanism on backtick (`` `...` `` quotes a sub-language,
    interpolated text being one instance), never plain string
    interpolation. The backtick syntax space is reserved and must not be
    spent otherwise. Content juxtaposition is the interim workaround
    (§7.13).
28. No implicit adjacent-literal concatenation, strings or symbols —
    `"a" "b"` and `'a' 'b'` never combine into one value (S16.3.1; the C
    missing-comma trap stays a loud error under S16.1.2). Distinct and
    kept: adjacent string items in *content* merge into one text node by
    the content-model **normalization** rule — construction-time document
    normalization, not expression-level concatenation. Long strings use
    explicit `++` (trailing-operator style); heavy multi-line text awaits
    the reserved backtick DSL (point 27) (§7.9).
29. `*` is the spread operator (so `*` stays in the S16.2.3 banned set);
    `*` and `...` are two wildcard families, not one — `*` = unit
    (glob/Kleene: path segment, any-key, `T*` repetition, spread), `...` =
    elided run (pattern gap, rest params), with the normative equivalence
    pattern `...` ≡ `any*`. Paths keep `*`/`**` (ellipsis collides with
    path dots — reasoned exception). `[...a]` gets a "spread is `*a`"
    diagnostic (§7.10).
30. (revised same day) `;` exits elements and object types; `,` takes
    over, under the two-regime doctrine: **pair-lists are strict comma
    lists** (maps, attrs, named args, params, fields/methods — comma
    always required; `{a: b c: d}` and `<div a:1 b:2>` both rejected —
    comma-optional attrs retracted, since inline pair lists have no
    line-start barrier and optional commas silently glue `(y)`/`[1]` onto
    greedy values), while **content and statements juxtapose**. Object
    types: one comma list, `, that expr` = object-level constraint (no new
    keyword), `, fn` = method (load-bearing vs fn types). Elements: strict
    attr commas; the boundary comma is a **biconditional** (v2) — present
    exactly when the element has both attributes and content:
    `<div "text">` and `<div a:1>` take none, `<div a:1, "text">` requires
    one, and both `<div a:1 "text">` and `<div, "text">` are errors. This
    retires the language's last optional delimiter, so ruling 15 now has no
    exception; rule 1 is safe only because §7.15 dissolved the
    `<svg .rect>`-vs-`<svg, .rect>` case. `;` has exactly one role
    language-wide:
    statement separation. S2.4.3v2 needs a `;`→`,` amendment (§7.11).
31. Unary `+` is kept (identity + string→number coercion), and `+` stays
    in the S16.2.3 banned set — class consistency over per-token
    minimality: the whole arithmetic family `- + * /` is banned uniformly
    ("arithmetic never continues at a line start"), rather than making `+`
    the lone freed exception. The banned set is final:
    `( [ - + * ^ / < .` (§7.12).
32. Two parsers, different jobs: the **C recursive-descent parser is
    production** (source → AST directly, no CST); the **Tree-sitter
    grammar is the official grammar and cross-checking reference**. S16
    is implemented in both, kept honest by the `compare_parser` lane.
    Because Tree-sitter leaves the production path, its size and speed
    stop being constraints: back to a single `grammar.js`, retiring the
    three-file split, `grammar-sync-check`, and the seven sub-language
    extraction externals — but a *small* scanner remains, since newline
    awareness is impossible in pure grammar rules (`extras` hides
    whitespace). Its new job is only the S16 guards (§4.4).
33. Closed-tail juxtaposition — full probe-verified open/closed table in
    §7.14 — (supersedes the blanket token-class rule at
    statement boundaries): a line-start dual-role token errors only when
    the previous statement ends in an EXPRESSION; after a structural closer
    of a non-postfixable construct (fn/pn/type{}/view bodies, braced
    if/for/while, match) or a self-complete keyword, it starts the next
    statement — "after a block, never `;`". Open-tail carve-outs: let/var/
    assign, type aliases (`type T = int` ⏎ `[3]` is the occurrence
    ambiguity), `=>` bodies, `return`, and every bare expression including
    maps/blocks/elements (primaries take postfix — closed-TAIL, not
    closed-bracket). Declarations are NOT expressions — `(fn () {})[1]` is
    impossible by design; parens promote non-primary exprs
    (`(match x {…})[0]`). Golden test holds: closed-tail juxtaposition has
    identical two-item readings in both spellings. Decided, not yet
    re-implemented; S16.1.3/S16.2.3 take a v2 (§7.14).
34. The relative path is respelled `\.a.b` (rooted `/.a.b` unchanged):
    lexically free, `\` already carries path flavour from its import-
    separator role, and unlike the `./a.b` front-runner it does not collide
    with S10.5.1's postfix root step `value./.name`. Consequence: `.ident`
    at a line start is pure member continuation, so the S16.2.4 carve-out
    widens from `.ident(` calls to full leading-dot fluent chains. `.` is
    sub-classified rather than retired from the dual-role set — `.digit`
    stays dual-role because `a.5` is member access with an integer field.
    Imports are unaffected — `import .mod` has a keyword introducer, so no
    ambiguity exists to escape. S2.4.1 takes a v3, S16.2.4 a v2 (§7.15).

35. *(ratified 2026-08-24 as S16.6.6, spec v13.0.0)* **Control statements
    require braces.** `return`/`break`/`continue` are rejected in every
    unbraced expression body — paren-form `if`/`for` body, `else` body,
    `case T:` arm, `=>` arrow body — with a diagnostic naming the braced
    repair. The braced block IS the statement spelling wherever it appears
    (`case T: { return x }` is legal). Decisive ground: the greedy return
    (S16.2.5) makes the C-family unbraced guard a silent-swallow trap —
    `if (c) return` ⏎ `cleanup()` would parse as `return cleanup()`, running
    cleanup only on the guard path and returning its value. Second ground:
    the paren/bare split (§5.1–§5.2) *is* the expression/statement split,
    which a statement body in the paren spelling would dissolve. Cost accepted:
    C-family developers will miss the non-braced form. Both front ends
    enforce it: the C parser by a direct check at each body site, the
    reference grammar by a zero-width `_expr_body_start` scanner guard
    (context-aware lexing means a grammar-only fix is impossible — the
    keyword falls back to `identifier` wherever only an identifier is
    valid). The guard is scoped to the four body positions, not to every
    identifier, keeping the §7.17 scanner blast radius small; it is
    stateless, so it carries none of that note's stale-carry hazard.
36. *(ratified 2026-08-24 as S16.6.7, spec v13.0.0)* **`pn` has one body
    form**: `pn name() { ... }`. No `=>` after a procedure signature, named
    or anonymous — `pn p() => expr` was redundant with `fn`, and
    `pn p() => { ... }` redundant with the braced form. The C parser
    accepting these was a front-end divergence (the TS grammar always
    rejected them); corpus cost was 1 doc site, 0 tests.

37. *(ratified 2026-08-24 as S16.6.8, spec v14.0.0; IMPLEMENTED same day)*
    **A procedural block is a statement, never an expression.** A braced
    block whose top level contains `return`/`break`/`continue`/`var`/
    assignment is rejected in every expression position — after `case T:`,
    in tuples/arguments/operands, as an arrow body. Probe that decided it:
    `let t = ({ return 99 }, 123)` returned 99 from the enclosing pn and
    `t` never existed — the expression evaporated. Explicitly KEPT:
    `case T: { <functional e> }`. Three grounds: maps after `:` force brace
    handling there regardless; fn blocks are expressions in every other
    position (arrow bodies rely on them); and under this very ruling the
    colon spelling can no longer conceal statements, so the redundancy that
    motivated banning it is gone. Interior decides — S16.4.1v2's doctrine
    lifted from map-vs-block to statement-ness.
38. *(ratified 2026-08-24 as S16.6.9, spec v14.0.0; IMPLEMENTED same day)*
    **Branch homogeneity: no mixed fn/pn branches.** `if`/`else` chains and
    `match` forms are all-value (every branch an expression — functional
    blocks, maps, and `raise` arms included) or all-control (every branch a
    procedural block; the form is a statement, illegal in value position).
    `if (c) { return 1 } else 0` dies; the repair is the sequencing style:
    `if (c) { return 1 }` then `let x = 0`. Semantic classification on the
    S12.1 boundary (fn/pn interior), NOT brace-shape uniformity — the
    brace-shape rule would have killed the all-fn block-else idiom
    (`if (x > 0) "ok" else { let r = d(x); … }`) for nothing. Home:
    build_ast per §5.6. The pn-call-in-expression question is deliberately
    NOT part of this ruling — recorded as SO36 (less severe: a pn call
    still produces a value; only effect ordering embeds in the expression).
    Implementation note: classification must be **three-way and recursive**.
    Scanning only a block's immediate top level for pn-only statements
    over-rejects a branch that merely wraps a nested control `if`, and an
    EMPTY branch is NEUTRAL — it commits to neither side and pairs with
    both, which is what `} else if (c) { } else {` relies on.
39. *(ratified 2026-08-27 as S16.10, spec v16.0.0)* **Keywords never name
    bindings; data names admit keywords.** The whole lexer keyword table is
    rejected as any binding name — `let`/`var`, parameters, `fn`/`pn`/
    `type`/`view` names, import aliases — with the declaration-site E201
    that `last` already gets; **no quoted escape** (a quoted use site is a
    symbol member and symbols never implicitly read bindings, S2.4.3). Map
    keys, element tags, and attribute names admit keywords — `<if a:1>`
    becomes legal (the bare-tag rejection was implementation, not a
    ruling); the quoted-symbol spelling is advised where a bare keyword
    confuses. Member access after `.` admits keywords (`m.type`, `x.if`);
    subscripts stay expression space (`last` keeps S7.2.2). Migration ~55
    corpus bindings; probes, capture analysis, and rejected alternatives
    in §7.24.
40. *(ratified 2026-08-27 as S12.3.7, spec v16.1.0)* **Sys-func shadowing
    is user-first, module-lexical, with a mandatory warning.** A module's
    `fn`/`pn` or value binding matching a sys-func name shadows it for that
    module/script only, statically — never globally (no JS-style prototype
    mutation); `pub` export extends it to importers through the explicit
    import only. Every shadowing draws a compile warning. Non-callable
    shadows give the not-callable error, never builtin fallback; the
    reserved core (keywords + base-type words) is un-shadowable via
    S16.10.1. Key ground: forward compatibility — a new sys func must never
    change an existing program. Alternative (collision = compile error)
    rejected: maximum silent-capture protection, but it freezes the stdlib
    namespace. Builtin-access spelling deferred (SO37). Full argument
    in §7.25.

**Open** *(O1 and O2 resolved above — decided points 13–14)***:**

- **O5: pn calls in expressions** — A-normal-form effect sequencing,
  recorded as SO36 in the formal spec; an S12 question, not an S16 one.
  Deliberately split off from points 37–38 and left open: it is **less
  severe** than the branching statements those rulings bar, because a pn
  call still produces a value — the enclosing expression evaluates rather
  than evaporating; the cost is only effects and their ordering embedded in
  expression evaluation. Adopting it would outlaw shipped idioms
  (`if (exists(p)) …`, `let config = if exists(p) { input(p, 'json') }
  else {…}`), so it needs its own cost survey.
- **O3: scanner-owned type tokens** *(audited 2026-08-21; reduced to one
  conformance fix)*. Multi-line type patterns live inside
  `type_pattern_token` et al., where `src/scanner.c` — not the statement
  grammar — decides where the token ends. The audit found the scanner has
  independently implemented both core rules of this design:
  `scan_type_pattern`'s `expect_primary` is Rule 2 (an unfinished pattern
  continues across the newline), and `operator_continues_after_newline`
  (`scanner.c:229`) is Rule 3 (a line-start token that can only continue,
  continues) — its comment even states this document's criterion, that the
  admitted operators "cannot start a statement."

  Its continuation set is `|`, `&`, `!`, plus the word `to` (deferred via
  `newline_pending`, since one character cannot distinguish `to` from an
  identifier). Against §3.3: `|`, `&`, and `to` agree; **`!` diverges** —
  the design classifies `!` as dual-role (set exclusion vs unary `not`) and
  bans it at line start. Under the authority order (§4.1) this is a scanner
  bug, not a documented exception: remove `!` from
  `operator_continues_after_newline`. The statement-level Rule 3 guard then
  produces the error by itself — with the type token ending at the newline,
  a following `! B` line is a line-start dual-role token after a complete
  statement. Repairs are the standard ones (`let x: A !` ⏎ `B`, or one
  line). Confirmed consistent, needing no change: `that` at depth 0 already
  breaks the pattern and is left to `constrained_type` (`scanner.c:342`),
  matching its continuation-token classification; and the entries §3.3
  gained later (`>`, `=`, and the rest) are meaningless in type space, so a
  proper subset is expected there.

  Residual work: apply the same check to the sibling scanners that share
  this logic — `primary_type_pattern_token`, `return_type_token`,
  `view_pattern_token`, `content_type_token`.
- **O4: formal ratification** — *done 2026-08-21*. Ratified as `S16 Surface
  Syntax` in `doc/Lambda_Formal_Semantics.md`, spec v8.0.0 → **v9.0.0**
  (MAJOR: no existing ruling changed meaning, but S16 breaks existing
  programs — `return` ⏎ *value*, trailing `;`, the control-form spellings.
  The spec's semver rule was amended in the same edit to make added-but-
  breaking rulings MAJOR, so the bump is self-consistent). All 18 decided
  points
  are normative as S16.1.1–S16.6.5, every one `*`-marked with an Appendix A
  conformance entry; the design doc is added to the spec Basis and
  Appendix C. `D#` was ruled the wrong home — the grammar change touches D8
  only insofar as parse-tree shapes change. A future dedicated formal syntax
  document is tracked as `SO35`.

  **Remaining: the user-facing doc sweep, measured 2026-08-24.** Every fenced
  ```lambda block in the four docs was extracted and parse-checked against the
  production parser: **60 of 172 fail**.

  | Document | Failing blocks |
  |---|---:|
  | `doc/Lambda_Cheatsheet.md` | 26 |
  | `doc/Lambda_Expr_Stam.md` | 20 |
  | `doc/Lambda_Func.md` | 7 |
  | `doc/Lambda_Reference.md` | 7 |

  Causes, by parser diagnostic:

  | Count | Diagnostic | Ruling |
  |---:|---|---|
  | 28 | *token cannot continue the previous line* | S16.2.3 — one-example-per-line listings (`-x` ⏎ `+x`, `[1,2] + [3,4]`) |
  | 11 | *expected an expression* | mixed; includes non-code fenced as `lambda` |
  | 5 | *object-type members are separated by `,`, not `;`* | §7.11 |
  | 4 | *trailing `;` is not a statement separator* | decided point 15 |
  | 3 | *`pub` modifies a declaration; write `pub let`* | §7.6 |
  | 2 | *expected `,` between element attributes and content* | §7.11 |
  | 7 | assorted (retired `@./path` sigil → `\.a.b` per §7.15; arity/paren shape) | — |

  Not all 60 are prose bugs: at least one block is an operator table
  (`+  -  *  /  div  %  **`) fenced as `lambda` and should lose the language
  tag rather than be rewritten. The sweep needs a per-block judgement, not a
  mechanical rewrite.

---

## 7. Surface Syntax Audit (2026-08-21)

A post-ratification sweep of the grammar for constructs that are
inconsistent, non-intuitive, or against what developers normally write.
Findings are probe-verified against the current build. Rulings 7.1–7.12 are
decided (ledger points 19–31); §7.13 lists the items still under discussion.
The decided items amend S16 and adjacent spec sections; §7.13 is fully
resolved, so the batch ratification is unblocked.

### 7.1 Unary `!` removed from value expressions (decided)

**Finding (silent trap).** Probes: `!true` → `type`, `!0` → `type`. Unary
`!` in value position is *type complement*, not logical negation — every
C/JS/Rust developer who writes `!ok` gets a type value, silently. Meanwhile
`not true` → `false` works as expected.

**Ruling.** Unary `!` is removed from value expressions. `not` is the one
logical negation (per S10.3.1, words over sigils). `!` retains its
type-level roles: infix set exclusion and type complement inside type
expressions. A value-position unary `!` becomes a syntax error whose
diagnostic says "use `not`".

**S16 impact.** With its prefix role gone, `!` is a pure infix token and
moves from the S16.2.3 dual-role banned set to the S16.2.2 continuation
set. (This also retro-validates the O3 scanner fix differently: the type
scanners may keep `!` as continuation once the value-level prefix is gone —
re-audit that one line when implementing.) The banned set shrinks to
`( [ - + * ^ / < .`; unary `+` stays in it by §7.12.

### 7.2 `not` re-tiered loose (decided)

**Finding (silent trap).** Probe: `not 1 == 2` → `false`, i.e.
`(not 1) == 2`. Lambda gave the keyword `not` C's `!` precedence; Python,
Ruby, and SQL all bind word-`not` *below* comparisons. Every developer
reads `not a == b` as `not (a == b)`.

**Ruling.** `not` binds below the comparison tiers (`binary_relation`,
`binary_eq`) and below `is`/`in`/`at`, above `and`/`or` — the Python
placement, so `not a in b` means `not (a in b)`. Unary `-` keeps its tight
arithmetic precedence; the unary tier no longer contains `not`.

### 7.3 Integer-spelled sized floats (decided)

**Finding (silent trap).** `sized_integer` accepts plain integers (`1i32`),
but `sized_float` demands a decimal point — so `1f32` lexes as two tokens
with context-dependent garbage results: bare `1f32` → `1`; `type(1f32)` →
`f32` (the stray `f32` is a base-type keyword).

**Ruling.** `1f32` is a valid f32 literal: `sized_float` accepts integer
spellings alongside the decimal-point forms, symmetric with the
sized-integer family.

### 7.4 Numeric separators (decided)

**Finding.** `1_000_000` is a parse error. JS, Python, Rust, Java, and
Swift all accept underscore separators; for a data-processing language the
gap is glaring.

**Ruling.** `_` is permitted between digits in every numeric literal family
— integer, float, decimal, sized, hex (`0xFF_FF`), and exponent digits —
under the standard constraints: between two digits only (no leading or
trailing underscore, none adjacent to `.`, `e`, or a type suffix, no
doubling). Underscores are spelling only; they never survive into the
value.

### 7.5 Hex only — `0b`/`0o` rejected (decided)

Binary and octal literals were considered and **rejected**; `0x` remains
the only radix prefix. Keeps the literal grammar small; bit-pattern work is
served by hex.

### 7.6 `pub` becomes a uniform modifier (decided)

**Finding.** Probe: `pub x = 5` works — in `let_stam`, `pub` *replaces*
`let` (`choice('let', 'pub')`), while in `fn_stam` it *modifies*
(`pub fn f`). One keyword, two composition rules.

**Ruling.** `pub` is a uniform prefix modifier: `pub let x = 1`,
`pub fn f`, `pub type T`. The bare `pub x = 1` spelling is removed.
`pub var` remains illegal — the original design intent (only immutable
bindings are publishable) is preserved by the modifier grammar simply not
composing with `var`, and now that rule is visible rather than implicit in
a substitution trick. Migration: `pub x = …` → `pub let x = …`, mechanical.

### 7.7 `apply;` fused token retired (decided)

**Finding.** `apply_stam` is `token(seq('apply', ';'))` — the `;` is baked
into the lexeme so that `apply(arg)` still parses as a call. A fourth,
private role for `;`.

**Ruling.** The fused token is retired. Bare `apply` is a keyword statement
(view/edit bodies only, semantic check unchanged); `apply(...)` remains an
ordinary call. Disambiguation needs no fused token because S16 already
solved this shape for `return` (S16.2.5): a same-line `(` after `apply` is
the call; a separator or block end makes it the bare statement; a
line-start `(` on the next line is the ordinary S16.2.3 dual-role error.
`;` around `apply` is ordinary statement separation. Migration cost is
near-zero: existing `apply; next` re-parses as `apply` + separator +
`next`; only a block-final `apply;` before `}` becomes the standard
trailing-separator error, same as `return;`.

### 7.8 Affirmed as designed (decided)

- **Decomposition syntax.** `let a, b = expr` (positional) and
  `let a, b at expr` (named) stand as designed; bracket patterns
  (`let [a, b] = e`) are rejected. The one-character distinction from
  multi-declare `let a = 1, b = 2` is accepted: the first `=`/`at` position
  is the discriminator.
- **Single-quote symbols.** `'name'` is a symbol, not a string — deliberate,
  consistent with the `b'…'`/`t'…'` quote family. (A
  symbol-where-string-expected diagnostic remains a §7.13 mitigation
  candidate.)

### 7.9 No implicit adjacent-literal concatenation (decided)

**Question.** Should `"a" "b" "c"` concatenate to one string, C-style
(including across lines)? And symbols, `'a' 'b' 'c'`?

**Ruling.** **No, for both.** Three grounds:

1. **S16.3.1 forbids it on principle**: adjacent-literal concatenation is a
   *combining* juxtaposition — a value computed from neighbours — and the
   combining form is permanently dead. String literals would be the one
   exception, reopening the door the design closed.
2. **C's version is a famous trap, and Lambda is currently immune**: a
   missing comma in a string list silently merges elements
   (`{"a", "b" "c", "d"}` is three strings, not four; Python linters ban
   the feature as `implicit-str-concat`; Rust dropped it). Under strict
   separators (S16.1.2), `["a", "b" "c"]` is a loud error today — implicit
   concat would convert exactly that error into the silent element merge.
3. **Symbols are identity-bearing atoms**: composing an *identity* by
   silent adjacency is worse than composing a string by accident. A
   composed symbol must be explicit (`'a' ++ 'b'`, or a constructor).

**Content normalization is a different thing, and stays.** In content
position, adjacent string items — `<p; "Hello " name "!">` — are **merged
into one text node**. This is a *content-model normalization rule* (the
document normalizes adjacent text), not expression-level concatenation:
the merge happens when content is constructed, and `let s = "a" "b"` in
value position remains illegal. Same spelling family, two rulings, no
conflict — sequencing plus normalization on one side, no combining on the
other.

**The real need — long strings across lines — is served by explicit
`++`**, trailing-operator style:

```
let s = "line one " ++
    "line two " ++
    "line three"
```

Not perfect, but not too bad — and the reserved backtick DSL (point 27) is
the eventual proper home for heavy multi-line text, which is a further
reason not to spend the need on a worse mechanism now.

### 7.10 `*` and `...`: spread decided, the two wildcard families (decided)

**Spread is `*` (decided).** Unary `*` is Lambda's spread operator
(`[*a, 3]` → `[1, 2, 3]`, probe-verified). Consequence for S16: `*` keeps
its prefix role, so it stays in the S16.2.3 dual-role banned set — the
"free `*` to the continuation set" option is closed. Diagnostic note: JS
hands will write `[...a, 3]`; that should error with "spread is `*a`" —
the one cross-convention trap this ruling creates, defused cheaply.

**`*` and `...` are two different concepts, not one wildcard with two
spellings.** The doctrine:

- **`*` is the unit family** (Kleene/glob heritage): one unit, or as a
  suffix, repetition of a unit. Path `a.*.b` matches exactly one segment;
  `{*: T}` matches any one key; type-suffix `T*` is zero-or-more
  repetition; value `*x` is spread.
- **`...` is the elided-run family** (ellipsis/rest heritage): a
  contiguous sequence of any length, never a single item.
  `[a, ..., b]` is a gap; `fn f(a, ...)` is a rest of arguments.

| Domain | one unit | any-length run |
|---|---|---|
| paths | `*` | `**` |
| sequence/type patterns | `any` | `...` (≡ `any*`) |
| parameters | — | `...` (rest) |
| values | `*x` spread | — |

**Normative equivalence: pattern `...` ≡ `any*`** — the two families meet
by definition, not by accident, and `[a, ..., b]` stays the preferred
spelling over `[a, any*, b]` (it reads as the mathematical ellipsis
"1, 2, ..., n").

**Paths keep `*`/`**` — the one asymmetry, with a recorded reason.** The
run wildcard in paths cannot be `...`: ellipsis dots collide with path
separator dots (`a....b` is lexically hopeless). And unifying the other
way (`[a, **, b]`, `fn f(a, **)`) would read as glob in sequence position,
collide with Python's kwargs meaning, and break C/Java/JS/TS varargs
muscle memory — the strongest convention in this area. Each domain keeps
its native spelling; the asymmetry is a reasoned exception, not drift.

`*` now legitimately carries four meanings — spread, glob segment, Kleene
suffix, `**` power — but the four spaces are syntactically disjoint and in
each one `*` is what that domain's developers already expect: principled
overloading, same as `.` carrying member/path/float roles under S16.

### 7.11 `;` exits elements and object types; `,` is the interior separator (decided; revised same day)

The last two non-statement roles of `;` (the element attr/content divider
and the `object_type` field/method divider) are retired; `,` takes their
place. `;` now has exactly one role in the entire language — statement
separation — completing what §7.7 started. *Revision note: the first draft
of this ruling made element-attr commas optional; that was retracted the
same day (reasoning below), and this section records the final strict
form.*

**The two-regime doctrine.** Separator-free juxtaposition is sound only
where a barrier polices it. At statement level, S16.2.3's line-start
classification is that barrier — a dual-role token at a line start is a
loud error. Inside a one-line pair list there is **no barrier at all**:
with optional commas, `{a: x [1] b: 2}` silently makes `[1]` an *index on
`x`*, and `<div a: x (y) b: 2>` silently makes `(y)` a *call* — the exact
silent-glue class this design exists to prevent, with nothing to catch it.
Hence:

- **Pair-lists are strict comma lists** — maps, element attributes, named
  arguments, parameters, object-type fields and methods. The comma is
  always required between items (ruling 15); no optional delimiters exist
  anywhere in this regime. `{a: b c: d}` is rejected — and this is also
  what forbids comma-optional attrs: `<div a:1 b:2>` legal while
  `{a: 1 b: 2}` illegal would be indefensible, since attrs and map items
  are the same `name: value` shape (and maps stay strict for their
  JSON-adjacent role besides).
- **Content and statements juxtapose** — strings, elements, and
  expressions as content items; statements per S16.1.3. This is the regime
  where the barrier (or literal-shaped items) makes juxtaposition safe.

**Object types: one comma list — fields, constraint, methods.**

```
type E { a: int, z: string, that ~.a > 0, fn render() => ... }
```

- `, that expr` is the object-level constraint: after a comma, `that`
  cannot start a field (fields need `name:`), so the separator itself
  disambiguates. `z: string that len(~) > 3` *without* a comma remains the
  field-level constrained type (CT1v2/CT2) — both forms coexist cleanly.
  No new constraint keyword is needed (`where` was considered; it buys
  nothing the comma doesn't already deliver, and one constraint keyword
  beats two).
- `, fn ...` introduces a method: `fn` after a comma cannot start a field.
  The comma before a method is load-bearing, not stylistic: fn *types*
  exist, so in `f: fn(int) int fn render() ...` the second `fn` could
  continue the field's type.

**Elements: strict attr commas; content juxtaposes after the list.**

```
<div class:"x", id:"y" "text" <b "child">>   // commas in the attr list;
                                             // content juxtaposes after it
<div "str">                                  // tag → content juxtaposes freely
<div a: x, (y)>                              // boundary comma REQUIRED:
                                             // else (y) is the call x(y)
<svg, .rect>                                 // boundary comma REQUIRED:
                                             // else qualified tag svg.rect
<div a:1, b:2, "text">                       // legal — optional boundary comma
<elmt, content>  <elmt a:b, content>         // legal — same
```

- Within the attr list, commas are required — pair-list regime.
- The attr-list→content boundary belongs to the content regime: after a
  complete attr value, a `name:`-shaped item is the next attr and anything
  else starts content, with no separator. `<` needs none either (§5.10:
  never an operator in element scope). Tag-to-content juxtaposition is the
  design default: `<div "str">` is the normal spelling.
- **The boundary comma is a BICONDITIONAL: present exactly when the element
  has both attributes and content** (v2, decided 2026-08-21 — the third and
  final form of this rule).

  ```
  <div "text">                 // content only — NO comma
  <div a: 1, "text">           // both — comma REQUIRED
  <div a: 1 "text">            // SYNTAX ERROR — missing boundary comma
  <div a: 1>                   // attrs only — no comma
  <div, "text">                // SYNTAX ERROR — no attrs, so no comma
  ```

  It settles the greedy attribute value deterministically rather than by
  lookahead luck: `<div a: x (y)>` makes the call `x(y)` the attribute
  value, `<div a: x, (y)>` makes `(y)` content. The comma's presence IS the
  discriminator, so nothing is guessed.

  **Revision history, because this rule moved twice.** It began as the `;`
  divider; the first version of §7.11 replaced it with a comma that was
  "always permitted, required at ambiguity" — the one deliberately optional
  delimiter in the language, carved out of ruling 15. That exception is now
  retired: the biconditional is checkable in one sentence and restores
  ruling 15's no-optional-delimiters doctrine without exception.

  **The cost, stated plainly: `<div a: 1 "text">` is lost.** That spelling
  — attributes running straight into content, exactly as HTML writes it —
  is the one form worth wanting here, and it is rejected. It cannot be kept
  without making the comma OPTIONAL (legal both with and without), which is
  precisely the exception this ruling exists to remove: an optional
  delimiter at a structural position means the reader must know which
  content-leading tokens are hazardous before they can predict whether the
  comma is required. Syntactic simplicity wins over the nicer spelling, and
  the trade is accepted knowingly.

  The consolation is that the required form is still better than the markup
  it replaces: `<div a: 1, "text">` against HTML's
  `<div a="1">text</div>` — one delimiter instead of a repeated closing
  tag, and no tag-name duplication to keep in sync.

  **Rule 1 (no comma when there are no attributes) is only safe because of
  §7.15.** The leading comma existed for exactly one case — `<svg .rect>`
  (maximal-munch qualified tag) versus `<svg, .rect>` (tag plus path child,
  S2.4.3v2). Respelling the relative path `\.` dissolved it: `.rect` can no
  longer be a path, so `<svg \.rect>` is unambiguous with no comma at all.
  Banning the leading comma before §7.15 would have reintroduced a real
  ambiguity.

**What remains of `;` in these scopes:** statements inside element content
still use it, as everywhere — `<div let x = 1; x + 1>` — that is the one
role it has language-wide, not a residue of the divider.

**Migration and spec touchpoints.** `<div; content>` → drop the `;` (or
`,` in the ambiguous cases); `type { fields; methods }` → comma;
S2.4.3v2's "explicit `;` before a relative-path element child" needs a v2
amendment to the comma spelling. Trailing commas remain banned (ruling
15): `<div a:1,>` errors.

### 7.12 Unary `+` kept; the arithmetic family stays banned uniformly (decided)

**Finding.** Unary `+` is not a no-op: `+5` → `5`, and `+"42"` → the
*number* `42` (JS-style string→number coercion; `+[1,2]` and `+true`
error). Removing it would have freed `+` into the S16.2.2 continuation set
— the same table win as §7.1's `!` removal — at the cost of the coercion
idiom (whose explicit spelling is the conversion sys-funcs) and
sign-symmetric data (`[+1, -1]`; JSON itself forbids `+5`).

**Ruling — kept, the opposite of the §7.1 pattern, for a stated reason:
class consistency beats per-token minimality.** With `+` banned, the
S16.2.3 set contains the *entire arithmetic family* `- + * /` uniformly,
and the teachable rule is "arithmetic operators never continue at a line
start." Freeing `+` alone would make it the lone exception — a per-token
table ("`+` continues but `-` doesn't") in place of a class rule, which is
harder to hold in the head than the symmetry it saves. `-` can never be
freed (negative literals), `*` is kept prefix by §7.10 (spread), `/` by
paths — so `+` freed alone buys one token of table shrinkage at the price
of the family's coherence. Unary `+` therefore survives with its current
semantics, and the S16.2.3 banned set is final: `( [ - + * ^ / < .`
(minus `!`, freed by §7.1 — a *full* role removal, which is exactly why it
was different: `!` left the family entirely rather than becoming an
exception within it).

### 7.13 Remaining audit items — ALL RESOLVED (as of the first pass; §7.14–§7.15 were added by the post-implementation review)

Every §7 audit ruling is now decided; the batch ratification into the
spec (S16 v2 amendments plus the S10/S2 touchpoints noted in §7.1–§7.12)
is unblocked. Dispositions of the final two items:

- **Arrow block bodies — resolved by §5.9 v3** (better than the JS-quirk
  candidate ruling recorded here earlier): interior differentiation gives
  `(x) => { let y = x + 1 y }` a block body and keeps `(x) => {a: x}` a
  map, with no `({...})` parenthesization quirk at all.
- **Diagnostics backlog** (non-ruling implementation work consolidated
  from §7): symbol-where-string-expected (§7.8), `[...a]` → "spread is
  `*a`" (§7.10), the `for (x in l) {}` empty-map-comprehension lint
  (§5.9), and the boundary-comma and dual-role repair messages (§4.2).

- **String interpolation — KIV (ruled 2026-08-21, direction fixed, design
  deferred).** None exists today, and none is being added now. Direction if
  Lambda ever does it: it will **not** be plain string interpolation — the
  backtick form `` `...` `` would quote a **DSL**, a general quoted
  sub-language of which interpolated text is one instance. The backtick is
  currently unused in the grammar (the identifier charset excludes it), so
  that syntax space is **reserved** for the quoted-DSL mechanism and must
  not be spent on anything else in the interim. Content juxtaposition
  (`"Hello " name "!"`) is acknowledged as a limited workaround for the
  time being, not the final answer.

### 7.14 Closed-tail juxtaposition (decided; supersedes the uniform token-class rule)

The first implementation applied S16.2.3 as a blanket token-class rule:
after ANY statement, a line-start dual-role token was an error. That made
`fn pick2(x) { … }` ⏎ `[pick(2), pick2(130)]` demand a `;` after the
declaration — a separator no brace language requires, and the single most
common class in the corpus migration. The refinement:

1. **Statement → statement: juxtapose, no `;`.** (Already the S16.1.3 rule
   for start-token-led statements.)
2. **Closed-tail statement → expression: juxtapose, no `;`** — including
   expressions led by dual-role tokens.
3. **Expression → expression: `;` required before a dual-role lead** — the
   existing S16.2.3 rule, unchanged.

**The sound boundary is the statement's TAIL, not its kind.** A statement is
*closed* when it ends in a structural closer of a non-postfixable construct —
the `}` of a `fn`/`pn`/`type { }`/`view` body, a braced `if`/`for`/`while`
body, a `match` arm list — or is a self-complete keyword (`break`,
`continue`, bare `apply`). No dual-role token has any grammatical attachment
to a closed form (you cannot call, index, or subtract from a declaration;
`if`/`match` are not primaries, so postfix cannot attach), so a following
dual-role-led expression is the ONLY reading. A statement is *open* when it
ends in a greedy expression, and rule 3 applies. The open-tail carve-outs,
each with its ambiguity exhibit:

- `let` / `var` / assignment: `let x = arr` ⏎ `[0]` — the split would
  silently hide an intended `arr[0]` (the Go/ASI silent-split failure §1.7
  forbids).
- (`import` was originally classified open and was moved to the CLOSED set
  on 2026-08-22: its tail is a module NAME, which only `,` `:` `.` `\\` can
  continue — never a dual-role token — so the separator guarded nothing.
  `import math [1, 2]` is two items on one line as well as two.)
- `type` aliases: `type T = int` ⏎ `[3]` — `[3]` is occurrence syntax
  (`int[3]`): a genuine two-reading ambiguity, not just a trap.
- Arrow bodies: `fn f() => x` ⏎ `(y)` — `(y)` could only mean the call.
- `return` with its greedy optional value.
- **Every bare expression — maps, blocks, elements included**: these are
  *primaries* and take postfix bare (`{a: 1, b: 2}["a"]` is an index on one
  line), so they sit on the open side despite visually ending in a closer.
  This is the trap in any "ends with `}`" mental shortcut: the rule is
  closed-*tail* (non-postfixable construct), never closed-*bracket*.

**The complete classification, probe-verified against the C parser
(2026-08-23).** The operational test is mechanical: put the construct on one
line and a line-start `[1]` on the next. If it parses, the tail is closed; if
it only parses once a `;` is added, the tail is open.

| Construct | Tail | Example |
|---|---|---|
| `let` | **open** | `let x = 5` |
| `var` | **open** | `var v = 5` |
| assignment | **open** | `v = 5` |
| type **alias** | **open** | `type T = int` |
| `fn`/`pn` with a `=>` body | **open** | `fn f() => 1` |
| `return` *value* | **open** | `return 5` |
| bare expression | **open** | `1 + 1`, `f(x)` |
| bare **map literal** | **open** | `{a: 1}` |
| bare **block expression** | **open** | `{ let q = 1 q }` |
| bare **element** | **open** | `<d "x">` |
| `fn`/`pn` with a **braced** body | closed | `fn f() { 1 }` |
| **object** type | closed | `type T { a: int }` |
| braced `if` | closed | `if c { … } else { … }` |
| braced `for` | closed | `for x in l { … }` |
| `while` | closed | `while c { … }` |
| `match` | closed | `match x { … }` |
| `view` / `edit` | closed | `view v { … }` |
| `import` | closed | `import math` |
| `break` / `continue` | closed | `break` |

Two rows do the teaching. **`type` and `fn` appear on BOTH sides** — the kind
never decides, only the tail: `type T = int` is open and `type T { … }` is
closed; `fn f() => 1` is open and `fn f() { 1 }` is closed. And **`{a: 1}` /
`<d "x">` are open despite ending in `}` and `>`**, because primaries take
postfix — which is why the shortcut is closed-*tail*, never closed-*bracket*.

The implementation encodes exactly this: `parse_content` grants closed-tail
status to `fn`/`pn`/`type`/`view`/`edit`/`while`/`match`/`if`/`for`/`pub` only
when `prev_kind == LAMBDA_TOK_RBRACE`. Just `break`, `continue`, and `import`
are closed on the keyword alone.

**Diagnostics (fixed 2026-08-23).** Every open-tail rejection now names its
repair: *"this token cannot continue the previous line; write ';' to start a
new statement, or move it to the end of that line"*. It briefly did not —
`record_direct_parse_error` (runner.cpp) discarded `parse_error->message`
and synthesized `Unexpected syntax near 'X'` for everything except the
`<`/`>` case, so EVERY parser diagnosis was replaced at the CLI boundary:
the S16.2.3 repair, `'pub' modifies a declaration; write 'pub let'`,
`object-type members are separated by ',', not ';'`, and the rest. The
parser's message is now used when it has one. One structural exception
remains by design: an unlexable token (`actual_kind == LAMBDA_TOK_ERROR`)
keeps the synthesized form, because "invalid token" names nothing while
`Unexpected syntax near '\d'` names the offender.

**The golden test DERIVES the classification (ruled).** The open/closed
boundary is not an enumerated list but a consequence of S16.1.1: **a tail
is open exactly when the one-line spelling glues.** If `let x = arr` ⏎
`[0]` were accepted as two items, newline→space would yield
`let x = arr [0]` — which parses as the ONE-item `arr[0]` — so acceptance
would change meaning across spellings and violate the invariant; rejection
is forced, not chosen. Same for `type T = int` ⏎ `[3]` versus the
occurrence `int[3]`. Conversely, closed tails juxtapose because their
one-line spelling does not glue either: `fn a() {1} [1, 2]` is declaration
+ array statement in BOTH spellings — probe-confirmed that the parser
errors identically on the one-line form today, proving the one-expr
reading never existed (`index_expr` requires a `primary_expr` operand;
`fn_stam` is not an expression). Juxtaposition does not create a reading;
it stops rejecting the only one there was.

**Repair spellings are uniform across every open tail** — let/type
declarations exactly like maps and elements: split the bracket for one
expression (`let x = arr[` ⏎ `0]`, `type T = int[` ⏎ `3]`), or `;` for two
statements (`let x = arr;` ⏎ `[0]`, `type T = int;` ⏎ `[3]`). One rule,
one pair of repairs, everywhere.

**Declarations are not expressions (decided, now explicit).**
`(fn () {})[1]` is impossible by design, not by accident: `fn`, `pn`,
`type`, and `view` declarations live outside expression space; only their
NAMES enter expressions. Immediate invocation uses an arrow
(`((x) => x * 2)(3)`); indexing a value-producing braced form promotes it
with parens (`(match x { … })[0]`, `(if (c) {…} else {…})[0]`) — parens are
what lift a non-primary expression to primary, uniformly in both spellings.

**Cross-line postfix on primaries (confirmed, no change).** `{a: 1, b: 2}` ⏎
`["a"]` is rejected (rule 3); the two spellings are
`{a: 1, b: 2}[` ⏎ `"a"]` (unfinished expression continues, S16.2.1) for one
expression, and `{a: 1, b: 2};` ⏎ `["a"]` for two. Both already work in both
parsers.

The teachable rule: **"after a block, never `;`; after an expression, `;`
before `( [ - + * / < . ^`."** This matches C/Go/Rust muscle memory (no
separator after a function body) and removes the majority of the corpus
migration edits. It knowingly reverses the earlier uniformity argument
(§4.5's harness encoded the blanket rule): the context cost is one bit —
"did the last statement end in an expression?" — which the reader resolves
visually at the `}`.

**Implementation status: DONE in both front ends** (61/61 each,
`make test-grammar-s16`). Tree-sitter splits the statement list into
`_closed_stam` / `_open_stam`, so `_stmt_boundary` is only demanded after
open tails; `if_expr` and `for_expr` split by spelling
(`_if_closed`/`_if_open`, `_for_closed`/`_for_open`) because the bare
spelling ends on a structural brace while the parenthesized one ends on a
greedy expression. That split reopened the dangling-else decision at the
grammar level, resolved per S16.6.3 by ranking `_if_open` above
`_if_closed` so the nearest `if` claims the `else`. The C parser tracks the
previous token kind and treats a statement as closed when it ended on `}`
under a block-form keyword, or is `break`/`continue`. S16.1.3/S16.2.3 take
a v2 at batch ratification.

One case the implementation caught that the ruling had not: the TYPE slot
needed the same guard. `type T = int` ⏎ `[3]` was being absorbed as the
occurrence `int[3]`, because `[` is dual-role in type space too (`int[3]`
occurrence versus `[int]` array type). `occurrence_count` now takes the
guarded same-line bracket, matching the C parser, which already reached
this through `parse_type_slot`.

### 7.15 Relative-path introducer: `\.a.b` (decided)

**Problem.** The relative path spelled `.a.b`, whose introducer is the
member-access dot — the reason `.` sits in the dual-role set at all, and a
standing reader hazard now that §7.14 legalizes `fn f() {}` ⏎
`.config.load()`: a line-start `.a.b` reads as a member continuation to a
human even where the parser knows better.

**Ruling: the relative path is `\.a.b`.** The rooted form is unchanged
(`/.a.b`). Options considered:

| Option | Verdict | Why |
|---|---|---|
| `~.a.b` | rejected | `~.a` already means member `a` of the PIPE context item — a different context notion than the resolution universe; overloading erases the distinction, and paths must work with no `~` bound |
| `path.a.b` | rejected | `path` cannot become a keyword: it is a hot identifier in a data/document language, and `path.a` is already member syntax on a variable named `path` |
| `*.a.b` | rejected | breaks the §7.10 wildcard doctrine — `*` is the unit wildcard, so `*.a.b` reads as a glob ("any one first segment, then a.b"), not "relative from here" |
| `..a.b` | rejected | lexically free, but `..` means PARENT everywhere in the world, while Lambda's parent step is `~~` — a permanent teaching tax |
| `./a.b` | rejected (was front-runner) | the universal relative spelling, but it collides with S10.5.1's postfix root step `value./.name`: `./` would then appear in BOTH line-start and postfix position, one dot apart |
| **`\.a.b`** | **ADOPTED** | lexically free (today `\` appears only in the `\(` / `\symbol(` pattern-island tags and as the alternative import-path separator); `\` already carries a path flavour from that import role; and it leaves `./` untouched, so the postfix-root-step collision never arises |

The decisive point over `./a.b` is the last one: choosing `./` for the
relative introducer would have created the very ambiguity this ruling
exists to remove, merely relocating it from `.` to `./`. `\.` touches no
existing path spelling.

**Payoff — with one correction to an earlier overclaim.** Retiring the
bare-`.` relative path means `.ident` at a line start has no start reading
left, so member access becomes its only meaning: the S16.2.4 carve-out
generalizes from `.ident(` CALLS to **full leading-dot fluent chains**
(`data` ⏎ `.filter(…)` ⏎ `.count`), the Swift/Kotlin style, on any member.

But `.` does **not** leave the dual-role set, contrary to the draft of this
section: `member_expr` admits an INTEGER field (`$.integer`), so `a.5` is
member access and `.5` is also a float literal — a live two-reading case
after a line break. `.` therefore becomes **sub-classified** rather than
retired: `.ident` is pure continuation, `.digit` stays dual-role. The
banned set becomes `( [ - + * ^ / <` plus `.digit`; the scanner already
sub-classifies `.` by its next character, so this costs no new machinery.
(A follow-on could retire integer member fields in favour of `[n]`
indexing, which would let `.` leave the set completely — not ruled here.)

**The mental model: `\` escapes the dot out of member access.** Reading
`\.` as an escaped dot is not a hazard to be tolerated — it is exactly the
right intuition, and the reason the spelling works. A bare `.` means member
access; prefixing the escape character says *this dot is not member
access, it introduces a path*. (`\.` also shares its first character with
the pattern-island tags `\(` / `\symbol(`, which diverge at the second
character, so no lexical conflict arises.)

**Imports keep `.mod` (decided).** The escape is needed only where the
ambiguity is: EXPRESSION position, where `.` collides with member access.
An import has a keyword introducer, so `import .mod` can only be a module
path — nothing to disambiguate and nothing to escape. `import .mod` and
`import \mod` both stand as they are; the new introducer does not
propagate there. This keeps `\.` meaning precisely one thing — "path,
not member" — rather than becoming a general relative-path decoration.

**Spec impact.** `S2.4.1` takes a **v3** at batch ratification (the
relative form respelled), and S16.2.4 takes a v2 (the carve-out widened
from `.ident(` to any `.ident`).

**Implementation status: DONE in both front ends.** Tree-sitter spells the
relative form `\\.` in `path_expr`; the C lexer emits a dedicated
`LAMBDA_TOK_PATH_REL` for it. Both widen the member-dot carve-out to any
`.ident` across a line break while keeping `.digit` dual-role — on the C
side that needed an extra test, since the lexer folds `.5` into a single
FLOAT token, so the token KIND alone cannot see the leading dot.

A further ambiguity dissolved as a side effect: `<svg .rect>` versus
`<svg, .rect>` (qualified tag versus tag-plus-path-child, S2.4.3v2's
motivating case) no longer competes at all — with the relative path
respelled, `.rect` cannot be a path, so `<svg .rect>` is unambiguously the
qualified tag and the path-child form is `<svg, \\.rect>`.

### 7.16 Digit-adjacent identifiers (decided and implemented — found by the implementation audit)

**Finding.** `let a = 123abc` parses as `let a = 123` followed by the
statement `abc`, and `let a = 0b1010` as `let a = 0` followed by `b1010`.
Neither is an error: S16.1.3 juxtaposition happily takes the identifier as
the next statement, so a typo or a habitual `0b` literal becomes a silent
two-statement split whose failure surfaces later as an undefined name.

**This is the §7.3 bug class, unfixed.** `1f32` had exactly this shape — a
number running into an identifier-like suffix, lexing as two tokens with a
context-dependent result — and §7.3 fixed it by making the spelling a valid
literal. The general case remains: no language admits `123abc`, and the
juxtaposition rule is what turns it from a lexical error into a silent
split.

**Candidate ruling (not decided).** Make a digit immediately followed by an
identifier-start character a LEXICAL error, after the existing suffix
families are matched (`1i32`, `1f32`, `1n`, `1m`, `4j`, `0xFF`). Juxtaposed
statements would then require a separator or whitespace — `123 abc` — which
costs nothing anyone writes deliberately. §7.5's ruling is unaffected
either way: it decided that no binary/octal literal FORM exists, which
remains true; this is about what `0b1010` should do instead of silently
splitting.

**Ruling: adopted and implemented.** A digit immediately followed by an
identifier-start character is a LEXICAL error, checked after the suffix
families are matched (`1i32`, `1f32`, `1n`, `1m`, `4j`, `0xFF`), so every
valid literal is unaffected while `123abc`, `0b1010`, and `1_` are rejected.
Juxtaposed statements simply need the whitespace nobody omits deliberately
(`123 abc`). §7.5 is untouched: it decided that no binary/octal literal FORM
exists, which stays true — this decides what `0b1010` does *instead* of
silently splitting.

The C lexer checks adjacency directly. Tree-sitter cannot express the
constraint in a regex (no lookahead), so `_number` carries a zero-width
`_num_boundary` guard that the external scanner withholds when the next
character continues an identifier — and, uniquely among the guards, it is
tested BEFORE any whitespace is skipped, since adjacency is the whole
point.

### 7.17 Known limitation: a comment can defeat the line-start guard in Tree-sitter

`let a = 1` ⏎ `/* c */ + 2` is correctly rejected by the C parser (S16.2.3:
`+` is dual-role at a line start) but ACCEPTED by the Tree-sitter reference
grammar, which reads it as one continued expression.

**Cause.** The scanner is stateless by design, so incremental parsing and
GLR speculation stay safe. It detects a line break only in whitespace it
skips itself. Here it skips the newline, declines to emit a guard, and
returns false — whereupon Tree-sitter consumes the newline and the comment
as `extras` and re-invokes the scanner *after* them, where no line break is
visible any more. Making the scanner remember the break across invocations
would require state that cannot be cleared reliably, because the scanner is
not consulted at every token.

**A fix was attempted and reverted (2026-08-22).** The obvious remedy is to
let the scanner OWN comments — declare `comment` as an external so the
scanner is consulted at every token position, and carry the line break in
one byte of state cleared by the next invocation. That does fix the case
(both `1` ⏎ `/* c */ + 2` and the `//` form reject correctly, with no stale
carry), but it regressed real corpus files: a function preceded by **three
or more consecutive comments** started rejecting an ordinary `x < 0` inside
its body. Allocating the scanner state per instance rather than sharing a
static did not help, so the interaction is with how Tree-sitter checkpoints
external-scanner state around consecutive extras, not with the carry logic
itself. Trading corpus regressions for one obscure corner is the wrong way
round, so the change was backed out and the scanner stays stateless.

**Two repair attempts failed (2026-08-22).** Making the scanner OWN comments
— emitting them as an external token and carrying the line break in one byte
of state — works for the guard itself but breaks ~320 other files. The carry
goes stale: the reasoning was that COMMENT, being valid wherever extras are,
would have the scanner consulted at every token and so cleared every time. It
is not. When no external is valid for several tokens, `nl_pending` survives
past its comment and wrongly blocks a later SAME-LINE operator
(`width - left_margin` two lines below a comment). A second variant, keeping
the leading `/` inside the emitted token, additionally mis-positions the
internal lexer on the division path. Both were reverted; the harness asserts
nothing here, and this note stands in place of the fix.

**Assessment.** The divergence is one-directional and benign for the
cross-check lane: the reference grammar accepts a little more than
production, and the compare lane flags the opposite direction (source
Tree-sitter rejects but the C parser accepts). The production parser is
correct, so no shipped program is mis-parsed. The two cases are asserted in
`test/c_s16_conformance.sh` only, with a comment in the TS suite saying
why. Revisit if the scanner ever needs state for another reason — but only
with the corpus cross-check as the gate.

### 7.18 `;` has exactly one role — and why `,` cannot replace it

**The inventory (audited 2026-08-22, both parsers, probe-verified).** After
§7.7 retired `apply;`, §7.11 retired the element and object-type dividers,
and ruling 15 banned terminator/trailing use, `;` means **statement
separation and nothing else**. It is legal in exactly the places a statement
list occurs — fourteen contexts, all the same role:

| Context | Example |
|---|---|
| document top level | `let a = 1; a` |
| block expression | `{ let y = 1; y }` |
| `fn` body | `fn f() { let a = 1; a }` |
| `pn` body | `pn main() { let a = 1; a }` |
| element content | `<div "a"; "b">` |
| `if` braced body | `if 1 { let a = 1; a }` |
| `else` braced body | same shape |
| `for` braced body | `for x in [1] { let a = x; a }` |
| `while` body | `while 0 { let a = 1; a }` |
| `match` arm braced body | `case int { let a = 1; a }` |
| handler body | `f() ^ { let a = 0; a }` |
| handler value arm | `~ { let a = 1; a }` |
| `view`/`edit` body | `view P: int { let a = 1; a }` |
| `on` event-handler body | `on click() { let a = 1; a }` |

These correspond exactly to the `parse_content` call sites in the C parser
and the `content` / `element_content` / `_body_block` / `_braced`
references in the grammar. Everywhere else `;` is a syntax error, verified
in both parsers: object-type members, map items, array items, call
arguments, parameter lists, and attribute lists all take `,`.

One nuance: `import math; sys` parses, but not as an import separator — it
is `import math`, statement separation, then a bare `sys` expression. The
import list itself is `import math, sys`.

That single role is the clearest measure of what this design bought. The
grammar began with `;` as a statement terminator, an element attr/content
divider, an object-type section divider, and a fused `apply;` lexeme, plus
an optional-trailing allowance — five jobs. It now has one.

**Why `,` cannot finish the job.** The tempting last step is to replace
`;` with `,` and drop the character entirely — `{ a, b }` instead of
`{ a; b }`. It does not work, for a reason the `let` grammar makes
concrete. Lambda already overloads `,` inside a declaration, and both
overloads compose (probe-verified with values):

```
let a = 1, b = 2                    // multi-declare: two declarations
let a, b = [1, 2]                   // decomposition: two targets, one value
let a, b = [1,2], c, d = [3,4]      // BOTH at once — a=1 b=2 c=3 d=4
let a, b = [1,2], c = 5             // decomposition then multi-declare
let a = 1, b, c = [2,3]             // multi-declare then decomposition
```

The comma is already the declaration separator *and* the decomposition
separator, told apart only by where the first `=`/`at` falls (§7.8). Making
it the statement separator too would give `{ let y = 1, y + 1 }` no
determinate reading: the `,` could continue the declaration list or end the
statement, and the parser cannot know until it has consumed an arbitrary
amount of what follows. `;` earns its place precisely by being the one
delimiter the declaration grammar does not use — which is also why
`{ let y = 1; y }` reads correctly while `{ let y = 1, y }` does not.

**`let` binds differently at statement and expression level (recorded
2026-08-22; probe-verified).** The comma overloads above are a statement-level
grammar. In expression position — the parenthesized `let` chain — each binding
needs its own `let`:

| Form | statement | expression |
|---|---|---|
| `let a = 1, b = 2` | ✓ | ✗ |
| `let a, b = [1,2]` (decomposition) | ✓ | ✓ |
| `let a, b = [1,2], c, d = [3,4]` | ✓ | ✗ |
| `(let a = 1, let b = 2, a + b)` | — | ✓ |

So decomposition works in both, but *chaining* declarations is
`(let a, b = [1,2], let c, d = [3,4], a + d)` at expression level.

This is structural rather than an oversight, and it is the same
"where does the list end" problem that produced the element boundary comma
(§7.11) and the closed/open tail rule (§7.14). An expression-level `let`
chain is TERMINATED BY ITS RESULT EXPRESSION, so the parser needs a marker
for "another declaration follows" — the repeated `let` is that marker, and
it decides with one token of lookahead. Admit the bare comma-list form and
`(let a = 1, b, c)` has no determinate reading: `b, c` is equally a
decomposition awaiting its `=` and the result expression. At statement level
no result expression competes, so the comma list is unambiguous there.

### 7.19 Considered and rejected: `,` everywhere (`{ , , , }`)

The last simplification available was to retire `;` entirely and separate
statements with `,` as well — one delimiter for the whole language, nothing
to toggle between. It was compared against the current `{ ; ; ; }` design
and **rejected 2026-08-22**.

**The deciding reason: Lambda is not a pure expression language.** Lisp can
get away with one separator because it has one kind of thing. Lambda
deliberately has BOTH expressions and statements — declarations, control
forms, procedural bodies — and that distinction is real, not incidental.
The delimiters should therefore mirror it rather than blur it:
**`,` separates items inside an expression-level construct; `;` separates
statements.** Merging them would not remove a concept, only hide one that
still exists. The programmer already carries the distinction in their head;
the two delimiters make it visible on the page.

**Pros of `,` everywhere.** One delimiter to learn and type; no switching
between `,` and `;` as the reader moves between construct interiors and
statement lists; a more homogeneous, data-like surface — arguably a fit for
a language whose subject matter is data and documents.

**Cons, in the order that decided it.**

1. **The pro is smaller than it looks, because `;` is already rare.** Under
   S16.1.3 juxtaposition and §7.14 closed tails, idiomatic Lambda contains
   almost no semicolons: statements juxtapose, and `;` surfaces only at
   genuine ambiguity points. Meanwhile `,` is everywhere — every map, array,
   call, parameter list, attribute list. The change would make the RARE
   delimiter adopt the UBIQUITOUS one's spelling, and the rare one is rare
   precisely because it marks the places a reader most needs a distinct
   signal.
2. **Four declaration forms collide, not just `let`.** Every comma-list
   declaration would have to repeat its keyword: `let a = 1, let b = 2`;
   likewise `var` and `type T = int, let U = string`. Multi-declare and
   decomposition chaining (§7.18) — among the most common statements in the
   language — become verbose.
3. **`import` becomes genuinely ambiguous**, not merely verbose.
   `import math, sys` is an import list today, while `import math; sys` is
   an import followed by a bare `sys` statement. With one delimiter those
   two readings collapse, re-introducing an ambiguity the current split
   resolves for free.
4. **It moves the overloading rather than removing it.** §7.18 records that
   `;` now has exactly ONE role, down from five. Comma-only does not
   eliminate a delimiter; it gives `,` FOUR roles — list items,
   multi-declare, decomposition, statements — which is the opposite
   direction from every other ruling here.
5. **`{ a, b }` reads as a collection literal** to anyone arriving from any
   other language, and sits right next to `{a: 1}` meaning a map. The
   §5.9v3 interior test still distinguishes them mechanically, but the
   reader is the one who pays.
6. **Erlang is the precedent, and it is a known wart.** Erlang separates
   expressions in a body with `,` (and clauses with `;`), which is among the
   most commonly cited complaints about its syntax.

**What would change the answer.** If Lambda's identity were "everything is
a comma-separated sequence" — a homogeneous, data-shaped surface with no
statement/expression split to express — comma-only would serve that
identity and the verbosity would be its honest price. That is a coherent
language; it is simply not this one.

### 7.20 Fluent chains and the dotted module name are one rule

§7.15 retired the bare-`.` relative path, which left `.ident` at a line
start with no start reading — member access became its only meaning. The
S16.2.4 carve-out therefore widened from the `.ident(` CALL form to ANY
member, and that is what makes leading-dot chains work:

```
data
.filter(fn (x) => x > 0)
.map(fn (x) => x * 2)
.count
```

**The same rule explains the dotted module name.** `import math` ⏎ `.sub`
continues into the module `math.sub`, which looks odd in isolation but is
not a special case: it is `.ident`-continues applied to an import. Guarding
it was considered and rejected — allowing leading-dot chains in expressions
while forbidding them after `import` would be two rules where one suffices,
and the golden test is satisfied either way (`import math .sub` on one line
is also `math.sub`, so no line break changes meaning).

Note the deliberate asymmetry with the other module separator: `import a` ⏎
`\\b` IS rejected, because `\\` is a START token (it opens `\\.` paths and
`\\(` pattern islands) rather than a continuation. The two separators
diverge because their token classes diverge, which is the rule doing its
job rather than an inconsistency.

### 7.21 Two rulings that shipped unimplemented (found by the second audit, now closed)

A ruling-by-ruling audit on 2026-08-22 — probing the ledger rather than
re-running the suite — found two rulings that were never enforced. Both were
shared gaps, not divergences: the parsers agreed with each other and
disagreed with the design.

**§3.6 handler brace, same line as its `^`.** `f() ^` ⏎ `{ 0 }` parsed as a
propagate followed by a separate block statement, while `f() ^ { 0 }` on one
line is a handler — different parse trees for the same token stream modulo a
line break, which is an S16.1.1 violation.

The framework names the cause exactly: after `f() ^` the expression is
*complete but extensible*, so a following `{` is DUAL-ROLE — handler body,
or a new map/block statement — and S16.2.3 requires that neither reading
win. The fix must be applied AT THE CARET, not at the brace: blocking only
the handler path lets GLR fall through to propagate-plus-a-statement, which
is the silent split itself. So the scanner now looks past the `^`, and if a
`{` follows across a line break it emits NO caret token at all, failing the
parse loudly. The C parser makes the same check on the brace's `nl_before`
flag. Repairs: keep the brace on the caret's line, or write `f()^;` ⏎
`{ … }` for a bare propagate followed by a block.

**§5.9v3 bare `{}` at procedural statement position.** `pn main() { {} }`
parsed. It is dead under either reading — a no-op block or a discarded empty
map — and this design answers meaningless input with an error. The C parser
now tracks procedural-body depth and rejects it there; `{}` remains a
meaningful empty map in value and content position, and in `fn` control
bodies. This rule is procedural-context-sensitive, so it is asserted in the
C suite only.

**Method note.** Both were invisible to the conformance suite AND to the
corpus cross-check, which agreed at 100% throughout. Corpus agreement proves
only that the two front ends match each other; it cannot show that either
matches the DESIGN. Only probing the ledger ruling by ruling finds this
class — and for anything involving precedence or evaluation, only value
assertions do (§7.2 shipped unimplemented for exactly that reason).

### 7.22 Optional type fields: `a?: T` (decided — a grammar addition)

**The distinction.** `a?: T` and `a: T?` are different claims and the type
language needs both:

- **`a?: T`** — the FIELD is optional. It may be absent from the value
  entirely.
- **`a: T?`** — the field is present; its VALUE is nullable.

Only the second was expressible. `attr_type` (and its pattern-position twin
`pattern_attr_type`) accepted `name : type [= default]` with no optional
marker, so an optional field had to be miswritten as a nullable one — losing
the distinction — or spelled some other way entirely.

**Ruling.** Both field rules take `optional(field('optional', '?'))` between
the name and the `:`, mirroring `parameter`, which has carried the same
marker for function arguments all along. The two markers now read alike:
`fn f(a?: int)` and `type T { a?: int }` both mean "may be absent."
`a?: T?` is legal and means an optional field whose value, when present, may
be null.

Surfaced by the corpus migration. (The case that raised it —
`<meta: Meta>?` in `doc_schema.ls` — turned out NOT to need this: checked
against `doc/Doc_Schema.md`, that construct is a child ELEMENT written with
a stray colon, and its Lambda spelling is `<meta Meta>?`. The gap it exposed
in the type language is real all the same, and the same file uses `a?: T`
elsewhere, in map-type position.)

The marker applies to every type-field position: `attr_type` (object-type
fields), `pattern_attr_type` (pattern position), and `map_type_item` — a map
type's fields are type fields too, which the first implementation missed.

**Implementation status.** Both parsers accept the marker, and the C parser
carries it as `LAMBDA_REDUCTION_FLAG_OPTIONAL` on the object-field
reduction — the same flag parameters already use. The SEMANTIC side is not
done: `build_ast` and the validator must treat an optional field as
"absent is valid" rather than "null is valid", which is the point of the
distinction and is tracked as remaining work.

---

### 7.23 Script top level is element content (decided — ratified as S16.7)

**Question.** A script body is a sequence of statements whose results are
observable. Is that sequence a *list* of results, or *content*? The two differ
observably, because content normalizes and containers do not.

**Ruling.** The top level is **element content**, modelled as the content of a
virtual `<file …>` / `<script …>` element. Two reasons, in order of weight:

1. **The syntax was designed to unify with element content.** Top-level
   juxtaposition (§7.14), separation (§3), and line-start classification (§3.2)
   are not a separate statement grammar that happens to resemble element
   content — they *are* the element-content rules applied to a file. Modelling
   the top level as a list would leave that unification as a coincidence.
2. **The mental model is teachable and complete.** "A script is the content of
   a virtual `<file>` element" answers, in one sentence, why bare strings
   juxtapose, why an `else`-less `if` contributes nothing, and why a script
   that produces a document produces it directly rather than wrapping it.

**Consequences — top level normalizes like content.**

- **Nulls are stripped.** A `null` reaching content contributes nothing,
  however it arose: written literally, read from a missing key (S7.1.1v2), or
  produced by an `else`-less `if` (S16.6.3). If stripping empties the content,
  the script's value is a single residual `null`.
- **Adjacent strings merge**, *after* stripping — so `"a" ⏎ null ⏎ "b"` is
  `"ab"`, not `"a" "b"`. The removed null does not separate its neighbours.
- **Containers do not normalize.** `[1, null, 2]` keeps its null. To observe a
  null, put it in a value (`let r = [s.b]`); a bare `null` statement is
  invisible by design.

**Accepted cost.** A bare missing-key read at top level prints nothing, which
reads as silent data loss to someone expecting list semantics. This is the
same class of trap the line-start ban (§3.2) was written to remove, and it is
accepted here only because the content model is the primitive: `<d "a" null
"b">` must yield `"ab"`, and the top level cannot diverge from the element
form it is defined as.

**Implementation status.** Already conformant — no engine change was required.
`list_push` (`lambda/runtime/collection_runtime.cpp`) skips `LMD_TYPE_NULL`,
and adjacent-string merging is the existing content-normalization rule (§7.9).
Verified at top level and in element position: `"a" ⏎ "b" ⏎ "c"` → `"abc"`,
`"a" ⏎ null ⏎ "b"` → `"ab"`, `<d "a" null "b">` → `"ab"`, `null` → `null`,
`[1, null, 2]` → `[1, null, 2]`. S16.7.1–S16.7.3 are therefore ratified
**unmarked**, unlike the rest of S16.

### 7.24 Keywords as names: bindings never, data names yes (decided 2026-08-27)

Keyword handling was a patchwork — the same word accepted in one name
position, rejected in the next, and silently misread in a third. Probe
evidence (2026-08-27, C parser, debug build):

| Probe | Behavior |
|---|---|
| `{type: 1, in: 2}` … `m.type + m.in` | works → `3` |
| `{if: 1}` … `x.if` | works → `1` |
| `<div if:1, "t">` … `v.if`; `<div int:1, "t">` … `v.int` | work |
| `<if a:1, "x">` | rejected: *expected an element tag* |
| `<'if' a:1, "x">` … `v.a` | works → `1` |
| `import edit: .lib.mod` … `edit.x` | import parses; **every use fails** *expected a type pattern* — the `edit` declaration keyword captures the statement |
| `import 'edit': .lib.mod` … `'edit'.x` | parses; use **silently yields null** — an unreachable binding |
| `let if = 1` … `if` | let parses; use fails *expected an expression* |
| `let type = 1` … `type` | parses; `type` **silently reads the base type** and prints `type` — a silent wrong answer |
| `let last = 3` | `error[E201]: 'last' is a reserved keyword …` at the declaration — the correct model, applied to exactly one keyword |
| `let order = 5; order + 1`; `let state = 1` | work today — but capture contexts exist (a `state` read inside a `view`, `order` in for-clause space) |

**The dividing principle.** A keyword can safely serve as a name exactly
where **both the defining occurrence and every use are sigil-guarded** —
after `{` `<` `,` before `:`, or after `.` — positions where no keyword
construct can begin, so no capture is possible. Map keys, attribute names,
and `.`-member steps pass on both sides. Binding names fail structurally: a
binding is re-spoken at statement-leading and expression positions where
keyword constructs begin, so a keyword binding can never be read everywhere
a binding must be readable — hence the accept-at-declaration /
fail-at-use traps above, and the worse silent-misread for `let type`.

**The four rulings (user, 2026-08-27; ratified as S16.10, spec v16.0.0):**

1. **Bindings ban the entire lexer keyword table** — statement and clause
   keywords (`view`, `edit`, `state`, `order`, `last`, … are all reserved),
   word operators, base-type words, and the named values — as `let`/`var`
   names, parameters, `fn`/`pn`/`type`/`view` declaration names. The error
   is E201 at the declaration site, extending the mechanism `last` already
   has. **No quoted escape for bindings**: a binding name used in an
   expression cannot be told apart from a `'symbol'` — symbols never
   implicitly read bindings (S2.4.3) — so no spelling makes a keyword
   binding readable. One-line teachable: *keywords never name bindings.*
2. **An import alias is a binding** and follows rule 1: `import edit: …`
   and `import 'edit': …` are both rejected at the import line; the repair
   is choosing a different alias. (Corpus: 0 keyword import aliases.)
3. **Data-container names admit keywords** — map keys, element tags, and
   attribute names: `{type: 1}`, `<div class:"a">`, and **`<if a:1, "x">`
   becomes legal** (the current bare-tag rejection was implementation, not
   a ruling — no prior point banned it, and S15.1's own observation that
   HTML has a real `<var>` requires keyword tags for ingested markup to
   round-trip as source). Advisory, not enforced: prefer the quoted-symbol
   spelling (`<'if' …>`, `{'type': 1}`) where a bare keyword would read as
   its construct.
4. **Keywords in member access are ordinary names**: after `.`, `m.type`
   and `x.if` read data members (composing with S16.2.4v2 member
   continuation, whose post-dot word must be read as a name regardless of
   its keyword kind). Subscripts are *expression* space, not name space —
   `a["type"]` is a string key and `last` keeps its S7.2.2 subscript
   meaning; nothing changes there.

**Clarification: which names in a declaration are bindings** (added
2026-08-27, user; spec S16.10.2). The bar applies to the *declared name*,
never to the members a declaration introduces:

```lambda
type T {           // T IS a binding — S16.10.1v2 applies
  a: int,          // a is NOT a binding — a data name (field)
  order: int,      // …so a keyword field is fine
  fn f() { … }     // f is NOT a binding either — a data name (method)
  fn state() => a  // …and a keyword method name is fine
}
```

Only `T` is spoken bare in expression position, so only `T` can be captured
by a keyword construct. Fields and methods alike are reached through a
receiver — `x.a`, `x.f()` — which is sigil-guarded by the leading `.`
(S16.10.3), so no capture is possible and the S16.10.1v2 argument simply
does not apply to them. Two consequences worth stating:

- **A method is never a shadow.** S12.3.7 governs module-level bindings; an
  object method named `sum` does not shadow the system function, it is
  reached only as `x.sum()`. (S12.3.3 already rules that a receiver member
  wins over a method-eligible builtin at such a call.)
- **Renaming a method is an API change**, not a local edit: its call sites
  are `.name(` spellings that no binding-rename pass will touch.

Implementation anchor: `push_name` gates the E201 bar on
`ast_node_declares_binding`, which excludes `AST_NODE_KEY_EXPR` — the scope
entries object-type fields and methods register for bare-field resolution
inside the type body. This was found the hard way: the first migration pass
renamed `fn state()` in `test/lambda/proc/object_direct_access.ls` while its
`tog.state()` call sites kept the old spelling, and the method silently
resolved to nothing (booleans printed as empty strings).

**Rejected alternatives.** (a) *Quoting as the binding escape*
(`import 'edit':` + `'edit'.x`) — breaches S2.4.3's symbol/name
separation, and today's behavior shows the failure shape: a silently
unreachable binding. (b) *Per-keyword contextual carve-outs* (keep
`order`/`state` bindable since they work today) — they work only until the
capture context is entered; per-context carve-outs are the fragility this
ruling removes. (c) *Banning keywords in data names too* (quote-everything)
— forces quoting onto the most common data idioms (`type:`, `class:`,
ingested JSON/HTML keys) for zero disambiguation gain.

**Migration and status.** 55 keyword-named bindings in `test/` + `lambda/`
(offset 12, group 9, state 8, to 5, by 4, range/order/list/div/desc 2 each,
others 1); 0 keyword import aliases. Enforcement must land in the C parser
and the reference grammar, and E201 must extend from `last` to the whole
table. Tracked as LR02-14 in the issue ledger; the `let type` silent
misread is the priority defect.

### 7.25 Sys-func shadowing: user-first, module-lexical (decided 2026-08-27)

May a user definition override a system function? Probes (2026-08-27,
debug build) showed the territory was **undefined and crashing**, not
merely unruled: `fn sum(a) => 99` then `sum([1,2,3])` compiles, executes on
the interpreter tier, prints **no result**, and dies at teardown (ASan
dealloc failure); `fn len(s) => -1` and `fn min(a,b) => "mine"` likewise.
Unshadowed calls (`sum(x) + len(x)` → 9) are fine. So no de facto behavior
existed to preserve — LR02-15 tracks the crash.

**The ruling (user, 2026-08-27; ratified as S12.3.7, spec v16.1.0):
user-first shadowing, module-lexical, with a warning.**

1. A module-level `fn`/`pn` **or value binding** whose name matches a
   system function shadows it for every call site **in that module/script
   only**, resolved statically. Resolution is never global — unlike JS
   prototype/global mutation, no other module's `sum` changes.
2. **Every such shadowing draws a compile (syntax) warning** — accidents
   surface, intent is permitted. This is normative, not an optional lint.
3. A shadowing definition is an ordinary definition: **`pub` exports it
   like any other**, and it then extends to the importing script through
   the explicit import — propagation is by export/import only, never
   ambient.
4. Uniform with value bindings: `let sum = 5` shadows too (a name binds to
   exactly one thing, S12.3.6); `sum(x)` is then the ordinary not-callable
   type error — **never a silent fallback to the builtin**.
5. The effect bit follows the actual callee (S12.1): shadowing `pn print`
   with an `fn` yields an fn, checked as such.
6. The reserved core is un-shadowable **for free** via S16.10.1 (§7.24):
   keywords and base-type words cannot be binding names, so `int()`,
   `string()`, `float()`, `type()` stay intrinsic. Two tiers fall out of
   existing rulings: a reserved core that can never be rebound, and an
   open library namespace that is user-first.

**Grounds, in decision order:**

1. **Forward compatibility is the key issue** (user). Under user-first
   resolution, adding a NEW sys func never changes the meaning of an
   existing program — the user's same-named definition keeps winning. Under
   sys-func-first or collision-error, every stdlib addition is a breaking
   change for someone. Lambda's sys-func surface is deliberately still
   growing (RF-series, S14.2 verbs and window functions), so this is the
   rule that lets the library grow without MAJOR events.
2. **Symmetry with S12.3.3**: member calls already resolve receiver-first,
   with the same rationale — every builtin name would otherwise be a
   latent trap in user code.
3. **S1.11 references agree**: ECMAScript and Python both allow user
   definitions to shadow builtins.
4. **Zero cost, unlike JS**: no eval (S1.8), no reference cells or
   monkey-patching (S9.1), static modules — so "is this name bound in this
   module?" is decidable in `build_ast` per call site. Calls to unshadowed
   builtins keep their intrinsic fast paths exactly; one resolution point
   keeps both execution tiers in agreement.

**Rejected alternative — collision = compile error.** It buys **maximum
protection against silent capture**: a user redefining `len` with different
semantics silently changes distant code in the same module, and a hard
duplicate-definition error makes that impossible. But it freezes the stdlib
namespace — every future sys-func name would collide with someone's
existing definition, so the protection is bought at the price of the
forward-compatibility property in ground 1, which is the key issue. The
mandatory warning (point 2) recovers most of the silent-capture protection
at none of the evolution cost.

**Deliberately deferred**: a spelling to reach a shadowed builtin from
inside the shadowing module (Python's `builtins.len`; ECMAScript has none
and survives). If ever demanded it rides the module system as a prelude
import, not new syntax — recorded as SO37.

---

## Appendix S — Superseded Rulings

Text that once stated a ruling and no longer does. It is kept for the
decision record only. **Nothing in this appendix is normative**; each entry
names what replaced it. Struck-through text is the superseded wording
verbatim.

### S.1 Blanket dual-role rule (§3.2 Rule 3, §3.3) — SUPERSEDED by §7.14

> ~~**Dual-role tokens** — tokens that could do either. **Syntax error.** The
> user must either write `;` (to start a new statement) or move the token to
> the end of the previous line (to continue). There is no default.~~
>
> ~~**Dual-role tokens (syntax error at line start after a complete
> expression)**~~

**Why it fell.** Applied after *any* statement, the rule made
`fn pick2(x) { … }` ⏎ `[pick(2), pick2(130)]` demand a `;` after the
declaration — a separator no brace language requires, and the single largest
class in the corpus migration.

**Replacement:** §7.14 closed-tail juxtaposition. The error survives only
expression → expression; after a closed-tail statement a dual-role-led
expression juxtaposes freely. The token *classes* of §3.3 were never
superseded — only this application of them.

### S.2 `{ ... }` Options 1 and 2 (§5.9) — SUPERSEDED by §5.9 v3

> - ~~**Option 1** — interior-shape dual reading (≈ the pre-S16
>   implementation).~~ Rejected: ambiguous `{}`, and a pn-statement map
>   restriction.
> - ~~**Option 2** — position decides unconditionally: body braces always
>   block, everything else always map; a map value in body position is
>   parenthesized (`if (c) ({a: 1})`) or block-wrapped.~~ Adopted briefly,
>   then rejected: ugly escapes, and it leaves arrow functions without block
>   bodies. **This is the version that was mistakenly ratified into the spec
>   as S16.4.1 on 2026-08-21**; corrected to S16.4.1v2 + S16.4.2 + S16.4.3
>   on 2026-08-22 (spec v10.0.0). If you find `if (c) ({a: 1})` presented as
>   a required repair anywhere, it is Option 2 residue — the parens are
>   unnecessary.
>

**Replacement:** §5.9 **v3** — interior decides, context breaks the empty
tie, and grouping parens never flip the reading. Ratified as
**S16.4.1v2 + S16.4.2 + S16.4.3** (spec v10.0.0, 2026-08-22).

**Erratum.** Option 2 was ratified into the spec as S16.4.1 on 2026-08-21 by
mistake, making `if (c) {a: 1}` a syntax error and prescribing
`if (c) ({a: 1})` as the repair. Any occurrence of that repair anywhere in
the tree is Option 2 residue — the parens are unnecessary. The C parser
still implements the erratum for the paren-head spelling (spec Appendix A).
