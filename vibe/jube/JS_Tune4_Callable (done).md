# JS Tune4 — Direct Callable Runtime Implementation Plan

**Date**: 2026-08-12

**Status**: IMPLEMENTED — C0–C8 complete under D6.2.2v2

**Implementation tree anchor**: master `88aa5556c8` plus this Tune4 worktree;
the source-faithful clean post-Tune3 C0 snapshot is recorded in §14

**Design authority**: [JS_Runtime_Callable.md](JS_Runtime_Callable.md),
especially **JC1–JC12**, design phases C0–C6, and adopted
**D6.2.2v2**. Governing formal rulings are **D4.6.1–D4.6.2**,
**D5.2.1–D5.2.2**, **D5.3.1–D5.3.5**, **D5.4.1–D5.4.4**,
**D6.2.1–D6.2.2**, **D8.4.2**, and **D8.4.3**.

This document is the execution plan and completion record for Tune4. It does
not reopen the callable design. The implemented result is one JavaScript call
kernel, a distinct construct kernel, and per-callee executable capabilities
selected when a function is created.
Names, catalog IDs, formal lengths, receiver categories, and constructor class
labels become metadata; none may select runtime behavior.

Per **D1.6**, all compiler and ABI work is MIR Direct only. The frozen C2MIR
path is not modified, wrapped, or used as an acceptance gate.

## 1. Outcome and non-negotiable exits

Tune4 is complete only when every `LMD_TYPE_FUNC` value is published with its
final `[[Call]]` and optional `[[Construct]]` capabilities, every ordinary
method call observes NameId property `Get` followed by call, and every dynamic
construction carries `newTarget` explicitly.

| Gate | Required final result | Evidence |
|---|---|---|
| Call ownership | Dynamic calls reach one internal call kernel and then the callee's stored call entry. | Call-entry assertions, focused tests, MIR fixtures, and structural scan. |
| Construct ownership | Dynamic construction reaches one construct kernel with an explicit `newTarget`; capability is the presence of a construct entry. | Construct fixtures and zero pending-state census. |
| Typed targets | MIR functions, native callbacks, and intrinsic semantic bodies use declared target kinds; no native target is invoked by casting a `void*` according to runtime arity. | Factory/field scan and adapter tests. |
| Intrinsics | Each intrinsic function object owns direct call/construct target references. No runtime builtin-ID switch chooses its algorithm. | Zero `js_dispatch_builtin` and `case JS_BUILTIN_*` semantic cases. |
| Identity | Intrinsic function objects are realm-local and binding-owned; only catalog-declared aliases share identity. | Same-realm alias and cross-realm identity fixtures. |
| Property semantics | Catalog methods/accessors are real properties. Static `receiver.name()` lowers as observable `Get -> Call`, not receiver/name helper dispatch. | Descriptor, monkey-patch, Proxy/order, and MIR fixtures. |
| Function semantics | `.name`, `.length`, catalog ID, and source spelling can change without changing callability, constructability, target selection, or allocation. | Mutation tests and zero semantic metadata reads. |
| Ownership | Argument spans, result homes, active realm/module state, and GC roots continue to obey **D5.2**, **D5.3**, and **D5.4**. | Forced-GC, scalar-home, teardown, and two-context tests. |
| Mechanism deletion | Pending `newTarget`, special-constructor classification, ambiguous native factory, receiver/name method APIs, and builtin semantic dispatcher are absent. | C8 deletion ledger and standing ratchets. |
| Source size | Net `lambda/js` production-source delta from the clean post-Tune3 C0 baseline is negative, targeting at least 1,500 lines removed. | `./utils/count_loc.sh`, per-phase deletion ledger, and diff review. |
| Hard JS runtime C/C++ LOC reduction | Final `lambda/js` C/C++ LOC must be at least **1,000 lines lower** than the clean post-Tune3 C0 baseline: `final_lambda_js_loc <= C0_lambda_js_loc - 1000`. This is mandatory even if the 1,500-line target is missed. | The `./lambda/js` line from the same unmodified `./utils/count_loc.sh` invocation at C0 and C8. |

The source-size gates are design constraints under **JC12**, not permission to
delete shared helpers blindly. Moving the dispatcher or constructor chain to a
new translation unit fails the gate. Tests, documentation, and generated
validation output do not offset production source growth.

## 2. Preconditions, scope, and fixed invariants

### 2.1 Tune3 handoff prerequisite

No Tune4 production ABI change begins until Tune3 is complete and the handoff
records all of the following:

1. **D3.4.4v2**, **D4.6.1v2**, and **D4.6.2v2** are adopted in
   `doc/Lambda_Formal_Design.md` with its semver bumped.
2. `NameId` is the sole persistent JavaScript property identity; the
   `String*` pointer-key route is gone.
3. The generated/static module NameId table and per-context dynamic child are
   stable, including the MIR module-state ABI and cache fingerprint.
4. Static named property `Get` has a stable C/MIR helper contract that Tune4
   can use without reopening the NameId representation.
5. Tune3's behavior, cache, GC, worker, release, and LOC gates pass on a clean
   integration commit.
6. No phase-local NameId compatibility adapter remains in callable/catalog or
   method-lowering code.

If any item is missing, C0 may collect data and add isolated semantic fixtures,
but C1 must stop. Tune4 must not freeze an intermediate name ABI into callable
entries or catalog tables.

Tune2 exception work is also a fixed prerequisite: every fallible call or
construct target returns success or an ERROR-tagged `Item` under **D8.4.3**.
Tune4 must not reintroduce a pending exception flag or second failure channel.

### 2.2 In scope

- Split executable capability from diagnostic/catalog metadata in
  `JsFunction`.
- Add a distinct construct-entry protocol and explicit `newTarget` operand.
- Consolidate current call wrappers into one ownership-aware call kernel while
  preserving scalar-home and pre-rooted argument variants.
- Split MIR factories, fixed-arity native factories, span-native factories,
  intrinsic factories, and bound/exotic factories.
- Redesign the existing X-macro catalog into target and binding concepts
  without creating a second registry.
- Extract all intrinsic call/construct algorithms from central semantic
  switches into typed target bodies and delete each old case in its batch.
- Replace runtime builtin cache keys with explicit realm identity keys.
- Install catalog bindings as real NameId properties with exact descriptors.
- Canonicalize static and computed method calls to property `Get -> Call`.
- Converge ordinary, arrow, method, class, bound, Proxy, intrinsic, host, and
  Function.prototype behavior on stored capabilities.
- Delete the obsolete dispatcher, name-selected constructor, pending-state,
  special-classification, and ambiguous native-factory mechanisms.

### 2.3 Out of scope

- Property feedback vectors, polymorphic call ICs, or speculative intrinsic
  devirtualization. These belong to JR8 after the semantic route is canonical.
- A full shape-ops/object-metadata rewrite. Tune4 leaves one explicitly named
  legacy class-map construct adapter for JR4 if removal would broaden scope.
- Promise state-machine redesign beyond converting its callable targets.
- Changing the Lambda core direct-call ABI, the JavaScript `Item* + argc`
  boundary, the source argument limit, or the scalar-home protocol.
- Replacing NameId, changing the Tune3 module-state ABI, or storing
  context-owned function objects in shared MIR/cache images.
- Parser, grammar, vendor, Radiant layout, or C2MIR work.
- A new lazy property-fabrication scheme introduced to hide realm-startup
  cost. Intrinsic objects may be lazily created as a unit, but publication is
  transactional and their bindings are real properties.

### 2.4 Invariants carried through every phase

1. `invoke != NULL` is the authoritative `IsCallable` fact for a published
   function; `construct != NULL` is the authoritative `IsConstructor`
   fact. Broad flags may describe syntax/reflection but never override entries.
2. A class is callable only through its deliberate call-rejection entry. Its
   construct capability is independent.
3. `JsCallEntry` and `JsConstructEntry` are protocol entries. Native semantic
   bodies do not duplicate rooting, stack-depth, realm/module, home-class, or
   result-home mechanics.
4. Arguments remain caller-rooted borrowed spans under **D5.3**. An adapter
   roots exactly the span it creates and destroys that root in LIFO order.
5. The caller-donated result home is forwarded through every entry and helper
   under **D5.2**. No wrapper returns a scalar pointer into its own restored
   number extent.
6. Active `this`, active `new.target`, module, realm, and home class describe
   the current dynamic extent. There is no pending one-shot handoff.
7. A catalog/identity ID may label, link, profile, or fingerprint a target. A
   branch on that ID to select executable semantics is forbidden.
8. A binding determines realm-local function identity. Sharing one C body is
   not proof of JavaScript `===`; sharing requires an explicit alias key.
9. An ordinary method call performs property lookup before argument
   evaluation in ECMAScript order. Exact target direct calls are optimizations
   with guards/fallbacks, never alternate semantics.
