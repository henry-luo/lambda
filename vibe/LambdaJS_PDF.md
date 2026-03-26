# LambdaJS PDF.js: Feasibility Assessment & Implementation Plan

## 1. Executive Summary

**Goal:** Run Mozilla's PDF.js library under the LambdaJS runtime to enable native PDF
parsing and rendering in Lambda, without embedding a separate JS engine.

**Verdict: Feasible with significant effort. 6 major feature gaps must be closed.**

PDF.js is a 120+ file, zero-dependency JavaScript library that parses and renders PDF
documents. Its core parsing engine (`src/core/`) can run without browser APIs by using
a "fake worker" fallback path already built into the codebase. LambdaJS (v15) already
supports ~75% of the JS features PDF.js requires, but the remaining 25% includes
architecturally critical features — particularly **async/await** and **ES modules** —
that are deeply embedded in PDF.js and cannot be worked around by transpilation alone.

### Feasibility Score

| Dimension | Score | Notes |
|-----------|:-----:|-------|
| **Core parsing (no rendering)** | 7/10 | Achievable; core uses fewer browser APIs |
| **Full library (parse + render)** | 4/10 | Canvas/Worker/fetch dependencies add layers |
| **Test pass rate potential** | 8/10 | CLI tests (57 specs) are the right target |
| **Implementation effort** | High | ~6 features × 1-3 weeks each |

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

#### Gap 1: Async/Await (🔴 CRITICAL)

**PDF.js usage:** 100+ `await` expressions across `src/core/`. Async is architecturally
fundamental — PDF page loading, font resolution, image decoding, and stream operations
are all async.

```javascript
// src/core/document.js — deeply embedded, not removable
const content = await this.pdfManager.ensure(this, "content");
const bytes = await content.asyncGetBytes();
await Promise.all(promises);
```

**LambdaJS status:** AST nodes exist (v14). `await` currently resolves synchronously
(unwraps already-resolved Promises). The generator-based state machine for true
suspend/resume is incomplete.

**Required work:**
- Complete the async function → state machine transformation in the transpiler
- Each `await` becomes a yield point; resume when promise resolves
- Integrate with microtask queue for correct ordering
- v15's libuv event loop provides the foundation

**Estimated complexity:** HIGH — This is the single largest gap. The transpiler must
transform async functions into resumable coroutines, which requires:
1. Stack-frame capture at each await point
2. Promise resolution callback that resumes execution
3. Correct error propagation (rejected promises → catch blocks)

