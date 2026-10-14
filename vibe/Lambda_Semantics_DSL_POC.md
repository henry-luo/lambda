# Lambda Semantics POC — Executable Semantics in Today's Lambda

**Status:** POC milestones M0–M3 landed 2026-08-29 (126 checks under `test/lambda/sem/`). The fixtures regressed silently under the S2.5 list rulings and a JIT defect, and were repaired 2026-10-13 (F11, F12): 127 checks, all `true` on the interp, jit and auto tiers. **In the baseline since 2026-10-14** (`test/lambda/sem` added to `FUNCTIONAL_TEST_DIRECTORIES`; the kernel renamed `mod_sem_poc_kernel.ls` per the harness's module-naming rule — §5). **M4 landed 2026-10-14**: `--emit-ast-dump` now prints Mark, `mod_sem_poc_ingest.ls` lowers it to terms, and `sem_poc_ingest.ls` grades the model on `test/lambda/closure.ls` with no hand-written term (§6.3, §8). **Stage 5 landed 2026-10-14**: `make semantics-verify` runs the model over all of `test/lambda` (26 fixtures verified, 439 pending with reasons) and gates the manifest's verified set in `make test-lambda-full` (§6.4, §13). The DSL of Part II is not started. On 2026-10-13 this doc absorbed the former `Lambda_Semantics_DSL_Proposal.md` (draft, 2026-07-03) as Part II, and that file was deleted.
**Scope:** Part I — the **functional core of Lambda only**: no procedural (`pn`) subset, no new syntax, no new runtime machinery. Part II — the native semantics DSL the POC seeds, for Lambda and LambdaJS.
**End goal:** an executable, independent semantic model that *verifies* the Lambda test fixtures under `test/lambda/`, breaking the circularity of goldens captured from the very runtime they test (§1.1).
**Spec linkage:** no `S#`/`D#` ruling covers meta-circular modeling, so the P-ledger (§1.5) holds working-doc decisions. By construction the model checks SI1 (boxing invisibility) and SI3v2 (inference evaluation-invariance); it relies on S4.1/SI7 (int arithmetic), SI4 (equality laws) and SI13 (no aliasing) in its host (§11.2).
**Thesis:** a big-step definitional interpreter for Lambda's functional core can be written **in plain, already-shipping Lambda** — elements as terms, `match` on tags as rule dispatch, the `T^E` channel as stuckness — and verified differentially against the runtime with **zero host-side code**. Every construct this document relies on was executed on the current `lambda.exe` before being written down (§3, §4).

---

# Part I — The POC

## 1. Motivation and approach

### 1.1 The oracle problem

The fixture pairs under `test/lambda/` (`*.ls` script + `*.txt` golden) are the
backbone of `make test-lambda-baseline`. But the `.txt` goldens are **captured from the
Lambda runtime's own output**. If the implementation has a semantic bug, the golden
captures the bug, the test passes, and the bug becomes load-bearing. The suite detects
*regressions*, not *incorrectness*. The per-test `// expect:` comments in the `.ls`
files are hand-written and closer to a specification, but they are informal, unchecked
prose.

What is missing is an **independent oracle**: a definition of what Lambda programs
*should* evaluate to, derived from stated rules rather than from the implementation.
The stated rules now exist as prose in `doc/Lambda_Formal_Semantics.md`; the model is
meant to be their executable form. LambdaJS has the same problem — its Node-compat
baseline is measured against Node's observed behavior, with no account of *why* an
output is right — which is why Part II keeps one rule language for both.

### 1.2 Prior art

- **K Framework** (Roșu et al.) — configurations as nested cells, semantics as rewrite
  rules over configurations, `strict` annotations that auto-generate evaluation-order
  rules. Proven at full scale: KJS passed the ECMAScript 5.1 conformance core
  (PLDI 2015).
- **Ott** (Sewell et al.) — grammar declarations with *binding specifications*,
  inductive judgments as inference rules, generation of LaTeX and proof-assistant
  definitions.
- **PLT Redex** — lightweight executable semantics embedded in a host language, with
  randomized testing against real implementations.
- **In-house: the Lambda Redex model (removed).** A PLT Redex big-step model of
  Lambda's functional core, procedural extension and object types (~8k lines of
  Racket under `lambda/semantics/`), with an `--emit-sexpr` bridge from the production
  parser, a `make test-redex-baseline` harness that reached 87/194 fixture agreement,
  and a designer-ruling protocol for disagreements. Its disagreement log (`list` vs
  `array` under `is`, `fill()` coercion into typed arrays, `type(error_value)`) was
  exactly the "suspect golden / underspecified semantics" bucket of §13.1. It
  established three things this doc builds on: the verification loop works
  end-to-end; big-step is sufficient for the functional core (§1.4); and the
  `--emit-sexpr` mapping is the precedent for AST ingestion (§6.3). Its doc was
  renamed legacy in `04f740440d` and the model removed in `8a1ca96825`; both remain in
  git history. With it went the only cross-host oracle (§13.3).

The design is closest in spirit to Redex ("embedded, executable, test-focused"); Part II
grafts on K's configuration/strictness machinery and Ott's binder specs.

### 1.3 Drive the terrain before building the vehicle

Part II designs a K/Ott-class semantics sub-language: destructuring
patterns, `syntax`/`config`/`rule`/`judgment` declaration forms, strictness
expansion, the k-cell discipline — three tiers of additions (§12.2–§12.4) before
the first Lambda model can be written, and a C++ matcher/rewriter engine
underneath.

The POC inverts the order. Before building the vehicle, drive the terrain:
write the Lambda model **now**, in the language as it exists, and let the
experience produce (a) an early version of the §13 fixture-verification payoff,
(b) a live inventory of exactly which missing features hurt and how much — data
the Tier 1/2/3 designs otherwise lack, and (c) hard evidence for (or against)
the §11 claim that Lambda is ready to be its own meta-language.

The POC is not a rival design. It is Stage 4 of the DSL plan (the Lambda
model, functional core; §15.2) pulled forward to Stage 0, at the cost of
verbosity that the DSL would later remove. Every hand-written `fn` arm in the
POC model corresponds 1:1 to a future `rule`/`judgment` declaration, so the
model is a migration **seed**, not throwaway code (§9).

### 1.4 The one big simplification: big-step, environment-based

Two structural choices eliminate the DSL's entire Tier 2 and Tier 3, and most
of Tier 1, for this scope:

- **Big-step instead of small-step.** Evaluation order is encoded by the
  meta-language's own recursion (`ev(t[0]) + ev(t[1])` *is* left-to-right
  strictness). No `strict` annotations, no heating/cooling, no k-cell, no
  configuration cells, no `config` form. This is exactly the shape of the
  former Redex model (§1.2), which reached 87/194 fixture agreement in
  precisely this style — big-step is *proven sufficient* for the functional
  core.
- **Environments instead of substitution.** Closures capture their defining
  environment as data; variables are looked up, never substituted. No
  capture-avoiding `subst`, no `alpha_eq`, no `binds` specs, no `fresh()` —
  §12.2.3 and the binder half of §12.3.1 are simply not needed.
  Recursion needs no mutation or cycles either: a named function value re-binds
  *itself* into the environment at each application (§4.4 — verified with
  factorial).

What big-step gives up — modeling divergence distinctly from stuckness, and the
per-step traces the §13.1 mismatch reports want — is an acceptable POC
trade: fixtures terminate, and mismatch localization falls back to per-check
granularity (§6) — once divergence is handled. It initially was not: a wrong
rule that made a modelled program *diverge* printed nothing at all, erasing
every other result in the file (finding **F5**, §7). M2 closed this with a
`fuel` argument bounding recursion depth, which turns divergence into ordinary
stuckness and restores per-check localization. The model is therefore **total**:
every term either yields a value or is stuck, and omega is just another check.

### 1.5 Decision ledger

For citability (per `doc/Doc_Convention.md` conventions; no `S#`/`D#` ruling
covers meta-circular modeling, so these are working-doc decisions):

