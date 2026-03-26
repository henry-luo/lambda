# LambdaJS PDF.js: Feasibility Assessment & Implementation Plan

## 1. Executive Summary

**Goal:** Run Mozilla's PDF.js library under the LambdaJS runtime to enable native PDF
parsing and rendering in Lambda, without embedding a separate JS engine.

**Verdict: Feasible with focused effort. 0 critical gaps remaining (down from 6).**

PDF.js is a 120+ file, zero-dependency JavaScript library that parses and renders PDF
documents. Its core parsing engine (`src/core/`) can run without browser APIs by using
a "fake worker" fallback path already built into the codebase.

**Update (2025-07-21):** Phases 0–3 all complete: primitives baseline (33/33),
ArrayBuffer/DataView (43/43), private fields (23/23), minor polyfills (27/27 assertions
in 1 test, 690/690 baseline). This closes 5 of the original 6 gaps (generators, fetch,
microtasks, ArrayBuffer/DataView, private fields) plus all trivial polyfills
(Promise.withResolvers, String.match/matchAll, TextEncoder/TextDecoder, WeakMap/WeakSet,
Math hyperbolic functions). ES modules are bypassed via bundling.

**Update (2025-07-21):** Phase 5 complete: async/await synchronous fast-path fully
implemented (14/14 assertions, 690/690 baseline). `async function` transpiles to
Promise-returning function with implicit try/catch, `await` synchronously unwraps
resolved promises, rejected promises propagate via exception mechanism. Async arrow
functions also supported.

**Update (2025-07-26):** Phase 6 complete: full async state machine for pending promises
(10/10 assertions, 14/14 Phase 5 backward-compatible, 690/690 baseline). Async
functions with `await` are now transformed into resumable coroutines that suspend on
pending promises and resume when they resolve. Reuses the generator state machine
infrastructure (env save/load, state dispatch). The only remaining work is Phase 7
(test convergence against PDF.js CLI specs).

### Feasibility Score (Updated)

| Dimension | Score | Notes |
|-----------|:-----:|-------|
| **Core parsing (no rendering)** | 9.5/10 | All critical gaps closed; full async/await with pending promise support |
| **Full library (parse + render)** | 6/10 | Canvas/Worker dependencies still add layers |
| **Test pass rate potential** | 9/10 | CLI tests (57 specs) are the right target |
| **Implementation effort** | Low | Only test convergence (Phase 7) remains |

---

## 2. PDF.js Architecture Overview

### 2.1 Module Structure

```
pdf.js/src/
├── core/          ~60 files — PDF parsing engine (streams, fonts, images, XFA)
├── display/       ~40 files — Rendering API, worker bridge, canvas/SVG output
├── shared/        ~7 files  — Utilities (util.js, message_handler.js)
├── scripting_api/  ~15 files — Acrobat JavaScript scripting (optional)
├── pdf.js         — Public API entry
├── pdf.worker.js  — Worker entry
└── pdf.sandbox.js — Script sandbox
```

### 2.2 What We Need vs What We Can Skip

| Component | Required? | Reason |
|-----------|:---------:|--------|
| `src/core/` | **Yes** | PDF parsing (fonts, streams, images, XFA) |
| `src/shared/` | **Yes** | Utilities used everywhere |
| `src/display/api.js` | **Yes** | Public API surface |
| `src/display/canvas.js` | Skip initially | Can output to Lambda's Radiant SVG instead |
| `src/display/` (workers) | Skip | Use fake-worker path (already built-in) |
| `src/scripting_api/` | Skip | Acrobat JS scripting; uses Proxy/Reflect |
| `web/` | Skip | Viewer UI; pure DOM/browser |

### 2.3 Key Design Advantage

PDF.js already supports **running without Web Workers** via a fake-worker fallback:

```javascript
// src/display/api.js — Node.js forces workers off
if (isNodeJS) {
  this.#isWorkerDisabled = true;
}
```

This means core parsing runs synchronously on the main thread, which aligns perfectly
with LambdaJS's current synchronous execution model.

---

## 3. Gap Analysis: LambdaJS vs PDF.js Requirements

### 3.1 Critical Gaps (Blocking — Must Implement)

#### Gap 1: Async/Await — ✅ CLOSED (Phase 5 sync fast-path + Phase 6 full state machine)

**PDF.js usage:** 100+ `await` expressions across `src/core/`. Async is architecturally
fundamental — PDF page loading, font resolution, image decoding, and stream operations
are all async.

```javascript
// src/core/document.js — deeply embedded, not removable
const content = await this.pdfManager.ensure(this, "content");
const bytes = await content.asyncGetBytes();
await Promise.all(promises);
```

