# JS Tune5 — Property Runtime and Elements-Kind Implementation Plan

**Date**: 2026-08-11

**Status**: PROPOSED IMPLEMENTATION PLAN — blocked on the Tune4 Callable
handoff

**Planning tree anchor**: master `88aa5556c8` plus the in-progress Tune4
Callable work; P0 must recapture a clean post-Tune4 baseline

**Design authority**: [JS_Runtime_Redesign.md](JS_Runtime_Redesign.md),
especially **JR6.1–JR6.6**. Governing formal rulings are **D2.6.1–D2.6.2**,
**D3.4.1**, **D3.4.4v2–D3.4.5**, **D4.6.1v2–D4.6.2v2**,
**D5.2.1–D5.2.2**, **D5.3.1–D5.3.5**, **D5.4.1–D5.4.4**,
**D6.2.2v2**, and **D8.4.3**. **JC8** governs the observable
`Get`-before-argument-evaluation order of method calls.

This document is the execution plan for JR6. It does not reopen the adopted
property design. The target is one NameId/index property lane, exactly eight
receiver-aware semantic operations, one temporary exotic adapter, one
per-array elements-kind state machine, and one realm-owned prototype-index
guard. Old entry points and duplicate algorithms are deleted with their last
caller.

Per **D1.6**, compiler and ABI work is MIR Direct only. The frozen C2MIR path
is not changed, wrapped, or used as an acceptance gate.

---

## 1. Outcome and non-negotiable exits

Tune5 is complete only when ordinary objects, arrays, Proxy, TypedArray,
Arguments, DOM/host objects, reflection, bytecode lowering, and native callers
all enter the same eight semantic operation families.

| Gate | Required final result | Evidence |
|---|---|---|
| Public semantic surface | Exactly eight public C property symbols: `Get`, `Set`, `DefineOwn`, `Delete`, `HasProperty`, `HasOwn`, `GetOwnPropertyDescriptor`, and `OwnKeys`. | Header, registry, definition, and call-site census. |
| Key identity | Every operation receives one resolved scalar lane: non-zero `NameId` or ordinary `uint32_t` array index. Raw bytes and `String*` are materialization only. | MIR fixtures, shape probes, and zero alternate-identity scan under **D4.6.1v2**. |
| Receiver | `Get` and `Set` thread the original receiver explicitly through prototype, accessor, and Proxy paths. No ambient receiver handoff remains. | Inherited accessor/Proxy fixtures and zero `js_proxy_receiver` / `ScopedProxyReceiver` references. |
| Outcomes | `Set`, `DefineOwn`, and `Delete` return success/failure or ERROR. Strict assignment and Object/Reflect throwing policy are owned by callers. | Strict/sloppy/Object/Reflect matrix and zero strict-mode reads in the semantic core. |
| Ordinary properties | One shape/slot tier implements ordinary own lookup, descriptor rules, prototype traversal, and mutation. | Code ownership review and structural scan. |
| Exotics | Exactly one operation-tagged `js_property_exotic_adapter` contains the temporary Proxy, TypedArray, Arguments, DOM/host, and legacy `map_kind` dispatch. | One adapter definition/switch and no exotic property switches elsewhere. |
| Cache boundary | Existing load/store ICs, while retained, are guarded outer probes only. A miss calls the core and is observationally invisible; the core has no IC or feedback parameter. | Cache-miss equivalence tests and zero `JsLoadIC`, `JsStoreIC`, or `FeedbackSlot` in core signatures. |
| Array state | Every ordinary JS array has one explicit `JsElementsKind`; Arguments/content arrays and TypedArrays are excluded. | Debug assertions, constructor census, and transition tests. |
| Numeric identity | `PACKED_NUMERIC -> PACKED_TAGGED` changes physical storage and both visible and GC type tags without changing the array Item. | Layout assertions, forced-GC promotion tests, and identity fixtures. |
| Descriptor overlay | The array companion Map remains an orthogonal named/index-descriptor overlay for both numeric and tagged arrays. | Descriptor, enumeration, GC, and named-property tests. |
| Prototype guard | Array holes bypass an unchanged intrinsic chain only through receiver-selected `{epoch, clean}` state. Re-entry invalidates captured facts. | Cross-realm, mutation, Proxy-chain, and callback tests. |
| TypeMap safety | Ordinary internal `Map.type` validation is a debug-only C assertion. Release contains no plausibility call, recovery branch, log-and-miss, or synthesized fallback. | Debug death/invariant test plus release object/source scan under **D3.4.1/D3.4.5**. |
| Deletion | The superseded property, array-index, descriptor, prototype-scan, ambient-state, and public map-fast routes are absent. | P9 deletion ledger and standing census ratchets. |
| Source size | The aggregate production C/C++ delta across `lambda/js` and any Lambda-runtime support added by Tune5 is net negative. Target at least 750 lines removed. | Clean P0/P9 LOC snapshots; moving code between directories cannot satisfy the gate. |
| Hard JS runtime LOC reduction | Final `lambda/js` production C/C++ LOC is at least **2,000 lines lower** than the clean post-Tune4 P0 baseline: `final_lambda_js_loc <= P0_lambda_js_loc - 2000`. This is mandatory independently of the aggregate source-size gate. | The `lambda/js` result from the same unmodified LOC command and file scope at P0 and P9. |
| Behavior | Baseline JS, Test262, MIR/GC, DOM/layout, and focused Tune5 suites are green in debug and release as applicable. | P9 validation transcript. |
| Performance | Release-only property/array benchmark buckets show no material regression; no debug build is used for timing. | Repeated benchmark samples and profile comparison. |

The source-size gate is a design constraint, not permission to hide complexity
in `lambda/runtime`. New shared runtime support is counted. Tests,
documentation, generated files, and movement into a new translation unit do
not offset production growth.

---

## 2. Preconditions, scope, and fixed invariants

### 2.1 Tune4 handoff prerequisite

No Tune5 production ABI change begins until Tune4 records a clean handoff with
all of the following:

1. **D6.2.2v2** is adopted and the formal-design semver is current.
2. A dynamic method call performs observable property `Get` before argument
   evaluation and then enters the resolved callee's `[[Call]]` capability
   under **JC8**.
3. The callable and construct kernels are stable and no receiver/name method
   dispatcher remains for Tune5 to preserve.
4. Tune3's NameId contract remains final: non-zero NameId is the only
   persistent property identity under **D3.4.4v2** and
   **D4.6.1v2–D4.6.2v2**.
5. Tune4 behavior, GC, release, and LOC gates pass on a clean integration
   commit.
6. No Tune4-local callable compatibility adapter still implements property
   lookup semantics.

P0 may collect a read-only census and add isolated behavior-locking tests while
Tune4 is in progress. P1 must stop if any prerequisite is missing. Tune5 must
not edit around an unfinished callable ABI.

### 2.2 In scope

- Freeze one scalar property-lane ABI and the exact eight public semantic
  symbols.
- Make receiver, observable key materialization, and mutation outcome explicit.
- Consolidate ordinary shape/slot, descriptor, prototype, and array-element
  semantics in `js_props` plus one elements module.
- Consolidate current exotic behavior behind one temporary adapter for JR4.
- Remove ambient Proxy receiver and accessor-suppression state.
- Move strict/boolean/throwing choices to assignment, Object, and Reflect
  callers.
- Store `JsElementsKind` in the three reserved `Container.array_flags` bits.
- Admit owned ordinary numeric arrays, implement monotone promotion, and
  preserve identity and precise tracing.
- Generalize the existing companion Map contract to ordinary numeric arrays.
- Centralize the ordinary array-index classifier and keep the TypedArray
  canonical-numeric classifier distinct.
- Reuse realm intrinsic mutation versions for the array-prototype epoch and
  add a clean-chain fact.
- Turn the TypeMap plausibility guard into a debug-only C assertion at the
  ordinary internal tier and remove release recovery.
- Migrate MIR Direct lowering, the system-function registry, JS builtins,
  modules, Jube hosts, and Radiant/DOM callers.
- Delete obsolete property and array helper families.

### 2.3 Out of scope

- JR4's final `JsPropertyOps` class table. Tune5 leaves one explicitly named
  exotic adapter and no other alternate semantic route.
- JR8 feedback vectors, polymorphic caches, megamorphic policy, or speculative
  lowering. Existing IC structs may survive only as outer guarded adapters.
- Elements respecialization from tagged/holey/sparse back to numeric.
- A side bitmap for numeric holes, a JS-private array wrapper, or a second
  object identity.
- TypedArray storage redesign or unification with ordinary array elements.
- Arguments-object representation redesign.
- Lambda array semantics outside the explicitly identified shared support
  needed to preserve JS array identity and GC correctness.
- Parser, grammar, vendor, C2MIR, or unrelated Radiant layout work.
- Conservative native-stack GC scanning, hidden global roots, or untracked
  temporary Items.

### 2.4 Invariants carried through every phase