| ID | Decision |
|----|----------|
| P1 | POC uses **zero new syntax and zero new C/C++ code**; optional Phase-3 ingestion (§6.3) is the only host-side item, and it is severable. |
| P2 | Big-step, environment-based, closures-as-data. No small-step, no substitution, no fresh names. |
| P3 | Terms are elements: tag = node kind, attributes = atomic metadata (names, literal values), children = sub-terms. Environments and closures are elements too (§4.2). |
| P4 | Stuckness = raised error on the `T^` channel; rule dispatch = `match` on `name(t)`; where fall-through choice is needed, errors-are-falsy `or` chaining (verified §3). |
| P5 | Model scalar arithmetic **uses Lambda's own numerics** — §16 Q5 resolved pragmatically for the POC, with the numeric-boundary caveat recorded in §10. |
| P6 | Verification is **self-differential in-script**: each check compares `ev(term)` against the runtime evaluating the same expression natively, in the same fixture; goldens are arrays of `true` (§6.1). |
| P7 | POC fixtures are ordinary `.ls`+`.txt` pairs under `test/lambda/sem/`, needing no new harness — but that directory is **deliberately not yet in the baseline's discovery list** (§5), so a model still being shaped cannot dictate what the runtime must do. |
| P8 | Scope ceiling: functional core (literals, arithmetic/comparison/logical, `let`, `if`, closures/application, recursion, arrays/maps/for basics). Errors-as-modeled-values, `pn`, modules, system functions beyond a tiny builtin list: out (§8, §9). |
| P9 | The POC model is the migration seed for the DSL's Stage 4 (§15.2); arm ↔ rule correspondence is maintained deliberately (§9.2). |
| P10 | Meta-fragment findings made while building the POC are recorded in §7 and fed to the `Lambda_Semantics_Formal.md` worklist — the §11.3 hardening loop, started early. |
| P11 | **Ingestion (M4) reads the production parser's own AST dump**, which `--emit-ast-dump` prints as Mark: `(X …)` → `<X …>` for nodes, fields and attributes alike (designer ruling 2026-10-14). The dump of a fixture is committed beside the model (`closure.ast.mark`, regeneration command in its header), `normalize` reads shapes only and never the inference annotations, and anything outside the core lowers to `<unsupported>` — stuck by the kernel's default arm. |

---

## 2. Construct-by-construct substitution map

What the DSL (Part II) adds vs. what the POC uses instead — all right-column
items exist and were executed today:

| DSL construct | POC substitute (shipping Lambda) |
|---|---|
| §12.2.1 destructuring `match` | `match name(t) { case 'add': ... }` + manual extraction `t[0]`, `t.n` |
| §12.2.2 rest/frame patterns | `for (c in el)` child iteration; list-variable splicing into element content |
| §12.2.3 `fresh()` | not needed (environment-based, P2) |
| §12.2.4 source ingestion | hand-encoded terms (Phases 1–2); optional Mark AST dump (Phase 3, §6.3) |
| §12.3.1 `syntax` sorts + binders | naming convention (§4.1) now; ordinary `type` decls later if wanted; no binders needed |
| §12.3.2 `config` cells | none — big-step needs no configuration |
| §12.3.3 `rule` + `-->` | one `match` arm per node kind inside a judgment `fn` |
| §12.3.4 `judgment` | plain recursive `fn ev(t, env) any^` — exactly what §12.3.4 says judgments compile to anyway |
| §12.3.5 strategy library | `or` over error-returning `fn`s where needed (verified); mostly unnecessary in big-step |
| §12.3.6 `builtin` | a named `fn` per trusted primitive, listed in one place in the model file |
| §12.4 strictness/k-cell/store | none — out of scope with big-step + functional-only |
| §13.1 verification pipeline | in-script self-differential checks; four-bucket logic at fixture granularity (§6) |

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
| `<add a b>` `<sub a b>` `<mul a b>` `<div a b>` `<idiv a b>` `<neg a>` `<pos a>` ... | arithmetic (`idiv` is `div`, in its operand domain — S4.5.3) |
| `<lt a b>` `<le a b>` `<eq a b>` ... | comparison |
| `<andd a b>` `<orr a b>` `<nott a>` | logical, short-circuit; the value is the deciding operand (S3.3v2 for `or`; `and` mirrors it — F15) |
| `<iff c t e>` | conditional |
| `<lett n:'x', e body>` | let-binding |
| `<lam p:'x', body>` | anonymous function |
| `<fun f:'f', p:'x', body>` | named (recursive) function |
| `<app f a>` | application (single-param MVP; n-ary later) |
| `<arr e*>` / `<idx a i>` | array literal / indexing |
| `<mp (<kv k:'k', e>)*>` / `<fld m k>` | map literal / field access |
| `<forr n:'x', src body>` | for-comprehension |
| `<funn f:'f', ps:['x','y'], body>` | named n-ary function (M4; `rclon` value, applied by `appn`) |
| `<cont e*>` | a content block's value items, normalized as content (M4, Stage 5; S2.5.3, S2.6.2–S2.6.4, collapses per S2.5.5v2); a trailing `lett` child carries the block's later declarations |
| `<sysf n:"len">` / `<unsupported kind:…>` | a system function by name (only `len` maps, to `lenn`); a construct outside the core — stuck |

### 4.2 Values, environments, closures

- **Scalar model values are host values** (P5): the model's `3` is Lambda's
  `3`. This is the §16 Q5 circularity, accepted deliberately: the
  independence argument (§13.3) rests on *different failure modes* —
  a rule that walks `<add a b>` and a JIT lane-inference bug have no shared
  mechanism — not on disjoint arithmetic. The numeric-boundary caveat is
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
    let hits = [for (b in env where b.n == n) b.v]
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

- **`ev` is the judgment** — `fn ev(t, env) any^` is literally what
  §12.3.4 says moded judgments compile to. The `^` on every recursive
  call is stuckness propagation; the arm-per-kind `match` is the rule set.
- **Stuckness is total and typed.** Anything the model does not cover raises;
  raises reach the check as the `'stuck'` sentinel via the `^ { }` handler.
  There is no way for an unmodeled construct to silently produce a value —
  the property the §13.1 four-bucket triage depends on.
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
§12.3.6 auditability ("what does this model trust?" = read that
section). No declaration form needed at this scale.

---

## 5. Files and layout