10. A compatibility adapter has one owner phase, delegates to the new kernel,
    and is deleted with its last caller in that phase. It cannot become a
    second authoritative route.
11. Function pointers and NameIds are not GC roots. Function-object Item
    edges, bound arguments, environments, properties, globals, home classes,
    and realm caches remain precisely traced.
12. At the third near-identical target/adapter variant, extract the shared
    algorithm or table before continuing, per repository rule 13.

## 3. Baseline, census, and branch discipline

### C0.1 Planning snapshot

The following values describe the planning tree only. They are useful for
sizing the migration but are not the acceptance baseline because Tune3 is
still changing the worktree.

| Surface | Planning observation |
|---|---:|
| `JS_BUILTIN_ID(...)` rows | 374 |
| `JS_BUILTIN_METHOD(...)` rows | 395 |
| `JS_BUILTIN_GLOBAL(...)` rows | 92 |
| `case JS_BUILTIN_*` in `js_runtime.cpp` | 337 |
| `js_new_function(...)` occurrences | 929 across 30 JS source/header files |
| `js_pending_new_target` / `js_has_pending_new_target` references | 106 |
| `special_ctor` references | 18 |
| `builtin_id` references in JS source/catalog | 369 |
| Main builtin semantic dispatcher | about 2,300 lines |
| Name-selected dynamic constructor | about 600 lines |

These are deliberately larger than a simple “374 builtins” estimate. The 929
factory sites include compiled functions, host/Node/DOM callbacks, internal
continuations, and constructors with different ABI needs. C2 therefore owns a
factory-site manifest; a global search-and-replace is not acceptable.

### C0.2 Authoritative clean baseline

Immediately after the Tune3 integration commit, record:

```bash
git status --short
git rev-parse --short HEAD
./utils/count_loc.sh
make build-test
make test262-baseline
make test-mir-gc-stress
make release
```

Capture callable counts with a checked-in census script
`utils/js_callable_census.py`. The script reads source only, emits deterministic
text/JSON, and exits non-zero when a ratcheted count grows. Store transient
captures under `temp/js_callable_stats/`; never write them to `/tmp`.

The initial manifest must classify each hit by owning file and semantic role:

- builtin dispatch definition, declaration, case, fallthrough, and helper;
- runtime semantic versus diagnostic `builtin_id` read;
- catalog target, binding, owner, compiler-lowering, and global row;
- ambiguous factory site: MIR, fixed-native, span-native, intrinsic, bound,
  dynamic Function constructor, or unresolved;
- pending/active `newTarget` read/write/reset/root registration;
- special constructor kind/name read/write;
- name-byte constructor branch;
- public receiver/name method helper declaration, import, call, and definition;
- property-miss catalog synthesis route;
- realm cache keyed by builtin/target/body rather than identity key; and
- direct MIR helper import justified by lexical identity, exact callee guard,
  or receiver/name inference only.

Every unresolved row blocks its owning conversion batch.

### C0.3 Deletion ledger and accounting

The implementation series maintains a deletion ledger with these columns:

```text
legacy symbol/mechanism | owning file | C0 count | replacement owner
phase/batch | callers converted | old LOC deleted | net lambda/js LOC
ratchet added | verification evidence
```

`./utils/count_loc.sh` is the only final LOC authority. `rg`, `wc`, and diff
statistics may diagnose a batch but do not define the gate. The final total
must satisfy both thresholds after all new callable production code is
included:

```text
hard exit:      final_lambda_js_loc <= C0_lambda_js_loc - 1000
stretch target: final_lambda_js_loc <= C0_lambda_js_loc - 1500
```

Here `lambda_js_loc` is the `./lambda/js` C/C++ count reported by the
unmodified script: `.c`, `.h`, `.cpp`, and `.hpp` files. Missing the 1,500-line
stretch target requires an explanation; missing the 1,000-line hard exit
blocks Tune4 completion.

Do not manufacture the reduction by moving code outside `lambda/js`, putting
runtime semantics in generated output, compressing readable algorithms, or
combining unrelated statements. The expected deletion comes from the central
dispatcher, constructor chain, duplicate method routes, state guards, and
ambiguous adapters.

### C0.4 Sequence rule

One conversion batch owns one semantic family end-to-end:

```text
fixture -> typed target -> catalog row/factory -> call or construct entry
        -> all callers -> delete old case/branch -> ratchet -> focused suite
```

A batch is not complete while the old case remains reachable. Do not first
copy all 337 cases into free functions and delete the switch later; that would
temporarily double the mechanism and obscure regressions. Commits remain
bisectable and green after each batch.

## 4. Work breakdown and dependency order

```text
C0 formal adoption + clean census + behavior-locking fixtures
 │
 └── C1 callable ABI, capability finalization, call/construct kernels
      │
      ├── C2 typed MIR/native/intrinsic factories and host-site migration
      │
      └── C3 catalog target/binding schema, identity cache, validation
            │
            └── C4 direct intrinsic call targets; delete dispatcher by batches
                  │
                  └── C5 explicit construct targets and newTarget; delete name chain
                        │
                        └── C6 real intrinsic properties and Get -> Call lowering
                              │
                              └── C7 bound/Proxy/class-map/host convergence
                                    │
                                    └── C8 final deletion, docs, release evidence
```

C2 and C3 may overlap after C1 freezes the typed entry ABI, but neither may
publish a function that the other cannot describe. C4 begins with
Function/Object only after both are green. C6 waits for direct builtin calls
and construction to be semantic owners; otherwise removing miss-time
fabrication would expose half-initialized functions.

## 5. Detailed implementation phases

### C0 — Formal adoption, clean census, and behavior-locking fixtures

**Purpose.** Make the design normative, freeze a post-NameId baseline, and
lock observable behavior before representation changes.

#### C0.1 Formal/document adoption

1. Replace **D6.2.2** with adopted **D6.2.2v2** from
   `JS_Runtime_Callable.md` and bump `doc/Lambda_Formal_Design.md` semver.
2. Revise JR5 in `vibe/jube/JS_Runtime_Redesign.md` to say one callable kernel
   with distinct Call/Construct capabilities and an explicit `newTarget`.
3. Mark `JS_Runtime_Callable.md` adopted and point its decision-record index
   to the formal ruling.
4. Reconcile old JS callable docs that say builtin ID/name/receiver type is an
   executable selector. Do not update implementation diagrams to the target
   state until C8; C0 only removes normative contradictions.

No C1 code lands before these four changes are in the same reviewed series.

#### C0.2 Baseline evidence

- Capture the commands and manifest in §3.
- Measure `sizeof(JsFunction)`, its selected GC size class, allocated bytes,
  and realm function-object count.
- Capture `JS_CALL_STATS=1` call-lane distribution and argc buckets on the
  standard benchmark corpus.
- Record call, method, construct, realm-startup, Test262 batch, and binary-size
  release measurements.
- Sample profiles and retain symbols showing time in `js_dispatch_builtin`,
  `js_new_from_class_object`, catalog lookup, receiver/name method helpers, and
  ordinary call lanes.

#### C0.3 Behavior fixtures landed before migration

Add focused tests for:

- extracted `Function.prototype.call/apply/bind/toString`;
- replacing and deleting Array, String, Number, Date, RegExp, Map, TypedArray,
  Promise, and a DOM method;
- getter and Proxy effects during method lookup, including lookup-before-
  arguments ordering;
- same-body distinct bindings, explicit iterator aliases, and two realms;
- `.name` and `.length` redefinition with unchanged behavior;
- arrows/methods/generators not constructable and classes rejecting call;
- `Date()` versus `new Date()`, primitive wrapper dual behavior,
  Symbol/BigInt rejecting `[[Construct]]` behavior, and collection
  construct-only behavior;
- bound constructors, target equality/substitution rules, and bound argument
  order;
- `Reflect.construct(A, args, B)`, builtin subclass prototypes, recursive
  construction, `super()`, and ERROR cleanup;
- callable/constructable/revoked Proxy behavior; and
- Function.prototype callable while ordinary Map/object values remain
  non-callable even if an own `call` property exists.

Fixtures that the planning runtime already satisfies land in C0. If a fixture
exposes an existing conformance defect, retain the standalone reproducer and
land the enabled regression test with the owning C4–C7 fix; do not make the
standing baseline red or weaken the expected result to match the defect.

**C0 exit.** Formal adoption is complete; the Tune3 handoff is clean; the
census has no unresolved categories; high-risk behavior is protected; release
baselines and layout numbers are archived.

### C1 — Freeze the callable ABI and the two kernels

**Purpose.** Express the final capabilities without yet changing intrinsic
semantics.

#### C1.1 Directional entry types

Add typed protocol contracts equivalent to:

