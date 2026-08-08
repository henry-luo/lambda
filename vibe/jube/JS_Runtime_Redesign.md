# LJS Runtime Redesign — One Mechanism per Concept, on Lambda's Mechanisms

**Date**: 2026-08-07  **Status**: JR3 IMPLEMENTED — Tune1 E8 validation
complete
**Tree anchor**: master `b9b30f4ac`
**Companions**: `JS_Profiling_Helpers.md` (measured evidence),
`JS_Runtime_Review.md` (complexity findings), `JS_Tune1_Helpers.md`
(performance phases — subsumed by this design where they overlap)

Decisions in this doc carry **JR#** ledger ids. Where a decision changes a
formal ruling, §8 names the `D#` to revise per rule 17 — landing any such
phase updates `doc/Lambda_Formal_Design.md` in the same change.

---

## 0. Mandate, governing principle, and gates

The runtime works but is messy and dispatch-bound: helpers consume 43.5% of
working CPU while JIT code contributes 0.6%; the review found four systemic
accretion patterns (string-keyed builtin dispatch, 54 property entry points,
five type discriminators, poll-based exceptions). This is a redesign, not a
tune: mechanisms are replaced and **their predecessors deleted**.

**JR1 — Extend Lambda; never reinvent beside it.** The JS runtime keeps and
deepens its alignment with the Lambda runtime: the Item data model (D2),
TypeMap shapes (D3.4), the stack model, scalar homes and precise rooting
(D5), the GC heap (D4), function values (D6.2), and module management (D7).
Where QuickJS or V8 practice is imported, it is expressed **through an
existing Lambda mechanism**, not as a JS-private sibling. The review's central
finding supports this: LJS's worst complexity sits exactly where it deviated
from Lambda's own machinery (static promise tables beside the GC heap,
sentinel properties beside shapes, a pending-flag channel beside `ItemError`,
raw-name probes beside the NamePool, a builtin catalog dispatch beside
function values, a private CJS loader beside the module registry). "One
mechanism per concept" and "reuse Lambda" are the same instruction here.

**Hard quality gates** (measured, per §5):

1. The number of JS runtime data structures / parallel mechanisms per concept
   **must go down** — census table in §5.1, enforced per phase.
2. **LOC must go down** — net-negative diff per phase, targets in §5.2.
3. Behavior: 327-test suite, js262 gate, layout/DOM suites, GC-stress —
   green; bench suite non-regressing; the profiling harness re-run per phase.

## 1. Design sources — what is imported, and into what

| Source practice | Import? | Hosted by (Lambda mechanism) |
|---|---|---|
| QuickJS: atoms (one interned name identity, int compare) | **Yes** | NamePool canonical names, D4.6 — already exists; make it the *only* key form |
| QuickJS: exception = in-band sentinel return + exception value in context | **Yes** | Lambda's in-band error signaling (`ItemError`, S-layer `T^E` model); the `LambdaError` carrier owns the payload |
| QuickJS: one `class_id` + exotic-methods table | **Yes** | TypeMap-carried class metadata + per-class ops table (extends D3.4) |
| QuickJS: builtins are function objects on prototypes | **Yes** | Lambda function values (D6.2) built from the existing catalog (D6.4/D7.4.3) |
| QuickJS: single property get/set path | **Yes** | One helper pair over shape/IC/exotic/proto tiers |
| QuickJS: refcount GC | **No** | Lambda precise GC stays (rule 15, D5.3) |
| QuickJS: interpreter | **No** | MIR Direct stays (D8.1.1) |
| V8: elements kinds (packed/holey × smi/double/tagged) | **Yes** | `LMD_TYPE_ARRAY_NUM` (elem-typed numeric array, already in the data model) + `LMD_TYPE_ARRAY`, plus one holey bit |
| V8: feedback vectors — IC state per function, out of code | **Yes** | Per-function slot array via the emitter's existing const-pool mechanism (`consts_reg`/`consts_bss`) |
| V8: maps/hidden classes + transition discipline | Already present | TypeMap shapes; this design consolidates onto them |
| V8: handle scopes | Already present | `RootFrame`/`Rooted` (D5.3) — unchanged |
| V8: multi-tier JIT, deopt, generational GC, Torque | **No** | Out of scope; one MIR tier remains |

## 2. Target architecture

### 2.1 One mechanism per concept — before → after