1. A property key has one semantic identity: `NameId` or ordinary array index.
   An observable key Item is a rooted materialization, never a lookup key.
2. The ordinary index classifier accepts only canonical decimal names in
   `0..2^32-2`. `2^32-1`, `-0`, leading-zero spellings other than `"0"`,
   signs, whitespace, decimals, exponents, NaN, and infinities remain named.
3. TypedArray `CanonicalNumericIndexString` is an exotic rule and never calls
   the ordinary classifier as a substitute.
4. `Get` and `Set` keep the original receiver while the target changes during
   prototype traversal.
5. `DefineOwn` never invokes an inherited setter. `Delete` affects only an own
   property. `HasProperty` does not invoke an ordinary getter.
6. `Set`, `DefineOwn`, and `Delete` distinguish `false` from ERROR. No core
   reads ambient strictness or guesses which public API called it.
7. A cache hit is legal only when it is equivalent to the complete semantic
   path. Anything unproven misses to the core without observable work.
8. Each published ordinary array has exactly one non-`NONE` elements state.
   Its physical TypeId, flags, data ownership, length, companion, and state
   agree at every safepoint.
9. A representation transition is observationally atomic: prepare and root
   before commit; publish no half-converted array to user code or GC.
10. Numeric arrays cannot contain holes. The first successful hole-producing
    operation promotes to tagged storage.
11. `undefined` is a present nonnumeric value and promotes numeric storage to
    packed tagged storage.
12. The companion Map is an overlay, not an elements kind. Named properties
    may coexist with numeric storage without forcing promotion.
13. Indexed accessors and non-default indexed descriptors are authoritative in
    the companion and require tagged element handling.
14. The prototype clean bit proves only the exact receiver-selected intrinsic
    `Array.prototype -> Object.prototype` chain at a captured epoch.
15. Any callback, accessor, Proxy trap, coercion, or host call that can execute
    JavaScript invalidates local array representation and prototype facts.
16. A non-null ordinary internal `Map.type` is a valid `TypeMap*` under
    **D3.4.1/D3.4.5**. Debug asserts it; release trusts it.
17. All fallible operations return success or an ERROR-tagged Item under
    **D8.4.3**; Tune5 introduces no pending exception channel.
18. Every temporary object/value that survives allocation or user re-entry is
    held by `RootFrame` / `Rooted` under **D5.3**.
19. Hot single-owner paths use the owner-thread model of
    **D5.4.1–D5.4.4**; Tune5 adds no locks or atomic reference protocol.
20. At the third near-identical type/kind/operation branch, extract or move the
    shared implementation; never copy a local `static` helper.

---

## 3. Planning baseline and authoritative census

### 3.1 Planning snapshot

The dirty planning tree contains active Tune4 changes, so these values size
the task but are not acceptance baselines:

| Surface | Planning observation |
|---|---:|
| `js_property_get` references | about 2,559 |
| `js_property_set` references | about 3,531 |
| `js_property_access*` references | dozens, including NameId and IC variants |
| `js_array_new` references | about 463 across JS, module, and host code |
| Direct `.array` field references in `lambda/js` | about 484 |
| `js_array_get_int` references | about 628 |
| `js_map_get_fast` / `_ext` references | about 133 / 134 |
| `js_proxy_receiver` references | about 26 |
| `ScopedProxyReceiver` references | about 21 |
| `js_skip_accessor_dispatch` references | about 39 |
| Prototype numeric-scan helper references | about 26 |
| TypeMap plausibility predicate sites in the relevant JS/runtime scope | about 10 |
| Numeric-name formatting sites in the relevant JS paths | about 72 |

Counts include declarations, comments, generated-like registries, and
non-semantic callers. They must not be used as blind replacement counts.

### 3.2 P0 clean baseline

Immediately after the Tune4 integration commit, record:

```bash
git status --short
git rev-parse HEAD
./utils/count_loc.sh
rg -n "js_property_(get|set|access)|js_array_(get|set)|js_map_get_fast" lambda/js lambda/runtime modules radiant test
rg -n "js_proxy_receiver|ScopedProxyReceiver|js_skip_accessor_dispatch|js_strict_mode" lambda/js
rg -n "js_try_exotic|MAP_KIND_|JS_CLASS_|TypedArray|Arguments" lambda/js
rg -n "proto_chain_has_numeric|numeric_keys|mutation_versions|mutation_serial" lambda/js
rg -n "typemap_ptr_is_plausible|Map\.type|->type" lambda/js lambda/runtime
rg -n "snprintf|array_key_is_index|parse_index|CanonicalNumericIndexString" lambda/js
```

Store the full output under `temp/tune5_property/`; never under `/tmp`. The
commit hash, compiler, platform, configuration, Test262 checkout/version, and
benchmark command lines are part of the record.

### 3.3 Required census manifest

Add `utils/js_property_census.py` before production migration. It prints
machine-readable counts and fails standing ratchets. The manifest classifies,
rather than merely counts:

- public semantic declarations, definitions, registry rows, and MIR calls;
- computed, raw-string, NameId, index, strict, descriptor, and IC wrappers;
- ambient receiver, accessor-suppression, and strictness reads;
- exotic switches and callbacks, grouped by all eight operations;
- ordinary shape/slot primitives versus exported semantic helpers;
- duplicate ordinary array-index classifiers and numeric name materializers;
- ordinary, content, Arguments, TypedArray, and Lambda-only array constructors;
- elements-state reads/writes and direct physical-layout casts;
- sparse maps, descriptor overlays, length mutations, and enumeration paths;
- prototype numeric scans, mutation hooks, epoch reads, and clean-bit reads;
- TypeMap plausibility predicates split into internal, external-input, and
  cache-validation boundaries;
- IC fast paths that perform receiver, descriptor, exotic, or prototype
  semantics;
- Jube/host property callbacks and their adapter ownership;
- obsolete symbol definitions whose last caller disappears in each phase.

Each row records `symbol`, `owner file`, `semantic operation`, `key form`,
`receiver form`, `can re-enter`, `can allocate`, `error/outcome form`,
`replacement`, `deletion phase`, and `test owner`.

### 3.4 Branch discipline

- P0 creates the census and behavior fixtures only.
- Each migration phase starts from green and ends green.
- A compatibility wrapper is allowed only when named in that phase's ledger,
  delegates without semantic branches, and is deleted with its last caller.
- Do not stack later work on a phase that has an unresolved behavior, GC,
  invariant, or release failure.
- Every bug fixed during Tune5 receives a brief root-cause/invariant comment at
  the fix point, per repository rule 12.

---

## 4. Final semantic ABI

### 4.1 `JsPropertyLane`

P1 freezes a scalar, non-GC `uint64_t` lane:

```c
typedef uint64_t JsPropertyLane;
```

The planned encoding is:

- bits `0..31`: `NameId` or ordinary `uint32_t` index payload;
- bit `32`: index discriminator (`1` = index, `0` = NameId);
- bits `33..63`: zero;
- named lane payload `0` is invalid because `NAME_ID_NONE` is not a property;
- indexed lane payload `0xffffffff` is invalid because the largest ordinary
  array index is `0xfffffffe`.

P1 adds constructors, predicates, and payload accessors with compile-time
assertions. No caller performs bit arithmetic directly. The lane is never
stored in a JS object, TypeMap, descriptor, or IC as a second key identity;
an IC may store the already-authoritative NameId/index payload it guards.

### 4.2 Key conversion and observable materialization

Computed source keys use two non-semantic conversion helpers:

1. `ToPropertyKey(raw)` returns a rooted canonical string/Symbol Item or ERROR.
2. `js_property_lane_from_key(key)` classifies a completed key without
   allocation or user code and returns a lane.

The converted key Item is passed as an optional `observable_key` only while a
Proxy, reflection, or exotic boundary may observe its spelling or Symbol
value. Ordinary lookup uses the lane alone. Static NameId/index lowering
passes an empty materialization marker and the adapter lazily materializes a
canonical key from the owning NamePool only if observation is required.

The empty marker is an ABI sentinel, not a semantic key. P1 must choose a value
that cannot be returned by `ToPropertyKey`, document it in the header, and
assert its use at the adapter boundary. It does not enter shapes or equality.

### 4.3 Exact eight symbols

P1 chooses final C names once and registers exactly these contracts:

| Operation | Inputs | Result |
|---|---|---|
| `js_get` | target, lane, observable key, receiver | value or ERROR |
| `js_set` | target, lane, observable key, value, receiver | boolean success or ERROR |
| `js_define_own` | target, lane, observable key, descriptor presence/attribute bits, value/getter/setter Items | boolean success or ERROR |
| `js_delete` | target, lane, observable key | boolean success or ERROR |
| `js_has_property` | target, lane, observable key | boolean or ERROR |
| `js_has_own` | target, lane, observable key | boolean or ERROR |
| `js_get_own_property_descriptor` | target, lane, observable key | descriptor object, `undefined`, or ERROR |
| `js_own_keys` | target | array of keys or ERROR |

