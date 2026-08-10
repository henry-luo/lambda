# JS Tune1 Proposal: Runtime Helper Cost Reduction

**Date**: 2026-08-07  **Status**: DISPOSED — absorbed by the redesign line.
P1a (lazy promise rooting) is implemented via `JS_Tune1_Runtime.md` (E1).
P5 (catalog tightening) did not land there and is carried by
`JS_Tune2_Exception.md`; P1's full fix is superseded by
`JS_Runtime_Redesign.md` JR7 (VMap promise), P2 by JR2/JR6, P3 by JR4/JR6,
P4 by JR5/R5. Do not implement P2–P4 from this doc — their targets are
covered by redesign phases with retirement lists.
**Tree anchor**: master `b9b30f4ac`
**Evidence**: `vibe/jube/JS_Profiling_Helpers.md` (release_profile, 261-script
`js-test-batch`, matched time/count runs)

Primary sources:

- Promise allocation & rooting: `lambda/js/js_runtime.cpp:31904` (register-roots
  loop), `lambda/js/js_runtime.cpp:31927` (`js_alloc_promise`),
  `lambda/runtime/gc/gc_heap.c` (`gc_register_root_range`)
- Property-key canonicalization: `lambda/js/js_property_attrs.cpp:67`
  (`js_find_shape_entry`), `lambda/js/js_runtime.cpp:3957` (`js_map_get_fast`)
- Prototype chain: `lambda/js/js_runtime.cpp:29653` (`js_get_prototype`),
  `js_get_implicit_proto`, `js_get_prototype_of` (`js_globals.cpp:6716`)
- Class instantiation: `lambda/js/js_runtime.cpp:2434`
  (`js_new_from_class_object`)
- Array generics: `js_array_generic_reverse` / `js_array_generic_sort`
  (`lambda/js/js_runtime.cpp:26215` region)

## 0. Goal and shape of the problem

The profiling pass established (all numbers: % of 29.1s working CPU on the
release_profile batch):

| Fact | Number |
|---|---:|
| Runtime helpers, total | **43.5%** |
| JIT-generated code self time | 0.6% |
| ABI trio (`js_check_exception` + scalar-home pair), 50.4% of all calls | **0.22%** |
| GC root-range registration, 100% from `js_alloc_promise` | **7.1%** |
| Name/shape lookup infrastructure (tail-called bucket) | **8.3%** |
| `js_get_prototype_of` (0% JIT-called) | **7.0%** |
| `js_new_from_class_object` | **5.6%** |

Consequences that shape this proposal:

- **Helper bodies are the only place with headroom.** Generated code is glue
  (0.6% self), and the high-frequency ABI helpers are already ~1–3ns/call.
  Reducing *call counts* of cheap helpers is not a lever; reducing the *work
  inside* the four expensive paths above is. Together they are ~28% of working
  CPU — two thirds of all helper time.
- **The heaviest paths are C-to-C.** `js_get_prototype_of` is never called from
  JIT code; the root-registration storm is reached through `js_alloc_promise`.
  No lowering/IC change can touch them; these are runtime-side fixes, distinct
  from the Tune10–Tune12 IC line.
- Explicit non-goals: inlining the ABI trio (ceiling ≈0.2%, measured), compile
  share (workload artifact of 261 short scripts), and any change to the D5.2
  scalar-home protocol semantics.

Phases are ordered by measured value and are independently landable.

## P1 — Promise GC-root registration storm (7.1% → target <0.5%)

### Root cause (confirmed in code, not inferred)

`js_alloc_promise` calls `js_promise_register_roots_once`
(`js_runtime.cpp:31904`). Once per heap epoch, that function registers the
rootable fields of **the entire static promise table**, live or not:

```c
for (int i = 0; i < JS_MAX_PROMISES; i++) {        // JS_MAX_PROMISES = 8192
    heap_register_gc_root_range(&js_promises[i].result, 1);
    heap_register_gc_root_range(js_promises[i].on_fulfilled, 8);
    ... // 7 ranges per promise
}
```