| Concept | Today (count) | Target (single mechanism) |
|---|---|---|
| Property key | raw `(chars,len)`, `PropertyKeyRef`, `String*`, pool strviews (**4**) | **JsName**: NamePool-canonical `String*` with id+hash (**1**) |
| Object discrimination | TypeId, `map_kind`, `JsClass` (44), shape flags, sentinel props (**5**) | TypeId + **shape-carried JsClassMeta** (class id, elements kind, flags) with per-class ops (**2**) |
| Builtin dispatch | catalog→id switch, per-kind name switches, direct-impl calls (**3**) | builtins are **function values** installed on prototypes; dispatch = get + call (**1**) |
| Property access | **54** entry points | `js_get` / `js_set` (+ define/delete/has) over internal tiers (**≤8**) |
| Error signal | pending flag + polls *and* `ItemError` (**2**) | **in-band**: the returned Item *is* the ERROR-tagged `LambdaError*` (**1**, landed in Tune1) |
| Promise | static record table + wrapper map w/ `__promise_idx` (**2**) | one GC-heap native struct presented as VMap (**1**) |
| IC state | `JsLoadIC`, `JsStoreIC`, callsite caches, shape-guard sites (**≥4**) | one **feedback slot** union per site, in a per-function vector (**1**) |
| Call entry | 12 `js_call_function*`/invoke variants | one public entry + one internal raw path (**2**) |
| Module loading | Lambda registry *and* private JS require/CJS loader (**2**) | Lambda module registry; Node resolution as a resolver plug-in (**1**) |

### 2.2 Alignment with the Lambda runtime (explicit, per JR1)

- **Data model**: JS objects remain `Map` + `TypeMap`; JS arrays remain Lambda
  arrays — dense numeric arrays use `LMD_TYPE_ARRAY_NUM` (elem_type already
  selects int/int64/float), generic use `LMD_TYPE_ARRAY`; the redesign adds
  only a holey/packed bit to shape meta, no new container types.
- **Stack & GC**: D5 unchanged — scalar homes, safepoint-current slots,
  `RootFrame`/`Rooted`. The redesign *removes* the one large deviation
  (promise static tables with epoch re-registration) by moving promises onto
  the GC heap where every other value lives.
- **Modules**: JS ESM/CJS instantiation rides the Lambda module registry and
  Jube packaging (D7.2/D7.3); Node path-resolution semantics become a
  resolver, not a loader.
- **Compilation**: AST→MIR Direct pipeline unchanged (D8.1.1); lowering gets
  *simpler* (no poll emission, fewer entry points to select among).
- **Hosting**: helper catalog metadata (D7.4.3) remains the ABI contract; the
  helper *surface shrinks* but every remaining row keeps its effect metadata.

## 3. Key mechanism redesigns

Each subsection: evidence → design → what is deleted → interactions.

### JR2 — Property keys: NamePool-canonical `JsName`

**Evidence.** The name/shape lookup infrastructure costs **8.3% of working
CPU**; `js_find_shape_entry` interns its key on *every* raw probe
(`js_property_attrs.cpp:67`), and array generics intern a synthesized index
name per element. Four key representations coexist.

**Design.** One key type: `JsName` = canonical NamePool `String*` carrying
`{id, hash, len}` (D4.6 already defines temporal-canonical identity — this
makes JS *only* speak it). All property APIs take `JsName`; the transpiler
emits pre-interned names through the const pool (it already interns literals);
integer keys never become names (dedicated indexed path, JR6). Shape probes
compare ids — QuickJS-atom semantics on Lambda's existing pool.

**Deleted.** Raw `(chars,len)` probe variants; per-lookup `heap_create_name`
canonicalization; duplicate hashing (`hashmap_sip` on hot paths);
`well_known_name_id` re-derivation (the well-known table becomes ids at boot).

**Interactions.** DO16 (intern growth bound) becomes easier to enforce:
interning happens at compile/boot, not per lookup.

### JR3 — Exceptions: in-band signal, one channel

**Evidence.** `js_check_exception` is **1 of 4 dynamic helper calls** (32.6M;
153,725 emitted poll sites) plus 467 hand-written flag checks; an entire
emission-time tracker (OE1–OE10) exists to elide polls. Cost is now ~0.2% —
the problem is the second error channel and everything built to manage it.

**History — the basis of this redesign.**

- **G0** — blind polls after essentially every throwing call: the check
  exists at runtime, redundantly.
- **G1** *(emit, then delete — `8840e937c`, 2026-07-20;
  [`Lambda_Tuning_Proposal.md`](../Lambda_Tuning_Proposal.md) R5)*: all
  checks still born; a post-hoc CFG dataflow (`jm_optimize_exception_polls`)
  deletes the provably redundant ones. Knowledge applied *after* emission —
  and the analysis itself became a compile-time cost.
- **G2** *(never born — `52c0f3c02`, 2026-07-24;
  [`Lambda_Impl_Online_Exception (done).md`](../Lambda_Impl_Online_Exception%20(done).md)
  OE1–OE10)*: knowledge moved to emission time — `exc_track` answers "can the
  flag possibly be set here?" before emitting anything; G1's peephole and
  offline pass were deleted. But its ceiling is the `UNKNOWN` state: catalog
  rows default to `MAY_SET`, so 153,725 emitted sites — 32.6M executed polls —
  remain as separate calls.
- **G3 = JR3** *(nothing separate exists)*: knowledge moves into the value
  representation. The signal rides the returned Item, so the "check" is a tag
  test on a value the caller already holds in a register — there is no
  separate thing to emit, elide, or optimize. And `exc_track` isn't
  discarded; it's promoted one rung: instead of eliding poll *calls*, it now
  elides the *branches* themselves for `PRESERVES` helpers.

