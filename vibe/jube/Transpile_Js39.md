# Transpile_Js39 — Structural Enhancements to Pass More test262

> Companion to [Transpile_Js38_Refactor.md](Transpile_Js38_Refactor.md). Js38
> rebuilt the property-model foundation (kernels, shape clone, JsClass enum,
> marker retirement). Js39 spends that capital on the **5,461 still-failing
> test262 tests** by closing the largest spec-gap clusters.

## 1. Baseline & failure landscape

Source: [test/js262/JS262_Test_Result2.md](../../test/js262/JS262_Test_Result2.md) (2026-04-27)
and [test/js262/JS262_Test_Result1.md](../../test/js262/JS262_Test_Result1.md).

| Metric | Value |
|---|---:|
| In-scope tests (ES2020) | 34,167 |
| Passing | 28,691 |
| Failing | **5,461** |
| Pass rate | 84.0% |

### Failure clusters ranked by absolute count

| Rank | Cluster | Failing | Theme |
|---:|---|---:|---|
| 1 | `language/statements/class` + `expressions/class` | **3,623** (incl. `dstr` ≈ 1,664, `elements` ≈ 1,180, async-gen-method ≈ 316) | Class destructuring params, private fields, async methods, super dispatch |
| 2 | `language/statements/for-await-of` | **1,142** | Async iteration + destructuring patterns |
| 3 | `language/expressions/dynamic-import` | **785** | `catch`/`usage`/`namespace` 0% |
| 4 | `built-ins/RegExp` (+ literals + annexB) | **~720** | Lookbehind, named groups, `/u`/`/v`, sticky, `lastIndex` |
| 5 | `built-ins/Promise` + `AsyncGenerator*` + `AsyncFunction` + `await-using` | **~700** | Microtask ordering, async generator protocol |
| 6 | `built-ins/TypedArray` + `TypedArrayConstructors` + `DataView` + `ArrayBuffer` | **~830** | Detach checks, `@@species`, byte-offset edge cases, BigInt views |
| 7 | `built-ins/Object` + `built-ins/Array` (non-iter) | **~810** | Property-descriptor enforcement, `defineProperty` semantics, `__proto__` |
| 8 | `language/expressions/object` + `compound-assignment` + `assignment` | **~310** | `Symbol.toPrimitive` ordering, computed keys + `__proto__` + spread |
| 9 | `built-ins/Atomics` + `SharedArrayBuffer` | **~270** | Entire feature missing |
| 10 | `built-ins/Iterator` + `Uint8Array` (helpers) | **~580** | ES2025 iterator-helpers (mostly skipped, but a fraction is in-scope) |
| 11 | `annexB/*` (eval-code, function-code, global-code) | **~345** | B.3.3 hoisting, sloppy-mode function decls |
| 12 | `language/statements/with` | **78** | Mostly unimplemented |
| 13 | `built-ins/Reflect` + `Proxy` | **~130** | Invariant checks, trap forwarding |
| 14 | `built-ins/String` + `Date` + `BigInt` | **~430** | `Symbol.toPrimitive`, ISO parse, BigInt coercion |

### Cross-cutting engineering gaps

Distilled from Result2 §"Major underlying engineering gaps":

1. **`OrdinaryToPrimitive` / `Symbol.toPrimitive`** — visible across Object,
   Array, String, Date, BigInt, addition, compound-assignment (~1,200+ fails).
2. **RegExp ES-semantics shim over RE2** — named groups, lookbehind, `/u`,
   sticky, strict `lastIndex` (~690 fails).
3. **TypedArray / DataView / ArrayBuffer detach + species** (~830).
4. **Class semantics** — private static, async methods, super edge cases (~610).
5. **AnnexB B.3.3 hoisting** + restricted-properties on strict functions (~265).
6. **Per-iteration `let`/`const` binding** in `for/for-in/for-of` (~107).
7. **`with` statement** (~81).
8. **Atomics / SharedArrayBuffer** (~196).
9. **Promise / microtask ordering**, async-generator protocol (~700 if you
   count the for-await-of collateral).