7 × 8192 = **57,344 range registrations per epoch**. `gc_register_root_range`
(`gc_heap.c`) dedups by **linear scan** of all previously registered ranges
before appending, so a full registration pass costs ~57,344²/2 ≈ **1.6 billion
pointer comparisons**. The batch host recycles the heap per script ("Batch heap
replacement discards its registry", `js_runtime.cpp:31921`), so every script
that touches a promise — which includes every `async`-using library — pays the
full storm again. The profile attributes 100% of
`heap_register_gc_root_range`'s 7.1% to exactly this chain.

Secondary damage: the registry then holds 57k ranges covering ~287k words
(35 rootable words × 8192 slots), which every subsequent mark phase must walk
even when three promises exist. D5.3.1's principle — root cost proportional to
**live** state, never to capacity — is violated on both the registration and
the scan side.

### Fix, in two independent steps

**P1a — lazy per-slot registration (small, local).** `js_promise_count` grows
monotonically within an epoch and slots are never reused mid-epoch. Register a
slot's 7 ranges at its **first allocation** in the epoch (high-water-mark
advance), not for all 8192 up front:

```c
// register roots for slots [registered_high_water, js_promise_count]
// before the new slot's fields can hold heap pointers
```

Registration volume becomes proportional to live promises (typical script:
dozens, not 8192×7), and the mark-phase walk shrinks identically. The ordering
invariant to keep: ranges must be registered **before** the first heap pointer
is stored into the slot — `js_alloc_promise` initializes fields to
`ItemNull`/zero first, so registering at slot-allocation time (before returning
`p`) preserves it.

**P1b — registry hardening (fixes the quadratic term for every caller).**
`gc_register_root_range`'s linear dedup is O(N) per call for all clients —
`concurrency.cpp` registers per-task mailbox/frame ranges through the same
path. Either key the dedup with a small open-addressing table (base → index),
or add a `_nodedup` entry point for callers that already guarantee
once-per-epoch (the promise path and `js_root_range_ensure_registered` both
qualify). P1a alone removes the measured cost; P1b removes the trap.

**P1c (follow-on, measure first) — promise wrapper weight.** Each surfaced
promise also builds a wrapper map (`js_promise_to_item`: fresh object +
`__promise_idx` property + class stamp + proto rewire + `constructor`
property). If post-P1a profiles still show promise machinery in the top ten,
give the wrapper a cached constructor shape (P4 machinery) rather than growing
it property-by-property.

### Acceptance

- `heap_register_gc_root_range` self ≤ 0.5% of working samples on the §6
  reproduction run; no change in any promise/async test; 327-test suite green.
- A GC-stress pass (`test_mir_gc_stress_gtest`) to guard the
  register-before-store invariant.

Expected recovery: **~6.5 points** of working CPU on this workload.

## P2 — Property-key canonicalization and array generics (8.3% bucket)

### Root cause

The tail-called lookup bucket (`name_pool_create_strview` 838 samples,
`well_known_name_id` 505, `js_map_get_fast` 347, `hashmap_sip` 251,
`typemap_hash_lookup_*` 286, `js_builtin_catalog_find` 198) is one mechanism
seen from several entrances. The sampled chain:

```
JIT → js_array_method_direct_impl → js_array_generic_reverse
    → js_has_property → js_has_own_property → js_own_shape_slot_status
    → js_find_shape_entry → heap_create_name → name_pool_create_strview
    → well_known_name_id / hashmap_sip
```

`js_find_shape_entry` (`js_property_attrs.cpp:67`) **interns its key on every
raw `(chars,len)` probe**: "canonicalize before probing so they cannot bypass
runtime PropertyKeyRef equality". Correct at an API boundary — but the array
generics sit above it synthesizing an index name per element and paying the
intern (SipHash + pool probe) per `HasProperty(k)`, then walking the prototype
chain per element on top (the P3 driver).

### Fix, ordered by leverage

**P2a — dense fast path for array generic methods.** For a receiver that is a
dense JS array (element count == length, no holes, no indexed own properties on
the props map) and with no indexed properties on `Array.prototype` (the guard
the Tune12 array-IC work already needs), `HasProperty(k)` for `k < length` is
identically `true` — the spec's per-element existence probe exists only to
observe holes and prototype-indexed properties. `js_array_generic_reverse`,
`_sort`, `_indexOf`, `_lastIndexOf`, `_includes`, `_join` can then operate on
dense storage directly and fall back to the generic walk on the first
irregularity. This deletes the per-element intern *and* the per-element
prototype walk at the source. Precedent and guard fixtures already exist
(`Js54P5ArrayProtoOnTypedArray`, `lib_marked.js` corrupt-map hardening in
`js_map_get_fast`).

