# Lambda / LambdaJS — Dual-Version Function Compiling

> **Status: IMPLEMENTED — Stage 1 core.** Lambda and LambdaJS now use the
> source-authoritative dual-entry plan described here: declared boundary
> admission, inferred fast/slow entries, and closed-world entry elision are
> implemented. Guard hoisting, lazy slow-body generation, and multi-version
> specialization remain deliberately future work.
>
> This supersedes the dual-version half of
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

## 1. Goal and semantic authority

The **user-defined source function is the semantic authority**. Its explicit
contracts, or their absence, determine which calls are legal and what the body
means. Neither generated entry is intrinsically authoritative: each is an
implementation of some part of the source function's domain.

An eligible function may have these two entries:

| Version | ABI | Role |
|---|---|---|
| **Unboxed** (`<name>`) | native MIR carriers for qualifying params; return ABI chosen independently | declared main body or inferred specialization |
| **Boxed** (`<name>_b`) | all-`Item` params, `Item` return | public checker/trampoline, or inferred dispatcher plus complete slow body |

Exactly **one** unboxed version per function is allowed in this design.
Multi-version specialization (one version per observed argument shape) is
future work — see §10.

### 1.1 Typed and untyped functions are different cases

| Source definition | Caller set | Generated shape | Meaning of a failed dynamic check |
|---|---|---|---|
| **typed**, native-eligible | closed and statically proven | unboxed main body; `_b` may be omitted | statically rejected call |
| **typed**, native-eligible | open/dynamic | unboxed main body + `_b` checking trampoline | declared-contract error; never a slow-body fallback |
| **untyped** | closed, with one proven profitable argument shape | unboxed specialization only; slow body and `_b` collapse | impossible in the compiled caller set |
| **untyped** | mixed or open, with a profitable shape | unboxed specialization + `_b` guard + complete boxed slow body | run the slow body; inference never creates a contract error |
| **untyped** | no profitable shape | boxed body only | ordinary dynamic execution |

A closed-world inferred shape does **not** become a user-visible contract. It
only proves that every caller reachable in the current compilation unit belongs
to the specialization domain. If a new caller can appear, the function is open
and needs the complete slow body; if a later compilation adds such a caller, the
unit is recompiled and the slow body is restored.

The closed untyped case therefore has the same *implementation shape* as the
typed case, but not the same failure semantics. An inferred function cannot
expose a checker that rejects arguments outside its specialization: if such an
argument can arrive, the function is open and needs the source-equivalent slow
body.

For a partially typed function, apply the rules per parameter. Declared params
use declared-boundary semantics; unannotated params remain eligible for an
inferred specialization and, when their caller set is open or mixed, require the
slow body. An unannotated param that remains boxed `Item` is not a specialization
and imposes neither a guard nor a slow body by itself.

### 1.2 Semantic type, carrier, and result domain are separate

Do not encode three different facts in one `TypeId`:

1. the parameter's **semantic type** (declared or exact inferred call shape);
2. the parameter's chosen **physical MIR carrier** (`I64`, `D`, raw pointer, or
   boxed `Item`); and
3. the body's **expression/result promotion domain**.

For example, an unannotated exact `int` argument remains semantic `int` at entry,
while an operation in the body may promote its result to `float`. Conversely, an
`int` admitted to a declared `float` parameter is normalized to the parameter's
`float` carrier before entry. Parameter specialization and return-ABI
specialization are independent decisions.

### 1.3 Union-typed params

A param inferred (or declared) as a union — `int | string` — **does not qualify
for unboxing**. It stays `Item` in *both* versions. It contributes no guard test
and no representation constraint. This is a representation decision, not a
soundness one: `int|string` has no single native MIR type.

---

## 2. Current state (measured, not assumed)

### 2.1 Lambda / MIR Direct

`transpile_func_def()` now makes a per-function native plan before lowering.
The plan records declared versus inferred-specialized parameters, the one raw
carrier shape, whether visible calls are closed, and whether a boxed entry and
slow body are necessary.

- A declared native function has one raw main body. Its optional `<name>_b`
  resolves defaults, performs declared implicit boundary admission and
  normalization, then calls the raw body. Failed admission returns the declared
  error; it never selects a slow body.
- An untyped function with one closed visible raw shape emits only `<name>`.
  An open or mixed shape emits `<name>` plus `<name>_b`; the boxed entry performs
  an exact inferred-shape predicate and lowers the complete original body on a
  failure.
- Direct calls use `<name>` only after every planned proof is statically known.
  All other calls use `_b`, so inference-derived unboxing is never coercive.
- `runtime_type_admit_value()` normalizes a successful concrete numeric
  admission through `lambda_numeric_boundary_admit`; an admitted `int` at a
  declared `float` boundary reaches the raw body as a float Item/carrier.
- Escaping through `Function*`, imports, exports, or `start` keeps `_b`
  available. `start(f, args)` is escaped because task dispatch invokes a public
  context ABI rather than the raw ABI.

`pn`/`var` params, closures, methods, variadics, typed arrays, and async/task
paths stay boxed-only in this stage. Inference retains a concrete candidate in
the presence of deferred calls, so an `int` fast path can coexist with a boxed
slow lane rather than being erased by an `any` caller.

### 2.2 LambdaJS

LambdaJS keeps its boxed body and optional `<name>_n` native body. The boxed
entry resolves defaults, combines exact predicates for specialized parameters,
calls `_n` on a match, and falls through to the complete existing boxed lowering
on a miss. Native eligibility accepts a mixed native/`Item` signature with at
least one `INT` or `FLOAT` parameter; `ANY` parameters remain boxed carriers.

