# Lambda Design: Structural Type Inference (Lambda + JS Unified)

- **Date:** 2026-08-18
- **Status:** PROPOSAL (survey complete; decision ledger `TI1+`)
- **Scope:** how static type inference should be structurally carried out over expressions in the Lambda front end (`lambda/runtime/build_ast.cpp`) and the LambdaJS front end (`lambda/js/build_js_ast.cpp` + `js_mir_function_collection_class_inference.cpp`), how the two unify, and the catalog of operations that currently degrade to `any`
- **Formal authority:** `doc/Lambda_Formal_Design.md` D2.4.1, D2.5.3, D3.1.1v2, D3.2.1, D3.2.3, D3.3.1v2–D3.3.4, D8.3.3, D8.4.3; `doc/Lambda_Formal_Semantics.md` S4.1.1–S4.1.2, S5.5.2, S7.1, S7.2.1, S11.4.1; invariant SI3v2 (revised 2026-08-18 alongside this doc: a type-error-free script's result is never affected by inference; static rejection lands straightaway; strictness is per-surface — Lambda strict, LambdaJS warn-only)
- **Related:** `vibe/Lambda_Design_Compiling_Lane.md` (ValueRep — representation side of the same coin), `vibe/Lambda_Impl_Tune19.md` (T19-1..T19-7 — the perf evidence that motivates this doc), `vibe/Lambda_Design_Type_Enforcement.md` (TE-1..TE-18 — enforcement at boundaries), `vibe/Lambda_Issue_Type_Support.md` (TS-3 ANY-downgrade)
- **ID series:** `TI#` (type-inference decisions), `TIG#` (gap catalog entries)
- **Implementation plan:** `vibe/Lambda_Impl_Type_Infer.md` (IP0–IP7 over the decided-but-unbuilt items)

## 0. Framing: two products, one pass

Inference produces two separable artifacts, and the codebase currently
conflates neither cleanly nor consistently:

1. **The static type** (`Type*` on each AST node) — the *semantic* answer
   "what values can this expression produce". D2.4.1: the `Type*` graph is the
   semantic authority; a TypeId alone is never a contract.
2. **The representation** (lane/carrier) — the *mechanical* answer "which
   machine carrier holds it". Owned by `ValueRep`
   (`vibe/Lambda_Design_Compiling_Lane.md`); always derived FROM the static
   type plus flow proofs, never the other way around (D3.3.4).

This doc rules the first product. The standing constraints:

- **D3.3.1v2 / SI3v2** (revised 2026-08-18 with this design) — a script
  with NO type error evaluates identically regardless of inference: erase
  every inferred type, run boxed, results identical. Inference IS
  statically observable — improved precision may reject a
  previously-compiling script, and Lambda rejects STRAIGHTAWAY (no
  warn-first transition; the sanctioned observable change is "runtime soft
  error with partial result" becoming "static error, no result").
  Strictness is per-surface per TI6: Lambda strict, LambdaJS warn-only.
- **D3.2.3** — declared and inferred are separate recorded facts; inference
  never overrides a declaration's *contract*, and an annotation never erases
  what inference *proved* (both facts persist side by side).
- **D3.3.2v2** — body inference does not retype parameters; entry-shape and
  body/result inference are separate products. (The former "inference never
  creates a `^` obligation" clause is retired: precise inference may
  surface a hidden error component and create a STATIC containment
  obligation; it never creates a runtime obligation or changes an accepted
  program's result.)
- **D3.3.4 / D2.5.3** — the public inferred type is the full contract
  (`a[i]` is `T?`); flow-sensitive proofs narrow privately.

## 1. Where inference lives today (survey result)

### 1.1 Lambda: one structural pass + one evidence pass

**Pass A — structural, in `build_ast.cpp`.** Bottom-up over the CST during
AST building: literal leaves get exact literal types (`is_literal` flag
preserved), numeric binaries classify through `lambda_numeric_classify`
(the S4.1 numeric-domain kernel — precise), comparisons/`is`/`in` type as
bool, `to` as range, map literals build `TypeMap` shapes, member access
resolves through the object's shape. This pass is genuinely structural and
in decent shape for numerics — its failures are the ANY catalog in §2.

**Pass B — evidence counters, in `transpile-mir.cpp` (~line 20931+).**
`FnParamEvidence` (`ast-core.hpp:805`): per-untyped-param counters
(`int_evidence`, `float_evidence`, `string_evidence`, `used_as_container`,
`compared_with_non_numeric`, `param_reassigned`), gathered by
`gather_evidence_multi` walking the body, plus alias tracking. Resolves
each parameter to one TypeId for entry specialization.

### 1.2 JS: a near-vacuous structural pass + the same evidence pass

**`build_js_ast.cpp`** sets types at leaves correctly (number→float,
bigint→decimal, string, bool, null) and then gives up: **every binary
expression is typed `&TYPE_FLOAT`** (`build_js_ast.cpp:737`) — including
`==`, `<`, `&&`, string `+`; every call and member access is `&TYPE_ANY`;
conditional is ANY when arms differ. `JsAstNode` **is** `AstNode`
(`js_ast.hpp:195`), so the `Type*` slot exists on every node — it just
isn't filled with anything structural.

**`js_mir_function_collection_class_inference.cpp`** ("P4/P6") then re-walks
function bodies with the *same* `FnParamEvidence` struct Lambda uses,
resolving untyped params to FLOAT/STRING/container/ANY, with alias
merge-back and a BigInt bail-out that boxes every param of the function.
Class/shape inference for `this` lives in the same file (D3.4.7 JsClassMeta).

### 1.3 The structural verdict

The two engines already share the type lattice (`Type*`/TypeId), the AST
node layout, and the evidence struct. What they do **not** share is the
structural pass: Lambda has one; JS replaced it with per-emitter heuristics
and the evidence walk. Meanwhile Lambda's own Pass B duplicates in weaker
form what Pass A already computed — evidence counters re-derive "this param
is used as float" when structural propagation from call sites could prove
it (Tune19 T19-4's closed-caller analysis is exactly that, done properly).

## 2. TIG — catalog of operations that currently infer `any`

Ordered by measured cost (Tune19/Result32 evidence where available).
`build_ast.cpp` line refs are to the surveyed tree (commit `a6192c108`).

| ID | Site | Current behavior | Should be |
|---|---|---|---|
| **TIG1** | Indexed read `a[i]`, `ARRAY`/`ARRAY_NUM` object (`build_ast.cpp:2604-2608`) | `ANY` ("transpiler will refine") | `elem(T)?` per D2.5.3 — the AST already knows `TypeArray.nested`; ANY here is why bounce's `int[]` compare lowers through double (T19-C) |
| **TIG2** | Map/element field access, field type non-scalar (`:2643-2653`) | `ANY` unless field type is native-numeric/bool/string | the shape's recorded field `Type*` — chained access `a.b.c` dies at the first container field today |
| **TIG3** | Field not found in shape / unshaped map | `ANY` | `any` is right for open maps, but a **closed** declared shape should make the miss a static E-diagnostic, not ANY |
| **TIG4** | 160 of 228 sys-func registry rows (`sys_func_registry.c`) | `return_type = &TYPE_ANY`; `success_type` set on **zero** rows | precise `T` or `T \| error` (§4). Worst offenders: all of `math.*` (sqrt/sin/…, native lane already returns double!), `abs`, `min`/`max`/`sum`/`avg`, `upper`/`lower`/`trim`/`split`/`join`, `slice`/`reverse`/`sort`/`unique`, `parse`/`input`/`fetch`, `push`/`set`, matrix/image ops |
| **TIG5** | Logical `and` (`:5325-5326`) | `ANY` ("truthy idiom") | same treatment `or` already gets (`:5330-5337`): normalized union of the operand contribution — `or` was fixed, `and` was not |
| **TIG6** | Relational `< <= > >=` with any non-native-numeric operand (`:5348-5352`) | `ANY` — even `string < string` | `bool` for every comparable pair (string/string, dtime/dtime per `known_magnitude_comparable`); `bool \| error` for unproven mixed pairs |
| **TIG7** | If-expr with differing non-diverging arms (`:5683-5684`) | `ANY` (union join deliberately deferred — E208 containment fallout documented in the comment) | normalized `T1 \| T2` (§5.2 staging: fix E208's error-visibility first) |
| **TIG8** | Match expr with differing arm types (`:5800`) | `ANY` | normalized union of arm types (same join as TIG7) |
| **TIG9** | Match/if arms: no scrutinee narrowing | arm body sees the unnarrowed scrutinee type | occurrence narrowing per D3.3.4: `match x { int => … }` binds `x:int` inside the arm; `if (x is T)` / `x != null` narrow in the then-branch (§5.1) |
| **TIG10** | For-loop over maps/elements/`any` source (`:8343`); key-only loops (`:8322`); symbol/all key filters (`:8356-8358`) | `ANY` | element type of the source contract where the shape knows it; `int \| symbol` for LOOP_KEY_ALL |
| **TIG11** | List literals (`:5817`), content blocks (`:10581`) | `ANY` ("returns Item, not List") | the built `TypeList` — the node throws away the type it just constructed |
| **TIG12** | Unary `+`/`-` on non-numeric-typed operand (`:4715`) | `ANY` | `number \| error`; on `T?` numeric, `T \| error` |
| **TIG13** | JS: every binary expr (`build_js_ast.cpp:737`) | `FLOAT` unconditionally (comparisons, `&&`, string `+`…) | structural: comparison→bool, `+`→float\|string\|decimal by operands, `&&`/`\|\|`→operand union, bitwise→int lane (ToInt32), typeof→string |
| **TIG14** | JS: every call / member / subscript | `ANY` | builtin catalog signatures (`js_builtin_catalog.def` exists!) + closed-caller return propagation; subscript on known-element array→`elem?` |
| **TIG15** | Decompose/destructuring vars (`:6167-6175`) | `ANY` ("determined at runtime") | project the source shape's field/element types through the pattern |
| **TIG16** | Pipe `\|>` with `~` (`:5378`) | bare `ARRAY` | `array(RHS-type)` — element type of the mapped expression |
| **TIG17** | Untyped `pn` params only used in `/` | mis-inferred int historically (INFER_FLOAT_CONTEXT patch) | subsumed by structural call-site propagation (§3 step 5) |

Confirmed non-gaps (already correct, keep): numeric binary classification
(`lambda_numeric_classify`), `or` union with error/null removal, indexed
reads under a **declared** compound destination (nullable-normalized,
`:2591-2598`), floor/ceil/round/trunc carrier preservation, bitwise call
typing (`infer_bitwise_call_type`), `index_of`/`ord` as `int?`, if-arm
divergence joining `T | error`.

## 3. TI1 — the unified structural algorithm

**TI1 (decision).** Both engines run the SAME structural discipline, in
this order, over every expression:

1. **Leaves.** Literals carry exact literal types (`is_literal` set);
   identifiers copy the binding's recorded type (declared if annotated,
   else inferred — see TI4); parameters carry their contract (D3.2.3).
2. **Bottom-up propagation.** Each expression node computes its type from
   its children's types through an operator-indexed rule table — the S4.1
   numeric kernel for arithmetic, `bool` for comparisons, normalized unions
   for joins (`or`, `and`, if/match arms), shape projection for
   member/index/destructure, catalog signatures for calls. **No node may
   default to ANY when a rule exists for its children's types**; ANY is
   reserved for genuinely open inputs (untyped params, open maps, dynamic
   member names).
3. **Absence and error are part of the type, not an excuse for ANY.**
   Totality wraps: indexed read `T?` (D2.5.3), fallible op `T | error`
   (S7.2.1). `lambda_type_nullable_normalized` / union normalization
   (S5.5.2) keep the graphs canonical.
4. **Flow narrowing** (§5) — a private, per-branch refinement layer over
   the public type (D3.3.4): `is`-tests, null-tests, match patterns.
5. **Cross-function propagation.** Closed-caller analysis (Tune18's landed
   witness machinery, generalized by Tune19 T19-4): when every non-escaped
   direct caller proves an argument type, the callee's *inferred* entry
   shape consumes it; returns propagate to call sites the same way.
   This SUBSUMES the evidence-counter passes — `FnParamEvidence` becomes
   the fallback for open/escaped functions only, and eventually retires.
6. **Representation last.** `ValueRep` derives from the final static type +
   flow proofs (Compiling-Lane doc); nothing in steps 1–5 reads a MIR reg
   type or a carrier.

**TI2 (decision).** The rule table is **shared infrastructure** between
Lambda and JS: one operator-typing kernel (`lambda_numeric_classify` +
comparison/join/shape rules) parameterized by language policy where
semantics genuinely differ (JS `+` string coercion, ToInt32 bitwise lane,
ToBoolean truthiness, Number=binary64). JS-specific rules live in a policy
table, not in a parallel implementation. `JsAstNode = AstNode` makes this
mechanical: `build_js_ast.cpp` fills the same `Type*` slot with real
structural results instead of `FLOAT`/`ANY` stubs (TIG13/TIG14).

**TI3 (decision).** ANY becomes **auditable**: every remaining
ANY-assignment in both builders must name its reason (open param, open
map, dynamic name, explicit `any` annotation, or a numbered TIG deferral).
A debug counter per reason (dumped with the existing compile-timing
instrumentation) turns "where does ANY come from" from an archaeology
project into a report. The MIR emission ratchet (D8.6.1) extends with an
ANY-census per benchmark so TIG regressions show up in review.

## 4. TI4 — sys-func signatures: precise `T` and `T | error`

**Decision.** The registry's `return_type`/`success_type`/`may_return_error`
fields become load-bearing and audited:

- Every row gets a precise `success_type`. Math scalar funcs → `float`
  (their native lane already returns double — the static type saying ANY
  while the JIT proves double is exactly the M-series boxing tax);
  string funcs → `string`; `split` → `array(string)`; collection ops
  (slice/reverse/sort/unique/take/drop) → element-preserving: result type
  is a function of the FIRST ARG's type, expressed as a small
  `sys_func_success_result_type` rule (mechanism already exists for
  floor/ceil — extend the switch, or add a `result_kind` enum column:
  `SAME_AS_ARG0`, `ELEM_OF_ARG0`, `ARRAY_OF_ARG0_ELEM`, `FIXED`).
- Fallible funcs type as `T | error`, never ANY: `parse`/`input`/`fetch`/
  IO procs (`can_raise` already true — the union just isn't surfaced),
  numeric conversions `int()`/`float()`/`number()`/`decimal()` (domain
  errors), `min`/`max`/`sum`/`avg` over unproven collections.
  `sys_func_call_result_type` already builds the union when
  `may_return_error` is set — the flags are simply false/unset on rows
  that need them.
- Aggregates over PROVEN element types are exact: `sum(int[])` is
  `int | error` (saturation), `sum(float[])` is `float`; the first-arg
  rule covers this.
- The TE-15/TE-16 error machinery consumes these unions unchanged — a
  precise `T | error` makes `^`-propagation and `e ^ { … }` handlers
  *cheaper* to check, not stricter, because the error component is now
  visible instead of hiding inside ANY. Newly-visible error components
  make E208 fire immediately on the Lambda lane (§5.2) — the corpus files
  it rejects are genuinely mis-typed and are fixed in the same change.

## 5. Narrowing and binding

### 5.1 TI5 — flow narrowing in if/match (user rule 4)

Occurrence typing as a **private refinement** (D3.3.4 wording: "flow-
sensitive proofs may narrow privately but never change the public type"):

- `if (x is T)` → `x : T` in the then-branch, `x : typeof(x) \ T` in the
  else-branch where the subtraction is representable, else unchanged.
- `x == null` / `x != null` on `T?` → `null` / `T` per branch. This is
  T19-2's `map?` tag-test fix expressed at the type level.
- `match x { P1 => e1, … }` → inside each arm, `x` (when the scrutinee is
  a simple binding) narrows to the arm's pattern type; `match` and `is`
  share one membership model (D3.2.1), so the narrowing rule is the same
  `matches` relation read statically.
- Narrowings die at the join point; the binding's public type is untouched
  (D3.3.3's spirit: narrowing is scoped to the region that proved it).
- Implementation: a scope-stacked refinement map (binding → narrowed
  `Type*`) consulted by identifier lookup during Pass A; both engines use
  it (JS: `typeof x === "string"`, `x === null`, `instanceof` are the
  narrowing predicates; the predicate table is language policy per TI2).

### 5.2 TI6 — per-surface strictness; no transition staging (revised 2026-08-18)

The `:5683` comment records why unions were held back: making error-capable
branches visible to the E208 containment checker changed which programs
compile (95 corpus failures). D3.3.1v2/SI3v2 sanction that change outright.
Ruling (user decision 2026-08-18):

- **No warn-first staging.** When a precision improvement (TIG5/6/7/8
  unions, TI4 registry precision) makes a type error visible, the Lambda
  lane rejects it straightaway. The 95-failure class of corpus files is
  genuinely mis-typed; each precision slice fixes the scripts it exposes
  in the same change. The observable transition — "runtime soft error,
  possibly partial results" → "static error, no result" — is exactly the
  sanctioned static observability of SI3v2.
- **Strictness is a per-surface policy with a per-invocation override.**
  - **Lambda lane: strict by default.** Static type errors
    (E201/E204/E208/…) reject compilation; the user gets no result.
    Data-processing and scripting users want wrong programs stopped early.
  - **`lambda --static-warning` (relaxed mode, IMPLEMENTED 2026-08-18):**
    the same semantic (E2xx) diagnostics report as `warning[E…]` and the
    script runs. Parse/syntax errors are never downgraded. A diagnosed
    annotation is no contract: unresolved annotations and statically
    rejected declarations fall back to the initializer's inferred type, so
    representation never follows a failed contract (SI14 — the emitter
    must not reinterpret a string pointer as a declared int). Implemented
    in `record_as_static_warning` (build_ast.cpp), `err_print_warning`
    (lambda-error.cpp), runner warning block, `apply_common_mir_option`
    (main.cpp); gtests `StaticWarningFlagDowngradesSemanticErrorsAndRuns`,
    `ConceptualTypeNamesSuggestDefinedSyntax`.
  - **LambdaJS lane: lenient.** The same static findings are emitted as
    warnings only; the script still runs and produces a dynamic result
    that may contain error values. A browser page's user prefers a
    partial, error-carrying result over a blank page. This matches JS's
    own nature: the checker informs, the runtime decides.
  - The inference machinery is shared (TI1/TI2); only the *diagnostic
    disposition* differs — one severity flag at the reporting boundary,
    never two divergent checkers.

### 5.3 TI7 — binding types: stricter-of, incompatible = error (user rule 5)

Current behavior (`build_assign_expr:6023-6035`): annotation wins
unconditionally (`ast_node->type = annotation_type`); the init's inferred
type survives only in `as->type`. Ruling:

- **Incompatible** declared vs inferred (`subtype(inferred, declared)`
  fails AND `subtype(declared, inferred)` fails, per the D3.2.1 static
  relation): **compile-time error** — today's
  `check_declaration_static_boundary` already rejects the provable cases;
  it becomes uniformly strict for fully-known scalar pairs.
- **Compatible:** the binding *records both facts* (D3.2.3 already
  requires the AST to keep `declared_type` and `as->type` separate) and
  its **effective type for downstream inference is the stricter (meet)**:
  `let a: number = 1` reads as `int` at use sites while the `number`
  contract still governs writes (for `var`) and boundary checks. For
  `let` (immutable) the meet is always safe; for `var`, subsequent
  assignments re-check against the DECLARED type, and the effective type
  is the union of all assigned types meeted with the declaration —
  conservatively the declaration itself when the body reassigns
  (`param_reassigned` evidence already tracks this).
- For-loop variables: same rule — declared key/value annotations meet the
  source's element type (TIG10's structural answer feeds this).
- This is exactly the "consume the proof, keep the contract" split Tune18
  E1 implemented at the representation level; TI7 lifts it to the type
  level so every consumer benefits, not just `emit_checked_boundary`.

## 6. Boxing avoidance (user rule 3) — delegated, with one new rule

Representation is owned by `vibe/Lambda_Design_Compiling_Lane.md` and
Tune19 T19-1 (ValueRep on expression results). This doc adds one typing-
side guarantee that the lane work depends on:

**TI8 (decision).** *The static type may never be less precise than what a
sibling analysis proves.* Concretely: no consumer should need a whitelist
(`mir_expr_proves_native_return_lane`, `mir_native_int_bitwise_tree`, …)
to recover a fact the type system dropped to ANY. Each TIG fix deletes the
corresponding whitelist entry; the Tune19 §4 rule ("same facts ⇒ same
code") becomes achievable only because the facts survive in the `Type*`
graph. Success metric: the four whitelist helpers named in Tune19 §4 have
zero callers once TIG1/TIG4/TIG13/TIG14 land.

## 7. Implementation plan (ranked, separately land-able)

| Phase | Content | Gate |
|---|---|---|
| **P0** | TI3 ANY-audit instrumentation + ANY-census baseline on the 59-benchmark suite and js262 corpus | census report checked in; no behavior change |
| **P1** | TIG4/TI4 registry precision (mechanical, ~160 rows + first-arg rule) + TIG5 `and` + TIG6 comparisons + TIG11/TIG12/TIG16 one-liners | `make test-lambda-baseline` 100%; ANY-census drops; corpus scripts newly rejected by precise types are fixed in the same change (TI6: no staging) |
| **P2** | TIG1 indexed-read element types + TIG2 chained shape access + TIG15 destructure projection | mir-check fixtures: declared-`int[]` compare emits `blts` not double-lowering (T19-C fixture shape); bounce/fannkuch categorical-bar rows |
| **P3** | TI7 stricter-of binding + strict incompatibility errors | corpus sweep for newly-rejected programs reviewed before merge |
| **P4** | TI5 flow narrowing (Lambda first, JS predicates second) | narrowing fixtures per predicate; SI3v2 boxed-differential green |
| **P5** | TIG13/TIG14 JS structural pass (fill the `Type*` slots for real; retire per-emitter re-derivation as consumers convert) | js262 baseline 40,261/40,261; Result-N JS rows non-regressing |
| **P6** | TI1 step 5 closed-caller propagation subsumes `FnParamEvidence`; evidence pass demoted to open-function fallback | Tune19 T19-4 acceptance rows (untyped ray ≤0.6ms etc.) |
| **P7** | TIG7/TIG8 arm-join unions (largest E208 exposure) + LambdaJS warn-only reporting seam | corpus updated in-change; JS lane verified to run scripts that Lambda rejects |

Standing gates throughout: SI3v2 boxed-vs-JIT differential green (inference
stays unobservable), `make test262-baseline` fully green, emission ratchet
shrinks or holds (D8.6.1), no benchmark-source edits to dodge a compiler
gap (Tune19 non-goal restated).

## 8. Non-goals

- No change to the `a[i] : T?` public contract (D2.5.3) — TIG1 makes the
  element type precise *inside* the `?`, it does not remove totality.
- No inline caches in Lambda script (LC1); shape/class inference for JS
  stays within D3.4.7's immutable JsClassMeta rules.
- No new user-facing type syntax; this is inference precision, not surface.
- No Hindley-Milner unification/generalization — propagation stays
  directional (leaves up, calls across via closed-caller); TG-series
  generics (`vibe/Lambda_Design_Type_Generics.md` TG4 first-binds) remain
  their own design.
- `FnParamEvidence` is not deleted until P6 proves the structural pass
  covers its wins (pn-float-div, container-store witness) on the corpus.

## 9. Appendix — evaluation-invariance audit (2026-08-18, for SI3v2/D3.3.1v2)

The ruling revision keeps one absolute constraint: *a script with no type
error evaluates identically regardless of inference.* Audit of where
that holds and where it currently does not, on the v32 release binary:

**Harness state.** The sanctioned instrument is the T0-interpreter
differential (`test/interp/tier_sweep.py`; Makefile `test-lambda-interp`
calls a tier mismatch "a T0 bug by definition"). Fresh sweep: **330 match,
2 mismatch, 324 fallback** of 657 scripts.

**Live violations found (both filed):**
1. `test/lambda/proc/proc_dir_listing.ls` — interp drops `.extension`
   fields (T3/T8/T9 empty, T10 false) where JIT matches the golden.
2. `test/lambda/editor/oracle_poc.ls` — 333 parity comparisons print
   `false` under interp, `true` under JIT.
Both are T0 walker defects (JIT agrees with goldens); they violate the
invariant's letter and block using the harness as a clean gate until fixed.

**Coverage hole.** 324/657 scripts (49%) fall back to JIT under
LAMBDA_TIER=interp and are therefore *not* differentially checked at all.
The invariant is currently only ~half-audited by the harness; interp
coverage growth is the cheapest way to widen the guarantee.

**Spot probes that came back clean (scripts under `temp/infer_audit/`):**
- int53 saturation edge (`big+big` → `inf`) identical through untyped
  params, typed params, and literals; `div 0` → `inf`/`nan` per SI7 on
  both lanes; literal `div 0` is a static E312 on both.
- OOB reads total-null (S7.1/SI11) through typed `int[]`, untyped, and
  generic-call routes; negative index consistent.
- ArrayNum vs generic array `==` ties (SI1): `float[]`==untyped,
  `int[]`==untyped, cross int/float arrays all `true`.
- Error identity: `int("abc")` behaves identically via untyped/typed
  containment (`or`), `e == e` false (SI4 poison), falsy (SI9).
- Declared map contract vs untyped map: mutation visibility through a
  captured reference agreed (`99`/`99`) — no annotation-driven divergence
  in this shape (the historical cd2 reify-vs-alias split did not reproduce).

**Known-open adjacent items, correctly OUT of this invariant's scope:**
- Forced-GC JS divergences (unrooted native locals) — GC lifetime, not
  inference (separate D8.6.3 sweeps).
- `INT64_ERROR == INT64_MAX == INT_LANE_INF` collision — probes
  (`INT64_MAX div 1`, `5i64 div 0` → `decimal.inf`) showed no user-visible
  conflation, but the collision remains a latent hazard on the i64 lane
  (v5 migration §5.8 gate); any future emitter that interprets a
  legitimate `INT64_MAX` result as the error signal becomes an
  evaluation-invariance bug of exactly the class this appendix tracks.
- Static-side note: `: int64` is not Lambda syntax — `i64` is the defined
  annotation (user ruling 2026-08-18; `int64` is conceptual only). Rejecting
  it is correct; the only improvement worth making is the diagnostic, which
  currently reads `E201: cannot initialize 'm' of type error with int64`
  instead of an E204 undefined-type message naming `int64`.

**Historical violations, since fixed** (the class exists; the harness must
keep watching): havlak untyped wrong answer (boxed `_b` result fed to raw
native entry → inf sentinel, v27 ledger — passes on v32), Tune18 §8 cd
`any`-null tag-test over-proof, pn-param float-div int mis-inference, MIR
declared-type carrier bugs (2026-07-29).
