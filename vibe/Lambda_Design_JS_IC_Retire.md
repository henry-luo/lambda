# Retiring the JS Callsite Inline Cache — Shared Runtime Property Path

**Status: IMPLEMENTED** (2026-08-15). Scope: LambdaJS named-property
load/store ICs and compiler-side indexed specializations. Lambda-lane dispatch
is governed by D8.4.1v2. The implementation retired the per-callsite IC and
the duplicated MIR array/typed-array lanes, routing named and indexed accesses
through the shared runtime reference/property kernels. The proposed
shape-resident probe and Tier A compile-predicted slot layer remain future
options, not part of this change. Companion history: `vibe/jube/JS_Tune_History.md`.

### Implementation result

The final design is intentionally smaller than the proposal below: one
immutable MIR lowering path constructs a Reference and calls the runtime
property operation; the runtime owns array, typed-array, prototype, accessor,
descriptor, and strictness semantics. This preserves D1.3's one-core-runtime
boundary without introducing another cache or compiler/runtime semantic copy.
The change removed 3,172 physical C/C++ lines from `lambda/js` relative to
the clean baseline, with the required baseline gates remaining the acceptance
criteria. Sections below retain the original alternatives as design history.

## 1. What existed before retirement

Every non-computed `obj.name` load/store site in MIR-lowered JS carries a **per-callsite polymorphic inline cache**: a `JsLoadIC`/`JsStoreIC` cell (`js_runtime.h:115–137`) with a mono/poly(4-way)/megamorphic state machine, probed by `js_get_name_id_ic` / `js_set_name_id_ic` (`js_runtime.cpp:8285–8609`). The cell caches `(shape ptr, ShapeEntry ptr, byte offset, name_id, receiver_kind)` and a hit revalidates ~6 guards before reading/writing the slot.

The cells are not free-standing — they drag a full lifecycle apparatus:

| Layer | Where | What it does |
|---|---|---|
| Cell structs + states | `js_runtime.h:102–137` | mono/poly/mega, 4-entry arrays, miss counters |
| Probe/install/guards | `js_runtime.cpp:8089–8609` (~520 lines) | hit revalidation, entry building, install policy, array-props receiver kind |
| Per-module cell tables | `LambdaModuleState.ic_cells/ic_count/ic_cell_size`; `js_runtime_state.cpp:945–999` | link/append/seal, "sealed count changed" failure mode |
| Preamble inheritance | `js_transpiler.hpp:189`; `js_mir_entrypoints_require.cpp:107–206, 492, 929, 1538–1594, 1673` | consumer base offsets, count inheritance across require/batch/cache-restore |
| Eval/batch bases | `js_mir_eval_lowering.cpp:1072, 2086–2087`; `js_mir_module_batch_lowering.cpp:5903` | fresh-scope vs inherited IC index bases |
| MIR cache key | `js_mir_cache.cpp:45` | compiled-module cache hash mixes `ic_count` |
| Transpiler plumbing | `jm_module_ic_index`, `jm_active_module_ic_at_index` (`js_mir_calls_boxing_types.cpp:519+`), `module_ic_cache[32]` per-function register memo (`js_mir_context.hpp:477–479`), `named_ic_index` on `JsMirReference` | index allocation at lowering, cell-pointer materialization calls in emitted MIR |
| Lowering branches | `js_mir_expression_lowering.cpp:420–438, 1239–1244, 1410–1421, 1502–1516, 6989–7003` | IC vs non-IC call forms at 3 site kinds, 2 env toggles |
| Profiling surface | `js_exec_profile.h:127–160` + counters (~145 IC-related lines across .h/.cpp) | 2×16 site-reason enums, per-site labels |
| Flags | `LAMBDA_INLINE_CACHE` (`lambda.h:21–27`, `build_lambda_config.json:70`), `LAMBDA_JS_LOAD_IC`, `LAMBDA_JS_STORE_IC`, `LAMBDA_JS_ARRAY_NAMED_IC` | build-out and runtime kill switches |

Total: roughly **800 lines of mechanism plus a 5-arg store ABI**, entangled with module identity, preamble inheritance, eval scoping, and the MIR cache key. And the cache serves a narrow slice: **own data properties on plain maps / array companion maps only** — prototype hits (i.e., every method load) never cache and fall to the generic path on every access.

## 2. Why the IC is the wrong layer