Contradicting literal calls no longer globally revoke a specialization. Matching
direct calls still use the native path; an unmatched direct or `Function` call
uses the boxed result path. Native inlining is disabled for mixed signatures.
Function expressions passed as callbacks remain boxed-only: their dynamic
receiver/context is not part of the scalar raw ABI, so an argument-only predicate
cannot prove that call convention. JS retains a boxed entry even for closed
cases; direct-`eval` outward escape propagation is not a prerequisite for
correctness or entry elision in this implementation.

### 2.3 Summary of what this proposal changes

| | Lambda implementation | LambdaJS implementation |
|---|---|---|
| Unboxed body | raw `<name>` for a profitable plan | native `<name>_n` for a profitable plan |
| Full boxed lowering | boxed-only or inferred open/mixed | always retained |
| Guard in boxed entry | exact inferred shape; declared params use admission | exact inferred shape |
| Dynamic call reaches fast path | through `_b` after proof/admission | through the boxed entry after an exact match |
| Closed inferred plan | `_b` and slow body may be omitted | boxed entry is retained |

---

## 3. Decisions

### DF1 — One source meaning, plan-dependent entries

Version planning follows §1.1 rather than imposing the same two-entry shape on
every function. Where both entries are needed, emit `<name>` (unboxed) and
`<name>_b` (boxed public entry). Their roles depend on the source definition:

- for a typed function, `<name>` is the only main-body lowering and `_b` is a
  checking/normalizing trampoline;
- for an untyped open or mixed function, `_b` contains a combined inferred-shape
  guard, a fast call to `<name>`, and a complete boxed lowering on guard failure;
- for a closed untyped function whose callers prove one shape, `<name>` alone is
  a complete implementation for the reachable call domain and `_b` is omitted.

Rejected alternative: per-param partial fallback (unbox the params that match,
box the rest). That needs 2^N entry shapes or a conversion that silently changes
an untyped binding's semantic type. Revisit only under §10.

### DF2 — Declared params use admission and normalization, not a guard

A declared non-`any` param crosses the declared boundary before native entry.
The boundary applies DF6's implicit-admission rule and, on success, normalizes a
concrete numeric target into the target semantic/carrier representation. The
unboxed body may then rely on that representation without an inferred-shape
guard. Failure returns the declared-contract error.

Consequences:

- a fully declared native-eligible function has no boxed slow body;
- its `_b`, when required by visibility, is only error/default handling →
  declared admission/normalization → unbox → call → box;
- a declared contract that admits a differently represented numeric value does
  not require a slow body; normalization is the contract-authorized operation.

### DF3 — A slow body is emitted only for incomplete inference coverage

Emit a complete boxed slow body **iff** all of these are true:

1. at least one qualifying param is specialized from inference rather than a
   declaration;
2. an unboxed version is profitable; and
3. the caller set is open, deferred, or contains at least one visible call shape
   outside that unboxed specialization.

If every visible call to a non-escaped untyped function belongs to one inferred
shape, the slow body and `_b` collapse. If no visible call belongs to a
profitable single shape, omit the unboxed version and keep only the boxed body.
An inferred return representation alone never causes a slow body: there is no
entry-time predicate that could select it, and parameter and return ABIs are
planned independently.

### DF4 — Declared violation raises; inferred mismatch falls through

Two different failure semantics, never conflated:

- **Declared** `a: int`, argument is a string → contract violation → error value
  per TE-9 (`T|error`), via the existing `emit_parameter_boundary` /
  `emit_return_if_item_error` path. **Not** a fallback.
- **Inferred** `a` (unannotated), argument is a string → perfectly legal program
  → guard fails → boxed body runs, `+` dispatches dynamically.

### DF5 — Inferred guards are exact semantic-shape tests, not admissions

The guard asks "does this Item already have the semantic type specialized by the
unboxed version?", not "can this value be admitted or converted?" Inference is
not a user contract and must never change the semantic type of an unannotated
binding. Per-type tests:

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

### DF6 — One general numeric implicit-boundary-admission rule

This rule applies to **declared concrete numeric boundaries**, not inferred
guards. For source type/value `S` and declared destination `T`:

1. **Static source:** admit only when the entire domain of `S` exactly embeds in
   `T`; normalize into `T`'s semantic/native carrier.
2. **Static non-embedding source:** reject at compile time. In particular,
   `float → int` is never implicitly admitted statically, even when a particular
   expression might evaluate to an integral float.
3. **Dynamic source:** inspect the runtime numeric value. Admit exactly when that
   value is representable in `T` without information loss; normalize it to `T`.
   Otherwise return the rich boundary error.
4. **Explicit conversion functions** such as `int(x)` and `float(x)` retain their
   separately defined, possibly lossy semantics.

This is **implicit boundary admission**, not a cast: admission is exact and
contract-driven; an explicit conversion is a separate source operation.

Examples:

| Crossing | Result |
|---|---|
| static `int → float` | admit and normalize; every Lambda `int` is exactly representable |
| static `float → int` | reject |
| dynamic `3.0 → int` | admit as `int 3` |
| dynamic `3.5 → int` | boundary error |
| static `i64 → float` | reject; the whole `i64` domain does not embed |
| dynamic `i64(2^52) → float` | admit |
| dynamic `i64(2^53 + 1) → float` | boundary error |
| static `i8 → int`, `f32 → float`, `int → i64` | admit and normalize |

