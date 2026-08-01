# Lambda / LambdaJS — Dual-Version Function Compiling

> **Status: DRAFT PROPOSAL.** Supersedes the dual-version half of
> [`Lambda_Box_Unbox.md`](Lambda_Box_Unbox.md) (C-transpiler era; the C path is
> frozen per CLAUDE rule 14). The type-checking-trampoline half remains valid and
> is retained here as the *declared-parameter* lane.
> Related: [`Lambda_Design_Compiling.md`](Lambda_Design_Compiling.md) (LC1: no
> inline caches in Lambda script — §10 multi-version dispatches via guard
> chain, never caches),
> [`Lambda_Design_Type_Enforcement.md`](Lambda_Design_Type_Enforcement.md) (TE-9 error-value returns),
> [`Lambda_Issue_Type_Support.md`](Lambda_Issue_Type_Support.md) (TS-1..TS-9),
> [`Lambda_Design_Item_Boxing.md`](Lambda_Design_Item_Boxing.md).

---

## 1. Goal

Compile **two versions** of every eligible user-defined function:

| Version | ABI | Role |
|---|---|---|
| **Unboxed** (`<name>`) | native MIR types for qualifying params, native return | fast path |
| **Boxed** (`<name>_b`) | all-`Item` params, `Item` return | the public/dynamic entry |

The boxed version is not a pure trampoline. It is:

```
guard: do the incoming Items match the unboxed version's parameter representations?
  yes → unbox, call the unboxed version, box the result, return   (fast path)
  no  → fall through into a full boxed lowering of the same body   (slow path)
```

Exactly **one** unboxed version per function in this design. Multi-version
specialization (one unboxed version per observed argument shape) is explicitly
future work — see §10.

### 1.1 The two cases from the request

- **Case A — untyped `fn f(a, b)`.** If ≥1 param is *inferred* to a determined
  unboxable type → emit unboxed + boxed. If none is → emit boxed only, and the
  boxed version has no guard and no unboxed callee (it *is* the body).
- **Case B — typed or partially typed `fn f(a: int, b)`.** Emit both by default.
  The boxed version exists for dynamic call sites whose argument types are not
  statically known.

### 1.2 Union-typed params

A param inferred (or declared) as a union — `int | string` — **does not qualify
for unboxing**. It stays `Item` in *both* versions. It contributes no guard test
and no representation constraint. This is a representation decision, not a
soundness one: `int|string` has no single native MIR type.

---

## 2. Current state (measured, not assumed)

### 2.1 Lambda / MIR Direct

`transpile_func_def()` ([transpile-mir.cpp:15150](../lambda/runtime/transpile-mir.cpp:15150))
emits **one body plus one wrapper**, never two bodies:

- Param types resolve declared-first, then inferred, into `resolved_param_types[16]`
  ([:15176–15219](../lambda/runtime/transpile-mir.cpp:15176)).
- `generate_native` gate ([:15221–15250](../lambda/runtime/transpile-mir.cpp:15221)):
  `!needs_task_context && !is_closure && !is_method && !is_variadic &&
  !has_typed_array_param`, **and** (≥1 param passes `mir_is_native_param_type`,
  **or** the inferred return type alone is a native scalar).
- The single body is `FN_ENTRY_NATIVE_BODY` when `generate_native`, else
  `FN_ENTRY_BOXED_BODY` ([:15450](../lambda/runtime/transpile-mir.cpp:15450)).
- `emit_boxed_abi_wrapper()` ([:14883](../lambda/runtime/transpile-mir.cpp:14883))
  runs for **every** function ([:16009](../lambda/runtime/transpile-mir.cpp:16009)),
  producing `<name>_b`. Its per-param sequence is: error short-circuit →
  `emit_optional_argument_value` → `emit_parameter_boundary` *(declared types
  only)* → `emit_unbox` → forward to the body.

**The structural gap.** `emit_unbox` ([:2153](../lambda/runtime/transpile-mir.cpp:2153))
is **coercive, not discriminating**: `it2i` / `it2d` / `it2b` / `it2s`. For a
param whose native type came from *inference* rather than declaration, there is
no boundary check in front of it. So a dynamic call `f("7")` against
`fn f(a) { a + 1 }` (a inferred `int`) is silently coerced instead of running
the boxed semantics of `+`. There is **no fallback body to fall back to.**

Direct call sites have the same shape ([:11039–11051](../lambda/runtime/transpile-mir.cpp:11039)):
a statically-`ANY` argument to a native param is `emit_unbox`-coerced; a
statically-mismatched argument is box-then-unbox-coerced.

**This is why inference is timid.** `resolve_inferred_type()`
([:14559–14580](../lambda/runtime/transpile-mir.cpp:14559)) carries an explicit
comment refusing to speculate `INT` on weak arithmetic evidence because that
guess "truncated float args at the call boundary (cd.ls positions/denominators)".
Without a fallback body, every inference must be *provably* right, so most
inference is abandoned. That conservatism is the measured cost recorded in
the measured `int`-widening ("flexint") poisoning of index arithmetic,
and TS-3.

### 2.2 LambdaJS

LambdaJS already emits **two full bodies**: `<name>` (boxed) and `<name>_n`
(native) ([js_mir_function_class_lowering.cpp:679–1035](../lambda/js/js_mir_function_class_lowering.cpp:679)).
Eligibility ([js_mir_module_batch_lowering.cpp:5829–5844](../lambda/js/js_mir_module_batch_lowering.cpp:5829)):
no captures, 1..16 simple params, no `arguments`, **all** params `INT|FLOAT`, and
return `INT|FLOAT`. Phase 1.76 revokes eligibility on contradicting call sites;
Phase 1.77 narrows still-`ANY` params from call-site evidence.

**The gap is the mirror image of Lambda's.** JS has both bodies but **no guard**:
the choice is made statically at the call site (`jm_call_direct_native`,
`jm_should_inline`). Every dynamic call — `js_call_function`, any `Function`
value, any method dispatch — lands in the boxed body and can never reach `_n`,
even when the runtime argument types match perfectly.

### 2.3 Summary of what this proposal changes

| | Lambda today | JS today | Proposed (both) |
|---|---|---|---|
| Unboxed body | yes (when native) | yes (`_n`) | yes |
| Boxed **body** | only when *not* native | always | always, when needed by DF3 |
| Guard in boxed entry | **none** (coerces) | **none** (never dispatches) | **yes** |
| Dynamic call reaches fast path | no (coerces, unsound) | no | yes, when types match |
| Inferred type must be provably right | **yes** | no (revocation) | **no** |