The exact exported names may change during the P1 header review, but the count,
operation mapping, and semantics may not. A rename is not a ninth operation.

`DefineOwn` crosses MIR with scalar presence/attribute bits and Item fields;
it does not expose a pointer to a transient C++ descriptor object. Inside the
core those operands initialize the existing rooted `JsPropertyDescriptor`
record. Internal tiers may use pointer records after rooting and validation.

### 4.4 Outcome ownership

The semantic core never reads `js_strict_mode` and never decides API policy:

| Caller | Core result `false` | Core ERROR |
|---|---|---|
| sloppy assignment | evaluate to assigned value per existing source semantics | propagate ERROR |
| strict assignment | create/return TypeError | propagate ERROR |
| `Reflect.set` / `Reflect.defineProperty` / `Reflect.deleteProperty` | return `false` | propagate ERROR |
| `Object.defineProperty` / `Object.defineProperties` | throw TypeError | propagate ERROR |
| `delete` expression | strict/sloppy lowering applies language rule | propagate ERROR |
| internal create-data-property callers | apply the spec-required assertion or throw at the caller | propagate ERROR |

P2 centralizes these caller policies into a small set of clearly named
non-semantic adapters. They must not repeat lookup, prototype, descriptor, or
exotic logic and are not registered as public property operations.

### 4.5 Core pipeline

Every operation follows the same structural pipeline:

```text
optional outer cache probe
  -> validate/classify target storage or primitive boxing rule
  -> one exotic adapter when class/key requires it
  -> ordinary own elements or shaped slot tier
  -> prototype traversal when the operation requires it
  -> operation-specific result normalization
```

Classification precedes reading fields whose meaning depends on TypeId or
storage kind. Transitional containers whose `type` word is not a TypeMap must
be routed before the ordinary Map tier.

---

## 5. Array representation contract

### 5.1 Elements-state encoding

The three reserved high bits of `Container.array_flags` form one mask. P4
adds an enum whose zero value deliberately means unmanaged/excluded:

| Encoded state | Meaning |
|---|---|
| `JS_ELEMENTS_NONE = 0` | Not an ordinary JS array: Lambda-only array, Arguments/content carrier, TypedArray view/backing, or not yet published. |
| `JS_ELEMENTS_PACKED_NUMERIC` | Owned one-dimensional ordinary `ARRAY_NUM`; every index below length is present and numeric. |
| `JS_ELEMENTS_PACKED_TAGGED` | Ordinary tagged `ARRAY`; every index below length is present. |
| `JS_ELEMENTS_HOLEY_TAGGED` | Ordinary tagged `ARRAY`; holes may occur. |
| `JS_ELEMENTS_SPARSE_TAGGED` | Ordinary logical array with tagged prefix and `SparseArrayMap`. |

The remaining encodings are invalid and asserted in debug builds. Accessors
mask only the reserved bits and preserve `is_ndim`, `is_view`, `is_pinned`,
`is_mutable_view`, and `is_native_lane_array`. `reserved_state` is untouched.

Zero cannot silently mean `PACKED_TAGGED`: existing Lambda, input, and
TypedArray allocations also begin with zero. P4 stamps every ordinary JS array
at its owning constructor before publication and asserts non-zero state on
entry to ordinary array semantics.

### 5.2 Ordinary numeric admission

P5 initially admits `PACKED_NUMERIC` only when all facts are proved at
construction:

- the value is an ordinary JS Array, not a TypedArray, Arguments object,
  content list, view, or n-dimensional array;
- storage is owned by the runtime GC/data zone and may be replaced;
- physical TypeId is `LMD_TYPE_ARRAY_NUM`;
- element storage is an eight-byte numeric lane supported by the existing
  `elem_type` contract (`ELEM_INT`, `ELEM_INT64`, or `ELEM_FLOAT64` as
  validated by the implementation);
- every logical index is present;
- the companion-tail reservation protocol can be honored;
- the creator knows all initial values are numeric.

Generic `js_array_new` remains tagged. Numeric array literals and measured
numeric-only builders use a distinct internal creator only when proof is
available. Tune5 must not bulk-convert hundreds of callers merely because
their current initial length is zero.

### 5.3 Physical compatibility gate

Current GC tracing selects layout from the GC header tag, while JS and Lambda
code also inspect the container's visible `type_id`. Therefore changing only
`Container.type_id` is incorrect. P5 may implement in-place identity-preserving
promotion only after proving and asserting:

- `sizeof(Array) == sizeof(ArrayNum)` for the promoted ordinary forms;
- the common header, data pointer, length, capacity, and `extra` offsets match;
- both allocations reserve compatible tail metadata;
- the GC allocation size remains valid for the replacement tag;
- the data-owner protocol permits releasing/replacing the old numeric buffer;
- no TypedArray/view/n-dimensional object reaches the helper.

Add one narrow Lambda-owned runtime primitive, provisionally
`heap_retag_container`, instead of allowing JS code to reach into a GC header.
It accepts the object plus expected and replacement TypeIds, performs debug
assertions for managed ownership, old header tag, visible old TypeId, layout,
and allocation size, and updates both tags inside a no-safepoint commit. It has
no recovery return and no side-table fallback. Release performs the required
retag directly; it does not turn invariant failure into a property miss.

If these facts cannot be proved on every supported GC configuration, P5 stops
and the design is brought back for review. Replacing the object, wrapping it,
or preserving identity in a hidden forwarding table is forbidden.

### 5.4 Promotion protocol

`PACKED_NUMERIC -> PACKED_TAGGED` proceeds in three stages:

1. **Prepare**: root the array, incoming value, companion, descriptors, and any
   materialized key; allocate a tagged Item buffer; box every present numeric
   element; reserve the companion tail; perform all fallible work.
2. **Validate**: re-read current state after any safepoint; verify the receiver
   still owns the expected storage and the pending operation is admissible.
3. **Commit**: in a no-allocation/no-callback extent, swap data/length/capacity
   metadata, set the new elements state, retag GC header and visible TypeId,
   and publish the authoritative companion. Reclaim the old backing through
   its actual owner protocol.

The commit is observationally atomic, not a request for hardware atomics.
There is no user code or safepoint between its writes under **D5.4**.

Numeric widening that stays numeric uses the same prepare/validate/commit
discipline for the backing buffer but does not retag the container. Integer
overflow or a floating value may widen to the supported numeric lane; a
nonnumeric value promotes to tagged.

### 5.5 Companion Map on numeric arrays

The existing reserved-tail companion mechanism is generalized, not copied:

- promote `js_array_has_props`, `js_array_props`, and `js_array_set_props` to
  one shared sequence helper accepting the common compatible container shape;
- reserve one high tail word, increment/maintain `extra`, and set
  `has_js_props` without changing elements kind;
- teach ordinary `ARRAY_NUM` GC tracing to mark that flag-gated tail Item;
- preserve the tail across numeric growth, widening, and tagged promotion;
- keep TypedArray views/backings excluded by explicit storage classification;
- keep the companion's Map/TypeMap descriptor and insertion-order behavior
  identical for numeric and tagged arrays.

No second numeric-array property table is allowed. If the current generic
ArrayNum grow routine cannot preserve the tail, P5 adds one shared
tail-preserving primitive or moves the existing implementation; it does not
duplicate a JS-private allocator.

### 5.6 Transition matrix ownership

P4 creates `lambda/js/js_elements.h` and `lambda/js/js_elements.cpp` only if
the baseline confirms no existing coherent owner. Existing helpers move from
`js_runtime.cpp`, `js_props.cpp`, and `collection_runtime.cpp`; they are not
copied. The module owns:

- state access and debug validation;
- ordinary index classification and element-presence probes;
- dense get/set/delete primitives with descriptor-overlay checks;
- capacity/growth and sparse-threshold decisions;
- numeric widening and tagged promotion;
- packed-to-holey and dense-to-sparse transitions;
- sparse-to-holey compaction when the existing measured threshold permits;
- length transaction support;
- element-key collection for `OwnKeys`.

The semantic operation orchestration remains in `js_props.cpp`. The elements
module does not traverse prototypes, invoke Proxy traps, inspect strict mode,
or expose a ninth public property API.

### 5.7 Required state transitions

| Successful event | Source | Final state / action |
|---|---|---|
| compatible indexed overwrite | packed numeric | stay numeric |
| contiguous compatible append | packed numeric | stay numeric |
| numeric value requiring wider numeric lane | packed numeric | widen backing, stay numeric |
| present nonnumeric value, including `undefined` | packed numeric | packed tagged |
| deletion of a present element | packed numeric/tagged | holey tagged |
| deletion of an absent element | any | no transition |
| length growth | packed numeric/tagged | holey tagged |
| nearby gapped write | packed numeric/tagged | holey tagged |
| sufficiently distant write | packed/holey | sparse tagged |
| indexed accessor or non-default descriptor | numeric/plain tagged | tagged state plus authoritative companion overlay |
| successful length shrink | any | transactional deletions; retain or weaken specialization as required |
| density crosses existing compaction threshold | sparse tagged | optional holey tagged |
| arbitrary later numeric-only contents | tagged/holey/sparse | no automatic respecialization |

