# Lambda Semantics DSL — Proposal

**Status:** draft proposal
**Scope:** a sub-language of Lambda for formally modeling the semantics of programming languages — Lambda itself first, then the engine's other supported languages (JavaScript/TypeScript, Python, Bash, Ruby)
**End goal:** an executable, independent semantic model that can *verify* the Lambda test fixtures under `./test/lambda`, breaking the circularity of goldens captured from the very runtime they test

---

## 1. Motivation

### 1.1 The oracle problem in the current test suite

The ~200 fixture pairs under `test/lambda/` (`*.ls` script + `*.txt` golden) are the
backbone of `make test-lambda-baseline`. But the `.txt` goldens are **captured from the
Lambda runtime's own output**. If the implementation has a semantic bug, the golden
captures the bug, the test passes, and the bug becomes load-bearing. The suite detects
*regressions*, not *incorrectness*. The per-test `// expect:` comments in the `.ls`
files are hand-written and closer to a specification, but they are informal, unchecked
prose.

What is missing is an **independent oracle**: a definition of what Lambda programs
*should* evaluate to, derived from stated rules rather than from the implementation.

### 1.2 One model, many languages

The Lambda engine already executes, or plans to execute, multiple guest languages:
JavaScript/TypeScript in core, and Python, Bash, Ruby in the Jube build
(`doc/Lambda_Jube_Runtime.md`). Each guest runtime faces the same oracle problem —
LambdaJS's Node-compat baseline, for example, is measured against Node's observed
behavior, with no formal account of *why* an output is right.

A semantics DSL amortizes across all of them: one rule language, one matching engine,
one verification harness — five language models.

### 1.3 Prior art we draw from

- **K Framework** (Roșu et al.) — configurations as nested cells, semantics as rewrite
  rules over configurations, `strict` annotations that auto-generate evaluation-order
  rules. Proven at full scale: KJS passed the ECMAScript 5.1 conformance core
  (PLDI 2015), and complete K semantics exist for Python 3.3 (Guth 2013) and
  PHP (Filaretti & Maffeis, ECOOP 2014).
- **Ott** (Sewell et al.) — grammar declarations with *binding specifications*,
  inductive judgments as inference rules, generation of LaTeX and proof-assistant
  definitions.
- **PLT Redex** — lightweight executable semantics embedded in a host language, with
  randomized testing against real implementations.
- **Smoosh** (Greenberg & Blatt) — the executable POSIX shell semantics; the reference
  point for how gnarly (and how tractable) modeling Bash actually is.
- **In-house: the Lambda Redex model** (`vibe/Lambda_Semantics.md`) — an existing
  PLT Redex big-step model of Lambda's functional core, procedural extension, and
  object types (~8k lines of Racket), with an `--emit-sexpr` AST bridge and a
  `make test-redex-baseline` harness already at 87/194 fixture agreement. It validates
  this proposal's end goal empirically and is discussed in §13.4.

The design below is closest in spirit to Redex ("embedded, executable, test-focused")
with K's configuration/strictness machinery and Ott's binder specs grafted on.

---

## 2. Why Lambda is already most of the way there

A semantics framework needs: (1) a term language, (2) an abstract-grammar language,
(3) rules, (4) an execution engine. Lambda's existing data model covers (1), (2), and
much of the machinery for (4):

| Semantics concept | K / Ott construct | Already in Lambda |
|---|---|---|
| Terms / AST nodes | KItems, Ott terms | **Elements**: `<add <lit 1> <lit 2>>` — code-as-data, no quotation mechanism needed |
| Labels, names, metavariables | KLabels, metavars | **Symbols** — interned, O(1) equality |
| Abstract grammar (BNF) | `syntax Exp ::= ...` | **Recursive union types + occurrence modifiers**; the validator schema DSL *is* an abstract-grammar language |
| Terminals / lexemes | token sorts | **String patterns**: `string Ident = \a \w*` |
| Configurations (cells) | `<k> <env> <store>` | Elements with attributes + children: `<cfg <k; ...> <env; {...}>>` |
| Environments, stores | Map sort | **Maps**, with spread merge `{*:base, x: v}` |
| Rule failure / stuckness | rule does not apply | **`T^E` error returns**, compile-enforced; errors are falsy, so `r1(t) or r2(t)` is deterministic rule choice for free |
| Sort / well-formedness checking | kompile sort checks | The **validator** checks terms against grammar types |
| Rule registry + recursive dispatch | the rewrite engine | Precedent: the **`view`/`edit`** template system (`grammar.js` `view_stam`) already implements pattern → template rules with a registry and recursive `apply;` |

The `view`/`edit` construct matters beyond analogy: it is the implementation blueprint.
`syntax` / `rule` / `judgment` declarations below follow the same pipeline —
grammar.js declaration forms → `build_ast.cpp` lowering → data structures + generated
functions — with a structural matcher runtime grown out of the validator's existing
type-vs-value walker.

What Lambda **lacks**, and what this proposal adds, splits into two tiers:
core-language gaps (§4) and the DSL declaration forms (§5), plus the
real-scripting-language machinery (§6).

---

## 3. Design overview

A semantics model is an ordinary Lambda module (`.ls` file) using new top-level
declaration kinds. A toy lambda-calculus, end to end:

```lambda
// ---- abstract grammar (reuses the type DSL, adds binder metadata) ----
syntax Expr = <lit value: int>
            | <var name: symbol>
            | <add; Expr, Expr> strict
            | <lam param: symbol; Expr> binds param in 1
            | <app; Expr, Expr> strict

syntax Value = <lit value: int> | <lam param: symbol; Expr>

// ---- small-step rules: pattern ~> template, with side conditions ----
rule add-vals:  <add <lit a> <lit b>>  ~>  <lit a + b>

rule beta:      <app <lam x; body> (v: Value)>  ~>  subst(body, x, v)

// ---- Ott-style moded judgment: typing ----
judgment types(env: map, e: Expr) Type^

rule t-lit:  |- types(g, <lit _>) = int

rule t-var:  x in g
             |- types(g, <var x>) = g[x]

rule t-add:  types(g, e1) == int, types(g, e2) == int
             |- types(g, <add e1 e2>) = int

// ---- strategies are a plain library over first-class rules ----
let step  = add_vals or beta            // errors are falsy: deterministic choice
let run   = fixpoint(innermost(step))
let final = run(<app <lam 'x; <add <var 'x> <lit 1>>> <lit 41>>)
```

Compilation model — no engine magic:

- a `rule` compiles to a function `fn(Term) Term ^ NoMatchErr`;
- a moded `judgment` compiles to a recursive function returning `T ^ StuckErr`;
- `strict` annotations expand into generated heating/cooling rules (§6.1);
- strategy combinators are ordinary higher-order functions;
- the existing compile-time error enforcement guarantees stuckness is handled.

---

## 4. Tier 1 — core-language additions

These are general-purpose language features, useful far beyond the DSL, and the DSL
cannot exist without them. Ordered by importance.

### 4.1 Destructuring patterns in `match`

The single biggest gap. Today `match` arms are type/literal/constraint patterns only;
the arm body sees the whole scrutinee as `~` and must extract sub-values manually
(`doc/Lambda_Expr_Stam.md` § Match Expressions). Semantics rules are 90%
"match a shape, bind the pieces", so this must become:

```lambda
match t {
    case <add (a: Expr) (b: Expr)>: eval(a) + eval(b)
    case <lit v>:                   v
    case [x, rest...]:              ...
    case {name: n, *others}:        ...
}
```

Design principles:

- **One pattern language.** Extend the existing type-pattern grammar: an identifier in
  child/field position becomes a *binder* (optionally annotated `(x: T)`), instead of
  inventing a parallel pattern syntax. `is`, queries (`?`, `[T]`), `match`, and rule
  left-hand sides all share it.
- **Linear first.** Repeated variables (non-linear patterns implying equality) are
  rejected in v1; they can be added later as sugar for a `when` equality condition.
- Literal children still match literally; `_` is the wildcard binder.

### 4.2 Rest / frame patterns

K's "ellipsis frames," needed both for sequence matching and for configuration rules
that mention only the cells they touch:

- **Sequence rest:** `<k pat rest...>` — match the first child, bind the remaining
  children as a list. Also at array level: `[x, rest...]`.
- **Map rest:** `{x: v, *rest}` — match one entry, bind the map minus that entry.
  **Restriction:** the matched key must be *determined* — a literal or an
  already-bound variable — never an unconstrained fresh variable. This deliberately
  avoids general associative-commutative matching (the expensive part of K); §6.3
  argues the restriction is sufficient for real language models.

### 4.3 Fresh symbol generation

Capture-avoiding substitution and heap allocation both need fresh names.
`fresh('x)` returns a symbol guaranteed distinct from any symbol currently interned
with that stem (`'x_1`, `'x_2`, ...). To preserve `fn` purity it follows the
`math.random(seed)` precedent — deterministic given a threaded counter — or is
provided implicitly inside `rule` bodies via a `fresh y` clause (§5.3), where the
engine threads the counter.

### 4.4 Source-as-data ingestion

To model real programs, source text must become element terms. This is an *input
parser* addition, not a language construct — and Lambda already embeds tree-sitter:

```lambda
let ast  = input("prog.ls",  'lambda')                       // Lambda source → element tree
let jast = input("prog.js",  {'type': 'ast', 'lang': 'javascript'})
let past = input("prog.py",  {'type': 'ast', 'lang': 'python'})
```

The mapping is mechanical: CST node type → element tag (symbol), named fields →
attributes, children → children, tokens → strings/symbols/numbers. The Jube build
already carries tree-sitter grammars for Python, Bash, and Ruby; core carries Lambda
and JS/TS. **Every language the engine can parse becomes a language the DSL can
model.** A thin per-language normalization pass (written in Lambda, as rules) lowers
the raw CST to the abstract grammar of §5.1.

---

## 5. Tier 2 — DSL declaration forms

New top-level statements, implemented exactly like `view_stam`: grammar.js rules →
`build_ast.cpp` → data + generated functions.

### 5.1 `syntax` — abstract grammar with binders and strictness

`syntax` is `type` plus two metadata annotations:

```lambda
syntax Expr = <lit value: int>
            | <var name: symbol>
            | <add; Expr, Expr> strict                 // evaluate all children, in order
            | <cond; Expr, Expr, Expr> strict(1)       // only the guard is strict
            | <and; Expr, Expr> strict(1)              // short-circuit: left only
            | <lam param: symbol; Expr> binds param in 1
            | <letex name: symbol; Expr, Expr> binds name in 2, strict(1)
```

- **`binds x in i, j, ...`** — Ott-style binding spec: attribute `x` binds in the
  listed child positions. From these specs the engine *derives* generic
  `free_vars(t)`, `subst(t, x, s)` (capture-avoiding, using §4.3), and
  `alpha_eq(t1, t2)` for every syntax sort — users never hand-write them per language.