---

## 3. Decisions

### DF1 — Two entries, one guard, one fallback

Emit `<name>` (unboxed body) and `<name>_b` (boxed entry). `<name>_b` contains,
in order: the existing prologue (error short-circuit, optional defaults,
declared-type boundary checks), then a **single combined guard**, then either a
call to `<name>` or a full boxed lowering of the body.

Rejected alternative: per-param guards with partial coercion (unbox the params
that match, box the rest). That needs 2^N entry shapes or a coercion that
reintroduces the current unsoundness. Revisit only under §10.

### DF2 — The guard tests *inferred* params only

A param with a declared non-`any` type already passes through
`emit_parameter_boundary`, which raises on violation. After that check the
representation is known, so the guard test for that param is statically true and
is **not emitted**.

Consequence: **a fully-declared function needs no guard and no boxed body.** Its
`_b` stays exactly the thin trampoline it is today. All new machinery is paid for
only where inference is involved.

### DF3 — When is a boxed body emitted?

Emit a boxed body in `<name>_b` **iff** at least one of:

1. ≥1 param's unboxed representation came from **inference** (not declaration); or
2. the **return** representation came from inference and the function escapes; or
3. a declared param's contract admits values whose representations differ (see O3).

Otherwise `_b` remains a trampoline: guard is empty, the boundary checks already
guarantee the call is legal, and a violation is an error, not a fallback.

This rule is the whole cost-control story. Declared code pays nothing new.

### DF4 — Declared violation raises; inferred mismatch falls through

Two different failure semantics, never conflated:

- **Declared** `a: int`, argument is a string → contract violation → error value
  per TE-9 (`T|error`), via the existing `emit_parameter_boundary` /
  `emit_return_if_item_error` path. **Not** a fallback.
- **Inferred** `a` (unannotated), argument is a string → perfectly legal program
  → guard fails → boxed body runs, `+` dispatches dynamically.

### DF5 — Guard predicates are representation tests, not conversions

The guard asks "is this Item already in the unboxed version's representation?",
not "can this Item be converted?". Per-type tests:

| Unboxed type | Guard predicate | Cost |
|---|---|---|
| `int` | `(item >> 56) == LMD_TYPE_INT` | shift + cmp |
| `int64` | `(item >> 56) == LMD_TYPE_INT64` | shift + cmp |
| `uint64` | `(item >> 56) == LMD_TYPE_UINT64` | shift + cmp |
| `bool` | `(item >> 56) == LMD_TYPE_BOOL` | shift + cmp |
| `float` | `(item & ITEM_DBL_MASK) != 0` **or** `(item >> 56) == LMD_TYPE_FLOAT` | and + cmp, or shift + cmp |
| `string` | `(item >> 56) == LMD_TYPE_STRING` **only** — see below | shift + cmp |
| union / `any` | *(no test — stays Item in both versions)* | 0 |

`float` is deliberately two tests: Lambda floats are either self-tagged inline
doubles (`bits & ITEM_DBL_MASK`) or tagged pointers into a number home
([lambda.h:140](../lambda/lambda.h:140), [:1285](../lambda/lambda.h:1285)). A
single tag compare would miss the common inline case.

`string` deliberately does **not** reuse `emit_text_pointer_lane`
([:2206](../lambda/runtime/transpile-mir.cpp:2206)), which also admits `tag == 0`
— that is `LMD_TYPE_RAW_POINTER` ([lambda.h:91](../lambda/lambda.h:91)), an
emitter-internal lane that exists only where the emitter itself produced the
`String*`. A value arriving at the public boxed entry is always a tagged Item, so
admitting tag 0 there would accept any untagged word as a string. Guard on the
`STRING` tag alone.

The tests AND together into one branch. Every tag is statically asserted
non-double (`ITEM_TAG_IS_NON_DOUBLE`, [lambda.h:142](../lambda/lambda.h:142) and
the assert block at [:157–174](../lambda/lambda.h:157)), so the `float` mask test
cannot be spoofed by a tagged non-float. `ITEM_NULL` and `ITEM_ERROR` are plain
tags ([:1118](../lambda/lambda.h:1118), [:1133](../lambda/lambda.h:1133)) and
fail every predicate, routing to the boxed body — or, where
`mir_param_short_circuits_item_error` applies, they are short-circuited before
the guard is reached.

### DF6 — `int` → `float` widening is allowed in the guard; nothing else is

For a `float` param, an `int` Item passes the guard and is widened with a single
`I2D`. This is total and lossless within INT53 ([lambda.h:1256](../lambda/lambda.h:1256)),
and it is the single most common real mismatch (`f(1)` against a float-inferred
param).

**`int64`/`uint64` are explicitly *not* admitted to a `float` param.** They are
number-homed full-width values with no INT53 bound, so the widening is lossy
above 2^53 — the same divergence O1 describes, arriving by a different route.
They fail the guard and take the boxed path.

No other cross-type admission is permitted either: every remaining "conversion"
loses information (`float`→`int`) or costs a call (`string`→`int`), and both
belong on the boxed path where the body's own dynamic dispatch handles them
correctly.

### DF7 — Naming and ABI stay as they are

`<name>` = unboxed body, `<name>_b` = boxed public entry. This is already the
shape of the code, of `FnEntryKind` ([value_rep.h:43](../lambda/runtime/value_rep.h:43)),
of the `Function*` construction path ([:3547–3555](../lambda/runtime/transpile-mir.cpp:3547)),
of imports ([:3497](../lambda/runtime/transpile-mir.cpp:3497)), and of method
dispatch. **No renaming.** `_b` gains a body; nothing else moves.

The `FN_ENTRY_PUBLIC_WRAPPER` variant keeps its dynamic result contract
(`SCALAR_RETURN_DYNAMIC`) — correct and now load-bearing, since the boxed body
can return anything.

### DF8 — The check lives in the callee, not the call site

**Decision: a call site that cannot statically prove the argument types calls
`<name>_b` and lets the guard decide.** It does not emit its own check.

