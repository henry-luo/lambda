# Lambda / LambdaJS — Compilation Strategy ADRs

> **Status: ACTIVE ledger (started 2026-08-01).** Top-level architecture
> decisions for how Lambda script and LambdaJS compile. This doc holds
> cross-cutting *policy*; mechanisms stay in their own design docs. On
> conflict, decisions here win over per-area docs (same convention as
> `Lambda_Design_Jube_Architecture.md` JA1–JA16).
> Related:
> [`Lambda_Design_Compiling_Dual_Func.md`](Lambda_Design_Compiling_Dual_Func.md) (DF1–DF17 — the specialization mechanism),
> [`Lambda_Tune_Typed_Vs_C2MIR.md`](Lambda_Tune_Typed_Vs_C2MIR.md) (M1–M8 measured evidence),
> [`Lambda_Design_Type_Enforcement.md`](Lambda_Design_Type_Enforcement.md),
> [`impl/Lambda_Issue_Type_Support (retired).md`](impl/Lambda_Issue_Type_Support%20(retired).md) (TS-1..TS-9),
> [`Lambda_Design_MIR_Cache_L3.md`](Lambda_Design_MIR_Cache_L3.md).

---

## LC1 — Specialization over caching: no inline caches in Lambda script

**Status: DECIDED 2026-08-01.**

### Decision

Stated in terms of *lanes and sites*, not functions:

1. **Specialized lowerings** — code compiled under known types, whether
   *declared* or *inferred behind a dual-func guard* (DF1/DF2) — contain no
   dynamic-dispatch sites by construction. ICs are ruled out here by absence
   of need; the policy is self-enforcing rather than enforced.
2. **Open sites in Lambda script** — in untyped functions, in the open sites
   of partially typed functions, and in boxed fallback bodies (DF3) alike —
   compile to plain runtime dispatch (`fn_add`-style closed switch) today,
   and to **multi-version compilation / guard hoisting** tomorrow
   (dual-func §10, DF16). **Never inline caches.**
3. **LambdaJS keeps its ICs.** JS has no type system to specialize against;
   the IC *is* its type feedback. Existing and future LJS IC work (Tune6
   lineage, store ICs, shape identity) is unaffected by this ADR.

### Original framing and the grey-area resolution

The decision was posed as a three-way function-granularity policy: typed
functions → no ICs; untyped functions → ICs allowed; partially typed
functions → grey area, resolved to no ICs (simpler implementation; the
correct long-term mechanism for open types is multiple compiled
versions/paths, not per-site caching).

The recorded formulation above is one notch stronger: it makes the entire
Lambda lane IC-free, including fully untyped functions. Function granularity
is ambiguous exactly where the dual-func design lives — a partially typed
function contains both closed and open *sites*, and the boxed fallback body
of a fully typed function *is* an untyped lowering, so "typed ⇒ no IC" and
"untyped ⇒ IC" would collide inside one function. **Escape condition:** if a
measured hot open site ever appears that inference cannot close, treat it as
a DF12 inference bug first; only if it survives that lens does the
untyped-Lambda IC question reopen.

### Rationale

- **R1 — The entry guard already is the cache, at better granularity.** An IC
  checks per site per execution; the DF1 guard checks once per call and buys
  the whole specialized body. For hot, type-stable code — the only case where
  ICs pay — function-level specialization strictly dominates site-level
  caching: one check amortized over N sites instead of N checks.
- **R2 — Lambda's dispatch space is closed; JS's is open.** ICs earn their
  keep on unbounded dispatch spaces (shapes, methods, prototype chains).
  Lambda's operator dispatch is a small closed type-pair switch; a
  well-predicted branch tree is within a few cycles of a monomorphic IC hit.
  The IC-vs-switch delta is small; the specialized-vs-either delta is the
  measured 9.48x (Result18 static-ceiling columns). Spend where the delta is.
- **R3 — Generated code stays immutable, which is an architectural asset.**
  ICs mutate code or cache cells; the cache-cell-vs-MIR-module-cache epoch
  problem was already encountered in the Tune6 era. Multi-version output is
  immutable — it composes cleanly with the L1/L2 module cache, the L3
  design, and the MarkPack sealed-module / AOT direction.
- **R4 — Union types make multi-versioning complete.** `int | string` has two
  members; §10 compiles one version per member with the boxed body as
  backstop. No megamorphic tail, no cache-miss cliff — a completeness JS ICs
  can never have.