```cpp
typedef Item (*JsCallEntry)(Item callee, Item this_value,
                            Item* args, int argc,
                            uint64_t* result_home,
                            bool args_prerooted);

typedef Item (*JsConstructEntry)(Item callee,
                                 Item* args, int argc,
                                 Item new_target,
                                 uint64_t* result_home,
                                 bool args_prerooted);

typedef Item (*JsNativeCallBody)(Item callee, Item this_value,
                                 Item* args, int argc,
                                 uint64_t* result_home);

typedef Item (*JsNativeConstructBody)(Item callee,
                                      Item* args, int argc,
                                      Item new_target,
                                      uint64_t* result_home);
```

Exact field order is decided only after the C0 layout measurement. Prefer
deleting `special_ctor_kind`, `special_ctor_name`, and eventually
`builtin_id` semantic storage before increasing the GC size class. If a compact
descriptor pointer is used, it must be immutable/context-safe and cannot
reintroduce target-kind switching on every invocation.

#### C1.2 Sole capability finalizer

Create one initialization/finalization routine that is the only writer of
published executable entries. It receives the declared function kind and
target descriptor, selects protocol entries once, validates impossible
combinations, and publishes only after all Item edges are rooted.

Required assertions:

- a published function has a non-null call entry, including a class's
  deliberate throwing entry;
- a null construct entry exactly means `IsConstructor == false`;
- a native call protocol has a matching typed native target;
- a native construct protocol has a matching typed construct target;
- a MIR protocol does not carry a native target of the wrong union kind;
- a bound construct entry exists iff the target is constructable;
- alias/cache publication occurs only after finalization; and
- no finalizer reads function `.name`, formal length, `builtin_id`, or property
  owner to choose a semantic body.

Replace scattered `js_function_call_lane_recompute` writes with the finalizer
or a narrowly named re-finalization operation for the few functions whose
compiled lane is deliberately completed later. A published function must not
transit through a callable state with null/wrong entries.

#### C1.3 One internal call kernel

Introduce one internal `js_call_value(...)` owner. Preserve the public
ownership-qualified wrappers:

- `js_call_function` allocates/finishes a safe temporary result home and roots
  an unrooted native argument span;
- `js_call_function_into` forwards the caller's home;
- `js_call_function_prerooted_args_into` forwards a caller-proven rooted span;
- MIR emission selects the correct wrapper from existing ownership facts.

All wrappers perform shared callable/type/error checks once and delegate to
`fn->invoke`. They do not branch on builtin ID, special constructor,
receiver type, or name. Preserve `JS_CALL_FORCE_GENERIC` and
`JS_CALL_LANE_CHECK` until C8 as differential diagnostics; update them to
compare protocol lanes, not restore the old builtin interpreter.

Until C4 converts an intrinsic, its finalizer selects one explicitly named
`js_call_entry_legacy_builtin`. Only that entry may reach the old dispatcher;
the generic user/MIR entries never inspect a builtin ID. Its catalog-row count
is a monotonic ratchet and the entry is deleted with the last C4 case. This is
the sole call-side compatibility adapter, not a second kernel.

#### C1.4 One internal construct kernel

Add `js_construct_value(...)` with the explicit operand order fixed for both C
and MIR. Initially it may delegate unconverted constructors to one named
legacy construct adapter, but all new construction call sites use the kernel.

The kernel:

1. roots `callee` and `new_target` across prototype lookup/allocation;
2. rejects a missing construct capability with the existing TypeError lane;
3. forwards `args`, `argc`, result home, and pre-rooted fact;
4. invokes `fn->construct` without examining name or catalog ID; and
5. preserves ERROR and scalar-home ownership unchanged.

#### C1.5 GC/layout work

- Update tracing only for new Item/descriptor Item edges; C function pointers
  and NameIds are not roots.
- Add static layout assertions and a focused GC allocation/trace test.
- Exercise GC at function finalization, cache publication, entry prologue,
  native body, return re-homing, and ERROR unwind.
- Record layout and realm retained-byte deltas against C0.

**C1 exit.** All currently published functions have explicit call
capabilities; dynamic call wrappers share one kernel; construction has an
explicit kernel and a single named compatibility adapter; existing suites
remain green; `JsFunction` stays in its verified size class or an approved
measured exception is recorded.

### C2 — Split factories and migrate ambiguous native sites

**Purpose.** Eliminate raw `void* + param_count` as a native executable type
without forcing every host callback into one span signature.

#### C2.1 Final factory families

Provide explicit constructors for these roles:

| Family | Stored target | Selected protocol |
|---|---|---|
| MIR function/method/closure | backend-opaque MIR entry plus compiler ABI metadata | existing generic/specialized MIR call entry |
| Fixed native 0..N | correctly typed union member | shared arity adapter selected once |
| Span native/rest | `JsNativeCallBody` or declared receiver/span form | native span call entry |
| Native constructor | `JsNativeConstructBody`, optionally paired call body | native construct entry |
| Intrinsic | immutable target spec plus realm binding spec | intrinsic call/construct protocol |
| Bound/exotic | target function/exotic payload | bound or Proxy protocol entry |

Directional names are `js_new_mir_function`, `js_new_mir_method`,
`js_new_native_function`, `js_new_native_method`,
`js_new_native_constructor`, and `js_new_intrinsic_function`. The actual API
may consolidate parameters into C+ structs, but it may not restore an
untyped `void*` native boundary.

#### C2.2 Shared fixed-arity adapter family

- Define typed callback typedefs for the arities actually present in the C0
  manifest, including the existing negative/rest conventions after they are
  classified explicitly.
- Use one tagged union or generated X-macro family. Select the adapter at
  creation; do not switch on `param_count` on each call.
- Centralize missing-argument `undefined`, ignored-extra-argument, `this`, and
  rest/span policy. The declaration must state which policy applies.
- Root any constructed adapter span exactly once and forward the caller's
  result home.
- Do not handwrite a wrapper per host function. A third structurally similar
  adapter is a mandatory extraction point.

#### C2.3 Site migration batches

Classify and convert all 929 planning occurrences by factory intent, not file
spelling. Recommended batches are:

1. core runtime continuations and promise/timer callbacks;
2. compiler-created ordinary/method/closure functions;
3. small Node namespaces and synchronous host modules;
4. filesystem/network/process modules with async lifetime fixtures;
5. DOM/events/fetch/XHR/clipboard callbacks;
6. dynamic Function/async/generator constructors; and
7. remaining intrinsic/global constructors, which then move to C3 target rows.

For each batch, the manifest records old signature, selected typedef, arity
policy, receiver use, rooting ownership, constructability, and test owner.
Use compiler type errors to expose mismatches; do not cast through `void*` to
silence them.

The legacy `js_new_function(void*, int)` may remain only as a phase-local
compile blocker with a deprecation scan. New call sites are forbidden after
the first batch. It is deleted when its occurrence count reaches zero, not in
C8.

#### C2.4 MIR ABI isolation

MIR function pointers remain backend-opaque only inside the MIR factory and
protocol entries. A native host callback cannot enter that storage family.
Update direct field-offset emitters and C readers in one commit. Bump any
internal callable-layout/cache fingerprint that persists offsets; shared MIR
must never embed a context-owned function object or native descriptor address.

**C2 exit.** Ambiguous native factory sites are zero; every native callback
uses a declared ABI and shared adapter; MIR functions use a separate factory;
all async/GC and scalar-home tests pass; no third copied adapter exists.

### C3 — Normalize the intrinsic catalog and realm identity cache

**Purpose.** Make the existing catalog describe executable targets and
observable bindings separately before extracting the dispatcher.

#### C3.1 One catalog, two concepts

Evolve `js_builtin_catalog.def`; do not add a parallel handwritten registry.
The final generated concepts are:

```text
INTRINSIC_TARGET
    stable catalog/link id
    typed call body or null
    typed construct body or null
    exception/GC effect reference
    MIR lowering kind
    default intrinsic prototype policy

INTRINSIC_BINDING
    owner object/prototype
    property NameId specification
    target id
    observable function name and length
    data/accessor descriptor attributes
    receiver/brand policy
    realm identity key
```

Global functions and constructors become bindings to the same target table.
Existing constructor IDs and MIR kinds may remain stable link/fingerprint
metadata. `JsBuiltinDispatchGroup` disappears after the last C4 case because
it has no final semantic role.

The C3 transition may encode an unconverted row as
`INTRINSIC_TARGET_LEGACY(id, group, mir_kind)`. Such a row selects only the
single `js_call_entry_legacy_builtin`, retains the exact pre-C4 dispatcher ID,
and is listed in the deletion manifest. C4 replaces the whole row with its
typed target and deletes the matching old case in one batch. A row may not
carry both a direct body and a legacy selector, and the legacy-row count may
only decrease.

#### C3.2 NameId handoff