```
test/lambda/sem/mod_sem_poc_kernel.ls  // the interpreter, `pub fn`s — a module (mod_ prefix), no .txt
test/lambda/sem/sem_poc_core.ls      // positive differential checks, M0-M2      (58)  [LANDED]
test/lambda/sem/sem_poc_stuck.ls     // negative suite: STUCK / TOTAL / ADOPTED  (36)  [LANDED]
test/lambda/sem/sem_poc_fixtures.ls  // real test/lambda programs, translated    (33)  [LANDED]
test/lambda/sem/mod_sem_poc_ingest.ls  // M4: the AST dump (Mark) lowered to terms — a module, no .txt
test/lambda/sem/closure.ast.mark       // M4: committed `--emit-ast-dump test/lambda/closure.ls`
test/lambda/sem/sem_poc_ingest.ls      // M4: closure.ls graded with no hand-written term   (4)  [LANDED]
test/lambda/sem/mod_sem_verify.ls      // Stage 5: the harness's model side (census, stuckness, `// expect:`) — a module, no .txt
test/lambda/sem/semantics_verify.py    // Stage 5: `make semantics-verify` — dump, lower, run, four buckets, manifest gate
test/lambda/sem/semantics-manifest.json // Stage 5: verified set (the gate), pending census, exclusions — written by --update
```

Every `.ls` in a golden-driven directory is a test and needs its golden
(AGENTS.md rule 8); an imported module is named `mod_*` so discovery skips
it. The kernel carries that prefix, and the three fixtures share one copy of
the interpreter — no duplication, and no harness change to arrange it.

Per P7 these are ordinary `.ls`+`.txt` fixtures under `test/lambda/sem/`, so
AGENTS.md rule 8 supplies the golden discipline and no harness, Makefile
target, or manifest is added for Phases 1–2. The kernel is duplicated per
fixture file at first (fixtures are self-contained); if that grates, module
import of a shared `mod_sem_poc_kernel.ls` is an ordinary Lambda `import` —
a later cleanup, not a requirement.

**In the baseline** (designer call, 2026-10-14): `test/lambda/sem` is listed in
`FUNCTIONAL_TEST_DIRECTORIES` in `test/test_lambda_gtest.cpp`, so
`make test-lambda-baseline` runs the three fixtures on the auto tier. It was
deliberately kept out from 2026-08-29 while the model was being shaped, so a
half-written model could not dictate what the runtime must do; that hold also
let the fixtures sit silently red for six weeks (F11), which is why M3's
completion closed it.

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

This is the §13.1 three-way comparison collapsed to two ways at
per-expression granularity, with the golden fixed at `true`:

- `cN == true` — model and runtime agree (§13.1 bucket ✓✓, **verified**);
- `cN == false` — they disagree: either a model bug or a **runtime bug**
  (bucket ✓✗ — the motivating case, now visible per-expression);
- `cN == 'stuck'`-shaped mismatches localize under-coverage instantly.

The independence argument is §13.3's unchanged: the native side runs
through parser → AST → MIR JIT → tagged values; the model side walks elements
and applies rules. A truncation bug in lane inference and a wrong side
condition in an arm have no mechanism for coinciding — and the historical bug
inventory (pn-param float-div inference, JS→MIR numeric inference, boxing
observability leaks) is exactly the class the native side would get wrong and
the model side would not. Both sides sharing one process and one parse is the
known, accepted weakness (§10). A cross-host oracle is the escape hatch
(§13.3); none is maintained since the Redex model was removed (§1.2).

### 6.2 Phase 2 — hand-translated real fixtures

Pick a slice of the real suite whose semantics the model covers — candidates
from the §13.2 tier 1–2 ordering: `numeric_expr.ls`, `chained_comparisons.ls`,
`closure*.ls` (simplest members first) — and hand-translate each script into a
term, asserting model result == native result of the *actual fixture
expressions*. Hand-translation is honest POC labor: it is bounded (a dozen
fixtures suffice to exercise the loop), it needs no host code, and every
translated fixture becomes a regression test for Phase 3's mechanical
translation.

### 6.3 Phase 3 — Mark AST ingestion (M4, landed 2026-10-14)

The production C parser's canonical AST dump (`lambda.exe --emit-ast-dump
f.ls`, `lambda/runtime/emit_ast_dump.cpp`) **is now Mark** — Lambda's own data
notation — so `input(f, 'mark')` reads it back as elements with no new parser.
The change was the delimiters alone (P11): `(AST_NODE_PRIMARY (expr
(AST_NODE_IDENT (name "x"))))` became `<AST_NODE_PRIMARY <expr <AST_NODE_IDENT
<name "x">>>>`, for nodes, fields and attributes alike; the header's language
is a string (`<ast-dump "lambda" …>`), the boolean flags are bare booleans
(`<value_may_error false>`, since F14's fix on 2026-10-14 the reader keeps
them as S2.6.2 requires), and a decomposition's names are one element each
because adjacent strings in content merge (S2.6.4). The JS dump followed, since the two
share their attribute primitives. The format's consumers moved with it (the
`TypeContractMetadataTest` expectations and `test/type_infer/refresh_census.sh`),
and every script under `test/lambda` dumps and reads back (481 of 481).

```lambda
let dump = input("test/lambda/sem/closure.ast.mark", 'mark')  // the fixture's dump, as elements
let term = normalize(dump)^      // dump shapes → §4.1 terms, written in Lambda (mod_sem_poc_ingest.ls)
run(term) == input("test/lambda/closure.txt", 'mark')          // the model vs the fixture's golden
```

`normalize` is a Lambda function over elements that knows shapes, never
semantics: `AST_NODE_IDENT` → `vr`, `BINARY op` → `add`/…, `CALL_EXPR` → `appn`,
`FUNC`/`FUNC_EXPR` → `funn`/`lamn`, `IF_EXPR` → `iff`, `ARRAY`/`INDEX`/`MAP`/
`MEMBER`/a plain `FOR_EXPR` → `arr`/`idx`/`mp`/`fld`/`forr`, a `PRIMARY`'s
literal source text read back through `parse(src, 'mark')`; a content block's
leading declarations become nested `lett` wrappers (a named `fn` binds its own
`funn` value) and its value items a `cont` term, which the kernel evaluates as
the list the block builds (S2.5.3, collapsing per S2.5.5v2). Inference
annotations are never read. Anything else — a `match`, a `pn`, a pipe — lowers
to `<unsupported>`, which the kernel's default arm reports as stuck: the
stuck-by-default property survives ingestion, and `sem_poc_ingest.ls` pins
that with a hand-built dump of a `match`. The dump of a fixture is committed
beside the model (its header carries the regeneration command) rather than
generated at test time, so the fixture depends on no tooling; drift shows up
as the lowering breaking. This is the direct descendant of the Redex bridge's
`--emit-sexpr` mapping (§1.2); the §13.1–§13.2 harness built on it is §6.4.
Since Stage 5 the lowering also folds a declaration that *follows* value
items (the `cont`'s last child is that declaration's `lett`, whose list
value splices, S2.6.3) and a multi-declarator `let` (one `lett` per
declarator, in order).

### 6.4 Phase 4 — the harness over the whole suite (Stage 5, landed 2026-10-14)

`make semantics-verify` (`test/lambda/sem/semantics_verify.py`) runs the
§13.1 pipeline over every fixture pair in `test/lambda` (465 today). Each
fixture is dumped with `--emit-ast-dump` at verification time (the M4 dump
stays the one committed), lowered by `normalize`, and evaluated by the kernel
with a deeper fuel. The model's value is the top-level value of a generated
script under `temp/sem/`, so the **same printer** that produced the golden
prints it (S16.7.1) and the two texts diff like for like; `lambda.exe` runs
the fixture on the baseline's tier for the runtime axis. A procedural
companion script prints the lowering census, the model's stuckness and the
`// expect:` results as JSON (`mod_sem_verify.ls`), so the driver needs no
Mark reader of its own.

A fixture whose lowering leaves the core, whose run is stuck, or whose value
holds a modelled map (`mapv` — the assoc list cannot print as the host's map
does, §6.2) is *pending* with its reason; otherwise the two axes place it in
the §13.1 table. `// expect: V` comments are checked per binding (§13.1 item
2): the lowered program is re-run with its value items replaced by one read of
the annotated name, placed after the block's declarations, and compared with
the comment's leading Mark value (trailing prose is dropped a word at a time,
so `125 (15 + 110)` is 125). `semantics-manifest.json` is the gate: its
verified set must stay verified (exit 1 otherwise); `--update` grows it from
one sweep and refreshes the pending census; exclusions are hand-written with
a reason. It is the last step of `make test-lambda-full`.

First sweep (2026-10-14): **26 verified, 0 suspect, 439 pending** (5 stuck),
8 s with 8 jobs. The first suspects were real: `comp_expr` exposed F15 (the
model's `and`/`or` returned booleans), and four others were the model's maps
reaching the output. Pending census leaders: `is` (96 fixtures), `type(…)`
(78), type statements (53), elements (49), imports (48), ranges `to` (43),
pipes (40), `for` clauses beyond one plain loop (38), `sum` (37), handlers
(33) — the §13.2 tier order, measured.

---

## 7. Findings already produced (the loop is already paying)

Probing for this document — before any model existed — surfaced four
meta-fragment findings in under an hour, confirming the §11.3 premise
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
  `lambda/graph/` produces) has *only* a nameless slot, `item_keys`
  returned the **empty list** for it. The graph schema validator walks
  `for (key, attr_value in map(value))` to type-check attributes, so for every
  canonical graph it iterated nothing and reported `valid: true` having checked
  **zero attributes**. Fourteen `graphviz_*` fixtures had captured that vacuous
  pass as their golden. Turning iteration on immediately exposed a second,
  independent latent bug in `lambda/graph/schema.ls`
  (`present_attr_diagnostics` type-checked null-valued — i.e. absent —
  attributes, contradicting its own `required_attr_missing`); with both fixed
  the baseline is 3982/3982.

  This is the §13.1 four-bucket triage playing out for real, and the
  outcome is the one the bucket table predicts as most valuable: goldens in the
  ✓✗ cell that had silently enshrined *two* stacked defects, one of which had
  disabled a whole validator.