- **R5 — Precedent.** Julia is exactly this policy (no ICs anywhere;
  multi-version specialization + dynamic-dispatch fallback) and is the
  best-performing system of its shape; SBCL and Static Hermes match on the
  typed lane. V8/JSC-style ICs are the workaround for a language with
  nothing to specialize against — LJS inherits that world; Lambda does not.

### Accepted costs

- **C1 — Shape dispatch is pushed onto the type system.** The one case ICs
  uniquely help that entry-level specialization does not: `m.field` in a hot
  loop on an unannotated map of unknown shape (per-iteration shape variance
  defeats an entry guard). The sanctioned answer is "declare the map type" —
  which makes the **TS-3 fix load-bearing**: today a named-map or typed-array
  annotation makes code *slower*, so the escape route this policy depends on
  must become a genuine win (Tune doc pillars T-A/T-C).
- **C2 — Genuinely per-iteration-polymorphic sites stay at dispatch cost.**
  Measured small (closed switch), and rare in Lambda's
  columnar/homogeneous-leaning workloads.

### Consequences

- No IC machinery is added to the Lambda-lane transpiler
  (`transpile-mir.cpp`); no IC state in Lambda-lane MIR modules.
- Dual-func §10 multi-version dispatch is a short **guard chain**, not a
  patchable cache.
- DF16 guard hoisting is the sanctioned intra-function "multiple paths"
  mechanism for partially typed loops.
- Optimization pressure on open sites goes to DF12 (speculative inference)
  and §10, not to caching.

---

## LambdaJS implementation note — guarded named-property inline caches

LambdaJS uses bounded, mutable **cache cells** beside otherwise immutable MIR;
it does not patch generated instructions. For the general compiled
non-computed named-member paths (`obj.field` and `obj.field = value`), lowering
allocates one `JsLoadIC` or `JsStoreIC` per site and passes its address, the
interned field name, and its length to `js_property_access_named_ic` or
`js_property_set_named_ic`.

Each cache entry records the receiver kind, the exact `TypeMap*` shape, its
`ShapeEntry*`, and the field byte offset. A cache starts empty, becomes
monomorphic after its first eligible receiver, holds up to four shapes in its
polymorphic state, and becomes megamorphic thereafter. On a hit, the helper
checks the receiver kind and exact shape pointer, then reads or writes the
cached slot directly. This replaces a property-key conversion/lookup and the
ordinary descriptor/prototype/exotic dispatch with a small, predictable guard
plus an offset access.

The fast path is intentionally narrow: it caches only descriptor-free,
non-deleted own data properties on plain maps (and eligible array companion
maps). Accessors, inherited and builtin properties, Proxies and other exotic
objects, computed/private names, and incompatible stores fall back to the
ordinary JavaScript `[[Get]]`/`[[Set]]` path. Stores first run that ordinary
path on a miss and are cached only if the resulting own slot can safely accept
the value type. This is guard-based rather than invalidation-based: structural
changes give the receiver a new `TypeMap*`, while descriptor changes move it
off the plain-map fast path, so stale entries cannot be used.

Constructor shape caching complements the per-site ICs by sharing a canonical
`TypeMap` among same-layout instances, allowing their exact-shape guards to
hit. Prototype-method call ICs are not implemented; inherited method lookup
remains on the normal dispatch path. The implementation lives in
`lambda/js/js_mir_expression_lowering.cpp`, `lambda/js/js_runtime.{h,cpp}`,
and uses the `TypeMap`/`ShapeEntry` layout in `lambda/lambda-data.hpp`.

---

## Function-call flows — compiler staging versus generated runtime execution

Compiler-local argument buffers do not exist after JIT compilation. This
section distinguishes those lowering-time structures from the root-frame
slots, registers, and ABI operands used by the generated code at runtime.

### Core Lambda

**Transpiling a statically resolved local call.** For `f(a, b)`, when `f` is
known to the MIR Direct compiler, `transpile_call_raw` first maps source
expressions to parameter positions (including named arguments and defaults)
in `resolved_args`. It evaluates each argument, records its MIR operand and
type in `arg_ops`/`arg_vars`, and assigns every boxed, GC-capable value an
`arg_root_slots` entry. Immediately before emitting the call, it reloads those
rooted values into `call_ops`; native scalar operands need no GC root.

