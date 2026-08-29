# Lambda Semantics POC — Executable Semantics in Today's Lambda

**Status:** draft POC proposal
**Parent:** [Lambda_Semantics_DSL_Proposal.md](Lambda_Semantics_DSL_Proposal.md) — same end goal, radically reduced means
**Scope:** the **functional core of Lambda only** — no guest languages, no procedural (`pn`) subset, no new syntax, no new runtime machinery
**Thesis:** a big-step definitional interpreter for Lambda's functional core can be written **in plain, already-shipping Lambda** — elements as terms, `match` on tags as rule dispatch, the `T^E` channel as stuckness — and verified differentially against the runtime with **zero host-side code**. Every construct this document relies on was executed on the current `lambda.exe` before being written down (§3, §4).

---

## 1. Relation to the DSL proposal

The parent proposal designs a K/Ott-class semantics sub-language: destructuring
patterns, `syntax`/`config`/`rule`/`judgment` declaration forms, strictness
expansion, the k-cell discipline — three tiers of additions before the first
Lambda model can be written (its §4–§6), and a C++ matcher/rewriter engine
underneath.

This POC inverts the order. Before building the vehicle, drive the terrain:
write the Lambda model **now**, in the language as it exists, and let the
experience produce (a) an early version of the §7 fixture-verification payoff,
(b) a live inventory of exactly which missing features hurt and how much — data
the Tier 1/2/3 designs currently lack, and (c) hard evidence for (or against)
the parent's §13 claim that Lambda is ready to be its own meta-language.

The POC is not a rival design. It is Stage 4 of the parent's plan (the Lambda
model, functional core) pulled forward to Stage 0, at the cost of verbosity that
the DSL would later remove. Every hand-written `fn` arm in the POC model
corresponds 1:1 to a future `rule`/`judgment` declaration, so the model is a
migration **seed**, not throwaway code (§9).

### 1.1 The one big simplification: big-step, environment-based

Two structural choices eliminate the parent's entire Tier 2 and Tier 3, and most
of Tier 1, for this scope:

- **Big-step instead of small-step.** Evaluation order is encoded by the
  meta-language's own recursion (`ev(t[0]) + ev(t[1])` *is* left-to-right
  strictness). No `strict` annotations, no heating/cooling, no k-cell, no
  configuration cells, no `config` form. This is exactly the shape of the
  existing Redex model (parent §13.4), which reached 87/194 fixture agreement in
  precisely this style — big-step is *proven sufficient* for the functional
  core.
- **Environments instead of substitution.** Closures capture their defining
  environment as data; variables are looked up, never substituted. No
  capture-avoiding `subst`, no `alpha_eq`, no `binds` specs, no `fresh()` —
  the parent's §4.3 and the binder half of §5.1 are simply not needed.
  Recursion needs no mutation or cycles either: a named function value re-binds
  *itself* into the environment at each application (§4.4 — verified with
  factorial).

What big-step gives up — modeling divergence distinctly from stuckness, and the
per-step traces the parent's §7.1 mismatch reports want — is an acceptable POC
trade: fixtures terminate, and mismatch localization falls back to per-check
granularity (§6) — once divergence is handled. It initially was not: a wrong
rule that made a modelled program *diverge* printed nothing at all, erasing
every other result in the file (finding **F5**, §7). M2 closed this with a
`fuel` argument bounding recursion depth, which turns divergence into ordinary
stuckness and restores per-check localization. The model is therefore **total**:
every term either yields a value or is stuck, and omega is just another check.

### 1.2 Decision ledger

For citability (per `doc/Doc_Convention.md` conventions; no `S#`/`D#` ruling
covers meta-circular modeling, so these are working-doc decisions):

| ID | Decision |
|----|----------|
| P1 | POC uses **zero new syntax and zero new C/C++ code**; optional Phase-3 ingestion (§6.3) is the only host-side item, and it is severable. |
| P2 | Big-step, environment-based, closures-as-data. No small-step, no substitution, no fresh names. |
| P3 | Terms are elements: tag = node kind, attributes = atomic metadata (names, literal values), children = sub-terms. Environments and closures are elements too (§4.2). |
| P4 | Stuckness = raised error on the `T^` channel; rule dispatch = `match` on `name(t)`; where fall-through choice is needed, errors-are-falsy `or` chaining (verified §3). |
| P5 | Model scalar arithmetic **uses Lambda's own numerics** — the parent's §11 open question 5 resolved pragmatically for the POC, with the A1 caveat recorded in §10. |
| P6 | Verification is **self-differential in-script**: each check compares `ev(term)` against the runtime evaluating the same expression natively, in the same fixture; goldens are arrays of `true` (§6.1). |
| P7 | POC fixtures are ordinary `.ls`+`.txt` pairs under `test/lambda/sem/`, needing no new harness — but that directory is **deliberately not yet in the baseline's discovery list** (§5), so a model still being shaped cannot dictate what the runtime must do. |
| P8 | Scope ceiling: functional core (literals, arithmetic/comparison/logical, `let`, `if`, closures/application, recursion, arrays/maps/for basics). Errors-as-modeled-values, `pn`, modules, system functions beyond a tiny builtin list: out (§8, §9). |
| P9 | The POC model is the migration seed for the parent's Stage 4; arm ↔ rule correspondence is maintained deliberately (§9.2). |
| P10 | Meta-fragment findings made while building the POC are recorded in §7 and fed to the `Lambda_Semantics_Formal.md` worklist — this is the parent's §13.3 hardening loop, started early. |