- Generated property names use their generated NameIds.
- Arbitrary catalog spellings link once through Tune3's sealed module/realm
  name table; immutable catalog data stores no context-dependent NameId.
- Installation and duplicate-owner validation compare NameId, not bytes or
  `String*` identity.
- Observable `.name` materializes a JavaScript string property and never feeds
  target selection.
- Symbol-named bindings carry their generated Symbol/NameId specification
  without pretending the display spelling is the property identity.

#### C3.3 Explicit identity and aliases

Replace `use_cache` and builtin-ID cache keys with a realm identity key. C0
must enumerate every current accidental share and decide whether it is:

- a spec-required same-object alias;
- two distinct function objects sharing one semantic target;
- one accessor target used by distinct descriptors; or
- an implementation bug hidden by the old cache.

At minimum audit Array/TypedArray iterator aliases, Map/Set iterator aliases,
Symbol protocol methods, species accessors, `%ThrowTypeError%`, and constructor
aliases. The default is distinct identity. An alias key is legal only with a
fixture asserting required equality.

The realm cache stores rooted Items and is indexed by the explicit identity
key. Immutable target descriptors may be process-global; cached function
objects, property maps, prototypes, globals, and linked arbitrary NameIds are
context-owned under **D5.4**.

#### C3.4 Catalog validation

Add a generated/static validator that rejects:

- target with neither call nor construct body;
- binding to a missing target or owner;
- duplicate owner + property NameId;
- accessor binding without the correct getter/setter capability;
- alias key whose target, descriptor role, observable metadata, or realm
  policy conflicts;
- call-only/construct-only capability inconsistent with declared global kind;
- unknown effect metadata or target/result-home mismatch;
- incompatible native pointer type;
- duplicate stable ID or unstable required ordering; and
- context-owned NameId/function pointer in immutable catalog data.

During C3/C4 only, the first rule accepts a manifest-listed legacy row instead
of a direct body. The validator rejects a legacy row with no matching old case,
a converted row with a remaining old case, a direct/legacy dual row, or any
increase relative to the ratchet. The exception disappears with the legacy
row macro at C4 exit.

Expose the validator through a standing `make test-js-callable-catalog`
target. Keep the existing exception-catalog test separate; both are required
because target effect references bridge the two catalogs. Edit
`build_lambda_config.json` and run `make` for generated build-list changes;
never edit generated `.lua` build files.

**C3 exit.** One catalog generates typed target and binding tables; realm
identity is explicit; NameId integration is stable; catalog validation is a
standing test; no second registry exists.

### C4 — Extract direct intrinsic call targets and delete the dispatcher

**Purpose.** Replace interpreted builtin tokens with direct per-callee target
bodies in bounded, behavior-tested batches.

#### C4.1 Batch conversion rule

For every target family:

1. isolate the existing case's semantic algorithm and all implicit inputs;
2. turn hidden mode/global inputs into typed target policy or explicit
   validated helper parameters;
3. reuse/promote existing shared helpers before adding a near-duplicate;
4. add the typed target reference to the existing catalog row;
5. create/finalize the realm function with its direct entry;
6. convert accessors and recursive builtin calls to call real function values
   or shared algorithms as appropriate;
7. delete the old switch case and obsolete helper/state in the same batch;
8. lower the census ratchet; and
9. run the focused Test262 directory, JS gtest subset, GC stress fixture, and
   release microbenchmark before starting the next family.

Do not preserve a fallback to `js_dispatch_builtin` for a converted target.
An ERROR is returned through the normal target body, not through a dispatcher-
specific lane.

#### C4.2 Required family order

| Batch | Families | Special proof point |
|---|---|---|
| C4A | Function.prototype, Function.prototype itself, Object prototype/static, Boolean/primitive basics | Recursive call/apply/bind paths and real callable Function.prototype. |
| C4B | Math, Number, BigInt, Symbol, String and string iterator | Scalar homes, boxing, rejecting Symbol/BigInt construct entries, and primitive receiver branding. |
| C4C | Array prototype/static and array iterator | Mutation/species/callback ordering without a global dispatch mode. |
| C4D | TypedArray, ArrayBuffer, SharedArrayBuffer, DataView, Atomics | Distinct typed-array brand/OOB policy feeding shared algorithms. |
| C4E | Map, Set, Weak collections, iterators, WeakRef, FinalizationRegistry | Iterator alias identity, GC reachability, and brand checks. |
| C4F | Date, RegExp, Error classes, JSON, URI/eval/global helpers | Dual Date call behavior, RegExp state, and error effect catalog. |
| C4G | Promise/static/prototype and async helpers | Re-entrant callbacks, realm ownership, and precise async roots. |
| C4H | Reflect, Proxy statics, CSS, DOM/host catalog tail | Proxy observability and host-specific receiver policy. |

If a family contains construct behavior, C4 installs the direct call body and
target record while C5 installs the construct body. It may retain one named
legacy construct adapter during that interval, but call must never fall back
to the old builtin dispatcher.

#### C4.3 Remove hidden Array/TypedArray mode

`js_dispatch_as_array_method` is prohibited final state. Split Array and
TypedArray target entries and pass an explicit compile-time wrapper policy or
validated algorithm-policy enum into shared lower helpers. The policy belongs
to the target chosen at creation; it is not mutable context state and is not
rediscovered from flags at each call.

Fixtures cover borrowed Array methods, borrowed TypedArray methods, detached
buffers, OOB views, species, holes, callback mutation, and iterator methods.
Delete all mode guards and runtime-state initialization with the last batch.

#### C4.4 End-state deletions

After C4H:

- delete `js_dispatch_builtin` definition and forward declaration;
- delete all runtime semantic `case JS_BUILTIN_*` labels;
- delete `JsBuiltinDispatchGroup` and group lookup tables;
- remove `fn->builtin_id` branches from call entries;
- rename any surviving ID field `catalog_id` only if it has a documented
  diagnostic/link user; otherwise delete it;
- add a lint/census that permits catalog IDs in tables, logs, fingerprints,
  and compiler metadata but rejects them in executable selection; and
- recapture profiles proving sampled intrinsic calls enter their target body
  directly.

**C4 exit.** Builtin calls use stored typed targets; central semantic dispatch
and Array/TypedArray global mode are zero; each family suite is green; source
size is already trending negative.

### C5 — Make construction explicit and delete name-selected allocation

**Purpose.** Complete `[[Construct]]`, explicit `newTarget`, and constructor
prototype behavior for ordinary, class, bound, intrinsic, and Proxy targets.

#### C5.1 Route every construct producer

Convert and inventory all producers:

- MIR lowering for `new C(args)` passes evaluated `C` as callee and initial
  `newTarget`;
- `Reflect.construct(target, args, newTarget)` passes its operands directly;
- `super(args)` forwards active derived `new.target`;
- bound construction substitutes the bound target only when
  `newTarget === boundFunction`, then forwards the original otherwise;
- builtin/internal species construction uses the same kernel; and
- DOM/host constructors represented as function values use registered
  construct entries.

`js_apply_constructor`, `js_new_from_class_object`, and defer-own-fields
variants become either thin phase-local adapters to `js_construct_value` or
are deleted when their callers move. There is one final construct kernel.

#### C5.2 Scoped active `new.target`

Implement one C+ scope/guard owner or explicit begin/end helper used only by
construct protocol entries:

```text
save active js_new_target
install explicit operand
invoke body / field initialization / super path
restore on success or ERROR
```

The guard itself is not the semantic handoff; the operand is. Add forced ERROR
and nested-construction fixtures before deleting pending state. The active
slot may remain in the context capsule while compiled-body ABI requires it.

Delete, in the same batch as the last producer/consumer:

- `js_pending_new_target`;
- `js_has_pending_new_target`;
- their GC-root registration and runtime reset checks;
- all caller-side set/clear sequences; and
- entry logic that conditionally consumes pending state.

#### C5.3 Ordinary/class construction

The ordinary/class construct entry owns:

1. `GetPrototypeFromConstructor(newTarget, defaultProto)`;
2. receiver allocation where the constructor kind requires it;
3. active `this`, home class/private brand, realm/module, and `new.target`;
4. base/derived return-value validation;
5. instance field/private initialization in required order; and
6. ERROR cleanup and result-home forwarding.

Extract one shared `GetPrototypeFromConstructor` helper. It performs observable
property access, accepts an explicit intrinsic default prototype policy, and
roots constructor/newTarget/prototype across allocation. Do not duplicate this
sequence in every builtin constructor.

#### C5.4 Intrinsic constructor matrix

Catalog target rows declare the actual matrix:

- call + construct: Object, Array, String, Number, Boolean, Date, RegExp, Error
  families where specified;
- call + rejecting construct: Symbol and BigInt expose `[[Construct]]` for
  `IsConstructor`/`extends`, but reject before argument coercion;