- **F2 — doc drift on the element attribute/content separator. FIXED.**
  `doc/Lambda_Data.md` showed `<div class: "main"; "content">`; the parser
  rejects `;` with *"expected ',' between element attributes and content"*.
  The docs predated the separator change; they now show `,`. (Same class as the
  §11.2 `'a' == "a"` finding: user-guide-grade docs diverging from the
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
  distinguish it from stuckness (§1.4), and the crash is whole-file. The
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
  **RULED since** (the second option): a list is a specialized array whose
  spread bit is a property of the value — it splices wherever it sits as an
  item (S2.5.1v2) and passes through a binding, argument or return unchanged
  (S2.5.6) — while `==` compares the sequence family by content (S5.3.1). A
  one-item list *is* its item and an empty one is `null` (S2.5.5v2), so
  `mk_comp()` above is now just `1`. The laundering is retired: `arr` builds
  with `[for …]` and `forr` returns the host's list (F11).

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

- **F11 — the fixtures went silently red, for three reasons (repaired
  2026-10-13).** Run on 2026-10-13, 46 of 57 core checks, 12 of 36 negative
  checks and all 33 real-fixture checks had turned `false`. `test/lambda/sem`
  is outside the baseline (§5), so nothing reported it. Two causes were
  ratified language changes, and the kernel was updated to them:
  - **A guard statement leaks a `null` into its block's value.** A block is the
    list of *all* its statements' results (S2.5.3), and an else-less `if`
    yields `null` in value position (S16.6.3); a list keeps its nulls
    (S2.5.1v2). So `ev`'s body `{ if (fuel <= 0) raise …  match … }` returned
    `(null, v)`, and nested calls piled the nulls up until `+` broadcast over
    lists of different lengths. The guards (fuel, and the `appn` arity check)
    are now `if … else`. Every pure-function guard written in this style has
    the same trap.
  - **A one-item comprehension is its item.** `let hits = for (…) b.v` then
    `len(hits)` read `len(3)` = 0 for a single hit (S2.5.5v2, S8.3.1v3), so
    every variable looked unbound. `lookup`, `fld` and `appn` now wrap with
    `[for …]`, the remedy S2.5.5v2 names. `forr` drops its array laundering and
    answers the host's list, so the one core check that expected `[]` from an
    empty comprehension now compares with the native `for` instead (both are
    `null` as values).
  - **A runtime defect: the JIT dropped a `match` value beside a raising arm.**
    With the kernel fixed, the interp tier went green but the jit tier still
    failed 42 core checks. `transpile_match` (`transpile-mir.cpp`) never reset
    the transpiler's "branch exited" flag per arm, so a `raise` in the default
    arm leaked out of the `match`, and the enclosing `if`'s join treated the
    whole `else` branch as exited and stored `0` (null) as its value.
    Minimal repro: `fn p(n) any^ => if (n <= 0) 0 else match n { case 1: 10
    default: raise error("s") }` gave `p(1)` = `null` on jit, `10` on interp.
    Fixed with the join rule the `^` handler already used (the join is
    reachable unless every arm exits and a default arm closes the no-match
    path). Pinned on all three tiers by `test/lambda/match_raise_arm_join.ls`
    in the tier-parity table. It hid in the baseline because the auto tier
    interprets a once-called body. This is the §13.1 ✓✗ payoff again: an
    executable model caught a tier divergence no golden had.
  - **Two more runtime defects fell out of the follow-up sweep** (every
    golden on the jit tier, same day). (a) The transpiler's tail-position
    flag leaked into constructors: `{*:f(x), type: t}` in tail position
    jumped into `f` and returned *its* map, dropping `type` — the math
    typesetter's `\mathrel{=}` came back as `mord` on the JIT. Tail position
    now passes only through forms whose value *is* one child's value
    (`transpile_expr_value_core`); pinned by
    `test/lambda/tail_position_constructors.ls` in the tier-parity table. (b) `list_push`'s S2.6.4 merge of
    adjacent content strings took the script's eval context for the owner
    whenever `parse()` ran from a script, so the Markdown parser's merged text
    runs were heap strings inside an arena-owned element nothing traces, swept
    at the next collection (`" and "` read back as `"binary"`, or as a
    neighbouring `" and "`). An active input parse now owns its merged text
    (D4.2.6 Input ownership); pinned by `test/mir/lambda/gc_parse_merged_text.ls`
    under the forced-collection stress sweep. Debugging note: a probe that
    *reads* the parsed text before the churn stores the pointers in a traced
    array and keeps the strings alive — several early "the tree survives"
    experiments were masked exactly this way.

- **F13 — `if (c) <e>` reads `<` as less-than.** `if (i >= len(items))
  <cont> else {…}` parsed as `(…) < cont > else …`: the JIT reported
  "undefined variable 'cont'", the interpreter returned `null`. An element
  literal can open an *expression* only where no complete expression precedes
  it, so after `if (…)` it needs parentheses, a block, or a `match` arm. Same
  family as F10 (content position), now in expression position; the parser's
  `<` disambiguation deserves a ruling. Found writing `normalize` (M4).

- **F14 — the Mark reader dropped a bool or symbol child silently (fixed
  2026-10-14).** `input` of `<r <a true> <b 'sym'> <c 1.5>>` kept only `c`'s
  child, while the same literal in Lambda source keeps all three and an
  attribute `v:true` read back as a bool. The loss was not in the content
  push: the element parser's attribute loop *consumed* a name token (bare
  word or `'symbol'`) as a candidate attribute name and, finding no `:`,
  broke to the content loop without restoring the cursor — only `"strings"`
  had a non-consuming lookahead. The fix scans the name token without
  consuming it (`mark_scan_name_token`, `input-mark.cpp`). It exposed a
  second defect in the same path: `parse_value` matched keywords by prefix,
  so `trueish` read as `true` + `'ish'`; keywords now end at a name boundary.
  S2.6.2 drops only `null` and `""`. Fixture `test/input/element_content.mark`
  with `test/lambda/mark_element_content.ls`; the AST dump now spells its
  boolean flags bare (P11).

- **F12 — a bound list became an array in the model.** An attribute stores a
  list as the array it is (S2.5.6), so a list bound by `lett` or a parameter
  and held in the environment's `v:` attribute came back as an array. It then
  nested where the native binding splices:
  `(let y = for (x in [1, 2]) x, [y, 3])` is `[1, 2, 3]`, but the model gave
  `[[1, 2], 3]`. This only became visible once `forr` returned lists (F11). A
  binding now records list-ness (`l:`) and `lookup` rebuilds the list. A new
  core check pins it.

Disposition: F1 is a runtime defect to fix; F2 a doc fix; F3/F4 are
grammar-ruling questions to raise per `doc/Doc_Convention.md` (ask, don't
assume). All four go to the `Lambda_Semantics_Formal.md` worklist. The POC
proposal's first deliverable — this findings section — thus exists before the
POC itself is approved, which is the strongest argument that the loop works.

---

- **F15 — the model's `and`/`or` returned booleans; the language returns
  the deciding operand. FIXED 2026-10-14.** `(5 or false)` is `5` and
  `(0 or false)` is `0` natively — `or` is the coalescing operator, truthy-0
  included (S3.3v2, S3.1) — while the kernel answered `true`. Now `a or b` is
  `a` when truthy and `b` otherwise, and `a and b` is `b` when `a` is truthy
  and `a` otherwise: the runtime's reading, which no `S#` states for `and`
  (only S3.3v2 for `or`) — a candidate ruling. Found by the Stage 5 harness
  on `comp_expr.ls` in its first sweep: the ✓✗ bucket resolving to a model fix.
- **F16 — duplicate map keys: the runtime keeps the later entry, the ruling
  calls it a bug.** `test/lambda/map_duplicate_key_lookup.ls` pins "the
  later entry wins" for `{a: 1, a: 2}` (and `len` counts both, printing
  both), while the 2026-08-28 designer ruling under F1 says a duplicate key is
  a bug, not a case to accommodate; no `S#` covers it. The model's assoc list
  reads the first entry. Hidden today behind `unsupported: map spread`; it
  resurfaces as a suspect once spread is modelled — a ruling is needed before
  the model takes a side.
- **F17 — the AST dump does not record parameter optionality or variadics.**
  `fn f(a?, b?)` and `fn g(a, ...)` dump as plain params, so the lowering
  cannot mark them unsupported and the model reports `arity mismatch`
  stuckness instead (`func_param`, `box_unbox_negative`,
  `default_callback_frame`). Emitter follow-up: `<optional>` / `<variadic>`
  attributes; deferred while the emitter is being edited for F14.
- **F18 — forward references between functions need a recursive group.**
  `forward_ref.ls` calls `second()` from a `first()` declared above it; the
  sequential `lett` chain binds names in source order, so the call is
  `unbound variable`. The model needs the §4.4 group binding (a `rclon`
  carrying its sibling declarations) — Stage 6.
- **F19 — a host error value is stuckness to the model.** `numeric_expr.ls`
  prints `error` items for `true % 2`; the kernel propagates every error
  through `^`, so the whole run is stuck. Whether an operator's error result
  is a *value* the model carries (S3.1 lists it as falsy; S2.5.5v2 reads it as
  no result) or stuckness is the tier-6 error-handling decision (§13.2).

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
  §12.3.6 boundary), and `mp`/`fld` for maps.
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
  a DSL over this data model needs dynamic map construction (§12.2.5).
  Deliberately not covered: `mp` key collisions, and slicing beyond one
  inclusive-range case.
- **M3 — negative suite + real-fixture slice. LANDED** (2026-08-29).
  **126 checks across three fixtures**, all `true`, sharing one kernel:
  the kernel module (now `mod_sem_poc_kernel.ls`) holds the interpreter as
  `pub fn`s and has **no golden**, so test discovery skips it. That removes the
  kernel duplication the original layout implied.
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
- **M4 — Mark ingestion. LANDED** (2026-10-14). The AST dump prints Mark
  (P11, §6.3); `mod_sem_poc_ingest.ls` (≈200 lines of Lambda) lowers it to
  terms; `sem_poc_ingest.ls` grades the model on `test/lambda/closure.ls` with
  **no hand-written term**: all ten closure results equal the fixture's golden,
  nothing in the program fell outside the core, and a dump of a `match`
  lowers to `unsupported` and is stuck. Two kernel additions were needed, both
  foreseen: `funn` (a named n-ary function, applied by `appn`, which now
  re-binds a `rclon`'s self-name as `app` does) and `cont` (a block's value
  items). What the lowering could *not* express is itself the finding: a
  declaration after a value item in one block (the fold would need a `cont`
  that splices), multi-declarator `let`s, and the remaining system functions
  — each raises an explicit "unsupported" rather than guessing. Graduation
  gate to the DSL's Stage 5 machinery (§15.2) is now open.
- **M5 — Stage 5 harness. LANDED** (2026-10-14, §6.4). `make semantics-verify`
  over `test/lambda`: 26 fixtures verified by the model, none suspect after
  the first triage, 439 pending with reasons; the manifest gates the verified
  set in `make test-lambda-full`. The lowering grew to blocks with
  declarations after value items and multi-declarator `let`s; the kernel
  gained `idiv`, `pos`, content normalization in `cont` (S2.6.2–S2.6.4) and
  operand-valued `and`/`or` (F15). Next: the census leaders, in §13.2 order,
  and F16–F19.

Estimated model size at M3, extrapolating from the kernel: 300–500 lines of
Lambda including checks — small enough to review as a whole.

## 9. Deliverables to the DSL design (Part II)