**LambdaJS status:** ✅ **Fully implemented** (Phase 5 sync fast-path 2025-07-21,
Phase 6 full state machine 2025-07-26).

**Phase 5 (sync fast-path):** `async function` transpiles to Promise-returning function.
`await` calls `js_await_sync()` which unwraps resolved promises inline, throws on
rejected, passes through non-promise values. Bodies wrapped in implicit try/catch.
Return values wrapped in `Promise.resolve()`. Used for async functions with no awaits
or only immediately-resolved awaits. 14/14 assertions pass.

**Phase 6 (full state machine):** Async functions containing `await` expressions are
transformed into resumable coroutines using the generator state machine infrastructure.
Each `await` is a conditional suspension point — resolved promises continue immediately
(fast path), pending promises save local state to env, return `[promise, next_state]`,
and resume when the promise settles via `.then()` callbacks. 10/10 assertions pass.

**What was implemented (Phase 6):**
- Runtime: `JsAsyncContext` struct, `js_async_must_suspend()` (check pending),
  `js_async_get_resolved()` (fast-path cache), `js_async_context_create()`,
  `js_async_start()`, `js_async_get_promise()`, `js_async_drive()` (core driver),
  `js_async_resume_handler()` / `js_async_reject_handler()` (promise callbacks)
- Transpiler: `jm_count_awaits()`, async state machine generation (mirrors generator
  SM with implicit try/catch), conditional suspend in await expression handler,
  async wrapper function (env alloc → context create → start → return promise)
- Microtask integration: `js_microtask_flush()` in resolve/reject callbacks
- Rejection handling: re-enters state machine with exception set (enables try/catch)

**No remaining work.** Both sync fast-path and full state machine are complete.

---

#### Gap 2: ES Modules — import/export (🔴 CRITICAL)

**PDF.js usage:** 100% ES modules. Every file uses `import`/`export`. No CommonJS.

```javascript
// Every single file in src/core/
import { Dict, Ref, RefSet } from "./primitives.js";
export { Page, PDFDocument };
```

Dynamic imports also used:
```javascript
await import("./internal_viewer_utils.js");
```

**LambdaJS status:** AST nodes defined (v14). Tree-sitter parses import/export.
**No module linking/loading logic implemented.**

**Required work:**
- Module resolver: resolve `"./primitives.js"` to filesystem path
- Module loader: parse, transpile, and cache each module
- Module linker: connect exports from one module to imports in another
- Handle re-exports (`export { X } from "./other.js"`)
- Handle dynamic `import()` (returns Promise of module namespace)
- Circular dependency handling (PDF.js has some)

**Estimated complexity:** HIGH — Module systems are notoriously tricky, especially with
circular dependencies. However, PDF.js has a clean dependency graph and the module
spec is well-defined.

**Strategy:** Bundle-first approach:
- **Option A (Recommended):** Pre-bundle PDF.js with a tool (esbuild/rollup) into a
  single file with no import/export. LambdaJS runs the bundle directly. This
  eliminates the module gap entirely for this specific use case.
- **Option B:** Implement a basic module loader that handles the subset PDF.js uses
  (named imports/exports, re-exports, no bare specifiers).

---

#### Gap 3: Private Class Fields — `#field` — ✅ CLOSED (Phase 2)

**PDF.js usage:** 100+ private fields across core files. Pervasive in data structures.

**LambdaJS status:** ✅ **Fully implemented** (2025-07-21). AST-level `#field` →
`__private_field` transform, instance field initialization with inheritance chain walk,
static private fields via module variables, `#field in obj` operator support.
23/23 test cases pass.

**What was implemented:**
- AST transform: `#field` → `__private_field` in `build_js_expression()`
- Instance field collection and initialization at `new ClassName()` call site
- Inheritance chain walk (base-first) for correct field init order
- Disabled A5/P3/P4 shaped slot optimization for classes with instance fields
- Static `#field++`/`--` write-back via `js_set_module_var`
- `#field in obj` compiled as string literal key lookup

---

#### Gap 4: ArrayBuffer / DataView — ✅ CLOSED (Phase 1)

**PDF.js usage:** Core binary data handling. PDF files are binary → ArrayBuffer is
the primary container.

```javascript
const buffer = new ArrayBuffer(length);
const view = new DataView(buffer);
view.getUint16(offset, /* littleEndian */ true);
```

**LambdaJS status:** ✅ **Fully implemented** (2025-07-20). ArrayBuffer, DataView,
and TypedArray↔ArrayBuffer bridge all working.