- **`strict` / `strict(i, ...)`** — evaluation-order spec; expansion in §6.1.
- Terminals reuse **string patterns** (`string Ident = \a \w*`); sorts reuse union,
  occurrence (`Stmt*`), and optionality — the whole validator type language carries
  over, and the validator itself checks term well-formedness against `syntax` sorts.

### 5.2 `config` — the configuration cell tree

```lambda
config Cfg = <cfg
    <k; KItem*>                 // computation: a sequence of KItems (§6.2)
    <env; {symbol: Addr}>       // lexical environment: name → address
    <store; {Addr: Value}>      // heap: address → value (§6.3)
    <out; string*>              // output channel (print effects)
>
```

`config` is just a `syntax` declaration with a distinguished role: `load(prog)`
produces the initial configuration; rules that omit cells are implicitly framed
(§6.2); a `final` predicate (user-declared) classifies terminal configurations.

### 5.3 `rule` — rewrite rules

```lambda
rule name?:  LHS-pattern  ~>  RHS-template
             when  <side-condition expr over bound vars>
             fresh y1, y2                // engine-supplied fresh symbols
```

- `~>` is the rewrite arrow (`=>` is taken by fn-expressions).
- LHS is a §4.1 pattern (with §4.2 frames); RHS is an ordinary element template —
  Lambda element literals already interpolate expressions and spread, so no
  quasiquotation machinery is needed.
- `when` sees all LHS bindings; it is the general guard (the existing `that` constraint
  syntax remains usable inside patterns for per-binder conditions).
- Each rule reifies as `fn(Term) Term ^ NoMatchErr` and is registered under its
  module's rule set; anonymous rules get generated names.

### 5.4 `judgment` — moded inference rules

For static semantics (typing, scoping, elaboration) and big-step evaluation:

```lambda
judgment types(env: TypeEnv, e: Expr) Type^     // inputs left of ')', output after

rule t-app:
    types(g, f) == <arrow t1 t2>, types(g, a) == t1
    |- types(g, <app f a>) = t2
```

- **Moded only, in v1.** Inputs and one output; premises are evaluated left-to-right;
  a premise that fails (error) makes the rule fail; the first rule whose conclusion
  pattern matches and whose premises hold produces the output. Compilation target: a
  recursive `fn` returning `Type ^ StuckErr` — which the existing `^` propagation and
  `let v^err` destructuring handle idiomatically.
- Full relational/backtracking (Prolog-mode) judgments are **deferred**: moded
  judgments cover typing, big-step eval, and elaboration — the working set for this
  proposal's goals.
- Derivation-tree recording (for proof output / debugging why a program is stuck) is a
  runtime flag: when on, each judgment call returns its derivation as an element tree,
  renderable via `format()`.

### 5.5 Strategies — a library, not syntax

Because rules are first-class functions with error-typed failure, the strategy layer
is ordinary Lambda code, shipped as `import semantics.strategy`:

```lambda
pub fn choice(r1, r2)  => (t) => r1(t) or r2(t)        // errors are falsy
pub fn seq(r1, r2)     => (t) => r2(r1(t)^)^
pub fn try(r)          => (t) => r(t) or t
pub fn fixpoint(r)     => ...                            // apply until NoMatch
pub fn innermost(r)    => ...                            // leftmost-innermost congruence walk
pub fn outermost(r)    => ...
```

The congruence walkers (`innermost`, `all_children`, ...) are generic tree traversals
over elements — implementable today, and worth adding to the standard library
regardless.

### 5.6 `builtin` — the trusted-primitive boundary

No one models `Array.prototype.sort` or Python's C stdlib as rules. A model declares
its primitive boundary explicitly:

```lambda
builtin js_string_concat(a: string, b: string) string = (a, b) => a ++ b
builtin math_sqrt(x: float) float = math.sqrt
```

Rules invoke builtins like judgments. The declaration form (rather than plain `fn`)
matters for **auditability**: `lambda semantics --list-builtins model.ls` enumerates
exactly what a model trusts rather than derives — the "trusted computing base" of each
language model.

---

## 6. Tier 3 — machinery for real scripting languages

Toy calculi need §4–§5. Lambda, JS, Python, Bash, and Ruby additionally need the
following three mechanisms — promoted here to required scope, since real languages are
the stated goal.

### 6.1 Strictness expansion (heating / cooling)

Real-language semantics are environment/store-based small-step, where evaluating
`<add e1 e2>` requires first reducing `e1` inside its context. Without automation this
means two hand-written rules per operator per argument position — hundreds of pure
boilerplate rules at JS scale. The `strict` annotation generates them.

For `<add; Expr, Expr> strict`, the engine emits (conceptually):

```lambda
// heat: schedule child 1, leave a hole
rule <k <add e1 e2> rest...> ~> <k e1 <add <hole> e2> rest...>
     when not (e1 is Value)
// cool: plug the result back
rule <k (v: Value) <add <hole> e2> rest...> ~> <k <add v e2> rest...>
// ... same pair for position 2, guarded on position 1 already a Value ...
```

with a distinguished `<hole>` term per sort. `strict(1)` limits which positions heat —
which is exactly how short-circuit `and`/`or`, `if`, and assignment targets are
expressed. `seqstrict` (strict, left-to-right, each position waiting on the previous)
is the default reading of bare `strict`.

This is the single highest-leverage generated construct: in KJS-class semantics,
heating/cooling is roughly a third of all rules, all mechanical.

### 6.2 The k-cell discipline: continuations as data

The `<k; ...>` cell holds a *sequence* of computation items; rules match on its
**prefix** (via §4.2 sequence-rest patterns). This one representation gives every
non-local control feature in the target languages:

```lambda
// exceptions: unwind discards the continuation until a handler frame
rule throw-unwind: <k <throw v> rest...>                      ~> <k <unwinding v>>
rule catch:        <k <unwinding v> <try-frame h env> rest...> ~> <k <apply h v> rest...>

// return: discard up to the call frame
rule return:       <k <return v> ... <call-frame k0>>          ~> <k v *k0>

// generators / await: the k-suffix IS the continuation — capture it into the store
rule yield:  <cfg <k <yield v> rest...> <store s> c...>
             ~> <cfg <k <yielded v>> <store {*s, (gaddr): <gen-state [rest]>}> c...>
```

Discarded suffix = exceptions, `break`/`continue`, Ruby's non-local block return,
Bash's `exit`/`errexit`. Captured suffix = JS generators, `async`/`await`
(plus a microtask-queue cell), Python generators/coroutines, Ruby fibers. No
first-class continuations are added to Lambda itself — continuations exist only as
*data* (lists of KItems) inside the model.

### 6.3 Store cells, object identity, determined-key matching

All five target languages have aliasing and mutation, so values alone don't suffice:
the model needs `<store; {Addr: Object}>` with fresh addresses (§4.3) for allocation.
Lookup and update use map rest-patterns:

```lambda
rule var-lookup:
    <cfg <k <var x> rest...> <env {(x): a, *_}> <store {(a): v, *_}> c...>
    ~> <cfg <k v rest...> <env {(x): a, *_}> <store {(a): v, *_}> c...>
```

The parenthesized `(x)` key marks "key determined by the already-bound variable `x`" —
distinguishing it from a literal key. Because every store/env access in KJS-style
semantics looks up a determined key, this restriction keeps matching linear-time while
avoiding general AC matching. If a future model genuinely needs "find *some* entry
such that ...", that is an explicit `for`/`that` query in a `when` clause — visible
cost, not silent engine magic.

### 6.4 Per-language notes

**Lambda** (the first and load-bearing model — see §7):
- The pure `fn` subset is a textbook environment-based functional language: closures,
  `let`, arithmetic with the int/int64/float/decimal ladder, structural `==`,
  elements/maps/arrays, pipes (`|`, `that` desugar to higher-order rules), `for`
  comprehensions with spread-flattening, `match`, string patterns.
- The delicate parts to model precisely — *precisely the parts where implementation
  bugs have historically hidden* (cf. the JS→MIR numeric-inference bugs, pn
  param-inference issues): numeric type promotion and truncation boundaries, boxing
  (`Item` tagging is *not* modeled — the model speaks abstract values; the model
  thereby checks that tagging is unobservable), error propagation `^`/`let v^err`,
  `pn` mutable-capture copy semantics, spread/flattening rules in array literals.
- Effects (`print`, `|>`, `input`) target the `<out>` cell and a virtual file map —
  which is exactly what fixture verification needs (§7).

**JavaScript / TypeScript:**
- Prototype chains = store-walking rules; `this`-binding, hoisting (a pre-pass
  judgment), and the coercion tower (`ToPrimitive`/`ToNumber`/abstract equality) are
  ordinary rules — voluminous but shallow. Event loop + microtasks = two queue cells
  and a scheduling rule. KJS calibrates the size: ~1,250 rules for the ES5 core.
- TypeScript is *not* separately modeled at runtime: model erasure (a judgment from TS
  AST to JS AST), optionally model the type system later as a `judgment`.

**Python:**
- LEGB scoping and cell variables = an env-chain in the store; the bulk of the rule
  count is object protocol — MRO (C3 linearization as a judgment), descriptors,
  dunder dispatch. Generators/coroutines via §6.2 suffix capture.

**Bash** (the odd one out — Smoosh is the roadmap):
- The heart is not control flow but **word expansion**: brace → tilde → parameter →
  command substitution → arithmetic → field splitting → globbing → quote removal, an
  8-stage pipeline that is a natural chain of judgments over word terms. Lambda's
  string patterns shine here.
- State is string-typed (`<env; {string: string}>`); exit-status threading maps
  neatly onto a status cell; pipelines/subshells need a process-tree cell where each
  process has its own k-cell — the one place the config schema gets interesting.
  File descriptors and external commands sit behind `builtin` (§5.6) with a virtual
  FS map, keeping the model deterministic.

**Ruby:**
- Open classes and `method_missing` = store mutation + a dispatch-failure rule;
  blocks/procs/lambdas differ *only* in how their captured k-suffix responds to
  `return`/`break` — a clean stress test of §6.2. `eval` is where §4.4 pays off:
  parse-at-runtime is just an `input()` call inside a rule.

---

## 7. The end goal: fixture verification and bootstrapping

### 7.1 The verification pipeline

For each fixture pair `test/lambda/foo.ls` + `foo.txt`:

```
foo.ls ──input(f,'lambda')──▶ element AST ──normalize──▶ abstract term
        ──load()──▶ initial Cfg ──fixpoint(step)──▶ final Cfg
        ──extract module value + <out> cell──▶ model value
        ──format(v, 'mark')──▶ model output  ⟷ diff ⟷  foo.txt (golden)
```

Three-way comparison per fixture:

1. **model output vs. golden `.txt`** — the headline check;
2. **model output vs. `// expect:` comments** — the `.ls` files carry per-test-point
   expectations (`let t1 = add10(5)  // expect: 15`); a small harness pass maps each
   annotated binding to its position in the module result and checks it individually,
   giving *finer-grained* localization than the whole-file golden;