- call only: ordinary global functions;
- construct only with deliberate call rejection: Promise, Map, Set, weak
  collections, TypedArrays, ArrayBuffer/DataView and specified host classes;
- special Proxy construction through its explicit target; and
- Function/AsyncFunction/GeneratorFunction families through typed dynamic-
  compiler targets, never display-name classification.

Each construct body allocates its required internal-slot-bearing object and
then applies the shared prototype policy. Subclass fixtures cover Array,
TypedArray, ArrayBuffer, DataView, Date, RegExp, Error, Promise, Map/Set, and
representative DOM constructors.

#### C5.5 Delete name/special constructor semantics

With the matrix green, delete:

- the large constructor-name byte chain;
- `special_ctor_kind` and `special_ctor_name` fields/accessors;
- `js_is_intrinsic_constructor_named` and equivalent classifiers;
- name-derived `Function`/async/generator constructor selection;
- flags used only to infer constructability; and
- any `.name` read in allocation/prototype selection.

Retain constructor/catalog IDs only for linking/diagnostics if a concrete
non-semantic user remains. Mutation of `.name` must pass before the deletion
ratchet is lowered to zero.

**C5 exit.** Every dynamic construction has an explicit `newTarget`; pending
state and spelling-selected construction are absent; capability queries read
entries; nested/super/bound/Proxy/subclass/ERROR fixtures and GC stress pass.

### C6 — Install real intrinsic properties and canonicalize method calls

**Purpose.** Remove the parallel builtin-property/method-dispatch mechanism so
intrinsics obey the same NameId property semantics as user functions.

#### C6.1 Transactional realm installation

For each intrinsic namespace/constructor/prototype:

1. allocate and root the object and all binding function/accessor values;
2. resolve catalog NameIds through Tune3's context/module table;
3. create function `.name`, `.length`, and `prototype` properties with exact
   attributes;
4. install data/accessor descriptors and explicit alias Items;
5. validate duplicate owner + NameId and capability consistency; and
6. publish the completed object/cache entry atomically to the realm.

Lazy construction of an entire intrinsic object may remain. An ordinary
property miss must not query the catalog or fabricate one property. After a
binding is deleted/redefined, normal property semantics decide the result.

#### C6.2 Replace miss-time synthesis

Inventory all `js_get_or_create_builtin`, `js_lookup_builtin_method_spec`,
prototype class lookup, and special property-get branches. Convert their
construction-time users to binding installation and their runtime users to
ordinary NameId property lookup. Delete each synthesis branch with its owner
object's conversion.

`js_get_or_create_builtin` may become an internal realm-binding constructor
temporarily, but the final API is identity-key/binding-based and is not called
from a property miss. Builtin ID alone is not a valid cache key.

#### C6.3 Canonical MIR method lowering

Both `receiver.name(args)` and `receiver[expr](args)` lower to:

1. evaluate receiver once;
2. perform required nullish/coercibility checks;
3. perform NameId/computed property `Get`;
4. retain receiver as the Reference base;
5. evaluate arguments in source order; and
6. call the loaded value through the ordinary call kernel with receiver as
   `this`.

Primitive boxing/prototype lookup belongs to property access. It must not be
encoded in `js_string_method`, `js_number_method`, `js_map_method`,
`js_array_method`, or `js_array_method_direct` dispatch.

Delete receiver/name helper MIR imports and public declarations after the last
call site. Preserve only lower algorithm helpers directly called by typed
intrinsic target bodies; rename them so they cannot be mistaken for property
dispatch.

#### C6.4 Audit direct lowerings

Classify every direct Math/Date/string/array/intrinsic lowering:

- lexical/local exact function identity: may remain;
- compiler-known class method protected by existing source-identity proof: may
  remain with documented proof;
- immutable internal operation not reachable as a mutable property: may
  remain;
- receiver TypeId + property spelling only: delete and use `Get -> Call`;
- future feedback/guarded target identity: defer to JR8.

A direct target must preserve property mutation, deletion, accessors, Proxy
traps, realm separation, and evaluation order. Do not add a realm mutation
epoch or feedback vector in Tune4 merely to retain an unsafe shortcut.

**C6 exit.** Intrinsic bindings are real properties; runtime misses do not
synthesize them; generic static and computed method calls share one NameId
property/call route; public receiver/name dispatch imports are zero; descriptor
and monkey-patch/Proxy/order fixtures pass.

### C7 — Converge bound, Proxy, class-map, Function.prototype, and hosts

**Purpose.** Remove the final exceptional callability paths and leave one
explicit bridge for object-metadata work only if necessary.

#### C7.1 Bound functions

- Bound call entry merges bound and supplied arguments into one dynamically
  sized precisely rooted span, uses bound `this`, and calls the target kernel.
- Bound construct entry exists iff the target has construct capability,
  ignores bound `this`, applies correct `newTarget` substitution, and calls the
  construct kernel.
- Bound `.length`/`.name` are observable properties only.
- Delete builtin-specific bound branches and flags used solely to choose a
  target body.

Test zero/many bound args, nested bounds, scalar arguments, GC during merge,
constructable/non-constructable targets, Proxy targets, and ERROR cleanup.

#### C7.2 Callable and constructable Proxies

Proxy capability is derived when the Proxy is created from the target's
capabilities. Its call/construct entries:

- validate revocation;
- perform the specified `apply`/`construct` trap lookup and call;
- fall back to the target call/construct kernel when the trap is absent;
- validate construct trap object results; and
- root target, handler, trap, args, and `newTarget` precisely.

There is no broad “Proxy is callable” flag independent of target capability.

#### C7.3 Function.prototype and non-function values

Construct Function.prototype as a real callable function whose target returns
`undefined` and whose construct capability is null. Remove:

- Map/sentinel callability exceptions;
- the compatibility path that treats an own `.call` property as `[[Call]]`;
- broad object flags that bypass `LMD_TYPE_FUNC`/specified exotic entries; and
- helpers that infer callability from class or property spelling.

Ordinary objects remain non-callable regardless of properties. Any host exotic
that genuinely needs callability must have an explicit documented entry and
payload, not a sentinel field.

#### C7.4 Legacy class-map boundary

If class maps still carry construction behavior outside `JsFunction`, keep one
named `js_construct_entry_legacy_class_map` bridge. Its contract is:

- it is reachable only from a finalized function's construct entry;
- it receives explicit `newTarget` and uses the common construct helper;
- it contains no name/catalog dispatch and no pending state; and
- JR4 will replace it with shape/object operations.

Record its census, owner, tests, and JR4 deletion condition. Do not create a
second bridge or expand class-map metadata in Tune4.

#### C7.5 Host/async final sweep

Re-run the factory and callability manifests across Node, DOM, events, timers,
promises, modules, networking, and internal continuations. There must be no
ambiguous factory, raw function-pointer cast, permanent per-call root, or
host-specific call engine. Run async teardown and event-loop GC stress before
lowering the ratchet.

**C7 exit.** Bound and Proxy behavior uses the common kernels; Function.prototype
is correctly callable; ordinary objects have no sentinel callability; all host
sites use typed factories; at most one documented JR4 class-map bridge remains.

### C8 — Delete scaffolding, update docs, and close release evidence

**Purpose.** Remove migration-only code and prove the new runtime is smaller,
correct, owned, and not relying on future optimization work.

#### C8.1 Required deletion sweep

Remove:

- all compatibility adapters except the explicitly accepted JR4 bridge;
- old callable-layout fields and flags with zero readers;
- legacy declarations/imports and stale X-macro expansions;
- semantic builtin dispatch groups and mode guards;
- pending `newTarget` root/reset/debug code;
- special constructor enums/name storage;
- ambiguous factory overloads and pointer casts;
- method-name dispatch APIs and property-miss synthesis;
- phase feature flags and dual-path differential code that can invoke old
  semantics; and
- comments/docs describing catalog IDs or names as executable tokens.

Promote shared `static` helpers to the owning module header instead of copying
them. If stable code is split from `js_runtime.cpp`, edit
`build_lambda_config.json`, run `make`, keep coherent ownership, and create no
new translation unit over 8,000 lines.

#### C8.2 Documentation updates

Update implementation reality in:

- `doc/dev/js/JS_04_MIR_Lowering.md` for call/construct/property lowering;
- `doc/dev/js/JS_05_Functions_Closures.md` for callable entries and factories;
- `doc/dev/js/JS_06_Objects_Properties_Prototypes.md` for real properties and
  `Get -> Call`;
- `doc/dev/js/JS_07_Classes.md` for construction, `super`, and `new.target`;
- `doc/dev/js/JS_10_Builtins.md` and `JS_12_TypedArrays.md` for target/binding
  catalog and intrinsic bodies;