The governing principle, generalized for the whole redesign: **runtime checks
are reduced as far as possible, in strict preference order — (a) eliminate
(prove statically via catalog effects / tracker state / shape guards that no
check is needed), (b) fuse (the check is a branch on a value already in hand
— in-band signaling, tag tests), (c) separate runtime call — never the
default.** G1→G2→G3 is exactly a walk up this ladder, and JR6's tier design
follows the same order.

OE's enabling facts (single call choke points, single label emitter, catalog
effects) are exactly JR3's enabling facts; OE's save/restore of tracker state
around nested function compilation must be preserved.

#### JR3.1 — JS adopts the Lambda error model

Cross-checked 2026-08-07 against the revised error handling that landed in
`0d8cc5222` (`Lambda_Impl_Error_Handling (done).md`, `Lambda_Error_Runtime.md`,
`lambda/runtime/lambda-error.h`). The Lambda side is already **single-lane
in-band**, not two-lane:

- A `T^E` (can-raise) callee returns **one merged Item** — success value or
  ERROR-tagged error — split only when `^`/`^err` consumes it
  (`transpile-mir.cpp:5354`: "its success and error lanes merge into one Item
  until `^` consumes it"). "Two-lane returns" was **SF14**, a *scalar-payload*
  design, superseded by D5.2.1 caller-donated homes; it was never the error
  mechanism.
- **TE-15** rejected error propagation through unboxed lanes (per-lane
  sentinels, re-boxing, or a polled side flag — "cost on every operation"):
  the exact disease G0–G2 fought on the JS side. Its corollary is live in the
  lowering: native raw-lane returns are recovered **only for non-raising
  signatures** (`can_raise` gate).

JS therefore does not get an error model of its own; it slots into Lambda's
four outcome kinds (`Lambda_Impl_Error_Handling (done).md` §6):

| Lambda outcome kind | JS counterpart |
|---|---|
| `RAISED_ERROR` — `raise` / `T^E` callees, merged-lane return ABI | JS `throw` + propagation: every fallible JS helper call is a dynamic `Item^JsError` |
| `FAULT` — stack overflow/OOM, non-local unwind via `LambdaRecoveryFrame` | already shared today (`lambda-stack.cpp` signal path) — unchanged |
| `SOFT_VALUE` — error-as-value contagion (S7.9) | no JS counterpart |
| `DEFECT_SKIP` — checked-boundary skips | no JS counterpart |

The correspondences, mechanism by mechanism: JS `try/catch` is the dynamic
analogue of `^`-consumption; the emitted branch-to-handler is the analogue of
`?`-propagation; a `PRESERVES` catalog row is the analogue of a non-raising
signature (and licenses branch elision, per the ladder).

**Interop dividend.** Errors are one of the major interop pieces. Before JR3,
the pending-flag protocol leaked beyond `lambda/js`: **four out-of-tree
translators** must speak it — `lambda/jube/jube_registry.cpp`,
`lambda/runtime/sys_func_registry.c`, `radiant/script_runner.cpp`,
`radiant/event.cpp` — each converting flag ↔ `ItemError` at its boundary and
guarding against a pending JS exception leaking into Lambda execution. Under
JR3 that entire protocol disappears:

- **Signal**: both sides speak the merged-lane ERROR-tagged Item, so
  cross-language propagation is *returning the value* — Lambda code calling a
  JS function through Jube gets `T^E`-compatible behavior for free (`^`/`^err`
  consume a JS throw directly), and JS `catch` consumes a Lambda raise
  directly. The "pending exception leaked across the boundary" bug class
  ceases to exist structurally.
- **Payload**: identity, with **zero wrapping in either direction** (JR3.2) —
  one struct serves both sides; `name`/`instanceof` resolve via the
  code↔class prototype table, so a Lambda error is a working JS Error and a
  JS error is a working Lambda error with no promotion or copy.
- **Async and concurrency**: promise rejections (the D6.1.2 Promise
  membrane) and concurrency mailbox messages already carry Items — with one
  error representation they carry errors across languages unchanged.
- **Generalizes to every guest**: with the JS-private pending model gone, the
  merged-lane ABI becomes the common error currency of Jube hosting — the
  Python and Bash guests inherit it rather than growing their own, which is
  D7.4.3's "JS pending-exception behavior is not reused as another guest's
  exception model" satisfied by construction.

#### JR3.2 — The exception object: `JsError` extends `LambdaError`

Ruling 2026-08-07 (supersedes the same-day error-map direction): Lambda
already has an error payload type — `LambdaError`
(`lambda/runtime/lambda-error.h:185`: `code`, `message`, `location`,
`stack_trace`, `help`, `details`, `cause`) — so the JS exception object
**extends that struct** rather than introducing a parallel layout:

- **Zero wrapping, both directions — `JsError` ≡ `LambdaError`.** The
  apparent gap was JS `name`, and it dissolves on inspection: per spec,
  `name` is a **prototype property, not instance data** — a fresh
  `new TypeError(m)` has *no own* `name`; `err.name` resolves through the
  prototype chain to `TypeError.prototype.name`. So `name` needs **no struct
  field at all**: it is derived from `code` by a small static bidirectional
  table, **code band ↔ JS error class/prototype** (type errors →
  `TypeError.prototype`, range/bounds → `RangeError`, reference →
  `ReferenceError`, syntax → `SyntaxError`, … default `Error`). The same
  table runs in reverse: every JS-side error construction assigns the
  matching `code` — and fills `location` from the throw site (the transpiler
  carries source positions) — so Lambda-side consumers always see a
  meaningful code and location.
  Custom names (`class MyError { … this.name = "MyError" }`) are ordinary
  property writes — they land in the overflow map like any user property,
  never in the struct. The remaining JS extras — `thrown_value` for
  non-object throws and the lazily-attached user-property overflow map — ride
  the **existing `details` slot** of `LambdaError`, allocated only when
  needed. Net: one concrete struct serves both sides; `it2err` consumers (3
  files) and every Lambda-side base-field reader work unchanged; a
  Lambda-raised error is admitted into JS **unmodified** (`e.message` /
  `e.stack` / `e.cause` read the base fields, `e.name` and `instanceof`
  resolve via the code-selected prototype, writes lazily attach overflow); a
  JS-thrown error is admitted into Lambda **unmodified** (`err is error`,
  `match`, `^`/`^err`, and base-field reads per S7.9). No promotion, no
  copies, identity preserved in both directions — Jube-boundary error
  crossing (D7.4.3) is **identity**, full stop.
- **In flight**: the propagating Item is the `LMD_TYPE_ERROR`-tagged
  `JsError*`. **At rest** (catch binding, stored in a variable), the same
  struct presents as an ordinary JS object of class Error — struct-backed
  reads for `message`/`stack`, arbitrary own-property writes routed to the
  overflow map. Throwing an existing Error object is a **retag of the same
  pointer** (zero allocation, identity preserved); `catch` retags back.
- **Resting-state representation** (ruling 2026-08-07, resolves §9 Q1b —
  Option 2, no new TypeId): `LambdaError` gains a **Map-compatible header
  prologue** (TypeId = `LMD_TYPE_MAP`, `TypeMap*` — the same header trick
  `JsPromise` uses today, made permanent and principled). At rest the error
  *is* a Map of class Error: its `TypeMap` is the **shared class-Error
  shape** describing the struct fields as fixed-offset slots, `.stack` is an
  accessor on that shape (the lazy read gate), and all existing Map
  machinery — property paths, shapes, even ICs — works on it unchanged. A
  dedicated error TypeId was rejected: it would ripple through every TypeId
  switch in the shared Lambda/JS runtime (D2 impact) for a cold path.
  Consequences owned: Lambda-side error constructors initialize the header
  facet; **static C14 fault records carry a NULL shape until first surfacing
  patches in the shared class-Error shape** (the fault path already runs
  conversion code at the recovery boundary); `it2err` consumers are
  unaffected (same struct, fields after the prologue). The selection
  criterion versus JR7: an **error is a value holder** — its fields *are* the
  JS-visible properties, so Map-over-struct-slots fits; a **promise is a
  native machine** whose state is spec-internal, so JR7 presents it as a
  **VMap** (the existing host-object mechanism). Two existing presentations,
  one stated criterion — no new mechanism either way.
- **`throw <non-object>`** (`throw 42`) uses a small `JsError` with
  `thrown_value` set; `catch` unwraps to the original value, so the carrier's
  identity is unobservable and carriers are reusable.
- **Pre-created commons come for free**: the C14 fault regime *already*
  stores faults in static non-allocating `LambdaError` records
  (`is_static`, `LambdaFaultRecord`) — the JS side adds pre-created
  stack-overflow/OOM `JsError` singletons on the identical pattern, and the
  map-payload design's static-map problem disappears. Observable
  `new Error()` stays freshly allocated (spec identity) but with a shared
  shape and **lazy stack capture** (V8 practice — the profiling pass measured
  eager stack symbolication inside `js_new_error_with_name_stack`).
- **Stack trace: adopt and adapt Lambda's capture pipeline** (ruling
  2026-08-07, resolves §9 Q1a). JS `.stack` reuses `err_capture_stack_trace`
  (`lambda-error.cpp:582`) — the zero-normal-overhead FP-chain walk that
  already traverses MIR JIT frames (no shadow stack; nothing runs on
  successful calls) with its two-tier resolution (per-script `debug_info`
  table for JIT frames, filtered `dladdr` for native ones) — **re-phased
  lazily**: error construction performs only the walk and stores the raw
  return-address array; the first `.stack` read through the class-Error read
  gate runs the existing resolution tiers and formatting once, then caches.
  `LambdaError.stack_trace` becomes **tri-state** — raw PC array → resolved
  `StackFrame` list → formatted string — so Lambda's own error constructors
  (`lambda-eval.cpp:152` et al., eager today) inherit the same lazy win.
  Lifetime is sound by existing ownership: raw JIT PCs are valid exactly as
  long as `script->debug_info`, and errors do not outlive their realm.
  Pre-created singletons skip capture entirely (fixed text). No new
  mechanism: the existing pipeline is split in half and the expensive half
  deferred.