### 9.1 A costed Tier-1 requirements list

After M3, write up, with line-count and defect evidence from the model:

- how much of the model is manual extraction that §12.2.1 destructuring would
  delete (expected: the dominant verbosity);
- whether any bug in the model was caused by wrong manual extraction
  (the safety argument for patterns, made empirical);
- whether `for`-based child iteration ever needed real frame patterns (§12.2.2);
- what the element-content restrictions (F3/F4/F10) should become as grammar
  rulings (§12.2.6);
- whether sort-checking terms (the validator, per §12.3.1) was missed in
  practice — i.e. how often a malformed hand-written term cost debugging time.

### 9.2 The migration seed

Maintained invariant (P9): each `match` arm is a future `rule`/`judgment`
body; `ev`'s signature is a `judgment` signature; the builtin section is the
future `builtin` block. When Tier 1/2 land, migrating the POC model is a
mechanical rewrite that *diffs against a green fixture suite* — the DSL's first
user arrives with its own regression tests.

### 9.3 The §11.2 verdict, evidenced

The POC directly tests §11.2's "ready in structure, not yet in rigor"
verdict: structure is confirmed if M0–M3 land without new runtime features
(P1); rigor gaps are enumerated as they are hit (§7 already started). Either
outcome sharpens the DSL design; a failure outcome (some wall that plain
Lambda cannot express) would be the cheapest possible time to learn it.

## 10. Risks and open questions

1. **Meta-circular trust surface** (§13.3, sharpened): the model
   inherits from the host — argument evaluation order, scalar arithmetic (P5;
   the numeric boundaries, now ruled by S4.1/SI7 and S4.1.5), `==` as the check
   primitive (SI4), element identity under the no-aliasing/COW rules (SI13,
   SI2). Mitigations: targeted unit checks at the numeric boundaries the model
   relies on; keep checks first-order where possible; a cross-host oracle for
   the arithmetic core (§13.3) — none is maintained since the Redex model was
   removed (§1.2).
2. **`match`-arm scaling**: one `match` over ~25 node kinds in one `fn` is
   fine; if arms need to be independently composable (Phase-3 `normalize`
   may want per-shape rules), the verified `or`-chain over error-returning
   `fn`s is the fallback — at some dispatch cost. Decide at M2.
3. **Golden discipline**: `[true, ...]` goldens are near-specification, but a
   *failing* check writes `false` into a golden only if someone blindly
   re-captures — the standing suite-hygiene risk, unchanged but worth naming:
   POC goldens must never be re-captured without triage.
4. **Where does the shared kernel live** once fixtures stop duplicating it —
   `test/lambda/` module vs. a new `semantics/` root (§16 Q3). Defer
   until duplication actually hurts (M2). Interim answer since M3: the
   golden-less `test/lambda/sem/mod_sem_poc_kernel.ls` (§5).
5. **n-ary functions and multi-binding `lett`** encoding: parameter lists as
   attribute-held arrays vs. header children — pick by probing which reads
   better under F3's content restrictions (M1).

---

# Part II — The Native Semantics DSL

Part II is the long-range design the POC seeds, condensed from the 2026-07-03
proposal. **Nothing in it is implemented.** Where the POC has since produced
evidence, it is noted inline.

## 11. Why Lambda can be its own meta-language

### 11.1 The data model already provides most of a framework

A semantics framework needs: (1) a term language, (2) an abstract-grammar language,
(3) rules, (4) an execution engine. Lambda's existing data model covers (1), (2), and
much of the machinery for (4):

| Semantics concept | K / Ott construct | Already in Lambda |
|---|---|---|
| Terms / AST nodes | KItems, Ott terms | **Elements**: `<add <lit value: 1> <lit value: 2>>` — code-as-data, no quotation mechanism needed |
| Labels, names, metavariables | KLabels, metavars | **Symbols** — interned, O(1) equality |
| Abstract grammar (BNF) | `syntax Exp ::= ...` | **Recursive union types + occurrence modifiers**; the validator schema DSL *is* an abstract-grammar language |
| Terminals / lexemes | token sorts | **String patterns**: `string Ident = \a \w*` |
| Configurations (cells) | `<k> <env> <store>` | Elements with attributes + children: `<cfg <k; ...> <env; {...}>>` |
| Environments, stores | Map sort | **Maps**, with spread merge `{*:base, x: v}` (computed keys missing — §12.2.5) |
| Rule failure / stuckness | rule does not apply | **`T^E` error returns**, compile-enforced; errors are falsy, so `r1(t) or r2(t)` is deterministic rule choice for free (verified, §3) |
| Sort / well-formedness checking | kompile sort checks | The **validator** checks terms against grammar types |
| Rule registry + recursive dispatch | the rewrite engine | Precedent: the **`view`/`edit`** template system (`view_stam`) already implements pattern → template rules with a registry and recursive `apply;` |

The `view`/`edit` construct is the implementation blueprint: `syntax` / `rule` /
`judgment` declarations would follow the same pipeline — declaration forms in the
parser → `build_ast.cpp` lowering → data structures + generated functions — with a
structural matcher grown out of the validator's existing type-vs-value walker.

### 11.2 Readiness: ready in structure; rigor now ruled

The 2026-07-03 verdict was **ready in structure, not yet in rigor**. The data model —
elements, symbols distinct from strings, structural equality, occurrence types,
errors-as-values with compile-enforced handling — was sound. The docs were
user-guide-grade, and probing the implementation found doc-vs-implementation
divergences on core operators within minutes (findings A1–A10 in
`vibe/Lambda_Semantics_Formal.md`: three integer-overflow regimes, three aliasing
regimes, non-reflexive `==`, `'a' == "a"` raising where docs said `false`, OOB
indexing yielding unchecked error values).

Since then `doc/Lambda_Formal_Semantics.md` has become the normative spec, and the
findings the meta-fragment depends on are ruled:

| What the meta-fragment needs | Finding | Ruling now |
|---|---|---|
| Model arithmetic runs on host numerics (P5) | A1 overflow regimes | S4.1 (`int` is int53, closed and total), SI7, S4.1.5 (machine ints wrap, Go-aligned) |
| `==` is the check primitive (P6) | A5 non-reflexive / representation-sensitive `==` | SI4 (`==` total; an equivalence modulo `nan`/`error`); the ArrayNum `==` fix is tracked in the findings doc |
| The engine passes terms around | A7 aliasing | SI13 no aliasing, SI2 COW unobservability |
| The model has no inference | B7 gradual guarantee | SI3v2 inference evaluation-invariance |
| The model has no boxes | — | SI1 boxing invisibility, S1.6 |

The POC confirmed "ready in structure" empirically: M0–M3 landed with zero new syntax
and zero new runtime features (P1). It also kept finding rigor gaps — F1–F12 in §7 —
so hardening is not finished; building the model is how it advances.

### 11.3 The hardening gate, and what the model checks by construction

Before the model becomes a CI gate (§5) or Stage 4 of the DSL (§15.2), every finding
the meta-fragment touches must be resolved — fixed, or formally documented as-is. The
§11.2 table is that gate's checklist.

Two invariants are not prerequisites but **outputs**. The model has no boxes and no
inference, so any model-vs-runtime divergence on a boxing- or inference-sensitive
program is an SI1 or SI3v2 violation by construction. The `box_unbox*.ls` fixtures
therefore verify the strongest property available: that boxing is unobservable.
Several historical engine bugs — numeric-inference truncation, reg-0 sentinels, the
pn-param float-div truncation — were observability leaks of exactly this kind.

## 12. The DSL design

### 12.1 Overview

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

// ---- small-step rules: pattern --> template, with side conditions ----
rule add-vals:  <add <lit value: a> <lit value: b>>  -->  <lit value: a + b>

rule beta:      <app <lam param: x; body> (v: Value)>  -->  subst(body, x, v)

// ---- Ott-style moded judgment: typing ----
judgment types(env: map, e: Expr) Type^

rule t-lit:  |- types(g, <lit value: _>) = int

rule t-var:  x in g
             |- types(g, <var name: x>) = g[x]

rule t-add:  types(g, e1) == int, types(g, e2) == int
             |- types(g, <add e1 e2>) = int

