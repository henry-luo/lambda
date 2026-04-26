# LambdaJS Runtime — Detailed Design Reference

> Companion to [JS_Runtime.md](JS_Runtime.md). This document covers subsystem internals not covered in the overview.

---

## Table of Contents

1. [Property System & Prototype Chain](#1-property-system--prototype-chain)
2. [Closure Implementation](#2-closure-implementation)
3. [Class System](#3-class-system)
4. [Generator State Machines](#4-generator-state-machines)
5. [Iterator Protocol](#5-iterator-protocol)
6. [Array Destructuring & Spread/Rest](#6-array-destructuring--spreadrest)
7. [Promise & Microtask Queue](#7-promise--microtask-queue)
8. [Async Functions & Await](#8-async-functions--await)
9. [RegExp Engine](#9-regexp-engine)
10. [TypedArray & ArrayBuffer](#10-typedarray--arraybuffer)
11. [Collections (Map, Set, WeakMap, WeakSet)](#11-collections-map-set-weakmap-weakset)
12. [Date Objects](#12-date-objects)
13. [Proxy Objects](#13-proxy-objects)
14. [eval() Implementation](#14-eval-implementation)
15. [Strict Mode](#15-strict-mode)
16. [Template Literals & Tagged Templates](#16-template-literals--tagged-templates)
17. [Error Handling & Stack Overflow](#17-error-handling--stack-overflow)
18. [Module Variable System](#18-module-variable-system)
19. [test262 Batch Testing Infrastructure](#19-test262-batch-testing-infrastructure)
20. [Symbol System](#20-symbol-system)
21. [Node.js Compatibility Layer](#21-nodejs-compatibility-layer)
22. [File Layout (Current)](#22-file-layout-current)

---

## 1. Property System & Prototype Chain

### 1.1 Map Struct Layout

JS objects are Lambda `Map` structs with a `TypeMap` shape descriptor:

```
Map (heap-allocated)
├── type_id         : TypeId (LMD_TYPE_MAP)
├── flags           : uint8  (map_kind:4 bits, is_content, is_spreadable, ...)
├── type            : TypeMap* (shape descriptor, OR sentinel pointer for exotic types)
├── data            : void*  (packed field values buffer)
└── data_cap        : int    (buffer capacity)

TypeMap (shape descriptor)
├── shape           : ShapeEntry* (linked list head — all fields)
├── last            : ShapeEntry* (tail for O(1) append)
├── field_index[32] : ShapeEntry* (FNV-1a hash table, open addressing)
├── field_count     : uint8  (entries in hash table)
├── slot_entries    : ShapeEntry** (indexed array for constructor-optimized objects)
├── slot_count      : int
└── byte_size       : int64  (total data buffer size)

ShapeEntry (per-property metadata)
├── name            : StrView* (interned property name)
├── byte_offset     : int64  (offset into Map.data)
├── type            : Type*  (runtime value type)
└── next            : ShapeEntry* (linked list next)
```

### 1.2 Property Lookup (`js_property_get`)

Dispatch order for `LMD_TYPE_MAP`:

1. **MapKind fast path** — 4-bit discriminator in `flags` enables O(1) exotic type dispatch:
   - `MAP_KIND_PLAIN` (default) — proceeds to hash lookup
   - `MAP_KIND_TYPED_ARRAY` → `JsTypedArray` indexed access
   - `MAP_KIND_ARRAYBUFFER` → `byteLength` special key
   - `MAP_KIND_DATAVIEW` → DataView property dispatch
   - `MAP_KIND_DOM` / `MAP_KIND_CSSOM` → Radiant DOM bridge
   - `MAP_KIND_ITERATOR` → `[Symbol.iterator]` returns self
   - `MAP_KIND_PROXY` → `js_proxy_trap_get()`
   - `MAP_KIND_COLLECTION` → Map/Set method dispatch

2. **Key coercion** — non-string keys converted: int→string, float→string, symbol→`__sym_N`

3. **Own property lookup** via `js_map_get_fast(map, key, len)`:
   - Hash table first: FNV-1a hash → `field_index[hash % 32]`, pointer comparison (interned strings), `memcmp` fallback
   - Linear scan: walks `ShapeEntry` chain for hash overflow
   - Nested map scan: entries with `name == NULL` contain nested `Map*` (for spread objects)

4. **Getter check (own)** — looks for `__get_<propName>` property; if found and is FUNC, invokes with `this = receiver`

5. **Prototype chain** via `js_prototype_lookup()` — walks `__proto__` chain (max depth 32):
   ```
   proto = obj.__proto__
   while proto != null && depth < 32:
       result = js_map_get_fast(proto, key)
       if found: return result
       proto = proto.__proto__
   ```

6. **Built-in method fallback** — if `__class_name__` property exists (e.g., "Date", "RegExp"), routes to type-specific method table via `js_lookup_builtin_method()`

### 1.3 Property Attributes (Marker-Based)

Property attributes are stored as **companion properties** with special prefixes rather than in `ShapeEntry` metadata:

| Prefix | Meaning | Example |
|--------|---------|---------|
| `__ne_<name>` | Non-enumerable | `__ne_message = true` |
| `__nw_<name>` | Non-writable (read-only) | `__nw_length = true` |
| `__nc_<name>` | Non-configurable | `__nc_length = true` |
| `__get_<name>` | Getter accessor | `__get_name = <function>` |
| `__set_<name>` | Setter accessor | `__set_name = <function>` |
| `__frozen__` | Object is frozen | `__frozen__ = true` |
| `__sealed__` | Object is sealed | `__sealed__ = true` |
| `__non_extensible__` | Preventextensions | `__non_extensible__ = true` |

**Trade-off**: Simple implementation (no `ShapeEntry` changes needed), but roughly doubles the property count for heavily-attributed objects. Works well in practice since most JS objects have few attributed properties.

### 1.4 Object.defineProperty

Implemented in `ValidateAndApplyPropertyDescriptor()` (`js_globals.cpp`):

1. Validates descriptor (rejects mixed accessor+data, non-callable get/set)
2. Non-extensible check for new properties
3. Non-configurable enforcement: rejects configurable→true, enumerable change, accessor↔data conversion, non-writable value change (uses SameValue)
4. Data descriptors: sets value via `js_property_set` (temporarily clears `__nw_` to bypass guard)
5. Accessor descriptors: writes `__get_<name>` / `__set_<name>` markers
6. Sets attribute markers based on descriptor's `enumerable`/`writable`/`configurable`

### 1.5 Built-in Method Dispatch

Two dispatch mechanisms:

**`js_lookup_builtin_method(type, name, len)`** — name-based dispatch for:
- Object.prototype: `hasOwnProperty`, `propertyIsEnumerable`, `toString`, `valueOf`, `isPrototypeOf`
- Array.prototype: ~35 methods (push, pop, map, filter, reduce, sort, etc.)
- String.prototype: ~50 methods (charAt, slice, replace, match, trim, etc.)
- Function.prototype: call, apply, bind, toString
- Number.prototype: toString, valueOf, toFixed, toPrecision, toExponential

Returns lazily-created singleton `JsFunction` objects via `js_get_or_create_builtin(builtin_id)`. Invocation dispatches through `js_dispatch_builtin()`.

**`__class_name__` system** — for wrapper objects and exotic types:
- Property value: `"String"`, `"Number"`, `"Boolean"`, `"Date"`, `"RegExp"`, `"Symbol"`, `"BigInt"`
- `js_property_get` checks `__class_name__` first, routes to type-specific prototype
- `__primitiveValue__` stores the wrapped primitive value

### 1.6 Constructor Shape Pre-allocation (A5)

When compiling a class constructor, the transpiler:

1. Pre-scans constructor body for `this.prop = expr` patterns
2. Creates a `TypeMap` with all property slots pre-built and hash-indexed
3. `js_constructor_create_object_shaped_cached()` clones this TypeMap for each new instance
4. Property writes find existing keys → in-place updates (O(1))
5. `js_get_shaped_slot(obj, N)` / `js_set_shaped_slot(obj, N, val)` — index-based access that bypasses hash lookup entirely

---

## 2. Closure Implementation

Closures use a **shared scope environment** model where all child closures of the same parent share a single `uint64_t[]` array, enabling mutable capture semantics.

### 2.1 Capture Analysis (Phases 1.5–1.7)

Three transpiler phases build the closure environment:

1. **Phase 1.5 — Free variable detection**: For each function, computes `free_vars = refs - params - locals - module_consts`. Any variable referenced but not locally defined is a capture candidate.

2. **Phase 1.6 — Transitive propagation**: Fixed-point iteration propagates captures through nested functions. If `inner()` captures `x` from `outer()`, and `middle()` contains `inner()`, then `middle()` must also capture `x` to thread it through.

3. **Phase 1.7 — Scope env computation**: For each function that has children with captures, allocates a scope env layout. Each captured variable gets a slot index in the parent's `scope_env[]` array.

### 2.2 Scope Environment Allocation

At runtime, the parent function allocates the shared environment:

```c
// Parent function entry:
scope_env = js_alloc_env(slot_count);    // uint64_t[slot_count], GC-rooted
```

All captured local variables are **backed by env slots** rather than MIR registers. The transpiler emits reads/writes to `scope_env[N]` instead of register operations for these variables.

### 2.3 Closure Creation

```c
Item js_new_closure(void* func_ptr, int param_count, uint64_t* env, int env_size);
```

Creates a `JsFunction` struct with:
- `func_ptr` — JIT-compiled function pointer
- `env` — pointer to parent's scope env (shared, not copied)
- `env_size` — number of captured slots

Child functions receive `env` as a hidden first parameter. The MIR signature becomes:
```
func_name(env: i64, arg0: i64, arg1: i64, ...) → i64
```

### 2.4 Mutable Capture Semantics

Because all closures share the same `scope_env` array, mutations are immediately visible:

```javascript
function counter() {
    let count = 0;                  // → scope_env[0]
    return {
        inc: () => ++count,         // reads/writes scope_env[0]
        get: () => count            // reads scope_env[0]
    };
}
// inc() and get() share the same scope_env array → both see mutations
```

**Writeback rule**: Any assignment to a captured variable emits an immediate store to the env slot:
```
// JS: count = count + 1
reg_tmp = MIR_ADD(env[0], 1)
env[0] = reg_tmp               // immediate writeback
```

### 2.5 Multi-Level Nesting

For deeply nested closures (grandchild captures from grandparent), each intermediate function must include the variable in its own env:

```javascript
function outer() {
    let x = 1;                     // outer.scope_env[0]
    function middle() {
        // middle.scope_env[0] = outer.scope_env[0]  (threaded through)
        function inner() {
            return x;               // reads middle.scope_env[0]
        }
        return inner;
    }
    return middle;
}
```

The transpiler copies the slot from parent env to child env at child function entry.

### 2.6 Interaction with Generators

Generator functions store their entire env in the `JsGenerator.env` array. On yield, all env-backed locals are already in the env (no additional save needed). On resume, registers are reloaded from env slots.

---

## 3. Class System

Classes compile to prototype-based constructor functions using the `JsClassEntry` / `JsClassMethodEntry` collection system.

### 3.1 Class Collection (Phase 1.0)

During the function collection phase, each `class` declaration produces a `JsClassEntry`:

```c
struct JsClassEntry {
    const char* name;               // class name
    JsAstNode* constructor_node;    // constructor AST (or NULL for default)
    JsAstNode* extends_node;        // superclass expression (or NULL)
    JsClassMethodEntry methods[64]; // prototype + static methods
    int method_count;
    JsClassFieldEntry fields[32];   // instance + static fields
    int field_count;
    int module_var_idx;             // module var slot for the constructor
    int proto_module_var_idx;       // module var slot for the prototype
};
```

### 3.2 Constructor Compilation

The constructor compiles to a regular function with special prologue/epilogue:

**Prologue:**
1. `obj = js_constructor_create_object()` — allocates new Map
2. Sets `obj.__proto__ = Constructor.prototype`
3. If shape pre-allocation (A5): `obj = js_constructor_create_object_shaped_cached(type_map)` — clones pre-built TypeMap with all slots

**Body:**
- `this` references resolve to `obj`
- `this.prop = expr` compiles to `js_property_set(obj, "prop", val)` (or `js_set_shaped_slot(obj, N, val)` with A5)

**Epilogue:**
- If constructor has no explicit `return obj_expr`, returns `obj`
- If explicit `return obj_expr`: returns `obj_expr` if it's an object, else `obj`

### 3.3 Shape Pre-Allocation (Optimization A5)

For constructors with predictable property patterns:

1. **Pre-scan**: Transpiler walks constructor body collecting `this.prop = expr` patterns
2. **TypeMap creation**: Builds a `TypeMap` with `ShapeEntry` for each discovered property, hash-indexed
3. **Cached allocation**: `js_constructor_create_object_shaped_cached()` clones this TypeMap for each `new` call
4. **Indexed access**: Property writes use `js_set_shaped_slot(obj, slot_index, val)` — O(1) direct offset write, no hash lookup

### 3.4 Method Compilation

Prototype methods:
1. Each method compiles as a regular function (with `this` as first logical parameter)
2. At class definition time: `Constructor.prototype.methodName = method_func`
3. Method calls on known-class instances can be devirtualized (P7)

Static methods:
1. Compile as regular functions
2. Stored on the constructor object: `Constructor.staticName = static_func`

### 3.5 Inheritance (`extends`)

```javascript
class Child extends Parent { ... }
```

Compiles to:
1. `Child.prototype = Object.create(Parent.prototype)` — sets up prototype chain
2. `Child.prototype.constructor = Child`
3. `Child.__proto__ = Parent` — for static method inheritance

**`super()` calls:**
- In constructor: `Parent.call(this, ...args)` — calls parent constructor with child's `this`
- In methods: `Parent.prototype.method.call(this, ...args)` — explicit parent dispatch

### 3.6 Computed Property Names & Private Members

**Computed properties:**
```javascript
class Foo {
    [Symbol.iterator]() { ... }  // key evaluated at class definition time
}
```
Key expression evaluated once, result used as property name string.

**Private fields/methods** (`#name`):
- Stored with mangled names: `#field` → `__private_field` (or similar internal prefix)
- Access checks should verify brand (the object was constructed by the class) — **partially implemented**

### 3.7 Compile-Time Method Resolution (Optimization P7)

When the transpiler can prove a variable holds an instance of a specific class:
1. Method lookup is resolved at compile time to a concrete function pointer
2. Call emits a direct `MIR_CALL` to the method function, bypassing prototype chain walk
3. Applies to: constructor-local `this`, variables immediately after `new ClassName()`

---

## 4. Generator State Machines

### 4.1 Generator Struct

```c
struct JsGenerator {
    TypeId type_id;           // LMD_TYPE_MAP
    void*  state_fn;          // JIT-compiled state machine function pointer
    Item*  env;               // closure environment array
    int    env_size;
    int64_t state;            // 0=initial, 1..63=resume points, -1=done
    bool   done;
    bool   executing;         // re-entrance guard
    bool   is_async;
    Item   delegate;          // active yield* target (ItemNull when none)
    int64_t delegate_resume;  // state to resume after delegation exhausted
    int    delegate_idx;
};
```

**Storage**: Fixed pool of `js_generators[4096]` — no heap allocation per generator. Generator objects are Lambda Maps with a hidden `__gen_idx` property storing the pool index.

### 4.2 Compilation Model

Each generator function compiles into **two MIR functions**:

1. **Wrapper function** — allocates env array, copies captures/params into env slots, calls `js_generator_create(state_machine_fn, env, env_size, is_async)`, returns generator object

2. **State machine function** — `gen_sm_<name>(Item* env, Item input, int64_t state) → Item`

**Env layout**: `[captures | params | locals | this_slot | arguments_slot | 32 for-of padding | 16 spill slots]`

**State dispatch**: Linear chain of `if (state == N) goto LN` (capped at 64 states).

### 4.3 Yield Emission

For each `yield expr`:

1. Save all env-backed locals: `env[slot] = reg`
2. Return `js_gen_yield_result(value, next_state)` → 2-element array `[value, next_state]`
3. Emit resume label for state N
4. Reload all env-backed locals: `reg = env[slot]`
5. Re-initialize try-finally state registers
6. Yield expression evaluates to `gen_input` register (the value passed to `.next(arg)`)

### 4.4 yield* Delegation

- Returns `js_gen_yield_delegate_result(iterable, resume_state)` → 3-element array `[iterable, resume_state, 1]`
- Runtime detects the flag, sets `gen->delegate`, recursively drains the delegate iterator
- Non-generator iterables are eagerly materialized via `js_iterable_to_array()` for delegation (trade: lazy delegation for simplicity)
- Generator-to-generator delegation is properly lazy

### 4.5 Limitations

- Max 64 yield points per generator (state machine caps at 63 resume labels)
- `generator.return()` and `generator.throw()` set `done=true` and `state=-1` — no attempt to resume into try-finally blocks (generator cleanup code won't run)
- Fixed pool of 4096 generators; recycling on pool exhaustion

---

## 5. Iterator Protocol

### 5.1 Sentinel-Based Done Signal

```c
#define JS_ITER_DONE_SENTINEL (0x7F00DEAD00000000ULL)
```

Uses type tag `0x7F` (unused in the tagged-pointer scheme) — cannot collide with any valid JS value including `null`, `undefined`, `false`, `0`, or empty string. Avoids allocating `{done: true, value: undefined}` result objects.

### 5.2 Iterator Creation (`js_get_iterator`)

Dispatch chain:
1. `null`/`undefined` → throw TypeError
2. **Array** → `js_create_array_iterator()` (fast path)
3. **String** → `js_create_string_iterator()` (fast path)
4. **Generator** → return as-is
5. **TypedArray** → `js_create_typed_array_iterator()` (fast path)
6. **Map/Set collection** → drain to array, wrap in array iterator
7. **Object with `[Symbol.iterator]`** → call factory, return result
8. **Object with `.next()`** → return as-is (already an iterator)
9. Otherwise → throw TypeError

### 5.3 Fast-Path Iterators (MAP_KIND_ITERATOR)

Lightweight `Map` objects with fixed-layout `JsIterData`:

```c
struct JsIterData {
    Item source;     // the iterable (array/string/typed array)
    int64_t index;   // current position
    int64_t length;  // snapshot length (-1 for live-length arrays)
};
```

Sub-type identified by sentinel marker in `m->type`:
- `js_array_iter_marker` — direct `array->items[index]` read
- `js_string_iter_marker` — UTF-8 code point advance (handles WTF-8/CESU-8 surrogate pairs)
- `js_typed_array_iter_marker` — `js_typed_array_get(source, index)`

**Zero hash lookups per step** — fast path reads `JsIterData` directly from `Map.data`, no property name resolution.

### 5.4 Iterator Step (`js_iterator_step`)

Returns the next value, or `JS_ITER_DONE_SENTINEL` when exhausted:

1. `null`/`undefined` → done
2. `MAP_KIND_ITERATOR` fast path — direct memory access per sub-type
3. Legacy synthetic iterators (`__arr__`/`__str__`/`__tarr__` properties) — backward compat
4. Generator → `js_generator_next()`, unwraps `{done, value}`
5. Generic iterator → calls `.next()`, unwraps `{done, value}`

### 5.5 For-of Compilation Pattern

```
iterator = js_get_iterator(iterable)
if (exception) goto l_iter_err

[push synthetic try context for IteratorClose]

l_test:
    step_result = js_iterator_step(iterator)
    if (step_result == JS_ITER_DONE_SENTINEL) goto l_end
    loop_var = step_result
    [destructuring if pattern]
    [body]
l_update:
    goto l_test

l_break:                    // break → close iterator
    js_iterator_close(iterator)
l_end:

l_iter_err:                 // exception → save, close, re-throw
    saved = js_clear_exception()
    js_iterator_close(iterator)
    js_throw_value(saved)

l_forit_ret:                // return-from-loop → close, propagate
    js_iterator_close(iterator)
    [propagate return]
```

**Design decisions**:
- Synthetic try context wraps the body to intercept exceptions for IteratorClose (ES §13.7.5.13)
- `for_of_iterators[32]` stack tracks nested iterators for cleanup on `return`
- Generator-aware: iterator and loop variable stored as env-backed variables (`_foriter_`, `_forlv_`) to survive yield

---

## 6. Array Destructuring & Spread/Rest

### 6.1 Array Destructuring (`jm_emit_array_destructure`)

Uses step-by-step iterator protocol (ES §13.3.3.6):

```
iterator = js_get_iterator(src)
if (exception) goto skip
iter_done = 0

for each element in pattern:
    if ELISION (,):
        if (!iter_done) step = js_iterator_step(iterator)
        if done: iter_done = 1
    elif REST (...x):
        if (!iter_done) rest = js_iterator_collect_rest(iterator); iter_done = 1
        else: rest = []
        bind rest → target
    else (regular binding):
        if (iter_done) goto assign_undef
        step = js_iterator_step(iterator)
        if (step == DONE_SENTINEL) { iter_done = 1; goto assign_undef }
        bind step → target
        goto end
        assign_undef: bind undefined → target
        end:

skip:
```

**Key**: Each elision comma calls `IteratorStep` exactly once (advancing generators correctly). The `iter_done` flag prevents stepping past exhaustion.

**Yield-in-destructuring fallback**: When destructuring defaults contain `yield` expressions, falls back to eager `js_iterable_to_array()` + index-based access (yield inside conditional MIR branches is not supported).

### 6.2 Object Destructuring (`jm_emit_object_destructure`)

1. `js_require_object_coercible(src)` → TypeError if null/undefined
2. For each property: extract key (literal or computed), call `js_property_get(src, key)`, recurse for nested patterns
3. For rest element `{...rest}`: collect exclude keys into array, call `js_object_rest(src, exclude_keys, exclude_count)` which iterates own enumerable properties, skipping excluded keys and `__*` internal markers

### 6.3 Spread in Call Arguments

When a call argument is `...expr`:
1. Evaluate argument
2. `js_iterable_to_array()` to materialize
3. Loop over result, push each element individually as call argument

---

## 7. Promise & Microtask Queue

### 7.1 Promise Struct

```c
struct JsPromise {
    JsPromiseState state;           // pending / fulfilled / rejected
    Item result;                    // fulfillment value or rejection reason
    Item on_fulfilled[8];           // then callbacks (max 8 per promise)
    Item on_rejected[8];
    int  next_promise[8];           // chained promise indices
    bool is_finally[8];
    int  handler_count;
};
```

**Storage**: Static array of `js_promises[1024]`. Promise objects are Lambda Maps with a hidden `__promise_idx` property.

### 7.2 Microtask Queue

Ring buffer in `js_event_loop.cpp`:
- `js_microtask_enqueue(callback)` — pushes callback
- `js_microtask_flush()` — drains all (including newly enqueued during execution), with `CAPACITY*2` safety limit
- Called after each top-level statement in batch mode

### 7.3 Methods Implemented

- `Promise.resolve()`, `Promise.reject()`
- `.then()`, `.catch()`, `.finally()`
- `Promise.all()`, `Promise.race()`, `Promise.any()`, `Promise.allSettled()`
- `Promise.withResolvers()`

**Limitations**: Max 1024 promises per run, max 8 chained handlers per promise, no thenable assimilation (only native promises recognized).

---

## 8. Async Functions & Await

### 8.1 Compilation

Async functions compile to state machines reusing generator infrastructure (`in_generator=true`, `in_async=true`):

```c
struct JsAsyncContext {
    void* state_fn;    // JIT-compiled state machine
    Item* env;         // closure env array
    int state;
    int promise_idx;   // backing promise
};
```

State machine signature: `async_sm_<name>(Item* env, Item input, int64_t state) → Item`

### 8.2 Await Semantics

Each `await expr` compiles to a yield point:
1. If value is a resolved promise → fast path via `js_async_must_suspend()` returns immediately
2. If pending → registers `js_async_resume_handler` / `js_async_reject_handler` on the promise
3. Resume re-enters state machine at the next state label

**Limits**: Max 256 async contexts, max 63 await points per function.

---

## 9. RegExp Engine

### 9.1 Storage

```c
struct JsRegexData {
    re2::RE2* re2;              // compiled RE2 pattern
    JsRegexCompiled* compiled;  // wrapper for post-filters
    bool global, ignore_case, multiline, sticky;
};
```

RegExp objects are Maps with a `JsRegexData*` in `Map.data` and `__class_name__` = `"RegExp"`.

### 9.2 JS-to-RE2 Transpilation

`js_regex_wrapper.cpp` converts JS-specific constructs into RE2-compatible patterns with post-processing filters:

| JS Feature | RE2 Translation | Post-Filter |
|-----------|----------------|-------------|
| Positive lookahead `X(?=Y)` | `X(Y)` | `PF_TRIM_GROUP` |
| Negative lookahead `X(?!Y)` | `X(Y)` | `PF_REJECT_MATCH` |
| Backreferences `\N` | `(.+)` | `PF_GROUP_EQUALITY` |
| Named groups `(?<name>...)` | Regular groups | Name→index mapping |

Max 16 post-filters and 16 capture groups per regex.

### 9.3 Caching

`unordered_map<uint64_t, JsRegexCacheEntry>` keyed by pattern string pointer identity. Benefits regex literals in loops but not `new RegExp()`.

### 9.4 Static Properties

`JsRegexpLastMatch` tracks `$1`–`$9` captures and `RegExp.lastMatch` / `RegExp.lastParen` / `RegExp.input`.

---

## 10. TypedArray & ArrayBuffer

### 10.1 Structs

```c
struct JsTypedArray {
    int element_type;    // Int8..Float64 (9 types)
    int length;
    int byte_length;
    int byte_offset;
    void* data;          // raw C buffer
    JsArrayBuffer* buffer;
};

struct JsArrayBuffer {
    void* data;
    int byte_length;
    bool detached;
    bool is_shared;
};
```

### 10.2 Storage

TypedArrays are Map objects with `map_kind = MAP_KIND_TYPED_ARRAY`. The `JsTypedArray*` is in `Map.data`. The transpiler has inline fast-path code that reads `JsTypedArray*` directly from `Map.data` (offset 16), then accesses `ta->length` and `ta->data` for zero-overhead indexed reads.

### 10.3 Supported Types

Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array (9 types). BigInt64Array/BigUint64Array are not yet implemented.

### 10.4 DataView

`JsDataView` struct with `buffer`, `byte_offset`, `byte_length`. Map objects with `map_kind = MAP_KIND_DATAVIEW`. Methods: `getInt8`..`getFloat64`, `setInt8`..`setFloat64`, with endianness parameter.

---

## 11. Collections (Map, Set, WeakMap, WeakSet)

### 11.1 Data Structure

```c
struct JsCollectionData {
    HashMap* hmap;                     // lib/hashmap.h (SipHash)
    int type;                          // JS_COLLECTION_MAP or JS_COLLECTION_SET
    bool is_weak;
    JsCollectionOrderNode* order_head; // doubly-linked list for insertion order
    JsCollectionOrderNode* order_tail;
};
```

Each collection is a Map object. `JsCollectionData*` stored as int64 in a `__cd` property.

### 11.2 Insertion Order

ES6 requires Map/Set iteration in insertion order. This is maintained by a **doubly-linked list** (`JsCollectionOrderNode`) alongside the hash table:
- `js_collection_order_upsert()` — updates existing in-place (preserves order) or appends
- `js_collection_order_remove()` — unlinks from list
- `size` stored as a regular property, updated after each mutation

### 11.3 Dispatch

`js_collection_method(obj, method_id, arg1, arg2)` with IDs: set=0, get=1, has=2, delete=3, clear=4, forEach=5.

### 11.4 WeakMap/WeakSet

Same struct with `is_weak = true`. **No actual weak reference semantics** — no GC integration. Objects are retained normally. This is a known limitation.

---

## 12. Date Objects

Date objects are Maps with `_time` property (epoch-ms as float64) and `__class_name__` = `"Date"`.

**Method dispatch**: `js_date_method(date_obj, method_id)` with integer IDs:
- 0-7: getTime, getFullYear, getMonth, getDate, getHours, getMinutes, getSeconds, getMilliseconds
- 8: toISOString, 9: toLocaleDateString, 10-16: getUTC* variants, 17: toString
- Setters via `js_date_setter()` (setFullYear, setMonth, etc.)

**Static methods**: `Date.now()`, `Date.parse()`, `Date.UTC()`. Parsing uses `strptime` with ISO 8601 + locale fallbacks.

---

## 13. Proxy Objects

Fully implemented with all 13 ES6 traps.

### 13.1 Storage

```c
typedef struct JsProxyData {
    uint64_t target;   // [[ProxyTarget]] as Item bits
    uint64_t handler;  // [[ProxyHandler]] as Item bits
    bool revoked;
} JsProxyData;
```

Map objects with `map_kind = MAP_KIND_PROXY`. `Map.data` points to `JsProxyData`.

### 13.2 Trap Dispatch

Each `js_proxy_trap_*()` function:
1. Check `revoked` → TypeError if true
2. Look up trap name on handler via `js_map_get_fast`
3. No trap → forward to target (transparent proxy)
4. Call trap with spec-defined arguments
5. Perform **invariant checks** (non-configurable non-writable data must match)

### 13.3 Traps Implemented

`get`, `set`, `has`, `deleteProperty`, `ownKeys`, `getOwnPropertyDescriptor`, `defineProperty`, `getPrototypeOf`, `setPrototypeOf`, `isExtensible`, `preventExtensions`, `apply`, `construct`.

`Proxy.revocable()` creates a revoke function that sets `revoked = true`.

---

## 14. eval() Implementation

### 14.1 Three-Phase Execution

`js_builtin_eval(code_item, is_global_scope)` in `transpile_js_mir.cpp`:

1. **Expression form**: wraps code as `return (code\n)` inside anonymous function → compile + call
2. **Statement form with return insertion**: `eval_try_insert_return()` prepends `return` before last expression statement
3. **Direct script fallback**: parses + compiles as top-level script using `eval_completion_reg`

**Fast path**: RegExp literals (`/pattern/flags`) bypass JIT entirely.

### 14.2 Known Limitation

The expression wrapping `return (code)` causes parse errors for code that isn't a valid expression (e.g., `var` declarations, multiple statements, certain control flow). This is a major source of test262 failures in the `eval-code/direct` category (~268 tests).

---

## 15. Strict Mode

### 15.1 Detection

`detect_strict_mode()` in `js_early_errors.cpp` checks first statement for `"use strict"` directive.

### 15.2 Propagation

Two-step pass in transpiler:
1. Mark functions with own directive or global strict mode
2. Propagate to child functions (strict parent → strict children)
3. At function entry, emit `js_set_strict_mode(1/0)` to set global flag

### 15.3 Runtime Enforcement

Global `bool js_strict_mode` flag:
- Non-writable property writes → TypeError (via `js_strict_throw_property_error()`)
- Arguments.callee/caller access → TypeError
- Early errors: reserved words disallowed as identifiers

**Limitation**: Per-function strict mode is approximated by a global flag set/reset at function entry, not tracked per-scope.

---

## 16. Template Literals & Tagged Templates

### 16.1 Regular Templates

Built via `StringBuf`:
1. Allocate `stringbuf_new(pool)`
2. Append static quasis as string literals
3. Evaluate interpolated expressions via `js_to_string`
4. `stringbuf_to_string` → final boxed string

### 16.2 Tagged Templates

1. Build parallel `cooked[]` and `raw[]` arrays from template elements (cooked may be `undefined` for invalid escapes per spec)
2. Call `js_build_template_object(cooked, raw, count)` to create template strings array with `.raw` property
3. Call tag function with `[template_object, ...expressions]`

`String.raw` implemented as `JS_BUILTIN_STRING_RAW`.

---

## 17. Error Handling & Stack Overflow

### 17.1 Exception State

```c
static bool js_exception_pending;
static Item js_exception_value;
static char js_exception_msg[1024];
```

### 17.2 Throw Functions

| Function | Error Type |
|----------|-----------|
| `js_throw_type_error(msg)` | TypeError |
| `js_throw_range_error(msg)` | RangeError |
| `js_throw_syntax_error(msg)` | SyntaxError |
| `js_throw_reference_error(msg)` | ReferenceError |
| `js_throw_value(item)` | Any value (user throw) |

Error objects are Lambda Maps with: `message`, `stack`, `__class_name__`, `constructor`, `__proto__` → ErrorType.prototype. Attribute markers: `__ne_message`, `__ne_stack`, `__ne_constructor`.

### 17.3 JIT Try/Catch

Try context stack (max 16 depth):
- After each statement in try body, emit `js_check_exception()` → branch to `catch_label`
- Catch calls `js_clear_exception()` to retrieve thrown value
- Finally always executes; return-in-try saved to register, finally still runs

### 17.4 Stack Overflow

**Signal-based (zero per-call overhead)**:
- `lambda_stack_init()`: installs SIGSEGV handler on alternate 128KB signal stack
- Handler checks fault address against stack guard region (64KB window)
- On overflow: `siglongjmp` → RangeError "Maximum call stack size exceeded"
- TCO loop guard: MIR BLE against 1M iterations

---

## 18. Module Variable System

### 18.1 Storage

- Fixed `JS_MAX_MODULE_VARS = 2048` static array, registered as GC root
- Transpiler assigns indices at compile time for function/class/var declarations
- `js_active_module_vars` pointer switches between static fallback and per-module allocated arrays

### 18.2 Preamble Interaction (test262 Batch)

- Harness compiled once → function objects stored in `module_vars[0..N]`
- `JsPreambleState` captures name→index mappings
- Subsequent test compilations inherit entries, start at index `N+1`
- `js_batch_reset_to(N)` preserves harness vars, zeros test vars

### 18.3 Nested Require

`js_save_module_vars()` / `js_restore_module_vars()` for nested `require()` calls.

---

## 19. test262 Batch Testing Infrastructure

### 19.1 Architecture

GTest harness spawns `lambda.exe js-test-batch` workers via `posix_spawn`:
- **50 tests/batch**, **12 parallel workers**
- 10s per-test timeout, 4GB RSS limit, max 10 crashes per worker
- Communication over stdin/stdout pipes with `\x01`-prefixed binary protocol

### 19.2 Protocol

```
harness:<length>\n<blob>              # sta.js + assert.js (compiled once)
source:<test_name>:<length>\n<blob>   # test source
```

Worker responses:
```
\x01 BATCH_START <path>
\x01 BATCH_END <exitcode> <elapsed_us> <rss_before> <rss_after>
\x01 BATCH_EXIT <reason>
```

### 19.3 Phases

| Phase | Purpose |
|-------|---------|
| **Phase 1** | Parse YAML metadata, partition into CLEAN and PARTIAL |
| **Phase 2** | Execute CLEAN tests in batched workers |
| **Phase 2a** | Execute PARTIAL tests individually (batch=1) |
| **Phase 2b** | Retry batch-lost tests individually |
| **Phase 3** | Evaluate results, classify non-fully-passing |
| **Phase 4** | Retry regressions individually; recovered → batch-unstable |

### 19.4 Crash Recovery (Three Layers)

**Layer 1 — Per-test crash** (SIGSEGV/SIGBUS/SIGABRT):
- 128KB alternate signal stack via `sigaltstack`
- `batch_crash_handler` → `siglongjmp` back to test loop
- Recovery: `heap_destroy()` + rebuild + preamble recompile from `saved_harness_src`

**Layer 2 — Per-test timeout** (SIGALRM):
- `alarm(10)` per test, `batch_alarm_handler` → `siglongjmp`

**Layer 3 — MIR compilation errors**:
- `batch_mir_error_handler` → `longjmp(mir_error_jmp, 1)`

**Parent-side**: per-worker watchdog thread kills via SIGKILL if hard timeout exceeded.

### 19.5 Batch Reset

**`js_batch_reset_to(checkpoint)`** — normal between-test reset:
- Zeros module vars beyond checkpoint
- Resets: exception state, all cached globals, constructor prototypes, generators/promises, RegExp statics, regex cache, event loop, 20+ Node.js module caches

**`js_batch_reset()`** — full reset after crash/timeout:
- Increments `js_heap_epoch` to invalidate cached objects
- Zeros entire module var table
- Full module registry clear

**Preserved**: GC heap, nursery, name_pool, preamble MIR context + compiled code pages.

### 19.6 Baseline Management

- **File**: `test/js262/test262_baseline.txt`
- **Update gate**: 0 regressions, 0 batch-lost, 0 crash-exits, count ≥ `STABLE_BASELINE_MIN` (21824)
- **Partial list**: `test/js262/t262_partial.txt` with tags: `CRASH_N`, `SLOW_N`, `BATCH_KILL`, `TIMEOUT_N`

---

## 20. Symbol System

### 20.1 Encoding

Symbols are negative ints: `-(id + JS_SYMBOL_BASE)` where `JS_SYMBOL_BASE = 1<<40`. Property keys use `__sym_N` internal string format.

### 20.2 Well-Known Symbols

| ID | Internal Key | Symbol |
|----|-------------|--------|
| 1 | `__sym_1` | `Symbol.iterator` |
| 2 | `__sym_2` | `Symbol.toPrimitive` |
| 3 | `__sym_3` | `Symbol.hasInstance` |
| 4 | `__sym_4` | `Symbol.toStringTag` |
| 5 | `__sym_5` | `Symbol.asyncIterator` |
| 6 | `__sym_6` | `Symbol.species` |
| 7 | `__sym_7` | `Symbol.match` |
| 8-13 | `__sym_8`–`__sym_13` | replace, search, split, unscopables, isConcatSpreadable, matchAll |

User-created symbols get IDs starting at 100.

### 20.3 Transpiler Handling

`Symbol.iterator` etc. are statically resolved to `js_symbol_well_known(name)` calls. Runtime property dispatch hardcodes `__sym_1` checks for `[Symbol.iterator]` access.

---

## 21. Node.js Compatibility Layer

The engine includes partial Node.js API compatibility across multiple files:

| Module | File | Features |
|--------|------|----------|
| `fs` | `js_fs.cpp` (1794 LOC) | readFileSync, writeFileSync, existsSync, stat, mkdir, readdir, unlink, rename |
| `path` | `js_path.cpp` (1084 LOC) | join, resolve, dirname, basename, extname, parse, format, normalize, relative |
| `buffer` | `js_buffer.cpp` (2473 LOC) | Buffer.from, Buffer.alloc, toString, slice, concat, copy, compare, indexOf |
| `crypto` | `js_crypto.cpp` (1753 LOC) | createHash (md5, sha1, sha256, sha512), randomBytes, randomUUID |
| `http`/`https` | `js_http.cpp` (1322), `js_https.cpp` (123) | createServer, request, get |
| `net` | `js_net.cpp` (652 LOC) | Socket, Server, createServer, connect |
| `child_process` | `js_child_process.cpp` (654 LOC) | execSync, spawnSync |
| `events` | `js_events.cpp` (564 LOC) | EventEmitter: on, emit, once, removeListener |
| `stream` | `js_stream.cpp` (848 LOC) | Readable, Writable, Transform, PassThrough |
| `url` | `js_url_module.cpp` (763 LOC) | URL, URLSearchParams |
| `querystring` | `js_querystring.cpp` (458 LOC) | parse, stringify |
| `zlib` | `js_zlib.cpp` (460 LOC) | gzipSync, gunzipSync, deflateSync, inflateSync |
| `os` | `js_os.cpp` (723 LOC) | platform, arch, hostname, tmpdir, cpus, totalmem, freemem |
| `dns` | `js_dns.cpp` (220 LOC) | lookup, resolve4 |
| `readline` | `js_readline.cpp` (127 LOC) | createInterface |
| `string_decoder` | `js_string_decoder.cpp` (154 LOC) | StringDecoder |
| `assert` | `js_assert.cpp` (1181 LOC) | ok, equal, strictEqual, deepStrictEqual, throws, doesNotThrow |
| `util` | `js_util.cpp` (1215 LOC) | inspect, format, types, promisify |

Module resolution via `require()` / CommonJS pattern in `module_registry.cpp`.

---

## 22. File Layout (Current)

| File | Lines | Purpose |
|------|------:|---------|
| `transpile_js_mir.cpp` | 27,963 | Core MIR transpiler: AST → MIR IR, type inference, closures, classes, generators, async |
| `js_runtime.cpp` | 21,296 | Runtime: operators, property access, prototype chain, iterators, generators, collections, batch reset |
| `js_globals.cpp` | 11,151 | Built-in objects: Object.*, JSON, Date, Symbol, Math, Reflect, constructors, URI, GOPD |
| `build_js_ast.cpp` | 3,957 | AST builder: Tree-sitter JS CST → typed JsAstNode tree |
| `js_dom.cpp` | 3,820 | DOM bridge: wraps Radiant DomElement as JS Maps |
| `js_buffer.cpp` | 2,473 | Node.js Buffer implementation |
| `js_fs.cpp` | 1,794 | Node.js fs module |
| `js_crypto.cpp` | 1,753 | Node.js crypto module |
| `js_cssom.cpp` | 1,355 | CSSOM bridge (getComputedStyle, CSSStyleDeclaration) |
| `js_http.cpp` | 1,322 | Node.js http module |
| `js_util.cpp` | 1,215 | Node.js util module |
| `js_assert.cpp` | 1,181 | Node.js assert module |
| `js_path.cpp` | 1,084 | Node.js path module |
| `js_typed_array.cpp` | 1,072 | TypedArray + ArrayBuffer + DataView |
| `js_early_errors.cpp` | 1,063 | Early error detection, strict mode validation |
| `js_stream.cpp` | 848 | Node.js stream module |
| `js_event_loop.cpp` | 802 | Event loop, microtask queue, timers |
| `js_regex_wrapper.cpp` | 780 | JS regex → RE2 transpilation + post-filters |
| `js_url_module.cpp` | 763 | Node.js url module (URL, URLSearchParams) |
| `js_runtime.h` | 741 | Runtime C API declarations |
| `js_os.cpp` | 723 | Node.js os module |
| `js_child_process.cpp` | 654 | Node.js child_process module |
| `js_net.cpp` | 652 | Node.js net module |
| `js_ast.hpp` | 624 | AST node types (~45 types, ~50 operators) |
| `js_tls.cpp` | 623 | TLS/SSL support |
| `js_events.cpp` | 564 | Node.js events (EventEmitter) |
| `js_xhr.cpp` | 551 | XMLHttpRequest |
| `js_dom_events.cpp` | 505 | DOM event handling |
| `js_zlib.cpp` | 460 | Node.js zlib module |
| `js_querystring.cpp` | 458 | Node.js querystring module |
| `js_scope.cpp` | 440 | Scope management: var/let/const semantics |
| `js_canvas.cpp` | 417 | Canvas 2D API |
| `js_fetch.cpp` | 405 | Fetch API |
| `js_dom.h` | 302 | DOM API declarations |
| `js_transpiler.hpp` | 256 | Transpiler context struct |
| `js_dns.cpp` | 220 | Node.js dns module |
| `js_print.cpp` | 178 | Debug AST printer |
| **Total** | **~95K** | |