---

## 2. Construct-by-construct substitution map

What the parent proposal adds vs. what the POC uses instead — all right-column
items exist and were executed today:

| Parent construct | POC substitute (shipping Lambda) |
|---|---|
| §4.1 destructuring `match` | `match name(t) { case 'add': ... }` + manual extraction `t[0]`, `t.n` |
| §4.2 rest/frame patterns | `for (c in el)` child iteration; list-variable splicing into element content |
| §4.3 `fresh()` | not needed (environment-based, P2) |
| §4.4 `input(f, 'lambda')` | hand-encoded terms (Phases 1–2); optional Mark AST dump (Phase 3, §6.3) |
| §5.1 `syntax` sorts + binders | naming convention (§4.1) now; ordinary `type` decls later if wanted; no binders needed |
| §5.2 `config` cells | none — big-step needs no configuration |
| §5.3 `rule` + `-->` | one `match` arm per node kind inside a judgment `fn` |
| §5.4 `judgment` | plain recursive `fn ev(t, env) any^` — exactly what §5.4 says judgments compile to anyway |
| §5.5 strategy library | `or` over error-returning `fn`s where needed (verified); mostly unnecessary in big-step |
| §5.6 `builtin` | a named `fn` per trusted primitive, listed in one place in the model file |
| §6.1–6.3 strictness/k-cell/store | none — out of scope with big-step + functional-only |
| §7.1 verification pipeline | in-script self-differential checks; four-bucket logic at fixture granularity (§6) |

The cost of the substitutions is **verbosity and weaker static assurance**
(no sort-checking of terms, manual extraction instead of patterns). The POC
accepts both, and measures them: §9.1 asks "which absence hurt most?" as a
formal deliverable back to the Tier-1 design.

---

## 3. Empirical feasibility — what was verified on today's runtime

Every load-bearing feature was probed on the current build (2026-08-28,
`lambda.exe` on master) before this document was written. Verified working:

| Feature | Evidence |
|---|---|
| Element terms: literal construction, `name(el)` tag, `el[i]` children, `.attr`, `len`, `for` iteration | `<add <lit value: 1> ...>`; `name(t)` → `'add'` |
| Variable interpolation as element children | `<add a b>` with `a`,`b` bound to elements → 2 children |
| List-variable splicing into content | `let more = for (c in old) c` then `<env first more>` → children spliced flat |
| Attributes holding **any** value — elements, arrays, closures | `<b n:'x', v: c>` where `c` is an element → `b.v` is that element |
| Structural `==` on elements | `<add <lit value:1>> == <add <lit value:1>>` → `true` |
| `match` on symbol values + `default` | the kernel's dispatch (§4.3) |
| Recursion, first-class closures, closures stored in data | `boxed[0](boxed[1])` → calls through array |
| Errors falsy ⇒ `or` is deterministic rule choice | `r1(3) or r2(3)` → `103` when `r1` errors; `r1(9) or r2(9)` → `18` |
| `raise` / `T^` / postfix `^` / `^ { }` handler | `run(5) ^ { -1 }` → `104`; `run(0) ^ { -1 }` → `-1` |
| Dynamic map lookup by symbol key; literal-key spread extension | `env[k]`, `{*:env, z: 30}` |
| `for ... where` filtering | the `lookup` fn (§4.3) |
| Nested arrays | `[['x', 10], ['y', 20]]` indexes correctly |

**Dialect notes** (parse rules the model code must respect — each cost one probe
iteration to discover; see also §7):

1. **Attribute/content separator is `,`** — `<clo p: t.p, body env>`; content
   children are then juxtaposed. (`doc/Lambda_Data.md` still shows `;` — stale;
   §7 F2.)
2. **Element content expressions are restricted to simple primaries.** `t[0]`
   or `(t[0])` in child position fails to parse; hoist into a `let` first.
   (§7 F3.)
3. **Line-delimiter rules (S16):** a line starting with `<` or `[` after an
   expression line needs an explicit `;` on the preceding statement, or it is
   glued to it. An inline parenthesized `(for ...)` following a child is
   parsed as a **call** on that child, silently yielding an `error` child.
   (§7 F4.)
4. Symbols are written `'x'`; anonymous functions are `(x) => ...` or
   `fn (x) { ... }`.

None of these block the design; all were absorbed by the kernel below.

---

## 4. The model

### 4.1 Term encoding

One element per AST node kind. Conventions (P3):

- **Tag** = node kind, a short symbol distinct from Lambda surface keywords
  (`lett` not `let`, `iff` not `if`, `vr` not `var`) so terms never collide
  with syntax and stay greppable.
- **Attributes** = atomic metadata: `v:` literal value, `n:` variable name
  (a symbol), `p:` parameter name, `f:` self-name of a recursive function.
- **Children** = sub-terms, in evaluation-order position.