| Call site knows | Today | Proposed |
|---|---|---|
| all arg types match unboxed params | call `<name>` direct | unchanged |
| some arg statically `any` | coerce via `emit_unbox`, call `<name>` | **call `<name>_b`** |
| some arg statically mismatches an *inferred* param | box→unbox coerce, call `<name>` | **call `<name>_b`** |
| some arg statically mismatches a *declared* param | box→unbox coerce | error per DF4 / TE-9 |
| dynamic (`Function*`, import, method) | `<name>_b` | unchanged |

**Why not caller-side as the default.** The alternative is that each call site
emits its own guard, testing only the params it does not already know, and calls
the unboxed version on success. The site-specificity is real, but as a *default*
it loses on four counts:

1. **It does not replace the callee-side work, it adds to it.** A complete boxed
   version must exist regardless — for exports, `Function*` values, methods, and
   imports (DF15). So the callee-side guard is incremental cost ≈ 0 on top of
   code that is emitted anyway; the caller-side guard is pure addition,
   replicated once per call site.
2. **The saving is small and the cost is per-site.** A call site that proves
   *all* its argument types emits no guard under either policy — it calls the
   unboxed entry directly. Only partially-known sites differ, and the difference
   is (params the caller already knew) × ~2–3 instructions, once per call.
3. **Callee-side is likely faster than today at exactly these sites.** Today a
   statically-`any` argument to an `int` param emits a runtime **call** to
   `it2i` ([:11044](../lambda/runtime/transpile-mir.cpp:11044)); N unknown params
   means N calls. One call to `_b` plus N inline tag compares is fewer calls, not
   more.
4. **One guard site, one correctness argument.** DF5 has real subtleties — the
   two-test `float` predicate, the `string` tag-0 exclusion. Replicating that at
   every call site is N chances to get it wrong, and N places to fix when O1
   lands. It also gives DF9 a single object to test.

Code size is not a neutral concern here: Lambda JITs through MIR at runtime, so
per-site expansion is compile time and I-cache as well as bytes, and the project
already runs a 0%-slack MIR budget ratchet.

None of these four argues that caller-side checking is *wrong* — only that it is
the wrong default. It is retained as a scoped optimization in DF16.

### DF9 — Entry equivalence is the correctness invariant

> For every argument tuple that passes the guard,
> `unboxed(args)` must produce the same observable result as
> `boxed_body(args)`.

This is the one property the whole design rests on, and it is directly testable
(§8). Everything in O1–O4 is a threat to it.

### DF10 — Recursion

A self-call inside the unboxed body calls `<name>` directly (params are already
native, guard is statically true). A self-call inside the boxed body calls
`<name>_b`. Mutual recursion resolves the same way through the forward
declarations already created in `prepass_forward_declare`.

Consequence worth knowing: a recursive function entered with *matching* types
guards once and then recurses natively; entered with *mismatching* types it
re-guards at every level, since each recursive call re-enters `_b`. That is
correct but repetitive. It is not worth optimizing before measurement — a
recursion that stays on the boxed path is dominated by the boxed body's own
dynamic dispatch, not by the guard in front of it.

### DF11 — Scope exclusions for Stage 1

Unchanged from today's `generate_native` gate: closures, methods, variadics,
task-context procs, and typed-array (`ARRAY_NUM`) params get boxed-only. Closures
are the most valuable relaxation (captures are Items regardless of the param
ABI); deferred to keep Stage 1 measurable.

### DF12 — Inference may become speculative

This is the payoff. With DF4's fallback in place, `resolve_inferred_type()` no
longer needs to be provably right — only *usually* right. Specifically, the
refusal documented at [:14576–14579](../lambda/runtime/transpile-mir.cpp:14576)
("we no longer SPECULATE INT here") can be lifted, because the float argument
that motivated it now fails the guard and runs boxed instead of being truncated.

Proposed post-DF4 rule: weak arithmetic evidence with no contradicting call-site
evidence → speculate `INT`. Land this **as a separate, separately-measured
change** after the guard is green (§7, P4) — never in the same commit.

### DF13 — LambdaJS adopts the same shape

JS already has both bodies. The change is narrower: give the boxed body an entry
guard that forwards to `_n`, and relax Phase 1.76's *revocation* into a *guard*.
Today a single contradicting call site deletes `_n` for all call sites
([js_mir_module_batch_lowering.cpp:3119](../lambda/js/js_mir_module_batch_lowering.cpp:3119));
with a guard, the contradicting site simply fails the guard.

JS eligibility requires **all** params `INT|FLOAT`. That is stricter than
Lambda's "≥1 native param" and should be relaxed to match once the guard exists
(mixed native/Item param lists are exactly what a guard handles).

### DF14 — The unboxed version's precondition

The unboxed version **assumes** every param already matches its declared or
inferred representation. It performs no checks of its own. It may therefore be
entered through exactly two paths:

1. a call site that statically proved every argument type (DF8 row 1), or
2. the boxed entry's guard (DF5).

Any third entry path is a bug, not a slow path. This makes the guard the *sole*
enforcer of the precondition, and gives DF9 its precise statement: entry
equivalence is required only over argument tuples that satisfy the precondition —
which is exactly the set the guard admits.

### DF15 — Visibility decides which versions exist at all

Export/escape analysis is a bigger lever than the guard itself, and the data
already exists: `CallSiteEntry` records `has_call`, `escaped`, and joined
`arg_types[]` per param ([:352](../lambda/runtime/transpile-mir.cpp:352),
[:16297–16350](../lambda/runtime/transpile-mir.cpp:16297)). `escaped` is already
set for `is_public` (exported), variadic, closures, `FUNC_EXPR`, unnamed
functions, dispatched functions, and any reference outside direct-callee
position — i.e. it already means *"has callers this unit cannot see or cannot
type"*.

| | **not escaped** — every caller visible | **escaped** — exported / value / dynamic |
|---|---|---|
| **all params declared** | every site matches → **omit `<name>_b` entirely**; a mismatching site is a DF4 error at compile time | `_b` required. Per DF2 it is today's trampoline: boundary checks, unbox, call, box. No guard, no boxed body. |
| **any param inferred** | every site matches → the inference is **proven, not speculative** → no guard, no boxed body, `_b` omitted. No site matches → **omit the unboxed version**. Mixed → guard + boxed body. | Full DF1 shape: guard + boxed body. |

Two consequences worth stating separately:

- **Exported ⇒ the boxed version is mandatory.** There is no visibility into how
  an importer calls it, so `_b` is the contract. This is already true
  structurally — imports resolve to the `_b` symbol
  ([:3497](../lambda/runtime/transpile-mir.cpp:3497)).
- **Non-exported with all-matching call sites ⇒ speculation becomes proof.** This
  is the cell that pays for DF12: within a closed caller set, an inferred type
  that every call site agrees with needs no fallback at all. The fallback body is
  the price of *open* caller sets, and only of those.

DF15 subsumes the escape gate previously sketched in §5, and it is what makes the
DF8 answer cheap: the boxed version is not extra work the guard imposes, it is
work the export surface already requires.

### DF16 — Caller-side guards as a scoped optimization: hoisting, not duplication

**Accepted, but only in the form of guard hoisting.** The call site does not get
its own bespoke check. It gets permission to *move* the callee's guard outward
when that is provably legal, and the failure edge still lands on `_b`.

The canonical case is loop unswitching:

```
;; before — N trips through the boxed entry
for i in 0..n { total = total + scale(v, i) }     ; `v` unknown type, `i` known int

;; after — one guard, N direct calls
if guard_float(v) {
    vf = unbox_float(v)
    for i in 0..n { total = total + scale(vf, i) }    ; calls <scale> directly
} else {
    for i in 0..n { total = total + scale_b(v, i) }   ; unchanged DF8 path
}
```

**Legality condition:** every operand of the hoisted guard is invariant over the
region it is hoisted out of — i.e. the argument register is not reassigned inside
the loop, and nothing in the loop can change its representation. This is an
ordinary loop-invariance test, not a new analysis.

**Why this framing rather than "caller-side guards":**

- It is *one* guard per loop, not one per call site, so DF8's objection 1
  (replication) does not apply.
- The guard predicate is still DF5's, emitted by the same helper. DF8's
  objection 4 (N chances to get it wrong, N places to fix for O1) does not apply
  — there is still exactly one definition of the predicate.
- The failure edge is literally the DF8 path. The optimization can be disabled
  at any point and the program still compiles and runs.
- It generalizes for free: two calls to native-param functions passing the *same*
  value in straight-line code share one guard by ordinary CSE, provided the guard
  is emitted as a plain expression rather than as an opaque intrinsic. Emit it
  that way.

**Gating.** Off by default until P6; each enablement justified by benchmark, not
by inspection. Before reaching for it on a given site, check whether better
inference would make the argument statically typed instead — that removes the
guard entirely rather than relocating it, and is strictly better whenever it is
available.

**Interaction with DF14.** Hoisting introduces a third syntactic path into the
unboxed version, but not a third *semantic* one: the hoisted guard is the same
predicate, merely evaluated earlier. DF14's precondition is preserved exactly
when the legality condition holds — which is why the legality condition is about
representation-invariance, not just value-invariance.

### DF17 — Late compilation units: REPL, hot reload, and `eval`

DF15 elides `_b` on the strength of a closed caller set. Three things can add a
caller after the fact. They get three different answers.

**REPL and hot reload — out of scope.** Resolved by recompiling the whole script.
The brute-force answer is the correct one here: these are already whole-unit
operations, and no incremental win is worth making DF15's analysis defend against
them.

One invariant this relies on: **recompilation must regenerate both entries
consistently.** A cached module compiled under one elision decision must never be
partially reused alongside code compiled under another — that is O7's
cache-version bump, and DF17 is now a second reason to take it.

**Lambda script — not applicable.** There is no `eval` system function; the
dynamic surface is `Function*` values, imports, and dispatched methods, all of
which already set `escaped`. Lambda's P0.5 elision is therefore unblocked.

**JS direct `eval` — an escape condition.** Direct eval sees its enclosing scope
chain, so it is exactly "a caller the owning unit could not see". Rule: *a direct
eval at scope S marks escaped every function binding visible from S* — walk S's
scope chain outward and mark all of them.

`has_direct_eval` already exists ([js_mir_context.hpp:213](../lambda/js/js_mir_context.hpp:213))
and already drives `observes_this` and `uses_with`
([js_mir_analysis.cpp:1962–1965](../lambda/js/js_mir_analysis.cpp:1962)), so the
detection is free. **The implementation gotcha is direction:** the flag today
describes *the function containing the eval*, and that function is already
deopted. What DF17 needs is the opposite propagation — outward and sideways, to
the *siblings and enclosing bindings* the eval can name. A module-scope `helper()`
called from `eval("helper('a')")` inside some other function is the case, and
`helper` carries no flag of its own today.

Indirect eval (`(0,eval)(…)`, `new Function`) runs in global scope and can only
reach global/exported bindings, which are already escaped. It needs nothing.

**What eval-compiled code then does.** With `_b` guaranteed to exist, eval follows
DF8 like any other compilation unit. But it should also emit DF5's guard and call
the unboxed entry directly where it can — and this matters more in eval than
anywhere else, because **eval-compiled code can never satisfy DF8 row 1**: it has
no static type knowledge of the module bindings it names, so without
guard-emission it would take the boxed path unconditionally, forever. The guard is
the only route by which eval'd code reaches an unboxed entry at all.

Same two constraints as DF16: the predicate must come from the shared DF5 helper,
never be hand-rolled at the eval site; and the fail edge is `_b`.

**Explicitly rejected: unboxed-only functions reachable from eval, with the eval
site emitting a guard and no fallback.** The guard-fail edge would have no correct
destination. Raising is semantically wrong — a mismatching call to an
inference-typed param is a legal program (DF4) — and recompiling the owning
function on demand is more machinery than the escape condition costs. The escape
condition is a few bits set during collection; the alternative is a deopt engine.

---

## 4. Generated shape

`fn dist(x, y) { sqrt(x*x + y*y) }` — both params inferred `float`, return `float`.

```
;; unboxed body — no guard, no boxing
double dist(Context* rt, double _x, double _y) { ... }

;; boxed public entry
Item dist_b(Context* rt, Item _x, Item _y, ScalarHome* _home) {
    if (is_error(_x)) return _x;                       ; existing short-circuit
    if (is_error(_y)) return _y;
    ;; --- guard (DF5) ---
    ok  = (_x & ITEM_DBL_MASK) | ((_x >> 56) == FLOAT) | ((_x >> 56) == INT)
    ok &= (_y & ITEM_DBL_MASK) | ((_y >> 56) == FLOAT) | ((_y >> 56) == INT)
    if (!ok) goto boxed_body;
    ;; --- fast path ---
    return box_float(dist(rt, unbox_float(_x), unbox_float(_y)));
boxed_body:
    ;; --- full boxed lowering of `sqrt(x*x + y*y)` ---
    ...
}
```