// ---- strategies are a plain library over first-class rules ----
let step  = choice(add_vals, beta)      // errors are falsy: deterministic choice
let run   = fixpoint(innermost(step))
let final = run(<app <lam param: 'x', <add <var name: 'x'> <lit value: 1>>> <lit value: 41>>)
```

Compilation model — no engine magic:

- a `rule` compiles to a function `fn(Term) Term ^ NoMatchErr`;
- a moded `judgment` compiles to a recursive function returning `T ^ StuckErr` — the
  shape the POC's `ev` already has (§4.3);
- `strict` annotations expand into generated heating/cooling rules (§12.4.1);
- strategy combinators are ordinary higher-order functions;
- the existing compile-time error enforcement guarantees stuckness is handled.

### 12.2 Tier 1 — core-language additions

General-purpose language features, useful far beyond the DSL; the DSL cannot exist
without them. Ordered by importance.

#### 12.2.1 Destructuring patterns in `match`

The single biggest gap. `match` arms are type/literal/constraint patterns only; the
arm body sees the whole scrutinee as `~` and must extract sub-values manually
(`doc/Lambda_Expr_Stam.md` § Match Expressions). Semantics rules are mostly "match a
shape, bind the pieces", so this must become:

```lambda
match t {
    case <add (a: Expr) (b: Expr)>: eval(a) + eval(b)
    case <lit value: v>:            v
    case [x, rest...]:              ...
    case {name: n, *others}:        ...
}
```

- **One pattern language.** Extend the existing type-pattern grammar: an identifier in
  child/field position becomes a *binder* (optionally annotated `(x: T)`), instead of
  inventing a parallel pattern syntax. `is`, queries (`?`, `[T]`), `match`, and rule
  left-hand sides all share it.
- **Linear first.** Repeated variables (non-linear patterns implying equality) are
  rejected in v1; they can be added later as sugar for a `when` equality condition.
- Literal children still match literally; `_` is the wildcard binder.

POC evidence: manual extraction (`t[0]`, `t.p`, and the `let` hoists F3 forces) is
expected to be the kernel's dominant verbosity; §9.1 asks for the measurement.

#### 12.2.2 Rest / frame patterns

K's "ellipsis frames," needed both for sequence matching and for configuration rules
that mention only the cells they touch:

- **Sequence rest:** `<k pat rest...>` — match the first child, bind the remaining
  children as a list. Also at array level: `[x, rest...]`.
- **Map rest:** `{x: v, *rest}` — match one entry, bind the map minus that entry.
  **Restriction:** the matched key must be *determined* — a literal or an
  already-bound variable — never an unconstrained fresh variable. This deliberately
  avoids general associative-commutative matching (the expensive part of K); §12.4.3
  argues the restriction is sufficient for real language models.

#### 12.2.3 Fresh symbol generation

Capture-avoiding substitution and heap allocation both need fresh names.
`fresh('x')` returns a symbol guaranteed distinct from any symbol currently interned
with that stem (`'x_1'`, `'x_2'`, ...). To preserve `fn` purity it follows the
`math.random(seed)` precedent — deterministic given a threaded counter — or is
provided implicitly inside `rule` bodies via a `fresh y` clause (§12.3.3), where the
engine threads the counter. The POC needs none of this (P2); small-step
substitution and store allocation do.

#### 12.2.4 Source-as-data ingestion

To model real programs, source text must become element terms. This is an
input-side addition, not a language construct. The 2026-07 draft planned
`input(f, 'lambda')` over a tree-sitter CST; the production parser is now the
first-party C parser (D8.1.1v18), and since M4 (§6.3, P11) its AST dump *is*
Mark, read back with `input(f, 'mark')`: node kind → element tag, fields and
attributes → child elements, literals → their source text (re-read as Mark
scalars by `normalize`). The `normalize` pass, written in Lambda, lowers the
dump to the abstract grammar of §12.3.1; today it targets the POC's §4.1
vocabulary. Two refinements remain open: emitting literals as typed values
rather than source text, and an in-process `parse(src, 'lambda')` that returns
the dump without a file, so a fixture can carry its program inline. LambdaJS
takes the same shape — its dump moved to Mark in the same change.

#### 12.2.5 Computed-key map construction (POC-derived)

`{(k): v}` is a parse error (re-checked 2026-10-13) and there is no runtime map
constructor, so a model cannot build a map whose keys are data at model-run time. The
POC worked around it with assoc-list elements (§4.2, M2); a DSL whose environments and
stores are maps (§12.3.2, §12.4.3) needs the real thing. A `{(k): v}` spelling would
also mirror the determined-key marker in patterns (§16 Q2).

#### 12.2.6 Element-content parse rules (POC-derived)

F3, F4 and F10 (§7): content position restricts children to simple primaries, glues
a following parenthesized expression onto the preceding child as a call, and parses
`name <...` as less-than. Rule right-hand sides are element templates assembled from
bound pieces — exactly the shape these rules break. They need grammar rulings (ask,
per `doc/Doc_Convention.md`) before Tier 2.

### 12.3 Tier 2 — DSL declaration forms

New top-level statements, implemented exactly like `view_stam`: parser declaration
forms → `build_ast.cpp` → data + generated functions.

#### 12.3.1 `syntax` — abstract grammar with binders and strictness

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
  `free_vars(t)`, `subst(t, x, s)` (capture-avoiding, using §12.2.3), and
  `alpha_eq(t1, t2)` for every syntax sort — users never hand-write them per language.
- **`strict` / `strict(i, ...)`** — evaluation-order spec; expansion in §12.4.1.
- Terminals reuse **string patterns** (`string Ident = \a \w*`); sorts reuse union,
  occurrence (`Stmt*`), and optionality — the whole validator type language carries
  over, and the validator itself checks term well-formedness against `syntax` sorts.

#### 12.3.2 `config` — the configuration cell tree

```lambda
config Cfg = <cfg
    <k; KItem*>                 // computation: a sequence of KItems (§12.4.2)
    <env; {symbol: Addr}>       // lexical environment: name → address
    <store; {Addr: Value}>      // heap: address → value (§12.4.3)
    <out; string*>              // output channel (print effects)
>
```

`config` is a `syntax` declaration with a distinguished role: `load(prog)` produces
the initial configuration; rules that omit cells are implicitly framed (§12.4.2); a
user-declared `final` predicate classifies terminal configurations.

#### 12.3.3 `rule` — rewrite rules

```lambda
rule name?:  LHS-pattern  -->  RHS-template
             when  <side-condition expr over bound vars>
             fresh y1, y2                // engine-supplied fresh symbols
```

- `-->` is the rewrite arrow (§16 Q1).
- LHS is a §12.2.1 pattern (with §12.2.2 frames); RHS is an ordinary element
  template — Lambda element literals already interpolate expressions and spread, so no
  quasiquotation machinery is needed.
- `when` sees all LHS bindings; it is the general guard (the existing `that`
  constraint syntax remains usable inside patterns for per-binder conditions).
- Each rule reifies as `fn(Term) Term ^ NoMatchErr` and is registered under its
  module's rule set; anonymous rules get generated names.

#### 12.3.4 `judgment` — moded inference rules

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
  `let v^err` destructuring handle idiomatically. The POC's `ev` is this shape,
  written by hand.
- Full relational/backtracking (Prolog-mode) judgments are **deferred**: moded
  judgments cover typing, big-step eval, and elaboration.
- Derivation-tree recording (for proof output, and for debugging why a program is
  stuck) is a runtime flag: when on, each judgment call returns its derivation as an
  element tree, renderable via `format()`.

#### 12.3.5 Strategies — a library, not syntax

Because rules are first-class functions with error-typed failure, the strategy layer
is ordinary Lambda code, shipped as a library module:

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

#### 12.3.6 `builtin` — the trusted-primitive boundary

No one models a host library's sort or string internals as rules. A model declares its
primitive boundary explicitly:

```lambda
builtin js_string_concat(a: string, b: string) string = (a, b) => a ++ b
builtin math_sqrt(x: float) float = math.sqrt
```

Rules invoke builtins like judgments. The declaration form (rather than plain `fn`)
matters for **auditability**: `lambda semantics --list-builtins model.ls` would
enumerate exactly what a model trusts rather than derives — the "trusted computing
base" of each language model. The POC's version is one marked section of the kernel
(§4.5).

### 12.4 Tier 3 — machinery for stateful, small-step models

The functional core needs none of this (§1.4). `pn`, LambdaJS, and any small-step
model need all three.

#### 12.4.1 Strictness expansion (heating / cooling)

Environment/store-based small-step semantics evaluate `<add e1 e2>` by first reducing
`e1` inside its context. Without automation that is two hand-written rules per
operator per argument position — hundreds of pure boilerplate rules at JS scale. The
`strict` annotation generates them. For `<add; Expr, Expr> strict`, the engine emits
(conceptually):

```lambda
// heat: schedule child 1, leave a hole
rule <k <add e1 e2> rest...> --> <k e1 <add <hole> e2> rest...>
     when not (e1 is Value)
// cool: plug the result back
rule <k (v: Value) <add <hole> e2> rest...> --> <k <add v e2> rest...>
// ... same pair for position 2, guarded on position 1 already a Value ...
```

with a distinguished `<hole>` term per sort. `strict(1)` limits which positions heat —
which is exactly how short-circuit `and`/`or`, `if`, and assignment targets are
expressed. `seqstrict` (left to right, each position waiting on the previous) is the
default reading of bare `strict`. This is the single highest-leverage generated
construct: in KJS-class semantics, heating/cooling is roughly a third of all rules,
all mechanical.

#### 12.4.2 The k-cell discipline: continuations as data

The `<k; ...>` cell holds a *sequence* of computation items; rules match on its
**prefix** (via §12.2.2 sequence-rest patterns). This one representation gives every
non-local control feature:

```lambda
// exceptions: unwind pops frames until a handler frame
rule throw:   <k <throw v> rest...>                        --> <k <unwinding v> rest...>
rule unwind:  <k <unwinding v> f rest...>                  --> <k <unwinding v> rest...>
              when not (f is TryFrame)
