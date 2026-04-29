# Transpile_Js38 — Engine Refactor for Robustness, Modularity & Regression Safety

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