**Strategy:** Two-phase approach:
- **Phase 1 (Synchronous fast-path):** If all promises are pre-resolved (common in
  PDF.js's fake-worker mode where data is already loaded), `await` returns immediately.
  This may cover 60-70% of PDF.js core operations.
- **Phase 2 (Full state machine):** Implement proper coroutine-style suspend/resume
  for truly async operations (file I/O, chunked loading).

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

#### Gap 3: Private Class Fields — `#field` (🟡 MEDIUM)

**PDF.js usage:** 100+ private fields across core files. Pervasive in data structures.

```javascript
// src/core/primitives.js
class Dict {
  #map = new Map();
  #xref;
  #suppressEncryption;
}

// src/core/document.js — 20+ private fields
class Page {
  #areAnnotationsCached = false;
  #pagePromises = new Map();
  #version = null;
}
```

**LambdaJS status:** Handled implicitly via `__private_FieldName` naming convention
in property access. The Tree-sitter grammar parses `#field` syntax.

**Required work:**
- Verify the `#field` → `__private_` transform works for all PDF.js patterns
- Handle `#field` in class initializers (field declarations with default values)
- Handle `static #field` (static private fields)
- Ensure WeakMap-like semantics (private fields not enumerable)

**Estimated complexity:** LOW-MEDIUM — The naming convention approach should work for
most cases. The main risk is edge cases around static private fields and private
methods.

**Strategy:** Test-driven: run PDF.js primitives_spec.js first, fix failures.

---

#### Gap 4: ArrayBuffer / DataView (🟡 MEDIUM)

**PDF.js usage:** Core binary data handling. PDF files are binary → ArrayBuffer is
the primary container.

```javascript
const buffer = new ArrayBuffer(length);
const view = new DataView(buffer);
view.getUint16(offset, /* littleEndian */ true);
```

**LambdaJS status:** TypedArrays (Int8–Float64) fully implemented. ArrayBuffer and
DataView are **not implemented**.

**Required work:**
- `ArrayBuffer`: Constructor, `.byteLength`, `.slice()`, `ArrayBuffer.isView()`
- `DataView`: Constructor (wrapping ArrayBuffer), 10 getter/setter pairs:
  `getInt8`, `getUint8`, `getInt16`, `getUint16`, `getInt32`, `getUint32`,
  `getFloat32`, `getFloat64`, `getBigInt64`, `getBigUint64` (+ set* variants)
- TypedArray constructor from ArrayBuffer: `new Uint8Array(buffer, offset, length)`
- `.buffer` property on TypedArrays exposing the underlying ArrayBuffer

**Estimated complexity:** MEDIUM — Well-defined API surface. Can reuse existing
TypedArray backing store infrastructure. DataView's endianness handling is the
main implementation detail.

**Strategy:** Implement ArrayBuffer as a thin wrapper around the existing raw buffer
allocation. DataView reads/writes with endianness conversion.

---

#### Gap 5: Generator Functions / yield (🟡 MEDIUM)

**PDF.js usage:** 19 occurrences. Used for iterating PDF objects, especially in
`Dict`, `RefSetCache`, and XFA tree traversal.

```javascript
// src/core/primitives.js
*[Symbol.iterator]() {
  for (const [key, value] of this.#map) {
    yield [key, value];
  }
}
```

**LambdaJS status:** AST nodes, `JsGenerator` struct, and runtime functions
(`js_generator_create/next/return/throw`) all exist. The state machine transform
in the transpiler is incomplete.

**Required work:**
- Complete the generator state machine transformation:
  - Each `yield` becomes a suspension point
  - Generator object tracks current state (PC + local variables)
  - `next()` resumes from last yield point
- Support `yield*` delegation (used in XFA)
- Implement `Symbol.iterator` protocol for generator objects

**Estimated complexity:** MEDIUM — The runtime infrastructure exists. The transpiler
transform is the main work: converting a function body with yield points into a
state machine (switch-case over states).

**Strategy:** Implement the subset PDF.js uses first. Most generators are simple
iteration helpers — the state machine doesn't need to handle complex control flow
(no try/finally around yield, no deeply nested yields).

---

#### Gap 6: Promise.withResolvers (🟢 LOW)

**PDF.js usage:** 33 occurrences. Modern convenience API.

```javascript
class WorkerTask {
  _capability = Promise.withResolvers();
}
```

**LambdaJS status:** Not implemented but Promise infrastructure exists.

**Required work:** Add one runtime function:
```c
Item js_promise_with_resolvers() {
    // Returns { promise, resolve, reject }
}
```

**Estimated complexity:** TRIVIAL — 20-30 lines of code.

---

### 3.2 Minor Gaps (Non-Blocking for Core Parsing)

| Feature | PDF.js Usage | Impact | Workaround |
|---------|:------------:|:------:|------------|
| `WeakMap` | Display layer caching | Low | Replace with Map (memory leak OK for testing) |
| `WeakRef` / `FinalizationRegistry` | 3 places, all optional | None | Remove or stub |
| `Proxy` / `Reflect` | Only in `scripting_api/` (skipped) | None | N/A |
| `structuredClone` | 1 place in editor (skipped) | None | N/A |
| `fetch()` API | Network loading | Low | Provide Lambda-native file reader |
| `TextEncoder` / `TextDecoder` | String encoding | Low | Implement thin wrappers |
| `String.match/matchAll` | Regex usage in parsing | Low | Implement using RE2 |
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
| **async/await** | ⚠️ | ✅ | ✅ | **Gap 1** |
| **ES modules** | ❌ | ✅ | ✅ | **Gap 2** |
| **Private fields (#)** | ⚠️ | ✅ | ✅ | **Gap 3** |
| **ArrayBuffer/DataView** | ❌ | ✅ | ✅ | **Gap 4** |
| **Generators/yield** | ⚠️ | ✅ | — | **Gap 5** |
| **Promise.withResolvers** | ❌ | ✅ | ✅ | **Gap 6** |

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

After bundling, the test plan targets the remaining gaps (1, 3–6) in the runtime.

### 4.3 Phased Implementation Plan

```
Phase 0: Bundle + Baseline          ─── Weeks 1-2
Phase 1: ArrayBuffer/DataView       ─── Weeks 3-4
Phase 2: Private Fields Polish      ─── Week 5
Phase 3: Promise.withResolvers      ─── Week 5
Phase 4: Generators (basic)         ─── Weeks 6-7
Phase 5: Async/Await (sync fast)    ─── Weeks 8-10
Phase 6: Async/Await (full)         ─── Weeks 11-14
Phase 7: Test Convergence           ─── Weeks 15-16
```

---

## 5. Implementation Plan — Detailed Phases

### Phase 0: Bundle + Baseline (Weeks 1–2)

**Goal:** Get PDF.js loading and failing with identifiable errors.

1. Bundle PDF.js core (`src/core/` + `src/shared/` + `src/display/api.js`) with esbuild
2. Strip dynamic imports, replace `fetch()` with Lambda file reader stub
3. Attempt to run the bundle under `./lambda.exe script.js`
4. Catalog all runtime errors — classify as known gaps vs new issues
5. Port Jasmine test runner basics (describe/it/expect) as a Lambda-native shim
6. Run `primitives_spec.js` (simplest core test) → measure pass rate

**Exit criteria:** `primitives_spec.js` loads and at least some tests execute.

---

### Phase 1: ArrayBuffer / DataView (Weeks 3–4)

**Goal:** Binary data handling works correctly.

**Implementation in `lambda/js/js_typed_array.cpp`:**

1. **ArrayBuffer**
   - Constructor: `new ArrayBuffer(byteLength)` → allocate raw buffer
   - Properties: `.byteLength`
   - Methods: `.slice(begin, end)` → copy portion into new ArrayBuffer
   - Static: `ArrayBuffer.isView(arg)` → check if TypedArray or DataView

2. **DataView**
   - Constructor: `new DataView(buffer, byteOffset?, byteLength?)`
   - 8 getter methods with endianness: `getInt8`, `getUint8`, `getInt16`,
     `getUint16`, `getInt32`, `getUint32`, `getFloat32`, `getFloat64`
   - 8 corresponding setter methods
   - Properties: `.buffer`, `.byteLength`, `.byteOffset`

3. **TypedArray ↔ ArrayBuffer bridge**
   - `new Uint8Array(arrayBuffer, offset, length)` — view over ArrayBuffer
   - `.buffer` property on all TypedArrays
   - `TypedArray.from()` and `TypedArray.of()` static methods

**Test:** `stream_spec.js`, `crypto_spec.js` — both exercise binary data heavily.

---

### Phase 2: Private Fields Polish (Week 5)

**Goal:** All `#field` patterns in PDF.js work correctly.

1. Audit the `#field` → `__private_` transform for completeness
2. Handle class field initializers: `#map = new Map()` (evaluate in constructor)
3. Handle static private fields: `static #cache = Object.create(null)`
4. Handle private methods: `#validateRange()` → `__private_validateRange()`
5. Handle `in` operator with private fields: `#field in obj`

**Test:** `primitives_spec.js` (Dict uses `#map`), `document_spec.js`.

---

### Phase 3: Promise.withResolvers + Minor Polyfills (Week 5)

**Goal:** Eliminate all trivial runtime gaps.

1. `Promise.withResolvers()` → return `{ promise, resolve, reject }` object
2. `String.prototype.match()` → wrap RE2 `exec()` to return match array
3. `String.prototype.matchAll()` → iterate exec() matches
4. `TextEncoder` / `TextDecoder` stubs (UTF-8 only, use Lambda's internal encoding)
5. `WeakMap` → alias to regular Map (acceptable for testing)
6. `Object.create(null)` → empty map with no prototype (already works?)
7. `Math.sinh/cosh/tanh` → trivial math functions

**Test:** `util_spec.js`, `core_utils_spec.js`.

---

### Phase 4: Generator Functions (Weeks 6–7)

**Goal:** `function*` / `yield` / `for...of` over generators all work.

**Transpiler changes in `lambda/js/transpile_js_mir.cpp`:**

1. Detect generator functions (`function*` or `*method()`)
2. Transform function body into state machine:
   ```
   State 0: code before first yield
   State 1: code between yield 1 and yield 2
   State N: code after last yield (return)
   ```
3. Each `yield expr` → set state, store locals, return `{ value: expr, done: false }`
4. `next(arg)` → restore locals, resume at saved state, `arg` becomes yield result
5. `return value` → `{ value, done: true }`, subsequent `next()` → `{ value: undefined, done: true }`
6. `yield*` delegation → iterate the delegated iterable, forwarding next/throw/return

**Runtime additions in `lambda/js/js_runtime.cpp`:**
- Ensure `JsGenerator` struct stores full local variable snapshot
- Support `generator.throw(error)` for error propagation
- Support `generator.return(value)` for early termination

**Test:** `primitives_spec.js` (Dict iterator), `parser_spec.js`.

---

### Phase 5: Async/Await — Synchronous Fast Path (Weeks 8–10)

**Goal:** `async function` / `await` works when promises resolve synchronously.

**Key insight:** In PDF.js's fake-worker mode (which we're targeting), most Promises
resolve immediately because data is already loaded in memory. We can exploit this:

1. `async function f()` → transpile to ordinary function that returns a Promise
2. `await expr`:
   - If `expr` is not a Promise → continue immediately (value = expr)
   - If `expr` is a resolved Promise → continue immediately (value = result)
   - If `expr` is a pending Promise → **error/fallback** (Phase 6 handles this)
3. Return value wrapped in `Promise.resolve(returnValue)`
4. Exception in async function → return `Promise.reject(error)`

**This fast path covers the majority of PDF.js operations** when loading from a local
file (all data available synchronously).

**Edge cases to handle:**
- `await Promise.all([...])` where all inner promises are resolved
- `try { await x } catch(e)` — rejected promise flows to catch
- Nested async: `async function a() { return await b(); }`

**Test:** `document_spec.js`, `evaluator_spec.js`, `api_spec.js`.

---

### Phase 6: Async/Await — Full State Machine (Weeks 11–14)

**Goal:** True suspend/resume for pending promises.

**Transpiler: async function → coroutine transform:**

1. Similar to generator state machine (Phase 4) but driven by Promises
2. Each `await` becomes a suspension point:
   ```
   // Before transform:
   async function load() {
     const a = await fetchPage(1);
     const b = await fetchPage(2);
     return [a, b];
   }

   // After transform (conceptual):
   function load() {
     return new Promise((resolve, reject) => {
       let state = 0, a, b;
       function step(input) {
         try {
           switch (state) {
             case 0: state = 1; return fetchPage(1).then(step, reject);
             case 1: a = input; state = 2; return fetchPage(2).then(step, reject);
             case 2: b = input; return resolve([a, b]);
           }
         } catch (e) { reject(e); }
       }
       step();
     });
   }
   ```
3. Integrate with microtask queue for correct scheduling
4. Support `for await...of` (async iteration) if needed

**Runtime: event loop integration:**
- `js_run_event_loop()` must process pending async operations
- Microtask flush after each promise resolution
- Correct ordering: microtasks before next macrotask

**Test:** All 57 CLI spec files.

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
| Async/await state machine proves harder than expected | High | Critical | Phase 5 sync fast-path provides fallback |
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
| Phase 0 complete | primitives_spec.js loads | ≥1 test passes |
| Phase 3 complete | Core type tests pass | primitives, util, core_utils specs: 100% |
| Phase 5 complete | Document loading works | document_spec, parser_spec: 100% |
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
