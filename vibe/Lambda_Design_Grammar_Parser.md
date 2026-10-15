# Lambda Grammar Module — Design

**Revision:** 5 (2026-10-15)
**Status:** GM1–GM4, GM4.1, GM4.2, GM5v2, GM5.1, GM5.2 ruled by USER on
2026-10-11 (see §0); GM6–GM12 proposed. **Implemented 2026-10-11** (P0–P3 of
[the plan](impl/Lambda_Impl_Grammar_Parser.md)): declaration, loader, lint,
engine with left recursion, `parse(text, g[, 'rule'])`, and GM5.3's
`text is g`. Implementation found two corrections, recorded in §4.2
(keyword-token boundaries) and §5.2 (the growth cap). Revision 3, the same
day: GM4.3, GM5.4, GM11v2 and the P5 scope ruled; composition (§4.5), spans
and the `lambda.grammar.xpath` package implemented (P4, P5). Revision 4
(2026-10-12): local `:=` rules see imported ones and composition works across
modules (§4.5); GM13 (`~x`, no space before a token) ruled and implemented
(§4.3); M4 `lambda.grammar.jq` and M5 `lambda.grammar.xquery` and
`lambda.grammar.rnc` done (§10); §5.4 compares the left-recursion mechanism
with Pratt parsing. Open rulings in §9.
**Formal linkage:** [D1.1](../doc/Lambda_Formal_Design.md#d1-architecture-principles)
(a guest is a grammar + AST builder + LangProfile), [S15.1 / S15.3](../doc/Lambda_Formal_Semantics.md#s15-metaprogramming)
(the `lm.` AST is an element tree; `compile(ast)` is the only way from data
to code), [D8.2.1](../doc/Lambda_Formal_Design.md#d82-unified-ast) (one core
node catalog serves host and guests), [S11.1.2v4](../doc/Lambda_Formal_Semantics.md#s111-types-compose-like-values)
(string patterns, which lexical rules reuse), [S16.10.1v2](../doc/Lambda_Formal_Semantics.md#s1610-keywords-as-names)
(`grammar` becomes a barred word). No `S#`/`D#` ruling covers grammar
declarations yet; the rulings below are candidates for ratification.
**Scope:** a grammar-based parser *module* — grammars declared in Lambda
source, interpreted at run time, producing a Lambda element tree that a
Lambda transpiler then maps to the `lm.` AST. Target guests: jq, XPath,
XQuery, RELAX NG compact, and similar small expression languages.
**Not in scope:** replacing Lambda's own C parser
([Lambda_Grammar_Parser.md](Lambda_Grammar_Parser.md), CGP1–CGP21), the
Tree-sitter reference grammars, or the retired hosted-language runtime
(D8.2.3v2).
**Series:** `GM#` (grammar module). New area; no existing series covers a
user-declared grammar facility (CGP = the C parser of Lambda itself, SP =
string patterns, U = unified AST).

## 0. Decisions (USER, 2026-10-11)

| ID | Ruling |
|---|---|
| **GM1** | The engine is a **PEG-style recursive-descent interpreter**: ordered choice, backtracking, built-in precedence climbing for operator rules, and a **static lint pass** that warns about the classic PEG traps. Not LL(k) tables, not LALR/LR, not GLR. |
| **GM2** | `\|` is **ordered choice, first match wins**, as in PEG. The lint warns when the order makes a later alternative unreachable or prefix-captured. |
| **GM3** | The inter-token whitespace rule is named **`space`** (`space := ...`), not `skip`. |
| **GM4** | The parse result is a **Lambda element tree** (one element per rule, tokens as strings), built the way every other input parser builds its result. The guest transpiler is an ordinary Lambda function over that tree. |
| **GM4.1** | Literal tokens are **double-quoted** (`"+"`, `"for"`) — the same lexical form as a `:=` fragment. **`'name'` and `name` keep their Lambda meanings everywhere in the block: a symbol and a name.** Neither is ever a token literal; single quotes are not repurposed. |
| **GM5v2** | **A grammar is a type**, declared and carried exactly like `type`: a `Type` of a new kind, compile-time only, owned by its module. **Each of its rules is a type too**: a `:=` rule is the string-pattern type it already is (S11.1.2v4); a `=` rule is a rule type that knows its grammar. `grammar` is a declaration keyword, barred like `type` (S16.10.1v2). `type(calc)` is `type`, as for any type value (S2.1.2). *(v1 had proposed a value of a new nominal kind reporting `grammar`.)* |
| **GM5.1** | **`parse(text, g)` and `parse(text, g, 'rule')`** are the V1 surface; with no rule named, **the first rule in the block** is the entry. `input(target, g[, 'rule'])` is the file form. |
| **GM4.2** | **A `:=` rule's quote domain decides what its match emits**: a `'...'` rule (`for := 'for'`) emits a **symbol**, a `"..."` rule (`word := "\a"+`) a **string**. The island's domain rules apply unchanged (S11.1.2v4: one domain per island, mixed quotes are a compile error). Literal tokens in `=` rules stay strings (GM4.1). Ruled while choosing how tokens appear in the tree, because element content merges adjacent strings (S2.6.4, §7). |
| **GM4.3** | **`'x'` in a `=` rule is a name and resolves to a rule name** (closes GM-O3): `'word'` refers to the rule `word`, as a bare `word` does. As a name it also spells a label: `'data-x': word` gives an attribute `data-x`. A symbol is never a literal token. |
| **GM5.4** | **`text is g.rule` is not valid syntax** (closes GM-O8): bind the rule first — `let r = g.rule` then `text is r`. **`input(target, g)` is not a supported usage** (closes GM-O9). |
| **GM11v2** | **Spans and the entry rule go in a third-argument options map**: `parse(text, g, {rule: 'expr', spans: true})`; `parse(text, g, 'rule')` stays. *(v1 wrote `{'spans': true}` alone.)* |
| **P5 scope** | **A guest grammar only parses**: `lambda.grammar.xpath` turns XPath into a Lambda element tree; evaluating it, and lowering to `lm.` (whose `compile`, S15.3, is unimplemented), are out of this phase. The XPath grammar is written **Tree-sitter style** — one directly left-recursive `Expr` with a precedence ladder and labels — rather than as a line-by-line EBNF transcription, which would make every precedence level a node. |
| **GM13** (2026-10-12) | **`~x` matches `x` where the previous token ended**: no `space` is skipped before it — Tree-sitter's `token.immediate`. It marks one literal or `:=` rule reference (`~(a \| b)` and `~rule` on a `=` rule are errors), so `~x*` is `(~x)*`. It is how a space-sensitive position is written inside spaced text: jq's string text after an interpolation, XQuery's element content and attribute values around `{ … }`. Ruled over an exit from `nospace` regions (a `spaced` modifier) and over per-rule `nospace`, after comparing with Tree-sitter, which has no regions: whether space may precede something belongs to that token. `nospace` stays for whole space-free regions. |
| **GM5.2** | **Diagnostics align with the existing parsers**: compile-time grammar errors use the script parser's E-code scheme and reporters; a failed `parse` is the same `ERR_PARSE_ERROR` every input parser raises, with the furthest-failure message as its `parse_error_message`; lint warnings go through the warning channel the script compiler already uses. No new catalog, no new tags. |
| **GM14** (extension implemented 2026-10-15 for the authorized diff package) | `parse(binary, g[, rule/options])` reuses the grammar over a byte alphabet. Ordinary lexical captures are binary (empty captures are `null`, S2.2.2v2); symbol captures remain symbols. Text input keeps Unicode/string behavior. Script-facing grammar results are GC-owned, with private parse storage released per call (D4.2.6/D5.3.3). Byte spans are checked before narrowing to 32 bits. |

USER also asked whether Tree-sitter-style precedence rules (`prec.left(n, …)`
on directly left-recursive alternatives) can be supported. Yes — §5 gives the
mechanism (bounded left recursion with precedence levels) and the limits.

## 1. Goal

Add a `grammar` declaration to Lambda so a script can define a language —
its tokens as string patterns, its syntax as rules — and parse text in that
language into a Lambda tree, then transpile the tree into the `lm.` AST and
run it on the Lambda runtime. The first guests are query languages whose
semantics map well onto Lambda's data model: jq, XPath 3.1, XQuery,
RELAX NG compact syntax.

```lambda
grammar calc {
    space  := \("\s"+ | "//" !"\n"*)              // GM3: inter-token rule
    number := \("\d"+ ("." "\d"+)?)
    ident  := \("\a" "\w"*)

    expr   = prec.left(1, expr, "+", expr)
           | prec.left(1, expr, "-", expr)
           | prec.left(2, expr, "*", expr)
           | prec.left(2, expr, "/", expr)
           | prec(3, "-", expr)                      // unary minus
           | prec.right(4, expr, "^", expr)
           | primary
    primary = number | ident | "(", expr, ")"
}

let tree = parse("1 + 2 * 3", calc)^
// <expr <expr <primary "1">> "+" <expr <expr <primary "2">> "*" <expr <primary "3">>>>
```

The design follows D1.1 literally: a guest is **a grammar** (this module),
**an AST builder** (the Lambda transpiler from the parse tree to `lm.`
elements), and a **LangProfile** (D8.2.1, operator semantics dispatched per
compilation unit). Nothing here adds a second AST or a second code path into
the runtime: the only exit is `compile(ast)` (S15.3), so a guest script is
data until the moment it is compiled, and strings are never code.

## 2. The question: LL(k), LR(k), LALR, GLR, or PEG?

For a general parser the usual options are:

| Family | What it is | Verdict |
|---|---|---|
| LL(1)/LL(k) tables | predictive parse from FIRST/FOLLOW sets, fixed lookahead | too weak for XQuery (needs 2–3 tokens of lookahead in places), and no simpler to build than backtracking |
| LALR(1) / LR(1) | bottom-up, automaton tables (yacc/bison) | strongest deterministic class, but conflicts are opaque, grammars do not compose, a separate lexer is forced, and the generator is the largest piece of code |
| GLR | LR with forked stacks on conflict (Tree-sitter) | handles ambiguity we do not need; a generator plus a graph-structured stack is out of proportion for a scripting-language module |
| Earley / GLL | general CFG, O(n³) worst case | same objection, plus speed |
| **PEG / backtracking RD** | ordered choice, unbounded backtracking, optional memoization | **chosen** — see §3 |
| ALL(*) (ANTLR) | adaptive LL with dynamic DFA lookahead | PEG-class power with a far more complex engine |

Speed was not a deciding factor. Deterministic LL and LR both parse in O(n);
hand-written recursive descent is usually fastest in practice, and the scripts
parsed here are small and parsed once before transpilation. Lexing and tree
construction dominate anyway.

## 3. GM1 — Why a PEG-style recursive-descent interpreter

Recorded reasons, in order of weight:

1. **The target specifications are already written this way.** XPath 3.1 and
   RELAX NG compact are published as EBNF with explicit lookahead notes; they
   transcribe nearly line by line into ordered-choice rules with repetition.
   jq's grammar is a bison file with `%left`/`%right`/`%precedence`
   declarations — it needs precedence annotations under any engine, which
   §5 provides. None of the four needs more than ordered choice +
   backtracking + precedence.
2. **XQuery is context-sensitive at the lexical level.** `<` is a tag start
   or a comparison depending on parser state; keywords are not reserved;
   direct element constructors switch the tokenization rules. With a separate
   LALR lexer this is the "lexer hack". With parser-driven lexing (§4.2) the
   rule being parsed says which tokens are legal at this position, so lexer
   modes fall out for free and need no mode syntax.
3. **Grammars must compose.** XPath embeds in XQuery, XQuery in XSLT, and
   the point of a *module* is to let one grammar import another's rules
   (§4.5). PEG is closed under composition. The union of two conflict-free
   LALR grammars is routinely conflicted, and the script author would be the
   one reading shift/reduce reports.
4. **Diagnostics for grammar authors.** These grammars are written inside
   Lambda scripts by people who are not parser-generator specialists. LR
   conflict messages are the worst in the field. A PEG never has conflicts;
   its failure mode is silent prefix capture (`"<" | "<="`) and unreachable
   alternatives, which a static lint catches (§6).
5. **Implementation size in C+** (AGENTS.md rules 3, 13). A PEG interpreter
   plus a lexer on the existing string-pattern engine is roughly 1.5–2k
   lines. An LALR(1) generator — item sets, closure, lookahead propagation,
   precedence-based conflict resolution, table compaction, error recovery —
   is 4–6k lines and much harder to get right, for tables that must be
   built at script load time rather than at build time.
6. **It is the parsing discipline the project already has.** Lambda's own
   production parser is hybrid recursive-descent + Pratt
   ([Lambda_Grammar_Parser.md](Lambda_Grammar_Parser.md)); the string-pattern
   engine (SP24) already provides the lexical layer. The module reuses
   known techniques rather than introducing a second one.

What is given up, knowingly:

- **No ambiguity detection by construction.** An LR generator proves a
  grammar unambiguous; a PEG resolves every ambiguity by order, silently.
  The lint (§6) recovers the common cases; it is not a proof.
- **Exponential backtracking is possible** on adversarial grammars. Rule-level
  memoization (`memo`, §4.6) bounds it where it matters; it is opt-in so the
  common case stays allocation-free.
- **Left recursion needs special handling** (§5). This is the price of a
  top-down engine and is paid once, in the engine.

## 4. The grammar declaration

### 4.1 Form

```
grammar <name> {
    <token>  := <string pattern>          // lexical rule (S11.1.2v4 island body)
    <rule>   =  <parsing expression>      // syntax rule
}
```

- **GM5v2 — `grammar` is a declaration keyword, and a grammar is a type.**
  The word begins a construct, so under S16.10.1v2 it joins the
  barred-as-binding list (Appendix K of
  [Lambda_Design_Syntax.md](Lambda_Design_Syntax.md) must be regenerated).
  `pub grammar` exports it like `pub type` (S16.9.1). The declared name is
  bound to a `Type` of a new kind, built once at compile time and owned by
  the module beside every other declared type; `type(calc)` is `type`
  (S2.1.2). Each rule is a type as well — `calc.number` is the string
  pattern it was declared as, `calc.expr` is a rule type carrying its
  grammar — so a rule can be named directly where a type is expected (see
  §4.4). Inside the block, rule names are data names (S16.10.2): a rule may
  be called `type`, `for`, or `element` without colliding with Lambda
  keywords, which the target specs need (XQuery has `TypeDeclaration`, jq
  has `Term`). **Migration:**
  one script in the corpus binds the word today
  (`test/lambda/graph/graphviz/formatter.ls:42`, `let grammar = …`); it is
  renamed in the same change that bars the word, exactly as the `int64`
  sweep was done (Appendix K).
- **`:=` lexical rules.** The right-hand side is exactly a string-pattern
  island body without the `\(`…`)` delimiter — the same engine, syntax and
  domain rules as S11.1.2v4, either domain (GM4.2). A token rule may reference
  another token rule declared **before** it (the island rule: a pattern names
  only what is defined before it). Lexical rules are **leaf matchers**: they
  never produce child nodes and they run with no `space` skipping inside
  them. `:=` never reaches the Lambda lexer: the body is handed to the
  loader as one span (Appendix A), and S16.8 has no prior use of it.
- **`=` syntax rules.** The right-hand side is a parsing expression:

  | Form | Meaning |
  |---|---|
  | `a, b, c` | sequence (ISO-EBNF comma; `space` runs between items, GM3) |
  | `a \| b` | ordered choice — first match wins (GM2) |
  | `a?` `a*` `a+` `a{n}` `a{n+}` `a{n,m}` | greedy, possessive repetition (same counting syntax as S16.8.6v3) |
  | `"lit"` | literal token — the same lexical form as a one-fragment `:=` body, so `"\s"` and friends mean what S11.1.2v4 says; the longest-literal rule of §4.2 applies (GM4.1) |
  | `'name'` | a name (GM4.3): a reference to the rule `name`, or a label (`'data-x': e`) — never a literal token |
  | `tok` | reference to a `:=` rule — matches one token |
  | `rule` | reference to a `=` rule — produces a child element |
  | `( … )` | grouping |
  | `&a` / `!a` | positive / negative lookahead (zero width) |
  | `~tok` / `~"lit"` | immediate (GM13): the token starts where the previous one ended, with no `space` skipped first |
  | `prec(n, …)` `prec.left(n, …)` `prec.right(n, …)` `prec.none(n, …)` | precedence-annotated alternative (§5); `prec.none` is bare `prec` without the non-associative lint warning (GM6.1) |
  | `import g` | a statement of the body, not an expression: compose grammar `g` (§4.5) |
  | `name: a` | field label on the child (becomes an attribute, §7) |
  | `_a` or `hidden(a)` | inline: the rule's children splice into the parent, no element of its own |

  `,` is the sequence operator rather than juxtaposition because a grammar
  body is a pair-list regime (S16.9.3): every item boundary is explicit, so a
  missing `|` or a stray newline cannot silently change the grammar
  (S16.1.1: line breaks carry no meaning, which holds here too).
- **GM3 — `space`.** The rule named `space` is a lexical rule the engine runs
  **between** sequence items and before/after a rule body, never inside a
  `:=` rule and never inside a `space`-less region (§4.3). A grammar with no
  `space` rule does no skipping: whitespace is then significant, which is
  what RELAX NG's and XML's content rules want in places. The name was
  chosen over `skip` because it names the *thing*, not the action, and
  because the same rule is what a formatter reinserts.

### 4.2 Parser-driven lexing

There is no separate token stream. At any position the engine matches the
literal or `:=` rule that the current parsing expression asks for, against
the raw text. Consequences:

- **Lexer modes are free.** Inside an XQuery direct constructor the rule for
  element content asks for `"<"`, `"{"`, and character data; outside it asks
  for operators and names. The same `<` text is tokenized differently because
  a different rule is active — no `%x` states, no mode syntax.
- **Keywords are not globally reserved.** A rule that lists `"for"` before
  `ident` sees the keyword; a rule that lists only `ident` accepts `for` as a
  name. XQuery and jq both require this.
- **Longest-literal order is enforced, not hoped for.** Within one choice,
  literal alternatives that are prefixes of one another (`"<"`, `"<="`,
  `"<<"`) are reordered longest-first by the loader, and the lint reports
  what it reordered. This is the one place where ordered choice is
  overridden, because no author ever intends `"<"` to win over `"<="`, and
  the reorder is semantics-preserving for pure literals.
- **Token boundaries.** A literal made of word characters (`"for"`) matches
  only if the next character is not a word character, so `"for"` does not
  match the prefix of `format`. **So does a `:=` rule whose pattern is only
  literals made of word characters** (`for := 'for'`, `kw := 'in' | 'let'`):
  it is the keyword case, and it has no other remedy, because an island
  compiles to RE2 and has no lookahead (Rev 1 suggested `!"\w"`, which inside
  an island consumes one non-word character). Any other `:=` rule has no
  implicit boundary; its pattern is greedy, so `word := "\a"+` already ends
  at a word boundary. Word characters are the Lambda lexer's identifier
  bytes, except that `$` never makes a literal keyword-like: it is
  punctuation in the guest languages (`"$"` before a name in XPath).
- **Trade-off accepted:** every token is re-recognized on backtracking. With
  `memo` on the few rules that backtrack heavily this is negligible for the
  inputs in scope (scripts, not documents).

### 4.3 Space-sensitive regions

A rule may be declared `nospace rule = …`: within its body (and the rules it
calls, unless they are themselves `space`-rules) no `space` skipping happens.
This is how string-literal bodies, XQuery direct element content and
RELAX NG's `\` escapes are written without lexing them in one giant `:=`
rule. `space` is implicitly a `nospace` rule.

A region cannot let space back in below it, so a space-sensitive position
*inside* spaced text is written with `~` instead (GM13, 2026-10-12): only the
marked token loses the skip. XQuery's element content is the case that
decided it — in `<a>x {1} y</a>` the text after `}` must keep its leading
space, while the expression inside the braces skips space and comments:

```
DirElemConstructor = _lt, name: ~qname, DirAttribute*,
                     (_empty_close | _gt, _DirElemContent*, _lt_slash, end: ~qname, _gt)
_DirElemContent    = ~elem_text | _DirectConstructor | EnclosedExpr
```

`~` cannot help where a text piece ends at a *multi-character* delimiter and
the delimiter starts with a character the text may contain (XQuery's
string constructor ``…`{`` and ``…]``` ``): a separate text token is
matched on its own and cannot see the delimiter that follows, so those
pieces are tokens that include their delimiter (``sc_start := "``[" chars
"`{"``).

### 4.4 Entry rule and the `parse` call

`parse(text, g)` and `parse(text, g, 'rule')` (GM5.1; extending the
existing `parse(str, format)` surface, Lambda_Sys_Func.md) run the named
rule — by default **the first rule in the block** — and require it to
consume the whole input after a final `space` run; otherwise the result is an
error (§8). It returns `element^E`, following S7.10's sys-func return
contract. The third argument may instead be an options map (GM11v2):
`parse(text, g, {rule: 'expr', spans: true})`. There is no file form
(GM5.4). The lint warns when
the implicit entry is the `space` rule (§6), since a grammar that starts with
`space :=` almost never means to parse whitespace.

Because a rule is a type (GM5v2), two further forms follow without new
mechanism and are proposed for after V1: `parse(text, calc.expr)` names the
entry rule as a type instead of a symbol, and `text is calc.expr` is true
exactly when the rule parses the whole text — the same `fn_is` branch that
already serves string patterns. **GM5.3 (proposed, implemented):** both
forms work. `is` takes a type in type position, where `g.rule` is not valid
syntax (GM5.4), so a rule is bound first: `let r = g.rule` then `text is r`.

### 4.5 Composition

`grammar xquery { import xpath; … }` makes every rule of `xpath` visible;
a rule defined locally with the same name **overrides** the imported one
(PEG is closed under this: the override is a new ordered-choice body, nothing
else changes). Imported `space` is overridden the same way. This is the XPath-
in-XQuery and XPath-in-XSLT case; nothing fancier (parameterized grammars,
mixins) is proposed until a target needs it.

As implemented (P4): `g` is a grammar binding in scope — declared earlier in
the module, or a `pub grammar` brought in by `import`. Its rules are copied
into this grammar and their references resolve again by name, so a local
override also changes what the imported rules call (an XQuery that overrides
`PrimaryExpr` changes every XPath rule that reaches it). Among imports a later
one overrides an earlier one. The entry follows GM5.1 in written order: an
`import` before every local rule contributes the imported entry (or the local
rule overriding it); otherwise the first local rule is the entry. Only local
rules are linted — the imported ones were linted where they were declared.
A local `:=` island names imported `:=` rules as it names its local siblings
(2026-10-12): it sees every imported `:=` rule that no local rule overrides,
wherever the `import` is written, plus the local `:=` rules written before it;
a local override hides the imported rule of that name. Names resolve the same
way for a grammar imported from another module. Not yet: a local override does
not reach the islands *of* imported `:=` rules, which were compiled where they
were declared (XPath's `eqname` keeps XPath's `ncname` even when an importer
overrides `ncname`); `=` rules have no such limit.

### 4.6 Memoization

`memo rule = …` makes the engine cache (rule, position) → (result, end) for
that rule during one parse. Left-recursive rules are memoized implicitly
(§5 needs it). Everything else is plain backtracking, so the common grammar
allocates nothing beyond its output tree. The lint suggests `memo` for a rule
that appears as the first item of two or more alternatives of the same
choice (the pattern that makes backtracking quadratic).

## 5. Left recursion and Tree-sitter-style precedence (GM6–GM8)

USER's question: can the module support precedence rules the way
Tree-sitter writes them — direct left recursion with `prec.left(n, …)` /
`prec.right(n, …)` annotations?

**Answer: yes, for direct left recursion, with the same surface and the same
reading of the numbers; indirect left recursion is a static error in V1.**

### 5.1 Why plain PEG cannot

A top-down parser entering `expr` at position *p* that first calls `expr` at
position *p* loops forever. The textbook PEG answer is to rewrite the rule as
repetition (`expr = term, (("+"|"-"), term)*`), which is how the XPath EBNF
is written. That rewrite builds a left-leaning tree only by convention and
makes a 12-operator language 12 rules deep, and jq's bison grammar would
have to be hand-rewritten.

### 5.2 The mechanism: bounded left recursion with precedence levels

The engine implements *seed growing* (Warth, Douglass & Millstein, "Packrat
parsers can support left recursion", PEPM 2008) in the corrected,
precedence-aware form of Medeiros, Mascarenhas & Ierusalimschy ("Left
recursion in Parsing Expression Grammars", Science of Computer Programming
96, 2014). In this design's terms:

- A rule with at least one alternative whose first item is the rule itself is
  **left-recursive**. The loader marks it and memoizes it (§4.6).
- A call of a left-recursive rule carries a **level** *k* (0 from outside).
  Parsing `rule@k` at position *p*:
  1. Record `(rule, p) → fail` as the seed and mark it *growing*.
  2. Try the **non-left-recursive alternatives** in order (`primary` in §1).
     The first success is the seed. If none succeeds, the rule fails.
  3. **Grow:** try the left-recursive alternatives in order, where the leading
     self-reference returns the current seed without consuming anything. An
     alternative annotated `prec n` is admissible only if **n ≥ k**. If an
     alternative succeeds and ends further right than the seed, it becomes
     the new seed and growing restarts from the first alternative. When a
     full pass grows nothing, the seed is the result.
  4. Inside an alternative, a **trailing** self-reference is called at level
     `n + 1` for `prec.left(n, …)` and at level `n` for `prec.right(n, …)`;
     a self-reference in the **middle** of an alternative (the `b` of
     `a ? b : c`) is called at level 0. `prec(n, …)` with no associativity
     calls trailing self-references at `n + 1`.
  5. **The growth cap** (added in implementation). A growth step whose
     alternative ends in the rule itself has let its right operand take
     every operator above its level, so later steps of the same loop stay
     at or below it: at `n` after `prec.left(n, …)`, below `n` after
     `prec(n, …)` and `prec.right(n, …)`. Without it the loop itself chains
     a non-associative operator — `a < b < c` grew `a < b`, then re-admitted
     the same alternative with that as its seed. With it `a < b < c` fails
     to parse without parentheses (Tree-sitter rejects the same chain as a
     generation-time conflict; here it is a parse failure, and the lint
     warns at load time, §6), while `a < b and c < d` still parses. Steps
     without a trailing self-reference (postfix calls and indexes) set no
     cap, so `f(a)(b)[0]` chains.

Trace of `a + b * c` with the §1 grammar, level 0: seed `a`; grow: the `+`
alternative (prec 1 ≥ 0) takes seed `a`, matches `+`, and calls `expr@2` on
`b * c` — whose own seed `b` grows with the `*` alternative (prec 2 ≥ 2) to
`b * c`, and whose `+` alternative is now inadmissible (1 < 2). Result
`a + (b * c)`. Then `a * b + c`: seed `a`; the `+` alternative fails on `*`;
the `*` alternative grows the seed to `a * b` calling `expr@3` for `b`; the
restart lets the `+` alternative take `a * b` as its left operand:
`(a * b) + c`. Unary minus `prec(3, "-", expr)` is not left-recursive; its
trailing operand is called at level 4 (step 4), so `-a * b` parses as
`(-a) * b` (`*` is prec 2, inadmissible inside the operand) and `-a ^ b` as
`-(a ^ b)` (`^` is prec 4, admissible) — the usual mathematical reading.
(Rev 1 said level 3; both give these results.) All four traces are pinned by
`test/lambda/grammar/calc.ls`.

### 5.3 What matches Tree-sitter and what does not

| Tree-sitter | This module |
|---|---|
| `prec.left(n, seq($.e, op, $.e))` | `prec.left(n, e, op, e)` — same meaning: higher *n* binds tighter, left-associative |
| `prec.right(n, …)` | same |
| `prec(n, …)` | same numbers; on a binary self-recursive alternative it means non-associative (lint warns) |
| `prec.dynamic(n, …)` | **not supported** — it only exists to score GLR forks; there are no forks here |
| `conflicts: [[…]]` | **not needed** — ordered choice is the conflict resolution |
| precedence numbers compared **across different rules** to resolve LR conflicts | numbers are **per rule**: the ladder of one left-recursive rule. Between different rules, order decides |
| postfix / call / index on the same rule (`prec(10, seq($.e, '(', args, ')'))`) | supported: left-recursive alternative with no trailing self-reference |
| ternary `prec.right(0, seq($.e, '?', $.e, ':', $.e))` | supported; middle operand at level 0 |
| indirect left recursion (`expr → postfix → expr`) | **static error (GM8)** naming the cycle; write the recursive alternative on the rule that is the cycle's head |
| `word:` / `extras:` | `ident`'s word boundary (§4.2) / the `space` rule (GM3) |

**GM6** — Direct left recursion is supported through bounded left recursion
with per-rule precedence levels, with the `prec` / `prec.left` /
`prec.right` surface and the Tree-sitter reading of the numbers.

**GM7** — Precedence levels are scoped to the rule that carries them;
there is no grammar-wide precedence table. Across rules, ordered choice
decides.

**GM8** — Indirect left recursion is a load-time error. Medeiros's algorithm
can be extended to it, but Tratt ("Direct left-recursive parsing expression
grammars", 2010) showed the interaction of indirect left recursion with
right recursion is where the published algorithms went wrong, and none of
the four target grammars needs it. Revisit only with a concrete grammar
that does.

The loader detects both kinds of left recursion statically by walking the
*nullable-prefix graph*: an edge from rule A to rule B whenever B can be the
first non-nullable item of an alternative of A (`a?, b` makes both `a` and
`b` possible firsts). A self-edge is direct left recursion; a longer cycle is
GM8's error. Doing this at load time means an author gets a message naming
the cycle, never a stack overflow mid-parse.

### 5.4 Relation to Pratt parsing

The engine is not a Pratt parser, but for operator rules it computes what
one computes. Pratt's top-down operator precedence and precedence climbing
are the same algorithm: a loop that parses a prefix form, then keeps
extending it with infix and postfix operators whose binding power is at
least the caller's minimum. GM1's "precedence climbing" is realized by §5.2's
bounded left recursion with precedence levels, which has the same shape
(`parse_left_recursive` in `grammar_parse.cpp`, checked 2026-10-12):

| Pratt | This engine |
|---|---|
| `parse_expr(min_bp)` | a left-recursive rule called at level *k* |
| *nud*: atoms and prefix operators, chosen by the current token | the non-left-recursive alternatives, the first that matches being the seed (`Term`, or `prec.left(9, op: minus, operand: Exp)` in jq) |
| *led*: infix and postfix operators, allowed when their left binding power ≥ `min_bp` | the left-recursive alternatives, allowed when their `prec` ≥ *k*; the leading self-reference takes the seed |
| right operand parsed at `bp + 1` (left-associative) or `bp` (right-associative) | trailing self-reference called at `n + 1` (`prec.left`, bare `prec`, `prec.none`) or `n` (`prec.right`) |
| postfix led: no right operand | a left-recursive alternative with no trailing self-reference (`prec(18, base: Expr, args: ArgumentList)`); it sets no cap, so `f(a)(b)[0]` chains |
| bracketed sub-expression parsed at bp 0 | a middle self-reference called at level 0 (a ternary's middle operand; jq's `try A catch B`) |
| non-associative: equal binding powers plus an explicit check | the growth cap: after a step at *n* that ends in the rule, later steps of the loop stay at *n* (`prec.left`) or below it |
| the loop repeats while the next token's binding power is high enough | the growth loop restarts from the first left-recursive alternative after every step that advances |

So `a + b * c`, `-a * b` against `-a ^ b`, right-associative chains,
postfix chains and the refusal of `a < b < c` all come out as a Pratt parser
gives them; `calc.ls`, `left_recursion.ls`, `xpath.ls` and `jq.ls` pin these.

Where the two differ:

- **No operator table.** A Pratt parser keeps binding powers in a table, or
  in code, keyed by token, apart from the productions. Here a level is
  declared on its alternative (GM6), scoped to its rule (GM7), and an
  operator is any parsing expression rather than one token: `left: Expr, op:
  instance, _of, type: SequenceType` and `op: arrow, function: …, args: …`
  are ordinary alternatives. This is what let XPath's productions and jq's
  bison precedence lines be written as grammar text without a second table
  (§10, M3 and M4).
- **Dispatch by trial, not lookup.** Pratt looks at the next token and finds
  its led in O(1). A growth step here tries the admissible alternatives in
  written order until one matches, and every step restarts from the first.
  For an operator alternative (seed, then an operator token) a failed
  attempt costs one token match, so a step costs up to one match per
  alternative: jq's `Exp` has ten. The cost is a constant factor per
  operator, not a change in complexity.
- **Memoized.** A left-recursive rule is cached by (rule, level, position),
  and the cache holds the growing seed while the loop runs. A retry at the
  same place — the next alternative of an enclosing choice, or jq's
  `Term as …` seed failing over to the plain `Term` seed — reuses the operand
  instead of re-parsing it. A Pratt parser needs no cache because it never
  backtracks.
- **Ordered seeds.** The prefix forms are an ordered choice like every other
  choice (GM2): the first seed that matches wins, even where a later one
  would match more. A Pratt nud is picked by token, so it never has to order
  alternatives that start alike.
- **Interpreted, not written.** Lambda's own C parser is hand-written
  recursive descent plus Pratt (§3, reason 6), so there the binding-power
  table is code; a guest grammar is data that the engine interprets.

A Pratt-style dispatch could be added without touching the grammar surface:
precompute, for each left-recursive alternative, the literals or tokens that
can follow its leading self-reference, and skip an alternative whose
operator cannot start at the current position. It is not implemented; as
with packrat-by-default (GM-O5), it waits for a measured need.

## 6. The lint (GM9)

Runs once when the grammar is loaded; warnings go through the ordinary
compiler warning channel so `lambda.exe` prints them with the script's other
diagnostics. Each warning names the rule, the alternative index and the
reason. **GM9** — every check below is advisory except GM8 and the
missing-rule / domain errors, which are errors.

| Check | Why it exists |
|---|---|
| **Prefix capture:** alternative *j* can only match text that alternative *i < j* already matches (literal prefix; or FIRST-set containment where computable) | the classic PEG trap: `"<" \| "<="` never reaches `"<="`. Pure literal cases are auto-reordered (§4.2) and reported; non-literal cases are warned, not changed |
| **Unreachable alternative:** alternative *j* has a FIRST set disjoint from every possible continuation, or an earlier alternative always succeeds (`a* \| b`) | an alternative nobody can hit is almost always a typo |
| **Nullable repetition:** `a*` or `a+` where `a` can match empty | infinite loop in any PEG engine; the engine also guards it at run time (a repetition that does not advance stops), but the author should know |
| **Left recursion without `prec`:** a left-recursive alternative with no annotation | legal (treated as `prec(0)`), but the ladder is probably incomplete |
| **Non-associative chain:** `prec(n, e, op, e)` with no `.left`/`.right` | chaining `a op b op c` will fail to parse (§5.2 step 4) |
| **Missing `space`:** a grammar with `=` rules and literal tokens but no `space` rule | whitespace is significant; usually unintended |
| **Entry is `space`:** the first rule in the block (the implicit entry, GM5.1) is the `space` rule | `parse(text, g)` would parse whitespace only; name the entry or move the rule |
| **`memo` suggestion:** the same rule begins two alternatives of one choice | quadratic backtracking |
| **Unused rule / undefined rule** | the latter is an error |
| **Symbol in token position:** `'x'` where a `=` rule expects a token | error, with the hint `write "x"` — a symbol is never a token (GM4.1) |

The lint runs on the loaded, *composed* grammar (§4.5) so an override that
shadows an import's alternative is checked in context.

## 7. The parse tree (GM4, GM10–GM11)

**GM10** — Each `=` rule that succeeds yields one element whose tag is the
rule name; the children, in order, are the elements of the sub-rules it
called and the text matched by its tokens: a **string** for a literal or a
`"..."` rule, a **symbol** for a `'...'` rule (GM4.2). A hidden token rule
(`_comma := ","`) emits nothing. **Adjacent strings merge** — element content
normalizes them into one text node (S2.6.4), so `"for", name: id, "in"`
yields `"forin"` once the label has moved `id` into an attribute, and
`"[", "]"` yields `"[]"`. A grammar keeps tokens apart by writing its fixed
vocabulary as symbol rules (`for := 'for'` emits `'for'`, which never
merges), or by labelling them. Labeled
items (`op: "+"`) become attributes, so a transpiler can read `node.op`
instead of counting children. `_rule` / `hidden(rule)` splices children into
the parent (Tree-sitter's `_` convention). Lookahead items contribute
nothing.

```lambda
// grammar: expr = prec.left(1, left: expr, op: "+", right: expr) | primary
<expr left: <expr "1">, op: "+", right: <expr "2">>
```

**GM11v2** — Source positions are attributes, on request: `parse(text, g,
{spans: true})` adds `start` and `end` byte offsets to every element. An
element starts at its first token, not at whitespace skipped before it. They
are not on by default because the tree is meant to be read and transformed
with ordinary element operations (`for`, `match`, member access), and a
transpiler that does not need positions should not pay for them in memory or
in the shape of every node.

Why a tree of elements and not a callback sink (the `LambdaParseSink` shape
the C parser uses): the whole module is user-facing data flow. A parse tree
that is a value can be printed, diffed against a golden, validated against a
schema (D3.2.2, [Lambda_Validator.md](../doc/Lambda_Validator.md)), cached,
and transformed by any Lambda function. The transpiler step then needs no new mechanism at all — it is a
function from one element tree (the guest's) to another (`lm.`), ending in
`compile` (S15.3). This is what D1.1's "grammar + AST builder" means when
both halves are written in Lambda.

Script-facing `parse` builds precisely rooted GC-owned values, then releases
its private parse Input and scratch arena (GM14, D5.3.3). The native engine also
supports a caller-owned Input/MarkBuilder result (D4.2.6). Neither mode retains
scratch nodes in the returned tree. The grammar itself is a type (GM5v2):
compiled rules and token matchers live in the module's type list with every
other declared type, immutable, shared by every execution of the module, and
released with it (D4.1.1v2, D7.2.1). A script never holds a run-time object
for a grammar — only the `Type*`, exactly as for `type T = …`.

### 7.1 Byte grammar input (GM14)

ASCII syntax can recognize byte-oriented documents without decoding their
payload: unified patches and NUL-delimited Git records are examples. Literal
text matches its encoded bytes; character sets/ranges are restricted to byte
ordinals 0–255 and complements use that alphabet. Byte matchers lower the
original pattern AST with a byte limit and use RE2's Latin-1 mode. Unicode
pattern compilation remains unchanged (S11.1.2v4). Matchers are cached per rule
for a parse and released with its scratch state.

Ordinary tokens become immutable binary; symbols retain GM4.2's symbol domain.
Spans are half-open byte offsets. Inputs larger than `UINT32_MAX` are rejected
before narrowing. Failure descriptions escape control bytes, so an expected NUL
does not truncate the message. Numeric syntax offsets are not a separate public
error field; the message carries its line/column, while packages can attach
byte spans to their semantic diagnostics. See the diff package's
[implementation record](impl/Lambda_Impl_Diff.md) for the motivating fixtures.

## 8. Errors (GM12)

A failed parse returns an error (S7.4's error channel) carrying the
**furthest position reached** and the **set of tokens that were tried
there** — the standard PEG heuristic, and in practice the message authors
want: `expected ")" or "+" at 1:7`. It is raised the way every input parser
raises one (GM5.2): the same parse-error code, the message as the input's
parse-error message, the same `^` discharge (S7.6). Internally every failure
is cheap (a flag and the position); only the furthest one keeps its
expected-set. A token rule that is a single literal (`_lparen := "("`,
`or := 'or'`) is shown as that literal, not by its rule name; up to 32
expected tokens are listed. There is no error recovery in V1: a guest script
with a syntax error does not run, so a partial tree has no consumer.

## 9. Open rulings

| ID | Question | Proposed default |
|---|---|---|
| **GM-O1** | Does `grammar` go into the formal spec as a new S16.9 declaration ruling (syntax) plus a D-ruling for the engine (likely a new D8.3), or only as a D-ruling with the syntax folded into S15 metaprogramming? | S16.9 for the surface (it is a declaration like `type`), D8 for the engine; S15 gains one line pointing at `parse(text, grammar)` as the second producer of trees that reach `compile` |
| **GM-O2** | Explicit level references in the grammar surface (`expr@3`) for authors who want to call a rule above a given precedence | **not in V1**; the ladder is private to the rule. Add if a target spec needs it |
| **GM-O3** | *Closed by GM4.3 (USER, 2026-10-11): `'x'` in a `=` rule is a rule name.* | closed |
| **GM-O8** | *Closed by GM5.4: `text is g.rule` is not valid syntax; bind the rule first.* | closed |
| **GM-O9** | *Closed by GM5.4: `input(target, g)` is not a supported usage.* | closed |
| **GM-O10** | `prec.none(n, …)` (GM6.1, implemented with P5): an explicitly non-associative alternative, identical to bare `prec` but without the lint warning, so a grammar that means it (XPath comparisons and `to`) compiles clean. | keep: a shipped grammar must not print warnings into every importing script |
| **GM-O4** | *Ruled by GM5.1 (USER, 2026-10-11).* `parse` takes the grammar as a type value and the entry rule as a symbol (`parse(text, g, 'rule')`); no grammar-by-name string form, which keeps S15.3's "strings are never code" clean and makes `pub grammar` the one sharing mechanism | closed |
| **GM-O5** | Packrat by default (memoize every rule) vs. opt-in `memo` | opt-in, as ruled in §4.6; measure on the XQuery grammar before revisiting |
| **GM-O6** | Semantic predicates (`&{ expr }`) in rules | **no**; context-sensitivity that parser-driven lexing and lookahead cannot express goes into the transpiler, which keeps the grammar a pure description |
| **GM-O7** | Does the module belong to `lambda-cli` (D7.1.6)? | yes — it is a core-language feature with no Radiant/JS/Jube dependency; the `cli` profile lists `lambda/runtime/grammar` since 2026-10-11 (that profile not yet rebuilt) |

## 10. Milestones (sketch; detail belongs in `vibe/impl/`)

1. **M1 engine + lint** on the §1 calculator and a JSON grammar: declaration
   parsing in the C parser, loader, nullable-prefix analysis, lint, ordered
   choice, repetition, lookahead, `space`, literal reordering, error report.
2. **M2 left recursion**: GM6–GM8 with a test that pins every row of the
   §5.3 table and the two §5.2 traces as goldens.
3. **M3 XPath 3.1** *(done 2026-10-11, P5)*: `lambda.grammar.xpath`, written
   Tree-sitter style per the P5 ruling; parsing only. Rev 1 planned an EBNF
   transcription plus a transpiler to `lm.`; both were ruled out of scope.
4. **M4 jq** *(done 2026-10-12)*: `lambda.grammar.jq`, ported from jq
   1.7.1's bison grammar — the acceptance test for §5: bison's `%right` /
   `%left` / `%nonassoc` / `%precedence` lines became the levels of one
   left-recursive `Exp` without reordering. String interpolation uses `~`.
5. **M5 XQuery and RELAX NG compact** *(done 2026-10-12)*:
   `lambda.grammar.xquery` composes over `lambda.grammar.xpath` (§4.5),
   overriding `_ExprSingle`, `_Primary` and three bindings; its direct
   constructors are what decided GM13. `lambda.grammar.rnc` needed neither
   `nospace` nor `~` beyond literal segments: the spec's no-mixing rule for
   `,` `&` `|` is three repetitions, not a precedence ladder. All three
   parse only, as P5 does.

Each milestone adds `test/lambda/grammar_*.ls` fixtures with goldens
(AGENTS.md rule 8) and the grammar lint has its own gtest.

## Appendix A — Implementation seams (brief)

As implemented 2026-10-11; the plan's §2 and phase notes carry the detail.

- **Declaration syntax:** `lambda_lexer.c` gains the `grammar` keyword
  (barred by `lambda_lexer_word_bars_binding`'s default arm) and
  `lambda_lexer_skip_block_body`; `parse_grammar_declaration` in
  `lambda_parser.c` hands the body over as one span, the way type slots
  reach the type-pattern parser, so no `:=` token exists in the main lexer.
  `build_ast.cpp` wraps it in the same `TYPE_STAM` + declarator as
  `type Name = T` (form `LSF_GRAMMAR`; D8.2.2 prefers a form field to a new
  node kind), so both tiers treat it as a compile-time type binding with no
  change. `grammar.js` reads the body as one opaque token (D8.1.2v3).
- **Island reuse:** `parse_pattern_island_syntax` (`parse_type_pattern.cpp`)
  parses a `:=` body; `resolve_type_pattern` compiles it, with earlier
  token rules in a private scope as named pattern definitions.
- **Types:** `TypeGrammar` and `TypeGrammarRule` extend `TypeType`
  (`lambda-data.hpp`, kinds `TYPE_KIND_GRAMMAR` / `_RULE`), so generic type
  readers stay in bounds; printing, `is`, member access and `parse` have
  grammar arms.
- **Engine:** `lambda/runtime/grammar/` — `grammar_load.cpp` (body parser,
  rule table, nullable-prefix graph, left-recursion classification, lint)
  and `grammar_parse.cpp` (interpreter, seed growing with the cap, scratch
  tree in an arena, iterative conversion to elements on `MarkBuilder`).
  Token matching uses `pattern_match_prefix_at` (RE2 `ANCHOR_START` at an
  offset, in Lambda's `re2_wrapper.cpp`; `lib/re2` is untouched).
- **Sys funcs:** `parse(text, g)`, `parse(text, g.rule)` and
  `parse(text, g, 'rule')` (`fn_parse2` grammar arm, new `fn_parse3`);
  return `element^E` (S7.10). `input(target, g)` is not implemented yet.
- **Where the lint reports:** `record_semantic_warning_span`, which joins the
  runner's printed warning list (`err_print_warning`) and never fails the
  compile.