`resolved_args`, `arg_ops`, `arg_vars`, `arg_root_slots`, and `call_ops` are
C++ lowering buffers only. They do not describe a runtime array ABI. The
current local-call staging arrays have 16 entries; that is an implementation
limit/bug boundary, not a MIR limit, and calls beyond it must be rejected or
lowered through dynamically sized staging before they are supported.

**Runtime execution of that direct call.** The emitted MIR runs inside the
caller's precise root frame, evaluates arguments, writes boxed ones to their
side-root slots, reloads them into registers, then makes a direct JIT call to
the known target. The physical call is `Context*`, followed by the source
arguments and, when its result contract needs it, a trailing caller-owned
scalar-result home. The callee has no `Function*` lookup or arity switch on
this path.

**First-class/dynamic core-Lambda call.** If the callee is a runtime function
value (for example `callback(a, b)`), lowering instead calls the `fn_call*`
runtime dispatcher. It receives a `Function*` plus values, validates the
signature, and chooses a native function-pointer cast. That dispatcher is a
separate ABI: it supports eight ordinary physical arguments, or seven user
arguments for a closure because the captured environment occupies one slot.
Variadic functions package surplus actual arguments in a `List`, so the
surplus is not itself constrained by that physical-arity switch.

### LambdaJS

**Transpiling a JS call or construction.** Every `CALL_EXPRESSION` and
`NEW_EXPRESSION` opens a `JsMirArgStackScope`. On the ordinary, non-suspending
path, `jm_build_args_array` evaluates each argument and writes its boxed
`Item` into a lexically assigned suffix of the generated function's precise
side-root frame. Nested calls use higher slots and sibling calls reuse slots;
the call helper can prove that this exact span is rooted and select the
`js_call_function_prerooted_args_into` path. Generator/async calls whose
arguments can suspend instead spill to their persistent environment and copy
to an allocated argument array after resumption.

This is the completed JS call-argument-stack merge: it replaced
`js_args_stack` with a root-frame suffix. It does **not** use the number
stack, which holds unscanned raw wide-scalar payloads and cannot hold general
boxed JS `Item` values.

**Root-frame comparison.** Both implementations use precise `Item` slots in
the same per-`Context` GC root side stack, but their call ABIs require
different layouts:

- **Core Lambda** roots only GC-capable boxed values; native scalar arguments
  stay in registers. Its direct-call ABI passes arguments as individual
  registers. Argument roots are ordinary root-frame slots and do not form a
  required `Item* + argc` argument-array interface.
- **LambdaJS** arguments are boxed `Item` values and its generic call ABI
  needs a contiguous `Item* args` plus `argc`. It therefore reserves a
  contiguous suffix for each lexically active call, passes its address, clears
  it when that call expression completes, and reuses it for sibling calls.

**Call-ABI comparison.** A direct core-Lambda call passes individual ABI
operands: `Context*`, `arg0`, `arg1`, …, and, when required, a trailing
`scalar_result_home`. A generic LambdaJS call instead crosses its dynamic
boundary as a contiguous `Item* args` plus `int argc`. Once the JS runtime
dispatcher has received that span, it invokes the selected compiled wrapper
through its fixed context ABI, so that wrapper ultimately receives individual
formal operands. `Item* + argc` is therefore the dynamic JS call boundary,
not the wrapper's final calling convention.

A direct JS wrapper call is possible only as an optimization when the compiler
can prove the target and preserve all dynamic call semantics. Core Lambda can
use that path much more often because its statically resolved calls have a
simpler, fixed language ABI.

**Runtime execution of a JS call.** The JS runtime receives an `Item* args`
and `argc`, resolves/calls the `JsFunction`, and retains the complete actual
argument span for `arguments` and rest-parameter semantics. Generated JS
functions use the context ABI and a dispatch table for up to 32 declared
physical formal slots. Rest parameters materialize an array from surplus
actual arguments, so actual argument count is not bounded by 32 except by
ordinary integer and memory limits. A function declaring more than 32
physical formals is currently rejected by the compiled context-ABI dispatcher;
the legacy non-context dispatcher has a separate 16-slot boundary.

**Native non-context JS callbacks.** The old 16-slot non-context function
pointer path remains as compatibility code, but it is not the ABI used by
ordinary source functions compiled by LambdaJS. JS MIR lowering creates those
functions with `js_new_*_mir` and finalizes them with
`JS_FUNC_FLAG_MIR_CONTEXT_ABI`, selecting the `Context*` wrapper ABI above.
Native host callbacks created through `js_new_function`, `js_new_closure`, or
`js_new_method_function` are normalized through the shared Item-only hosted
dispatcher. The retained non-context switch is therefore a wrapper/legacy
compatibility boundary, not a general LambdaJS source-call limit.