Failed operations and no-ops do not mutate representation. Thresholds affect
storage choice only and never JS-observable results.

### 5.8 Descriptor and key-order rules

- A present ordinary element without an overlay synthesizes a writable,
  enumerable, configurable data descriptor.
- A hole is absent.
- An indexed companion entry overrides the default element descriptor and may
  carry an accessor.
- `length` remains non-enumerable and non-configurable with its existing array
  semantics; its writable transition is transactional.
- Length shrink processes indices in descending semantic order, stops on a
  non-configurable property, restores the required resulting length, and
  returns `false` rather than partially pretending success.
- `OwnKeys` emits canonical array-index keys in ascending numeric order, then
  other strings in insertion order, then Symbols in insertion order.
- The merge removes duplicates between dense/sparse elements and companion
  entries without stringifying every element inside the scan.
- Key strings are materialized only while constructing the observable result.

---

## 6. Prototype-index guard and invalidation

### 6.1 Reuse existing realm state

`JsIntrinsicState` already owns intrinsic prototypes,
`mutation_versions[JS_CLASS__COUNT]`, and `mutation_serial`. Tune5 reuses
`mutation_versions[JS_CLASS_ARRAY]` as the array-chain epoch and adds only:

- `array_indexed_proto_clean`;
- an initialization/known-state bit if required to distinguish “not computed”
  from `false`.

It does not add another epoch counter or a process-global clean flag.

### 6.2 Exact clean predicate

`clean == true` only when all are true:

1. the receiver's prototype is that realm's exact intrinsic
   `Array.prototype`;
2. that object's prototype is the same realm's exact intrinsic
   `Object.prototype`;
3. neither prototype has an own canonical ordinary array-index property,
   descriptor, or accessor;
4. neither link or object involved is a Proxy or unrecognized host exotic;
5. the captured epoch equals the realm's current Array mutation version.

The active caller realm is insufficient. Cross-realm arrays select state by
their own intrinsic prototype identity. A custom or unrecognized chain uses
the generic path.

### 6.3 Mutation ownership

Successful `Set`, `DefineOwn`, `Delete`, and prototype-link changes already
converge on intrinsic mutation notification. P7 moves notification to the new
semantic commit points and ensures:

- canonical indexed mutation on `Array.prototype` increments the Array epoch;
- canonical indexed mutation on `Object.prototype` also increments the Array
  epoch because the guarded chain includes it;
- descriptor-kind and accessor changes count even when the value is unchanged;
- changing either prototype link increments and conservatively recomputes;
- removing the last indexed entry may recompute `clean = true`;
- a failed/no-op mutation does not claim a semantic change;
- wrapper APIs do not notify a second time.

The recomputation scans only after relevant mutation, not on every element
read. The current repeated `js_proto_chain_has_numeric_keys` family is deleted
after all callers use the epoch/clean guard.

### 6.4 Re-entry rule

An optimized array loop may capture state and epoch only until the next
potential JavaScript re-entry. After a callback, getter/setter, Proxy trap,
coercion, comparator, species constructor, iterator call, or host callback it
must reload:

- receiver physical TypeId and `JsElementsKind`;
- data pointer, length, capacity, and sparse version;
- companion presence and relevant descriptor facts;
- receiver-selected realm epoch and clean bit.

The implementation may centralize this in a loop-state snapshot helper, but
that helper is not a cache of semantics and must have an explicit revalidation
call after re-entry.

---

## 7. TypeMap hard-invariant migration

### 7.1 P0 classification

Every `typemap_ptr_is_plausible` call is assigned to one of:

1. **ordinary internal property/class tier** — hard invariant;
2. **external serialization/host input boundary** — validation may be real;
3. **cache or stored-shape guard** — validation may legally mean cache miss;
4. **wrong-kind field interpretation** — fix classification before reading.

Category 1 is the JR6 target. Category 4 is a root-cause bug, not a reason to
keep the predicate. Categories 2–3 require an explicit allowlist and separate
function naming so they cannot recreate internal recovery.

### 7.2 Final internal form

After null/exotic/wrong-kind classification, every ordinary Map entry uses:

```c
#ifndef NDEBUG
assert(!map->type || typemap_ptr_is_plausible(map->type));
#endif
```

Release code proceeds directly from the invariant. It contains no predicate
call, branch, log, error construction, `undefined`, `ItemNull`, class-none, or
property-miss recovery for an implausible non-null TypeMap.

The assertion belongs in C/C++ runtime code, not emitted MIR. MIR calls the
semantic property ABI and never emits a duplicate TypeMap plausibility check.

### 7.3 Regression ownership

The prior `lib_marked.js` symptom came from MIR last-closure environment
tracking. Preserve the focused closure/block-shadow regression as the
root-cause gate. Add a debug-only unit/death test that deliberately constructs
the invalid internal state at the narrow test seam and proves the assertion
fires. Add a release structural/object check proving the predicate is absent
from the ordinary internal route. Do not corrupt ordinary integration tests
or make release behavior depend on corruption.

---

## 8. Phase dependency graph

```text
Tune4 clean handoff
        |
        v
P0 census + behavior locks + deletion ledger
        |
        v
P1 lane/eight-op ABI + TypeMap invariant boundary
        |
        v
P2 receiver-explicit ordinary semantic core
        |
        +------------------+
        v                  v
P3 one exotic adapter   P4 elements-state ABI + classifier
        |                  |
        |                  v
        |               P5 numeric storage, companion, GC retag
        |                  |
        |                  v
        +-------------->P6 descriptors, sparse, length, array algorithms
                           |
                           v
                    P7 realm epoch/clean guard
                           |
                           v
                    P8 MIR/host migration + old API deletion
                           |
                           v
                    P9 ratchets, docs, release evidence
```

P3 and early P4 can proceed independently only after P2's contracts are
frozen. P5 cannot begin before the physical compatibility gate passes. P8 is
the only phase allowed to delete the final delegating legacy ABI because it
owns the complete caller migration.

---

## 9. Detailed implementation phases

### P0 — Clean handoff, behavior locks, and measured ledger

#### Objectives

- Establish the authoritative post-Tune4 baseline.
- Separate actual semantic APIs from raw storage helpers and host protocols.
- Freeze existing observable behavior before changing representation.
- Identify exact deletion owners and stop conditions.

#### Work

1. Verify the Tune4 handoff in §2.1 and record the clean anchor.
2. Run the P0 commands and add `utils/js_property_census.py`.
3. Build a call-graph ledger for the eight operations from source lowering,
   Object/Reflect, Proxy, TypedArray, Arguments, arrays, DOM/host, modules,
   and internal algorithms.
4. Classify all array creators by ordinary/tagged, proven numeric,
   Arguments/content, TypedArray/view, and Lambda-only ownership.
5. Classify every TypeMap plausibility site per §7.1.
6. Inventory existing tests before adding new fixtures; extend an existing
   focused test where it already owns the behavior.
7. Add missing behavior locks for:
   - inherited getter and setter receiver identity;
   - Proxy key materialization and trap order;
   - `Reflect.set` receiver distinct from target;
   - strict versus sloppy failed assignment;
   - `DefineOwn` bypassing inherited setters;
   - `HasProperty` versus `HasOwn` and getter non-invocation;
   - delete own-only behavior;
   - ordinary key-boundary spellings;
   - holes versus present `undefined`;
   - array descriptor overlays and key ordering;
   - length shrink around non-configurable indices;
   - custom/cross-realm/Proxy prototype chains;
   - the focused closure/block-shadow TypeMap regression.
8. Record debug/release baseline tests and release-only benchmark samples.

#### Exit gate

- Clean anchor and LOC/census artifacts recorded.
- Every old symbol has an owner phase and intended replacement.
- Every array constructor has a classification.
- All behavior locks pass before production changes.
- Any unexplained baseline failure stops P1.

### P1 — Freeze the lane ABI, eight operations, and invariant boundaries

#### Objectives

- Establish the final semantic signatures before migrating algorithms.
- Ensure computed conversion happens once and static lowering needs no string.
- Make descriptor and outcome ABIs MIR-safe.
- Replace ordinary internal TypeMap recovery with the debug assertion.

#### Work

1. Add `JsPropertyLane`, mask/constructor/accessor helpers, static assertions,
   and debug validity assertions in the narrow shared header.
2. Centralize the ordinary array-index classifier over resolved NameId/NameRef
   data; prove the boundary table from §2.4.
3. Keep the TypedArray canonical-numeric classifier separate and rename it if
   current names invite accidental reuse.
4. Add final prototypes for the eight symbols and one internal operation enum
   for the exotic adapter.
5. Define the empty observable-key marker and lazy NameId/index
   materialization helper.
6. Freeze `DefineOwn` descriptor presence/attribute bit layout with compile-time
   assertions shared by C and MIR registry metadata.