The implementation should use the shared numeric-kind lattice for static
domain embedding and one value-aware runtime helper for every numeric target.
Union and abstract numeric targets (`number`, `integer`) do not arbitrarily pick
a concrete member carrier; they remain boxed unless separately specialized.

### DF7 — Naming and ABI stay as they are

`<name>` = unboxed body, `<name>_b` = boxed public entry. This is already the
shape of the code, of `FnEntryKind` ([value_rep.h:43](../lambda/runtime/value_rep.h:43)),
of the `Function*` construction path ([:3547–3555](../lambda/runtime/transpile-mir.cpp:3547)),
of imports ([:3497](../lambda/runtime/transpile-mir.cpp:3497)), and of method
dispatch. **No renaming.** `_b` gains a slow body only in DF3's inferred
open/mixed case; typed functions retain the checking-trampoline shape.

The `FN_ENTRY_PUBLIC_WRAPPER` variant keeps its dynamic result contract
(`SCALAR_RETURN_DYNAMIC`) — correct and load-bearing whenever the boxed slow body
can return any source-defined value.

### DF8 — The check lives in the callee, not the call site

**Decision: a call site that cannot statically prove the required declared
admission or inferred specialization calls `<name>_b`.** The callee performs the
one required boundary/guard operation; the caller does not duplicate it.

| Call site knows | Today | Proposed |
|---|---|---|
| declared params are statically admitted | call `<name>` direct, sometimes by coercion | apply DF6 normalization, call `<name>` direct |
| a declared param is statically rejected | box→unbox coerce | compile-time contract error |
| a declared param is deferred (`any`) | unchecked unbox | call checking trampoline `<name>_b` |
| inferred args exactly match the specialization shape | call `<name>` direct | unchanged |
| inferred args are outside or deferred relative to the specialization | box→unbox coerce | call `<name>_b`; its slow body preserves source semantics |
| dynamic (`Function*`, import, method) | `<name>_b` | call `<name>_b`; it checks declared params and/or dispatches inferred ones |

The rows compose across parameters. A direct unboxed call is legal only when
every declared param is statically admitted and every inferred-specialized param
has the exact planned semantic type. An unannotated param deliberately retained
as `Item` needs no proof.

**Why not caller-side as the default.** The alternative is that each call site
emits its own guard, testing only the params it does not already know, and calls
the unboxed version on success. The site-specificity is real, but as a *default*
it loses on four counts:

1. **It does not replace the callee-side work for an open function, it adds to
   it.** A boxed public entry must exist for exports, `Function*` values,
   methods, and imports (DF15). For an inferred open function that entry also
   needs the complete slow body. A caller-side guard is therefore pure
   duplication, replicated once per site.
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
   every call site is N chances to get it wrong and N places to maintain. It
   also gives DF9 a single specialization predicate to test.

Code size is not a neutral concern here: Lambda JITs through MIR at runtime, so
per-site expansion is compile time and I-cache as well as bytes, and the project
already runs a 0%-slack MIR budget ratchet.

None of these four argues that caller-side checking is *wrong* — only that it is
the wrong default. It is retained as a scoped optimization in DF16.

### DF9 — Source-relative correctness is the invariant

The source function and formal semantics are the oracle. Generated entries have
three separate obligations:

1. **Declaration-driven main body:** after declared admission/normalization,
   `<name>` has the same observable behavior as the source function under its
   declared contracts.
2. **Complete slow body:** for every argument tuple, including declared-contract
   failures, the boxed lowering has the same observable behavior as the source
   function and its declared boundaries.
3. **Inferred specialization:** for every tuple satisfying all declared
   admissions and DF5's exact inferred shape, `<name>` has the same observable
   behavior as the source function.

Comparing the two generated bodies over the guard-passing intersection remains
a useful differential test, but neither body defines the other's semantics.
Guard-failing inputs are tested only against the slow-body/source obligation;
they must never be passed to the unboxed entry. O1–O5 and O10 threaten these
properties.

### DF10 — Recursion

A self-call inside the unboxed body calls `<name>` directly (params are already
native and satisfy its precondition). A self-call inside an untyped boxed slow
body calls `<name>_b`. Mutual recursion resolves the same way through the
forward declarations already created in `prepass_forward_declare`.

For an inferred open function, a call entered with the specialized shape guards
once and then recurses natively; a call on the slow path re-guards at every
level, since each recursive call re-enters `_b`. That is correct but repetitive.
It is not worth optimizing before measurement — a recursion that stays on the
boxed path is dominated by the boxed body's own dynamic dispatch.

### DF11 — Scope exclusions for Stage 1

Unchanged from today's `generate_native` gate: closures, methods, variadics,
task-context procs, and typed-array (`ARRAY_NUM`) params get boxed-only. Closures
are the most valuable relaxation (captures are Items regardless of the param
ABI); deferred to keep Stage 1 measurable.

### DF12 — Inference may become speculative

This is the payoff for an open/mixed untyped function. With DF4's complete slow
body, the specialization shape need only be profitable, not universally true:
an argument of another semantic type fails the exact guard and runs the source-
equivalent boxed lowering instead of being truncated.

Inference must, however, keep two products separate:

- **entry-shape inference** determines the exact semantic types accepted by the
  specialization and is driven primarily by caller shapes;
- **body/result inference** chooses operation and return promotion domains and
  may use body evidence.

Using a param in float arithmetic does not by itself change an unannotated
`int` argument into semantic `float`; the param can retain semantic `int` while
the operation result promotes. A physical `D` carrier is likewise not evidence
that the binding's semantic type is float.