3. **runtime output vs. model output** — differential testing on generated/edge-case
   programs beyond the fixture set (Redex-style random term generation over the
   `syntax` sorts is nearly free once sorts exist).

Every fixture then lands in one of four buckets:

| runtime == golden | model == golden | verdict |
|---|---|---|
| ✓ | ✓ | **verified** — implementation and spec agree |
| ✓ | ✗ | **suspect golden** — either a captured implementation bug (the motivating case!) or a model bug; triage required, and either way something written down is wrong |
| ✗ | ✓ | runtime regression with an independent confirmation the golden is right |
| ✗ | ✗ | both moved — usually an intended semantics change; update spec and golden together |

The ✓/✗ bucket is the payoff: today those cases are *invisible*. Each triage either
fixes a runtime bug, fixes a model rule, or surfaces genuinely under-specified
semantics that gets settled and documented — all three outcomes are wins.

Harness integration:

```bash
make semantics-verify suite=lambda          # all fixtures, bucket report
make semantics-verify test=closure          # one fixture, with rule trace on mismatch
```

On mismatch the harness reports the first diverging step: the configuration, the rule
that fired (or `stuck: no rule matches`), and the sub-value diff — this is where
derivation recording (§5.4) earns its keep.

### 7.2 Coverage strategy: grow the verified subset

Do **not** attempt all 409 files at once. Order by semantic dependency, roughly:

1. literals, arithmetic + numeric promotion, `let`, comparison (`arith*.ls`,
   `box_unbox*.ls`, `chained_comparisons.ls`, ...)
2. closures, higher-order functions (`closure*.ls`)
3. arrays/maps/elements, spread/flattening, indexing/slicing (`comp_expr*.ls`, ...)
4. pipes, `that`, queries (`child_query.ls`, ...)
5. `match`, string patterns
6. error handling (`raise`, `^`, `let v^err`)
7. `pn` procedural subset: `var`, `while`, mutable captures, assignment targets
8. modules/imports; then long-tail system functions (each an audited `builtin` until
   promoted to rules)

The harness tracks a `semantics-manifest.json`: which fixtures are in the verified
set, which are pending, which are excluded-with-reason (e.g. depend on real I/O).
The baseline CI gate is "verified set stays verified" — the set only grows.

`box_unbox*.ls` deserves a call-out: boxing is an *implementation* concept. The model
deliberately has no boxes — so these fixtures verify the strongest possible property,
that boxing is semantically unobservable. Historically several engine bugs
(numeric-inference truncation, reg-0 sentinels) were exactly observability leaks of
this kind.

### 7.3 Bootstrapping and the circularity question

The Lambda model is written in Lambda and executes on the Lambda runtime — the runtime
under test. Is the oracle independent? Honestly stated:

- **The trusted core** is: the DSL engine (matcher, rewriter, strictness expansion —
  mostly C++ following the validator/view code paths, plus generated glue) and the
  small Lambda subset the strategy library uses. This core is *small*, exercised by
  every rule application (so bugs in it fail loudly and broadly, not silently on one
  fixture), and testable in isolation with its own unit fixtures.
- **The independence argument** is not "different codebase" but **different failure
  modes**: the runtime implements `+` via JIT-compiled tagged-value machine code; the
  model implements it via a rule that fires on abstract values. A truncation bug in
  MIR codegen and a wrong side-condition in a rule have essentially no mechanism for
  coinciding. This is the same argument by which a compiler's test oracle may be an
  interpreter for the same language, sharing the parser.
- **Shared-frontend caveat:** both sides consume the same tree-sitter parse. Parser
  bugs are out of scope of this oracle (mitigation: grammar-level tests; and Ott-style
  export, below).