7. Freeze boolean-or-error encoding. Prefer the existing Item boolean/error
   lane so `false` cannot be confused with `ItemNull` or ERROR.
8. Introduce delegating shells for the eight operations. Initially they may
   enter the existing implementation, but the shell contains no duplicate
   semantics and each delegation is listed for deletion.
9. Replace category-1 TypeMap plausibility/recovery sites with the exact
   debug-only assertion. Fix wrong-kind classification sites before asserting.
10. Rename/encapsulate approved external or cache validators so the final
    census can distinguish them.
11. Add registry and ABI compile tests plus key-lane table tests.

#### Exit gate

- Eight declarations and eight definitions exist with frozen signatures.
- All lane constructors/classifiers pass boundary tests.
- Static names/indices require no allocation before ordinary lookup.
- No ordinary internal release path calls the plausibility predicate or
  recovers from an invalid non-null TypeMap.
- Existing legacy APIs still pass only by delegating to or from one named
  transition seam.

### P2 — Receiver-explicit ordinary semantic core

#### Objectives

- Make ordinary Map/property semantics authoritative in `js_props.cpp`.
- Thread receiver and outcomes explicitly.
- Delete ambient semantic state.

#### Work

1. Move/promote existing raw shape/slot helpers into one internal ordinary
   tier; delete duplicated probes instead of copying them.
2. Implement own-property lookup by lane:
   - index lane first consults ordinary array elements/overlay when applicable;
   - NameId lane uses TypeMap shape entries;
   - Symbols/private names remain NameId identities with correct visibility.
3. Implement `Get` prototype traversal with immutable original receiver and
   accessor invocation through Tune4's call kernel.
4. Implement `Set` using the ordinary `[[Set]]` algorithm: inherited writable
   data, inherited setter, receiver own-property checks, extensibility, and
   receiver-based definition all return boolean/error explicitly.
5. Implement `DefineOwn` through the existing descriptor validation/apply
   engine, then remove `js_skip_accessor_dispatch` and its RAII guard.
6. Implement own-only `Delete`, `HasOwn`, prototype-aware `HasProperty`, own
   descriptor synthesis, and ordinary `OwnKeys` orchestration.
7. Remove `js_proxy_receiver` and `ScopedProxyReceiver`; P3's adapter receives
   receiver explicitly.
8. Move strict/sloppy/Object/Reflect conversions to caller-policy helpers and
   remove strictness reads from all semantic paths.
9. Ensure every allocation/accessor call roots target, receiver, key
   materialization, descriptor fields, pending value, and traversal cursor.
10. Add re-entry comments at points where receiver/shape/prototype facts must
    be reloaded.

#### Exit gate

- Ordinary objects pass all eight operations through the new core.
- Inherited getters/setters and distinct receivers pass forced-GC tests.
- `js_proxy_receiver`, `ScopedProxyReceiver`, and
  `js_skip_accessor_dispatch` have zero references.
- The core has zero ambient strictness reads.
- Descriptor, non-extensible, sealed, and frozen-object matrices pass.

### P3 — Consolidate the one exotic adapter

#### Objectives

- Preserve current exotic behavior without preserving scattered switches.
- Make the adapter operation-complete and receiver/key explicit.
- Leave one clean replacement seam for JR4.

#### Work

1. Add one internal `js_property_exotic_adapter(op, target, lane,
   observable_key, receiver, descriptor/value operands)` contract whose result
   distinguishes handled, not-handled, false, value, and ERROR without
   ambiguous sentinels.
2. Move, do not copy, the existing Proxy branches for all eight operations.
   Trap selection, target forwarding, receiver, invariant validation, and key
   materialization live in this adapter.
3. Move TypedArray numeric-key dispatch while retaining its distinct
   CanonicalNumericIndexString and bounds/detachment rules.
4. Move Arguments mapped-index behavior, String exotic indexed properties,
   Function special properties, Error, iterator, ArrayBuffer/DataView,
   ProcessEnv, DOM/host, and transitional `map_kind` behavior as actually
   present in the P0 ledger.
5. Translate Jube host callbacks at the adapter boundary. Existing host ops
   remain host protocols, not additional JS semantic operation families.
6. Ensure static lanes lazily materialize observable keys only for adapters
   that need them. Proxy receives the exact `ToPropertyKey` result for computed
   keys and the canonical materialization for static lanes.
7. Delete `js_try_exotic_property_get/set/has/delete/own_names/own_desc` and
   operation-specific switches from `js_runtime.cpp`, `js_globals.cpp`, and
   host bridges with each migrated batch.
8. Add table-driven coverage proving every exotic class has an explicit
   disposition for every operation: handled, ordinary fallback, or forbidden.

#### Exit gate

- Exactly one transitional exotic adapter definition/switch exists.
- No receiver, key-conversion, prototype, or descriptor algorithm is copied
  into the adapter.
- Proxy, TypedArray, Arguments, String, DOM/host, and Jube focused suites pass.
- An unhandled exotic cannot silently fall through by treating an overloaded
  field as ordinary TypeMap state.

### P4 — Elements-state ABI, constructor stamping, and index unification

#### Objectives

- Introduce the explicit per-instance state without yet broadening numeric
  admission.
- Give all current ordinary tagged arrays a truthful state.
- Centralize dense/sparse/index descriptor mechanics.

#### Work

1. Add enum/mask/accessors in the existing common container header with
   compile-time offset/mask assertions.
2. If needed, register `js_elements.cpp` through
   `build_lambda_config.json` and regenerate build files with `make`; never
   edit generated `.lua` files.
3. Move existing array index, dense, hole, sparse, companion, and threshold
   helpers into their single owners.
4. Stamp ordinary JS constructors:
   - length zero or fully present generic arrays -> packed tagged;
   - arrays created with an absent range -> holey tagged;
   - sparse constructors/materializations -> sparse tagged;
   - excluded carriers -> none.
5. Add debug entry assertions connecting state to TypeId, holes, sparse map,
   view flags, ownership, and companion kind.
6. Route all eight operations' array-index lanes through elements + companion
   tiers; no per-element NameId interning.
7. Replace duplicate ordinary index parsers with the central classifier.
8. Make descriptor synthesis and `OwnKeys` merge work for all current tagged
   states before numeric admission.
9. Update array algorithms to ask state/presence helpers instead of directly
   assuming `.array` layout where semantics matter.

#### Exit gate

- Every published ordinary tagged JS array has correct non-zero state.
- Excluded containers remain zero and never enter ordinary transitions.
- One ordinary index classifier remains.
- Tagged packed/holey/sparse behavior and key ordering pass.
- Direct layout access census is reduced to documented internal storage code.

### P5 — Numeric arrays, companion tracing, and identity-preserving promotion

#### Objectives

- Land the hard physical prerequisite correctly.
- Admit only proven numeric ordinary arrays.
- Preserve companion properties and precise GC tracing through promotion.

#### Work

1. Prove and encode the layout/ownership assertions from §5.3 on all build
   targets.
2. Add the narrow Lambda-owned GC/container retag primitive; keep GC-header
   details out of JS source.
3. Generalize the reserved-tail companion helpers to compatible ordinary
   tagged/numeric sequences and remove the old Array-only copies.
4. Update `ARRAY_NUM` tracing to mark a flag-gated companion only for the
   admitted owned ordinary form. Verify views/TypedArrays cannot set the flag
   through this path.
5. Add tail-preserving numeric grow and widening through a shared owner.
6. Add a proven-numeric ordinary-array creator and convert only constructor
   sites whose P0 classification establishes the proof.
7. Implement numeric read/write/append and widening.
8. Implement numeric-to-tagged prepare/validate/no-safepoint commit, including
   GC header retag, visible TypeId, state, backing buffer, and companion tail.
9. Force promotion for nonnumeric present values, hole creation, indexed
   accessors, and non-default descriptors.
10. Add debug-only representation inspectors for tests if no existing seam can
    prove the state. They must not ship as JS-observable release APIs.
11. Exercise promotion during allocation pressure and immediately before/after
    minor and full GC.

#### Exit gate

- Identity (`===`) survives numeric widening and tagged promotion.
- Named properties and prototype identity survive every transition.
- GC traces companion and boxed contents before, during, and after promotion.
- No ordinary numeric array can contain a hole.
- No view, TypedArray, Arguments/content, or Lambda-only array is retagged.
- Failure of the physical compatibility proof stops the phase; no workaround
  is committed.

### P6 — Sparse, descriptor, length, and array-algorithm convergence

#### Objectives

- Complete the JR6 transition matrix.
- Make array algorithms consume semantic presence without repeated name work
  or prototype scans.
- Preserve exact descriptor and length failure behavior.

#### Work

1. Route gapped and distant writes through one threshold decision using the
   existing measured sparse policy.
2. Implement packed-to-holey and any-state-to-sparse prepare/commit paths.
3. Preserve `SparseArrayMap.sparse_indices` and `sparse_version` through
   mutation and iteration; centralize version updates.
