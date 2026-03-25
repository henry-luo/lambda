# Transpile_Js14: Async Runtime — Generators, Promises, ES Modules, Timers

## 1. Executive Summary

After v13 (optimization parity with Lambda restructuring), LambdaJS covers all synchronous
ES6+ features. Four significant gaps remain in the feature matrix, all sharing a common
dependency on **asynchronous execution infrastructure**:

| Feature | Current Status | Dependency |
|---------|:-:|---|
| Generators / `yield` | ❌ | Coroutine / suspension mechanism |
| `async` / `await` / `Promise` | ❌ | Event loop + microtask queue |
| `import` / `export` (ES modules) | ❌ | Module loader (+ top-level await needs event loop) |
| `setTimeout` / `setInterval` | ❌ | Timer queue + event loop |

**v14 goal:** Add an event loop and coroutine system to the JS runtime, enabling all four
features. This is the single largest structural addition since the initial JS transpiler.

### Do We Need libuv?

**Short answer: No.** libuv is designed for full I/O multiplexing (filesystem, TCP, UDP,
DNS, child processes, signals, polling). LambdaJS needs a much narrower subset:

| Capability | libuv | What LambdaJS Actually Needs |
|-----------|:-----:|---|
| Timer queue (setTimeout/setInterval) | ✅ | ✅ — Simple min-heap of callbacks |
| Microtask queue (Promise resolution) | ❌ (not built-in) | ✅ — FIFO queue, drained after each macrotask |
| Async file I/O | ✅ | ❌ — Lambda already has synchronous `read_text_file()` |
| TCP/UDP/DNS | ✅ | ❌ — Lambda uses libcurl for HTTP |
| Child processes | ✅ | ❌ — Lambda_Worker.md proposes fork+pipe, not libuv |
| Signal handling | ✅ | ❌ |
| Thread pool | ✅ | ❌ — Single-threaded JS semantics |

**Decision: Build a lightweight custom event loop** (~300–400 LOC) for v14. libuv migration
is deferred to **Js15**, where it will replace libevent and enable async file I/O,
child processes, and full Node.js-like I/O support. The reasons for custom-first:

1. **Microtask queue is essential but libuv doesn't provide one.** Promise resolution
   requires a microtask queue that drains between each macrotask. With libuv you'd still
   need to build this yourself on top of `uv_async_send`.

2. **Lambda already has `libevent`.** The project links `libevent` (in `setup-mac-deps.sh`
   and `build_lambda_config.json`) for libcurl's HTTP event handling. Adding libuv would
   create a second event loop dependency.

3. **Generators don't need an event loop at all.** They need a coroutine/suspension
   mechanism (stack switching or state machine transform). libuv provides nothing here.

4. **Build complexity.** libuv requires cmake + platform-specific configuration. Lambda's
   Premake5 build system would need significant adaptation. The existing deps are all
   built from source with simple Makefiles.

5. **Binary size.** Lambda's release binary is ~8MB. libuv adds ~200KB of code for
   capabilities Lambda won't use.

### Alternative Libraries Considered

| Library | Size | Pros | Cons | Verdict |
|---------|------|------|------|---------|
| **libuv** | ~35K LOC | Battle-tested, Node.js uses it | Overkill for v14 scope; no microtasks; cmake build | ⏳ Deferred to **Js15** |
| **libevent** (already linked) | — | Already a dependency, timer support | Low-level, no microtask queue, designed for socket I/O | Reuse for inspiration only |
| **libdill** | ~8K LOC | Structured concurrency, coroutines | Linux-only (no macOS/Windows), abandoned (last commit 2021) | ❌ Pass |
| **minicoro** | ~1K LOC | Tiny stackful coroutines, cross-platform | Only does coroutines, no event loop | ✅ Consider for generators |
| **Custom** | ~400 LOC event loop + ~500 LOC coroutines | Minimal, exactly what we need, no new deps | Must test cross-platform | ✅ **Recommended** |