**What was implemented:**
- `ArrayBuffer`: Constructor, `.byteLength`, `.slice()`, `ArrayBuffer.isView()`
- `DataView`: Constructor wrapping ArrayBuffer, 16 getter/setter methods
  (`getInt8`–`getFloat64` + `setInt8`–`setFloat64`) with endianness support
- TypedArray from ArrayBuffer: `new Uint8Array(buffer, offset, length)` — shared view
- TypedArray from TypedArray: copy semantics (independent data)
- `.buffer`, `.byteOffset`, `.byteLength` properties on TypedArrays and DataViews
- `.BYTES_PER_ELEMENT` on TypedArrays
- TypedArray `.slice()`, `.subarray()`, `.set()` methods
- Smart constructor: `new Uint8Array(number|ArrayBuffer|TypedArray|Array)`

**Architecture:** Shared buffer model — `JsArrayBuffer` owns the `calloc`'d memory,
TypedArrays and DataViews hold a pointer to the buffer plus byte offset. TypedArray
`data` pointer stays at struct offset 16, preserving all existing MIR inline codegen.

---

#### Gap 5: Generator Functions / yield — ✅ CLOSED (v15)

**PDF.js usage:** 19 occurrences. Used for iterating PDF objects, especially in
`Dict`, `RefSetCache`, and XFA tree traversal.

**LambdaJS status:** ✅ **Fully implemented in v15 Phase 8** (2025-07-14).
Complete state machine transform in transpiler: yield prescan, env save/load,
state dispatch via EQS/BT chain, boxed wrapper. Runtime: `js_gen_yield_result`,
`js_is_generator`, `js_iterable_to_array`, generator method dispatch
(`.next`, `.return`, `.throw`). 8 test cases pass.

**No remaining work.** Generator functions, `yield`, `yield*`, and `for...of`
over generators all work correctly.

---

#### Gap 6: Promise.withResolvers — ✅ CLOSED (Phase 3)

**PDF.js usage:** 33 occurrences. Modern convenience API.

```javascript
class WorkerTask {
  _capability = Promise.withResolvers();
}
```

**LambdaJS status:** ✅ **Fully implemented** (Phase 3, 2025-07-21). Returns
`{ promise, resolve, reject }` object with bound resolve/reject callbacks via
`js_bind_function`. Part of the Phase 3 minor polyfills batch.

**No remaining work.**

---

### 3.2 Minor Gaps (Non-Blocking for Core Parsing)

| Feature | PDF.js Usage | Impact | Workaround |
|---------|:------------:|:------:|------------|
| `WeakMap` | Display layer caching | Low | ✅ Aliased to Map (Phase 3) |
| `WeakRef` / `FinalizationRegistry` | 3 places, all optional | None | Remove or stub |
| `Proxy` / `Reflect` | Only in `scripting_api/` (skipped) | None | N/A |
| `structuredClone` | 1 place in editor (skipped) | None | N/A |
| `fetch()` API | Network loading | None | ✅ Implemented in v15 Phase 5 |
| `TextEncoder` / `TextDecoder` | String encoding | Low | ✅ Implemented (Phase 3) |
| `String.match/matchAll` | Regex usage in parsing | Low | ✅ Implemented (Phase 3) |
| `Object.getOwnPropertyDescriptor` | Scripting API (skipped) | None | N/A |
| `Object.defineProperty` (full) | Scripting API (skipped) | None | Partial implementation exists |
| `setTimeout` / `setInterval` | Testing timeouts | Low | Already implemented in v15 |
| `class` static blocks | Not used in core | None | N/A |

### 3.3 Feature Compatibility Matrix