### Dynamic adapter spans

The former `padded_args[32]` marshalling buffer has been retired. The adapter
applies only to calls that cross the LambdaJS dynamic `Item* args + argc`
boundary. A statically proven direct JS wrapper call continues to pass
individual ABI operands and needs no adapter.

The implementation separates two spans for every adapted dynamic invocation:

- the immutable **source-actual span** and original `argc`, retained for
  `arguments` and for the source-level call semantics;
- the **invoke-adapter span**, the contiguous operands supplied to the selected
  wrapper after missing-formal padding and/or rest-parameter transformation.

When source actuals already have the wrapper shape, the adapter borrows the
caller's active argument-suffix span directly. A generic native caller first
copies source actuals into the generic dispatcher's exact root suffix, then
borrows that suffix when the shape already matches. Missing-formal and rest
calls get a dispatcher-owned exact-sized span of side-root-frame slots. In
particular, a rest call keeps its original span intact for `arguments` and uses
a distinct adapter span for its fixed operands plus the materialized rest
array.

The active generated suffix has only its source-actual extent; the dispatcher
never assumes an adjacent slot belongs to it. Therefore this implementation
does not reserve a speculative caller tail for a runtime-resolved dynamic
target. A future tail optimization requires lowering to prove a named,
call-owned exact extent; otherwise an adapted call stays on the exact owned
span path.

Adapter spans are precise GC roots and are destroyed in LIFO order with the
dispatcher/callee frames. They are dynamically sized to the exact invocation;
the design has no `Item[N]`, `MAX_ARGS`, or speculative per-call root
reservation. The existing 32-formal context-wrapper and 16/15-formal native
callback boundaries remain checked dispatch-ABI limits, not adapter-storage
capacities. The complete implementation plan is
`vibe/impl/Lambda_Impl_JS_Dynamic_Arg.md`.

### Fixed call-related arrays and limits

The codebase also contains fixed C/C++ arrays in call paths. They are not one
uniform language arity limit; their significance depends on whether the array
is temporary compiler staging, a checked ABI boundary, optional specialization
metadata, or a deliberately bounded API adapter.

**Core Lambda.** The direct-local-call lowering arrays (`resolved_args`,
`arg_ops`, `arg_vars`, `arg_root_slots`, and `call_ops`) each have 16 entries.
This is the unsafe compiler-staging limit described above, rather than a MIR
direct-call ABI limit. The system-function variadic fallback also uses a
16-entry `arg_ops` staging array, so it must not silently discard actuals past
that boundary. Core source-analysis records use the intentional 16-slot
`LAMBDA_MAX_FUNCTION_ARGS` limit, while JS formal metadata is dynamically
sized. MIR function-argument bookkeeping records up to 32 register arguments;
these are compiler analysis/tracking limits, not the dynamic `Function*` call
ABI. Separately, context-ABI imported runtime helpers
have an explicit eight-argument boundary, as does the dynamic `fn_call*`
physical dispatcher (seven user arguments when a closure environment occupies
one operand).

**LambdaJS.** The ordinary generic JS call path has no corresponding
16-element argument-array cap: its rooted argument suffix and `Item* + argc`
boundary scale with the actual count. The compiled context-wrapper dispatcher
checks a 32-declared-formal ABI limit but no longer allocates a 32-entry
marshalling array; an adapted invocation reserves only its exact formal count.
The retained legacy non-context function-pointer path has the separate
16/15 compatibility limit described above. It is not a JS source-formal
limit: JS parameter metadata and `arguments`/duplicate-formal bookkeeping are
derived from the AST and dynamically sized.

Some runtime adapters intentionally bound *forwarded* callback arguments:
diagnostic-channel helpers cap them at 16 (or 15 in one tracing callback
shape), and timer/immediate handles retain up to eight extra callback
arguments. These are API-specific forwarding limits, not limits on a normal
JS function call. Likewise, many small arrays such as `Item args[2]` are
fixed-arity calls to known internal helpers and do not constrain general
calling.

---

*Future compilation-strategy ADRs (tiering, AOT/sealed-module boundaries,
lane interactions) land here as LC2+.*