### Proposed Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    JS Execution                          │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  Generators   │  │ async/await  │  │  ES Modules   │  │
│  │  (coroutines) │  │  (Promises)  │  │  (loader)     │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘  │
│         │                 │                   │          │
│  ┌──────▼───────┐  ┌──────▼───────┐          │          │
│  │ State Machine │  │  Microtask   │          │          │
│  │ Transform     │  │  Queue       │          │          │
│  └──────────────┘  └──────┬───────┘          │          │
│                           │                   │          │
│                    ┌──────▼───────────────────▼───┐      │
│                    │       Event Loop              │      │
│                    │  ┌─────────┐ ┌────────────┐  │      │
│                    │  │ Timer   │ │ Macrotask   │  │      │
│                    │  │ Heap    │ │ Queue       │  │      │
│                    │  └─────────┘ └────────────┘  │      │
│                    └──────────────────────────────┘      │
│                                                          │
├──────────────────────────────────────────────────────────┤
│            Existing Lambda Runtime (unchanged)            │
│  Item types · GC heap · MIR JIT · import_resolver         │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Feature Design

### 2.1 Generators / `yield` (Phase 1)

Generators require **suspendable execution** — a function that can pause mid-body and
resume later. Two implementation strategies:

#### Option A: State Machine Transform (Recommended)

Transform generator functions into state machines at the MIR level. Each `yield` becomes
a state transition. The generator object holds the current state index and all live local
variables.

```js
// Source:
function* range(start, end) {
    for (let i = start; i < end; i++) {
        yield i;
    }
}

// Conceptual transform → state machine:
// state 0: init, set i = start
// state 1: check i < end, if false → done. yield i, advance i, → state 1
// state 2: done
```

**Implementation in MIR:**

```c
// Generator object (stored as Lambda Map with sentinel marker)
typedef struct JsGenerator {
    MIR_val_t func_ptr;      // pointer to state machine function
    int state;                // current state index
    Item* locals;             // saved local variable values
    int local_count;
    bool done;                // iteration complete
    Item return_value;        // final return value
} JsGenerator;

// Runtime functions
Item js_generator_create(void* func_ptr, int local_count);
Item js_generator_next(Item gen, Item send_value);   // .next(value)
Item js_generator_return(Item gen, Item value);       // .return(value)
Item js_generator_throw(Item gen, Item error);        // .throw(error)
```

The transpiler transform:
1. Identify generator functions (`function*`) via the `is_generator` AST flag (already parsed, currently `false`)
2. In Phase 1.0 function collection, mark as generator
3. In Phase 2 code generation, emit a state machine function instead of normal function body:
   - Each `yield` expression becomes: save locals → set state → return yielded value
   - Each `.next()` call: restore locals → jump to saved state → execute until next yield
   - Local variables are stored in the `JsGenerator::locals` array between suspensions
4. `for...of` on generators calls `.next()` in a loop until `done === true`