10. **Reflect / Proxy invariants** (~94).

## 2. Why a structural enhancement (not point fixes)

Js38 closed the property-model bug-class. The remaining 5,461 are not bugs in
existing features — they are **missing or partially-implemented spec
machinery**. Point-fixing (one regex feature, one TypedArray method) repeats
the Js38-era mistake of parallel paths drifting. Each of the gaps above is a
**single ES abstract operation or single subsystem** whose absence radiates
into hundreds of tests.

The high-ROI lever is to identify the abstract operation that gates a cluster
and implement it once, in one place, with a Stage-B-style harness as the
inner loop — exactly the discipline Js38 established for `Ordinary*`.

## 3. Phases (priority order, each independently shippable)

Each phase's exit criteria: `make build` clean, `Props.*` gtest 20/20,
test262 baseline `regressions=0`, and a measured improvement in the cluster
the phase targets (recorded in commit message). Run twice for batch stability.

### Phase J39-1 — `OrdinaryToPrimitive` plumbing (fast win, ~600+ tests)

**Spec**: ES §7.1.1 ToPrimitive, §7.1.1.1 OrdinaryToPrimitive,
`Symbol.toPrimitive`, `valueOf`, `toString` order.

**Scope**:
- Single `js_to_primitive(Item, Hint)` kernel in `lambda/js/js_coerce.cpp`
  (new file). Hint ∈ `{default, number, string}`.
- Steps: lookup `@@toPrimitive` exotic method first; if absent, ordered
  `valueOf`/`toString` (or reversed for `string` hint).
- Route through `js_props.h` accessor dispatch — Js38 already centralized
  this.
- Audit and reroute every coercion site:
  - Arithmetic operators (`+`, `-`, `*`, …) in `js_runtime.cpp`,
  - Compound assignment in `transpile_js_mir.cpp`,
  - `String(x)`, `Number(x)`, `BigInt(x)` constructors,
  - Date constructor + `Date.prototype.toJSON`,
  - `Array.prototype.join` element coercion,
  - JSON.stringify replacer + value coercion,
  - Property-key coercion already routed (Js38-A1 `ToPropertyKey`).

**Inner-loop harness**: extend `test_js_props_gtest.cpp` with a
`Coerce.*` suite (~20 tests) covering all 6 hint × class combinations and
the spec error paths.

**Estimated unlock**: ~600–900 tests across `expressions/compound-assignment`
(~112), `Date` (~109), `String` (~145), `BigInt` (~45), `expressions/object`
spread/computed (~76), portions of `Object`/`Array` descriptor tests.

### Phase J39-2 — Per-iteration lexical binding for `for`/`for-in`/`for-of` (✅ already implemented)

**Status (verified 2026-04-30 during J39-1 follow-up)**: the engine already
allocates a fresh per-iteration env for `let`/`const` in C-style `for`,
`for-in`, and `for-of`. All canonical `*_fresh_binding_per_iteration_js`
test262 tests pass; the closure-array test
`for (let i = 0; i < 3; i++) fns.push(() => i)` returns `[0,1,2]`.

The ~127 remaining failures in `for-of` (113) and `for-in` (14) are not
binding-environment issues — they are:

- **Iterator close protocol** edge cases (`iterator-close-non-object`,
  `generator-close-via-*`, return-on-throw close) — folded into J39-7 sweep.
- **TDZ-in-head destructuring** (`scope-body-lex-open` etc.) — small subset
  (~6 tests), folded into J39-7.
- **`await using` / `using` (ES2024)** — out of scope (Js40).
- **For-await-of** body — owned by J39-5.

**Action**: no work in this phase; original ~110 estimate counted toward the
J39-7 sweep.

### Phase J39-3 — Class semantics completion (~600 tests, largest cluster)

**Spec**: ES §15.7 Class definitions, §10.2 ECMAScript Function Objects.

This is the single largest cluster. Sub-phases:

| Sub | Scope | Est. unlock |
|---|---|---:|
| **3a** | **Destructuring parameters in class methods** (`dstr` cluster). Reuse function-param destructuring path in `mir_emit_pattern.cpp`; the failures are uniform: dstr tests instantiate a class then call a method with a destructured arg. Single bind-site fix expected. | ~800 |
| **3b** | **Private fields edge cases**: `#x in obj`, private static, brand-check on inherited access, `delete this.#x` early-error. Build on existing private-fields impl ([per `private-fields-implementation.md`](../../memories/repo/private-fields-implementation.md)). | ~250 |
| **3c** | **`super` property set/get on derived classes** + super-call ordering with `new.target`. | ~80 |
| **3d** | **Accessor/method name resolution** (`accessor-name-inst/static`, computed names). | ~40 |
| **3e** | **Subclass-builtins** (extending `Array`, `Error`, `RegExp` correctly via `@@species` + `OrdinaryCreateFromConstructor`). | ~50 |

Async / async-generator class methods are punted to Phase J39-5.

**Inner-loop harness**: new `test_js_class_gtest.cpp` mirroring Stage B —
construct each pattern programmatically, assert observable state.

### Phase J39-4 — RegExp ES-semantics shim over RE2 (~690 tests)

RE2 is a fixed dependency (no backtracking, no lookbehind). The shim layer in
`lambda/js/js_regex.cpp` translates ES regex → RE2-compatible form and
post-processes match results. Sub-phases:

| Sub | Scope | Est. unlock |
|---|---|---:|
| **4a** | **Named capture groups** `(?<name>…)` — RE2 supports natively; thread group names through `RegExpExec` result, `String.prototype.replace` with `$<name>`. | ~200 |
| **4b** | **Sticky `y` flag** + strict `lastIndex` advancement (already partially in [`regex-lastindex-strict-set.md`](../../memories/repo/regex-lastindex-strict-set.md)). | ~120 |
| **4c** | **Unicode `/u` semantics** — surrogate-pair handling in character classes; `\u{…}` codepoint escapes. RE2 supports UTF-8 mode; need preprocessing pass. | ~250 |
| **4d** | **Lookbehind `(?<=…)` / `(?<!…)`** — RE2 *does not* support. Detect at compile, fall back to a hand-rolled JS-only matcher for the lookbehind subset (no backrefs, no nested lookbehind). | ~120 |

**Out of scope**: backreferences, full lookahead with backrefs (not supportable
on RE2; quantify deferred count for a future Phase J40).

**Inner-loop harness**: `test_js_regex_gtest.cpp` with golden-tested
Pattern × Input × Result tuples per sub-phase.

### Phase J39-5 — Async iteration & microtask ordering (~700 tests)

**Spec**: ES §27 Control Abstraction, §14.7.5 ForIn/OfStatement async path,
§25.5 AsyncGenerator.

**Scope**:
- **5a** `for-await-of` desugaring: async-iterator protocol
  (`@@asyncIterator` → `next()` → `await` value → `done` check). Currently
  ~7% pass rate is the dominant failure surface.
- **5b** Async generator state machine in `transpile_js_mir.cpp` —
  suspend/resume queue, `next/throw/return` dispatch, internal slot
  management. Builds on existing generator infra.
- **5c** Microtask queue ordering — `Promise.then` callbacks must drain in
  FIFO before the next macrotask. Audit `js_event_loop.cpp` to ensure the
  microtask phase is exhausted between every macrotask.
- **5d** `await` in non-async contexts → SyntaxError; `await` operand
  promise resolution must use the spec `PromiseResolve` (handles thenables).

**Estimated unlock**: ~589 in for-await-of async-gen iteration + ~134 in
for-await-of dstr (Phase 2 dependency) + ~161 in `built-ins/Promise` + ~46
in `built-ins/AsyncGenerator*` ≈ **~900** combined with Phase J39-2.

### Phase J39-6 — TypedArray/ArrayBuffer detach + species (~830 tests)

**Spec**: ES §25.1 ArrayBuffer, §25.2 SharedArrayBuffer (in-scope subset),
§23.2 TypedArray, `@@species` (§7.3.22).

**Scope**:
- **6a** `IsDetachedBuffer` check at every TypedArray accessor and DataView
  read/write site. Single helper, route every entry. Throw `TypeError` per
  spec.
