# LJS Runtime Redesign — One Mechanism per Concept, on Lambda's Mechanisms

**Date**: 2026-08-12  **Status**: JR2/JR3/JR5 IMPLEMENTED — JR6 DESIGN ADOPTED
**Tree anchor**: master `88aa5556c8` plus the Tune4 implementation worktree
**Companions**: `JS_Profiling_Helpers.md` (measured evidence),
`JS_Runtime_Review.md` (complexity findings), `JS_Tune1_Helpers.md`
(performance phases — subsumed by this design where they overlap),
`JS_Runtime_Name.md` / `JS_Tune3_Name.md` (adopted NameId design and
implementation), and `JS_Runtime_Callable.md` / `JS_Tune4_Callable.md`
(adopted Callable design and implementation plan)

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

### JR1 — Extend Lambda; never reinvent beside it

The governing principle; JR2–JR10 are its application per area. The JS
runtime keeps and deepens its alignment with the Lambda runtime: the Item
data model (D2),
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
| QuickJS: atoms (one interned name identity, int compare) | **Yes** | NamePool-owned `NameId`, **D4.6.1v2–D4.6.2v2** — the only semantic key identity |
| QuickJS: exception = in-band sentinel return + exception value in context | **Yes** | Lambda's in-band error signaling (`ItemError`, S-layer `T^E` model); the `LambdaError` carrier owns the payload |
| QuickJS: one `class_id` + exotic-methods table | **Yes** | TypeMap-carried class metadata + per-class ops table (extends D3.4) |
| QuickJS: builtins are function objects on prototypes | **Yes** | Lambda function values (D6.2) built from the existing catalog (D6.4/D7.4.3) |
| QuickJS: single property get/set path | **Yes** | One helper pair over shape/IC/exotic/proto tiers |
| QuickJS: refcount GC | **No** | Lambda precise GC stays (rule 15, D5.3) |
| QuickJS: interpreter | **No** | MIR Direct stays (D8.1.1) |
| V8: elements kinds (packed/holey × smi/double/tagged) | **Yes** | one per-array state machine over `LMD_TYPE_ARRAY_NUM`, tagged `LMD_TYPE_ARRAY`, and the existing sparse overlay |
| V8: feedback vectors — IC state per function, out of code | **Yes** | Per-function slot array via the emitter's existing const-pool mechanism (`consts_reg`/`consts_bss`) |
| V8: maps/hidden classes + transition discipline | Already present | TypeMap shapes; this design consolidates onto them |
| V8: handle scopes | Already present | `RootFrame`/`Rooted` (D5.3) — unchanged |
| V8: multi-tier JIT, deopt, generational GC, Torque | **No** | Out of scope; one MIR tier remains |

## 2. Target architecture

### 2.1 One mechanism per concept — before → after

| Concept | Baseline before its owning phase (count) | Target (single mechanism) |
|---|---|---|
| Property key | raw `(chars,len)`, `PropertyKeyRef`, `String*`, pool strviews (**4**) | **`NameId`** owned/resolved by NamePool (**1**); strings are observable materialization only |
| Object discrimination | TypeId, `map_kind`, `JsClass` (44), shape flags, sentinel props (**5**) | TypeId + **shape-carried JsClassMeta** (class/prototype/exotic policy); callability stays per value (**2**) |
| Array element storage | `ARRAY_NUM`, tagged `ARRAY`, holes, sparse overlays, descriptor side paths | one per-array **JsElementsKind** state machine over the existing physical forms |
| Builtin dispatch | catalog→id switch, per-kind name switches, direct-impl calls (**3**) | builtins are **function values** installed on prototypes; dispatch = get + call (**1**) |
| Property access | **54** entry points | eight receiver-aware semantic operation families; caches and phase adapters stay outside the core |
| Error signal | pending flag + polls *and* `ItemError` (**2**) | **in-band**: the returned Item *is* the ERROR-tagged `LambdaError*` (**1**, landed in Tune1) |
| Promise | static record table + wrapper map w/ `__promise_idx` (**2**) | one GC-heap native struct presented as VMap (**1**) |
| IC state | `JsLoadIC`, `JsStoreIC`, callsite caches, shape-guard sites (**≥4**) | one **feedback slot** union per site, in a per-function vector (**1**) |
| Call entry | 12 `js_call_function*`/invoke variants | one public entry + one internal raw path (**2**) |
| Module loading | Lambda registry *and* private JS require/CJS loader (**2**) | Lambda module registry; Node resolution as a resolver plug-in (**1**) |