| Feature | LambdaJS | PDF.js Core | PDF.js Display | Status |
|---------|:--------:|:-----------:|:--------------:|:------:|
| Variables (var/let/const) | ✅ | ✅ | ✅ | Ready |
| Arrow functions | ✅ | ✅ | ✅ | Ready |
| Classes + inheritance | ✅ | ✅ | ✅ | Ready |
| Template literals | ✅ | ✅ | ✅ | Ready |
| Destructuring | ✅ | ✅ | ✅ | Ready |
| Spread/rest | ✅ | ✅ | ✅ | Ready |
| Optional chaining (`?.`) | ✅ | ✅ | ✅ | Ready |
| Nullish coalescing (`??`) | ✅ | ✅ | ✅ | Ready |
| for...of / for...in | ✅ | ✅ | ✅ | Ready |
| Map / Set | ✅ | ✅ | ✅ | Ready |
| Symbol | ✅ | ✅ | — | Ready |
| TypedArrays (8 types) | ✅ | ✅ | ✅ | Ready |
| RegExp (basic) | ✅ | ✅ | ✅ | Ready |
| JSON | ✅ | ✅ | ✅ | Ready |
| Error subclasses | ✅ | ✅ | ✅ | Ready |
| Getters/setters | ✅ | ✅ | ✅ | Ready |
| `instanceof` | ✅ | ✅ | ✅ | Ready |
| Promise (basic) | ✅ | ✅ | ✅ | Ready |
| **async/await** | ✅ | ✅ | ✅ | ~~Gap 1~~ ✅ CLOSED (Phase 5 sync + Phase 6 state machine) |
| **ES modules** | ❌ | ✅ | ✅ | **Gap 2** (bypassed by bundling) |
| **Private fields (#)** | ✅ | ✅ | ✅ | ~~Gap 3~~ ✅ CLOSED (Phase 2) |
| **ArrayBuffer/DataView** | ✅ | ✅ | ✅ | ~~Gap 4~~ ✅ CLOSED (Phase 1) |
| **Generators/yield** | ✅ | ✅ | — | ~~Gap 5~~ ✅ CLOSED (v15) |
| **Promise.withResolvers** | ✅ | ✅ | ✅ | ~~Gap 6~~ ✅ CLOSED (Phase 3) |

---

## 4. Recommended Approach

### 4.1 Target: PDF.js CLI Unit Tests (57 specs)

PDF.js has a dedicated Node.js CLI test runner (`gulp unittestcli`) that runs 57 spec
files covering core parsing, primitives, streams, fonts, crypto, and utilities — all
without browser/DOM dependencies. This is the ideal first target.

### 4.2 Pre-Processing: Bundle PDF.js

Use **esbuild** or **rollup** to pre-bundle PDF.js into a single file. This:
- Eliminates the ES module gap (Gap 2) entirely
- Resolves circular dependencies
- Produces a flat script LambdaJS can execute directly
- Keeps the effort focused on runtime features, not module loading

```bash
# Example: Bundle PDF.js core for Lambda
esbuild src/pdf.js --bundle --format=iife --outfile=pdf.bundle.js \
  --external:canvas --platform=neutral
```

After bundling, the test plan targets the remaining gaps (1, 3–4, 6) in the runtime.
Gap 5 (generators) was closed by v15 Phase 8.

### 4.3 Phased Implementation Plan (Updated)

```
Phase 0: Bundle + Baseline          ─── ✅ DONE
Phase 1: ArrayBuffer/DataView       ─── ✅ DONE
Phase 2: Private Fields Polish      ─── ✅ DONE
Phase 3: Promise.withResolvers      ─── ✅ DONE
Phase 4: Generators (basic)         ─── ✅ DONE (v15)
Phase 5: Async/Await (sync fast)    ─── ✅ DONE
Phase 6: Async/Await (full)         ─── ✅ DONE
Phase 7: Test Convergence           ─── Next
```

---

## 5. Implementation Plan — Detailed Phases

### Phase 0: Bundle + Baseline (Weeks 1–2)

**Goal:** Get PDF.js loading and failing with identifiable errors.

**Status: ✅ COMPLETE** (2025-07-19)

**Approach taken:**
1. Bundled PDF.js primitives (`src/core/primitives.js` + `src/shared/util.js`) with esbuild
   using `--format=esm` (ESM format avoids IIFE closure capture issues)
2. Created standalone test harness with 33 assertions covering: Name, Cmd, Dict, Ref,
   RefSet, RefSetCache — all core PDF.js primitive types
3. Iteratively fixed LambdaJS runtime issues discovered during testing

**Bugs found and fixed in LambdaJS:**
- **Class expression alias:** `var X = class _Y {}` — `new X(...)` couldn't find class `_Y`.
  Fixed by adding `alias_name` to `JsClassEntry` and checking it in `jm_find_class`.
- **Anonymous class naming:** `var X = class {}` — anonymous classes had NULL name,
  making methods unresolvable. Fixed by propagating variable name to class entry.
- **`void 0` semantics:** `void 0` returned `null` instead of `undefined`.
  Fixed `JS_OP_VOID` to return `ITEM_JS_UNDEFINED`.
- **Missing argument default:** Unpassed function arguments defaulted to `null` instead
  of `undefined` (JS semantics). Fixed in 3 code paths: inline, native, and direct call.
- **Getter/setter parsing:** `get size()` in class methods was not parsed. Added
  getter/setter detection in `build_js_method_definition()` and `__get_` prefix
  convention for property installation.
- **`instanceof` for class expressions:** Class expressions now create objects with
  `__class_name__` property to support `instanceof` checks.

**Result:** 33/33 primitives tests pass. All 689 Lambda baseline tests pass (no regressions).

**Exit criteria met:** `primitives_spec.js` equivalent loads and all tests pass.

---

### Phase 1: ArrayBuffer / DataView (Weeks 3–4)

**Goal:** Binary data handling works correctly.

**Status: ✅ COMPLETE** (2025-07-20)

**Implementation in `lambda/js/js_typed_array.cpp` (~520 lines rewritten):**

1. **ArrayBuffer**
   - Constructor: `new ArrayBuffer(byteLength)` → `calloc` raw buffer
   - Properties: `.byteLength`
   - Methods: `.slice(begin, end)` → copy portion into new ArrayBuffer
   - Static: `ArrayBuffer.isView(arg)` → check if TypedArray or DataView

2. **DataView**
   - Constructor: `new DataView(buffer, byteOffset?, byteLength?)`
   - 8 getter methods with endianness: `getInt8`, `getUint8`, `getInt16`,
     `getUint16`, `getInt32`, `getUint32`, `getFloat32`, `getFloat64`
   - 8 corresponding setter methods with endianness
   - Properties: `.buffer`, `.byteLength`, `.byteOffset`

3. **TypedArray ↔ ArrayBuffer bridge**
   - `new Uint8Array(arrayBuffer, offset, length)` — view over ArrayBuffer (shared memory)
   - `new Uint8Array(otherTypedArray)` — copy into new independent buffer
   - `new Uint8Array(jsArray)` — copy from JS array
   - `.buffer` property on all TypedArrays (wraps as ArrayBuffer on demand)
   - `.byteOffset`, `.BYTES_PER_ELEMENT` properties
   - `.slice()` (copy), `.subarray()` (view), `.set()` (bulk copy) methods
   - Smart constructor dispatch: detects number/ArrayBuffer/TypedArray/Array argument

**Architecture:** Shared buffer model. `JsArrayBuffer` owns `calloc`'d memory.
TypedArrays hold `byte_offset` (struct offset 12) and `buffer` pointer (offset 24).
Critically, `data` pointer stays at struct offset 16, preserving all existing MIR
inline codegen for element access. Three sentinel `TypeMap` markers distinguish
ArrayBuffer, DataView, and TypedArray in the `Map.type` field.

**Bugs found and fixed:**
- **`ITEM_JS_TRUE`/`ITEM_JS_FALSE` undefined:** Used non-existent macros in
  `js_arraybuffer_is_view_item`. Fixed to `(ITEM_TRUE)` / `(ITEM_FALSE)` from `lambda.h`.
- **bool return type vs MIR Item:** `js_arraybuffer_is_view` returned `bool` but MIR
  expects `Item` (i64). Created `_item` wrapper returning `ITEM_TRUE`/`ITEM_FALSE`.
- **TypedArray method dispatch to array path:** Runtime type cascade branched typed
  arrays to `l_array` → `js_array_method` instead of `l_map` → `js_map_method`.
  Fixed: `.slice()`, `.subarray()`, `.set()` now dispatch correctly.

**Files modified:** `js_typed_array.h` (full rewrite), `js_typed_array.cpp` (full rewrite),
`transpile_js_mir.cpp` (constructor dispatch, `jm_call_5`, cascade fix),
`js_runtime.cpp` (property access + method dispatch), `sys_func_registry.c` (15 new functions).

**Result:** 43/43 ArrayBuffer/DataView tests pass. 689/689 baseline tests pass (no regressions).

**Test:** `stream_spec.js`, `crypto_spec.js` — both exercise binary data heavily.

---

### Phase 2: Private Fields Polish (Week 5)

**Goal:** All `#field` patterns in PDF.js work correctly.

**Status: ✅ COMPLETE** (2025-07-21)

**Approach taken:**
1. AST-level transform: `#field` → `__private_field` (strip `#`, prepend `__private_`)
   in `build_js_expression()` for `private_property_identifier` nodes
2. Instance field collection in `jm_collect_functions`: non-static field definitions
   stored as `JsInstanceFieldEntry` with name + initializer
3. Instance field initialization at `new ClassName()` call site (before constructor),
   walking inheritance chain base-first for correct initialization order
4. Disabled A5/P3/P4 shaped slot optimization for classes with instance fields
   (avoids shape/slot conflicts between dynamic field inits and P3 slot writes)
5. Static private field increment/decrement: write-back via `js_set_module_var`
   instead of `js_property_set` for `ClassName.#field++` patterns
6. `#field in obj`: compile left-side `__private_*` identifier as string literal key

**Results:** 23/23 private fields test cases pass. 689/689 baseline regression tests pass.

1. ~~Audit the `#field` → `__private_` transform for completeness~~ ✅
2. ~~Handle class field initializers: `#map = new Map()` (evaluate in constructor)~~ ✅
3. ~~Handle static private fields: `static #cache = Object.create(null)`~~ ✅
4. ~~Handle private methods: `#validateRange()` → `__private_validateRange()`~~ ✅
5. ~~Handle `in` operator with private fields: `#field in obj`~~ ✅

**Test:** `primitives_spec.js` (Dict uses `#map`), `document_spec.js`.

---

### Phase 3: Promise.withResolvers + Minor Polyfills — ✅ COMPLETE

**Goal:** Eliminate all trivial runtime gaps.

1. ✅ `Promise.withResolvers()` → returns `{ promise, resolve, reject }` object
2. ✅ `String.prototype.match()` → RE2-based, supports global and non-global
3. ✅ `String.prototype.matchAll()` → iterates RE2 matches with capture groups
4. ✅ `TextEncoder` / `TextDecoder` (UTF-8 only, handles plain arrays and TypedArrays)
5. ✅ `WeakMap` / `WeakSet` → aliased to regular Map/Set (acceptable for testing)
6. ✅ `Object.create(null)` → already works
7. ✅ `Math.sinh/cosh/tanh/asinh/acosh/atanh/expm1/log1p` → 8 C stdlib functions

**Test:** `phase3_polyfills.js` — 27 assertions, all passing. 690/690 baseline.

**Implementation notes:**
- TextEncoder/TextDecoder dispatched through `js_map_method()` method cascade
- TextDecoder.decode handles both plain arrays and TypedArrays (Uint8Array etc.)
- Promise.withResolvers creates bound resolve/reject callbacks via `js_bind_function`

---

### Phase 4: Generators — ✅ COMPLETE (v15 Phase 8)

Fully implemented in LambdaJS v15 (2025-07-14). State machine transform in transpiler,
runtime generator struct with `.next()`, `.return()`, `.throw()`, `yield*` delegation,
`for...of` protocol. 8 test cases pass. No remaining work for PDF.js generators.

---

### Phase 5: Async/Await — Synchronous Fast Path

**Goal:** `async function` / `await` works when promises resolve synchronously.

**Status: ✅ COMPLETE** (2025-07-21)

**Key insight:** In PDF.js's fake-worker mode (which we're targeting), most Promises
resolve immediately because data is already loaded in memory. We can exploit this:

1. `async function f()` → transpile to ordinary function that returns a Promise
2. `await expr`:
   - If `expr` is not a Promise → continue immediately (value = expr)
   - If `expr` is a resolved Promise → continue immediately (value = result)
   - If `expr` is a pending Promise → log warning, return undefined (Phase 6 handles)
3. Return value wrapped in `Promise.resolve(returnValue)`
4. Exception in async function → return `Promise.reject(error)`

**Implementation:**

1. **Runtime function `js_await_sync(Item value)`** (js_runtime.cpp)
   - Not a promise → return as-is
   - Fulfilled promise → return `p->result`
   - Rejected promise → `js_throw_value(p->result)`, return null (exception propagates)
   - Pending promise → log warning, return undefined (Phase 6 will handle)

2. **Transpiler changes** (transpile_js_mir.cpp)
   - Added `bool in_async` flag to `JsMirTranspiler` struct
   - `await expr` → `js_await_sync(jm_transpile_box_item(expr))`
   - `return val` in async → wraps in `js_promise_resolve(val)` via delayed-return
   - Async function body wrapped in implicit try/catch:
     - Push `JsTryContext` with catch/end labels
     - After each statement: `js_check_exception()` → branch to catch
     - Catch block: `js_clear_exception()` → `js_promise_reject(error)` → return
     - End label: check `has_return_reg` flag → return stored value or
       `Promise.resolve(undefined)`
   - Arrow expression body in async: wrap result in `Promise.resolve()`