For `fn dist(x: float, y: float)` — both declared — DF2/DF3 collapse this to
today's trampoline: boundary checks, unbox, call, box. No guard, no second body.

---

## 5. Cost model

**Fast path added cost:** 2–3 MIR instructions per inferred param, one branch,
fully predictable on monomorphic call sites. Replaces (for the statically-`any`
case) a runtime `it2i`/`it2d` **call**. Net win.

**Slow path added cost:** none relative to today's *correct* behaviour — today
there is no correct behaviour to compare against.

**Extra frame:** under DF8 a partially-typed call site now reaches the unboxed
body through `_b` rather than directly, costing one call/return. Against that it
saves N `it2i`/`it2s` runtime calls. Net-positive for N ≥ 1, which is every case
where the question arises; a zero-unknown-param site never goes through `_b` at
all.

**Code size:** the real cost. A boxed body is a second full lowering. Controls:

- DF2/DF3 exempt all fully-declared functions.
- **DF15 visibility elision** removes one version outright for most
  module-private helpers — and, in the all-matching case, removes the guard too.
- **Size gate:** above a body-node-count threshold, skip the unboxed version
  entirely rather than duplicate. Threshold to be set from measurement, not
  guessed.

MIR emission budgets (`test/mir/mir_budgets.json`, MT7 0%-slack ratchet, see
[`Lambda_Design_MIR_Emission_Test.md`](Lambda_Design_MIR_Emission_Test.md)) **will** move. Re-baselining is part of the work,
and the diff is the review artifact — an unexplained budget jump is the primary
signal that a gate is missing.

---

## 6. Open issues

### O1 — `int` overflow semantics diverge between versions *(pre-existing, now observable)*

`i2it` returns `ITEM_ERROR` when a value leaves the INT53 band
([lambda.h:1278](../lambda/lambda.h:1278)). A native `int64_t` add in the unboxed
body wraps silently. So for arguments near INT53, unboxed and boxed bodies
already disagree — today that is a latent bug; under DF9 it is a **visible
entry-equivalence violation**, because the same call can take either entry.

Options: (a) emit overflow checks in the unboxed body's `int` arithmetic —
correct, costs the win; (b) narrow the guard to a value-range test — costs 2 more
instructions per int param, still cheap, and preserves DF9; (c) accept and
document. **Recommend (b)**, measured. This is the same root as
the `int`-widening ("flexint") poisoning of index arithmetic, and must be
settled before DF12.

### O2 — Return-representation soundness under proven params

The unboxed version's native return type is inferred by `infer_return_type()`.
Under DF1 that inference now runs with *proven* param types, which makes it
strictly more sound than today. It is not automatically sound: a body whose
`int` arithmetic can widen still needs the O1 answer. Until O1 lands, keep the
return-type gate exactly as conservative as it is today.

### O3 — Does a declared-type boundary guarantee *representation*?

DF2 skips the guard for declared params on the assumption that
`emit_checked_boundary` guarantees not just type-compatibility but the exact
representation `emit_unbox` expects. If the validator accepts an `int64`-homed
Item for a declared `int` param (or an integral `float`), the assumption breaks
and DF3 clause 3 applies. **This must be verified against the validator's
`Type*` acceptance rules before DF2 is implemented** — it is the one place where
the design could be quietly wrong.

### O4 — GC rooting across the two lanes

Unboxed params are raw MIR values and are not GC roots — **except** `string`,
which is `VALUE_REP_RAW_GC_POINTER`. `FnParamAnalysis` already carries the
per-param `ValueRep` ([:14866–14872](../lambda/runtime/transpile-mir.cpp:14866)),
so the machinery exists; the new boxed body must publish its Item params as
roots in the side-root frame while the unboxed body must not. Getting this
backwards is a use-after-free, not a wrong answer. Treat the P3 forced-GC sweep
([`Lambda_Design_MIR_Emission_Test.md`](Lambda_Design_MIR_Emission_Test.md)) as a blocking gate, not a nice-to-have.

### O5 — Guard placement vs. optional params and defaults

The guard must sit **after** `emit_optional_argument_value` and the declared
boundary checks (a default value is itself typed and can feed the fast path) and
**before** `emit_unbox` — i.e. exactly at
[:15035](../lambda/runtime/transpile-mir.cpp:15035). An omitted optional param
with no default resolves to null, which fails every guard predicate; that is the
correct outcome (boxed body handles absence).

### O6 — Deopt signal

Optional: count guard failures per entry under a debug flag. A guard that always
fails means the unboxed version is dead weight and the inference was wrong — a
cheap, direct feedback signal for tuning DF12. Not required for correctness.

### O7 — MIR module cache interaction

The L1 module cache ([`Lambda_Design_MIR_Cache.md`](Lambda_Design_MIR_Cache.md)) keys on module content. Two entries
per function changes the emitted module but not the keying. Confirm no cached
module can carry a `_b` compiled under a different guard policy across a
transpiler change — a cache-version bump is the safe answer.

### O8 — `_b` is currently unconditional; DF15 makes it optional

`emit_boxed_abi_wrapper` runs for every function today
([:16009](../lambda/runtime/transpile-mir.cpp:16009)), so every `_b` lookup
succeeds. DF15 lets it be omitted, which means every lookup site must degrade to
the raw name. Some already do — the `Function*` path tests
`uses_wrapper = wrapper_item != NULL`
([:3550](../lambda/runtime/transpile-mir.cpp:3550)) — but the import path
([:3497](../lambda/runtime/transpile-mir.cpp:3497)), method dispatch
([:13743](../lambda/runtime/transpile-mir.cpp:13743)), the task-launch wrapper
([:15504](../lambda/runtime/transpile-mir.cpp:15504)), `main` resolution
([:17639](../lambda/runtime/transpile-mir.cpp:17639)) and export registration
([:17753](../lambda/runtime/transpile-mir.cpp:17753)) each need an audit before
omission is enabled.