rule catch:   <k <unwinding v> <try-frame h env> rest...>  --> <k <apply h v> rest...>

// generators / await: the k-suffix IS the continuation — capture it into the store
rule yield:   <cfg <k <yield v> rest...> <store s> c...>
              --> <cfg <k <yielded v>> <store {*s, (gaddr): <gen-state [rest]>}> c...>
```

Discarded suffix = exceptions, `return` (the same unwind, stopping at a call frame),
`break`/`continue`. Captured suffix = JS generators, `async`/`await` (plus a
microtask-queue cell). No first-class continuations are added to Lambda itself —
continuations exist only as *data* (lists of KItems) inside the model.

#### 12.4.3 Store cells and determined-key matching

Mutation needs `<store; {Addr: Object}>` with fresh addresses (§12.2.3) for
allocation. For Lambda the store holds `var` bindings only — `let` is final and
values never alias (SI13) — so `let`-finality and COW unobservability (SI2) become
checkable model properties (`vibe/Lambda_Design_Runtime_COW.md` §9.6 defers them
here). JS needs the full object store. Lookup and update use map rest-patterns:

```lambda
rule var-lookup:
    <cfg <k <var name: x> rest...> <env {(x): a, *_}> <store {(a): v, *_}> c...>
    --> <cfg <k v rest...> <env {(x): a, *_}> <store {(a): v, *_}> c...>
```

The parenthesized `(x)` key marks "key determined by the already-bound variable `x`" —
distinguishing it from a literal key (§16 Q2). Because every store/env access in
KJS-style semantics looks up a determined key, this restriction keeps matching
linear-time while avoiding general AC matching. If a model genuinely needs "find
*some* entry such that ...", that is an explicit `for`/`that` query in a `when`
clause — visible cost, not silent engine magic.

### 12.5 Per-language notes

**Lambda** (the first and load-bearing model):
- The pure `fn` subset is a textbook environment-based functional language: closures,
  `let`, arithmetic over the numeric tower, structural `==`, elements/maps/arrays,
  pipes (`|`, `that` desugar to higher-order rules), `for` comprehensions with
  spread-flattening, `match`, string patterns.
- The delicate parts to model precisely are where implementation bugs have
  historically hidden: numeric promotion and truncation boundaries, boxing (not
  modeled — §11.3), error propagation `^`/`let v^err` and the two error channels the
  POC collapses (F9), `pn` mutable-capture copy semantics, spread/flattening in array
  literals (F8).
- Effects (`print`, `|>`, `input`) target the `<out>` cell and a virtual file map —
  which is exactly what fixture verification needs (§13).

**LambdaJS (JavaScript / TypeScript):**
- Prototype chains = store-walking rules; `this`-binding, hoisting (a pre-pass
  judgment), and the coercion tower (`ToPrimitive`/`ToNumber`/abstract equality) are
  ordinary rules — voluminous but shallow. Event loop + microtasks = two queue cells
  and a scheduling rule. KJS calibrates the size: ~1,250 rules for the ES5 core.
- TypeScript is *not* separately modeled at run time: model erasure (a judgment from
  TS AST to JS AST), optionally the type system later as a `judgment`.

The 2026-07 draft also sketched Python, Bash and Ruby models. Those front ends have
since been removed from the engine (hosted languages, 2026-10-07), so they are out of
scope.

## 13. Verification at full scale

### 13.1 The pipeline and the four buckets

For each fixture pair `test/lambda/foo.ls` + `foo.txt`:

```
foo.ls ──AST dump (§12.2.4)──▶ element AST ──normalize──▶ abstract term
        ──load()──▶ initial Cfg ──fixpoint(step)──▶ final Cfg      (big-step: ev(term))
        ──extract module value + <out> cell──▶ model value
        ──format(v, 'mark')──▶ model output  ⟷ diff ⟷  foo.txt (golden)
```

Three-way comparison per fixture:

1. **model output vs. golden `.txt`** — the headline check;
2. **model output vs. `// expect:` comments** — the `.ls` files carry per-test-point
   expectations (`let t1 = add10(5)  // expect: 15`); a harness pass maps each
   annotated binding to its position in the module result and checks it individually,
   giving *finer-grained* localization than the whole-file golden (§16 Q4);
3. **runtime output vs. model output** — differential testing on generated/edge-case
   programs beyond the fixture set (§14.1).

Every fixture then lands in one of four buckets:

| runtime == golden | model == golden | verdict |
|---|---|---|
| ✓ | ✓ | **verified** — implementation and spec agree |
| ✓ | ✗ | **suspect golden** — either a captured implementation bug (the motivating case!) or a model bug; triage required, and either way something written down is wrong |
| ✗ | ✓ | runtime regression with an independent confirmation the golden is right |
| ✗ | ✗ | both moved — usually an intended semantics change; update spec and golden together |

The ✓/✗ bucket is the payoff: today those cases are *invisible*. Each triage either
fixes a runtime bug, fixes a model rule, or surfaces under-specified semantics that
gets ruled in the formal spec — all three outcomes are wins. F1 (§7) is this bucket,
found by hand.

Harness integration (landed 2026-10-14, §6.4):

```bash
make semantics-verify                        # test/lambda, bucket report, manifest gate
make semantics-verify test=closure           # one fixture: census, expect checks, diffs
make semantics-verify ARGS='--update'        # grow the verified set from one sweep
```

On mismatch the harness prints the unified diffs of the model's and the runtime's
output against the golden, the `// expect:` verdict per binding, and for a pending
fixture its reason (the unsupported kinds, or the stuck message). The first diverging
*step* — the configuration, the rule that fired — is what derivation recording
(§12.3.4) would add; a big-step model has no steps to report.

### 13.2 Coverage strategy: grow the verified subset

Do **not** attempt the whole suite at once. Order by semantic dependency, roughly:

1. literals, arithmetic + numeric promotion, `let`, comparison (`numeric_expr.ls`,
   `mixed_numeric_ops.ls`, `box_unbox*.ls`, `chained_comparisons.ls`, ...)
2. closures, higher-order functions (`closure*.ls`)
3. arrays/maps/elements, spread/flattening, indexing/slicing (`comp_expr*.ls`, ...)
4. pipes, `that`, queries (`child_query.ls`, ...)
5. `match`, string patterns
6. error handling (`raise`, `^`, `let v^err`)
7. `pn` procedural subset: `var`, `while`, mutable captures, assignment targets
8. modules/imports; then long-tail system functions (each an audited `builtin` until
   promoted to rules)

The POC covers tiers 1–2 and part of 3 (§8). `test/lambda/sem/semantics-manifest.json`
(§6.4) tracks it: the verified set (26 fixtures on 2026-10-14), the pending census with
each fixture's reason — the tier order above, measured — and exclusions with a reason
(none yet; real I/O would be the first). The CI gate is "verified set stays verified"
— the set only grows, by `--update` from one sweep, never by hand.

### 13.3 Independence and bootstrapping

The Lambda model is written in Lambda and executes on the Lambda runtime — the runtime
under test. Is the oracle independent? Honestly stated:

- **The trusted core** is the DSL engine (matcher, rewriter, strictness expansion —
  mostly C++ following the validator/view code paths, plus generated glue) and the
  small Lambda subset the strategy library uses; for the POC, the host features the
  kernel leans on (§10, risk 1). This core is *small*, exercised by every rule application
  (so bugs in it fail loudly and broadly, not silently on one fixture), and testable
  in isolation with its own unit fixtures.
- **The independence argument** is not "different codebase" but **different failure
  modes**: the runtime implements `+` via JIT-compiled tagged-value machine code; the
  model implements it via a rule that fires on abstract values. A truncation bug in
  MIR codegen and a wrong side-condition in a rule have essentially no mechanism for
  coinciding. This is the same argument by which a compiler's test oracle may be an
  interpreter for the same language, sharing the parser.
- **Shared-frontend caveat:** both sides consume the same parse (the C parser, via the
  AST dump). Parser bugs are out of scope of this oracle; grammar-level tests cover
  them.