**The gap the IC papers over is a linear lookup.** The runtime's NameId→slot lookup, `typemap_hash_lookup_name_id` → `typemap_shape_lookup_last_by_name_id` (`lambda-data.hpp:617–625`), is a **full linear walk of the ShapeEntry chain** (last-writer-wins forces scanning to the end). The per-TypeMap open-addressed hash table (`field_index[32]`, `lambda-data.hpp:342, 689–708`) exists and is maintained on every insert — but it is only ever probed by **name bytes**, never by NameId. The IC exists to skip an O(fields) walk that should never have been O(fields).

**An IC hit is barely cheaper than the honest lookup.** The mono hit path (`js_runtime.cpp:8285–8380`) already pays: `name_pool_resolve_id`, the host-dynamic-property check, receiver-map extraction + plain-shape admission, then ~6 guard compares (name_id, receiver_kind, shape identity, offset bounds, ctor reservation) before the slot read. A direct O(1) probe of the shape's own table costs the same admission plus one hash probe (integer compare) — the cell saves at most one or two dependent loads, and in exchange every guard is a staleness class to maintain.

**The measured wins came from shape work, not from the cells.** The Tune11/12 record (`vibe/jube/JS_Tune_History.md:145–189`) attributes every headline number to work that made *shapes* canonical and lookups cheaper — constructor shape sharing + derived pre-shaping (richards/deltablue megamorphic → 0), cached intrinsic prototypes (richards −23.3% wall, a *proto-chain* fix outside the IC entirely), array companion admission, sparse→dense promotion. All of that survives this proposal untouched; the state machine layered on top is what retires. The incremental value of the cells *over a fast shape lookup* has never been measured — §7 measures exactly that before anything is deleted.

**The cells are hostile to where the engine is going.** D8.1.1v2 tiering wants execution state that both an interpreter tier and JIT'd code share. Per-callsite cells are owned by MIR module images (allocated per module state, indexed by lowering, hashed into the MIR cache key); a shape-resident fast path is tier-neutral by construction — T0 and T1 hit the same tables with zero extra wiring. The AST-interpreter design already commits the Lambda lane to "no per-site mutable dispatch state" (`vibe/Lambda_Design_Ast_Interpreter.md` n4, §"must never become inline caches").

## 3. The QuickJS reference

Bellard's QuickJS has **no inline caches at all** and is competitive on property-heavy code. Its recipe: property names are **atoms** (interned integers — LambdaJS equivalent: `NameId`, already the definitive property identity per D4.6.1v2); every object points to a **shared shape** (hidden class — LambdaJS equivalent: shared `TypeMap` via constructor pre-shaping and per-callsite literal shapes, `js_property_attrs.h:133`); each shape carries an **atom-keyed hash table**: lookup is `atom & mask` → bucket → short `hash_next` chain of *integer* compares → property index into the object's flat slot array. A property hit is ~3 dependent loads with no per-site state, no invalidation, no staleness — the shape's table *is* the cache, and it is authoritative rather than a copy.