JIT call sites branch on the ERROR tag exactly where `jm_emit_exception_route`
branches today — minus the separate poll call. `JsExceptionState` now contains
only the fault-bridge slots. The exc-track analysis is a branch-elision hint
driven by catalog `exception_effect` (`PRESERVES` rows emit no branch). GC
note: ERROR-tagged Items are heap references and must be traced as such
(D8.4.3).

**The scalar-return rule.** Raw-scalar returns (MIR_T_D/I64 lanes) cannot
carry a sentinel — this is *why* polling existed. Survey: only **4 helpers**
are emitted with raw-double returns (`js_get_number`, `js_math_ceil_d`,
`js_math_pow_d`, `js_math_round`). Rule: **only infallible (`PRESERVES`,
non-throwing) helpers may return raw scalars**; fallible helpers return Item.
The audit is 4 rows today; the rule becomes a catalog lint.

**Deleted.** Poll emission (~153k sites), `js_check_exception` from the hot
ABI, 467 manual flag checks (mechanical rewrite to sentinel checks), the
poll-elision half of the OE tracker; DO15 resolves.

**Interactions.** New D-ruling required (§8). D7.4.3's "JS pending-exception
behavior is not reused as another guest's exception model" is *satisfied
better*: JS stops having a private signaling model at all. `void` helpers
return status Items or are audited infallible.