MVP node-kind inventory (grows per the §8 ladder):

| Term | Meaning |
|---|---|
| `<lit v: k>` | literal scalar `k` (int/float/string/bool/null/symbol) |
| `<vr n: 'x'>` | variable reference |
| `<add a b>` `<sub a b>` `<mul a b>` `<div a b>` ... | arithmetic |
| `<lt a b>` `<le a b>` `<eq a b>` ... | comparison |
| `<andd a b>` `<orr a b>` `<nott a>` | logical (short-circuit encoded by `iff` desugaring or dedicated arms) |
| `<iff c t e>` | conditional |
| `<lett n:'x', e body>` | let-binding |
| `<lam p:'x', body>` | anonymous function |
| `<fun f:'f', p:'x', body>` | named (recursive) function |
| `<app f a>` | application (single-param MVP; n-ary later) |
| `<arr e*>` / `<idx a i>` | array literal / indexing |
| `<mp (<kv k:'k', e>)*>` / `<fld m k>` | map literal / field access |
| `<forr n:'x', src body>` | for-comprehension |

### 4.2 Values, environments, closures

- **Scalar model values are host values** (P5): the model's `3` is Lambda's
  `3`. This is the parent's §11-Q5 circularity, accepted deliberately: the
  independence argument (parent §7.3) rests on *different failure modes* —
  a rule that walks `<add a b>` and a JIT lane-inference bug have no shared
  mechanism — not on disjoint arithmetic. The A1 numeric-boundary caveat is
  carried in §10.
- **Environment** = element assoc-list: `<env <b n:'x', v: V> ...>`, newest
  binding first. `bind` prepends; `lookup` takes the first hit — which makes
  **shadowing** correct by construction. Values ride in the `v:` attribute
  precisely because attributes hold any value without list-splicing hazards
  (verified §3).
- **Closure** = `<clo p:'x', body env>`: parameter as attribute, body term and
  captured environment as the two children. Recursive closure =
  `<rclo f:'f', p:'x', body env>` (§4.4).

The map-based alternative (`{*:env, x: v}`) was probed and rejected for the
general case: map literals cannot use a **computed** key, so an environment
keyed by names that are *data* at model-run time cannot be extended — and the
probe also tripped a then-live `len`-on-spread-map bug (§7 F1, since fixed).
The element
assoc-list has neither problem, at O(n) lookup cost that is irrelevant at
fixture scale.

### 4.3 The kernel — runs verbatim today

The following is the actual POC kernel, executed on the current build; final
output `[true, true, true, true, true]`. It is ~40 lines for literals,
arithmetic, comparison, conditionals, let, closures, application, and
stuckness:

```lambda
// sem_poc kernel: big-step definitional interpreter for a Lambda functional core
// terms are elements; env is an element assoc-list; closures are elements

// env: <env <b n:'x', v: <any value>> ...>; lookup walks children left-to-right
fn lookup(env, n) => {
    let hits = for (b in env where b.n == n) b.v
    if (len(hits) > 0) hits[0] else raise error("unbound variable")
}

fn bind(env, n, v) => {
    let rest = for (b in env) b;
    <env <b n: n, v: v> rest>
}

fn ev(t, env) any^ => match name(t) {
    case 'lit': t.v
    case 'vr':  lookup(env, t.n)^
    case 'add': ev(t[0], env)^ + ev(t[1], env)^
    case 'mul': ev(t[0], env)^ * ev(t[1], env)^
    case 'lt':  ev(t[0], env)^ < ev(t[1], env)^
    case 'iff': if (ev(t[0], env)^) ev(t[1], env)^ else ev(t[2], env)^
    case 'lam': {
        let body = t[0];
        <clo p: t.p, body env>
    }
    case 'app': {
        let f = ev(t[0], env)^
        let a = ev(t[1], env)^
        let body = f[0]
        let cenv = f[1]
        ev(body, bind(cenv, f.p, a))^
    }
    case 'lett': ev(t[1], bind(env, t.n, ev(t[0], env)^))^
    default: raise error("stuck: no rule for term")
}

let env0 = <env>

// differential checks: model result vs the runtime evaluating the same expression
let c1 = (ev(<add <lit v:3> <mul <lit v:4> <lit v:5>>>, env0) ^ { 'stuck' }) == (3 + 4 * 5)
let c2 = (ev(<lett n:'x', <lit v:10> <add <vr n:'x'> <lit v:1>>>, env0) ^ { 'stuck' }) == (let x = 10, x + 1)
let c3 = (ev(<app <lam p:'y', <add <vr n:'y'> <lit v:2>>> <lit v:40>>, env0) ^ { 'stuck' }) == (((y) => y + 2)(40))
// closure capture: let a = 7; let f = \z. z + a; f 5
let c4 = (ev(<lett n:'a', <lit v:7>
              <lett n:'f', <lam p:'z', <add <vr n:'z'> <vr n:'a'>>>
               <app <vr n:'f'> <lit v:5>>>>, env0) ^ { 'stuck' }) == 12
// stuckness: unbound variable is a raised error, not a value
let c5 = (ev(<vr n:'q'>, env0) ^ { 'stuck' }) == 'stuck';
[c1, c2, c3, c4, c5]
```