The natural experiment already ran: **quickjs-ng added callsite ICs and then removed them**. [PR #884 "Remove inline caches"](https://github.com/quickjs-ng/quickjs/pull/884) (merged 2025-02-06, **−486/+18 lines**) was prompted by [issue #876 "NG is slower compared to Bellard version"](https://github.com/quickjs-ng/quickjs/issues/876) — with ICs, the engine executed ~3% *more* instructions than the IC-free original on the same workload. When the underlying per-shape lookup is an integer-keyed O(1) probe, IC bookkeeping (probe + install + megamorphic transitions + guard revalidation) is overhead, not acceleration.

## 4. Proposed design — the shape is the cache

### IR1 — One shape-resident lookup replaces all per-site state

Retire `JsLoadIC`/`JsStoreIC` and every layer in the §1 table. The named fast path becomes a stateless kernel run per access: receiver admission → O(1) NameId probe of the receiver's TypeMap table → guard flags/reservation/deleted → slot read/write. No install, no states, no invalidation — the admission checks that today gate an IC *install* simply gate the *access*.

### IR2 — The enabler: O(1) NameId probe of the existing per-TypeMap table (zero new memory)

Add `typemap_hash_lookup_by_name_id(TypeMap* tm, NameId id, uint32_t key_hash)`: probe the **existing** `field_index` open-addressed table at the name's byte-hash, accepting on `entry->name_id == id` (identity compare per D4.6.1v2; the idless-Input byte-fallback rule from `js_load_ic_name_matches` is preserved as a second predicate). The byte-hash is **O(1)** — `property_key_hash(NameRef)` reads the hash cached in `NameMeta` (`lambda/core/name_identity.h:33, 114`); no FNV recompute, no memcmp on the id predicate. Table-full/absent falls back to the linear chain walk exactly as byte probes do today (`lambda-data.hpp:695–696`). Inserts are untouched — the table is already maintained at every shape append (`typemap_hash_insert`), and its last-writer-wins replacement already keys on `name_id` when both entries carry one (`typemap_shape_entries_equal`). This is QuickJS's atom-keyed shape hash, obtained by adding **one probe function (~20 lines)** rather than a second table.

### IR3 — Fast heads live in the existing plain entries; the admission kernel survives

`js_get_name_id(obj, id)` and `js_set_name_id(obj, id, v, strict)` (`js_runtime.cpp:8383–8398`) gain fast heads that are today's `js_named_ic_find_entry` (`js_runtime.cpp:8163–8231`) verbatim — renamed `js_named_fast_lookup` — with the internal linear id lookup swapped for the IR2 probe. Load-head order preserved from the IC entry: resolve NameRef → `js_get_host_dynamic_property` check (live `window` props must stay uncacheable-and-uncached) → admission (plain map or array companion, DOM live-collection exclusion, `length`/index-name filter) → probe → guards (`flags != 0` → descriptor promotion + slow; ctor reservation; deleted sentinel) → slot read. The head stays allocation-free and RootFrame-free like today's hit path; misses enter the rooted generic kernel (`js_get_reference` / `js_set_key_policy`) unchanged.

### IR4 — Store head keeps the same-slot write kernel and its hooks

The store head reuses `js_store_ic_can_write_same_slot` / `js_store_ic_write_same_slot` (`js_runtime.cpp:8408–8439`, renamed `js_named_fast_store_*`): existing own data slot, type-compatible in place (same TypeId, or INT→FLOAT/INT64 widening) — everything else (new property, transition, accessor, non-writable) goes to the semantic setter, exactly as the IC's install policy ruled. The two hit-path side hooks — `js_sync_global_var_module_binding` and `js_note_event_handler_property_set` — move into the head so behavior is bit-identical.

### IR5 — Lowering shrinks to one call form per site kind

Delete the IC branches and env toggles at all three lowering sites; named loads emit `js_get_name_id(obj, name_id)` (2 args) and named stores `js_set_name_id(obj, name_id, value, strict)` (4 args). Emitted MIR loses, per named site: the IC-cell pointer operand, the per-function `js_active_module_ic` materialization calls (`module_ic_cache` memo), and at direct member reads the boxed key literal (`js_mir_expression_lowering.cpp:6986`). MIR volume is the tiering pressure metric (Ast_Interpreter §Memory) — every named access site gets smaller.

### IR6 — Retirement inventory (deleted outright)

Structs/defines `js_runtime.h:102–137`; probe/install bodies and `_ic` entry points + forwarders in `js_runtime.cpp` (~400 of the ~520 lines; the kernel + store-write helpers survive renamed); `ic_cells/ic_count/ic_cell_size` on `LambdaModuleState` + `js_link_module_ic_table` / `js_append_module_ic_table` / `js_active_module_ic{,_count}` (`js_runtime_state.cpp:945–999`); `JsPreambleState.ic_count` and all require/batch/eval/cache-restore inheritance sites; the `ic_count` term in the MIR cache hash (`js_mir_cache.cpp:45` — one fewer cross-session invalidation input); `jm_module_ic_index` / `jm_active_module_ic_at_index` / `module_ic_cache[32]` / `ic_count` / `module_ic_base` / `named_ic_index`; `LAMBDA_INLINE_CACHE` (both the `lambda.h` define and the `build_lambda_config.json` entry), `LAMBDA_JS_LOAD_IC`, `LAMBDA_JS_STORE_IC`, `LAMBDA_JS_ARRAY_NAMED_IC` (array-companion admission becomes unconditional — it is shape admission, not IC policy).

### IR7 — Profiling slims to the kernel's truth

The 2×16 site-reason enums collapse to per-entry counters: `NAMED_FAST_PROBE`, `NAMED_FAST_HIT`, `NAMED_FAST_MISS_<reason>` (reuse the existing miss-reason taxonomy minus the install/mono/poly/mega states, which no longer exist). `JS_EXEC_PROFILE` keeps per-helper counts; hit-rate regressions stay visible.

### IR8 — Optional follow-up: proto-chain fast walk (separate, measured decision)

With O(1) per-shape probes, a QuickJS-style chain walk (repeat the probe up the prototype chain; any accessor/flagged/exotic level bails to the semantic path) would accelerate **method loads — the case the callsite IC never served**. Tune11 P6a (intrinsic-proto caching, richards −23.3%) shows chain-walk cost is real. Out of scope here; propose only after IR1–IR7 land and measure.

### IR9 — Doctrine update (landed, per rule 17)

D8.4.1v2 now extends *specialization over caching* to the JS lane: named
property accesses use the shared runtime reference/property kernels, with no
per-site mutable dispatch state in either lane; generated code stays immutable
(DI14 unchanged). The implementation intentionally stopped short of the
proposal's new shape-resident and Tier A compiler fast paths: removing the
existing IC and the duplicated indexed MIR lanes was the smaller design that
kept runtime semantics in one owner. The formal spec was bumped to v1.24.0;
this ledger records the proposal and its implemented simplification.

## 5. Tier A — compile-predicted member specialization (`load_member` / `store_member`)

Tier B makes every named access an honest O(1) lookup; Tier A removes even that lookup at sites where the **compiler** can predict the receiver's shape. The helpers are `js_load_member(obj, site_desc)` and `js_store_member(obj, site_desc, value, strict)`, where `site_desc` is one packed immutable constant `[type_index:16 | prop_index:16 | name_id:32]` (NameId is already `[pool16][ordinal16]`, D4.6.1v2): `type_index` names the predicted canonical pre-shape, `prop_index` indexes that TypeMap's existing `slot_entries[]`, and `name_id` feeds the Tier B head on guard failure — inside the same helper, so a miss costs one branch, never a cliff. Mono by design: one predicted type per site, no install, no state transitions; polymorphic sites simply run Tier B for the minority shapes (and IR13 recovers the dominant polymorphism family, subtypes).

### IR10 — Type identity, guard, and the invariant that pays for it

Canonical pre-shapes receive a runtime `fast_type_id` (`uint32` on TypeMap) at registration; the site's `type_index` is **module-image-relative** and resolves through the active module state exactly like NameIds (`js_active_module_name_id` pattern + per-function register memo) — baking a raw id or pointer would bake a realm into shared MIR (same D5.4.3/D5.4.4 discipline the IC comment cites). Guard = `receiver.map->type->fast_type_id == resolved_id`: two dependent loads + one compare. The load-bearing invariant: **only canonical, unmutated pre-shapes carry an id, and the Map-local clone primitive (`js_property_attrs.h:133`) never copies it** — so delete, redefine, accessor install, freeze, and post-construction extension all detach the instance to an id-less TypeMap and the guard fails. One enforcement point, and it makes the per-hit `flags`/deleted checks *free* — they are subsumed by shape identity. The one thing identity does **not** subsume is the ctor reservation mask (same shared TypeMap, per-instance mask — the exact hazard `js_store_ic_try_hit_entry` documents), so the reserved-bit test for `prop_index`'s offset stays on the hit path.

### IR11 — `slot_entries[]` is the sole property-ordinal index

Do **not** add `MemberSpec[]`, `by_ordinal[]`, or any second ordinal representation. Generalize the existing `TypeMap::slot_entries` index (`lambda-data.hpp:347–348`) so that, whenever it is published, its invariant is:

```text
slot_entries[i] = the i-th ShapeEntry in definition/layout order
slot_count      = the number of indexed ShapeEntry pointers
prop_index      = i
```

The shape chain remains authoritative (D3.4.1/D3.4.2); `slot_entries[]` is only its immutable O(1) index. Tier A resolves `ShapeEntry* entry = predicted_type->slot_entries[prop_index]`, then uses `entry->byte_offset`, `entry->type`, `entry->flags`, `entry->name_id`, and the shared lane read/store helpers. Keeping the full `ShapeEntry` is required by D3.4.6: storage is derived from the full `Type*`/`LaneStorageDesc`, so a reduced `{TypeId, offset, flags}` copy would be both duplicate state and insufficient for nullable/composite contracts.

Today `slot_count` also implicitly means "the whole indexed region is a contiguous pointer-width constructor prefix." Split that physical-layout fact from logical property indexing: add `fixed_slot_count`, the number of leading `slot_entries[]` whose offsets are `i * sizeof(void*)`. `slot_count` owns ordinal coverage; `fixed_slot_count` is only a storage-layout certificate used by `typemap_fixed_slot_prefix_count` / `typemap_entry_uses_fixed_slot`. It is metadata about one prefix, not another ordinal table. Producers, transition/rebuild paths, and TypeMap clone paths must rebuild the full `slot_entries[]` in chain order and preserve or recompute `fixed_slot_count`; no cloned index may retain pointers into the source chain.

Registration of a Tier A descriptor requires one proof point: `slot_entries != NULL`, `prop_index < slot_count`, the indexed entry's property identity matches the compiled property, and the canonical shape/layout certificate is valid. After the exact shape guard, load hit = entry fetch + typed slot read; store hit = same-slot type compatibility against that entry (exact, INT→FLOAT, INT→INT64 — today's `js_store_ic_can_write_same_slot` rule) or Tier B fallback. The per-instance constructor reservation test remains because shape identity cannot subsume it. Exotic receivers — the global object, `window` host dynamics, DOM live collections, array companion maps — are excluded by **never assigning them ids**: all admission policy centralizes in id assignment instead of per-access checks.