### JR4 — Object metadata: shape-carried class + one exotic ops table

**Evidence.** Five discriminators decide behavior (review §3.3); sentinel
properties (`__promise_idx`, `__instance_proto__`) are load-bearing;
`js_get_implicit_proto` synthesizes prototypes from class stamps with raw-name
probes inside the chain walk (7.0% prototype-walk cost).

**Design.** A one-word **JsClassMeta** carried by the TypeMap (extends D3.4):
`{class_id (~44 values), elements_kind, holey, callable, exotic}` — plus a
static per-class **ops table** (`get_own`, `define_own`, `delete`, `call`,
`construct`, `proto`) for exotic classes only (typed arrays, proxy, arguments,
host VMaps, DOM). Plain objects never consult ops. The prototype is resolved
through one place: shape meta → memoized `__proto__` entry or class intrinsic
table (subsumes Tune1-P3).

**Deleted.** `JsClass` side-stamping as a separate mechanism (folds into
shape meta), `map_kind` checks outside storage code, all sentinel properties,
`js_get_implicit_proto`'s string probes, the per-kind special cases spread
through property code.

### JR5 — Builtins are function values; one call convention

**Evidence.** ~930 catalog rows dispatch via `js_dispatch_builtin`
(**2,479-line switch**, `js_runtime.cpp:11178`) and three per-kind name
switches (≈3.8k lines more); 510 strcmps in one TU; 12 call-entry variants;
method semantics (extraction, `.call/.apply`, monkey-patching) re-implemented
case by case.

**Design.** At realm boot, catalog rows are installed as **Lambda function
values** (D6.2) on their prototypes: a shared-shape builtin function object
carrying `{C entry, arity, flags, name id}`. Method dispatch is property get
(JR6) + call — one mechanism, and one **call convention** for user functions,
builtins, bound functions, and class constructors:
`call(callee, this, args*, argc, new_target) → Item`. The two `js_call_*`
survivors are the public entry and the raw internal (pre-rooted) path. V8's
practice arrives later as a *feedback* optimization (JR8): hot sites
devirtualize to direct C calls when the feedback slot proves a stable callee —
without ever reintroducing name dispatch.

**Deleted.** The three dispatch switches (≈6.3k lines), name-string travel
through call paths, `js_builtin_catalog_find` from hot paths (boot-time only),
10 of 12 call-entry variants.

### JR6 — One property path + elements kinds

**Evidence.** 54 entry points; non-IC path carries 7.5× the IC path's calls;
array generics pay per-element interning + prototype walks
(`js_array_generic_reverse → js_has_property → …`, Tune1 P2).

**Design.** One internal pair with explicit tiers:

```
js_get(obj, JsName, FeedbackSlot*) :
  own shaped slot → feedback fast hit → shape lookup → exotic ops → proto chain
js_get_index(obj, i64) :
  elements store by elements_kind (ARRAY_NUM int/float, ARRAY, holey check)
  → exotic ops → proto chain (holey only)
```