- **6b** `@@species` lookup for `TypedArray.prototype.{slice,subarray,map,filter}`
  and `ArrayBuffer.prototype.slice`.
- **6c** Byte-offset / byte-length validation: alignment per element-type,
  bounds, integer overflow (`toIntegerOrInfinity` exact behavior).
- **6d** BigInt-typed arrays (`BigInt64Array`, `BigUint64Array`) — DataView
  `getBigInt64`/`setBigInt64` already partially in place.

**Inner-loop harness**: `test_js_typedarray_gtest.cpp`.

**Estimated unlock**: ~830.

### Phase J39-7 — Object/Array property-descriptor remainder (~400 tests)

After Js38 closed the kernel, the remaining `Object`/`Array` failures are
narrower:
- `Object.defineProperties` argument validation order (descriptor extraction
  before any define),
- `__proto__` setter on object literal (only first occurrence per spec),
- `Array.from` with iterator that throws mid-iteration (close protocol),
- `Array.prototype.{flat,flatMap,copyWithin,fill}` `toIntegerOrInfinity`
  edge cases,
- Sparse array hole semantics in `Array.prototype.{forEach,map,filter,
  reduce}`.

These are per-method audits, not a structural change. Bundle as a sweep.

### Phase J39-8 — AnnexB B.3.3 hoisting + restricted properties (~265 tests)

**Spec**: ES §B.3.3 Block-Level Function Declarations Web Legacy, §B.3.7
`__proto__` Property in Object Initializers.

**Scope**:
- Sloppy-mode block-scoped function declarations create both block-scope
  binding and var-scope binding when name doesn't conflict.
- `arguments` / `caller` poisoned getters on strict-mode functions.
- Indirect-eval AnnexB hoisting (deferred — capture-analyzer rework, may stay
  in Js40).

**Estimated unlock**: ~200 (eval-code AnnexB punted).

### Phase J39-9 — Reflect / Proxy invariant checks (~130 tests)

**Spec**: ES §10.5 Proxy Object Internal Methods.

Audit each trap forwarder for spec invariants:
- `[[GetPrototypeOf]]` consistency on non-extensible target,
- `[[Get]]`/`[[Set]]` non-configurable non-writable agreement,
- `[[OwnKeys]]` must return all non-configurable own keys of target.

Self-contained, well-bounded.

### Phase J39-10 — `with` statement (~78 tests)

Implement scoped object-property lookup for `with(obj) { … }`:
Insert a synthetic `WithObjectEnvironment` in the lexical chain at compile
time; identifier resolution consults it via `HasProperty` before the outer
scope.

Low-strategic-value but well-bounded; complete the language.

### Deferred to Js40 (out-of-scope here)

- **Atomics / SharedArrayBuffer** (~270) — requires shared-memory threading
  model not present today.
- **Dynamic import `catch`/`usage`/`namespace`** (~600+) — requires module
  loader rework; partially blocked on ES-module stabilization.
- **AnnexB indirect-eval hoisting** (~120) — capture-analyzer rework.
- **`Iterator` helpers / `Uint8Array.fromBase64` etc.** — ES2025 stage 3,
  many already excluded by feature-flag skip.

## 4. Suggested execution order

Ordered by **(unlock count) / (engineering risk)**:

| # | Phase | Risk | Est. unlock | Cumulative |
|--:|---|---|---:|---:|
| 1 | **J39-1** ToPrimitive plumbing | low | ~700 | 29,400 (86.0%) |
| 2 | **J39-2** Per-iter `let`/`const` | low | ~110 | 29,510 |
| 3 | **J39-3a** Class dstr params | medium | ~800 | 30,310 (88.7%) |
| 4 | **J39-7** Object/Array remainder sweep | low | ~400 | 30,710 |
| 5 | **J39-4** RegExp shim (4a/4b first) | medium | ~320 | 31,030 |
| 6 | **J39-3b–3e** Class remainder | medium | ~420 | 31,450 (92.0%) |
| 7 | **J39-6** TypedArray/buffer detach + species | medium | ~830 | 32,280 |
| 8 | **J39-5** Async iteration + microtask | high | ~900 | 33,180 (97.1%) |
| 9 | **J39-4c–4d** RegExp Unicode + lookbehind | medium | ~370 | 33,550 |
| 10 | **J39-8** AnnexB B.3.3 | medium | ~200 | 33,750 |
| 11 | **J39-9** Reflect/Proxy invariants | low | ~130 | 33,880 |
| 12 | **J39-10** `with` statement | low | ~78 | 33,958 (99.4%) |