Note that DF15 never omits `_b` for an *escaped* function, and every one of those
sites is reachable only from an escaped function — so the audit is expected to
confirm the omission is already safe rather than to find breakage. It is listed
because "expected to be safe" is not the same as verified, and a missing `_b` is
a link failure at JIT time, not a compile error.

### O9 — An async proc cannot host a second body inside `_b`

`emit_boxed_abi_wrapper` sets `mt->in_async_proc = false` deliberately, with the
comment that "the wrapper has no suspend/resume state of its own" and that
inheriting the raw async body's frame register would make wrapper parameter
stores refer to registers owned by a different MIR function
([:14938–14941](../lambda/runtime/transpile-mir.cpp:14938)). A boxed body
containing `await` needs its own state-machine dispatcher, its own
`async_state_labels`, and its own async frame — none of which `_b` has.

Note that `is_async_proc` (`analysis->may_await`) is a **different flag** from
`needs_task_context`, and only the latter appears in today's `generate_native`
gate ([:15233](../lambda/runtime/transpile-mir.cpp:15233)). So an awaiting proc
can be native-eligible right now.

Two ways out: (a) add `may_await` to DF11's exclusion list, so awaiting procs stay
boxed-only; or (b) emit the boxed body as a **separate** MIR function that `_b`
tail-calls, letting it own a full async frame. **Recommend (a) for Stage 1** — (b)
is the more general answer and is also what O14 would want, but it is a bigger
change than the guard itself and should not ride along with it.

### O10 — The two result paths in `_b` must agree on one protocol

**Scope note:** an earlier revision called this the most delicate piece of P2.
On reading the epilogue machinery that is an overstatement — the existing single
return funnel absorbs most of it. What is left is one function to verify and one
contract not to break. The structural item in P2 is O9, not this.

*The mechanism.* `_b` declares `RETURN_LANE_SCALAR` with
`MIR_SCALAR_RETURN_DYNAMIC` and receives a caller-donated `_scalar_home`, because
"the wrapper's side-number frame is about to be reclaimed; its result must
therefore be adopted by the caller-provided home"
([:15122](../lambda/runtime/transpile-mir.cpp:15122)). Every return funnels
through one point: `emit_function_return` writes `frame.return_reg` and jumps to
`frame.return_label` ([:977](../lambda/runtime/transpile-mir.cpp:977)), and
`finish_function_epilogue` emits that label once and rehomes via
`em_adopt_scalar_item` ([:1043](../lambda/runtime/transpile-mir.cpp:1043)).

*Two non-issues, recorded so they are not re-litigated.* A boxed body inlined
into `_b` contributes many return sites, but they all join that single funnel.
And the body's own `infer_boxed_return_mode` being narrower than `_b`'s `DYNAMIC`
is safe: `DYNAMIC` is the widest mode and discriminates at run time.

*What actually remains:*

1. **`_b` must keep its single-lane contract.** The native body may be
   `RETURN_LANE_ERROR`, reporting out-of-band through `Context.mir_return_lane`
   ([:1064](../lambda/runtime/transpile-mir.cpp:1064)); `_b` reads it and folds it
   into the returned Item
   ([:15090–15117](../lambda/runtime/transpile-mir.cpp:15090)). Under DF1 it will
   be tempting to give `_b` an error lane of its own so the native error can pass
   straight through. **Do not** — that is what makes two conventions collide. The
   fold is the design.
2. **`Context.mir_return_lane` is left dirty on the slow path.** A raising call
   inside the boxed body writes the lane; `_b` returns via the SCALAR path, which
   never touches it. Benign today, because only callers of a `RETURN_LANE_ERROR`
   function read the lane and `_b` is not one — which is exactly what item 1
   protects.
3. **Verify `em_adopt_scalar_item` handles the *unbound* case.** Path 1's value
   sits in a logical home `_b` allocated for the callee and recorded in the
   binding table ([:15119](../lambda/runtime/transpile-mir.cpp:15119)). Path 2's
   value may be a scalar the boxed body placed directly on the side-number frame
   with no home binding at all. Both must survive teardown. This is a read of one
   function, not a redesign.

Failure mode is still a wrong value rather than a crash, so it belongs in the DF9
test regardless of its reduced scope.

### O11 — `var` and proc params must be excluded from unboxing — verify

A `var` param borrows caller storage and writes back
(`is_var_param`, `cow_children_may_be_shared`,
[:15764–15768](../lambda/runtime/transpile-mir.cpp:15764); the call side takes a
`borrow_root`, [:11088](../lambda/runtime/transpile-mir.cpp:11088)). Plain `pn`
params are separately marked `is_proc_param` because "their typed container
writes must remain visible to the caller"
([:15762](../lambda/runtime/transpile-mir.cpp:15762)). Passing either as a raw
native scalar destroys the write-back.

Today's native gate filters by **type** only
([:15236](../lambda/runtime/transpile-mir.cpp:15236)) — nothing there inspects
`is_var_param`. So `pn f(var a: int)` appears to qualify. Determine whether that
is reachable today; if so it is a **pre-existing bug**, not one this design
introduces, and the fix (exclude `is_var_param`/`is_proc_param` from both
`generate_native` and the guard) belongs in P0 either way.

### O12 — Late callers vs. `escaped` — **RESOLVED, see DF17**

Closed. REPL and hot reload are out of scope (whole-script recompile); Lambda has
no `eval`, so P0.5 is unblocked; JS direct eval becomes an escape condition.

Residue, tracked into P5: implement the **outward** propagation. `has_direct_eval`
today marks the function *containing* the eval; DF17 needs every binding *visible
from* that scope marked instead. Getting the direction wrong looks like it works —
the containing function is deopted either way — while leaving exactly the sibling
bindings eval actually calls unprotected.

### O13 — "Observable result" in DF9 needs a definition

DF9 says the two entries must produce "the same observable result". Pin down what
that includes before writing the test, or the test will either fail on cosmetics
or be quietly weakened until it passes.

Proposed: equivalence **requires** equality of the returned value, and equality of
*whether* an error was raised, and equality of writes through `var`/proc params.
It **does not require** identical error message text (the boxed body's dynamic
dispatch will naturally word things differently from the native path), identical
container identity, or identical log output.

### O14 — A cold boxed body is still JIT-compiled

MIR generates code for the whole module, so a boxed body that never executes
still costs JIT time on every run — the cost lands on startup, where Lambda is
most sensitive. DF15 removes many of them and the size gate (§5) caps the rest,
but neither makes a *rarely-taken* body free.