Design points visible in the kernel:

- **`ev` is the judgment** — `fn ev(t, env) any^` is literally what the
  parent's §5.4 says moded judgments compile to. The `^` on every recursive
  call is stuckness propagation; the arm-per-kind `match` is the rule set.
- **Stuckness is total and typed.** Anything the model does not cover raises;
  raises reach the check as the `'stuck'` sentinel via the `^ { }` handler.
  There is no way for an unmodeled construct to silently produce a value —
  the property the parent's §7.1 four-bucket triage depends on.
- **Left-to-right strictness for free**: Lambda's own argument-evaluation
  order in `ev(t[0], env)^ + ev(t[1], env)^` is the model's evaluation order.
  (This *is* meta-circular trust — flagged in §10.)

### 4.4 Recursion without mutation — verified

Environment-based recursion normally wants a cyclic environment (Landin's
knot) or a store. The POC needs neither: a named function value carries its
own name, and **application re-binds the value under its own name** before
evaluating the body:

```lambda
    case 'fun': {
        let body = t[0];
        <rclo f: t.f, p: t.p, body env>
    }
    case 'app': {
        let f = ev(t[0], env)^
        let a = ev(t[1], env)^
        let body = f[0]
        let cenv = f[1]
        let env1 = bind(bind(cenv, f.p, a), f.f, f)
        ev(body, env1)^
    }
```

Verified: the model evaluating
`let fact = fun fact(n) => if n <= 1 then 1 else n * fact(n-1); fact(6)`
as a term yields `720`, equal to native `fact(6)` in the same script. (For the
MVP, `lam` can be treated as `fun` with an unused self-name, collapsing the two
arms.) Mutual recursion is deferred to the ladder (§8, M2) — the same trick
generalizes by binding a *group* of `rclo`s, at the cost of a group attribute.

### 4.5 The builtin boundary

Per P8, the model trusts a short list of primitives (`len`, string concat,
numeric coercion as needed by covered fixtures), each an ordinary named `fn` in
one clearly-marked section of the model file — the POC's version of the
parent's §5.6 auditability ("what does this model trust?" = read that
section). No declaration form needed at this scale.

---

## 5. Files and layout

```
test/lambda/sem/sem_poc_kernel.ls    // the interpreter, `pub fn`s — NO .txt, so not discovered
test/lambda/sem/sem_poc_core.ls      // positive differential checks, M0-M2      (57)  [LANDED]
test/lambda/sem/sem_poc_stuck.ls     // negative suite: STUCK / TOTAL / ADOPTED  (36)  [LANDED]
test/lambda/sem/sem_poc_fixtures.ls  // real test/lambda programs, translated    (33)  [LANDED]
```

Discovery collects only `.ls` files that have a matching `.txt`, so the kernel
module is skipped automatically and the three fixtures share one copy of the
interpreter — no duplication, and no harness change to arrange it.

Per P7 these are ordinary `.ls`+`.txt` fixtures under `test/lambda/sem/`, so
CLAUDE.md rule 8 supplies the golden discipline and no harness, Makefile
target, or manifest is added for Phases 1–2. The kernel is duplicated per
fixture file at first (fixtures are self-contained); if that grates, module
import of a shared `sem_poc_kernel.ls` is an ordinary Lambda `import` —
a later cleanup, not a requirement.

**Not in the baseline yet** (designer call, 2026-08-29): `test/lambda/sem` is
deliberately absent from `FUNCTIONAL_TEST_DIRECTORIES` in
`test/test_lambda_gtest.cpp`, so `make test-lambda-baseline` does not run these
while the model is still being shaped. Adding that one line is what turns the
POC into a CI gate — do it when the M3 negative suite lands, not before, so a
half-written model cannot start dictating what the runtime must do.

Goldens for these fixtures are arrays of `true` — which sidesteps the
"golden captured from the implementation" objection for the *checks
themselves*: a golden of `[true, true, ...]` is a specification, not a
captured output. (The objection survives for the *native side* of each
comparison; that is the point — see §6.1.)

---

## 6. Verification: the differential scheme

### 6.1 Phase 1 — in-script self-differential checks (P6)

Each check pairs a term with the same program in native syntax, in one script:

```lambda
let cN = (ev(TERM, env0) ^ { 'stuck' }) == NATIVE_EXPR
```

This is the parent's §7.1 three-way comparison collapsed to two ways at
per-expression granularity, with the golden fixed at `true`:

- `cN == true` — model and runtime agree (parent bucket ✓✓, **verified**);
- `cN == false` — they disagree: either a model bug or a **runtime bug**
  (bucket ✓✗ — the motivating case, now visible per-expression);
- `cN == 'stuck'`-shaped mismatches localize under-coverage instantly.

The independence argument is the parent's §7.3 unchanged: the native side runs
through parser → AST → MIR JIT → tagged values; the model side walks elements
and applies rules. A truncation bug in lane inference and a wrong side
condition in an arm have no mechanism for coinciding — and the historical bug
inventory (pn-param float-div inference, JS→MIR numeric inference, boxing
observability leaks) is exactly the class the native side would get wrong and
the model side would not. Both sides sharing one process and one parse is the
known, accepted weakness (§10; the maintained Redex fragment remains the
cross-host escape hatch, parent §13.4).