### IR12 — Prediction: where `type_index` comes from (the key challenge)

Untyped JS means prediction is a static-analysis budget question. Ranked by coverage-per-cost, all static in v1:

1. **`this` inside class bodies and recognized prototype-idiom methods** → the enclosing class's combined pre-shape. The lowering already recognizes these constructors to build pre-shapes (Tune12 P2's idiom list, cross-checked inheritance patterns); the method's definition site is lexically bound to the class, so the prediction is free and *hot OO code is overwhelmingly `this.field`* (richards, deltablue, box2d). This is the anchor case — it alone should carry most of Tier A's value.
2. **`const`/provably-single-assignment locals initialized by a visible `new Ctor(...)` or object literal** → that ctor/literal pre-shape. This is precisely the `jm_infer_jube_type` proof pattern ("deliberately small: only declared-signature results and the immutable global seed participate; unknown assignments and joins return NULL", `js_mir_expression_lowering.cpp:1332–1377`) extended from Jube host types to user shapes.
3. **(later) Parameters of module-local, non-escaping functions** whose closed caller set agrees on one predicted type — the Lambda lane's dual-function-by-caller precedent (D8.3.1). JS's open world makes this rarer; it is an extension, not a v1 requirement.
4. **No runtime feedback in v1.** The ABI deliberately decouples prediction from mechanism: a future tier-up profiler (once JS tiering exists per D8.1.1v2) just supplies better `site_desc` constants at re-lowering — no runtime change, no cells, and JR8's load/store feedback slots stay dead.