Lazy generation of the slow path is the real answer and is out of scope here; it
belongs with the L2 cache work in
[`Lambda_Design_MIR_Cache_L3.md`](Lambda_Design_MIR_Cache_L3.md). Recorded so the
startup-time cost is not mistaken for a surprise later.

---

## 7. Implementation phases

| Phase | Scope | Exit |
|---|---|---|
| **P0** | Resolve **O3** (validator) and **O11** (`var`/proc params); audit the `_b` lookup sites (**O8**); add `is_inferred` per param alongside `resolved_param_types` | O3/O11 answered in writing; DF2 confirmed or DF3.3 activated; O8 audit complete |
| **P0.5** | **DF15 elision only, Lambda** — omit `_b` for non-escaped functions whose visible call sites all match. No guard, no second body, no semantic change. Unblocked: Lambda has no `eval` (DF17) | Pure code-size/compile-time win; baseline unchanged; budgets drop |
| **P1** | Emit the guard in `emit_boxed_abi_wrapper` for inferred params, slow path still today's coercion | No behaviour change intended; budgets move by the guard only; baseline green |
| **P2** | Replace the slow path with a real boxed body (DF1/DF3/DF4/DF14); settle **O10** (one result protocol) and **O9** (exclude `may_await`); apply the rest of DF15 and the size gate (§5) | Entry-equivalence tests (§8) green; budget diff explained gate-by-gate |
| **P3** | Settle **O1** (recommend range-narrowed guard); switch call-site policy per DF8 | No divergence in the O1 stress corpus |
| **P4** | Lift inference conservatism per DF12, measured separately | Result-suite geomean improves; no correctness regressions |
| **P5** | LambdaJS per DF13: guard in the boxed body, revocation → guard, relax all-params-native. Then DF17's direct-eval escape (outward propagation) before any JS-side elision, plus guard emission in the eval transpiler | `make node-baseline` no-regress; JS bench geomean improves; an eval-calls-module-private test exists and passes |
| **P6** | DF16 guard hoisting: loop-invariance test, unswitch, off by default behind a flag | Per-site benchmark justification; entry equivalence unchanged; flag flipped on only where measured |

P0.5 is worth landing on its own: it is a code-size and JIT-compile-time win that
depends on none of the guard machinery, and it exercises the DF15 analysis in
isolation where a mistake is a link failure rather than a wrong answer.

P1 and P2 are separately revertable — that separation is the point.

---

## 8. Test & gates