- **Escape hatch for full independence:** because `syntax`/`rule`/`judgment`
  declarations are *data* after build_ast, an exporter to K (or LaTeX for human
  review, Ott's headline feature) is a formatter, not a redesign. Cross-running even
  the arithmetic fragment in K would spot-check the trusted core itself. Deferred, but
  the design keeps the door open.

And bootstrapping compounds: once the Lambda model is trustworthy, the *guest*
language models (JS, Python, Bash, Ruby) run on a verified substrate, and their
verification targets already exist — `test/js/`, the Node-compat baseline, and the
Jube suites — giving each guest runtime the same four-bucket triage against *its*
captured goldens.

---

## 8. Further directions: what else the model buys

Fixture verification (§7) is one consumer of a more general asset — an executable,
queryable definition of "what should happen." Once the model exists, the following
applications open up, roughly ordered by how directly they pay off for this repo.
None are v1 scope; all are reachable without redesign.

### 8.1 Fuzzing the JIT with a real oracle

Once `syntax` sorts exist, Redex-style random program generation is nearly free — and
the model says what each generated program *should* produce. This targets the bug
class historically found by accident rather than by search: the JS→MIR
numeric-inference bugs, the pn param-inference mis-typing, the reg-0 index-assign
sentinel, the GC-rooting use-after-free were each exposed because some benchmark
happened to trip them. A generator biased toward the delicate corners (numeric
promotion boundaries, closures over `var`, spread-flattening, error propagation
through pipes) plus the model as oracle finds these systematically.

### 8.2 Arbitrating between the three execution paths

`lambda-eval.cpp` interpretation, MIR-Direct, and legacy C2MIR are already de-facto
differential tests of each other — but when they disagree, deciding which is right is
reasoning from intent. The model is the tie-breaker, and disagreement between any one
backend and the model localizes the fault to that backend rather than to "somewhere in
the pipeline."

### 8.3 Spec-first language evolution

Lambda grows features at a fast clip (`push`/`splice`, error destructuring, typed
arrays, the object system), and today a feature's edge-case semantics is settled
implicitly by whatever the implementation does — discovered later as inference bugs.
With the DSL, an RFC ships as rules: edge cases (how does `^` interact with pipes?
what does spread do inside a `for` inside an array literal?) surface when the rule is
*written*, not when a benchmark breaks. The rules then become the regression contract
when the transpiler is refactored.

### 8.4 Testing metatheory, not just programs

Property-test the language design itself, over random programs:

- **type soundness** — run the typing judgment, then step the term: well-typed terms
  don't get stuck;
- **determinism** — no two rules fire on the same configuration;
- **equivalence laws** — `xs | f | g` ≡ `xs | g(f(~))`, purity of `fn`, confluence of
  the functional subset.

The last item matters for the performance work: every optimization proposal
implicitly claims a semantic equivalence, and the model can check that claim on
random inputs before the optimization lands.

### 8.5 Generated, always-correct documentation

Ott's headline feature: since `syntax`/`rule`/`judgment` declarations are data after
`build_ast`, rendering them to LaTeX/HTML/Markdown is a formatter. The prose
semantics in `doc/Lambda_*.md` can carry generated rule boxes that *cannot* drift
from the checked model — and Lambda's own math/LaTeX pipeline is unusually well
suited to render inference rules.

### 8.6 Tooling built on the judgments

The typing judgment is a type checker for free — usable for hover types in an LSP,
ahead-of-time diagnostics in the REPL, or a `lambda check script.ls` mode. Derivation
recording (§5.4) gives an "explain" facility — *why* is this expression `int`? *why*
is this term stuck? — useful both to users and when triaging engine bugs.

### 8.7 Conformance grading for the guest languages

For JS/Python/Bash/Ruby, the model doubles as a coverage instrument: run a corpus
through model and runtime, and the diff *is* the compat report — which constructs the
runtime mis-executes versus simply doesn't reach. That is sharper than a bare
pass-rate (e.g. the Node-compat baseline), which counts outcomes without attributing
semantic causes. The same mechanism validates the TS→JS erasure pass as a judgment.

### 8.8 Long-term options the rule format keeps open

K's ecosystem shows where this road goes if ever wanted: symbolic execution (rules
over terms with logical variables), deductive program verification, and
correct-by-construction interpreter generation. All out of scope here — but they are
the reason rules stay declarative data rather than being compiled away irreversibly.

The common thread: §8.1–8.4 attack the same root problem as §7 — the implementation
is currently its own specification — but from the direction of *future* code
(fuzzing, spec-first) and *invariants* (metatheory) rather than existing goldens.

---

## 9. Deliberate non-goals (v1)

- **Concrete-syntax rules** (K's writing rules in the object language's own notation).
  Element notation is a readable term syntax; tree-sitter ingestion covers real
  programs. Cuts the largest single implementation cost in K.
- **General AC matching** on maps/sets/bags — §6.3's determined-key restriction.
- **Backtracking judgment search** — moded judgments only (§5.4).
- **Symbolic execution / deductive verification** — K's long game; out of scope. The
  rule format doesn't preclude it later.
- **Performance as a runtime.** The model is an oracle, not an interpreter users run.
  Fixture-suite wall-clock should stay in CI-tolerable minutes; if rule dispatch
  becomes the bottleneck, compiling rules through MIR (they are already functions) is
  the natural lever — but only when measured.
- **Modeling the GC, boxing, arenas** — implementation strata are intentionally
  invisible to the model; that invisibility is itself the property being verified.

---

## 10. Implementation plan

Staged so each stage lands with its own tests and is useful standalone.

**Stage 0 — structural matcher + destructuring `match`** *(core language)*
Extend the type-pattern grammar with binders, rest patterns, determined-key map
patterns; extend the validator's walker into a binding matcher (C++). Ship
destructuring `match` to the whole language. Unit fixtures under `test/lambda/`
(with goldens — yes, captured; they enter the verified set in Stage 5).

**Stage 1 — `syntax` declarations + term ingestion**
`syntax_stam` in grammar.js; binder metadata; derived `free_vars`/`subst`/`alpha_eq`;
`fresh()`. `input(f, 'lambda')` CST→element mapping. Validator integration for sort
checking.

**Stage 2 — `rule`, strategies, `config`**
`rule_stam` with `~>`/`when`/`fresh`; rules as `fn(Term) Term^NoMatch`; strategy
library; `config` + cell framing. Milestone: the toy lambda-calculus of §3 runs.

**Stage 3 — `judgment` + strictness expansion**
Moded judgment compilation; derivation recording; `strict`/`strict(i)` heating/cooling
generation with `<hole>`. Milestone: typed lambda-calculus with a typing judgment and
auto-generated evaluation order.

**Stage 4 — Lambda model, functional core**
`semantics/lambda/` model covering coverage tiers 1–5 of §7.2. `builtin` declarations
for system functions. Milestone: `closure.ls` and `arith*.ls` evaluate correctly
under the model.

**Stage 5 — verification harness**
`make semantics-verify`; `// expect:` extraction; four-bucket report;
`semantics-manifest.json`; CI gate on the verified set. **This is the first
end-goal payoff milestone.**

**Stage 6 — Lambda model completion + first triage sweep**
Tiers 6–8 (errors, `pn`, modules). Sweep all fixtures; every ✓/✗ bucket entry triaged
to a runtime fix, a model fix, or a documented semantics decision.

**Stage 7+ — guest languages** *(each independent, orderable by need)*
JS core (largest; KJS as the map — validate against `test/js/` and the Node-compat
baseline) → Python → Ruby → Bash (Smoosh as the map). Each starts with the
CST-normalization judgment and the §6 machinery already in place.