**P2b — PropertyKeyRef plumbing on hot internal callers.**
`js_find_shape_entry_key` (interned-key probe, no canonicalization) already
exists. Internal callers that hold a `String*`/interned key — or that probe the
same name repeatedly (`js_has_own_property` behind `in`, descriptor reads,
`js_get_implicit_proto`'s `"__instance_proto__"` probe) — should call the
`_key` form; the canonicalizing wrapper remains the API boundary for genuinely
raw bytes. This is enforcement of D4.6 (one name identity, temporal canonical)
rather than a new mechanism; DO16's dynamic-intern growth bound is the
watch-item — fewer per-lookup interns also means less pool churn.

**P2c — integer keys never become strings.** Any `HasProperty`/get with an
integer key on an array-kind receiver should test the element store and (only
on miss) the props map, without materializing a name string. Audit the
`js_has_property` integer path; the profile says at least the generic-method
entrance misses this today.

### Acceptance

- Tail-called lookup bucket ≤ 3% of working samples; `name_pool_create_strview`
  out of the top-20 tail symbols.
- `js262` array-suite subset green (holes, frozen arrays, `Array.prototype`
  with indexed properties, getters on indices — each must take the fallback).

Expected recovery: **4–6 points**, part of it shared with P3.

## P3 — Prototype chain resolution (7.0%)

### Root cause

`js_get_prototype_of` is pure C-side (0% JIT-called): the driver is property
*misses* — `HasProperty`/get walking chains — and each chain step re-resolves
`__proto__` through a shape probe (`js_proto_shape_entry` with the interned
key) plus, on absence, implicit-proto synthesis from the class stamp
(`js_get_implicit_proto`, including a raw string probe for
`"__instance_proto__"`).

P2a removes the dominant driver (array generics). What remains is legitimate
chain walking; make each step cheaper:

- **P3a — memoize the `__proto__` `ShapeEntry*` per TypeMap.** The entry
  position is a property of the (immutable) shape chain, so a one-word memo on
  `TypeMap` (or a side table keyed by TypeMap identity) turns the per-step hash
  probe into a load + slot read. Invalidation rides the existing shape-clone
  discipline (`js_typemap_clone_for_mutation` — mutation already produces a new
  TypeMap) and the `js_intrinsic_note_property_mutation` seam for the intrinsic
  tables.
- **P3b — constant-fold the implicit-class prototype.** For maps with no own
  `__proto__` slot, the prototype is a pure function of the stamped class byte;
  `js_get_intrinsic_prototype_for_class` should be (verify, then guarantee) a
  direct table load, and `js_get_implicit_proto`'s `"__instance_proto__"`
  probe should use the `_key` form per P2b.
- **P3c — measure before caching values.** A per-shape cache of the prototype
  *value* is unsound where `__proto__` is an own slot (shape shared, value
  per-instance); only the *entry* memo (P3a) is safe in general. If post-P2a/P3a
  profiles still show chain cost, the next step is a per-(TypeMap, class) miss
  cache with explicit invalidation — proposed only as KIV.

### Acceptance

`js_get_prototype_of` + `js_get_prototype` combined ≤ 2.5% of working samples;
proto-mutation fixtures (`__proto__` reassignment, `Object.setPrototypeOf`,
deleted proto slot) green.

Expected recovery: **2–4 points** beyond P2a's share.

## P4 — Class instantiation `js_new_from_class_object` (5.6%)

7,695 JIT calls; C-side cost ≈1.6s. The attribution excludes nested JIT
(constructor bodies) and nested helpers, so this is genuinely per-`new`
runtime overhead — but its internal split is not yet measured, and 40% of its
sampled invocations arrive from C (`Reflect.construct`, subclass paths in
`js_globals.cpp`), so the per-call figure (~211µs against JIT calls) is an
upper bound.

Plan: instrument the branch split first (bound-function arg merge, pending
`new.target` setup, `js_apply_constructed_builtin_prototype`, error-path
formatting), then apply the one that dominates. The expected winner, consistent
with Tune12's P2 direction (constructor shape caching): give each class object
a **cached instance TypeMap** so `new C()` allocates at final shape instead of
growing the instance property-by-property through the store path — the profile
already shows `js_property_set` at 2.8% with a 49% JIT share, and instance
initialization is a plausible contributor to its C-side half.

Deliberately staged after P1–P3: it needs its own measurement pass, and P2b's
key plumbing reduces its property-definition cost on the way.

Expected recovery: **2–3 points** (low confidence until the split is measured).

## P5 — Exception-effect catalog tightening (hygiene, ≤0.11%)

32.6M `js_check_exception` calls are the OE-tracker residual: most catalog rows
default to `JIT_EXCEPTION_MAY_SET`, so `exc_track` re-enters `UNKNOWN` after
almost every call. At 1ns/call this is not a time lever (measured ceiling
≈0.1%); it *is* cheap correctness-adjacent metadata work — D7.4.3 already
requires accurate exception behavior per catalog row, and every row tightened
to `PRESERVES` also removes poll *sites* (smaller MIR volume, marginally faster
compile). Do it opportunistically, audit-style, with the D6.1.3 polarity rule:
a row is `PRESERVES` only when mechanically verified, never by absence of
evidence.

## Measurement protocol (all phases)

Baseline and reruns use the §6 reproduction from the profiling doc, unchanged:
release_profile build, matched pair of runs over `temp/js_batch_manifest.txt`
(1ms `sample` uninstrumented + `JS_EXEC_PROFILE=1` counted), analyzed with
`temp/parse_sample2.py` + `temp/symbol_origin.tsv`. Every phase lands with:

- before/after bucket table and the phase's named symbols,
- 327-test `test_js_gtest` green + targeted fixtures listed per phase,
- one wall-clock number for the uninstrumented batch (baseline: **34.4s**).

Combined expectation if P1–P3 land: helper share 43.5% → **~30%**, batch wall
time −10–15%. Claims beyond that wait for the post-P1 profile; per Tune12
practice, re-rank after each phase rather than trusting this table.

## Risks

- **P1 rooting**: a promise slot whose ranges are registered after a heap
  pointer lands in it is a lost root — the register-before-store ordering must
  be asserted (debug tripwire: on epoch change, verify high-water registration
  covers `js_promise_count` before any resolve/then mutation). GC-stress gate
  required.
- **P2a semantics**: the dense fast path must bail on holes, indexed own
  properties, indexed `Array.prototype`/`Object.prototype` properties, frozen
  elements, and non-plain receivers. Every bail condition needs a fixture; the
  fallback is the current code, so wrong-bail is a perf bug, not a correctness
  bug, *only if* the guard is conservative.
- **P2b identity**: two probe forms must not become two identities — the `_key`
  form is a fast path over the same canonical PropertyKeyRef, never a parallel
  equality (D4.6).
- **P3a invalidation**: the memo is per-TypeMap and shapes are
  clone-on-mutate; the risk is a stale memo on a TypeMap that was mutated in
  place somewhere outside the clone discipline — the audit in P3a is finding
  any such site, not the memo itself.
- **P4** touches constructor semantics (`new.target`, bound constructors,
  derived classes); it stays behind its own measurement pass and narrow first
  cut, mirroring how Tune11/12 staged IC work.

## Open questions

1. P1a vs P1b ordering: is there any *other* caller registering ranges at
   scale (grep says `concurrency.cpp` task mailboxes — bounded by task count)?
   If not, P1b can wait indefinitely.
2. Should `JS_MAX_PROMISES` (8192, static) become growable while P1 is open
   anyway? The static table is why capacity-proportional rooting existed at
   all; a chunked growable table with per-chunk registration would fix both the
   cap and the storm — larger change, same invariant.
3. For P2a, is the "no indexed properties on `Array.prototype`" guard already
   maintained as a global invalidation flag by the Tune12 array-IC work, or
   does it need to be introduced (one bool + write-barrier on
   `Array.prototype` indexed defines)?
4. For P3a, is TypeMap identity stable enough across the shape-clone paths that
   a side-table keyed by pointer is safe, or must the memo live in TypeMap
   itself to travel with clones?
5. Does the promise wrapper (`js_promise_to_item`) need to exist per promise at
   all, or can the wrapper be created lazily only when a promise escapes to a
   property/identity-observing context? (Escape analysis is out of scope; a
   cheap "created on first `.then` from JS" check may not be.)