- `doc/dev/js/JS_13_Web_DOM.md` and `JS_14_Node_Compat.md` for typed host
  callbacks;
- `doc/dev/js/JS_15_Performance.md` for the final call/construct profile;
- `doc/dev/js/JS_00_Overview.md` diagrams/index where needed;
- `vibe/jube/JS_Runtime_Callable.md` status and measured outcome; and
- `vibe/jube/JS_Runtime_Redesign.md`, marking JR5 complete and recording JR6
  as the next mechanism.

Every updated design/implementation document cites **D6.2.2v2** and the other
applicable D rulings before JC ledger IDs.

#### C8.3 Final evidence

- Run the full validation sequence in §10.
- Re-run every structural ratchet in §8.
- Capture release A/B performance, startup, allocation, retained bytes, and
  binary size under the same environment and corpus as C0.
- Produce the deletion ledger with C0/final counts, net LOC, proof of the
  mandatory 1,000-line reduction, and the 1,500-line stretch result.
- Sample profiles showing no builtin dispatcher, constructor-name chain,
  miss-time synthesis, or receiver/name method helper.
- Record any retained JR4 bridge and its exact deletion issue; no other known
  compatibility path may be deferred.

**C8 exit.** All acceptance gates pass, `lambda/js` C/C++ LOC is at least
1,000 lines below C0 and targets at least 1,500 lines below C0, documentation
describes the implemented route, and JR6 property representation can proceed
without preserving a Tune4 shadow mechanism.

## 6. File and ownership map

This table is directional; C0 refreshes exact paths after Tune3. New files are
created only when ownership is clearer than extending an existing coherent
module.

| Surface | Primary files | Phase | Final owner |
|---|---|---|---|
| Function layout and entry typedefs | `lambda/js/js_function.hpp` | C1 | Callable value representation. |
| Finalization, factories, cache publication | `lambda/js/js_runtime_function.cpp`, `js_runtime_internal.hpp` | C1–C3 | One callable factory/finalizer module. |
| Call/construct kernels and protocol entries | `lambda/js/js_runtime.cpp`, possibly a bounded extracted callable TU | C1, C5, C8 | One call kernel and one construct kernel. |
| Runtime active state | `lambda/js/js_runtime_state.cpp` and state headers | C5 | Active state only; no pending handoff. |
| Catalog declaration/schema | `lambda/js/js_builtin_catalog.def`, `js_builtin_catalog.hpp` | C3 | Single target/binding source of truth. |
| Realm registry/identity cache | `lambda/js/js_runtime_builtin_registry.cpp` | C3, C6 | Binding/identity-key construction and installation. |
| Intrinsic semantic bodies | existing runtime/algorithm modules, split coherently after extraction | C4 | Typed bodies plus shared algorithms, no mega-switch. |
| MIR call/property lowering | `lambda/js/js_mir_expression_lowering.cpp`, `js_mir_calls_boxing_types.cpp`, related MIR headers | C1, C5, C6 | NameId Get plus common call/construct imports. |
| Class/super lowering | JS MIR class/function lowering modules | C5 | Explicit `newTarget` construct calls. |
| Host/Node/DOM factories | `lambda/js/js_*.cpp` manifest owners | C2, C7 | Typed native factories only. |
| Tests | `test/`, `test/js/`, Test262 harness/catalog | every phase | Focused fixture per batch plus standing suites. |
| Build source list | `build_lambda_config.json` only if files split | C8 | Generated Lua remains untouched. |

Avoid creating `js_callable_compat.cpp`, `js_builtin_dispatch2.cpp`, or a
second catalog. A new module must own stable final behavior and receive a net
deletion from the old owner in the same series.

## 7. API and ABI completion checklist

| Surface | Required final state | Owner phase |
|---|---|---|
| `JsFunction.invoke` | Non-null final call-protocol entry for every published function. | C1 |
| `JsFunction.construct` | Null exactly when not constructable; explicit protocol otherwise. | C1, C5 |
| Native call/construct targets | Typed body/union/descriptor, selected once. | C1–C2 |
| `func_ptr` | MIR-only opaque storage or removed/retyped; never ambiguous native storage. | C2 |
| `builtin_id` | Removed or renamed metadata-only `catalog_id`; zero semantic reads. | C4 |
| `special_ctor_*` | Removed. | C5 |
| Call public wrappers | Ownership-qualified adapters into one `js_call_value`. | C1 |
| Construct public wrappers | Ownership-qualified adapters into one `js_construct_value`. | C5 |
| Pending `newTarget` | Removed, including root/reset/debug state. | C5 |
| Active `new.target` | Scoped dynamic state installed from explicit operand. | C5 |
| Capability queries | Entry-presence based; class call rejection is explicit. | C1, C7 |
| Native factories | Separate MIR/fixed/span/intrinsic/construct families. | C2 |
| Catalog | One generated target table and binding table. | C3 |
| Realm builtin cache | Explicit binding identity key, rooted/context-owned. | C3 |
| Intrinsic property installation | Real NameId data/accessor descriptors. | C6 |
| Property miss | No catalog synthesis. | C6 |
| Static/computed method lowering | NameId/computed Get, receiver preservation, common call kernel. | C6 |
| Receiver/name public helpers | Removed as dispatch APIs. | C6 |
| Bound/Proxy entries | Forward through target capabilities and common kernels. | C7 |
| Function.prototype | Real call-only function value. | C7 |
| Ordinary object callability | No Map/sentinel/own-`.call` compatibility. | C7 |
| Shared MIR/cache | Symbolic imports and current-context linking only. | C1–C8 |

Any direct MIR field-offset emission and every native reader of that field
change in the same commit. There is no supported mixed old/new `JsFunction`
layout or cached module fingerprint.

## 8. Structural ratchets

The C0 census script owns exact parsing and exclusions. These command sketches
remain useful during development but do not replace the script's semantic
classification.

| Ratchet | Planning baseline | Phase target becomes zero |
|---|---:|---:|
| `js_dispatch_builtin` definition + declaration | 2 | C4 |
| Runtime `case JS_BUILTIN_*` semantic labels | 337 | C4 |
| Runtime semantic branch on builtin/catalog ID | non-zero | C4 |
| `js_dispatch_as_array_method` references | non-zero | C4 |
| `js_pending_new_target` / `js_has_pending_new_target` | 106 | C5 |
| `special_ctor` references | 18 | C5 |
| Constructor selection by function-name bytes | one large chain | C5 |
| `js_new_function(void*, arity)` sites | 929 unclassified occurrences | C2/C7 |
| Public receiver/name method dispatch families | at least five | C6 |
| Property-miss builtin synthesis calls | non-zero | C6 |
| Null call entry on published function | builtin/partial paths exist | C1 |
| Non-function own-`.call`/sentinel callability paths | non-zero | C7 |
| Compatibility callable mechanisms | several | C8 (JR4 bridge excepted) |

Required standing scans/assertions:

- every function factory calls the sole finalizer before publication;
- no native call site casts a raw `void*` based on arity;
- no switch/if on catalog ID, display name, formal length, owner, or receiver
  type selects a call/construct body;
- no shared MIR embeds a context-owned `JsFunction*`, identity-cache Item,
  target descriptor pointer, or arbitrary context NameId;
- no call/construct entry allocates a permanent root range;
- every adapter-created span has one precise root and LIFO cleanup;
- every result-home-taking entry forwards it to all fallible/scalar helpers;
- no generic static method MIR imports receiver/name dispatch helpers;
- catalog alias keys have explicit equality fixtures; and
- only the accepted `js_construct_entry_legacy_class_map` bridge may remain,
  with an exact-name census count of one if JR4 still needs it.

The scan must distinguish semantic and diagnostic uses. For example,
`catalog_id` in a log/profile label is permitted; `switch (catalog_id)` to
invoke a body is not.

## 9. Test matrix and batch gates

### 9.1 Focused semantic matrix

| Area | Required cases |
|---|---|
| Ordinary call | fixed/variadic args, recursion, closure env, method `this`, ERROR, scalar return home, pre-rooted MIR args. |
| Function kinds | ordinary, arrow, method, generator, async, class call rejection, dynamic Function family. |
| Native adapters | every supported arity/policy, missing/extra args, rest/span, receiver use, GC in body, ERROR and scalar result. |
| Function.prototype | callable prototype, call/apply/bind/toString, extracted calls, invalid receivers. |
| Intrinsic properties | descriptors, ownKeys, extraction identity, deletion, redefinition, accessor functions, aliases, cross-realm separation. |
| Method order | receiver once, nullish check, getter/Proxy lookup, arguments, call; mutation and accessor replacement. |
| Construct | new, Reflect.construct distinct target, base/derived class, super, field/private init order, object/primitive return. |
| Builtin duality | Date, Object/Array, primitive wrappers, RegExp, Error; Symbol/BigInt rejecting construct entries; collection/Promise/TypedArray construct-only. |
| Prototype choice | primitive/non-object `.prototype`, cross-realm defaults, subclassing every internal-slot family. |
| Bound | bound this/args, nested bind, length/name observability, construct substitution, non-constructable target. |
| Proxy | call/construct trap, absent trap, revoked proxy, invalid trap result, target capability matrix. |
| GC/ownership | allocation in lookup/adaptation/body/return, bound span, nested construct, ERROR unwind, async teardown, two contexts. |
| Host | Node callbacks, DOM events, timer, promise continuation, fetch/XHR, module callback, representative network completion. |