### 6.2 Phase 2 — hand-translated real fixtures

Pick a slice of the real suite whose semantics the model covers — candidates
from the parent's §7.2 tier 1–2 ordering: `arith*.ls`, `chained_comparisons.ls`,
`closure*.ls` (simplest members first) — and hand-translate each script into a
term, asserting model result == native result of the *actual fixture
expressions*. Hand-translation is honest POC labor: it is bounded (a dozen
fixtures suffice to exercise the loop), it needs no host code, and every
translated fixture becomes a regression test for Phase 3's mechanical
translation.

### 6.3 Phase 3 (optional, the only host-code item) — Mark AST ingestion

The production C parser already emits a canonical s-expression AST dump
(`lambda.exe --emit-ast-dump f.ls`, `lambda/runtime/emit_ast_dump.cpp`), and
`input(f, 'mark')` already parses Mark — Lambda's own data notation — into
elements. The gap is only the *format*: add a Mark-flavored sibling of the
existing dump emitter (`--emit-ast-mark`), a formatter over the already-built
AST, not a parser. Then:

```lambda
let raw  = input("temp/fixture_ast.mark", 'mark')   // dump of the fixture, as elements
let term = normalize(raw)                            // AST-dump shapes → §4.1 terms, written in Lambda
let v    = ev(term, env0)^
```

`normalize` is itself a Lambda function over elements — the first real test of
the model style on a *big* vocabulary, and the direct descendant of the Redex
bridge's `--emit-sexpr` mapping (parent §13.4). Phase 3 is severable: if it is
never built, Phases 1–2 still deliver the POC's evidence. If it is built, the
parent's §7.2 manifest/`make semantics-verify` machinery becomes worth adding —
*that* is the natural graduation point from POC to the real Stage 5.

---

## 7. Findings already produced (the loop is already paying)

Probing for this document — before any model existed — surfaced four
meta-fragment findings in under an hour, confirming the parent's §13.3 premise
that building the model is itself the hardening instrument:

- **F1 — `len` and iteration skipped spread-built map fields. FIXED
  2026-08-28.** `let b = {*:m, w: 5}` with `len(m) == 3` gave a `b` that
  printed all four entries and read `b.w` correctly, but `len(b) == 2`.
  Root cause: a spread is one *nameless* `ShapeEntry` holding a raw `Map*`
  link, and both `fn_len`'s map arm (raw `map_type->length`) and `item_keys`
  (`if (field->name)`) counted that single slot as one entry instead of
  flattening through it. Fixed by a shared flattening key walk
  (`map_flat_field_count` / `map_collect_flat_keys` in
  `lambda/runtime/lambda-data-runtime.cpp`), so `len` now provably equals the
  iteration count S8.3.1 defines it as, for maps and element attribute spread
  alike. A `TypeMap::has_spread` bit, set where a nameless entry enters a shape
  and propagated across shape clones, keeps that walk off the common path:
  without a spread `len` reads `length` directly, since **map keys are unique —
  a duplicate key is a bug, not a case to accommodate** (designer ruling,
  2026-08-28). Regression fixture: `test/lambda/map_spread_len.ls`.

  **The blast radius is the real finding.** Because a *purely* spread-built
  element (`<graph *:attrs, ...>` — the shape every canonicalizing builder in
  `lambda/package/graph/` produces) has *only* a nameless slot, `item_keys`
  returned the **empty list** for it. The graph schema validator walks
  `for (key, attr_value in map(value))` to type-check attributes, so for every
  canonical graph it iterated nothing and reported `valid: true` having checked
  **zero attributes**. Fourteen `graphviz_*` fixtures had captured that vacuous
  pass as their golden. Turning iteration on immediately exposed a second,
  independent latent bug in `lambda/package/graph/schema.ls`
  (`present_attr_diagnostics` type-checked null-valued — i.e. absent —
  attributes, contradicting its own `required_attr_missing`); with both fixed
  the baseline is 3982/3982.

  This is the parent's §7.1 four-bucket triage playing out for real, and the
  outcome is the one the bucket table predicts as most valuable: goldens in the
  ✓✗ cell that had silently enshrined *two* stacked defects, one of which had
  disabled a whole validator.
- **F2 — doc drift on the element attribute/content separator. FIXED.**
  `doc/Lambda_Data.md` showed `<div class: "main"; "content">`; the parser
  rejects `;` with *"expected ',' between element attributes and content"*.
  The docs predated the separator change; they now show `,`. (Same class as the parent's §13.2
  `'a' == "a"` finding: user-guide-grade docs diverging from the
  implementation.)
- **F3 — element content expressions are restricted.** `<clo p: x, t[0]>` and
  `<clo p: x, (t[0])>` both fail to parse; only simple primaries (identifiers,
  literals, nested elements) are accepted as children. Workaround is a `let`
  hoist. Worth either documenting as a rule or widening the grammar —
  a candidate for an explicit `S16` ruling either way.