plus `js_set`, `js_set_index`, `js_define`, `js_delete`, `js_has` — **≤8
public entry points total**. Elements kinds ride Lambda's existing containers:
`ARRAY_NUM(elem_type)` for packed numeric, `ARRAY` for tagged, one holey bit
in shape meta; dense receivers skip per-element `HasProperty` entirely
(spec-equivalent for no-hole, clean-prototype receivers — the guard bit from
the Tune12 array-IC line). Lowering selects *tiers by hint*, never *different
helpers by era*.

**Deleted.** The other ~46 entry points, `js_map_get_fast{,_ext}` as public
API (becomes the internal shape tier), the P0 corrupt-pointer guard (root
cause fixed or asserted, per rule 1), per-element name synthesis.

### JR7 — Promises and jobs on the GC heap

**Evidence.** Static 8,192-slot table, fixed `[8]` reaction arrays, wrapper
maps with `__promise_idx`, and a 7-range × 8,192 per-epoch root registration
storm measured at **7.1% of working CPU** with 100% of samples from
`js_alloc_promise`.

**Design** (representation ruling 2026-08-07). A promise is **one GC-heap
native struct presented as a VMap** (`LMD_TYPE_VMAP` — the existing
vtable-dispatch container): lean fields (state, result Item, growable
reaction vector, flags) that native promise machinery touches directly —
allocation, resolve/reject, `.then` append, job drain, and `await` all run at
struct-field speed with no Item boxing or shape indirection on the hot path.
JS-side reads route through the VMap vtable — slightly slower, and
deliberately so: real code touches promise *instances* only via
`Promise.prototype` methods and `await`; instance own-property access is
cold. Two further reasons VMap is the right fit:

- **Spec correctness by construction**: promise state is spec-*internal*
  (`[[PromiseState]]`, `[[PromiseFulfillReactions]]`) — a spec promise has no
  own properties. A Map-with-slots presentation would leak state as
  enumerable own properties unless hiding machinery were added; the VMap
  exposes exactly what its vtable chooses (nothing, plus lazily-attached
  expando storage for user `p.foo = 1` writes).
- **GC is already wired**: `gc_heap.c` carries `vmap_trace` /
  `vmap_destroy` callbacks, so tracing the promise's Item fields uses the
  existing VMap hook — no new GC mechanism.

The job queue is one runtime-state deque of heap items, rooted once via the
existing `js_root_range_ensure_registered` pattern. No caps, no wrappers, no
index identity, no epoch re-registration — the GC sees promises like any
other value (this *is* the JR1 principle applied).

**Deleted.** `JsPromise` static table + `JS_PROMISE_STATE_MAX`, `[8]` caps,
`js_promise_to_item` wrapper machinery, the register-roots-once loop
(`js_runtime.cpp:31908`), Tune1-P1 becomes moot.

### JR8 — Feedback vectors: one IC mechanism, out of the code

**Evidence.** `JsLoadIC`/`JsStoreIC`/callsite caches/shape-guard sites are
four site-state shapes (`js_runtime.h:106/114/126`); IC structs are allocated
from the transpiler pool and referenced by absolute address from JIT code;
the newest IC generation carries a fraction of traffic (§JR6 evidence).

**Design.** Per-function **feedback vector**: an array of one `FeedbackSlot`
union type `{load, store, call, guard}` allocated alongside the function's
const pool and addressed the same way (the emitter's existing
`consts_reg`/`consts_bss` mechanism — reuse, not a new registration path).
Lowering allocates slot indices; helpers take `FeedbackSlot*`. State survives
re-transpile of the same source (batch preamble reuse), telemetry reads one
array, and a future re-lowering tier has its input ready (KIV, out of scope).

**Deleted.** The four per-site struct kinds and their pool/lifetime special
cases; IC-state plumbing through `js_exec_profile` becomes one dump loop.

### JR9 — Module management: one registry

**Evidence.** JS carries a private require/CJS loader
(`js_mir_entrypoints_require.cpp`, 2,228 lines) beside Lambda's
`module_registry` (D7); two caching/instantiation lifecycles.

**Design.** JS modules (ESM and CJS) register, cache, and instantiate through
the Lambda module registry and Jube packaging; Node semantics (extension
probing, `node_modules` walk, JSON modules, compile cache) become a
**resolver + format plug-in** consulted by the one loader. The Node compile
cache keys into the same registry entry rather than a parallel table.

**Deleted.** The private loader's duplicate registry/caching layers (the
resolver logic itself is kept — it is genuine Node semantics).

### JR10 — Decomposition of `js_runtime.cpp`

After JR2–JR8 delete their share, split the remaining core along the now-real
seams: `js_object.cpp` (shape/property), `js_array.cpp` (elements),
`js_call.cpp` (calling convention), `js_promise.cpp`, `js_string.cpp`,
`js_class.cpp`, `js_builtin_boot.cpp` (catalog install). Gate: **no TU over
8k lines**; statics promoted per rule 13, moved once.

## 4. What does not change