The refusal documented at
[:14576–14579](../lambda/runtime/transpile-mir.cpp:14576) ("we no longer
SPECULATE INT here") may be revisited only after this separation and the slow
path are green. Land the policy change as a separate, separately measured phase.

### DF13 — LambdaJS adopts the same planning matrix

Implemented: for an open/mixed inferred function, the boxed body has an entry
guard that forwards to `_n`; a miss falls through to the complete boxed body.
Contradicting literal calls no longer revoke `_n` globally, so they retain a
direct fast path for matching calls and use the boxed path otherwise. As in
Lambda, inferred JS types select an implementation; they do not create a source
contract.

Eligibility now permits mixed native/`Item` parameter lists, provided at least
one parameter is `INT` or `FLOAT` and the return is numeric. The `Item` formals
are passed through unchanged and are not inlined or TCO-specialized. Callback
function expressions remain a deliberate boxed-only exclusion because their
dynamic receiver/context is not represented in the raw ABI.

JS currently retains the boxed entry rather than applying Lambda's closed-world
elision. This makes `eval` and other late callers safe without a separate outward
escape propagation pass; that pass is required before any future JS elision.

### DF14 — The unboxed version's precondition

The unboxed version **assumes** every param already has its planned semantic
type and physical carrier. It performs no checks of its own. It may therefore be
entered through exactly two proof-producing paths:

1. a call site that statically proved the inferred shape and completed every
   required declared DF6 admission/normalization; or
2. `_b`, after declared admission/normalization for typed params and the DF5
   exact guard for inferred params.

Any third entry path is a bug, not a slow path. DF5 is the sole dynamic proof for
an inferred specialization; DF6 is the declared admission/normalization rule.
DF9's specialization obligation applies only to tuples satisfying this
precondition.

### DF15 — Visibility decides which versions exist at all

Export/escape analysis is a bigger lever than the guard itself, and the data
already exists: `CallSiteEntry` records `has_call`, `escaped`, and joined
`arg_types[]` per param ([:352](../lambda/runtime/transpile-mir.cpp:352),
[:16297–16350](../lambda/runtime/transpile-mir.cpp:16297)). `escaped` is already
set for `is_public` (exported), variadic, closures, `FUNC_EXPR`, unnamed
functions, dispatched functions, and any reference outside direct-callee
position — i.e. it already means *"has callers this unit cannot see or cannot
type"*.

Use TE-2's three-way call-site classification rather than collapsing it to
"match/mismatch":

- **statically admitted** — every declared boundary admission and inferred
  shape is proven;
- **statically rejected** — a declared contract error; compilation fails and
  this call contributes no runtime entry requirement;
- **deferred/outside specialization** — needs `_b`.

| Function plan | **not escaped** — every caller visible | **escaped** — exported / value / dynamic |
|---|---|---|
| **native plan uses declarations only** (other params may remain `Item`) | all declared boundaries admitted → omit `_b`; any deferred boundary → keep checking trampoline | `_b` checking trampoline required; no guard or slow body |
| **native plan includes ≥1 inferred-specialized param** | every site admitted and in exact shape → unboxed only; mixed/deferred → guard + complete slow body | guard + complete slow body required |
| **no profitable native plan** | boxed body only | boxed body only |

Two consequences worth stating separately:

- **Exported ⇒ the boxed version is mandatory.** There is no visibility into how
  an importer calls it, so `_b` is the contract. This is already true
  structurally — imports resolve to the `_b` symbol
  ([:3497](../lambda/runtime/transpile-mir.cpp:3497)).
- **Non-exported with one exact visible call shape ⇒ the specialization domain
  covers the reachable domain.** This is the cell that pays for DF12 without
  turning inference into a contract. It needs no slow body because no reachable
  call lies outside the shape; recompilation restores the slow body if that fact
  changes.

DF15 subsumes the escape gate previously sketched in §5, and it is what makes the
DF8 answer cheap for open functions: the boxed public entry is work the export
surface already requires, while closed functions can elide it entirely.

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
  objection 4 (N chances to get it wrong, N places to maintain) does not apply
  — there is still exactly one definition of the predicate.
- The failure edge is literally the DF8 path. The optimization can be disabled
  at any point and the program still compiles and runs.
- It generalizes for free: two calls to native-param functions passing the *same*
  value in straight-line code share one guard by ordinary CSE, provided the guard
  is emitted as a plain expression rather than as an opaque intrinsic. Emit it
  that way.

**Gating.** Off by default until P7; each enablement justified by benchmark, not
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

One invariant this relies on: **recompilation regenerates the whole module.** The
runtime cache retains imports only within one `Runtime`, invalidates a changed
source/dependent cone, and persists no compiled artifact across processes. There
is therefore no cross-version compiler artifact that needs a cache-key bump.

**Lambda script — not applicable.** There is no `eval` system function; the
dynamic surface is `Function*` values, imports, and dispatched methods, all of
which already set `escaped`. Lambda's P0.5 elision is therefore unblocked.

**JS direct `eval` — future escape condition for elision.** Direct eval sees its
enclosing scope chain, so it is exactly "a caller the owning unit could not see".
Before JS elides a boxed entry, a direct eval at scope S must mark escaped every
function binding visible from S by walking the scope chain outward.

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

**Current behavior.** JS retains `_b`, so eval follows DF8 like any other dynamic
compilation unit and has a correct boxed destination. The callee-side guard is
already the route from eval to the unboxed specialization; eval needs no bespoke
caller-side predicate.

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
    ok  = (_x & ITEM_DBL_MASK) | ((_x >> 56) == FLOAT)
    ok &= (_y & ITEM_DBL_MASK) | ((_y >> 56) == FLOAT)
    if (!ok) goto boxed_body;
    ;; --- fast path ---
    return box_float(dist(rt, unbox_float(_x), unbox_float(_y)));
boxed_body:
    ;; --- full boxed lowering of `sqrt(x*x + y*y)` ---
    ...
}
```

For `fn dist(x: float, y: float)` — both declared — DF2/DF3 collapse this to a
checking trampoline: resolve defaults, apply DF6 admission and normalization,
unbox, call, box. An incoming `int` is statically or dynamically normalized to
declared `float`; a rejected value returns the contract error. There is no guard
and no second body.

---

## 5. Cost model

**Dynamic inferred fast-path cost:** 2–3 MIR instructions per specialized param,
one branch, and one extra call through `_b`. The branch should be predictable on
monomorphic call sites and replaces unchecked `it2i`/`it2d` calls, but the net
effect is a benchmark question, not an asserted win.

**Slow-path execution cost:** one failed guard and branch before the boxed
lowering, relative to a boxed-only function. There is no meaningful comparison
with today's mismatched inferred-native path because that path does not preserve
the source semantics.

**Extra frame:** under DF8 a partially-typed call site now reaches the unboxed
body through `_b` rather than directly, costing one call/return. Against that it
saves N `it2i`/`it2s` runtime calls. `_b` also owns root/number-frame prologue and
epilogue work, so even N = 1 is not presumed positive. A zero-unknown-param site
never goes through `_b` at all.

**Code size:** the real cost. A boxed body is a second full lowering. Controls:

- DF2/DF3 exempt fully declared functions from body duplication.
- **DF15 visibility elision** removes one version outright for most
  module-private helpers — and, in the all-matching case, removes the guard too.
- **Size gate:** for an inferred open/mixed function above a body-node-count
  threshold, skip the unboxed specialization entirely rather than duplicate.
  It does not apply to a declared unboxed body plus thin trampoline. Threshold
  to be set from measurement, not guessed.

MIR emission budgets (`test/mir/mir_budgets.json`, MT7 0%-slack ratchet, see
[`Lambda_Design_MIR_Emission_Test.md`](Lambda_Design_MIR_Emission_Test.md)) **will** move. Re-baselining is part of the work,
and the diff is the review artifact — an unexplained budget jump is the primary
signal that a gate is missing.

---

## 6. Open issues

### O1 — Promotion-aware numeric inference and lowering

**Implemented Stage 1 boundary.** Flex `int` is handled by the existing numeric
operation promotion rules, not by a special function-boundary conversion. The
dual-entry planner specializes only the proven `int`/`float` input shape; sized,
decimal, union, and otherwise ambiguous numeric candidates remain boxed instead
of forcing an unsafe raw carrier. This preserves the standard promotion result
while keeping entry-shape inference separate from body/result inference.

Flex `int` is not a function-boundary special case. Like every other numeric
type, it participates in the numeric promotion lattice: the operation selects
its result domain, and that domain may differ from the operands' domains. In
particular, `int` arithmetic promotes to `float` when the result leaves INT53.
The original risk was that `i2it` could return `ITEM_ERROR` outside that band
([lambda.h:1278](../lambda/lambda.h:1278)), while a raw native `int64_t`
operation used as a flex-`int` carrier may wrap. Neither behavior implements the
source rule.

This is not solved by narrowing the entry guard: an input-only range check cannot
bound arbitrary body arithmetic. The required fix is shared promotion-aware
inference **and lowering**:

- infer the possible result domain of each numeric operation, including
  value-dependent promotion;
- choose a native return only when that result domain has one proven carrier;
- otherwise allow native params with a boxed/dynamic return;
- emit the operation checks/conversions needed to produce the promoted value,
  never native wrap or `ITEM_ERROR` as an accidental overflow policy.

The entry guard remains an exact input-shape test; it does not attempt to predict
body overflow. Stage 1 keeps a candidate boxed when no one raw carrier is proven.

### O2 — Parameter and return specialization must be independent

**Resolved.** The native plan records parameter specialization independently from
the inferred return carrier. A return-only inference never creates a raw entry or
a slow body, and an open inferred function routes only on its parameter shape.

`infer_return_type()` currently encourages the emitter to treat "native params"
and "native return" as one version decision. They are independent. A function
may profitably receive raw params while returning an `Item` because its result
is a union, a dynamic call result, or a promoted numeric value.

An inferred return alone never justifies a second body (DF3). If the result
carrier is not proven, keep the unboxed body's return dynamic or omit the
unboxed version; do not emit an unreachable slow body or replay an effectful
body after observing its result.

### O3 — Declared numeric normalization is required — **RESOLVED**

`runtime_type_admit_value()` now routes every successful concrete numeric
boundary through `lambda_numeric_boundary_admit` before membership is accepted.
The resulting Item is rebound and rooted before raw unboxing. Thus a dynamic or
statically embedded `int` admitted by `float` reaches a declared raw float
parameter as a float, while static `float → int` is rejected and dynamic values
must be exactly representable.

### O4 — GC rooting across the two lanes

**Verified.** The slow lane uses the ordinary boxed lowering and finalized
parameters are rebound before it can allocate. The 25-case forced-GC MIR sweep,
including the new dual-entry cases, passes.

The boxed lane must publish every finalized Item parameter — after defaults and
declared normalization — in its side-root frame. Native scalar params are not
Item roots. A native `string` param is different: it has
`VALUE_REP_RAW_GC_POINTER` and needs an explicit lifetime proof through a live
caller Item root or an equivalent precise callee root. `FnParamAnalysis` already
carries the per-param `ValueRep`
([:14866–14872](../lambda/runtime/transpile-mir.cpp:14866)), so the machinery
exists. Getting the lane or finalized binding wrong is a use-after-free, not a
wrong answer. Treat the forced-GC sweep
([`Lambda_Design_MIR_Emission_Test.md`](Lambda_Design_MIR_Emission_Test.md)) as a blocking gate, not a nice-to-have.

### O5 — Guard placement vs. optional params and defaults

**Implemented.** Defaults and declared admission run before the combined inferred
guard; guard failure branches before any raw unboxing and enters the complete
boxed lowering.

The inferred guard must sit **after** `emit_optional_argument_value`, every
declared DF6 admission/normalization, and rebinding/rooting of the finalized
Items, but **before** native unboxing. A default value is itself a source value
and may feed the fast path. An omitted untyped optional param with no default
resolves to null, which fails every specialized predicate; the boxed slow body
handles absence. A declared optional instead follows its declared nullable
contract and never uses an inferred fallback to excuse a violation.

### O6 — Deopt signal

Optional: count guard failures per entry under a debug flag. A guard that always
fails means the unboxed specialization is dead weight for the observed workload
— a cheap, direct feedback signal for tuning DF12. It does not mean the source
program or inference contract was "wrong"; no such contract exists. Not required
for correctness.

### O7 — MIR module cache interaction

**Resolved.** The L1 cache retains only imported `Script`s inside one `Runtime`;
changed source invalidates its dependent cone, and no compiled artifact survives
across processes. A compiler-version cache-key bump is therefore unnecessary.

### O8 — `_b` is currently unconditional; DF15 makes it optional

**Resolved.** The audit covers direct calls, `Function*`, imports/exports, task
launch, main resolution, and `start`. Escaped call paths retain `_b`; non-escaped
closed plans can use the raw entry alone. The `start` syntax is explicitly marked
escaped because its task dispatcher uses the public context ABI.

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

**Resolved for Stage 1.** Async/task procedures, `pn`, and `var` parameters are
excluded from raw specializations and stay boxed-only. A separate async slow-body
state machine is future work, not a hidden ABI exception.

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

**Verified.** The raw and boxed slow returns both use the existing dynamic boxed
return funnel; forced-GC and scalar-home MIR probes pass.

**Scope note:** an earlier revision called this the most delicate piece of P3.
On reading the epilogue machinery that is an overstatement — the existing single
return funnel absorbs most of it. What is left is one function to verify and one
contract not to break. The structural item in P3 is O9, not this.

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

### O11 — `var` and proc params must be excluded from unboxing — **RESOLVED**

A `var` param borrows caller storage and writes back
(`is_var_param`, `cow_children_may_be_shared`,
[:15764–15768](../lambda/runtime/transpile-mir.cpp:15764); the call side takes a
`borrow_root`, [:11088](../lambda/runtime/transpile-mir.cpp:11088)). Plain `pn`
params are separately marked `is_proc_param` because "their typed container
writes must remain visible to the caller"
([:15762](../lambda/runtime/transpile-mir.cpp:15762)). Passing either as a raw
native scalar destroys the write-back.

The raw-native gate now rejects every `pn`/`var` parameter. This preserves
borrow/write-back semantics and prevents a raw carrier from replacing a caller
owned `Item` location.

### O12 — Late callers vs. `escaped` — **RESOLVED, see DF17**

Closed for Lambda: REPL/hot reload recompile whole scripts, and Lambda has no
`eval`. LambdaJS intentionally retains boxed entries, so direct eval has a boxed
destination. Outward direct-eval escape propagation remains a prerequisite only
for future JS entry elision.

### O13 — "Observable behavior" in DF9 needs a testable definition

**Applied by the new coverage.** Dual-entry tests assert value and `type()` on
both the raw and slow lanes; the static float-to-int rejection test asserts the
declared error channel.

Plain Lambda `==` is too weak as the oracle: numerically equal values can have
different semantic types, and `type()` can observe that difference. Source-
relative equivalence requires:

- the same returned semantic type and value;
- the same value-vs-raised error channel and the same observable error payload
  (code/category, expected/actual data, and user-visible message fields; stack
  addresses or compiler-internal locations may differ);
- the same writes through `var`/proc params and the same other language-visible
  proc effects.

It does not require identical physical boxing, number-home address, COW sharing,
or internal debug log output. Container identity is not an observable value
property under Lambda's no-alias semantics; container contents and visible
mutation effects are.

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
| **P0** | **Complete.** Per-parameter declared/inferred planning, numeric boundary normalization, `pn`/`var` exclusion, and `_b` lookup audit | Target-normalized concrete numeric Items; escaped public ABIs retain `_b` |
| **P0.5** | **Complete.** Lambda DF15 elision for closed declaration and inferred plans | Closed raw-only MIR fixtures; deferred/escaped callers retain `_b` |
| **P1** | **Complete for Stage 1.** Entry shape is distinct from the existing numeric promotion result domain; unsafe carrier candidates remain boxed | Flex-`int` promotion corpus and raw-boundary tests pass |
| **P2** | **Complete.** Exact inferred-shape predicate | `int` and `float` checks never cross-admit |
| **P3** | **Complete.** Inferred guard failure enters the complete boxed slow body; async/task/proc paths excluded | Source-relative dual-entry tests and forced-GC sweep pass |
| **P4** | **Complete.** Direct calls prove raw preconditions or route to `_b`; plans control elision | MIR size ratchet rebaselined with checked fixtures |
| **P5** | **Deferred intentionally.** Broader speculative body-only inference is a separately measured optimization, not needed for correct dual entries | Future Result-suite work |
| **P6** | **Complete core.** LambdaJS guard, slow fallback, mismatched-call retention, and mixed native/`Item` signatures | Dedicated JS compiler/coercion/guard tests pass; JS keeps boxed entries |
| **P7** | **Future optimization.** DF16 guard hoisting remains off and unimplemented | Requires a separate benchmark justification |

P5 and P7 are deliberately outside the completed Stage 1 semantic feature: they
change optimization policy, not the source-correct fast/slow behavior.

---

## 8. Test & gates

- **Declared implicit-boundary admission (DF2/DF6).** Cover every concrete
  numeric source/target pair in both static and deferred form: whole-domain exact
  embeddings admit statically; non-embeddings reject statically; dynamic exact
  values normalize to the target; dynamic inexact values return the rich error.
- **Untyped fast specialization (DF5/DF9).** For guard-passing exact semantic
  shapes, compare the unboxed result with a forced-boxed/source reference. Check
  semantic type as well as value, including float zero/negative-zero, INT53
  edges, full-width homes, strings, and errors admitted by explicit contracts.
- **Untyped slow path (DF3/DF4/DF9).** For guard-failing inputs, compare `_b`
  only with a forced-boxed/source reference; never call the unboxed entry outside
  its precondition. Include `int` for inferred `float`, `float` for inferred
  `int`, strings, null, errors, and `i64`/`u64` values above 2^53.
- **Closed-world collapse (DF15).** Verify that one exact visible call shape
  omits `_b`, while a deferred or outside-shape caller retains it; adding a later
  caller through whole-unit recompilation must restore the slow body, not create
  an inferred contract error.
- **Promotion corpus (O1/O2).** Exercise every numeric operation at promotion
  boundaries through boxed-only, native-param/dynamic-return, and declared-
  return lanes.
- **Completed evidence.** The focused Lambda dual-entry and numeric-admission
  tests pass; the focused LambdaJS guard/mixed-parameter tests pass; the MIR
  emission suite (12), ratchet suite (15), and forced-GC sweep (25) pass. A
  release build also executes both new Lambda and LambdaJS guard fixtures.
- **Baseline context.** `make test-lambda-baseline` passes every Lambda/MIR gate
  and reports 3 pre-existing DOM-library JS fixture failures
  (`dom_module_props`, `hljs_highlight`, `lib_htmx`). They reproduce with native
  JS function emission disabled. The broader Node official corpus is likewise
  not a green repository gate (1169 unrelated platform/API failures after its
  socket preflight), so it cannot measure this feature's regression status.
- **Performance.** P5 and P7 are deferred optimization work. A future Result
  snapshot must report Lambda and LambdaJS geomeans separately and attribute any
  inference-policy delta to that later work.

---

## 9. What this does not do

- No multi-version specialization (one unboxed version only).
- No bespoke per-call-site checks. DF16 guard *hoisting* is accepted but is P7,
  flag-gated, and never emits a predicate other than DF5's.
- No runtime type feedback / OSR / tiering.
- No change to the frozen C2MIR path (CLAUDE rule 14).
- No closure, method, variadic, or typed-array unboxing (DF11).

---

## 10. Future: multiple unboxed versions

Once DF9 is enforced and O6 gives a failure signal, the natural extension is N
unboxed versions keyed by observed argument shape, with the boxed entry
dispatching through a small guard chain. The prerequisites are all in this
design: an exact semantic-shape guard (DF5), a source-equivalent complete slow
body (DF4), and a source-relative correctness invariant for every specialization
(DF9). Union-typed params become the first candidate: `int | string` would get
one `int` version and one `string` version instead of staying boxed.

---

## 11. Prior art

None of the individual mechanisms in this document is new. Dual entry points,
typed wrappers, exact representation guards, and speculation backed by a
complete slow implementation all have precedents. No one precedent is the
semantic authority for Lambda, and the declared and inferred lanes have
different closest analogues. This section maps those analogues without claiming
that another system has Lambda's exact combined shape.

### 11.1 Two entry points per function — DF1, DF7, DF8, DF15

| System | Fast entry | Public entry | How the version is chosen |
|---|---|---|---|
| **SBCL** (Common Lisp) | internal/local-call convention | full-call external entry point (XEP) and prologue | known local calls use the local convention; unknown/full calls use the XEP |
| **Cython** | `cdef` C function | `cpdef` also emits a Python-callable wrapper that converts and forwards | caller's language decides statically |
| **Numba (current)** | `nopython` specializations | dispatcher object | runtime selection/compilation by observed argument types; automatic object-mode fallback was removed in 0.59 |
| **Numba (≤0.58, historical)** | attempted `nopython` specialization | object-mode compilation | compilation failure could fall back to a complete generic implementation |
| **Julia** | specialized method instance | generic entry / `invoke` | runtime dispatch on concrete argument types |
| **Static Python** (Meta Cinder) | typed bytecode with unboxed primitives | normal Python entry | boundary checks at the typed/untyped edge |
| **Static Hermes** (Meta, announced 2023) | native AOT from typed JS | untyped JS path | static where types are known |
| **Sorbet Compiler** (Stripe, experimental; not actively developed publicly) | LLVM-compiled `sig`-typed method | CRuby VM method | per-method fallback into the interpreter |
| **This design, declared** | `<name>` main body | `<name>_b` admission/normalization trampoline | static calls skip the wrapper; dynamic calls enforce the user contract |
| **This design, inferred open/mixed** | `<name>` specialization | `<name>_b` exact-shape dispatcher + complete slow body | exact shape takes fast path; every other legal value preserves source semantics in the slow body |

Three things to take from the table.

1. **Callee-side dispatch is established practice, not a claimed majority.**
   SBCL's full entry and Numba's dispatcher put an open-world decision in one
   place per function. DF8 uses the same concentration of responsibility;
   DF16's caller work is scoped to measured hoisting.
2. **Local/full-call separation supports DF15's direction.** A closed caller set
   can use an internal convention and avoid paying the public-entry path. Lambda
   goes further for module-private functions because its lack of runtime string
   eval makes more caller sets genuinely closed.
3. **Historical Numba object fallback is a warning, not a current exact match.**
   It demonstrates the cost of silently falling into a complete generic
   implementation; current Numba removed that automatic behavior. O6 should
   therefore report specialization usefulness without presenting a fallback as
   an error.

Primary references: [SBCL calling convention](https://www.sbcl.org/sbcl-internals/Calling-Convention.html),
[Cython `cpdef`](https://cython.readthedocs.io/en/latest/src/quickstart/cythonize.html),
and [Numba 0.59 fallback removal](https://numba.readthedocs.io/en/0.61.0/release/0.59.0-notes.html).

LambdaJS's current state (§2.2) — two bodies, no guard, static call-site choice —
is closest to the Cython-style static split. DF13 adds runtime specialization
dispatch; it should not be described as adopting current Numba's exact shape.

### 11.2 Annotations as representation, not merely as checks — O3, TS-3

**Cython** (`cdef int x`), **mypyc**, **Julia**, and **Static Python** all treat an
annotation as a commitment about machine representation: the annotated value is
stored unboxed and operations on it compile to native instructions; unannotated
values remain the boxed universal representation. mypyc compiles mypy itself with
this model for a roughly 4× speedup.

These systems get the typed-region invariant by *construction*: the annotation
is enforced at the untyped boundary **and the admitted value is converted into
the typed carrier**. That second half is essential. A compatibility predicate
that leaves an int-tagged Item at a declared float boundary has not established
the representation DF2 requires. DF6's normalization is therefore part of the
boundary, not an optional unbox afterward.

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
- **Julia's "type instability"** is analogous to the representation-planning
  problem: one result domain the inferencer cannot pin can degrade downstream
  arithmetic to a generic path. It is not the semantic rule for Lambda flex
  `int`; O1 remains governed by Lambda's promotion lattice. Julia's tooling answer
  (`@code_warntype`) is a diagnostic that shows where inference gave up — the same
  need as O6, approached from the tooling side rather than the runtime side.
- **Crystal** is the counterfactual: union types plus whole-program inference and
  *no* dynamic escape hatch, compiled to LLVM. It shows what §1.3's union params
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

**Where this design sits in that spectrum:** the literature applies directly to
the **declared** lane, where annotations impose runtime enforcement at a dynamic
boundary. Lambda's concrete scalar checks are shallow, while structural
map/array contracts may validate deeply at the boundary; Lambda does not install
higher-order blame wrappers. The exact classification should follow the
guarantee actually provided by each contract kind.

DF5's inferred guard is **not gradual-type enforcement at all**. It creates no
user-visible type promise and exists only to select an optimization. Its closest
prior art is specialization/deoptimization, not transient soundness. Likewise,
DF16 hoists inferred representation guards; hoisting expensive declared
boundaries would be a separate optimization with separate legality rules.

Primary reference: [Greenman & Felleisen, *A Spectrum of Type Soundness and
Performance*](https://www2.ccs.neu.edu/racket/pubs/icfp18-gf.pdf).

### 11.5 Benchmark method worth borrowing — §8

The Takikawa configuration-lattice method is directly applicable to the `*2.ls`
typed corpus. Today those rows are graded as a per-row geomean of fully-typed vs
fully-untyped, which cannot distinguish "annotations helped" from "annotations
added a boundary that happened to be cheap here". Measuring a lattice of partial
typings for a handful of representative benchmarks would attribute cost to
boundaries directly, and would give P5 (DF12 inference lifting) a baseline that
is not confounded by annotation placement. Recorded as a suggested addition to
§8, not a gate.

### 11.6 What is not precedented

The combination, not the parts: union types plus optional annotations plus a
tagged 64-bit `Item` plus a self-hosted MIR JIT plus a document/data-processing
runtime, in one small toolchain. Crystal has the union inference; Julia has the
specialization and the instability problem; Cython and mypyc have the
representation pinning; none of them has all three.

One structural advantage is unusually broad in Lambda: **DF15 can prove more
module-private caller sets closed.** Python, JavaScript, Ruby, and Lisp generally
retain eval or open-world name/dispatch surfaces that limit global entry
elision. Lambda has no runtime string eval (DF17), so P0.5 can omit `_b` for a
module-private function whenever the existing escape analysis and three-way
call-site audit prove the caller set closed. The opportunity comes from a
language decision made elsewhere; the proof still belongs to Lambda's own
escape analysis, not to analogy with another compiler.