3. **AST builder fix** (build_js_ast.cpp)
   - Removed `if (!is_arrow)` gate on async detection — arrow functions now correctly
     detect `async` keyword by scanning children before `=>`

**Bugs found and fixed:**
- **Arrow async detection:** `is_async` was always `false` for arrow functions due to
  `if (!is_arrow)` guard. Fixed by removing the guard and adding `=>` as stop token.
- **`make_js_undefined` not in registry:** Used `static inline` function in JIT code.
  Fixed by emitting `ITEM_JS_UNDEFINED` constant directly.
- **Delayed return not checked:** `async_end_label` always returned
  `Promise.resolve(undefined)` even when a `return` statement stored a value.
  Fixed by checking `async_has_return_reg` and returning stored value.
- **`py_class.cpp` missing from build:** Pre-existing linker error. Fixed by
  regenerating premake to include the file.

**Files modified:** `js_runtime.cpp` (+`js_await_sync`), `js_runtime.h` (declaration),
`sys_func_registry.c` (registry entry), `transpile_js_mir.cpp` (4 changes: struct field,
await expression, return wrapping, async body wrapper), `build_js_ast.cpp` (arrow fix).

**Result:** 14/14 Phase 5 assertions pass. 690/690 baseline tests pass (no regressions).

**Test coverage:** Basic async return, await on non-promise, await on resolved promise,
multiple awaits, throwing async (→ rejected promise), await rejected in try/catch,
async arrow functions, async arrow expressions, nested async calls, async with no
explicit return, `Promise.all` with async functions.

