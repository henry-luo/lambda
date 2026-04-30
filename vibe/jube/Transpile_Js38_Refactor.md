# Transpile_Js38 — Engine Refactor for Robustness, Modularity & Regression Safety

## Progress snapshot (current)

Verification gates throughout: `make build` (Errors: 0), `./test/test_js_props_gtest.exe`
(16/16 PASS), test262 baseline batch
(`ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-partial`):
**~28746–28747 fully passing, 0 regressions, 0 improvements** vs the locked baseline
(small ±1 batch noise observed across consecutive runs even without code changes;
the gate is `regressions=0`, met).

| Stage | Status | Notes |
|---|---|---|
| B — Property-model GTest harness | DONE | `test/test_js_props_gtest.cpp`, 16 tests in `Props.*` suite, ~600ms. Inner-loop gate for all Stage A work. |
| A1 — `ToPropertyKey` extraction & sweep | DONE | `js_to_property_key` in `js_runtime.cpp`. 6 ad-hoc `js_to_string(key)` sites converted. URLSearchParams left as `js_to_string` (spec). |
| A1 — `Ordinary*` kernels in `lambda/js/js_props.{h,cpp}` | DONE | 11 kernels: `js_ordinary_get_own`, `_get_own_descriptor`, `_set_via_accessor`, `_has_own`, `_own_status` (tri-state), `_has_property`, `_get`, `_set`, `_delete`, `_resolve_shape_value`, plus the `js_props_*` query helpers. MAP-only chokepoints; callers wrap with full ABI. |
| A1 routing — call-site replacement | LARGELY DONE | Routed: Array companion-map delete branch in `js_globals.cpp` (~30 → ~12 lines); R4 `js_has_own_property` MAP branch via tri-state `js_ordinary_own_status`; R7 `js_super_property_set` setter dispatch via `js_ordinary_set_via_accessor` (A1.4); **R1 `js_property_get` MAP branch via `js_ordinary_get_own` (L5373) and FUNC branch's `properties_map` slot+sentinel+IS_ACCESSOR via `js_ordinary_get_own(pm_obj, key, object)` (L5837)**; **R2 `js_property_set` MAP branch setter dispatch via `js_ordinary_set_via_accessor` (L7111)**. Investigated/blocked (WONTFIX with source comments): R3 `js_in` (proxy-on-proto walk per step), R5 FUNC delete and R2 FUNC set (both depend on virtual `length`/`name`/`prototype` shadow semantics + `__nw_X` markers — the kernels are spec-pure MAP-only and can’t model these without losing their narrowness). Net: every kernel-eligible leaf primitive in the `get`/`set`/`delete`/`has` MAP entrypoints is now routed; FUNC virtual-property paths are intentionally left hand-rolled. |
| A2 — `PropertyDescriptor` table | PARTIAL | A2.1 read-side facade `js_get_own_property_descriptor` (synthesizes descriptor from shape+slot+legacy markers) — DONE; A2.3 write-side kernel `js_descriptor_from_object` + `js_define_own_property_from_descriptor` (ToPropertyDescriptor + ValidateAndApply storage tail) — DONE; A2.5 `Object.defineProperty` / `Object.getOwnPropertyDescriptor` / `Object.defineProperties` routing — DONE; A2.6 marker write helpers `js_attr_set_writable/_enumerable/_configurable` (centralizes the `snprintf("__nX_%.*s") + heap_create_name + js_defprop_set_marker` pattern) — DONE, **14 sites routed**: 6 in the descriptor kernel (final attribute writes), 4 in `js_runtime.cpp` (`js_mark_non_*` family + `js_mark_all_non_enumerable` loop), 3 in `js_globals.cpp` `js_delete_property` marker-clear cluster (probe stays to avoid spurious shape entries; only the conditional clear is routed), 1 in `js_props.cpp` `js_define_own_property_from_descriptor` data-branch save/restore (the `had_nw` clear-then-restore pair). **A2-T1+T2 (Map-local shape clone) — DONE**: `js_typemap_clone_for_mutation` in `js_property_attrs.cpp` allocates a private TypeMap+ShapeEntry chain on first attribute mutation, repopulates field_index hash and slot_entries, marks `is_private_clone=true` for idempotency, swaps `m->type`. `js_shape_entry_update_flags` rerouted: probe-first to skip cloning when entry absent or mutation is a no-op (avoids stranding maps that started with type==NULL via the &EmptyMap-derived path). **A2-T3 (`jspd_set_accessor` rerouting) — DONE**: new `js_shape_entry_set_accessor(obj, name, len, bool)` helper, all 4 in-place `jspd_set_accessor(_se, false)` sites routed (js_props.cpp:280,684; js_globals.cpp:9155,9347). Sibling Maps sharing a TypeMap via per-callsite shape cache are now safe from cross-contamination on attribute mutation. Verified `0 regressions / 28748 fully passing` after T3. Markers (`__nw_/__ne_/__nc_/__get_/__set_`) still dual-written for transition safety; T5 onwards retire them in phases. |
| A3 — `JsClass` enum | IN PROGRESS (T1+T2 DONE) | T1: 50-entry `JsClass` enum + helpers in `lambda/js/js_class.h`; `uint8_t js_class` byte added to `TypeMap` (zero-init = `JS_CLASS_NONE`); clone-aware writer `js_class_set_for_map` reuses A2-T1 `js_typemap_clone_for_mutation`; reader `js_class_get(Item)` is a pure inline read. T2: `js_proto_class_method_dispatch` fast-path switch on `JsClass` ahead of legacy `__class_name__` strncmp chain (Boolean/String/Array migrated; Number/Date/RegExp left in legacy path until T3+ stamps their prototypes). Verified `0 regressions / 28853 / Props 20/20` after each step. T3+ (per-file writer migration to dual-write byte AND string, then reader migration, then string drop) outstanding — full retirement scope is 214 sites across 20 files. |
| C — Megafile splits | NOT STARTED | Mechanical, gated on A2. |
| D — Hidden-state RAII | DONE | `lambda/js/js_state_guards.h` (`ScopedSkipAccessorDispatch`); anon-namespace `ScopedProxyReceiver` adjacent to file-static `js_proxy_receiver` in `js_runtime.cpp`. 5 manual save/restore sites converted (3 in `js_runtime.cpp`, 1 in `js_props.cpp`, 1 in `js_property_attrs.cpp` — last one drops a manual restore on early-return). Builtin caches and name-pool reuse audit still pending. |
| E — Debug-only invariant assertions | DONE | 3 macros in `js_props.cpp`, NDEBUG-gated to `((void)0)`: `JS_PROPS_ASSERT_KEY` (non-NULL name + non-negative len), `JS_PROPS_ASSERT_NOT_DEL_ACCESSOR` (sentinel + IS_ACCESSOR mutually exclusive), `JS_PROPS_ASSERT_ACCESSOR_PAIR` (IS_ACCESSOR ⇒ slot decodes to non-null `JsAccessorPair*`). Inserted at all 6 `js_ordinary_*` kernel entries and at the joint slot+shape sites in `_get_own_descriptor` and `_delete`. No assertions trip on full baseline. |
| F — Spec-driven change discipline | ONGOING | Adopted in commit notes. |