### 9.2 MIR fixtures

Check emitted MIR for:

- `obj.name(args)` performing NameId property load then one call import;
- `obj[expr](args)` using the same call import and preserving the receiver;
- mutation/Proxy fixtures lacking a receiver/name intrinsic shortcut;
- lexical exact-function direct call retaining its valid direct lane;
- `new C(args)` passing both callee and explicit initial `newTarget`;
- `Reflect.construct(A, args, B)` carrying B directly;
- `super(args)` forwarding active newTarget without a setter import;
- call/construct ERROR propagation as an ERROR-tagged Item branch;
- result-home forwarding and pre-rooted span selection; and
- no function-object/descriptor/context pointer immediate in shared MIR.

### 9.3 Per-batch gate

Every C2/C4/C5/C6 family batch runs, at minimum:

1. its focused gtest/JS fixtures;
2. the relevant Test262 built-ins, Function/class/Proxy/Reflect/subclassing
   directory;
3. `make test-mir-gc-stress` when the batch changes rooting or async lifetime;
4. the catalog validator;
5. the structural census with a lowered ratchet; and
6. a release microbenchmark for the affected hot algorithm.

Do not defer all Test262 work to C8. The old case has already been deleted, so
its family must be green before the next batch obscures the regression.

## 10. Validation sequence

Run focused checks after each phase and the complete sequence for C8. Use a
release build for every performance comparison under repository rule 10.

```bash
# Formal/catalog/mechanical invariants
python3 utils/generate_well_known_names.py --check
python3 utils/js_callable_census.py --check
make test-js-exception-catalog
make test-js-callable-catalog
./utils/count_loc.sh
git diff --check

# Build and direct JS/runtime regression
make build-test
./test/test_js_gtest.exe
make test-lambda-baseline

# Conformance, GC, and embedding regression
make test262-baseline
make test-mir-gc-stress
make test-radiant-baseline

# Performance build and capture
make release
```

The final report records:

1. clean C0 and C8 commit IDs and `git status`;
2. all census rows, exceptions, and the deletion ledger;
3. `JsFunction` size class, realm function count, allocations, and retained
   bytes before/after;
4. focused callable/construct/property/alias/cross-realm results;
5. Test262 total with no baseline loss;
6. forced-GC/scalar-home/two-context/async-teardown results;
7. MIR fixture excerpts or automated assertions;
8. release benchmark corpus, machine, environment, repetitions, variance, and
   interleaved A/B results;
9. sampled stack evidence for removed mechanisms; and
10. C0/final `./utils/count_loc.sh` output and binary sizes.

An unrelated host failure is reported with its command and root cause. It does
not authorize declaring Tune4 complete without all scope-relevant gates.

## 11. Release performance and growth measurement

### 11.1 Measurements

Use the same clean post-Tune3 binary/corpus as the baseline and record:

| Metric | Constraint |
|---|---|
| Ordinary user-function call | No statistically significant regression; builtin fields must not perturb hot MIR lanes. |
| Intrinsic method call | Dispatcher/catalog-name work absent from sampled stack; wall/cycles no regression after variance. |
| Static generic method | Correct Get cost measured honestly; unsafe name/type shortcut is not a valid baseline requirement. |
| Dynamic construction | Constructor-name comparisons absent; explicit operand/entry overhead measured. |
| Bound call/construct | No permanent roots; merge allocation and wall time reported. |
| Realm startup | Time, function count, allocations, and retained bytes; >5% regression requires design review. |
| Test262/js batch | Wall time no statistically significant regression. |
| Binary size | Before/after reported and explained. |
| `lambda/js` C/C++ LOC | Hard exit `<= C0 - 1,000`; stretch target `<= C0 - 1,500`. |

Use interleaved A/B runs and report variance; the existing benchmark history
shows code/data placement can mimic a semantic regression. `JS_CALL_STATS`,
`JS_CALL_FORCE_GENERIC`, and `JS_CALL_LANE_CHECK` may diagnose lane mix, but a
future JR8 feedback path cannot be used to mask a Tune4 regression.

### 11.2 Startup stop condition

If eager real-property installation raises realm startup time or retained
bytes by more than 5%:

1. verify alias keys are not accidentally duplicating identities;
2. verify function objects are not constructed before their intrinsic object
   is requested;
3. reduce redundant per-function metadata or obsolete `JsFunction` fields;
4. retain lazy whole-object construction with transactional publication; and
5. stop for measured design review if the threshold remains.

Do not restore per-property miss-time fabrication or a second identity cache.

## 12. Risks and hard stop conditions

| Risk | Control / hard stop |
|---|---|
| Tune3 ABI still moving | C1 stops until the NameId/module-state handoff is clean and formal. |
| `JsFunction` grows beyond GC class | Delete obsolete fields and measure compact descriptor options first; size-class change requires recorded allocation/startup evidence. |
| Adapter ABI mismatch | Use typed typedefs and compiler errors; stop rather than cast through `void*`. |
| New mega-dispatch appears in target kind/arity | Selection is creation-time only. A repeated-call switch that chooses semantics fails C1/C2. |
| Target extraction duplicates algorithms | Promote/reuse existing helper; third near-identical variant stops the batch until shared shape exists. |
| Array/TypedArray brand drift | Separate target policy, focused borrowed/detached/OOB/species tests; no mutable global mode. |
| Alias identity drift | Default distinct, explicit alias key, same/cross-realm fixtures; never key cache by body pointer. |
| Pending `newTarget` retained for convenience | Stop. Fix the producer/entry operand flow and scoped active-state restoration. |
| Nested/super ERROR leaks active state | Stop C5, add the failing fixture, repair the scope owner; no cleanup flag workaround. |
| Method fast path bypasses mutation/Proxy | Delete it or add exact identity guard/fallback already justified by existing machinery; do not infer from spelling/TypeId. |
| Real properties cost startup | Follow §11.2; no miss-time semantic side channel. |
| Shared MIR gains context pointer | Stop and use symbolic import/current-context link state under **D5.4**. |
| GC bug tempts native-stack scan | Stop and fix precise `RootFrame`/`Rooted` ownership under **D5.3**; conservative scanning is retired. |
| Hard 1,000-line LOC exit missed | Tune4 remains incomplete. Revisit still-duplicated mechanisms in the ledger; do not relocate, compress, pad, or exclude files from the counter. |
| 1,500-line stretch target missed | Record the remaining justified production surface and measured delta; this does not relax the 1,000-line hard exit. |
| Class-map scope expands | Keep the one named JR4 bridge and stop adding metadata/branches. |
| Conformance regression postponed to later batch | Stop at the owning family; restore neither old switch nor compatibility fallback. |

## 13. Recommended commit series

Commits may be split more finely, but this dependency and deletion order stays
visible in review:

1. `js: adopt D6.2.2v2 and add callable census/fixtures`
2. `js: add distinct call and construct capability ABI`
3. `js: centralize call/construct ownership kernels`
4. `js: split MIR and typed native factories`
5. `js: migrate host factory batches and delete ambiguous factory`
6. `js: split intrinsic catalog targets from bindings`
7. `js: make realm intrinsic identity explicit`
8. `js: direct Function/Object/primitive intrinsic calls`
9. `js: direct numeric/string intrinsic calls`
10. `js: direct Array and TypedArray calls; delete global mode`
11. `js: direct collection/Date/RegExp/Error/Promise/host calls`
12. `js: delete builtin semantic dispatcher and ID groups`
13. `js: pass newTarget explicitly through MIR and runtime`
14. `js: direct intrinsic/bound/Proxy construct entries`
15. `js: delete pending and name-selected constructor state`
16. `js: install real intrinsic properties`
17. `js: lower all generic methods as NameId Get plus Call`
18. `js: remove sentinel callability and finish host convergence`
19. `js: close callable ratchets, docs, LOC, and release evidence`

Each commit that fixes a lifecycle, rooting, evaluation-order, or ABI defect
adds a brief root-cause/invariant comment at the fix point, per repository rule
12. Generic narration comments are not a substitute.

## 14. Completion evidence

### 14.1 Reproducible C0/C8 accounting