**Why not stackful coroutines (e.g., minicoro)?** State machine transform is:
- Portable (no assembly, no `setjmp/longjmp`, no platform-specific stack switching)
- Compatible with MIR JIT (MIR functions are standard C calling convention)
- GC-safe (no hidden stack frames the conservative scanner can't find)
- Used by Babel, TypeScript, and C# for the same purpose

#### Transpiler Changes

| File | Change |
|------|--------|
| `build_js_ast.cpp` | Detect `function*` and `yield` expressions, set `is_generator = true` |
| `js_ast.hpp` | Add `JS_AST_NODE_YIELD_EXPRESSION` node type |
| `transpile_js_mir.cpp` | New `jm_transpile_generator_function()` — state machine emission |
| `js_runtime.cpp` | `JsGenerator` struct, `js_generator_create/next/return/throw` |
| `js_runtime.h` | Declare generator runtime functions |

**Estimated LOC:** ~600 (transpiler) + ~200 (runtime) = ~800

---

### 2.2 Promises and async/await (Phase 2)

Depends on: Generators (Phase 1) for conceptual framework, Event Loop (Phase 3) for resolution.

#### Promise Data Model

```c
typedef enum JsPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED
} JsPromiseState;

typedef struct JsPromise {
    JsPromiseState state;
    Item result;                    // resolved value or rejection reason
    Item* on_fulfilled;             // callback array
    int on_fulfilled_count;
    int on_fulfilled_cap;
    Item* on_rejected;              // callback array
    int on_rejected_count;
    int on_rejected_cap;
    Item* finally_callbacks;
    int finally_count;
    int finally_cap;
} JsPromise;
```

Promises are wrapped as Lambda Maps with a `js_promise_type_marker` sentinel (same
pattern as DOM nodes and TypedArrays).

#### Core Promise API

```c
// Construction
Item js_promise_new(Item executor);           // new Promise((resolve, reject) => ...)
Item js_promise_resolve(Item value);          // Promise.resolve(value)
Item js_promise_reject(Item reason);          // Promise.reject(reason)

// Instance methods
Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
Item js_promise_catch(Item promise, Item on_rejected);
Item js_promise_finally(Item promise, Item on_finally);

// Static combinators
Item js_promise_all(Item iterable);           // Promise.all([...])
Item js_promise_all_settled(Item iterable);   // Promise.allSettled([...])
Item js_promise_race(Item iterable);          // Promise.race([...])
Item js_promise_any(Item iterable);           // Promise.any([...])

// Internal resolution
void js_promise_resolve_internal(JsPromise* p, Item value);
void js_promise_reject_internal(JsPromise* p, Item reason);
```

#### async/await Transform

`async` functions are syntactic sugar over Promises + generators. The transform:

```js
// Source:
async function fetchData(url) {
    const response = await fetch(url);
    const data = await response.json();
    return data;
}

// Conceptual transform:
function fetchData(url) {
    return new Promise((resolve, reject) => {
        // State machine (like generator):
        // state 0: call fetch(url), .then → state 1
        // state 1: receive response, call response.json(), .then → state 2
        // state 2: receive data, resolve(data)
        // any rejection → reject(error)
    });
}
```

The transpiler:
1. Detect `async function` / `async () =>` via Tree-sitter (the AST `is_async` field already exists)
2. Transform the function body into a state machine (reuse generator infrastructure)
3. Wrap the state machine in a Promise: each `await expr` → `.then()` chain
4. Unhandled errors → Promise rejection

#### Transpiler Changes

| File | Change |
|------|--------|
| `build_js_ast.cpp` | Detect `async` keyword, set `is_async = true`; parse `await` expressions |
| `js_ast.hpp` | Add `JS_AST_NODE_AWAIT_EXPRESSION` node type |
| `transpile_js_mir.cpp` | `jm_transpile_async_function()` — Promise + state machine emission |
| `js_runtime.cpp` | `JsPromise` struct, resolve/reject/then/catch/finally, microtask enqueuing |
| `js_runtime.h` | Declare Promise runtime functions |

**Estimated LOC:** ~400 (transpiler) + ~500 (runtime) = ~900

---

### 2.3 Event Loop and Timers (Phase 3)

This is the scheduling infrastructure that drives Promise resolution and setTimeout/setInterval.

#### Event Loop Design

```c
// js_event_loop.h

typedef struct JsTimerEntry {
    int id;                     // timer ID (for clearTimeout/clearInterval)
    double fire_at_ms;          // monotonic timestamp when timer should fire
    Item callback;              // JS function to call
    double interval_ms;         // >0 for setInterval, 0 for setTimeout
    bool cancelled;
} JsTimerEntry;

typedef struct JsEventLoop {
    // Microtask queue (Promise callbacks) — FIFO, drained completely after each macrotask
    Item* microtasks;
    int microtask_count;
    int microtask_cap;

    // Macrotask queue (setTimeout/setInterval callbacks) — min-heap ordered by fire_at_ms
    JsTimerEntry* timers;
    int timer_count;
    int timer_cap;
    int next_timer_id;

    // State
    bool running;
    double current_time_ms;     // cached monotonic time
} JsEventLoop;

// Event loop lifecycle
void js_event_loop_init(JsEventLoop* loop);
void js_event_loop_run(JsEventLoop* loop);       // run until no pending work
void js_event_loop_destroy(JsEventLoop* loop);

// Microtask queue (used by Promise resolution)
void js_microtask_enqueue(JsEventLoop* loop, Item callback);
void js_microtask_drain(JsEventLoop* loop);       // drain all microtasks

// Timer API (setTimeout, setInterval, clearTimeout, clearInterval)
int  js_timer_set(JsEventLoop* loop, Item callback, double delay_ms, bool repeat);
void js_timer_clear(JsEventLoop* loop, int timer_id);
```

#### Event Loop Algorithm

```
js_event_loop_run(loop):
    while (has_pending_work(loop)):
        1. drain_microtask_queue(loop)          // ALL pending microtasks
        2. if timer_heap is empty: break
        3. now = monotonic_time_ms()
        4. pop all timers where fire_at_ms <= now
        5. for each expired timer:
             call timer.callback()
             if timer.interval_ms > 0:         // setInterval
                 timer.fire_at_ms = now + interval_ms
                 re-insert into heap
             drain_microtask_queue(loop)        // microtasks between macrotasks
        6. if no expired timers and microtask queue empty:
             sleep until next timer fires       // avoid busy-wait
```

This is ~200 LOC for the event loop + ~100 LOC for the timer min-heap.

#### Integration with JS Execution

The event loop runs **after** `js_main()` returns:

```
transpile_js_to_mir():
    ...
    result = js_main(&eval_context)         // synchronous execution
    if (event_loop has pending work):
        js_event_loop_run(&event_loop)      // drain timers + promises
    return result
```

This matches browser and Node.js semantics: synchronous code runs first, then the
event loop processes all pending async operations.

#### Runtime Functions (exposed to JIT)

```c
extern "C" Item js_setTimeout(Item callback, Item delay);
extern "C" Item js_setInterval(Item callback, Item delay);
extern "C" void js_clearTimeout(Item timer_id);
extern "C" void js_clearInterval(Item timer_id);
```

**Estimated LOC:** ~300 (event loop) + ~100 (timer heap) + ~100 (transpiler integration) = ~500

---

### 2.4 ES Modules — `import` / `export` (Phase 4)

ES modules are largely a **compile-time** concern (module graph resolution, export binding)
with minimal runtime support. The async aspect (top-level `await`) depends on Phase 2.

#### Module Model

```c
typedef struct JsModule {
    const char* specifier;          // "./utils.js", "lodash"
    const char* resolved_path;      // absolute file path
    JsModuleStatus status;          // unlinked → linking → linked → evaluated
    Item namespace_object;          // the module's exported bindings
    JsAstNode* ast;                 // parsed AST (for deferred evaluation)
    MIR_item_t init_func;           // MIR function that evaluates the module body
    int module_var_base;            // base index in js_module_vars for this module
} JsModule;

typedef enum JsModuleStatus {
    JS_MODULE_UNLINKED,
    JS_MODULE_LINKING,
    JS_MODULE_LINKED,
    JS_MODULE_EVALUATING,
    JS_MODULE_EVALUATED,
    JS_MODULE_ERROR
} JsModuleStatus;
```

#### Export Binding

```js
// Named exports → module namespace object properties
export function foo() { ... }    // → js_module_export(mod, "foo", foo_item)
export const PI = 3.14159;       // → js_module_export(mod, "PI", pi_item)
export default class App { ... } // → js_module_export(mod, "default", app_item)

// Re-exports
export { bar } from './other.js' // → resolve other.js, copy binding
```

#### Import Resolution

```js
import { foo, bar } from './utils.js';
import * as utils from './utils.js';
import defaultExport from './utils.js';
```

Resolution at compile time:
1. Parse the import specifier (`'./utils.js'`)
2. Resolve relative to the importing file's directory
3. If module not yet loaded: read file → parse → build AST → transpile to MIR
4. Link imported names to the module's namespace object properties
5. In the importing module's MIR code, imported values are loaded from the namespace

#### Transpiler Changes

| File | Change |
|------|--------|
| `build_js_ast.cpp` | Parse `import`/`export` declarations (Tree-sitter already parses these) |
| `js_ast.hpp` | Add `JS_AST_NODE_IMPORT_DECLARATION`, `JS_AST_NODE_EXPORT_DECLARATION` |
| `transpile_js_mir.cpp` | Module graph builder, multi-module MIR compilation, namespace objects |
| `js_runtime.cpp` | `js_module_load()`, `js_module_export()`, `js_module_import()` |
| `main.cpp` | Module resolution (path → file → parse), cycle detection |

**Estimated LOC:** ~500 (transpiler/loader) + ~200 (runtime) = ~700

---

## 3. Implementation Plan

### Phase Ordering and Dependencies

```
Phase 1: Generators          (standalone — no event loop needed)
    │
    ▼
Phase 2: Promises            (needs microtask queue from Phase 3,
    │                         but Promise data model is independent)
    │
Phase 3: Event Loop + Timers (enables Promise resolution + setTimeout)
    │
    ▼
Phase 4: ES Modules          (standalone, but top-level await needs Phase 2+3)
```

**Practical ordering:** Implement Phases 1 and 3 first (independent), then Phase 2 (builds on both),
then Phase 4.

### Phased Delivery

| Phase | Feature | Est. LOC | Depends On | Deliverable |
|:-----:|---------|:--------:|:----------:|---|
| 1 | Generators / `yield` | ~800 | — | `function*`, `yield`, `for...of` on generators, `yield*` delegation |
| 3 | Event Loop + Timers | ~500 | — | `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, event loop drain |
| 2 | Promises + async/await | ~900 | Phase 1, 3 | `Promise`, `.then/.catch/.finally`, `Promise.all/race/any/allSettled`, `async`/`await` |
| 4 | ES Modules | ~700 | Phase 2, 3 (for top-level await) | `import`/`export`, module graph, namespace objects |
| — | **Total** | **~2,900** | | |

### Testing Strategy

Each phase adds tests under `test/js/`:

| Phase | Test Files | Coverage |
|-------|-----------|----------|
| 1 | `generator_basic.js`, `generator_delegation.js`, `generator_for_of.js` | Basic yield/next, yield*, for-of consumption, early return |
| 2 | `promise_basic.js`, `promise_chain.js`, `async_await.js`, `promise_combinators.js` | resolve/reject, .then chains, async/await, Promise.all/race/any |
| 3 | `timer_setTimeout.js`, `timer_setInterval.js`, `timer_clear.js` | Callback ordering, interval repetition, cancellation |
| 4 | `module_import.js`, `module_export.js`, `module_default.js`, `module_cycle.js` | Named/default imports, re-exports, circular dependency handling |

---

## 4. Detailed Design Decisions

### 4.1 Why State Machine over Stackful Coroutines for Generators

| Criterion | State Machine Transform | Stackful Coroutines (minicoro/setjmp) |
|-----------|:----------------------:|:-------------------------------------:|
| Portability | ✅ Pure C, any platform | ⚠️ Assembly per arch, or setjmp hacks |
| MIR compatibility | ✅ Standard functions | ⚠️ Stack switching may confuse MIR-gen |
| GC safety | ✅ All locals in heap array, visible to GC | ❌ Hidden stack frames, conservative scanner misses them |
| Debuggability | ✅ State index is inspectable | ❌ Opaque stack context |
| Performance | ⚠️ Indirect jumps, local save/restore | ✅ Faster context switch (single instruction) |
| Implementation complexity | Medium (~800 LOC transform) | Low runtime (~200 LOC) but fragile |

State machine wins on portability and GC safety — both critical for Lambda's design.

### 4.2 Microtask Queue Semantics

Per ECMAScript specification, Promise `.then`/`.catch`/`.finally` callbacks are **microtasks**,
not macrotasks. They run to completion before any macrotask (setTimeout callback) executes.
This means:

```js
setTimeout(() => console.log("macro"), 0);
Promise.resolve().then(() => console.log("micro"));
console.log("sync");
// Output: sync, micro, macro
```

The event loop must drain the entire microtask queue between each macrotask. This is
why libuv alone is insufficient — it has no concept of microtask priority.

### 4.3 Module Resolution Algorithm

Following Node.js-style resolution for file specifiers:

```
resolve("./utils", from="/path/to/main.js"):
    1. Try /path/to/utils.js        (exact + .js)
    2. Try /path/to/utils/index.js  (directory index)
    3. Error: module not found
```

Bare specifiers (`import "lodash"`) are **not supported** in v14 — only relative paths
(`./`, `../`) and absolute paths. Package/node_modules resolution is a future scope item.

### 4.4 Event Loop Termination

The event loop exits when **both** queues are empty:
- No pending microtasks
- No pending timers (or all timers cancelled)

This prevents infinite loops when all async work is complete. Unlike Node.js, there are
no I/O watchers or `ref`/`unref` semantics to manage — the loop is purely timer+microtask driven.

### 4.5 GC Considerations

Callbacks stored in the timer queue and microtask queue must be **GC roots**. Options:

1. **Register each callback as a GC root** via `heap_register_gc_root()` — simple but O(n) root set growth
2. **Store callbacks in a pool-allocated array** (invisible to GC, manually managed) — consistent with existing closure/function pattern
3. **Store in a Lambda Array** (GC-managed container) — GC traces it naturally

Option 2 (pool-allocated) is recommended, consistent with how closures and JsFunction
are already stored. Callbacks are `JsFunction*` pointers, pool-allocated and GC-invisible
by design.

---

## 5. File Layout (New and Modified)

| File | Status | Purpose |
|------|:------:|---------|
| `lambda/js/js_event_loop.h` | **New** | Event loop, timer heap, microtask queue declarations |
| `lambda/js/js_event_loop.cpp` | **New** | Event loop implementation (~300 LOC) |
| `lambda/js/js_promise.cpp` | **New** | Promise data model and resolution (~400 LOC) |
| `lambda/js/js_module.cpp` | **New** | ES module loader, resolution, namespace (~400 LOC) |
| `lambda/js/js_ast.hpp` | Modified | Add YIELD, AWAIT, IMPORT, EXPORT node types |
| `lambda/js/build_js_ast.cpp` | Modified | Parse generator/async/yield/await/import/export |
| `lambda/js/transpile_js_mir.cpp` | Modified | State machine emission, async transform, module compilation |
| `lambda/js/js_runtime.h` | Modified | Declare generator, promise, timer, module runtime functions |
| `lambda/js/js_runtime.cpp` | Modified | Generator runtime (~200 LOC) |
| `lambda/sys_func_registry.c` | Modified | Register new JS runtime imports |
| `lambda/main.cpp` | Modified | Event loop drain after js_main(), module resolution |
| `build_lambda_config.json` | Modified | Add new .cpp files to build |

---

## 6. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| State machine transform is complex for nested control flow (try/catch inside generator) | Generator correctness | Start with simple generators (no try/catch inside yield), add nesting support iteratively |
| Promise resolution ordering must match spec exactly | Compatibility | Port relevant Test262 promise ordering tests |
| Event loop may starve if microtask queue grows unboundedly | Hang / OOM | Add configurable microtask drain limit (e.g., 10,000 per turn) |
| ES module circular dependencies | Hang during linking | Implement spec's "linking" state detection; return partial namespace for cycles |
| GC during event loop may collect live callbacks | Crash | Pool-allocate all callbacks (consistent with existing design) |
| Cross-platform timer precision | Timer ordering bugs | Use `mach_absolute_time` (macOS) / `clock_gettime` (Linux) / `QueryPerformanceCounter` (Windows) — same as existing `performance.now()` |

---

## 7. Out of Scope (Future)

| Feature | Reason |
|---------|--------|
| `fetch()` / HTTP async | Requires async I/O integration with libcurl; defer to **Js15** |
| `Worker` / `SharedArrayBuffer` | Multi-threaded JS; defer to Lambda_Worker integration |
| `WeakRef` / `FinalizationRegistry` | GC hooks; requires GC callback support |
| `Proxy` / `Reflect` | Metaprogramming; orthogonal to async |
| Bare module specifiers (`import "react"`) | Package resolution; needs npm/package.json support |
| `ReadableStream` / `WritableStream` | Streaming API; defer to I/O iteration |
| `queueMicrotask()` | Low priority; trivial to add once microtask queue exists |
| `requestAnimationFrame()` | Rendering-specific; only relevant if Lambda adds a GUI loop |

---

## 8. Forward: Js15 — libuv Migration

Once v14 establishes the async runtime (custom event loop + microtask queue + timers),
**Js15 will migrate to libuv** as the platform I/O layer. This enables:

| Capability | v14 (Custom Loop) | v15 (libuv) |
|-----------|:-:|:-:|
| Timers (setTimeout/setInterval) | ✅ | ✅ |
| Microtask queue (Promises) | ✅ | ✅ (still custom on top) |
| Generators / yield | ✅ | ✅ |
| ES Modules (import/export) | ✅ | ✅ |
| Async `fetch()` (HTTP) | ❌ | ✅ via curl_multi + uv_poll |
| Async file I/O (`fs.readFile`) | ❌ | ✅ via uv_fs + threadpool |
| Child processes (`child_process`) | ❌ | ✅ via uv_spawn |
| `fs.watch` / file watching | ❌ | ✅ via uv_fs_event |
| Pipes / IPC | ❌ | ✅ via uv_pipe |

**Js15 migration plan (sketch):**
1. Add libuv as a build dependency (build from source, static link)
2. Replace custom timer heap with `uv_timer_t`
3. Rewrite `lib/serve/server.c` from libevent → libuv (`uv_tcp_t`)
4. Replace `network_thread_pool.h` with `uv_queue_work()`
5. Bridge `curl_multi` into `uv_poll_t` for async HTTP
6. Add async file I/O via `uv_fs_*` functions
7. Remove libevent dependency

The v14 custom event loop is designed to make this migration straightforward:
the `JsEventLoop` API (`js_microtask_enqueue`, `js_timer_set`, `js_event_loop_run`)
becomes a thin wrapper over libuv primitives rather than a from-scratch replacement.