Everything unpredicted emits the plain Tier B call. Expectation-setting: Tier A is a targeted accelerator for method bodies and provenance-visible locals, not a universal layer — Tier B is the universal layer, and the measured profile (helpers ≈43.5% of CPU, generated code ≈0.6%) says the floor matters more than the peak.

### IR13 — Subtype admission by id ranges (the inheritance optimization)

Combined derived pre-shaping is already **base-first**, so prefix layout compatibility is the physical default for class hierarchies. Assign `fast_type_id`s in preorder over the module's static class hierarchy so every subtree is a contiguous range; an A-site guard becomes `(id − A_first) <= A_subtree_span` — **one compare admits every layout-compatible descendant**, turning the dominant polymorphism family (subtype poly: deltablue's variable/constraint hierarchies, box2d shapes) back into Tier A hits. The binding constraint is **representation** compatibility, not name-prefix compatibility: the pre-shape merge must preserve every base field's `slot_entries[]` position, `byte_offset`, and full `Type*`/lane descriptor in the derived layout (D3.4.6) — widen at merge time (e.g., INT→FLOAT unification) or refuse. Refusal is graceful: C gets its own id family, A-sites fall back to Tier B for C receivers, nothing is wrong — just slower. Dynamic proto surgery (`Object.setPrototypeOf`, expando-heavy factories) never gets ids at all.

### IR14 — Helper first, inline later