4. Permit optional sparse-to-holey compaction only through the existing
   density threshold and never to numeric.
5. Make indexed descriptor/accessor overlays authoritative and ensure ordinary
   dense storage cannot bypass them.
6. Implement transactional length growth/shrink, including descending
   deletion and non-configurable failure semantics.
7. Convert array generics (`reverse`, shifts, splice-like algorithms, search,
   iteration, copying, sorting, and other P0 owners) to the shared
   presence/get/set/delete primitives.
8. Remove per-element numeric-key formatting/interning from semantic loops.
9. Reload state/version/companion after any callback or coercion in array
   algorithms.
10. Add transition-pair and algorithm tests across numeric, packed tagged,
    holey, sparse, and descriptor-overlay inputs.

#### Exit gate

- Every row in §5.7 has focused behavior and forced-GC coverage.
- Holes and `undefined` remain distinguishable through all algorithms.
- Length failure is transactional and returns the correct boolean/error.
- Array algorithms contain no private descriptor or sparse semantics.
- Numeric-key materialization happens only when an observable key is needed.

### P7 — Realm prototype epoch/clean guard

#### Objectives

- Replace unchanged-chain rescans with one realm-owned invalidation fact.
- Make fast hole handling correct across callbacks and realms.

#### Work

1. Add the clean/initialized bits to `JsIntrinsicState`; reuse the Array class
   mutation version as epoch.
2. Implement exact intrinsic-chain recognition by prototype identity.
3. Implement canonical-index scan/recompute for Array.prototype and
   Object.prototype own properties/descriptors.
4. Move epoch notification into successful semantic mutation commit points and
   the existing prototype-link mutation point.
5. Ensure Object.prototype indexed changes bump the Array epoch.
6. Replace repeated numeric-prototype scans in array algorithms with guarded
   snapshots and generic fallback.
7. Add explicit invalidation/reload after every re-entry point identified in
   P6.
8. Test deletion of the last prototype index returning the chain to clean.
9. Test two realms, foreign arrays, custom prototypes, Proxy links, and
   prototype mutation during callback execution.
10. Delete old repeated scan helpers and redundant version counters.

#### Exit gate

- One epoch source and one clean fact exist per realm.
- No caller borrows the active realm's guard for a foreign receiver.
- Every absent-slot fast path proves exact chain + clean + matching epoch.
- Re-entry tests observe mutations immediately.
- Repeated prototype numeric-scan helper references are zero.

### P8 — MIR Direct, registry, host migration, IC containment, and API deletion

#### Objectives

- Move every remaining caller to the final ABI.
- Keep caches outside semantics.
- Delete the legacy 54-entry-point era.

#### Work

1. Update MIR static named access to pass linked NameId lanes directly.
2. Update known numeric access to pass index lanes directly.
3. Update computed access to execute `ToPropertyKey` once, root the observable
   result through the call, classify once, and pass the same lane/core ABI.
4. Update assignment and delete lowering to convert boolean failure according
   to strict/sloppy source policy outside the core.