Risk notes: Stage 0 touches the shared pattern grammar — the one stage with
regression surface on the existing language; it gates on `make test-lambda-baseline`
staying 100%. Stages 1–3 are additive (new statement kinds, view_stam-style). The
long tail is Stage 6 builtin coverage — bounded by making every builtin an explicit,
listable declaration (§5.6).

---

## 11. Open questions

1. **Rewrite arrow spelling** — ~~`~>` assumed here~~ **resolved: use `-->`**
   (the classic PL reduction arrow). The parser-ambiguity check this question asked
   for was done during the C6 pipe-syntax discussion
   (`Lambda_Semantics_Formal2.md` §C6.2) and killed `~>`: rule templates and `when`
   clauses contain expressions, where `~ > 3`-style comparisons are common —
   `~>` would differ from `~ >` by one space with silently different meanings.
   Read every `~>` in this proposal's rule sketches as `-->`.
2. **Determined-key marker** — `(x)` in map-pattern key position (§6.3) vs. an
   explicit `key(x):`. The former is terser; the latter survives skimming better.
3. **Where models live** — `semantics/` at repo root vs. `lambda/semantics/` vs.
   shipping as loadable `.ls` packages. Affects whether guest models ship in Jube
   builds.
4. **`// expect:` formalization** — keep as comments parsed by the harness, or promote
   to a checked `expect` statement in test scripts? Comments preserve fixture
   compatibility; a statement is harder to let rot.
5. **Numeric-tower fidelity** — the model needs bit-exact int/i64/f64/decimal
   semantics including overflow/truncation boundaries; decide early whether model
   arithmetic uses Lambda's own numerics (circularity: acceptable? probably yes with
   targeted unit tests on the boundaries) or explicit bit-level rules for the
   boundaries only.

---

## 12. Summary

Lambda's elements, symbols, recursive union types, string patterns, error values, and
the view-template precedent already provide the term language, grammar language, and
failure semantics of a K/Ott-class framework. This proposal adds, in order:
destructuring and frame patterns, fresh symbols, and source ingestion (Tier 1);
`syntax`/`config`/`rule`/`judgment`/`builtin` declaration forms with a strategy
library (Tier 2); and strictness expansion, the k-cell continuation discipline, and
determined-key store matching (Tier 3) — the trio that lifts the DSL from calculi to
real scripting languages, as proven feasible at scale by KJS, K-Python, and Smoosh.

The end goal is concrete: an executable Lambda semantics that independently verifies
the `test/lambda` fixtures, converting a golden suite that can only detect regressions
into one that can detect *captured bugs* — and then extends the same oracle to every
guest language the engine runs.

---

# Follow-up Parts

The sections below were added after the original proposal, following a close review
of Lambda's own semantics (findings in
[Lambda_Semantics_Formal.md](Lambda_Semantics_Formal.md)) and an assessment of how
far the framework stretches toward low-level languages.

## 13. Follow-up: is Lambda ready to be the meta-language?

Honest assessment, in four parts.

### 13.1 The data model: yes

Elements, symbols-distinct-from-strings, structural equality, occurrence types,
errors-as-values with compile-enforced handling — the fragment the DSL engine and
models stand on is more principled than any mainstream scripting language's core.
Nothing in the review shook the §2 mapping table; the term language, grammar
language, and failure semantics are sound foundations.

### 13.2 The definition rigor: not yet

The docs are user-guide-grade, not spec-grade, and probing the implementation found
doc-vs-implementation divergences on core operators within minutes (`'a' == "a"`
raises where docs say `false`; three inconsistent integer-overflow regimes; three
aliasing regimes; OOB indexing yielding unchecked error values). The full verified
list is [Lambda_Semantics_Formal.md](Lambda_Semantics_Formal.md) Part A (findings
A1–A10), with design recommendations in Part B.

This does not undermine the proposal — it sharpens it. The DSL uses a bounded
meta-fragment, and every A-finding is precisely a decision the formal rules would
have forced anyway. But it does create a prerequisite:

### 13.3 Prerequisite: meta-fragment hardening

Before Stage 4 (the Lambda model) is written, the semantic corners the *meta-language
itself* relies on must be decided and fixed — at minimum A1 (overflow regimes,
because model arithmetic runs on Lambda numerics; this supersedes open question 5 in
§11), A5 (`==` reflexivity and representation-independence, because `==` is the
harness's comparison primitive), and A7 (aliasing, because the rewrite engine passes
terms around). The triage table in Lambda_Semantics_Formal.md is the worklist; the
Stage-4 gate is "every finding the meta-fragment touches is resolved (fixed or
formally documented as-is)."

A second, structural caveat: Lambda's type system is *gradual*, not "strong static"
as the docs claim, and inference is currently **observable** — inferred types have
changed runtime results twice (the pn param float-div truncation; the JS→MIR
numeric-inference bugs). For a meta-language this must become an invariant: erasing
inferred types must never change observable results (the "gradual guarantee",
finding B7). The model itself is the checking instrument — it has no inference, so
any model-vs-runtime divergence on inference-sensitive programs is a violation by
construction.

### 13.4 The existing Redex model: prior art, seed, and second oracle

[Lambda_Semantics.md](Lambda_Semantics.md) documents an existing PLT Redex model of
Lambda — functional core, procedural extension (store-based), and object types, with
an `--emit-sexpr` bridge from the production parser and a fixture-verification
harness (`make test-redex-baseline`, 87/194 agreement, and a designer-ruling protocol
for disagreements). Three consequences for this proposal:

- **Feasibility is no longer speculative.** The Redex project has already executed
  this proposal's §7 loop end-to-end: model → bridge → fixture comparison →
  disagreement triage. Its §4.7 disagreement log (e.g. `list` vs `array` under `is`,
  `fill()` coercion into typed arrays, `type(error_value)`) is exactly the
  "suspect golden / underspecified semantics" bucket §7.1 predicts.
- **It is a migration source, not a competitor.** The native DSL replaces the
  external Racket dependency and big-step-only shape with in-language declarations,
  small-step configurations, strictness generation, binder support, and guest-language
  reach. The Redex big-step rules port naturally to `judgment` declarations
  (§5.4 compiles moded judgments to exactly the recursive-function shape Redex's
  evaluator has); the `--emit-sexpr` mapping is the direct precedent for
  `input(f, 'lambda')` (§4.4); and the Redex-verified fixture subset seeds Stage 5's
  `semantics-manifest.json`.
- **Keep it as the independent cross-check.** §7.3's honest weakness was that model
  and runtime share a host. A maintained Redex fragment (even just arithmetic +
  closures) runs on a *different* host and satisfies the "escape hatch for full
  independence" §7.3 wanted — cheaper than the deferred K exporter.

**Verdict:** ready in structure, not yet in rigor — and the gap is a finite,
enumerated worklist (Lambda_Semantics_Formal.md) rather than an open-ended research
problem. The fixture-verification goal and the hardening prerequisite reinforce each
other: fixing the meta-fragment is the first triage sweep.

## 14. Follow-up: modeling low-level languages — the C+ subset

Can the framework stretch to a C/C++ subset — specifically the **C+ convention**
(`doc/dev/C_Plus_Convention.md`) Lambda itself is written in? Yes, and it is arguably
a *better* fit than the scripting languages, because low-level formalization is the
most battle-tested application of this style: CompCert's **Clight** (the verified-C
fragment), and **KCC / RV-Match**, which turned an executable K semantics of C into a
commercial undefined-behavior detector.

### 14.1 What a C+ model needs beyond §6

- **A memory model as cells — with abstract pointers.** Model pointers
  CompCert-style as *(block, offset)* pairs rather than raw integers over a flat byte
  array: allocation creates a fresh block (§4.3 `fresh()`), field access is
  offset-within-block, and pointer arithmetic that escapes its block simply has no
  rule. Struct layout can stay abstract (field-indexed, as Clight does) unless a
  model needs `sizeof`/padding fidelity — then a per-target layout judgment computes
  offsets explicitly.
- **UB as stuckness — the payoff.** Out-of-bounds access, use-after-free, signed
  overflow, aliasing violations: in a rewrite semantics these are configurations
  where **no rule applies**, i.e. detected, not silently executed. That converts the
  model into a UB detector for C+ code — directly relevant given the engine's own bug
  history (JIT GC-rooting use-after-free, `array_num_get` guard violations) is
  dominated by exactly this class.
- **Bit-precise arithmetic in the meta-language.** A C model needs exact
  i8…u64/f32/f64 operations including wrap. Ironically, Lambda's sized types with
  wrapping semantics are the right tool — but per finding A1
  ([Lambda_Semantics_Formal.md](Lambda_Semantics_Formal.md)), their own semantics is
  currently implicit. The §13.3 hardening prerequisite therefore gates this model
  too: the meta-language's numeric story must be written down before it can carry
  someone else's.
- **Control flow** (`goto`, `switch` fallthrough, `break`/`continue`) is the k-cell
  discipline of §6.2, unchanged. Function pointers are store values. `longjmp`-style
  features are absent from C+ and stay out of scope.

### 14.2 Why C+ specifically is a well-chosen target

The C+ convention deliberately excludes the features that make full C++
formalization hopeless — exceptions, virtual dispatch, RTTI, template
metaprogramming (no one has formalized their interaction). What remains is roughly
Clight plus a small delta, each part cheap in the framework:

| C+ feature | Modeling cost |
|---|---|
| Structs, pointers, arrays | The §14.1 memory model — the core work |
| Struct inheritance (layout extension) | Prefix-layout: field lookup walks the base chain — one rule |
| Inline member functions | Desugar to free functions with explicit `this` — a normalization judgment |
| `extern "C"` / dual `.h`/`.hpp` views | Irrelevant at the semantic level (same layout by construction) |
| Manual ref-counting, pools, arenas | Ordinary heap operations *in* the model — and their disciplines (no leak, no double-free, arena-outlives-borrower) become checkable properties |
| Error sentinels, `GUARD_ERROR` | Plain values and rules; no unwinding machinery needed |
| Templates (utility-only) | Instantiate-then-model; no generic template semantics required |

Tree-sitter's C/C++ grammar covers ingestion (§4.4). The scope is a Clight-sized
effort, not a C++-standard-sized one.

### 14.3 The horizon: the full bootstrap

The Lambda runtime is *written* in C+. A C+ model therefore closes the deepest loop
available to this project: model enough of C+ to state and check invariants of the
runtime's own core — `Item` tagging (a value is never read at a type it wasn't
tagged with), ref-count/ownership discipline, arena-lifetime rules ("arena data
lives until reset"), the GC-rooting invariant that BUG-001 violated. That is Lambda
modeling the language it is implemented in, with its own implementation as the test
corpus. Explicitly a horizon goal, not a milestone — but nothing in the DSL design
needs to change to keep it reachable, which is itself a reason to prefer the §14.1
block-based memory model from the start.