- **Escape hatch for full independence:** because `syntax`/`rule`/`judgment`
  declarations are *data* after `build_ast`, an exporter to K (or LaTeX for human
  review, Ott's headline feature) is a formatter, not a redesign. Cross-running even
  the arithmetic fragment on another host would spot-check the trusted core itself.
  The Redex model played this role until its removal (§1.2); nothing does today.

Bootstrapping compounds: once the Lambda model is trustworthy, the LambdaJS model runs
on a verified substrate, and its verification targets already exist — `test/js/` and
the Node-compat baseline — giving the JS runtime the same four-bucket triage against
*its* captured goldens.

## 14. Further directions

Fixture verification is one consumer of a more general asset — an executable,
queryable definition of "what should happen." None of these are v1 scope; all are
reachable without redesign.

- **14.1 Fuzzing the JIT with a real oracle.** Once `syntax` sorts exist, Redex-style
  random program generation is nearly free, and the model says what each generated
  program *should* produce. This targets the bug class historically found by accident:
  the JS→MIR numeric-inference bugs, the pn param-inference mis-typing, the reg-0
  index-assign sentinel, the GC-rooting use-after-free. A generator biased toward the
  delicate corners (numeric promotion boundaries, closures over `var`,
  spread-flattening, error propagation through pipes) finds these systematically.
- **14.2 Arbitrating between execution tiers.** The T0 interpreter and the MIR JIT
  (D8.1.1v18 tiered execution) are de-facto differential tests of each other, but when
  they disagree, deciding which is right is reasoning from intent. The model is the
  tie-breaker, and a disagreement between one tier and the model localizes the fault
  to that tier.
- **14.3 Spec-first language evolution.** With rules, a feature ships as rules: edge
  cases (how does `^` interact with pipes? what does spread do inside a `for` inside
  an array literal?) surface when the rule is *written*, not when a benchmark breaks.
  The formal spec now states rulings in prose; rules would be their executable,
  regression-checked form across transpiler refactors.
- **14.4 Testing metatheory, not just programs.** Property-test the design over
  random programs: **type soundness** (well-typed terms don't get stuck),
  **determinism** (no two rules fire on the same configuration), **equivalence laws**
  (`xs | f | g` ≡ `xs | g(f(~))`, purity of `fn`, confluence of the functional
  subset). Every optimization implicitly claims an equivalence; the model can check
  the claim on random inputs before the optimization lands.
- **14.5 Generated, always-correct documentation.** Declarations are data after
  `build_ast`, so rendering them to LaTeX/HTML/Markdown is a formatter. The docs can
  carry generated rule boxes that *cannot* drift from the checked model, and Lambda's
  own math/LaTeX pipeline suits inference rules.
- **14.6 Tooling built on the judgments.** The typing judgment is a type checker for
  free — LSP hover types, REPL diagnostics, a `lambda check script.ls` mode.
  Derivation recording gives "explain": *why* is this `int`? *why* is this stuck?
- **14.7 Conformance grading for LambdaJS.** Run a corpus through model and runtime;
  the diff *is* the compat report — which constructs the runtime mis-executes versus
  simply doesn't reach. Sharper than a bare pass rate, which counts outcomes without
  attributing causes. The same mechanism validates the TS→JS erasure judgment.
- **14.8 Long-term options.** K's ecosystem shows where this road goes: symbolic
  execution, deductive program verification, correct-by-construction interpreter
  generation. Out of scope, but they are why rules stay declarative data rather than
  being compiled away irreversibly.

## 15. Non-goals and staged plan

### 15.1 Non-goals (v1)

- **Concrete-syntax rules** (K's rules written in the object language's own
  notation). Element notation is a readable term syntax; AST ingestion covers real
  programs. Cuts the largest single implementation cost in K.
- **General AC matching** on maps/sets/bags — §12.4.3's determined-key restriction.
- **Backtracking judgment search** — moded judgments only (§12.3.4).
- **Symbolic execution / deductive verification** (§14.8).
- **Performance as a runtime.** The model is an oracle, not an interpreter users run.
  Fixture-suite wall-clock should stay in CI-tolerable minutes; if rule dispatch
  becomes the bottleneck, compiling rules through MIR (they are already functions) is
  the natural lever — but only when measured.
- **Modeling the GC, boxing, arenas** — implementation strata are invisible by design
  (SI1, S1.6); that invisibility is itself the property being verified.
- **Low-level languages** (C/C++, including the C+ subset the runtime is written in) —
  not pursued here.

### 15.2 Staged implementation plan

Staged so each stage lands with its own tests and is useful standalone.

| Stage | Content | Status |
|---|---|---|
| 0 | Structural matcher + destructuring `match` (§12.2.1–§12.2.2): binders, rest patterns, determined-key map patterns in the type-pattern grammar; the validator's walker grown into a binding matcher (C++). Ships to the whole language. | not started |
| 1 | `syntax` + term ingestion (§12.3.1, §12.2.3–§12.2.4): binder metadata, derived `free_vars`/`subst`/`alpha_eq`, `fresh()`, AST-dump ingestion, validator sort checking. | not started; ingestion = POC M4 |
| 2 | `rule`, strategies, `config` (§12.3.2–§12.3.5). Milestone: the §12.1 toy calculus runs. | not started |
| 3 | `judgment` + strictness expansion (§12.3.4, §12.4.1), derivation recording. Milestone: typed calculus with auto-generated evaluation order. | not started |
| 4 | Lambda model, functional core (§13.2 tiers 1–5); `builtin` declarations. Milestone: `closure.ls` and `numeric_expr.ls` verified. | **pulled forward as the POC** — M0–M3 in plain Lambda (§8) |
| 5 | Verification harness: `make semantics-verify`, `// expect:` extraction, four-bucket report, `semantics-manifest.json`, CI gate on the verified set — the first end-goal payoff. | **landed 2026-10-14 in POC form** (§6.4, M5): driver, manifest gate, `// expect:` checks; the rule trace waits for §12.3.4 |
| 6 | Lambda model completion (§13.2 tiers 6–8) + first triage sweep: every ✓/✗ entry resolved to a runtime fix, a model fix, or a ruling. | — |
| 7 | LambdaJS model (KJS as the map; validated against `test/js/` and the Node-compat baseline), starting with the AST-normalization judgment and §12.4 already in place. | — |

Risk notes: Stage 0 touches the shared pattern grammar — the one stage with regression
surface on the existing language; it gates on `make test-lambda-baseline` staying
100%. Stages 1–3 are additive (new statement kinds, `view_stam`-style). The long tail
is Stage 6 builtin coverage — bounded by making every builtin an explicit, listable
declaration (§12.3.6).

## 16. Open questions (DSL)

1. **Rewrite arrow spelling** — **resolved: `-->`** (the classic PL reduction arrow).
   The 2026-07 draft used `~>`; the C6 pipe-syntax discussion
   (`vibe/Lambda_Semantics_Formal2.md` §C6.2) killed it: rule templates and `when`
   clauses contain expressions, where `~ > 3`-style comparisons are common, so `~>`
   would differ from `~ >` by one space with silently different meanings.
2. **Determined-key marker** — `(x)` in map-pattern key position (§12.4.3) vs. an
   explicit `key(x):`. The former is terser; the latter survives skimming better. If
   computed-key map literals land as `{(k): v}` (§12.2.5), `(x)` in patterns mirrors
   construction — an argument for it.
3. **Where models live** — `semantics/` at repo root vs. `lambda/semantics/` (the
   former Redex home) vs. shipping as loadable `.ls` packages. Interim answer for the
   POC: `test/lambda/sem/` with a golden-less kernel module (§5).
4. **`// expect:` formalization** — keep as comments parsed by the harness, or promote
   to a checked `expect` statement in test scripts? Comments preserve fixture
   compatibility; a statement is harder to let rot. Interim (Stage 5, §6.4): the
   harness parses the comments — the leading Mark value counts, trailing prose
   is ignored — and only a top-level binding can be probed.
5. **Numeric-tower fidelity** — model arithmetic on Lambda's own numerics vs. explicit
   bit-level rules at the boundaries. Resolved for the POC as P5 (host numerics). The
   boundaries themselves are now ruled (S4.1, S4.1.5, SI7), so host arithmetic is
   checked against a written spec rather than trusted blind; targeted boundary checks
   remain the mitigation (§10, risk 1).

## 17. Summary

The end goal — an executable, independent semantic model that verifies the Lambda
test fixtures — does not have to wait for the DSL. Big-step + environments eliminates
the machinery tiers for the functional core; elements, `match`, closures, and the
`T^E` channel are enough for the rest, and a working kernel with differential checks —
closures, capture, recursion, stuckness — **already runs, verbatim, on today's
runtime**. The POC turns the DSL design's largest open risks (is Lambda meta-ready?
what must Tier 1 contain? does the four-bucket loop pay?) into a few hundred lines of
committed Lambda.

The "does it pay?" risk is already retired. Merely *probing* for this document
— before a single line of the model was committed — surfaced a doc divergence,
two grammar sharp edges, and a `len`/iteration defect whose repair revealed
that the graph schema validator had been type-checking zero attributes on every
canonical graph, with fourteen goldens capturing the vacuous pass (§7 F1).
That is §13.1's ✓✗ bucket, found by hand, on the first afternoon.

Part II keeps the destination in view: destructuring and frame patterns, fresh
symbols, source ingestion and computed-key maps (Tier 1);
`syntax`/`config`/`rule`/`judgment`/`builtin` declaration forms with a strategy
library (Tier 2); strictness expansion, the k-cell continuation discipline, and
determined-key store matching (Tier 3) — the machinery that lifts the approach from
the functional core to `pn` and LambdaJS, proven feasible at scale by KJS. The POC is
its Stage 4, done first, arriving with its own regression suite.