### 2.2 Alignment with the Lambda runtime (explicit, per JR1)

- **Data model**: JS objects remain `Map` + `TypeMap`; JS arrays remain Lambda
  arrays — dense numeric arrays use `LMD_TYPE_ARRAY_NUM` (elem_type already
  selects int/int64/float), generic arrays use `LMD_TYPE_ARRAY`, and sparse
  arrays retain the existing companion storage. JR6 records elements state on
  each array instance, not in shared shape metadata, and adds no container
  type (D2.6.1/D2.6.2).
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

### JR2 — Property keys: NameId-first, NamePool-owned

**Status: implemented.** The adopted authority is **D3.4.4v2** and
**D4.6.1v2–D4.6.2v2**; `JS_Runtime_Name.md` records decisions RN1–RN16 and
`JS_Tune3_Name.md` records the completed migration.

**Baseline evidence.** The name/shape lookup infrastructure cost **8.3% of
working CPU**; `js_find_shape_entry` interned its key on *every* raw probe
(`js_property_attrs.cpp:67`), and array generics interned a synthesized index
name per element. Four key representations coexisted.

**Adopted design.** One semantic key identity: `NameId`. NamePool assigns,
owns, and resolves it; no shape, IC, registry, transition, Symbol, private
name, or routing decision compares a `String*` address (**D4.6.1v2**).
Generated catalog IDs may be MIR immediates. Arbitrary static and dynamic
names load their context-linked IDs through the existing module-table and
NamePool pipeline, never through a code-baked context value
(**D4.6.2v2**, **D5.4.3**).

All named-property semantic cores take `NameId`. Static lowering passes only
the linked ID. A computed wrapper performs `ToPropertyKey`, derives or interns
the ID, and retains the original key `Item` only until an exotic/Proxy or
reflection boundary makes its spelling or Symbol value observable. A
`String*` resolved from NamePool is therefore a **NameRef materialization**,
never identity. Integer keys stay on JR6's dedicated indexed path; cached
ordinary array-index classification may select that path from a string key,
while TypedArray canonical-numeric-index strings retain their distinct
ECMAScript classifier.

Every JS-created `ShapeEntry` carries non-zero `name_id` and compares it
exactly (**D3.4.4v2**). `NAME_ID_NONE` remains only at the explicit id-less
Input seam, where key kind, length, and bytes confirm a match; Symbol and
private entries never use that fallback.

**Retired by the implementation.** Pointer identity and pointer-key
compatibility paths; raw-key semantic probes; per-lookup `heap_create_name`
canonicalization; duplicate hot-path hashing; and well-known-name ID
re-derivation. Raw bytes survive only at the explicit Input/materialization
boundary and never define a second runtime identity relation.