The authoritative C0 is a source-faithful archive of the clean post-Tune3 JS
tree. C0 and C8 use the same unmodified `utils/count_loc.sh` definition. The
checked-in census accepts `--root` so the same parser can inspect that archive;
catalog-schema validation is intentionally final-only because C0 predates the
target/binding schema.

| Measure | C0 | C8 | Result |
|---|---:|---:|---:|
| `lambda/js` C/C++ LOC | 225,711 | 221,377 | **−4,334 (−1.92%)** |
| Release `lambda.exe` | 22,292,520 bytes | 22,499,400 bytes | +206,880 (+0.93%) |
| `JsFunction` payload | 224 bytes | 256 bytes | +32 bytes |
| GC allocation slot including header | explicit class 7, 400 bytes | class 6, 272 bytes | **−128 bytes (−32%)** |

The C0 release SHA-256 is
`b76fe64b7dbd46ceb99c7cc7c51abc7b351a4bbe013a17807166678a770efb07`;
the final clean C8 release SHA-256 is
`aa9c55b9d3408ff229c42657d0aa29b78b9ec18fe7a7bbf20c99aba19389632b`.
The 4,334-line deletion exceeds both the mandatory 1,000-line exit and the
1,500-line stretch target. The modest binary increase is explained by typed
entry/factory coverage across JS, Jube, host modules, and diagnostics; it did
not relocate the deleted dispatcher or constructor chain.

The deletion ledger uses exact source counts:

| Retired mechanism | C0 | C8 |
|---|---:|---:|
| `js_dispatch_builtin` declarations / total references | 2 / 11 | 0 / 0 |
| Runtime `case JS_BUILTIN_*` semantic cases | 337 | 0 |
| Ambiguous `js_new_function` / raw native casts | 930 / 896 | 0 / 0 |
| Pending `newTarget` references | 106 | 0 |
| Special-constructor fields / references | 15 | 0 |
| Constructor-name comparisons in the dynamic-new chain | 43 | 0 |
| Intrinsic receiver/name dispatch references | 36 | 0 |
| Host receiver/name dispatch references | 55 | 0 |
| Property-miss synthesis references | 67 | 0 |
| Array receiver side-state references | 30 | 0 |
| Legacy invoke wrappers / migration flags | 8 / 4 | 0 / 0 |
| Direct global catalog shortcuts | 2 | 0 |

One exact global-identity load remains within its ratchet because its lowering
is justified by the proven binding, not receiver/name inference. The only
compatibility mechanism is `js_construct_entry_legacy_class_map` (one
definition, two total references). JR4 deletes it when class maps publish
ordinary Function/capability values; no other shadow callable route remains.

### 14.2 Startup, allocation, and release profile

Transactional whole-object laziness for `process`, `Buffer`, and `crypto`
keeps their real own global slots while deferring namespace construction until
the slot is observed. In the C8 empty-realm trace this reduced GC
`JsFunction` creation from the eager intermediate implementation's 202
objects / 54,944 requested bytes to 42 / 11,424. C0 allocated function records
from a different pool regime, so its per-object requested-byte count is not an
apples-to-apples retained-byte comparator. The matched whole-process capture
was 55/65 MiB C0 versus 58/68 MiB C8 for macOS footprint/RSS; RSS grew about
4.6%. Empty-realm median startup grew only 0.71%, below the 5% review
threshold. The rounded footprint signal was reviewed and led to the lazy
whole-object implementation rather than restoring miss-time fabrication.

The final release A/B used 11 interleaved measured repetitions per side:

| Workload | C0 median | C8 median | Delta |
|---|---:|---:|---:|
| Ordinary dynamic call | 51.684 ms | 48.791 ms | −5.60% |
| Intrinsic target | 345.869 ms | 235.358 ms | −31.95% |
| Static source method | 116.126 ms | 371.868 ms | +220.23% |
| Computed source method | 87.166 ms | 82.396 ms | −5.47% |
| Dynamic `Date` construction | 99.495 ms | 130.793 ms | +31.46% |
| Bound call | 125.277 ms | 110.306 ms | −11.95% |
| Bound construction | 82.232 ms | 80.952 ms | −1.56% |
| Empty-realm startup | 14.244 ms | 14.345 ms | +0.71% |

The static-method C0 result came from the deleted spelling/receiver shortcut,
which skipped observable property `Get`; the computed-property control uses
the canonical path on both sides and improves 5.47%. C0's dynamic `Date`
construction also skipped required `Get(newTarget, "prototype")` when
`newTarget === callee`; the bound-construction control improves 1.56%.
Therefore both slower rows are measured costs of restoring **D6.2.2v2**
semantics, not unexplained callable-kernel regressions. Full variance and
interpretation are also recorded in `doc/dev/js/JS_15_Performance.md` §2.1.

### 14.3 Correctness, ownership, and embedding gates

Final verification produced all of the following:

- generated well-known-name check, callable catalog validation, exception
  catalog validation, the complete callable census ratchet, and
  `git diff --check`: pass;
- nine focused Tune4 JS fixtures plus the call/construct MIR fixture: pass in
  debug, release, and forced-GC lanes;
- standalone JS GTest: 338/338; rebuilt input/runtime matrix: 3,692/3,692;
  MIR forced-GC stress: 61/61. The aggregate runner's idle watchdog killed
  the two quiet JS/Lambda binaries, which recovered standalone at 338/338 and
  713/713; all other aggregate lanes passed 2,641/2,641;
- Test262 baseline: 40,261 passing tests, zero regressions. Resource-killed or
  slow parallel items were rerun in isolation and all recovered; the external
  js262 tree changes exactly the 12 intended stripped TypedArray sources;
- scope-relevant Radiant embedding: view 23/23, page load 105/105, DOM/UI
  integration 63/63; and
- scalar-home, closure-environment ownership, nested/new-target cleanup,
  async spills/teardown, two-context realm identity, aliases, bound/Proxy,
  host constructor capabilities, and catalog/MIR budgets all pass their
  focused and aggregate lanes.

The broad Radiant sweep also reported pre-existing/out-of-scope layout sizing,
cross-origin image, and editor-automation issues; Radiant layout is explicitly
outside Tune4 under §2.3. The three callable/realm/document embedding suites
above isolate the surfaces Tune4 changes and are green.

## 15. Completion checklist

- [x] Tune2 ERROR-tag and Tune3 NameId prerequisites are complete.
- [x] **D6.2.2v2** is formally adopted and the formal-design semver is bumped.
- [x] C0 clean baseline, callable census, layout, release profile, and deletion ledger are recorded.
- [x] Every published function has a final call entry and exact construct capability.
- [x] Call wrappers delegate to one ownership-aware `js_call_value` kernel.
- [x] All construct producers delegate to one explicit-`newTarget` `js_construct_value` kernel.
- [x] MIR, fixed-native, span-native, intrinsic, constructor, bound, and exotic factories are typed and distinct.
- [x] `js_new_function(void*, arity)` and all mismatched native pointer casts are gone.
- [x] The one catalog produces target and binding tables with NameId and explicit identity keys.
- [x] All intrinsic call bodies are direct; `js_dispatch_builtin` and runtime `case JS_BUILTIN_*` semantics are gone.
- [x] `js_dispatch_as_array_method` and its guards/state are gone.
- [x] All intrinsic construct bodies are direct and use shared prototype selection.
- [x] Pending `newTarget`, special constructor fields, and name-byte constructor selection are gone.
- [x] Intrinsic methods/accessors are real realm properties; runtime misses do not synthesize them.
- [x] Generic static and computed method calls lower through NameId/computed `Get -> Call`.
- [x] Public receiver/name method dispatch imports and unsafe name/type direct lowerings are gone.
- [x] Bound and Proxy call/construct entries forward through the common kernels correctly.
- [x] Function.prototype is a real call-only function and ordinary objects have no sentinel/own-`.call` callability.
- [x] At most one named JR4 legacy class-map construct bridge remains, with a recorded deletion condition.
- [x] Catalog, structural, MIR, behavior, Test262, GC, scalar-home, async, two-context, and Radiant gates pass.
- [x] Release results show no unexplained regression and realm startup/retained growth is within the 5% review threshold.
- [x] `./utils/count_loc.sh` proves `lambda/js` C/C++ LOC is at least **1,000 lines below** the clean C0 baseline; this hard exit is not waived by passing tests or performance gates.
- [x] `lambda/js` C/C++ LOC targets at least 1,500 lines removed, with any shortfall above the hard exit explained in the final report.
- [x] JS implementation docs and `JS_Runtime_Redesign.md` describe the implemented mechanism and mark JR5 complete.

When this checklist is complete, callable behavior has one semantic owner per
callee, and the next redesign is JR6: property representation and lookup. JR6
can then optimize one canonical `Get -> Call` route instead of preserving
builtin, receiver-name, and constructor-name shadow semantics.