v1 emits the helper call (simple, measurable, one new entry pair). But the profiling record is blunt — JIT-generated code is ~0.6% of working CPU while runtime helpers are ~43.5% (`vibe/jube/JS_Profiling_Helpers.md` §2) — so the endgame for Tier A is **inlining the guard + typed read into MIR** (load type, load id, compare, branch-to-helper, typed slot read), with the helper kept as the out-of-line miss body. The packed single-constant ABI is what makes that expansion mechanical: inline sequence and helper share the same operands. Immutability holds throughout — emitted constants, never-patched code (DI14). This is D8.4.1's own words realized in the JS lane: *"Multi-version dispatch, when it comes, is a guard chain, never a patchable cache."*

### IR15 — Precedent: this generalizes the Jube ordinal path

The triple already ships. The Jube interface path lowers exactly `(type slot, member ordinal, key fallback)`: `ref.jube_slot`/`jube_ordinal` (`js_mir_expression_lowering.cpp:1260–1276`) → `js_jube_member_get_by_ordinal(receiver, slot, ordinal, key)` (`js_runtime.cpp:7880`) over per-type member tables that carry ordinal→kind/arity/result metadata (`jube_interface.h:49–65`), compile-time proven by `jm_infer_jube_type`, with the ordinary property path as the semantic fallback. Tier A extends the dispatch pattern from host interface types (document, DOM) to user object shapes, but its user-shape ordinal resolves through the one existing `TypeMap::slot_entries[]` index from IR11. Jube's member **kind** dimension also names the deferred upgrade: with a compile-known class, `prop_index` can address prototype *method* ordinals — accelerating `this.method()` dispatch, which neither the retiring IC nor Tier B serves (OQ5, staged with IR8).

## 6. Performance expectations

Expected outcome is **parity-to-win on IC-friendly benchmarks from Tier B alone, a strict win where Tier A applies, and uniform win everywhere else**, mirroring quickjs-ng's result:

- **Tier A hit strictly beats the IC mono hit**: 2 loads + 1 compare (+1 range compare under IR13) + reserved-bit test + typed read, versus the IC's NameRef resolve + host check + admission + cell load + ~6 guard compares. It also skips Tier B's resolve+probe entirely.
- **Tier B mono-hit parity with the IC**: the IC hit and the IR2 probe do the same admission work; the delta is cell-load+6-guards vs hash-probe+3-guards — noise-level either way.
- **Poly/mega/miss win**: no 4-entry scans, no probe-then-install double lookups, no permanently-penalized megamorphic sites (today's mega sites pay probe + slow path on every access; the kernel just runs once).
- **Compile/link win**: fewer MIR operands and calls per named site, no IC table link/append per module activation, simpler MIR cache key.
- **Memory win**: `sizeof(JsLoadIC)` / `sizeof(JsStoreIC)` is 136 bytes on the current 64-bit ABI × sites per module image, gone; Tier B adds no per-TypeMap allocation (IR2 reuses the existing table). Tier A adds no second ordinal table: canonical pre-shapes reuse `slot_entries[]` and add only identity/registry metadata. Generalizing a previously partial `slot_entries[]` may add suffix pointers, still once per shape and amortized across all instances.
- **Proto-hit unchanged for fields** (both tiers miss today; IR8 covers it); prototype **methods** become reachable only via the IR15/OQ5 method-ordinal extension — the single biggest still-unserved pattern.

## 7. Migration plan

Tier B lands first and retires the IC; Tier A layers on top of a proven fallback. Retirement is not blocked on Tier A.

| Phase | Work | Gate |
|---|---|---|
| P0 | IR2 probe + unit tests (id hit, idless fallback, dup-name last-writer-wins, table-full fallback, 33+-field dynamic table); microbench `js_get_name_id` on 4/16/64-field shapes | probe ≡ linear-walk results on the full corpus; O(1) confirmed |
| P1 | IR3/IR4 fast heads behind `LAMBDA_JS_NAMED_FAST=1`, IC path still default; lowering unchanged | `make test-js` green both ways |
| P2 | A/B: full JS baseline + Test262 (rule 18 — no masking) + jetstream (richards, deltablue, hashmap, navier_stokes), cube3d, box2d, with `JS_EXEC_PROFILE` hit-rate comparison IC vs kernel | wall-time within noise or better on every fixture; hit-rate parity on the IC's own served slice |
| P3 | Flip lowering to IR5, delete IR6 inventory, slim profiling (IR7), doctrine (IR9), update `doc/dev/js/` property-access design doc | baselines 100%; LOC delta reported |
| P4 | Tier A: id registry + module type-index tables (IR10), generalize `slot_entries[]` and add `fixed_slot_count` (IR11), `js_load_member`/`js_store_member` helpers, prediction pass cases 1–2 (IR12), behind `LAMBDA_JS_MEMBER_FAST=1` | slot index/chain invariants tested across build, transition, rebuild, and clone paths; differential vs Tier B on full corpus; Tier A hit-rate reported per benchmark |
| P5 | Flip Tier A default; measure wall vs Tier B-only | strict improvement on `this.`-heavy fixtures (richards, deltablue, box2d); no regression elsewhere |
| P6 | IR13 preorder id ranges + merge representation policy; optionally IR14 inline expansion if P5 profiles show helper-call overhead dominating the hit | measured per phase; each optional |

P2 is the honest checkpoint: if the cells measurably beat the kernel somewhere, that result comes back to this doc before any deletion — the flag makes the comparison one env var.

## 8. Risks

- **Hidden IC-hit dependency**: some site relies on the IC returning without running generic-path side effects (ordering of `js_note_event_handler_property_set`, global-binding sync). Mitigation: hooks move verbatim (IR4); differential A/B in P2 covers the DOM/event suites.
- **Byte-hash collision chains in the shared table**: id probes traverse byte-hash collisions of unrelated names; predicate is one integer compare per step, and 32-slot tables at ≤50% recommended occupancy keep chains ~1. Microbench in P0 confirms.
- **Shapes that mutate under the probe**: none — the probe reads the live table; there is no cached copy to go stale. This risk exists only in the design being retired (dangling cached `ShapeEntry*` after Map-local shape clone, guarded today by shape-identity compare).
- **Ultra-hot monomorphic loops regress a few percent under Tier B alone**: the lever is Tier A (P4) — compile-predicted specialization at exactly those sites — not a shape-resident memo hack; if P2 shows a gap on a fixture Tier A cannot predict, that result comes back here before P3.
- **Tier A invariant leaks**: a path that mutates a canonical pre-shape without going through the Map-local clone primitive would leave a stale `fast_type_id` matching a changed layout. Mitigation: the id lives beside the shape data it describes; a debug assert in the clone primitive plus a `LAMBDA_JS_MEMBER_FAST=0` kill switch; P4 differential runs with the assert hot.
- **`slot_entries[]` publication drift**: a producer could publish a partial index or a clone could retain source-chain pointers, making `prop_index` disagree with definition order. Mitigation: one invariant for every populated index, construction-time/debug assertions that `slot_count` covers the chain exactly, and focused P4 tests for constructor build, append/transition, type-changing rebuild, descriptor clone, delete, and resurrection. `fixed_slot_count` is validated separately as a prefix-layout certificate.
- **Prediction staleness across MIR cache reuse**: `prop_index`/`type_index` are meaningful only against the pre-shape layout computed by the same compilation; module type-index tables are populated by the same module image, and cross-version reuse must invalidate via the existing cache keys (OQ4 audits this).

## 9. Open questions

- **OQ1**: `js_get`/`js_set` (`js_props.cpp:1231/1253`) still accept NameId-bearing lanes from non-lowering callers — should their lane path route through the same kernel head for uniformity, or stay generic? (Lean: route through, it's one call.)
- **OQ2**: does any preamble/batch consumer depend on `ic_count` in the MIR cache hash for correctness beyond IC layout (i.e., as an accidental version discriminator)? Audit before deleting the hash term.
- **OQ3**: IR8 proto-walk interaction with `js_get_intrinsic_prototype_for_class` caching — one chain-walk design or two?
- **OQ4**: Tier A id/table lifecycle across the MIR cache (L1/L3) — are pre-shape registration and type-index tables always re-derived from the loaded image, or can a cached image pair with differently-laid-out pre-shapes? Must be settled before P4.
- **OQ5**: prototype **method** ordinals in the Tier A table (IR15): what guards prototype-content mutation — the prototype map's own shape identity, a class-seal bit, or restriction to classes whose prototype is provably untouched after definition? Interacts with IR8; one design should cover both.
- **OQ6**: IR13 merge policy — how aggressive is representation widening at pre-shape merge (INT→FLOAT always? INT64?), and does widening the base layout for a derived class's benefit ever regress base-only workloads? Needs a measurement gate of its own.