**Interactions.** Statically known names allocate/link before execution.
Previously unseen computed/eval/REPL names allocate append-only in the
owner-thread dynamic NamePool child under RN12/**DO16**; a resolved lookup
does not re-intern. Cross-context Input rebinding remains the explicitly
deferred RN-D5 issue and does not alter runtime property identity.

**Design and implementation record**: `vibe/jube/JS_Runtime_Name.md`
(RN1–RN16) and `vibe/jube/JS_Tune3_Name.md` (R0–R7). They supersede the
pointer-identity parts of `vibe/Lambda_Design_Name_Identity.md`; no
pointer-based `JsName` or `PropertyKeyRef` compatibility ABI remains.

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
carry a sentinel — this is *why* polling existed. Rule: **only infallible
(`PRESERVES`, non-throwing) helpers may return raw scalars**; fallible helpers
return Item. Tune1 did not complete that catalog audit; Tune2 supplies the
emitter value-class gate and standing catalog lint against D8.4.3.

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
`{class_id (~44 values), flags, prototype policy, property_ops}` — with one
static property-ops table (`get`, `set`, `define`, `delete`, `has`, `ownKeys`,
`proto`) for exotic classes only (typed arrays, proxy, arguments, host VMaps,
DOM). Plain objects never consult ops. The prototype is resolved through one
place: shape meta → memoized `__proto__` entry or class intrinsic table
(subsumes Tune1-P3). Array elements state is per container and is not a
`JsClassMeta` field. Callability/constructibility remains a per-value Callable
capability under D6.2.2v2; class metadata may select an exotic protocol
implementation but cannot grant callability by class alone.

**Deleted.** `JsClass` side-stamping as a separate mechanism (folds into
shape meta), `map_kind` checks outside storage code, all sentinel properties,
`js_get_implicit_proto`'s string probes, the per-kind special cases spread
through property code.

### JR5 — Builtins are function values; one callable kernel

**Status: implemented.** The adopted authority is **D6.2.2v2**;
`JS_Runtime_Callable.md` records JC1–JC12 and `JS_Tune4_Callable.md` records
the completed C0–C8 migration and release evidence.

**Baseline evidence.** ~930 catalog rows dispatch via `js_dispatch_builtin`
(**2,479-line switch**, `js_runtime.cpp:11178`) and three per-kind name
switches (≈3.8k lines more); 510 strcmps in one TU; 12 call-entry variants;
method semantics (extraction, `.call/.apply`, monkey-patching) re-implemented
case by case.

**Design.** At realm boot, catalog rows are installed as **Lambda function
values** (D6.2) on their prototypes: a shared-shape builtin function object
carrying `{C entry, arity, flags, NameId}`. Method dispatch is property get
(JR6) + call — one mechanism, and one **callable kernel with distinct
`[[Call]]` and `[[Construct]]` capabilities** for user functions, builtins,
bound functions, and class constructors. Per **D6.2.2v2**, call receives
`(callee, this, args*, argc)` while construct receives
`(callee, args*, argc, newTarget)` explicitly; a pending one-shot `newTarget`
handoff is forbidden. Ownership-qualified public, result-home, and pre-rooted
adapters enter those same kernels. V8's
practice arrives later as a *feedback* optimization (JR8): hot sites
devirtualize to direct C calls when the feedback slot proves a stable callee —
without ever reintroducing name dispatch.

**Deleted.** The three dispatch switches (≈6.3k lines), name-string travel
through call paths, `js_builtin_catalog_find` from hot paths (boot-time only),
10 of 12 call-entry variants.

**Implemented result.** Per-callee call/construct entries, typed factories,
realm-local catalog bindings, explicit `newTarget`, and observable
`Get -> Call` now own the mechanism. The final census is zero for the builtin
dispatcher, pending construct state, ambiguous native factory/casts,
receiver/name host and intrinsic dispatch, and property-miss synthesis. The
sole compatibility boundary is the named JR4 class-map construct bridge.

### JR6 — One property path + elements kinds

**Evidence.** 54 entry points; non-IC path carries 7.5× the IC path's calls;
array generics pay per-element interning + prototype walks
(`js_array_generic_reverse → js_has_property → …`, Tune1 P2).

**Adopted contract (2026-08-11).** JR6 owns the semantic property kernel,
array representation state, and prototype-index guard. It does not own
inline-cache policy (JR8) or the final class ops table (JR4).

#### JR6.1 — Eight receiver-aware semantic operations

The public semantic ABI has exactly these operation families:

1. `Get(target, key, receiver)`
2. `Set(target, key, value, receiver)`
3. `DefineOwn(target, key, descriptor)`
4. `Delete(target, key)`
5. `HasProperty(target, key)`
6. `HasOwn(target, key)`
7. `GetOwnPropertyDescriptor(target, key)`
8. `OwnKeys(target)`

These map one-to-one to eight public C symbols. The name/index lane tag and
its `NameId` or `uint32_t` payload are scalar ABI arguments, not a stored key
object or a second identity mechanism; name-only/index-only helpers remain
internal tiers rather than another exported family. A computed call may also
carry its rooted `ToPropertyKey` result as an optional observable payload until
an exotic/Proxy/reflection boundary consumes it; that Item never participates
in identity or ordinary lookup, and static calls materialize lazily from the
lane if such a boundary is reached (D5.3, D4.6.1v2).

At the C/MIR boundary, `key` is a resolved lane: either `NameId` or an
ordinary array index. A computed-key wrapper performs `ToPropertyKey` once,
retains the original `Item` only when Proxy/reflection/exotic code must observe
it, and otherwise enters the same lane. The ordinary index classifier accepts
exactly canonical decimal property names in `0..2^32-2`; `2^32-1`, `-0`, and
non-canonical spellings stay named. TypedArray canonical-numeric strings use
the TypedArray exotic classifier instead, including its distinct `-0`, NaN,
infinity, integral, and bounds rules. This applies D4.6.1v2/D4.6.2v2.

`Get` and `Set` carry the original receiver through every prototype step and
accessor/Proxy call. `Set`, `DefineOwn`, and `Delete` return an explicit
success/failure-or-error result; assignment strictness and the throwing versus
boolean Object/Reflect API choice remain in their callers. The core never
discovers strictness from ambient state. `DefineOwn` never invokes an inherited
setter, `Delete` affects only an own property, and `HasProperty` includes
prototypes without invoking ordinary getters. Getter/Proxy lookup occurs before
argument evaluation for a source method call under **JC8**; D6.2.2v2 then
governs dispatch through the resolved callee's per-value capability.

#### JR6.2 — One semantic path, explicit phase adapters

The lookup pipeline is:

```text
site cache probe (outside JR6 core; a miss is observationally invisible)
  -> class/storage classification
  -> current exotic internal operation, when the class/key requires it
  -> ordinary own elements or shaped slot
  -> prototype loop, retaining the original receiver
```

JR6 contains exactly one transitional `js_property_exotic_adapter` for
current Proxy, TypedArray, DOM/host, Arguments, and legacy `map_kind`
behavior. JR4 replaces that adapter's implementation with `JsPropertyOps`
without changing the eight semantic operations. JR8 replaces the outer cache
wrappers with the unified feedback vector without changing JR6 semantics or
adding a `FeedbackSlot*` parameter to the core. Neither adapter may duplicate
receiver propagation, key conversion, descriptor rules, or prototype
traversal. Lowering selects tiers by hint, never different semantic helpers by
era. Initial cache admission is limited to guarded ordinary data-property
cases; accessors, Proxy traps, descriptors, and unproven prototype results
miss to the core.

#### JR6.3 — Per-array elements state

`JsElementsKind` is stored per array instance in the three currently reserved
bits of `Container.array_flags`; `Container.reserved_state` remains available
for the element's semantic contract. The state is not stored in `TypeMap` or
shared class metadata. The four ordinary-array states are:

| State | Physical representation | Invariant |
|---|---|---|
| `PACKED_NUMERIC` | `ARRAY_NUM` | every index below `length` is present and numeric |
| `PACKED_TAGGED` | tagged `ARRAY` | every index below `length` is present |
| `HOLEY_TAGGED` | tagged `ARRAY` | slots may contain the existing hole sentinel |
| `SPARSE_TAGGED` | logical array plus tagged dense prefix and `SparseArrayMap` | distant indices need not allocate intervening storage |

The companion `Map` and its `TypeMap` shape are an orthogonal overlay for named
properties, indexed descriptors/accessors, and `length` attributes; the
companion is not an elements kind. Arguments/content arrays and TypedArrays
are excluded from this ordinary-array state machine. TypedArrays retain their
canonical-numeric exotic semantics and never acquire holes; any
`ArrayNum`-like backing is an
implementation detail rather than an ordinary-array state.

For an ordinary own element, `GetOwnPropertyDescriptor` synthesizes the
default writable/enumerable/configurable data descriptor unless the companion
contains an authoritative descriptor/accessor; a hole is absent. `OwnKeys`
merges elements and the companion without duplicates: array-index keys in
ascending numeric order, then other strings in insertion order, then Symbols
in insertion order. Exotic `OwnKeys` results retain their own validation and
ordering rules.

This is an application of D2.6.1/D2.6.2: representation may change, but array
value semantics and identity do not.

#### JR6.4 — Transition matrix and promote-on-hole rule

| Event | From | To / required action |
|---|---|---|
| compatible indexed write or contiguous append | `PACKED_NUMERIC` | stay numeric; widen integer storage to double storage when required |
| present nonnumeric value, including `undefined` | `PACKED_NUMERIC` | atomically box into `PACKED_TAGGED` |
| successful deletion of a present element, length growth, or a non-sparse gapped write | packed state | atomically promote to `HOLEY_TAGGED` |
| sufficiently distant/sparse write | packed or holey state | promote to `SPARSE_TAGGED` without proportional allocation |
| density crosses the existing measured dense threshold | `SPARSE_TAGGED` | optionally compact to `HOLEY_TAGGED`; semantics cannot depend on the threshold |
| indexed accessor or non-default descriptor | numeric or plain tagged storage | preserve values in tagged storage and install the authoritative companion overlay |
| length shrink | any ordinary state | transactionally delete indices at or above the new length, respecting non-configurable entries |

Every row describes a successfully admitted operation. A failed write/delete,
a rejected non-writable length change, or a no-op leaves the representation
unchanged.

`PACKED_NUMERIC` cannot represent holes. JR6 therefore adopts promote-on-hole
and rejects a side bitmap: the first operation that actually creates absence
atomically promotes to tagged holey storage. Storing `undefined` creates a
present tagged element and is never a hole. A no-op deletion of an already
absent index does not force a transition. Representation changes preserve
object identity, named properties, prototype, logical length, and descriptors,
and all allocations/intermediate values are rooted under D5.3.

Transitions are monotone with respect to specialization in JR6: tagged/holey
arrays do not automatically respecialize to `PACKED_NUMERIC`. A later measured
optimization may add guarded respecialization without changing this semantic
contract.

#### JR6.5 — Prototype-index clean guard

Each realm owns `{epoch, clean}` state for the intrinsic
`Array.prototype -> Object.prototype` chain, applying D5.4.1-D5.4.4.
Defining, deleting, or changing a canonical array-index
property/descriptor/accessor on either intrinsic prototype, or changing either
prototype link, increments the epoch and conservatively recomputes `clean`.
Removing the last indexed property may make the chain clean again.

An own present dense data slot may bypass prototype lookup without consulting
this guard. Treating a hole/absent own slot as absent without prototype
traversal is valid only when the receiver uses that exact intrinsic chain,
`clean` is true, and its captured epoch still matches. Any callback or other
user-code re-entry is a guard boundary: array methods reload elements kind,
companion/descriptor state, and the realm epoch before resuming a hoisted
fast loop. A custom prototype or any Proxy in the chain uses the generic
semantic path for absent slots. Receiver-owned indexed descriptor/accessor
overlays are checked separately and are never justified by the realm clean
bit. The guard state is selected from the receiver's intrinsic prototype
identity, never merely from the active caller realm; an unrecognized or
foreign chain falls back rather than borrowing the wrong epoch.

#### JR6.6 — TypeMap hard invariant

Per **D3.4.1** and **D3.4.5**, every non-null
`Map.type` that reaches the ordinary internal shape/property/class tier is a
valid `TypeMap*` whose layout exactly describes `Map.data`. JR6 replaces the
P0 plausibility-and-recovery branch with a standard C `assert` in debug builds:

```c
#ifndef NDEBUG
assert(!map->type || typemap_ptr_is_plausible(map->type));
#endif
```

The release build contains neither the assertion/predicate call nor a recovery
branch; it trusts the invariant and proceeds directly. An implausible pointer
must never become an ordinary property miss, `ItemNull`, `undefined`, or
`JS_CLASS_NONE`. A null `type` remains legal only on representations that
explicitly permit no shape. Storage kinds whose `type` field is deliberately
not a `TypeMap*` (for example a transitional synthetic-iterator sentinel) must
dispatch by their explicit kind before entering this tier. True external or
cache-validation boundaries may validate their own inputs, but may not reuse
the retired P0 behavior to hide corruption of an internal JS object.

The `lib_marked.js` family was caused by MIR last-closure environment tracking,
not property lookup. Its focused closure/block-shadow regressions remain the
root-cause gate; the debug assertion is a diagnostic invariant, never a
correctness fallback.

**Deleted.** The other ~46 entry points, `js_map_get_fast{,_ext}` as public
API (becomes the internal shape tier), the release-build P0 corrupt-pointer
check and its log-and-return-miss recovery, per-element name synthesis,
per-operation prototype numeric scans on an unchanged clean intrinsic chain,
and direct semantic branches from IC wrappers or the transitional exotic
adapter.

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
Lowering allocates slot indices; the site adapter takes `FeedbackSlot*`, probes
it, and calls the JR6 semantic operation without a slot on miss. State survives
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
| Property get/set/access entry-point definitions | 54 | 8 |
| Named-property identity representations in property APIs | 4 | 1 |
| Object discriminators consulted in property code | 5 | 2 |
| Builtin dispatch mechanisms | 3 | 1 |
| Error channels | 2 | 1 |
| Promise representations | 2 | 1 |
| IC site-state struct kinds | 4 | 1 |
| `js_call_*` entry variants | 12 | 2 |
| Module loader/caches for JS | 2 | 1 |
| Emitted exception-poll sites (batch) | 153,725 | 0 |
| Emitted ERROR-tag tests (fixed MIR probe) | Tune2 baseline | 1 |
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
- R4 adds a differential fixture for every JR6.4 transition row, hole versus
  `undefined`, descriptor/length failures, Ordinary Array versus Arguments and
  TypedArray classification, receiver-preserving accessors/Proxy traps, and
  callback/cross-realm prototype invalidation.
- R4's symbol census must show exactly eight public semantic property
  operations, one internal transitional exotic adapter, no `FeedbackSlot*` in
  the JR6 core, and no release reference to the TypeMap plausibility predicate.
- Bench suite (Tune line) non-regressing; the **profiling harness**
  (`JS_Profiling_Helpers.md` §6 protocol) re-run per phase — expected
  end-state on the batch workload: helper share 43.5% → **≤30%**, lookup
  bucket 8.3% → ≤3%, root-registration 7.1% → ~0, wall −10–20%.

## 6. Phasing

Ordered so the tree stays green and each phase's deletions are immediate:

| Phase | Content | Depends on | Retires |
|---|---|---|---|
| R1 | JR2 NameId-first property identity — **landed** | — | pointer/raw-key identity paths, per-lookup interning |
| R2a | JR3 in-band signaling — **landed** | R1 (mechanical churn shared) | polls, tracker half, DO15 |
| R2b | JR3.2 payload unification — **landed** | R2a + shared class-Error shape (struct-backed slots, `.stack` accessor) | today's JS Error map construction |
| R3 | JR5 builtins as values + one callable kernel with distinct Call/Construct entries — **landed** | R1 | dispatch switches, 10 call variants |
| R4 | JR6 semantic property kernel + per-array elements states + realm prototype-index epoch + one transitional exotic adapter | R1, R3 | 46 entry points, per-operation prototype scans, release P0 check/recovery |
| R5 | JR4 class/prototype metadata + `JsPropertyOps` behind the JR6 adapter seam | R4 | JsClass side-stamp, sentinels, transitional exotic adapter implementation |
| R6 | JR7 promises/jobs | R5 | static tables, wrapper, root storm |
| R7 | JR8 feedback vectors | R4 | four IC struct kinds |
| R8 | JR9 modules; JR10 file split | R2–R7 | private loader layers; the 40k TU |

R2 was split so the wide mechanical change and representation change could be
verified independently. Both R2a and R2b are now landed in the Tune1
implementation; E8 validation is complete and the remaining broad Node
compatibility failures are outside the JR3 error-lane scope.
**Implementation record:** `vibe/jube/JS_Tune1_Runtime.md` (E0–E8).

Tune1 mapping: P1 ⊂ R6 (superseded by construction), P2 ⊂ R1+R4, P3 ⊂ R5,
P4's constructor-shape work lands naturally in R3/R5. P5 was not completed
by Tune1; `JS_Tune2_Exception.md` owns the JR3 catalog lint and tag-test
ratchet. If a quick win is wanted before R6 lands, Tune1-P1a remains a valid
stopgap.

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
- **JR6 representation drift** (hole versus `undefined`, far indices,
  descriptors/accessors, or non-configurable length shrink). Mitigation:
  differential fixtures cover every transition-matrix row, Arguments and
  TypedArray exclusions, and sparse↔dense threshold changes.
- **JR6 receiver/prototype drift** after getters, Proxy traps, callbacks, or
  cross-realm prototype mutation. Mitigation: receiver/source-order fixtures,
  epoch-invalidating callback tests, custom-prototype/Proxy fallbacks, and a
  debug assertion that a skipped-`HasProperty` loop holds a matching clean
  realm epoch.

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
- **D3.4 extension (JR4 formal work)**: shape-carried `JsClassMeta` contains
  class/prototype/exotic-property policy and is the sole object discriminator
  beside TypeId. Per-value Callable capability and per-array elements state
  are deliberately outside the shared class meta.
- **D2.6.1/D2.6.2 application**: ordinary arrays use the JR6 per-container
  state machine over `ARRAY_NUM`, tagged `ARRAY`, and sparse storage.
  `ARRAY_NUM` promotes atomically to tagged holey storage when an operation
  first creates absence; no side hole bitmap and no automatic tagged→numeric
  respecialization are part of JR6. D5.3 governs rooting during transitions.
- **D5.4.1-D5.4.4 application**: the indexed-prototype clean bit and mutation
  epoch live in context-owned realm state, never at a code-baked address. The
  receiver's intrinsic identity selects the state; callback/user-code re-entry
  invalidates a captured epoch, while foreign/custom/Proxy chains take the
  semantic fallback.
- **D3.4.1 / D3.4.5 application**: an internal ordinary object's non-null
  `Map.type` is a hard TypeMap/layout invariant. JR6 retains only a debug-build
  C `assert`; release builds emit no plausibility check and never recover by
  reporting a property miss. This applies the existing rulings and requires no
  formal-spec revision.
- **D6.2.2v2/D6.4 application (landed)**: builtin callables are ordinary
  function values with distinct per-callee Call/Construct capabilities and
  explicit `newTarget`. The catalog remains creation-time ABI/metadata, never
  a repeated-call semantic selector.
- **D7.3 note**: JS module instantiation flows through the Jube/module
  registry; Node resolution is a resolver plug-in.
- **D3.4.4v2 / D4.6.1v2–D4.6.2v2 (adopted)**: JS property identity is
  NamePool-owned `NameId`; `String*` is materialization only. Static names
  allocate/link before execution, while previously unseen dynamic names use
  the owner-thread dynamic child under RN12/**DO16**. Computed wrappers retain
  the original key Item only for observable Proxy/reflection boundaries.

The JR3 contract landed as D8.4.3 in `doc/Lambda_Formal_Design.md` version
1.5.0. JR5 landed under D6.2.2v2 in version 1.11.0; this doc records both
implementation states and evidence.

## 9. Open questions

1. **JR3** — *fully resolved 2026-08-07*: the sentinel is `ItemError` itself;
   the exception object shares the `LambdaError` struct (JsError ≡
   LambdaError, extras via `details`); commons are pre-created; `.stack`
   adopts and adapts Lambda's `err_capture_stack_trace` pipeline, re-phased
   lazily; resting state rides `LMD_TYPE_MAP` with the shared class-Error
   shape over struct-backed slots (no new TypeId — the D2 ripple was judged
   too costly for a cold path). Implemented; E8 validation is complete.
   The sub-ruling series continues as JR3.3–JR3.9 (catalog conformance and
   lane-check elision) in `vibe/jube/JS_Tune2_Exception.md` §1.
2. **JR5** — *fully resolved 2026-08-12*: D6.2.2v2 fixes the semantic shape
   and Tune4 implements it. `JsFunction` is 256 bytes in GC class 6 (272-byte
   slot including the GC header), versus C0's 224-byte payload in explicit
   class 7 (400-byte slot), so typed capabilities reduce actual slot cost by
   128 bytes. `Function.prototype` is an ordinary call-only function and its
   methods observe builtin functions through the same callable protocol.
3. **JR6** — *design resolved 2026-08-11*: eight receiver-aware semantic
   operations; one transitional exotic adapter owned by JR6 and replaced by
   JR4; IC policy outside the core and replaced by JR8; per-array elements
   state; promote-on-hole with no bitmap; descriptor overlay; realm-local
   prototype-index epoch; and the hard **D3.4.1/D3.4.5** TypeMap invariant
   with debug-only C `assert` and no release recovery-as-miss. Implementation
   census, fixtures, and profiling remain R4 work rather than design choices.
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