5. Update method lowering without disturbing Tune4's `Get -> argument
   evaluation -> Call` ordering.
6. Update Object/Reflect builtins, internal array/object algorithms, module and
   Node bridges, DOM/Radiant, and Jube hosts in ledger-sized batches.
7. Update `sys_func_registry.c` to expose the eight semantic operations and
   required conversion/materialization helpers with correct effect metadata.
8. Convert existing `JsLoadIC`/`JsStoreIC` routes into outer adapters that may
   hit only guarded ordinary own data slots. Accessors, exotics, descriptor
   overlays, uncertain prototypes, or state mismatch miss directly to core.
9. Ensure a cache miss performs no getter, trap, conversion, allocation, or
   mutation before the core.
10. Do not add `FeedbackSlot*` to any core signature; JR8 owns replacement of
    the transitional IC structures.
11. Delete, with last callers, old get/set/access/name-id/index/strict/delete/
    has/descriptor/own-name helper families and public
    `js_map_get_fast{,_ext}` semantic exposure.
12. Internalize the one shape-slot primitive that survives; it cannot be
    registered or called as an alternate semantic API.
13. Run the census after each caller batch to prevent an unowned compatibility
    island.

#### Exit gate

- Exactly eight public semantic property symbols remain.
- Static MIR paths contain no runtime string conversion/interning.
- Computed MIR paths perform `ToPropertyKey` exactly once.
- IC adapters contain no accessor/exotic/prototype/descriptor semantics and
  the core has no IC/feedback parameter.
- All legacy semantic wrappers in the deletion ledger are absent.
- C2MIR has no Tune5 diff.

### P9 — Structural ratchets, documentation, release, and handoff

#### Objectives

- Make architectural convergence mechanically durable.
- Close every deletion and evidence obligation.
- Hand JR4 and JR8 one stable seam each.

#### Work

1. Turn final census targets in §13 into build/test ratchets.
2. Run debug, forced-GC, release, Test262, DOM/layout, and benchmark gates.
3. Compare P9 LOC to the clean P0 anchor across all counted production files.
4. Inspect release symbols/disassembly or preprocessed/object evidence to prove
   the ordinary TypeMap plausibility predicate and recovery branch are absent.
5. Update:
   - `vibe/jube/JS_Runtime_Redesign.md` — mark JR6 implemented and record
     evidence;
   - `doc/dev/js/JS_06_Objects_Properties_Prototypes.md` — final property,
     elements, descriptors, keys, and prototype guard;
   - `doc/dev/js/JS_04_MIR_Lowering.md` — lane lowering and outcome policy;
   - `doc/dev/js/JS_12_TypedArrays.md` — explicit exclusion and classifier
     boundary;
   - `doc/dev/js/JS_15_Performance.md` — cache boundary and measurements;
   - `doc/dev/js/JS_16_Testing.md` — structural and transition gates;
   - the relevant `doc/dev/lambda/LR_*` memory/value document for the narrow
     retag and companion-tracing support.
6. If implementation revealed a change to a formal ruling rather than an
   application of the cited rulings, stop and revise the formal D# in place
   with a `v2` suffix and semver bump, plus the JR6 doc in the same commit.
7. Record explicit handoff contracts:
   - JR4 replaces only the implementation of
     `js_property_exotic_adapter` with class ops;
   - JR8 replaces only outer cache adapters with feedback slots;
   - neither changes the eight semantic operations.

#### Exit gate

- All behavior, GC, structural, LOC, and release-performance gates pass.
- Documentation describes the implementation, not the retired paths.
- No compatibility adapter lacks a named future deletion owner.
- JR6 can be marked implemented without exceptions hidden in prose.

---

## 10. File ownership map

| File / area | Tune5 ownership |
|---|---|
| `lambda/core/name_identity.h` | Reuse `NameId`; only add lane-adjacent assertions elsewhere unless a truly shared primitive belongs here. |
| `lambda/lambda.h` | Elements-state mask/enum/accessors and layout assertions; preserve the eight-byte `Container` header and `reserved_state`. |
| `lambda/js/js_props.h` | Final eight public declarations and internal descriptor/ordinary-tier contracts. |
| `lambda/js/js_props.cpp` | Authoritative ordinary semantic pipeline, descriptor rules, prototype traversal, and one exotic-adapter call seam. |
| `lambda/js/js_elements.h/.cpp` | Single owner of ordinary elements state/storage/transitions if extraction is justified; no public semantic property API. |
| `lambda/js/js_property_attrs.cpp` | Reuse/move descriptor validation and TypeMap transition primitives; delete duplicate semantic wrappers. |
| `lambda/js/js_runtime.h` | Remove legacy property/array semantic exports; retain only unrelated runtime API. |
| `lambda/js/js_runtime.cpp` | Migrate/delete get/set/access/array/exotic/prototype implementations and retain only delegated non-property runtime work. |
| `lambda/js/js_runtime_internal.hpp` | Remove ambient Proxy receiver and expose narrow internal classification/materialization contracts. |
| `lambda/js/js_runtime_state.hpp` | Remove ambient accessor/receiver state; add realm clean fact beside existing mutation versions. |
| `lambda/js/js_state_guards.h` | Delete property-specific ambient RAII guards; retain unrelated dynamic-extent guards. |
| `lambda/js/js_globals.cpp` | Object/Reflect policy wrappers migrate to eight core operations; delete separate has/delete/own-key/exotic paths. |
| `lambda/js/js_class.h` and intrinsic state code | Reuse class/mutation identity; route transitional classes into the one adapter. |
| `lambda/js/js_typed_array.cpp` | Retain distinct canonical-numeric semantics and enter via exotic adapter. |
| `lambda/js/js_mir*.cpp`, expression/statement lowering | Emit lanes, computed conversion, explicit receiver, and caller-owned failure policy. |
| `lambda/runtime/sys_func_registry.c` | Register the final MIR ABI/effects and remove legacy rows. |
| `lambda/runtime/collection_runtime.cpp` | Promote existing companion-tail helper rather than duplicate it. |
| `lambda/runtime/gc/gc_heap.c` | Trace admitted ordinary numeric-array companion tail; no JS semantic policy. |
| Lambda memory/heap owner | Narrow, audited GC-visible container retag primitive. |
| `build_lambda_config.json` | Register a new JS translation unit if extraction is accepted; regenerate with `make`. |
| `modules/`, DOM/Radiant, Node/Jube hosts | Migrate caller/host-protocol adapters without adding semantic families. |
| `test/js`, `test/mir/js`, JS GTests | Behavior, ABI, representation, GC, realm, and structural coverage. |
| `utils/js_property_census.py` | Standing architecture census and deletion ratchets. |

No new file is mandatory merely for visual neatness. If the P0 ownership scan
shows an existing coherent owner, extend and simplify it. If `js_elements`
is created, old helpers move there and disappear from their former files in
the same phase.

---

## 11. Operation-by-operation migration ledger

| Operation | Existing major owners | Final ordinary owner | Exotic owner | Caller policy owner |
|---|---|---|---|---|
| Get | runtime get/access/NameId/IC/array helpers, Proxy/host branches | `js_props.cpp` + element own tier | one adapter | source/Object/Reflect simply consume value/error |
| Set | runtime set/strict/set-v/NameId/IC/array helpers | `js_props.cpp` + element mutation tier | one adapter | assignment strictness and Reflect boolean |
| DefineOwn | property attrs plus Object/Reflect and accessor-suppression paths | descriptor engine entered by `js_props.cpp` | one adapter | Object throw vs Reflect boolean |
| Delete | runtime/globals strict/sloppy/map/array/exotic variants | `js_props.cpp` + element deletion | one adapter | delete expression/Object/Reflect policy |
| HasProperty | `js_in`, prototype helpers, exotic has | `js_props.cpp` prototype loop | one adapter | boolean/error consumer |
| HasOwn | `hasOwnProperty`, Object.hasOwn, raw descriptor probes | `js_props.cpp` own tier | one adapter | boolean/error consumer |
| GetOwnPropertyDescriptor | globals/property attrs/exotic descriptor helpers | descriptor synthesis in `js_props.cpp` | one adapter | descriptor-object materialization |
| OwnKeys | own names/symbols/enumeration/exotic helpers | ordered element+shape merge | one adapter | Object APIs filter strings/symbols after core |

`Object.keys`, `Object.getOwnPropertyNames`, and
`Object.getOwnPropertySymbols` are consumers of `OwnKeys` plus descriptor or
key-kind filtering, not additional semantic operations. Enumeration iterators
may use an internal ordered snapshot builder owned by `OwnKeys`, but cannot
define a competing order.

---

## 12. Test strategy

### 12.1 Focused semantic suites

Prefer extending existing files after P0's inventory. Where gaps require new
fixtures, use focused names such as:

- `test/js/props/tune5_property_core.js` + `.txt`;
- `test/js/props/tune5_property_receiver.js` + `.txt`;
- `test/js/props/tune5_property_exotics.js` + `.txt`;
- `test/js/props/tune5_elements_transitions.js` + `.txt`;
- `test/js/props/tune5_array_descriptors.js` + `.txt`;
- `test/js/props/tune5_array_proto_epoch.js` + `.txt`;
- `test/mir/js/tune5_property_lanes.js` + `.mir-check`.

Any new Lambda `*.ls` unit fixture must also add its expected `*.txt` file per
repository rule 8.

### 12.2 Required semantic matrix

For each operation, cover:

- own data, own accessor, inherited data, inherited accessor, absent;
- string, Symbol, ordinary index, index-looking non-index spelling;
- extensible/non-extensible, configurable/non-configurable,
  writable/non-writable;
- ordinary object, ordinary array, Proxy, TypedArray, Arguments, String exotic,
  DOM/host where supported;
- same receiver, distinct receiver, primitive receiver, cross-realm receiver;
- success, `false`, and ERROR separately;
- callback/trap mutation of target, receiver, descriptor, and prototype.

### 12.3 Key-boundary table

At minimum test:

```text
"0", "1", "4294967294"          -> ordinary index lane
"4294967295", "4294967296"      -> NameId lane
"00", "01", "+1", "-1", "-0" -> NameId lane
"1.0", "1e0", " 1", "1 "      -> NameId lane
"NaN", "Infinity", "-Infinity" -> NameId lane
Symbol and private names           -> NameId lane, never string identity
```

Repeat relevant values at a TypedArray boundary to prove the exotic
CanonicalNumericIndexString behavior remains distinct.

### 12.4 Elements transition matrix

For every source state, exercise:

- overwrite, append, nearby gap, distant gap, delete present, delete absent;
- `undefined`, object, Symbol, int overflow, float, NaN, infinities;
- named property before and after transition;
- indexed default/non-default descriptor and accessor;
- length grow, successful shrink, failed shrink;
- sparse compaction threshold boundary;
- `OwnKeys`, descriptor lookup, iteration, reverse/splice/sort/callback methods;
- identity and prototype preservation.

Tests must assert behavior first. A debug-only native inspection test may
assert physical kind/state, but no release JS API exposes the implementation.

### 12.5 GC and ownership matrix

Run with forced minor/full collection at:

- computed key after `ToPropertyKey` and before lookup;
- getter/setter/Proxy trap with distinct receiver;
- descriptor creation and application;
- numeric buffer allocation before promotion commit;
- immediately after GC/visible tag retag;
- companion creation on numeric arrays;
- numeric grow/widen with companion present;
- dense-to-sparse and sparse-to-holey conversion;
- array callback that mutates elements kind/prototype;
- cross-realm teardown with surviving rooted receiver/key.

Verify no old backing, companion, descriptor field, or boxed value is retained
by conservative scanning; only precise roots/edges are accepted under
**D5.3**.

### 12.6 Prototype guard matrix

- clean intrinsic chain and hole;
- numeric data/accessor on Array.prototype;
- numeric data/accessor on Object.prototype;
- add, redefine, and delete last indexed property;
- replace/restore each prototype link;
- custom prototype and Proxy in chain;
- array from realm A accessed/called in realm B;
- callback mutates prototype after an iteration snapshot;
- own descriptor overlay while realm chain remains clean.

### 12.7 TypeMap invariant tests

- Existing closure/block-shadow/lib-marked regression stays green.
- Debug-only native test reaches the category-1 ordinary tier with a corrupted
  non-null TypeMap and observes assertion termination.
- Release build/source/object scan proves the plausibility predicate call and
  recovery branch do not exist in the ordinary tier.
- Approved cache validators still treat stale cache metadata as a cache miss,
  not as object corruption.
- Wrong-kind transitional objects dispatch before Map.type interpretation.

### 12.8 Test262 groups

Run at least the relevant groups for:

- property accessors, assignment, delete, and `in`;
- Object descriptor/keys/name/symbol/seal/freeze APIs;
- Reflect get/set/define/delete/has/ownKeys;
- Array length, methods, holes, species, and prototype effects;
- Proxy get/set/defineProperty/deleteProperty/has/getOwnPropertyDescriptor/
  ownKeys invariants;
- TypedArray integer-indexed exotic behavior;
- Arguments mapped and unmapped objects;
- classes/super where receiver-sensitive property operations are exercised.

Record any pre-existing exclusions at P0. Tune5 may not grow the exclusion
list without a documented root cause and explicit review.

---

## 13. Structural ratchets

At P9, `utils/js_property_census.py --check` enforces:

| Ratchet | Final target |
|---|---:|
| Public semantic property operation definitions | 8 |
| Public semantic property operation registry families | 8 |
| Transitional exotic adapters | 1 |
| `js_proxy_receiver` references | 0 |
| `ScopedProxyReceiver` references | 0 |
| `js_skip_accessor_dispatch` references | 0 |
| Ambient strict-mode reads inside semantic property core | 0 |
| Core signatures containing `JsLoadIC`, `JsStoreIC`, or `FeedbackSlot` | 0 |
| IC paths that invoke accessors/exotics or perform prototype semantics | 0 |
| Legacy public `js_property_get/set/access*` semantic definitions | 0 |
| Legacy public `js_array_get/set*` semantic definitions | 0 |
| Public `js_map_get_fast{,_ext}` semantic exports | 0 |
| Repeated prototype numeric-scan helpers | 0 |
| Ordinary array-index classifiers | 1 |
| Per-element numeric-key formatting inside semantic array loops | 0 |
| Internal ordinary TypeMap plausibility recovery branches | 0 |
| Release ordinary-tier plausibility predicate calls | 0 |
| Published ordinary arrays with `JS_ELEMENTS_NONE` | 0 in debug validation |
| Automatic tagged-to-numeric respecialization sites | 0 |
| JS-private GC-header writes | 0 |
| Side tables/forwarders preserving array identity | 0 |

The census also emits non-zero allowlists for true external/cache validators,
host property protocol callbacks, and excluded array constructors. Each entry
must have a named owner and explanation; an allowlist is not a wildcard.

### 13.1 Source-size accounting

Record at P0 and P9:

- total `lambda/js` production C/C++ LOC;
- Tune5-touched Lambda runtime production LOC;
- property-surface LOC (`js_props`, property sections of runtime/globals,
  elements owner, registry rows);
- number of public semantic definitions and wrappers;
- moved versus newly written/deleted lines.

Hard gates:

1. Aggregate production delta is negative. Target: at least 750 lines removed.
2. Final `lambda/js` production C/C++ LOC is at least 2,000 lines below the
   clean post-Tune4 P0 baseline:
   `final_lambda_js_loc <= P0_lambda_js_loc - 2000`.

Both gates must pass. If new elements/GC support makes the aggregate positive,
simplify or delete the remaining predecessors before landing; do not exempt
the support because it resides outside `lambda/js`. Runtime-support deletion
also cannot offset a failure to remove 2,000 lines from `lambda/js` itself.

---

## 14. Validation commands and batch gates

Use the repository's actual targets confirmed at P0. The expected core gate is:

```bash
make build
make build-test
./test/test_js_gtest.exe
make test-lambda-baseline
make test262-baseline
make test-mir-gc-stress
make test-radiant-baseline
make lint
```

If target names differ, record the discovered equivalents; do not silently
skip a suite. Focused commands run after every phase, broad baseline and GC
gates after P2/P3 and every elements phase, and the full set at P9.

Release evidence:

```bash
make release
./utils/js_property_census.py --check --configuration release
./utils/count_loc.sh
```

Performance tests use only the release executable. Capture repeated samples
for object get/set, inherited/accessor get, array numeric/tagged/holey/sparse
loops, descriptor/OwnKeys, callback array methods, and existing representative
programs such as sieve, n-body, and GC/property stress where available.

No new benchmark becomes an acceptance claim until its inputs, warmup,
iterations, aggregation, and machine state are recorded.

---

## 15. Performance acceptance

JR6 is primarily convergence and semantic simplification; JR8 owns the final
feedback-vector optimization. Tune5 nevertheless must prove:

1. no statistically meaningful regression in the aggregate release JS
   benchmark suite;
2. static NameId and known-index access does not allocate or intern;
3. array semantic loops do not format/intern a key per element;
4. clean intrinsic prototype chains are not rescanned per absent slot;
5. ordinary own-data IC hits remain at least behaviorally and materially no
   worse than the P0 route;
6. numeric packed arrays show the expected memory/throughput direction without
   penalizing generic tagged arrays through repeated kind conversions;
7. promotion cost is paid once because transitions are monotone;
8. descriptor/exotic/cache misses do not perform observable work twice.

Report medians and dispersion across repeated runs. A noisy result is rerun;
it is not labeled improvement. Any regression attributable to a correct but
temporary missing optimization is either fixed inside the adopted design or
explicitly reviewed—JR8 is not a blanket waiver.

---

## 16. Risks and stop conditions

| Risk | Required response |
|---|---|
| Array/ArrayNum layout or GC allocation sizes are incompatible | Stop P5 and return to design review. No replacement object, wrapper, or forwarding side table. |
| GC cannot safely retag a live managed container | Stop P5; do not update only visible TypeId and hope tracing agrees. |
| Numeric companion tail aliases TypedArray/view metadata | Refine explicit admission/classification or stop; never infer from TypeId alone. |
| A legacy helper mixes multiple semantic operations | Split by calling the new core, migrate callers, and delete it; do not bless it as a ninth family. |
| Proxy requires the original computed key Item | Preserve it as a rooted observable materialization; do not restore string/pointer identity. |
| Existing IC hit embeds accessor/prototype semantics | Narrow it to ordinary own data or force miss; JR8 owns richer cache policy. |
| TypeMap plausibility catches a wrong-kind object | Fix dispatch ordering/root cause before removing recovery; do not retain release fallback. |
| Release crashes after guard removal | Treat as invariant violation and find the producer; do not convert it to miss/undefined. |
| Array callback mutates representation mid-loop | Reload the full loop snapshot after re-entry; do not pin stale raw pointers. |
| Cross-realm receiver cannot identify an intrinsic chain | Use generic traversal; never borrow active-realm clean state. |
| Sparse threshold changes observable behavior | Fix the algorithm: thresholds may choose storage only. |
| Source delta grows | Finish the deletion ledger or simplify ownership before landing. Moving code does not count. |
| Formal ruling must change | Update the formal D# in place with `v2`, bump doc semver, and update JR6 in the same commit before implementation continues. |
| A fix appears to require vendor or C2MIR edits | Stop and request direction; both are outside Tune5 authority. |

---

## 17. Planned commit series

Keep commits reviewable and green. A likely sequence is:

1. `test(js): lock property receiver key and outcome behavior`
2. `tool(js): add Tune5 property census and baseline manifest`
3. `refactor(js): define property lane and eight-operation ABI`
4. `fix(js): enforce TypeMap invariant only in debug ordinary paths`
5. `refactor(js): converge ordinary property operations`
6. `refactor(js): remove ambient property receiver and accessor state`
7. `refactor(js): consolidate transitional exotic property adapter`
8. `refactor(js): add ordinary array elements-state contract`
9. `refactor(js): centralize array index and descriptor storage tiers`
10. `refactor(runtime): support traced companions and container retagging`
11. `feat(js): admit packed numeric arrays and atomic tagged promotion`
12. `refactor(js): converge sparse length and array algorithms`
13. `perf(js): add realm array-prototype epoch guard`
14. `refactor(mir): lower all property operations through final ABI`
15. `refactor(js): contain legacy ICs outside property semantics`
16. `refactor(js): delete legacy property entry points`
17. `test(js): ratchet Tune5 structure representation and GC behavior`
18. `docs(js): record implemented JR6 property architecture`

Commit boundaries may shift to keep code compiling, but behavior changes and
deletion of their predecessor belong in the same logical batch. No commit may
introduce an unbounded compatibility layer.

---

## 18. Completion checklist

### Design and ABI

- [ ] Tune4 handoff is clean and recorded.
- [ ] Cited formal rulings are still sufficient; any required revision landed
      formally and in JR6 together.
- [ ] `JsPropertyLane` encoding and descriptor/outcome ABI are frozen.
- [ ] Exactly eight public semantic operation symbols remain.
- [ ] Static and computed lowering share the same lane/core contract.

### Ordinary and exotic semantics

- [ ] Receiver is explicit through prototype/accessor/Proxy paths.
- [ ] Strictness and Object/Reflect policy live outside core.
- [ ] Ambient Proxy receiver and accessor-suppression state are deleted.
- [ ] One ordinary shape/slot tier owns ordinary behavior.
- [ ] One transitional exotic adapter owns all current exotic operations.
- [ ] Existing ICs are outer guarded probes only.

### Array representation

- [ ] Reserved array-flag bits encode the adopted states without changing
      `reserved_state` or other flag meanings.
- [ ] Every ordinary JS array constructor stamps a non-zero state.
- [ ] TypedArray, Arguments/content, views, and Lambda-only arrays are excluded.
- [ ] Ordinary numeric admission is proof-based.
- [ ] GC and visible tags change together during promotion.
- [ ] Companion Maps survive and trace across numeric/tagged transitions.
- [ ] Holes force tagged storage; `undefined` remains present.
- [ ] Sparse, descriptor, length, and key-order rules pass.
- [ ] No automatic numeric respecialization or side bitmap exists.

### Guards and ownership

- [ ] Realm Array epoch reuses existing mutation versions.
- [ ] Clean facts are receiver-realm specific and revalidated after re-entry.
- [ ] Repeated prototype scans are deleted.
- [ ] Ordinary TypeMap plausibility is a debug C assertion only.
- [ ] Release contains no ordinary TypeMap predicate/recovery route.
- [ ] All transition/intermediate values are precisely rooted.
- [ ] No conservative-stack, hidden-root, lock, or atomic workaround exists.

### Evidence and deletion

- [ ] Focused property, array, exotic, realm, TypeMap, MIR, and GC tests pass.
- [ ] Relevant Test262, Lambda, DOM/layout, and broad JS gates pass.
- [ ] Release-only performance results are recorded and non-regressing.
- [ ] Structural census meets every P9 ratchet.
- [ ] Aggregate production LOC is net negative; target reduction is reported.
- [ ] Final `lambda/js` production C/C++ LOC is at least 2,000 lines below the
      clean post-Tune4 P0 baseline.
- [ ] Old helpers, switches, ambient state, and public fast-map APIs are gone.
- [ ] Implementation docs describe only the surviving architecture.
- [ ] JR4 and JR8 handoff seams are recorded.

Tune5 is not complete when the eight new functions merely coexist with old
routes. It is complete when all property behavior is owned by those eight
families, all ordinary array storage is governed by one explicit state
machine, the TypeMap invariant has no release fallback, and the predecessor
mechanisms have been removed.