- **F4 — juxtaposition hazard in content position.** `<env <b n:9> (for (c in
  old) c)>` parses the parenthesized comprehension as a **call** on the
  preceding child element, producing `<env error>` silently at build time.
  The line-delimiter rules protect statement position but not content
  position; the silent `error` child is the sharp part.

- **F5 — a diverging model destroys the whole fixture's output, not one
  check.** Mutation-testing M1 (a wrong `sub` arm computing addition) turned
  the modelled factorial into a non-terminating program: `error[E308]: Stack
  overflow`, and the fixture printed **nothing at all** — because a script's
  top level is collected content that prints only once evaluation completes
  (S16.7), one diverging check erases the other 34 results. The golden still
  catches it (empty ≠ 35 lines), so the suite has teeth, but §6.1's claim of
  *per-expression* localization does not hold for divergence: big-step cannot
  distinguish it from stuckness (§1.1), and the crash is whole-file. The
  standard remedy is a **fuel parameter** — `ev(t, env, fuel)` decrementing
  per step and raising `out of fuel` at zero — which converts divergence into
  ordinary stuckness, makes the judgment total, and restores per-check
  localization. Deferred: it threads an extra argument through every arm, and
  is worth doing when M2's coverage makes accidental divergence likelier.

- **F6 — compile-time rejection of literal `div`/`%` by zero contradicts the
  run-time answer.** `1 % 0` and `1 div 0` are **compile errors** (E312,
  `build_ast.cpp:7727`, guarded on `OPERATOR_IDIV`/`OPERATOR_MOD` and a literal
  zero, citing S3.3.4). But the identical operations are **legal at run time**
  once the zero is not a literal: `10 % zero(3)` = `nan`, `1 div zero(3)` =
  `inf`. So constant-foldability decides program legality — the same expression
  is rejected or accepted depending only on whether the compiler can see the
  operand — which is the observable-inference class SI3v2 rules out. Note the
  check does NOT cover `/`: `1 / 0` compiles and yields `inf` at both times,
  consistently.
  The comparison languages do not support the current combination. Go, Rust,
  Swift, C# and Zig all reject a literal integer division by zero at compile
  time — but in every one of them the run-time operation *traps or panics*, so
  the compile-time error is an early diagnosis of a real fault. Lambda has no
  such fault to diagnose: the run-time answer is an ordinary `inf`/`nan` value.
  Either the run time should reject what the compiler rejects, or the compiler
  should accept what the run time accepts; today a program's legality depends
  on where the zero comes from. Pinned by a check in `sem_poc_core.ls`.
  Two adjacent oddities surfaced with it, not yet triaged: an integer operator
  (`div`) escaping to a float `inf`, and `type(nan)` answering `int`.

- **F7 — an unknown infix word silently splits an expression.** There is no
  `mod` operator (the table has `div` and `%` only), so `let b = 7 mod 2` does
  not fail — it binds `b = 7` and leaves `mod` and `2` as separate top-level
  content, printing an `error` value and `2`. A misspelled operator should be a
  diagnostic, not a silent three-way split; same family as F4.

- **F8 — a comprehension's result stays spreadable, so two `==`-equal arrays
  behave differently later.** A `for`-expression yields a *spreadable* array,
  and that flag survives a `let` binding and a function return. Minimal repro:
  `fn mk_comp() => for (x in [1]) x` and `fn mk_lit() => [1]` produce values for
  which `c == l` is **true**, yet `for (i in [0]) c` gives `[1]` (spliced) while
  `for (i in [0]) l` gives `[[1]]`. Equal values, identical use, different
  results — a referential-transparency break. S8.3.3 says for-expressions
  "splice at the construction site"; here the splice happens at a *later,
  unrelated* site, one function boundary away from the construction.
  A second face of the same flag: an empty comprehension returned from a
  function's **block** body yields `null`, while the identical expression as an
  arrow body yields `[]` — the empty spreadable splices away to nothing in the
  block's result position.
  Cost to the POC: every array-producing rule (`arr`, `forr`) must launder its
  result through `[*items]`, or nested arrays silently flatten and empty ones
  vanish. Both failure modes appeared, and mutation-testing confirms the
  laundering is load-bearing (removing it fails exactly 2 checks). Needs a
  ruling: either spreadability ends at the construction site as S8.3.3 reads,
  or it is a real property of values and `==` should not equate values that
  splice differently.

- **F9 — the model collapses Lambda's two error channels into one.** Lambda has
  a returned error *value* that flows as ordinary data and a raised error that
  must be handled. Natively `1 + "str"` (a real line in `test/lambda/expr.ls`)
  yields an error **value**; the model reports `'stuck'`, because every arm
  propagates with `^` and so routes it into the raised channel. Both sides agree
  the term has no ordinary value, and disagree about which channel says so. This
  is a **model limitation, not a runtime defect** — P8 puts errors-as-modelled-
  values out of POC scope — and `sem_poc_stuck.ls` pins it as a fact (comparing
  is-there-a-value rather than equality) instead of hiding it. Modelling the two
  channels honestly is the natural M4+ scope increase.