### Outstanding tasks (priority order)

1. **A2 dense storage via SHAPE TRANSITIONS — design committed.**

   **Corrected picture of what's actually shared.** The Lambda `shape_pool.cpp`
   is **not used by the JS engine at all** — it's only used by Lambda input
   parsers ([`input.cpp`](../../lambda/input/input.cpp),
   [`shape_builder.cpp`](../../lambda/shape_builder.cpp)). JS Maps acquire their
   `TypeMap` either by direct allocation
   ([`js_runtime.cpp::js_new_object_with_shape` L4763](../../lambda/js/js_runtime.cpp#L4763))
   or via **per-call-site shape cache** (`shape_cache_ptr` —
   [`transpile_js_mir.cpp` §7 L256, L4450, L4453](../../lambda/js/transpile_js_mir.cpp#L256);
   populated by
   [`js_constructor_create_object_shaped_cached` L4866](../../lambda/js/js_runtime.cpp#L4866)).

   So the real "siblings get corrupted" mechanism is: every `new Foo(...)`
   from the same callsite shares the cached `TypeMap*`. When one instance
   mutates `ShapeEntry::flags` in place, every other instance sees the change.
   The legacy `__nw_/__ne_/__nc_/__get_/__set_` markers exist as the per-Map
   override that papers over this.

   **Chosen design — Map-local shape clone (V8/JSC hidden-class pattern):**
   `ShapeEntry` stays immutable once a `TypeMap` is published (i.e. cached or
   referenced by ≥1 Map). Attribute mutation (writable / enumerable /
   configurable / accessor flip / delete) **clones the `TypeMap` + shape chain
   for that Map only**, mutates the cloned entry, and swaps `m->type`. The
   shape cache `shape_cache_ptr` still points at the original immutable
   TypeMap — future `new Foo(...)` calls keep getting the unmodified blueprint.

   **Why this is the cleanest option:**
   - Single source of truth for property attributes: `ShapeEntry::flags`. No
     dual-write, no marker probes, no fallback.
   - Zero coupling to `shape_pool.cpp` — the pool stays Lambda-only; the JS
     change is contained to `js_property_attrs.cpp` plus reader cleanup.
   - Readers stay simple: `jspd_is_writable(se)` is the entire query. The
     `js_props_query_*` helpers and the `__nw_/__ne_/__nc_/__get_/__set_` markers
     all retire.
   - The clone primitive composes with `js_shape_entry_update_flags` and
     `jspd_set_accessor` mutation sites — same call shape, just goes through
     `js_shape_clone_for_mutation(m)` first.
   - Unblocks FUNC virtual-property paths (R5 / R2-FUNC WONTFIX) — once
     attributes live uniformly on `properties_map`'s shape, the kernels can
     absorb FUNC writes without a special MAP-only contract.

   **Phased implementation (each phase independently shippable, gated by
   `make build` + Props gtest + test262 baseline):**

   - **Phase A2-T1 — `js_shape_clone_for_mutation(Map* m)` primitive.** In
     `js_property_attrs.cpp`: allocates a new `TypeMap` and a new `ShapeEntry`
     chain copying the existing `name`/`type`/`byte_offset`/`flags`/`ns`/`default_value`
     (`next` rewired). Repopulates `field_index` hash and `slot_entries` (if
     present). Swaps `m->type`. Idempotent: tracks whether the current TypeMap
     is already this Map's private clone (e.g. via a `bool is_private` field on
     TypeMap, or by recording the clone-source pointer) to avoid re-cloning on
     repeated mutations to the same Map.
   - **Phase A2-T2 — Route `js_shape_entry_update_flags` through clone.** Body
     becomes: `js_shape_clone_for_mutation(m); ShapeEntry* se =
     js_find_shape_entry(...); se->flags = ...;`. Same external contract;
     siblings now safe.
   - **Phase A2-T3 — Route the 4 `jspd_set_accessor` sites through clone.**
     Same pattern — clone first, then mutate. (Sites: js_props.cpp:280, 684;
     js_globals.cpp:9155, 9347.)
   - **Phase A2-T4 — Add Stage B sibling-Map invariant tests. — DONE.**
     Three new `Props.*` gtests added to `test_js_props_gtest.cpp`:
     `SiblingMapSharingTypeMap_NonWritablePerMap` (two `new Foo()` instances,
     `defineProperty` non-writable on one, asserts other still writable),
     `AccessorToData_NoSiblingCorruption` (accessor→data conversion on one
     instance leaves sibling's accessor intact), `CloneIdempotent_NoDoubleAlloc`
     (multiple mutations on same Map; sibling defaults preserved). Suite is
     19/19 PASS in ~600ms. These would FAIL without A2-T1+T2+T3 — they
     capture the cross-Map contamination class structurally.
   - **Phase A2-T5 — Stop dual-writing markers from `js_attr_set_*`. — DONE.**
     `js_attr_set_writable/_enumerable/_configurable` now go through new
     `js_attr_apply_or_marker` helper: probe `js_find_shape_entry` first; if
     entry exists, mutate via the clone-aware `js_shape_entry_update_flags`
     (no marker write); if absent, fall back to legacy marker write so the
     attribute applies once the property is later defined and the marker
     reader fallback in `js_props_query_*` picks it up. Three downstream
     callers had to switch from marker-presence probes to shape-aware
     `js_props_query_*` probes (their pre-T5 logic relied on the inadvertent
     "marker = was-defineProperty'd" sentinel that T5 invalidates):
     `js_globals.cpp` `js_delete_property` MAP marker-clear loop,
     `js_runtime.cpp::js_create_data_property_or_throw` (ARRAY+MAP branches:
     check configurable + clear writable/enumerable via shape rather than
     marker presence), and `js_runtime.cpp::js_regex_set_lastindex_strict`.
     Also added `JS_DELETED_SENTINEL_VAL` filtering to the marker-fallback
     branch of `js_props_query_*` (latent bug exposed by T5: prior code
     always overwrote stale tombstones, masking the truthy-sentinel
     misread). Verified `0 regressions / 28853 baseline passing` after T5.
   - **Phase A2-T6 — Retire marker reads (named-property scope). — DONE
     (via AT-4 Option B; 0 regressions / 28853 / Props 20/20).**
     `js_props_query_*` marker-fallback path is now scoped to
     digit-string (indexed) names only. Named-property reads consult
     the shape flag exclusively. The fallback codepath is preserved
     for indexed properties on `MAP_KIND_ARRAY_PROPS` companion maps
     (still load-bearing — `arr->items[idx]` storage has no ShapeEntry
     to carry the bit). Full marker-scheme retirement (covering
     indexed properties too) is deferred as AT-4 Option A.
   - **Phase A2-T7 — Retire `__get_X`/`__set_X` accessor markers. — DONE
     (via AT-1 + AT-3).** See "AT-Phase" subsection below for the
     full retirement story. All `__get_%d` / `__set_%d` /
     `__get_%.*s` / `__set_%.*s` snprintf sites in
     `lambda/js/*.cpp` are retired (string-grep returns 0 matches).
     Verified `0 regressions / 28853 baseline passing` and Props 19/19
     after each batch.
   - **Phase A2-T8 — `JSPD_DELETED` shape bit. — DONE (T8a + T8c). T8b/T8d
     CANCELLED.** What landed:
     - **T8a**: bit `0x10` added to the `JSPD_*` set in
       `lambda/lambda-data.hpp`; `jspd_is_deleted` / `jspd_set_deleted`
       predicates in `lambda/js/js_property_attrs.h`; per-Map clone-safe
       mutator `js_shape_entry_set_deleted` in
       `lambda/js/js_property_attrs.cpp` (wraps the existing
       `js_shape_entry_update_flags` primitive). Central
       `js_delete_property` site in `js_globals.cpp` (~L9295) dual-writes —
       slot sentinel + shape bit. Props gtest #20
       `Tombstone_DeletePropagatesAcrossReaders` added as reader-agreement
       gate.
     - **T8c**: writer dual-write at the remaining bare-key sentinel-write
       sites — `js_props.cpp` `js_ordinary_delete` (~L298),
       `js_globals.cpp` FUNC delete bare-key (~L9046), `js_globals.cpp`
       array companion-map bare-key (~L9114), `js_runtime.cpp` `__proto__`
       MAP→non-MAP transition (~L7235). Skipped on principle:
       `js_define_own_property_from_descriptor` data→accessor transient
       (would need paired clear at accessor install — risk of stale
       DELETED bit over a live accessor); marker keys (separate keyspace,
       no ShapeEntry); `arr->items[idx]` positional storage.
     - Verified `0 regressions / 28853 baseline passing` / Props 20/20
       PASS after each batch.

     **T8b/T8d cancellation rationale.** Detailed scope analysis after
     T8c surfaced two structural issues:
     1. **T8d cannot drop the macro.** Of 95 `JS_DELETED_SENTINEL_VAL`
        references, ~25 are array-position writes/reads
        (`arr->items[idx]`) and ~16 are marker-key reads/writes
        (`__nw_X`/`__ne_X`/`__nc_X` tombstone filters). Both categories
        are out of scope for T8 (no ShapeEntry to carry the bit). The
        macro and array/marker usage stay, contradicting T8d's stated
        goal of dropping it.
     2. **T8b adds runtime cost without correctness benefit.** The bug
        class T8 was meant to fix (FLOAT-key sentinel-misread) is a
        key-canonicalization issue already eliminated by A1's
        `ToPropertyKey`. Migrating ~30 map-slot reader sites to consult
        `jspd_is_deleted(se)` adds a `js_find_shape_entry` hash lookup
        per read in exchange for no observable behavior change. Most
        sites don't have `se` in scope today.

     The T8a+T8c foundation is preserved as a building block for any
     future feature that wants to query "deleted" via shape (e.g. the
     dense `PropertyDescriptor` table in §A2). It is NOT a precursor to
     dropping the sentinel macro — that would require AT-4 first
     (companion-map digit-string ShapeEntries) and even then array-hole
     positional storage keeps the sentinel.
   - **Phase A2-T9 — FUNC virtual-property unification — DONE (0 regressions / 28853, Props 20/20).**
     Both R5 (FUNC delete in `js_globals.cpp`) and R2-FUNC (FUNC set
     prototype/.name/.length/arbitrary in `js_runtime.cpp`) cleaned up
     post AT-4 Option B. Approach was **dead-code removal**, not kernel
     routing — the original WONTFIX root cause (kernel `js_ordinary_delete`
     short-circuits on absent slots, breaking FUNC's virtual-shadow
     sentinel-write trick) still applies and would still regress 35
     S15_*_A9 tests. Kept the inline FUNC `js_property_set(..., SENTINEL)`
     write that shadows the virtual `length`/`name`/`prototype`. Removed:
     (a) R5 marker-tombstone loop over `__nw_/__ne_/__nc_<key>` (now dead
     for named keys — readers consult shape flag only post AT-4 B);
     (b) R2-FUNC prototype branch's direct `__nw_prototype` probe →
     replaced with `js_props_query_writable`;
     (c) R2-FUNC `.name`/`.length` and arbitrary-prop branches' tri-state
     `js_prop_attrs_fast_path` + legacy marker fallback → collapsed into
     single `js_props_query_writable` call. Same safety contract as AT-4
     Option B: when a FUNC property slot exists, dual-write guarantees a
     shape entry exists, so the `fp == -1` legacy fallback was
     unreachable in practice. Kernel routing of FUNC delete remains
     deferred indefinitely (R5 root cause unchanged).

   **Non-goals during A2:** no `Map` header changes, no GC-tracer changes, no
   `shape_pool.cpp` changes, no `transpile_js_mir.cpp` changes. The single new
   `TypeMap` field is `bool is_private` (or equivalent clone-source pointer) —
   one byte.

   ### AT-Phase — Array Attribute Table (companion-map shape parity)

   Goal: give `MAP_KIND_ARRAY_PROPS` companion maps the same shape-entry
   storage parity as regular maps, unblocking T6/T7/T9.

   - **AT-1 — DONE (0 regressions / 28853).** Removed
     `MAP_KIND_ARRAY_PROPS` bypass in `js_intercept_accessor_marker`
     (`lambda/js/js_property_attrs.cpp` ~L561). Writers calling
     `js_property_set(props, "__get_5", fn)` now route through intercept
     → `js_install_native_accessor` storing `JsAccessorPair*` under
     digit-string name "5" with IS_ACCESSOR + NON_ENUMERABLE shape flags.
     Insight: bypass was obsolete; Phase 5D added IS_ACCESSOR support on
     companion maps under digit-string names.
   - **AT-2 — SUBSUMED by AT-1.** Writers auto-migrate via intercept.
   - **AT-3 — DONE (0 regressions / 28853).** Retired all
     `__get_<X>`/`__set_<X>` reader fallbacks across `js_runtime.cpp` +
     `js_globals.cpp` + `js_props.cpp`. Critical fix discovered during
     batch 2: `Object.keys` array branch was missing non-enumerable
     accessors because it only checked `__ne_<idx>` marker; post-AT-1
     the NON_ENUMERABLE bit lives on the digit-string shape entry
     directly. Added `jspd_is_enumerable(_se_idx)` probe alongside the
     marker probe (`js_globals.cpp` ~L6325). Final string-grep for
     `__get_%`/`__set_%` in `lambda/js/*.cpp` returns zero matches.
   - **AT-4 — DONE (Option B; 0 regressions / 28853 / Props 20/20).**
     Scoped the `js_props_query_*` marker-fallback path in
     `lambda/js/js_property_attrs.cpp` to **digit-string (indexed) names
     only** via a new `js_attrs_name_is_digits(name, len)` helper
     (rejects leading-zero numerics per CanonicalNumericIndexString).
     Named properties: shape flag is now authoritative; if unset, the
     attribute is the default. Indexed properties: marker scheme remains
     the source of truth (no structural change to `arr->items[idx]`
     storage). Verified by analysis that every `js_attr_set_*` call site
     for a named property in current code first writes the data slot
     (in `js_define_own_property_from_descriptor` /
     `js_create_data_property_or_throw` / the delete-property
     marker-clear cluster), so the shape entry exists by the time the
     attribute write happens — the named-property fallback path was
     dead code in practice. **Unblocks T6 (named-property reader
     migration is structurally complete) and T9 (FUNC virtual-property
     unification — `properties_map` is a regular map, all named).**
     Option A (full marker-scheme retirement via placeholder-sentinel
     on companion-map digit-string slots) is deferred — its payoff is
     deleting the marker scheme for indexed properties, which requires
     auditing every companion-map digit-string read path; small win
     relative to risk now that B unblocks the downstream work.
   - **AT-5 — REVISED.** With AT-4 Option B done, what remains is:
     (a) the marker-fallback codepath itself (still needed for indexed
     properties), and (b) any direct marker write sites that bypass
     `js_attr_set_*`. For T9 unblock specifically: nothing further from
     AT-5 is required since FUNC `properties_map` is named-only. AT-5
     can be retitled "deferred Option A work" and parked alongside
     T8b/T8d as low-priority structural cleanup.

2. **A3 — `JsClass` enum.** T1+T2 DONE: `JsClass` enum (50 entries covering all class names currently written via `__class_name__` in `lambda/js/`), `uint8_t js_class` byte on `TypeMap`, clone-aware writer (`js_class_set_for_map`), pure-inline reader (`js_class_get`), and a fast-path switch in `js_proto_class_method_dispatch` for Boolean/String/Array. Outstanding work (A3-T3+, multi-session):
   - **T3a — Stamp built-in prototypes at construction time.** For each class, find the prototype-creation site (e.g. Boolean.prototype in `js_globals.cpp`) and add `js_class_set_for_map(map, JS_CLASS_BOOLEAN)` next to the existing `js_property_set(..., "__class_name__", "Boolean")` write. Dual-write keeps legacy readers working.
   - **T3b — Stamp instances at construction time.** Constructor functions for `new Foo(...)` (e.g. `js_array_new`, `new Date()`, embedder ctors in `js_clipboard.cpp`/`js_events.cpp`/etc) get the same dual-write next to their `__class_name__` set.
   - **T3c — Migrate `js_proto_class_method_dispatch` Number/Date/RegExp branches** to the byte switch (legacy fallback can stay until T4 reader sweep).
   - **T4 — Reader migration.** Convert duck-type checks `js_property_get(it, "__class_name__")` → `js_class_get(it)` across builtins (~60 sites in js_runtime.cpp, ~40 in js_globals.cpp, the rest spread).
   - **T5 — Drop `__class_name__` writes**, then GC sweep on first cycle after deploy. Drop reader fallbacks last.

   Risk profile: T3 is purely additive (dual-write); T4 is the bulk; T5 is mechanical cleanup. Each phase verified by `make build` + Props gtest + test262 baseline.
3. **D — extended hidden-state audit.** Builtin function caches keyed by enum, name-pool reuse across batches. Convert remaining bare set/restore patterns (if any surface during A2) to the existing RAII guards.
4. **C — megafile splits.** Mechanical once A2 lands. Do `js_runtime.cpp` first along the boundaries listed below.
5. **Stage B harness expansion.** Add coverage rows for new kernels added during A2 (descriptor merge semantics, configurable→non-configurable transitions, accessor↔data conversions). Also add explicit FUNC-branch tests once A2 has unified the descriptor model.

## Motivation

After many rounds of test262 crash and regression triage we are not making net progress. Each fix tends to either:
- miss a parallel code path (recently: `js_proto_class_method_dispatch` had two call sites; neither honored the deleted sentinel — `delete Boolean.prototype.toString` then calling it stack-overflowed),
- leave a hidden invariant un-restored (recently: `js_delete_property` only canonicalized non-negative INT keys, so FLOAT/negative-INT keys bypassed marker + IS_ACCESSOR clearing → SIGSEGV at `0x400dead00dead08`), or
- pass on the focused script but produce batch-unstable behaviour (Boolean.prototype.toString tests passing in Phase-4 retry only).

These are not random bugs. They share root causes — too many parallel code paths for the same operation, identity encoded in magic strings and tagged sentinels, and no inner test loop fast enough to gate changes.

`lambda/js/` is currently ~105k lines, dominated by:

| File | Lines |
|---|---:|
| `transpile_js_mir.cpp` | 28,994 |
| `js_runtime.cpp` | 24,190 |
| `js_globals.cpp` | 12,436 |
| `build_js_ast.cpp` | 4,038 |
| `js_dom.cpp` | 4,947 |
| `js_buffer.cpp` | 2,475 |

This document proposes an incremental refactor — each step is independently shippable, validates against the existing test262 baseline, and pays off immediately.

---

## 1. Why we keep regressing — root causes

1. **Three megafiles carry 60% of the engine.** Concerns are interleaved; navigation depends on grep.
2. **Multiple parallel dispatch paths for the same operation.** Property `get`/`set`/`delete`/`has` each have 4–6 paths: own slot, IS_ACCESSOR pair, legacy `__get_X` marker, `__class_name__` builtin dispatch, proto fast-path, proxy trap. Bugs hide in the gaps.
3. **String-tagged class identity.** `__class_name__`, `__is_proto__`, `__sym_N`, `__private_X`, `__get_X`, `__primitiveValue__` are magic strings sharing the property namespace. Any code that walks shape entries sees them as ordinary properties. No type-system help.
4. **Sentinel and tagged-pointer hazards.** `JS_DELETED_SENTINEL_VAL` is encoded as `LMD_TYPE_INT` and overlaps the int domain. FLOAT/INT keys must be canonicalized at every entry point or the sentinel is misread. Each new code path must remember this.
5. **Hidden global mutable state.** `js_proxy_receiver`, `js_skip_accessor_dispatch`, builtin caches, name pool — order-dependent, leading to “batch-unstable” tests.
6. **test262 is the only validator.** A 12+ minute outer loop is too coarse and too slow to be the inner development loop. We learn about invariant violations after-the-fact.
7. **No spec citations in code.** Comments mention `ES §...`, but the abstract operations themselves (`OrdinaryGet`, `OrdinarySet`, `ToPropertyKey`, `OrdinaryDefineOwnProperty`) are not reified as functions. Each handler reimplements pieces of them, drifting over time.

---

## 2. Proposal — incremental, no big-bang rewrite

The plan is staged so each step is independently shippable. Steps 1–3 alone would, by inspection, prevent the majority of crash classes seen in recent triage.

### Stage A — Property model (highest ROI)

#### A1. `lambda/js/js_props.{h,cpp}` — abstract operations as the only primitives

Mirror ECMA-262 §7.3 / §10.1 directly:

```cpp
// Stable canonical key — INT / FLOAT / Symbol / numeric-string all funneled here
struct PropertyKey {
    enum class Kind : uint8_t { String, Symbol };
    Kind kind;
    const char* str;   // interned
    int len;
    Item symbol;       // when kind == Symbol
};
PropertyKey ToPropertyKey(Item arg);          // §7.1.19

Item   OrdinaryGet           (Item O, PropertyKey P, Item Receiver);
bool   OrdinarySet           (Item O, PropertyKey P, Item V, Item Receiver);
bool   OrdinaryDelete        (Item O, PropertyKey P);
bool   OrdinaryHasProperty   (Item O, PropertyKey P);
PropertyDescriptor OrdinaryGetOwnProperty (Item O, PropertyKey P);
bool   OrdinaryDefineOwnProperty(Item O, PropertyKey P, PropertyDescriptor D);
```

Every existing entry point (`js_property_get`, `js_property_set`, `js_delete_property`, `js_super_property_set`, proxy traps, `Object.defineProperty`, accessor dispatch in `js_prototype_lookup_ex`, etc.) becomes a thin wrapper that builds a `PropertyKey` and calls one abstract operation.

Deleted-sentinel handling, IS_ACCESSOR dispatch, `__class_name__` builtin lookup, and proxy `[[Get]]`/`[[Set]]` forwarding live in **one** place per operation. Today's Boolean `toString` fix and the prior FLOAT-key delete fix would have been one-line changes to a single function.

**Migration**: introduce alongside existing code, route one operation at a time, delete the displaced paths once green. Each routing PR is verified against the test262 baseline.

#### A2. `PropertyDescriptor` table replacing parallel sentinel/IS_ACCESSOR encodings

```cpp
struct PropertyDescriptor {  // 16 bytes
    Item value_or_pair;      // tagged: data value | JsAccessorPair*
    uint8_t flags;           // WRITABLE | ENUMERABLE | CONFIGURABLE | ACCESSOR | DELETED
    uint8_t reserved[7];
};
```

Stored densely per-shape, replacing:
- the side-channel `JSPD_IS_ACCESSOR` shape flag,
- the magic `JS_DELETED_SENTINEL_VAL` tagged-pointer encoding,
- the legacy `__get_<key>` / `__set_<key>` marker properties.

Eliminates the recurring "we forgot to clear IS_ACCESSOR" bug class entirely (today the flag and slot can desync; with a single descriptor record they cannot).

#### A3. `JsClass` enum replacing `__class_name__` strings

```cpp
enum class JsClass : uint8_t {
    None, Object, Boolean, Number, String, Array,
    Date, RegExp, Error, Map, Set, Promise,
    TypedArray, ArrayBuffer, DataView, Proxy, Generator
};
```

Stored as a fixed byte in the `Map` header (or `TypeMap`), not as a property. Class-method dispatch becomes a switch on enum:

```cpp
static Item dispatch_class_method(JsClass klass, PropertyKey key) { ... }
```

No `strncmp("Boolean", 7) == 0` chains, no `__class_name__` aliasing risk in user objects, no enumeration filters needed.

### Stage B — Property-model unit-test harness (the missing inner loop)

A GTest binary `test_js_props.exe` driving the abstract operations on hand-built objects, exercising the interaction matrix:

- own data | own accessor | own deleted | inherited data | inherited accessor | proxy trap
- × INT key | FLOAT key | string key | symbol key | numeric-string key | negative INT key
- × configurable on/off, writable on/off, enumerable on/off
- × strict-mode set | non-strict set | super-property set

Runs in <1s. Run on every commit. This is what's been missing — test262 is too coarse and too slow to be the inner loop. The last 9 bugs we shipped fixes for would each have tripped a row in this matrix.

### Stage C — Megafile splits along ES specification boundaries

`js_runtime.cpp` (24k → ~3k each):

| New file | Concerns |
|---|---|
| `js_props.cpp` | OrdinaryGet/Set/Delete/Has, descriptor logic |
| `js_proto_chain.cpp` | prototype walks, class-method dispatch |
| `js_call.cpp` | function call, apply/bind, super calls |
| `js_iter.cpp` | iterators, for-of, generators dispatch |
| `js_coerce.cpp` | ToString/ToNumber/ToObject/ToPrimitive |
| `js_class_dispatch.cpp` | class-method tables (Boolean/Number/Date/RegExp/...) |

`js_globals.cpp` (12k) — split per global, mirroring how DOM / Buffer / FS already split:

`js_object_global.cpp`, `js_array_global.cpp`, `js_string_global.cpp`, `js_typed_array_global.cpp`, `js_promise_global.cpp`, `js_reflect_global.cpp`, `js_proxy_global.cpp`, ...

`transpile_js_mir.cpp` (29k) — split by AST node category:

`mir_emit_expr.cpp`, `mir_emit_stmt.cpp`, `mir_emit_class.cpp`, `mir_emit_pattern.cpp`, `mir_emit_module.cpp`, plus `mir_emit_common.cpp` for shared lowering helpers.

These are mechanical splits, low risk once Stage A is done because the dispatch surface is small and well-defined.

### Stage D — Hidden-state audit

Audit `static`/extern globals in `js_runtime.cpp` and `transpile_js_mir.cpp`:

- `js_proxy_receiver`
- `js_skip_accessor_dispatch`
- builtin function caches keyed by enum
- name-pool reuse across batches

Move per-call state into a `JsExecContext*` argument, or wrap with stacked save/restore RAII (`ScopedProxyReceiver`, `ScopedSkipAccessorDispatch`). This eliminates the "batch-unstable" tests and the repeating cleanup-on-exception holes.

### Stage E — Debug-only invariant assertions

At every property operation entry, in debug builds:

```cpp
assert(key.kind == PropertyKey::Kind::String || key.kind == PropertyKey::Kind::Symbol);
assert(!js_is_deleted_sentinel(slot) || (desc.flags & DELETED));
assert(jspd_is_accessor(se) == (slot is JsAccessorPair*));
```

Any of the last 9 bugs shipped would have tripped at least one of these in development.

### Stage F — Spec-driven change discipline

Each PR touching property semantics cites the ES2020 abstract-operation step it implements/fixes. Bug fixes link to the test262 file *and* the spec section. The framing question changes from "what test passes now?" to "what invariant did we restore?".

---

## 3. Suggested order

| # | Stage | Risk | Payoff |
|--:|---|---|---|
| 1 | B — Property-model unit-test harness | none (additive) | inner-loop fix; immediate leverage |
| 2 | A1 — `ToPropertyKey` extraction; route every key entry through it | low | kills key-canonicalization bug class |
| 3 | A1 — `OrdinaryGet/Set/Delete/HasProperty` introduced; route incrementally | medium (gated by test262 + Stage B) | collapses parallel dispatch paths |
| 4 | A2 — `PropertyDescriptor` table | medium | eliminates IS_ACCESSOR / sentinel desync |
| 5 | A3 — `JsClass` enum | low | removes magic-string class identity |
| 6 | C — Megafile splits | low (mechanical, post-A) | navigability, per-file ownership |
| 7 | D — Hidden-state audit | medium | fixes batch-unstable tests |
| 8 | E + F — Assertions and spec discipline | none | locks in the gains |

Each stage exits when:
- the existing test262 baseline (`28851 fully-passing`, gate `regressions=0`, `crash-exits=0`) is preserved or improved, and
- the property-model GTest suite passes.

---

## 4. Concrete first deliverable — Stage B harness sketch

`test/test_js_props_gtest.cpp` (new) covers, at minimum:

```
TEST(Props, OwnDataGetSet)
TEST(Props, OwnAccessorGet_GetterDispatch)
TEST(Props, OwnAccessorSet_SetterDispatch)
TEST(Props, AccessorOnlyGetter_SetIsTypeError_Strict)
TEST(Props, AccessorOnlyGetter_SetIsSilent_NonStrict)
TEST(Props, DeleteThenGet_FallsThroughToProto)
TEST(Props, DeleteOnFloatKey_NoSentinelLeak)
TEST(Props, DeleteOnNegativeIntKey_NoSentinelLeak)
TEST(Props, DefineAccessorAfterDeletedAccessor_NoCrash)
TEST(Props, ClassMethodDispatch_HonorsOwnDeletedSentinel)
TEST(Props, ClassMethodDispatch_HonorsProtoDeletedSentinel)
TEST(Props, ConfigurableFalse_DefineThrows)
TEST(Props, WritableFalse_StrictSetThrows)
TEST(Props, NumericStringKey_EquivalentToIntKey)
TEST(Props, ProxyGetTrap_PreservesReceiver)
TEST(Props, SuperSet_FindsInheritedSetter)
```

Each test constructs a fresh `Map`, drives `OrdinaryGet/Set/Delete/Define`, and asserts both observable result and internal invariants (no leaked sentinels, no IS_ACCESSOR/slot desync). This file becomes the gatekeeper for Stage A changes.

---

## 5. Open questions

1. **Migration policy for legacy `__get_X` markers.** Plan: keep read compatibility during Stage A, refuse new writes after A2 lands, sweep on first GC after A2.
2. **`PropertyKey` storage for symbol keys.** Plan: intern via the existing name-pool path; symbols carry their unique name there already.
3. **MIR transpiler regeneration scope.** Stage C splitting `transpile_js_mir.cpp` may invalidate prebuilt MIR caches; not a correctness issue but mention in commit notes.
4. **Embedder API stability.** The C ABI exposed via `lambda/js/js_runtime.h` and `lambda-embed.h` must be preserved; refactor is internal.

---

## 6. Non-goals (explicit)

- No language-feature additions during the refactor.
- No JIT codegen redesign — `transpile_js_mir.cpp` split is mechanical.
- No performance-tuning during the refactor; performance is measured before/after each stage and any regression is treated as a blocker.
- No new dependencies.