- **Entry equivalence (DF9, scoped by O13)** — the primary new test. For each
  dual-compiled function in the corpus, call it through both entries with the
  same arguments and diff the results. Argument sets must include: exact-match,
  `int`-for-`float`, deliberate mismatch, `null`, `error`, INT53 boundary values,
  and `int64` values above 2^53 (DF6's exclusion).
- `make test-lambda-baseline` — 100%, per CLAUDE.
- MIR emission budgets — re-baselined with a written justification per gate.
- P3 forced-GC sweep — blocking for O4.
- `make node-baseline` no-regress for P5.
- Perf: the existing Result-suite harness. Report Lambda and LJS geomeans
  separately; P4's delta must be attributable to DF12 alone.

---

## 9. What this does not do

- No multi-version specialization (one unboxed version only).
- No bespoke per-call-site checks. DF16 guard *hoisting* is accepted but is P6,
  flag-gated, and never emits a predicate other than DF5's.
- No runtime type feedback / OSR / tiering.
- No change to the frozen C2MIR path (CLAUDE rule 14).
- No closure, method, variadic, or typed-array unboxing (DF11).

---

## 10. Future: multiple unboxed versions

Once DF9 is enforced and O6 gives a failure signal, the natural extension is N
unboxed versions keyed by observed argument shape, with the boxed entry
dispatching through a small guard chain. The prerequisites are all in this
design: a representation-exact guard (DF5), a correct fallback (DF4), and an
equivalence invariant to test each new version against (DF9). Union-typed params
become the first candidate: `int | string` would get one `int` version and one
`string` version instead of staying boxed.

---

## 11. Prior art

None of the individual mechanisms in this document is new. Every one of them —
dual entry points, a representation guard, a boxed fallback body, annotations
that pin representation, speculation backed by a slow path — is settled practice
somewhere. What follows maps the precedents onto the decisions in §3, so that
each decision can be argued from someone else's measured experience rather than
from first principles, and so the known failure modes are named before we hit
them.

*(Version/status details below are from memory and worth re-checking before being
cited outside this document; the design points they illustrate are not in doubt.)*

### 11.1 Two entry points per function — DF1, DF7, DF8, DF15

| System | Fast entry | Public entry | How the version is chosen |
|---|---|---|---|
| **SBCL** (Common Lisp) | internal entry, "local call" convention | external entry point (XEP): arg-count + declared-type checks, then the same body | callee-side check; known call sites use the local convention and skip the XEP entirely |
| **Cython** | `cdef` C function | `cpdef` also emits a Python-callable wrapper that converts and forwards | caller's language decides statically |
| **Numba** | `nopython` compiled specialization | dispatcher object | runtime dispatch on observed argument types, with `object` mode as a real fallback |
| **Julia** | specialized method instance | generic entry / `invoke` | runtime dispatch on concrete argument types |
| **Static Python** (Meta Cinder) | typed bytecode with unboxed primitives | normal Python entry | boundary checks at the typed/untyped edge |
| **Static Hermes** (Meta, announced 2023) | native AOT from typed JS | untyped JS path | static where types are known |
| **Sorbet Compiler** (Stripe, experimental; not actively developed publicly) | LLVM-compiled `sig`-typed method | CRuby VM method | per-method fallback into the interpreter |
| **This design** | `<name>` | `<name>_b` | callee-side guard (DF8), fallback is a real body (DF1) |

Three things to take from the table.

1. **The callee-side check is the majority position.** SBCL's XEP and Numba's
   dispatcher both put the decision in one place per function rather than at each
   call site. DF8 lands where the field already is; DF16's caller-side work is
   correctly scoped down to *hoisting* an existing predicate rather than
   duplicating the decision.
2. **SBCL already does DF15.** A function whose calls are all local and visible
   gets no external entry point at all. That is the same analysis as P0.5, and
   it is the oldest and least controversial part of this proposal.
3. **Numba is the closest match to DF1's specific shape** — two-tier with a
   genuine second body (`object` mode), not a coercion. It is also a standing
   warning: users routinely hit silent `object`-mode fallback and lose all the
   speed with no diagnostic. That is exactly what O6 (deopt signal) is for.

LambdaJS's current state (§2.2) — two bodies, no guard, static call-site choice —
matches the *Cython* column, not the Numba one. DF13 moves it to the Numba shape.

### 11.2 Annotations as representation, not merely as checks — O3, TS-3

**Cython** (`cdef int x`), **mypyc**, **Julia**, and **Static Python** all treat an
annotation as a commitment about machine representation: the annotated value is
stored unboxed and operations on it compile to native instructions; unannotated
values remain the boxed universal representation. mypyc compiles mypy itself with
this model for a roughly 4× speedup.

This is the answer O3 is looking for, and these systems get it by *construction*:
the annotation is enforced at the untyped boundary, so inside the typed region the
representation is guaranteed and needs no further test. That is precisely the DF2
argument — a declared param needs no guard because the boundary already ran.

It is also the direction the measured TS-3 penalty points: today an `int[]` or
named-map annotation on a *local* installs a boundary without pinning a
representation, so it costs (4.24×) and buys nothing.

### 11.3 Speculation with a fallback — DF12, DF4, O6

- **Tracing JITs (PyPy, V8, LuaJIT)** are the canonical guard-and-deoptimize
  systems. Lambda's DF4 fall-through is deoptimization *without OSR*: the guard is
  at function entry and the fallback is a complete alternative body, so there is
  no mid-function state mapping to reconstruct. This is a real simplification, not
  a shortcut, and it is worth stating as such — the hard part of deopt is
  transferring live state at an arbitrary point, and DF1's entry-only guard
  structurally avoids it. The price is granularity: one bad argument runs the whole
  call boxed.
- **Julia's "type instability"** is the same phenomenon recorded in the flexint
  poisoning finding: one value the inferencer cannot pin degrades every
  downstream arithmetic operation to the boxed path. Julia's tooling answer
  (`@code_warntype`) is a diagnostic that shows where inference gave up — the same
  need as O6, approached from the tooling side rather than the runtime side.
- **Crystal** is the counterfactual: union types plus whole-program inference and
  *no* dynamic escape hatch, compiled to LLVM. It shows what DF1.2's union params
  could become under §10 (tagged-union representation with a dispatch, rather than
  staying boxed) — at the cost of the dynamic lane Lambda intends to keep.
- **Strongtalk** (Bracha & Griswold, OOPSLA 1993) originated the "optional types
  that do not affect runtime semantics" position. DF4 deliberately does *not* take
  that position for declared types (they raise), which is worth noting as a
  conscious divergence: Lambda's declared lane is enforced, its inferred lane is
  advisory.

### 11.4 What gradual typing costs — the boundary literature

The relevant results, all of which measure systems structurally similar to this
one:

- **Takikawa et al., *Is Sound Gradual Typing Dead?* (POPL 2016)** — measured every
  typed/untyped configuration of each benchmark rather than only the fully-typed
  and fully-untyped endpoints, and found order-of-magnitude slowdowns in
  *partially* typed configurations of Typed Racket. The cost tracks the number of
  typed/untyped **boundaries**, not the number of annotations.
- **Greenman & Felleisen, *A Spectrum of Type Soundness and Performance*
  (ICFP 2018)** — separates *deep* (guarded, contract-at-boundary) from *shallow*
  (transient, check-at-use) from *erased*, with the cost profile of each.
- **Vitousek et al., *Big Types in Little Runtime* (POPL 2017)** — Reticulated
  Python's transient semantics: cheap, shallow, pervasive checks instead of
  expensive boundary wrappers.
- **Kuhlenschmidt, Almahallawi & Siek, Grift (PLDI 2019)** — an ahead-of-time
  compiler for a gradually typed language emitting C/LLVM, built specifically to
  measure the cost of gradual typing at native-code speed, with space-efficient
  coercions. This is the closest academic analogue of this entire document.
- **Wrigstad et al., Thorn "like types" (POPL 2010)** — a third mode between
  `dyn` and concrete: checked at the boundary, usable by the compiler for
  representation, not deeply enforced.

**Where this design sits in that spectrum, stated explicitly:** DF2 + DF5 are a
*shallow/transient* discipline (a representation test at entry, no wrappers, no
blame tracking), and DF4 splits the two lanes — the declared lane is *deep-ish*
(the boundary raises, per TE-9), the inferred lane is transient-with-fallback.
Naming this matters because it selects which cost model applies: the Takikawa
wrapper-cost results are about the deep lane and largely do not apply to the
inferred lane, whereas the transient results (steady per-use overhead, no
catastrophic boundary blowup) do. It also predicts where a blowup *could* appear
here — at declared boundaries crossed in a loop, which is exactly what DF16's
hoisting is for.

### 11.5 Benchmark method worth borrowing — §8

The Takikawa configuration-lattice method is directly applicable to the `*2.ls`
typed corpus. Today those rows are graded as a per-row geomean of fully-typed vs
fully-untyped, which cannot distinguish "annotations helped" from "annotations
added a boundary that happened to be cheap here". Measuring a lattice of partial
typings for a handful of representative benchmarks would attribute cost to
boundaries directly, and would give P4 (DF12 inference lifting) a baseline that
is not confounded by annotation placement. Recorded as a suggested addition to
§8, not a gate.

### 11.6 What is not precedented

The combination, not the parts: union types plus optional annotations plus a
tagged 64-bit `Item` plus a self-hosted MIR JIT plus a document/data-processing
runtime, in one small toolchain. Crystal has the union inference; Julia has the
specialization and the instability problem; Cython and mypyc have the
representation pinning; none of them has all three.

One structural advantage is genuinely ours: **DF15 is unavailable to most of this
list.** Python, JavaScript, Ruby, and Lisp all have `eval` or open-world dispatch,
so a public entry can essentially never be proven dead — SBCL's local-call
elision applies only to lexically local functions, not to global `defun`s. Lambda
has no `eval` (DF17), which is why P0.5 can elide `_b` for module-private
functions outright. That is the cheapest win in this document and it is available
precisely because of a language decision made elsewhere.