**Test:** `phase5_async_await.js` — 14 assertions.

---

### Phase 6: Async/Await — Full State Machine

**Goal:** True suspend/resume for pending promises.

**Status: ✅ COMPLETE** (2025-07-26)

**Architecture:** Reuses generator state machine infrastructure. Async functions with
`await` expressions are transformed into `async_sm_<name>(Item* env, Item input,
int64_t state) → Item` state machine functions. Each `await` is a conditional
suspension point:

1. **Fast path:** `js_async_must_suspend(promise)` returns 0 → promise already
   resolved/rejected → `js_async_get_resolved()` returns cached value → continue
2. **Suspend path:** returns 1 → promise pending → save locals to env →
   return `[promise, next_state]` → runtime registers `.then(resume, reject)` →
   microtask flush → function returns
3. **Resume:** Promise settles → `js_async_resume_handler` re-enters state machine
   at saved state → load locals from env → continue from `gen_input_reg`
4. **Rejection:** `js_async_reject_handler` sets exception + re-enters state machine
   → implicit try/catch catches it → return `[error, -2]` → promise rejected

**State machine return protocol:** `[value, next_state]` via `js_gen_yield_result`:
- `next_state == -1`: fulfilled (value = return value)
- `next_state == -2`: rejected (value = error)
- `next_state >= 0`: suspended on pending promise (value = the promise)

**Runtime functions added** (js_runtime.cpp, ~130 lines):
- `JsAsyncContext` struct: `state_fn`, `env`, `env_size`, `state`, `promise_idx`
- `js_async_must_suspend(Item)` — returns 1 if pending, 0 otherwise; caches resolved
- `js_async_get_resolved()` — returns cached value from fast path
- `js_async_drive(ctx, input, state)` — core driver: calls SM, handles result
- `js_async_resume_handler` / `js_async_reject_handler` — `.then()`/`.catch()` callbacks
- `js_async_context_create(fn_ptr, env, env_size)` — allocates context + pending promise
- `js_async_start(ctx)` — initial call at state 0
- `js_async_get_promise(ctx)` — returns the async function's result promise

**Transpiler changes** (transpile_js_mir.cpp):
- `jm_count_awaits()` — counts await expressions (mirrors `jm_count_yields`)
- Async state machine generation block (~130 lines): env layout, SM function,
  state dispatch, implicit try/catch, body transpilation, done/catch labels
- Await expression handler: conditional suspend with fast/suspend paths
- Async wrapper function: env alloc → context create → start → return promise
- Arrow expression body support in async state machines

**Microtask integration:**
- `js_microtask_flush()` added to `js_resolve_callback`/`js_reject_callback`
- Ensures external promise resolution (e.g., `Promise.withResolvers`) triggers
  async function resumption

**Known limitation:** Inner function declarations inside async state machines cannot
capture enclosing scope variables (same as generators — no `scope_env_reg` in SM).

**Files modified:** `js_runtime.cpp`, `js_runtime.h`, `sys_func_registry.c`,
`transpile_js_mir.cpp`.

**Result:** 10/10 Phase 6 assertions pass. 14/14 Phase 5 assertions pass (backward
compatible). 690/690 baseline tests pass (no regressions).

**Test coverage:** Pending promise resolution, mixed resolved/pending awaits, chained
pending promises, rejection through pending promise, return pending, nested async with
pending, already-resolved Promise.withResolvers, sequential pending awaits, computation
after resume, async arrow with pending promise.

**Test:** `phase6_async_state_machine.js` — 10 assertions.

---

### Phase 7: Test Convergence (Weeks 15–16)

**Goal:** 100% PDF.js CLI test pass rate.

1. Run all 57 spec files, triage failures into categories:
   - Missing runtime function → implement
   - Semantic difference → fix
   - Edge case in existing feature → patch
2. Focus on highest-impact fixes first (one fix may unblock many tests)
3. Performance profiling — PDF.js is computation-heavy; ensure JIT performance
4. Memory profiling — PDF files can be large; ensure GC handles pressure

---