Item representation and tagging (D2), TypeMap shape internals and transitions
(D3.4), GC algorithm and rooting protocol (D5 — promises simply join it),
scalar-home ABI (D5.2; JR3 narrows who may use raw lanes), MIR Direct pipeline
and emitter (D8.1.1), helper-effect catalog as the ABI contract (D7.4.3),
Node/DOM compat layers' *behavior* (they migrate to the new entry points
mechanically), and all S-layer JS semantics — this is an implementation
redesign, not a semantics change.

## 5. Quality gates (measured)

### 5.1 Gate 1 — mechanism census (must reach target, verified by grep census)

| Census | Baseline | Target |
|---|---:|---:|
| Property get/set/access entry-point definitions | 54 | ≤8 |
| Name/key representations in property APIs | 4 | 1 |
| Object discriminators consulted in property code | 5 | 2 |
| Builtin dispatch mechanisms | 3 | 1 |
| Error channels | 2 | 1 |
| Promise representations | 2 | 1 |
| IC site-state struct kinds | 4 | 1 |
| `js_call_*` entry variants | 12 | 2 |
| Module loader/caches for JS | 2 | 1 |
| Emitted exception-poll sites (batch) | 153,725 | 0 |
| Sentinel-property conventions | ≥2 | 0 |

The census script (extension of the review's greps) lives in `utils/` and
runs in the per-phase gate; a phase that adds a mechanism without deleting
its predecessor **fails the gate** (review §6 item 7, made enforceable).

### 5.2 Gate 2 — LOC (net-negative per phase; targets at completion)

| Area | Baseline | Target |
|---|---:|---:|
| Core semantic runtime (`js_runtime` + globals + props/attrs/typed/regex/state/value) | ~75k | **≤60k** |
| `js_runtime.cpp` single TU | 40,730 | dissolved; no TU >8k |
| Builtin dispatch switches | ≈6.3k | ≈0 (bodies become per-builtin functions) |
| Exception plumbing (polls, tracker, manual checks) | ≈2k + 153k emitted sites | ≤0.5k |
| MIR lowering | 44,663 | ≤42k (poll/variant-selection code removed; feedback plumbing added) |

New mechanisms (feedback vectors, class-ops tables, builtin boot) add code;
the same phase must delete more than it adds. LOC is counted by `wc -l` over
`lambda/js` core buckets, recorded per phase in the phase doc.

### 5.3 Behavior and performance gates

- 327-test `test_js_gtest`, js262 gate, GC-stress, layout/DOM baselines green
  per phase; goldens byte-identical.
- Bench suite (Tune line) non-regressing; the **profiling harness**
  (`JS_Profiling_Helpers.md` §6 protocol) re-run per phase — expected
  end-state on the batch workload: helper share 43.5% → **≤30%**, lookup
  bucket 8.3% → ≤3%, root-registration 7.1% → ~0, wall −10–20%.

## 6. Phasing

Ordered so the tree stays green and each phase's deletions are immediate:

| Phase | Content | Depends on | Retires |
|---|---|---|---|
| R1 | JR2 names (JsName everywhere) | — | raw-key probes, per-lookup interning |
| R2a | JR3 in-band signaling — **landed** | R1 (mechanical churn shared) | polls, tracker half, DO15 |
| R2b | JR3.2 payload unification — **landed** | R2a + shared class-Error shape (struct-backed slots, `.stack` accessor) | today's JS Error map construction |
| R3 | JR5 builtins as values + one call convention | R1 | dispatch switches, 10 call variants |
| R4 | JR6 property path + elements kinds | R1, R3 | 46 entry points, P0 guard |
| R5 | JR4 class meta + exotic ops | R4 | JsClass side-stamp, sentinels |
| R6 | JR7 promises/jobs | R5 | static tables, wrapper, root storm |
| R7 | JR8 feedback vectors | R4 | four IC struct kinds |
| R8 | JR9 modules; JR10 file split | R2–R7 | private loader layers; the 40k TU |

R2 was split so the wide mechanical change and representation change could be
verified independently. Both R2a and R2b are now landed in the Tune1
implementation; E8 validation is complete and the remaining broad Node
compatibility failures are outside the JR3 error-lane scope.
**Implementation record:** `vibe/jube/JS_Tune1_Runtime.md` (E0–E8).

Tune1 mapping: P1 ⊂ R6 (superseded by construction), P2 ⊂ R1+R4, P3 ⊂ R5,
P4's constructor-shape work lands naturally in R3/R5, P5 becomes the JR3
catalog lint. If a quick win is wanted before R6 lands, Tune1-P1a remains a
valid stopgap.

## 7. Risks

- **R2 is the widest mechanical change** (467 manual checks + all lowering
  call sites). Mitigation: catalog-driven rewrite (effects metadata names
  every fallible helper), differential fixture runs per batch of helpers, and
  the scalar-return lint from day one.
- **Observable-behavior drift** in R3/R5 (method identity, `Function.name`,
  own-property order of prototypes, promise wrapper identity if leaked).
  Mitigation: js262 + targeted fixtures *before* each migration; JR7's open
  question 2 in the review (wrapper identity leak audit) is a precondition.
- **Node/DOM layers** call old entry points ~everywhere. Mitigation: R-phases
  keep thin deprecated wrappers *within the phase only*; the census gate
  counts them, forcing same-phase cleanup.
- **Perf regressions from uniformity** (e.g., builtin call via function value
  vs direct switch). Mitigation: JR8 direct-call feedback devirtualization;
  bench gate per phase; the profile shows dispatch *overhead* is the current
  cost, not the switch bodies.

## 8. Formal-spec impact (rule 17)

- **D8.4.3 (landed)**: JS helper error signaling is in-band via the returned
  Item — the same merged-lane ABI Lambda's `T^E` callees already use
  (`transpile-mir.cpp:5354`; TE-15). The propagating value is an
  `ItemError`-tagged **`LambdaError*` shared by both sides** (base fields
  `code`/`message`/`location`/`stack`/`cause`; JS extras — thrown value,
  user-property overflow — ride the existing `details` slot lazily; `name`
  and `instanceof` derive from a bidirectional code ↔ error-class prototype
  table, never from a struct field) presented to JS at rest as a **Map of
  class Error** — a Map-compatible header prologue on `LambdaError`, the
  shared class-Error shape over struct-backed slots — with zero
  cross-language wrapping and no new TypeId; the GC traces ERROR-tagged Items
  as heap references (D2 footnote); pre-created singleton errors are
  permitted where identity is unobservable (the C14 static fault-record
  pattern, unchanged); raw-scalar returns are restricted to catalog-verified
  infallible helpers (the existing TE-15/`can_raise` rule applied to JS;
  revises the D5.2 impl footnote; resolves **DO15**).
- **D3.4 extension**: shape-carried JsClassMeta (class id, elements kind,
  holey, exotic) as the sole object discriminator beside TypeId.
- **D6.2/D6.4 note**: builtin callables are ordinary function values created
  from the catalog at realm boot; the catalog remains the ABI/metadata source.
- **D7.3 note**: JS module instantiation flows through the Jube/module
  registry; Node resolution is a resolver plug-in.
- **D4.6/DO16 note**: JS property keys are NamePool-canonical; interning
  moves to compile/boot time.

The JR3 contract landed as D8.4.3 in `doc/Lambda_Formal_Design.md` version
1.5.0; this doc records the implementation state and evidence.

## 9. Open questions

1. **JR3** — *fully resolved 2026-08-07*: the sentinel is `ItemError` itself;
   the exception object shares the `LambdaError` struct (JsError ≡
   LambdaError, extras via `details`); commons are pre-created; `.stack`
   adopts and adapts Lambda's `err_capture_stack_trace` pipeline, re-phased
   lazily; resting state rides `LMD_TYPE_MAP` with the shared class-Error
   shape over struct-backed slots (no new TypeId — the D2 ripple was judged
   too costly for a cold path). Implemented; E8 validation is complete.
2. **JR5**: builtin function objects — shared immutable shape with a C-entry
   slot, or a distinct lightweight callable header? Cost target: ≤2 words over
   a plain function value; `Function.prototype` methods must see them as
   ordinary functions.
3. **JR6**: exact holey semantics on `ARRAY_NUM` (numeric arrays cannot store
   holes in-band) — promote-on-hole to `ARRAY`, or a side bitmap? V8 chose
   promotion (holey-double boxes); promotion matches Lambda's existing
   `ARRAY_NUM → ARRAY` widening.
4. **JR7**: unhandled-rejection tracking without the static table — epoch
   sweep over a weak list, or a small strong queue drained by the job loop
   (current design has both; one must win)?
5. **JR8**: feedback vector lifetime for `eval`/dynamic functions — pool-owned
   with the function, or GC-owned? (The const pool precedent says
   pool-owned.)
6. **JR9**: does the Node compile cache's integrity manifest
   (`update_jube_manifest_integrity.py` flow) key cleanly into module-registry
   entries, or does the registry need a content-hash field first?
7. Sequencing R2/R3 — *analyzed*: measured file surfaces are
   region-disjoint but share the two biggest TUs. R2's surface: the pending
   flag lives in 5 runtime files (js_runtime.cpp 400 refs,
   js_runtime_value.cpp 41, js_globals.cpp 12, js_runtime_state.cpp 11,
   js_child_process.cpp 3) + 4 lowering files (completion,
   hashmap_scope_utils, statement, function_class). R3's surface: the
   dispatch/switch regions of js_runtime.cpp, js_globals.cpp registration,
   js_runtime_builtin_registry.cpp, catalog files, a new boot TU + 2 lowering
   files (expression, calls_boxing). Overlap = js_runtime.cpp, js_globals.cpp,
   js_mir_statement_lowering.cpp — different *regions*, but R2's mechanical
   sweep would rewrite hundreds of check sites inside the very switch bodies
   R3 deletes. Preferred order therefore: land **R3's deletions first**, then
   run R2's sweep over the smaller tree; true parallelism is possible only
   with region ownership (R2 defers its js_runtime.cpp body sweep until R3
   merges).