Realistic ceiling under ES2020 scope (after Js40 dependencies remain): **~33,500
passing, ~98% pass rate**. Items 8 and 9 carry execution risk and may slip.

## 5. Inner-loop discipline (continuation of Js38 Stage B)

Each phase MUST land its harness file *before* the implementation work, and
the harness must capture the failing behavior *before* fixing it (red→green
discipline). Files to add, mirroring `test_js_props_gtest.cpp`:

| Phase | Harness |
|---|---|
| J39-1 | `test/test_js_coerce_gtest.cpp` (`Coerce.*`) |
| J39-3 | `test/test_js_class_gtest.cpp` (`Class.*`) |
| J39-4 | `test/test_js_regex_gtest.cpp` (`Regex.*`) |
| J39-5 | `test/test_js_async_gtest.cpp` (`Async.*`) |
| J39-6 | `test/test_js_typedarray_gtest.cpp` (`TypedArray.*`) |

These harnesses run in <1s each, are run on every commit by `make build-test`,
and become the gate for any change in their respective subsystem — keeping
the test262 outer loop (~12 min) off the inner loop.

## 6. Spec-citation discipline (continuation of Js38 §F)

Every PR cites the ES2020 abstract-operation step it implements. Each new
file under `lambda/js/` for Js39 carries a header block listing the spec
sections it implements, e.g.:

```cpp
// lambda/js/js_coerce.cpp
// Implements:
//   ES §7.1.1  ToPrimitive
//   ES §7.1.1.1 OrdinaryToPrimitive
//   ES §7.1.4  ToNumber       (relevant fragments)
//   ES §7.1.17 ToString       (relevant fragments)
```

This is the lever that converts "what test passes now" into "what invariant
did we restore" — the framing Js38 adopted and that needs to carry forward.

## 7. Non-goals

- No new dependencies (RE2 stays the only regex backend; Atomics deferred).
- No JIT-codegen redesign — `transpile_js_mir.cpp` extensions are additive.
- No performance regressions tolerated — release-build perf measured before
  and after each phase; any regression is a blocker (per Js38 discipline).
- No baseline shrinkage — `test262_baseline.txt` may only grow.
- No ES2021+ features beyond what already creeps in for free (`logical
  assignment`, `numeric separators`, `nullish coalescing` already work).

## 8. Open questions

1. **Async-generator state machine**: persist via heap-allocated frame
   (V8-style) or via continuation-passing transform in
   `transpile_js_mir.cpp`? Recommend frame approach — symmetric with the
   sync generator implementation already in place.
2. **RegExp lookbehind fallback matcher**: ship as separate file
   `js_regex_es_matcher.cpp` (~500 LOC) or reuse a tiny third-party header
   like `regex_es.h`? Recommend the former — keeps zero-dependency
   posture.
3. **TypedArray species cache**: cache `@@species` lookup per constructor on
   first use (V8-style species protector cell)? Worth ~5% on
   `slice`-heavy benchmarks. Defer to a follow-up perf phase.
4. **Microtask queue**: single FIFO or per-realm? Single is sufficient for
   ES2020 (no realms) and matches current `js_event_loop.cpp` shape.

---

**Summary**: Js38 made the engine refactorable. Js39 spends that capital
on six well-bounded spec-completion phases (ToPrimitive, per-iteration
binding, class destructuring, RegExp shim, async iteration, TypedArray
detach/species), each with its own Stage-B-style harness, projected to
move pass-rate from **84.0% → ~98%** under the ES2020 scope.