- **F10 — an identifier in element-content position followed by `<` parses as
  less-than.** `<app make_adder <lit v:10>>` does not parse ("expected a
  statement separator"), while `<app make_adder a10>` (both children names) and
  `<app <lam ...> <lit v:10>>` (literal first) both do. The ambiguity is
  resolved toward comparison, so a *variable* child may not be followed by an
  *element-literal* sibling — including across a line break. Cost to the POC:
  every argument term in `sem_poc_fixtures.ls` is bound to a name first, purely
  to get around this. Same family as F3/F4: content position has parsing rules
  that surprise, and this one bites hardest exactly where terms are assembled
  from reusable pieces — the normal way to write a model.

Disposition: F1 is a runtime defect to fix; F2 a doc fix; F3/F4 are
grammar-ruling questions to raise per `doc/Doc_Convention.md` (ask, don't
assume). All four go to the `Lambda_Semantics_Formal.md` worklist. The POC
proposal's first deliverable — this findings section — thus exists before the
POC itself is approved, which is the strongest argument that the loop works.

---

## 8. Scope ladder and milestones

Each milestone is a committed fixture set, green under
`make test-lambda-baseline`:

- **M0 — kernel fixture. LANDED** (2026-08-29) as
  `test/lambda/sem/sem_poc_core.ls` + golden: §4.3's kernel with the
  `fun`/`rclo` recursion arms (§4.4), and 11 differential checks — arithmetic,
  `let`, abstraction/application, closure capture, shadowing, both conditional
  arms, factorial vs. the native recursive `fn`, and three stuckness checks
  (unbound variable, no-rule tag, propagation out of a nested position). All 11
  return `true`. `lam`/`fun` share one `app` arm: a plain `clo` has no `f`
  attribute, so `f.f` reads null and the self-rebind is skipped.
- **M1 — functional-core breadth. LANDED** (2026-08-29), `sem_poc_core.ls`
  grown to **35 checks**, all `true`. Added `div`/`mod`/`cat`, the full
  comparison set (`gt`/`ge`/`eq`/`ne`), short-circuit `andd`/`orr` plus `nott`,
  and n-ary `lamn`/`appn`/`clon` with the parameter list in a `ps` attribute
  and arity enforced as a semantic rule. Stuckness widened: arity mismatch
  (both directions), applying a non-function, and mixing the unary and n-ary
  application forms. **Multi-binding `lett` was deliberately dropped** — the
  fold over bindings would need mutual recursion between the folder and `ev`,
  or dynamic term construction, and nested `lett` already has identical
  semantics.
  Two encoding rules keep the model stable against a pending runtime change:
  child access is always `t[i]` (children-only) and `len(t)` is read only on
  attribute-free terms, so it does not matter whether element `len` counts
  attributes (S8.3.1).
  Notable: short-circuit is **proven, not asserted** — the right operand of the
  deciding cases is an unbound variable, so a value coming back is evidence the
  model never recursed into it, and the converse cases confirm it is reached
  when the left operand does not decide.
- **M2 — collections + comprehension. LANDED** (2026-08-29), **56 checks**,
  all `true`, in `sem_poc_core.ls` (kept as one self-contained fixture rather
  than the planned `sem_poc_collect.ls`: a shared kernel module would itself be
  discovered as a test and need its own golden once `sem/` enters the list).
  Added `arr`/`idx`/`slice`/`forr`, `lenn` as a declared host builtin (the
  parent's §5.6 boundary), and `mp`/`fld` for maps.
  **Totality (closes F5):** `ev` now takes a `fuel` argument bounding recursion
  DEPTH, decremented on every arm including tail positions. Omega —
  `(\x. x x)(\x. x x)` — and a base-case-free recursive function are now
  ordinary stuck terms, and a check *after* them still reports, so divergence
  no longer erases the file. Verified by mutation: deleting the fuel guard
  returns the run to zero output lines, exactly F5's signature.
  **Maps are the one place the model cannot mirror the host.** `{(k): v}` is a
  parse error and there is no runtime map constructor, so a modelled map is an
  assoc-list element and the checks compare *field reads* (S8.4) rather than
  whole values. This is the sharpest Tier-1 requirement the POC has produced:
  a DSL over this data model needs dynamic map construction.
  Deliberately not covered: `mp` key collisions, and slicing beyond one
  inclusive-range case.
- **M3 — negative suite + real-fixture slice. LANDED** (2026-08-29).
  **126 checks across three fixtures**, all `true`, sharing one kernel:
  `sem_poc_kernel.ls` holds the interpreter as `pub fn`s and has **no golden**,
  so test discovery — which only collects `.ls` files having a matching `.txt` —
  skips it. That removes the kernel duplication the original layout implied.
  - `sem_poc_core.ls` (57) — positive differential checks, M0-M2.
  - `sem_poc_stuck.ls` (36) — the negative suite, written as a **spec artifact**
    rather than a differential one: it partitions terms into **STUCK** (no rule
    applies), **TOTAL** (a rule applies and answers null — OOB and negative
    indices, missing keys, a field of a non-map, an attribute-less `lit`; these
    are *not* errors, because Lambda's reads are total per S7.1.1v2/C15), and
    **ADOPTED** (the model has no opinion and takes the host's — a non-boolean
    `iff` guard is truthiness, not a type error).
  - `sem_poc_fixtures.ls` (33) — Phase-2 hand-translations of real programs from
    `test/lambda/closure.ls` (all seven closure tests) and `test/lambda/expr.ls`,
    checked against both those fixtures' own expectations and the runtime
    evaluating the native source.

  **The headline result is a mutation test.** Switching the model from lexical
  to dynamic scoping — capturing the caller's environment instead of the
  definition's, about as fundamental an error as this model admits — left
  **every check in `sem_poc_core.ls` still passing**, while **10 of the 33
  real-fixture translations failed immediately**. The invented checks were blind
  because their captured name happened to also be in scope at the call site;
  the real closure factories (`make_adder`, `make_nested`, `outer(2)(3)(4)`)
  are exactly the shape where it is not. This is the empirical case for §6.2:
  a model exercised only on self-authored examples is graded on what it is
  already good at. A discriminating check has since been added to
  `sem_poc_core.ls`, and the mutation now fails there too.
  Also notable: the discriminator's native side needs a factory, because
  rebinding a name in one scope is a duplicate-definition error — only the
  *term* can spell the shadowing that makes it discriminating.
- **M4 (optional) — Mark ingestion.** §6.3's `--emit-ast-mark` + `normalize`
  + first mechanically-ingested fixture. Graduation gate to the parent's
  Stage 5 machinery.

Estimated model size at M3, extrapolating from the kernel: 300–500 lines of
Lambda including checks — small enough to review as a whole.

## 9. Deliverables back to the DSL proposal

### 9.1 A costed Tier-1 requirements list

After M3, write up, with line-count and defect evidence from the model:

- how much of the model is manual extraction that §4.1 destructuring would
  delete (expected: the dominant verbosity);
- whether any bug in the model was caused by wrong manual extraction
  (the safety argument for patterns, made empirical);
- whether `for`-based child iteration ever needed real frame patterns (§4.2);
- what the element-content restrictions (F3/F4) should become as grammar
  rulings;
- whether sort-checking terms (the validator, per parent §5.1) was missed in
  practice — i.e. how often a malformed hand-written term cost debugging time.

### 9.2 The migration seed

Maintained invariant (P9): each `match` arm is a future `rule`/`judgment`
body; `ev`'s signature is a `judgment` signature; the builtin section is the
future `builtin` block. When Tier 1/2 land, migrating the POC model is a
mechanical rewrite that *diffs against a green fixture suite* — the DSL's first
user arrives with its own regression tests.

### 9.3 The §13 verdict, evidenced

The POC directly tests the parent's "ready in structure, not yet in rigor"
verdict: structure is confirmed if M0–M3 land without new runtime features
(P1); rigor gaps are enumerated as they are hit (§7 already started). Either
outcome sharpens the parent proposal; a failure outcome (some wall that plain
Lambda cannot express) would be the cheapest possible time to learn it.

## 10. Risks and open questions

1. **Meta-circular trust surface** (parent §7.3, sharpened): the model
   inherits from the host — argument evaluation order, scalar arithmetic (P5,
   A1 numeric boundaries), `==` as the check primitive (A5), element identity
   under the C4/COW aliasing rules (A7). Mitigations: targeted unit checks at
   the numeric boundaries the model relies on; keep checks first-order where
   possible; the Redex fragment as cross-host oracle for the arithmetic core.
2. **`match`-arm scaling**: one `match` over ~25 node kinds in one `fn` is
   fine; if arms need to be independently composable (Phase-3 `normalize`
   may want per-shape rules), the verified `or`-chain over error-returning
   `fn`s is the fallback — at some dispatch cost. Decide at M2.
3. **Golden discipline**: `[true, ...]` goldens are near-specification, but a
   *failing* check writes `false` into a golden only if someone blindly
   re-captures — the standing suite-hygiene risk, unchanged but worth naming:
   POC goldens must never be re-captured without triage.
4. **Where does the shared kernel live** once fixtures stop duplicating it —
   `test/lambda/` module vs. a new `semantics/` root (parent §11-Q3). Defer
   until duplication actually hurts (M2).
5. **n-ary functions and multi-binding `lett`** encoding: parameter lists as
   attribute-held arrays vs. header children — pick by probing which reads
   better under F3's content restrictions (M1).

## 11. Summary

The parent proposal's end goal — an executable, independent semantic model
that verifies the Lambda test fixtures — does not have to wait for the DSL.
Big-step + environments eliminates the machinery tiers for the functional
core; elements, `match`, closures, and the `T^E` channel are enough for the
rest, and a working kernel with differential checks — closures, capture,
recursion, stuckness — **already runs, verbatim, on today's runtime**. The POC
turns the DSL proposal's largest open risks (is Lambda meta-ready? what must
Tier 1 contain? does the four-bucket loop pay?) into a few hundred lines of
committed, CI-green Lambda.

The "does it pay?" risk is already retired. Merely *probing* for this document
— before a single line of the model was committed — surfaced a doc divergence,
two grammar sharp edges, and a `len`/iteration defect whose repair revealed
that the graph schema validator had been type-checking zero attributes on every
canonical graph, with fourteen goldens capturing the vacuous pass (§7 F1).
That is the parent's §7.1 ✓✗ bucket, found by hand, on the first afternoon.