## 6. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|:----------:|:------:|------------|
| ~~Async/await state machine proves harder than expected~~ | ~~High~~ | ~~Critical~~ | ✅ Completed — Phase 6 state machine fully working |
| Circular module dependencies break bundling | Low | Medium | esbuild handles cycles; manual fixup if needed |
| PDF.js relies on JS semantics LambdaJS subtly differs on | Medium | Medium | Test-driven fixes; each spec isolates issues |
| RE2 regex limitations block parsing | Low | Medium | PDF.js uses basic regex; RE2 covers 99% |
| Performance regression under heavy PDF processing | Medium | Low | Lambda JIT is already 2.2× Node.js; headroom exists |
| GC pressure from large PDF files | Medium | Medium | Tune nursery/arena sizes for PDF workloads |
| `Object.create(null)` prototype handling | Low | Low | Verify shape system handles null prototype |

---

## 7. Success Metrics

| Milestone | Target | Measurement |
|-----------|--------|-------------|
| Phase 0 complete | primitives_spec.js loads | ✅ 33/33 pass |
| Phase 1 complete | ArrayBuffer/DataView works | ✅ 43/43 pass |
| Phase 2 complete | Private fields work | ✅ 23/23 pass |
| Phase 3 complete | Minor polyfills work | ✅ 27/27 pass, 690/690 baseline |
| Phase 5 complete | Async/await sync fast-path | ✅ 14/14 pass, 690/690 baseline |
| Phase 6 complete | Full async state machine | ✅ 10/10 pass, 14/14 Phase 5 compat, 690/690 baseline |
| Phase 7 complete | Full CLI suite passes | 57/57 spec files, all assertions green |
| Stretch goal | Parse real PDFs | `lambda.exe parse file.pdf` outputs document tree |

---

## 8. Strategic Value

Running PDF.js natively in Lambda would:

1. **Validate LambdaJS maturity** — PDF.js is a large, real-world JS library (120+ files,
   ~50K LOC). Successfully running it proves LambdaJS can handle production code.

2. **Enable native PDF input** — Lambda already has PDF input support; PDF.js would provide
   a second, more complete implementation battle-tested by millions of users.

3. **Benchmark real-world performance** — PDF parsing is CPU-intensive with heavy binary
   data processing, exercising JIT codegen, GC, and TypedArray paths.

4. **Drive feature completion** — The gaps identified (async/await, modules, generators)
   are needed for any serious JS library, not just PDF.js. Closing them makes LambdaJS
   viable for the broader JS ecosystem.

5. **Demonstrate multi-language synergy** — Parse a PDF with JS, process data with Lambda
   Script, render with Radiant. The unified runtime makes this zero-cost interop.

---

## 9. Alternative Approaches Considered

### A. Transpile PDF.js to Lambda Script
**Rejected.** PDF.js is too large and idiomatic JS for automated translation. The OOP
patterns (classes with private fields, prototype chains) don't map cleanly to Lambda's
pure functional model.

### B. Embed QuickJS/V8 for PDF.js Only
**Rejected.** Defeats the purpose of LambdaJS. Adds binary size, complexity, and a
second GC. LambdaJS should grow to handle this natively.

### C. Use PDF.js's Legacy Build (ES5 Transpiled)
**Partially viable.** PDF.js's `generic-legacy` Gulp target transpiles to ES5 with Babel.
This eliminates private fields, optional chaining, and some modern syntax — but still
requires async/await (Babel compiles to generators, which also need support) and modules
(Babel and webpack resolve them). Not a shortcut, but useful as a reference for what
patterns the transpiled code produces.

### D. Port Only the Core Parser (No async)
**Partially viable.** A synchronous subset of PDF.js core could work with Phase 5's
sync fast-path. This covers local file parsing (no streaming/chunked loading). Good
as an intermediate milestone but not a complete solution.

---

## 10. Conclusion

Running PDF.js under LambdaJS is **feasible but requires focused implementation work**
across 6 feature gaps. The recommended approach is:

1. **Bundle PDF.js** to eliminate the ES module gap
2. **Implement ArrayBuffer/DataView** — well-defined, bounded scope
3. **Complete the generator state machine** — enables both generators and async
4. **Implement async/await** (sync fast-path first, full state machine later)
5. **Drive to 100% CLI test pass** via iterative test-fix cycles

The bundling strategy (Option A in §4.2) reduces the problem from "implement a full
ES module system" to "implement 5 runtime features", making the project significantly
more tractable. The async/await work also benefits from the prior generator
implementation, since both use the same coroutine transformation pattern.

Achieving 100% PDF.js test pass would establish LambdaJS as capable of running
production-grade JavaScript libraries — a major milestone for the project.
