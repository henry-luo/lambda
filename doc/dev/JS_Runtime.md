# LambdaJS Runtime — Design Document

## Overview

LambdaJS is Lambda's embedded JavaScript engine (~95K LOC across 35+ source files). It compiles JavaScript to native machine code through a four-stage pipeline reusing Lambda's type system, memory management, and JIT infrastructure. JavaScript programs execute within the same `Item`-based runtime as Lambda scripts, enabling direct interop with Lambda's input parsers, output formatters, and the Radiant HTML/CSS layout engine.

### Design Goals

1. **Unified runtime** — JS values are Lambda `Item` values. No conversion boundaries.
2. **Near-native performance** — Multi-phase type inference + native arithmetic fast paths.
3. **DOM integration** — JavaScript can manipulate Radiant `DomElement` trees using standard DOM APIs.
4. **Reuse over reimplementation** — JSON, URL, regex, formatting all delegate to existing Lambda subsystems.

---

## 1. Architecture

### 1.1 Compilation Pipeline

```
JS Source (.js)
    │
    ▼
Tree-sitter Parser     (tree-sitter-javascript grammar)
    │
    ▼
JS AST Builder         (build_js_ast.cpp → typed JsAstNode tree)
    │
    ▼
MIR Transpiler         (transpile_js_mir.cpp → MIR IR instructions)
    │
    ▼
MIR JIT Compiler       (MIR_link + MIR_gen → native machine code)
    │
    ▼
Execution              (js_main() function pointer call → Item result)
```

### 1.2 Unified Runtime Architecture

LambdaJS shares the same runtime layer as Lambda scripts:

```
┌──────────────────────────────────────────────────────────────────┐
│                      CLI (main.cpp)                              │
│         ./lambda.exe js script.js [--document page.html]         │
├──────────────┬───────────────────────────────────────────────────┤
│  Lambda Path │               JavaScript Path                     │
│  (.ls files) │               (.js files)                         │
│              │                                                   │
│  build_ast   │   build_js_ast.cpp (Tree-sitter JS → JsAstNode)  │
│  transpile   │   transpile_js_mir.cpp (JsAstNode → MIR IR)      │
│              │                                                   │
├──────────────┴───────────────────────────────────────────────────┤
│                  Shared Runtime Infrastructure                    │
│                                                                   │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Item Type  │ │ GC Heap  │ │  MIR JIT    │ │ import_resolver│  │
│  │ System     │ │ & Nursery│ │ (mir.c)     │ │ (sys_func_    │  │
│  │ (TypeId)   │ │          │ │             │ │  registry.c)  │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Name Pool  │ │ Memory   │ │ Shape System│ │ I/O Parsers & │  │
│  │ (interning)│ │ Pool     │ │ (ShapeEntry)│ │ Formatters    │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│                    Radiant (HTML/CSS Engine)                      │
│         DomDocument / DomElement / CSS Cascade / Layout          │
└──────────────────────────────────────────────────────────────────┘
```

**Key insight:** JS bypasses Lambda's runner (`runner.cpp`). The CLI detects `.js` files and calls `transpile_js_to_mir()` directly, which handles parsing, compilation, and execution in one call.

### 1.3 Execution Entry Point

```
main.cpp                           transpile_js_mir.cpp
────────                           ────────────────────
argv[1] == "js"
  │
  ├─ runtime_init(&runtime)        // shared Runtime struct
  ├─ lambda_stack_init()           // GC stack bounds
  ├─ read_text_file(js_file)       // read JS source
  │
  ├─ [if --document page.html]
  │    ├─ input_from_source()      // HTML → Lambda Element tree
  │    ├─ dom_document_create()    // Element → DomDocument
  │    └─ build_dom_tree_from_element()  // → DomElement hierarchy
  │
  └─ transpile_js_to_mir()  ──────→  js_transpiler_create()
                                      js_transpiler_parse()        // TS parse
                                      build_js_ast()               // TS CST → JS AST
                                      jit_init(2)                  // MIR ctx
                                      transpile_js_mir_ast()       // AST → MIR IR
                                      MIR_link(import_resolver)    // link imports
                                      find_func(ctx, "js_main")   // locate entry
                                      js_main(&eval_context)       // execute
                                      ← return Item result
```

---

## 2. JS Data Types ↔ Lambda Runtime Types

All JavaScript values are represented as Lambda `Item` (64-bit tagged value). There is no conversion overhead between JS and Lambda — they share the same representation.

### 2.1 Type Mapping Table

| JS Type | Lambda TypeId | Representation | Boxing |
|---------|--------------|----------------|--------|
| `number` (integer) | `LMD_TYPE_INT` | Tag bits + 56-bit signed value inline | Inline (no allocation) |
| `number` (float) | `LMD_TYPE_FLOAT` | GC nursery-allocated `double*` | Pointer to nursery |
| `string` | `LMD_TYPE_STRING` | `String*` via `heap_create_name()` | GC heap allocated |
| `boolean` | `LMD_TYPE_BOOL` | Tag bits + 0/1 inline | Inline (no allocation) |
| `null` | `LMD_TYPE_NULL` | Tag-only sentinel | Inline |
| `undefined` | `LMD_TYPE_UNDEFINED` | Tag-only sentinel (`ITEM_JS_UNDEFINED`) | Inline |
| `object` | `LMD_TYPE_MAP` | Lambda `Map` with `ShapeEntry` field chain | GC heap allocated |
| `array` | `LMD_TYPE_ARRAY` | Lambda `Array` (dynamic `Item*` buffer) | GC heap allocated |
| `function` | `LMD_TYPE_FUNC` | Pool-allocated `JsFunction` struct | Pool allocated |
| `RegExp` | `LMD_TYPE_MAP` | Map wrapping `JsRegexData` (RE2 backend) | Pool + heap |
| `Symbol` | `LMD_TYPE_SYMBOL` | Lambda symbol with global registry | GC heap allocated |
| DOM node | `LMD_TYPE_MAP` | Map wrapping `DomElement*` with sentinel marker | GC heap allocated |
| TypedArray | `LMD_TYPE_MAP` | Map wrapping `JsTypedArray` (raw C buffer) | Heap + malloc |

### 2.2 Special Type Handling

**`undefined` vs `null`:** Lambda does not natively distinguish these. LambdaJS adds `LMD_TYPE_UNDEFINED` with its own tag value (`ITEM_JS_UNDEFINED`) to preserve JS semantics (e.g., `typeof null === "object"` vs `typeof undefined === "undefined"`).

**Objects as Maps:** JS objects use Lambda's `Map` struct with linked `ShapeEntry` chains. Property names are interned via the name pool. The shape system enables O(1) field access when the object layout is known at compile time.

**Sentinel-based Subtyping:** DOM nodes, TypedArrays, and RegExp objects are all Lambda Maps with a unique `TypeMap*` sentinel in the `Map::type` field. This enables O(1) type discrimination:
```c
bool js_is_dom_node(Item item)    → map->type == &js_dom_type_marker
bool js_is_typed_array(Item item) → map->type == &js_typed_array_type_marker
```

### 2.3 Reuse of Lambda System Functions

LambdaJS reuses Lambda's existing subsystems rather than reimplementing them:

| Capability | Lambda Subsystem | JS API Surface |
|-----------|-----------------|----------------|
| JSON parsing | `input-json.cpp` → `parse_json_to_item()` | `JSON.parse(str)` |
| JSON output | `format/format-json.cpp` → `format_json()` | `JSON.stringify(value)` |
| URL encoding | `lib/url.c` → `url_encode_component()` | `encodeURIComponent()` |
| URL decoding | `lib/url.c` → `url_decode_component()` | `decodeURIComponent()` |
| Regex (RE2) | `re2_wrapper.hpp` | `/pattern/flags`, `.test()`, `.exec()`, `.match()` |
| String interning | `name_pool.hpp` → `name_pool_get()` | All property name lookups |
| Shape system | `shape_builder.hpp` | Object property layout |
| GC heap | `lambda-mem.cpp` → `heap_alloc()`, `gc_nursery` | All object/string allocation |
| Memory pools | `lib/mempool.h` → `pool_create()` | Temporary allocations, closures |
| Format output | `format/format.h` | Final result formatting |

**`JSON.parse` example flow:**
```
js_json_parse(str_item)
  → js_to_string(str_item)           // coerce to string
  → parse_json_to_item(js_input, buf) // Lambda's JSON parser
  → return Item                       // already in Lambda/JS shared format
```

**`JSON.stringify` example flow:**
```
js_json_stringify(value)
  → pool = pool_create()
  → format_json(pool, value)          // Lambda's JSON formatter
  → heap_strcpy(json->chars, len)     // copy to GC heap
  → pool_destroy(pool)
  → return string Item
```

---

## 3. JS + Radiant DOM Integration

### 3.1 DOM Bridge Architecture

The DOM bridge (`js_dom.cpp` / `js_dom.h`) connects JavaScript to Radiant's `DomElement`/`DomDocument` tree. DOM nodes are wrapped as Lambda Maps with a sentinel marker pointer, enabling zero-overhead type checking.

```
JavaScript Code                          Radiant Engine
──────────────                           ──────────────
document.getElementById("app")    →     js_document_method()
                                          │
                                          ├─ DomDocument::find_by_id()
                                          └─ js_dom_wrap_element(DomElement*)
                                               │
                                               └─ Map { type = &js_dom_type_marker,
                                                        data = DomElement* }

element.style.color = "red"        →     js_dom_style_set()
                                          │
                                          ├─ camelCase → kebab-case conversion
                                          └─ dom_set_inline_style(elem, "color", "red")

getComputedStyle(element)          →     js_get_computed_style()
                                          │
                                          └─ Map { type = &js_computed_style_marker,
                                                   data = DomElement* }
```

### 3.2 Wrapping Strategy

DOM elements are wrapped in Lambda `Map` structs using a **sentinel-based** pattern:

```c
// Wrapping: DomElement* → Item
Item js_dom_wrap_element(void* dom_elem) {
    Map* wrapper = heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type = &js_dom_type_marker;   // sentinel for type check
    wrapper->data = dom_elem;               // DomElement* stored directly
    return (Item){.map = wrapper};
}

// Unwrapping: Item → DomElement*
void* js_dom_unwrap_element(Item item) {
    Map* m = item.map;
    if (m->type == &js_dom_type_marker) return m->data;
    return NULL;
}
```

This approach avoids HashMap allocation per DOM node — O(1) wrap/unwrap via pointer comparison.

### 3.3 Document Setup with `--document`

When `--document page.html` is passed on the CLI:

1. HTML file is parsed by Lambda's HTML5 input parser → Lambda `Element` tree
2. `dom_document_create()` creates a Radiant `DomDocument`
3. `build_dom_tree_from_element()` recursively converts `Element` → `DomElement` hierarchy
4. CSS property system is initialized for inline style parsing
5. The `DomDocument*` is stored and made available to JS runtime functions via `js_dom_set_document()`

### 3.4 Supported DOM APIs

**Document methods:** `getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `createTextNode`, `createDocumentFragment`, `write`

**Element properties:** `tagName`, `id`, `className`, `textContent`, `innerHTML` (get/set), `children`, `parentNode`, `parentElement`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `nextElementSibling`, `previousElementSibling`, `childNodes`, `childElementCount`, `nodeType`, `offsetWidth`, `offsetHeight`, `clientWidth`, `clientHeight`, `style`, `classList`, `dataset`

**Element methods:** `getAttribute`, `setAttribute`, `removeAttribute`, `hasAttribute`, `appendChild`, `removeChild`, `insertBefore`, `replaceChild`, `cloneNode`, `contains`, `closest`, `matches`, `getBoundingClientRect`

**Style access:** `element.style.propertyName = value` (camelCase → CSS kebab-case conversion, delegates to `dom_set_inline_style()`)

**Computed style:** `getComputedStyle(element)` wraps computed CSS values, returns property values resolved through the CSS cascade

**classList:** `add`, `remove`, `toggle`, `contains`, `replace`

### 3.5 DOM Integration with CSS Selectors

Query methods (`querySelector`, `querySelectorAll`, `getElementsByClassName`, etc.) use Radiant's CSS selector matcher (`selector_matcher.hpp`). The full CSS selector syntax is supported — descendant, child, sibling combinators, pseudo-classes, attribute selectors, etc.

---

## 4. JS-Specific Transpiler & Runtime Design

### 4.1 Transpiler Architecture (`transpile_js_mir.cpp`)

The JS transpiler emits MIR IR directly (no intermediate C code), mirroring the Lambda MIR transpiler's approach but with JS-specific semantics. The core struct is `JsMirTranspiler`:

```c
struct JsMirTranspiler {
    MIR_context_t ctx;              // MIR JIT context
    MIR_module_t module;            // current MIR module
    MIR_item_t current_func;        // MIR function being emitted
    JsTranspiler* tp;               // JS parser/AST context
    JsMirVarEntry vars[1024];       // variable register table
    int var_count;
    JsFuncCollected func_entries[256]; // collected function definitions
    int func_count;
    JsClassEntry class_entries[64]; // collected class definitions
    JsLoopLabels loop_stack[32];    // break/continue label stack
    HashMap* import_cache;          // MIR import deduplication
    // ... counters, flags, type inference state
};
```

### 4.2 Multi-Phase Compilation

The transpiler uses a multi-phase approach to handle JavaScript's hoisting, closures, and optimization opportunities:

| Phase | Name | Description |
|-------|------|-------------|
| 1.0 | Function Collection | Post-order walk collects all function and class declarations |
| 1.1 | Constant Collection | `const` with literal values become compile-time constants; constant folding for expressions like `4 * PI * PI` |
| 1.3 | Module Variables | Mutable top-level `let`/`var` get module variable indices (indexed array, not closures) |
| 1.3b | Implicit Globals | Assigned-but-undeclared variables (sloppy mode) become module vars |
| 1.5 | Capture Analysis | Free variables per function: `refs - params - locals - module_consts = captures` |
| 1.6 | Transitive Propagation | Multi-level closure captures propagated (fixed-point iteration) |
| 1.7 | Scope Env Computation | Parent functions get shared scope env arrays for mutable capture semantics |
| 1.75 | Type Inference | Evidence-based parameter/return type inference (arithmetic patterns → int/float) |
| 1.9 | Forward Declarations | MIR forward refs for all functions (enables mutual recursion) |
| 2 | Code Generation | `jm_define_function()` emits MIR for each function body |
| 3 | Entry Point | `js_main()` creation — top-level statements, function hoisting |

### 4.3 Code Generation Design

All JS values are boxed `Item` values (`MIR_T_I64`). Expressions are emitted as sequences of MIR instructions that produce `Item` results:

```
JS:    let x = a + b * 2

MIR:   reg_lit = call js_box_int(2)
        reg_mul = call js_multiply(reg_b, reg_lit)
        reg_add = call js_add(reg_a, reg_mul)
        // reg_add assigned to x's variable register
```

**Native fast path:** When types are statically known (via Phase 1.75 inference), arithmetic uses MIR instructions directly:

```
JS:    function square(n) { return n * n; }  // n inferred as INT

MIR:   // native version (_n suffix):
        reg_result = MIR_MUL(reg_n, reg_n)   // no boxing, no runtime call
```

### 4.4 Closure Implementation

Closures use a **shared scope environment** model: all child closures of the same parent share a single `uint64_t[]` array allocated via `js_alloc_env()`, enabling mutable capture semantics. Child functions receive the env pointer as a hidden first parameter. Phases 1.5–1.7 handle capture analysis, transitive propagation, and scope env layout.

> See [JS_Runtime_Detailed.md §2](JS_Runtime_Detailed.md#2-closure-implementation) for env allocation, writeback rules, multi-level nesting, and generator interaction.

### 4.5 Class System

Classes compile to prototype-based constructor functions. The transpiler collects class declarations into `JsClassEntry` structs, compiles constructors with special prologue (object allocation + `__proto__` setup) and epilogue (implicit return), and attaches methods to the prototype. Inheritance uses `Object.create(Parent.prototype)` + `super()` dispatch.

> See [JS_Runtime_Detailed.md §3](JS_Runtime_Detailed.md#3-class-system) for shape pre-allocation (A5), method compilation, private members, and compile-time resolution (P7).

### 4.6 Exception Handling

Exceptions use global thread state rather than C++ exceptions:

```c
static bool js_exception_pending;
static Item js_exception_value;
```

- `throw expr` → `js_throw_value(val)` sets the pending exception
- After each function call inside a `try` block, `js_check_exception()` tests the flag; if set, jumps to the catch label
- `catch(e)` → `js_clear_exception()` retrieves and clears the exception value
- `finally` blocks execute regardless, using `return_val_reg` / `has_return_reg` to delay returns until after `finally`
- `js_new_error(message)` / `js_new_error_with_name(name, message)` create Error objects (Maps with `name` and `message` properties)

### 4.7 Optimization Strategies

The transpiler implements several speculative optimizations:

| Code | Optimization | Description |
|------|-------------|-------------|
| — | Native arithmetic | When operand types are known INT or FLOAT, emit MIR arithmetic directly (`MIR_ADD`, `MIR_DADD`) — no boxing, no runtime calls |
| P4 | Dual compilation | Numeric-only functions get a boxed version + a native version (`_n` suffix). Call sites dispatch to the native version when arg types match |
| P7 | Method devirtualization | Known-class instances dispatch method calls to concrete function pointers at compile time |
| P9 | TypedArray tracking | Variables holding typed arrays track element type. Access emits direct C array reads with casts, bypassing boxing entirely |
| P10f | Fast map lookup | `js_map_get_fast()` uses hash table first, then linear scan fallback, with interned string pointer comparison |
| A5 | Constructor shape pre-alloc | `this.prop = expr` patterns in constructors pre-allocate all property slots at object creation, making writes O(1) |
| A6 | Pointer comparison | Interned string pointers enable pointer equality (`==`) instead of `strcmp` for property name matching |
| TCO | Tail-call optimization | Self-recursive functions in tail position are rewritten as loops |
| — | Float widening pre-scan | INT variables assigned FLOAT values are promoted to FLOAT registers from the start, avoiding unnecessary conversions |
| — | Constant folding | Phase 1.1 folds constant expressions (e.g., `4 * Math.PI * Math.PI` → literal) |

### 4.8 MIR Import Resolution

Both Lambda and JS JIT code share the same `import_resolver()` function (defined in `mir.c`). It resolves symbol names to native function pointers via a static hashmap built at `jit_init()` time:

- **Lambda system functions** (`sys_func_defs[]`) — `len`, `type`, string ops, math, etc.
- **JS runtime functions** (`jit_runtime_imports[]`) — 100+ functions registered in `sys_func_registry.c`

Categories of JS runtime imports:

| Category | Examples |
|----------|---------|
| Type conversion | `js_to_number`, `js_to_string`, `js_to_boolean`, `js_is_truthy` |
| Operators | `js_add`, `js_subtract`, `js_equal`, `js_strict_equal`, `js_typeof` |
| Objects | `js_new_object`, `js_property_get`, `js_property_set` |
| Arrays | `js_array_new`, `js_array_push`, `js_array_get`, `js_array_set` |
| Closures | `js_new_function`, `js_new_closure`, `js_alloc_env`, `js_call_function` |
| Strings | `js_string_method` (30+ methods dispatched by name) |
| Collections | `js_map_collection_new`, `js_set_collection_new`, `js_collection_method` |
| DOM | `js_document_method`, `js_dom_element_method`, `js_dom_get_property` |
| TypedArrays | `js_typed_array_new`, `js_typed_array_get`, `js_typed_array_set` |
| Globals | `js_json_parse`, `js_json_stringify`, `js_console_log`, `js_parseInt` |
| Exceptions | `js_throw_value`, `js_check_exception`, `js_clear_exception`, `js_new_error` |
| Module vars | `js_get_module_var`, `js_set_module_var` |

### 4.9 Scope & Variable Management

JavaScript scoping semantics are implemented through `JsScope` (parser phase) and `JsMirVarEntry` (transpiler phase):

- **`var`:** Function-scoped — scope lookup skips block scopes to find the nearest function or global scope
- **`let` / `const`:** Block-scoped — defined in the current block scope
- **Hoisting:** Function declarations are hoisted to the top of `js_main()` (Phase 3); `var` declarations are hoisted with `undefined` initialization
- **Module variables:** Top-level mutable bindings use a fixed-size `js_module_vars[256]` indexed array, accessible from any function via `js_get/set_module_var()` without closure capture overhead

### 4.10 File Layout

| File | Lines | Purpose |
|------|------:|---------|
| `transpile_js_mir.cpp` | ~28K | Core MIR transpiler: AST → MIR IR, type inference, native optimization, closures, classes, generators, async |
| `js_runtime.cpp` | ~21K | Runtime library: operators, type coercion, property access, prototype chain, iterators, generators, collections |
| `js_globals.cpp` | ~11K | Built-in objects: JSON, Date, Symbol, Object.*, Math, Reflect, constructors, URI encoding |
| `build_js_ast.cpp` | ~4K | AST builder: Tree-sitter CST → typed JS AST nodes |
| `js_dom.cpp` | ~3.8K | DOM bridge: wraps Radiant DomElement as JS Maps via sentinel markers |
| `js_buffer.cpp` | ~2.5K | Node.js Buffer implementation |
| `js_fs.cpp` | ~1.8K | Node.js fs module (readFileSync, writeFileSync, stat, etc.) |
| `js_crypto.cpp` | ~1.8K | Node.js crypto module (hash, randomBytes, randomUUID) |
| `js_typed_array.cpp` | ~1.1K | TypedArray support: Int8 through Float64 arrays, ArrayBuffer, DataView |
| `js_early_errors.cpp` | ~1.1K | Early error detection, strict mode validation |
| `js_event_loop.cpp` | ~800 | Event loop, microtask queue, timers |
| `js_regex_wrapper.cpp` | ~780 | JS regex → RE2 transpilation with post-filters |
| `js_runtime.h` | ~740 | Runtime C API: function declarations callable from JIT code |
| `js_ast.hpp` | ~620 | AST node types: ~45 node types, ~50 operators, struct definitions |
| `js_scope.cpp` | ~440 | Scope management: var/let/const semantics, Tree-sitter parser init |
| `js_transpiler.hpp` | ~256 | Transpiler context struct, scope types, forward declarations |
| `js_print.cpp` | ~178 | Debug AST printer |
| + 18 more | ~17K | Node.js compat (http, net, path, os, stream, etc.), DOM events, CSSOM, Canvas, Fetch, XHR |

> See [JS_Runtime_Detailed.md](JS_Runtime_Detailed.md) for the complete file listing and detailed subsystem documentation.

---

## Appendix: Comparison with Lambda Transpiler

| Aspect | Lambda Transpiler | JS Transpiler |
|--------|------------------|---------------|
| Source grammar | Tree-sitter Lambda (`grammar.js`) | Tree-sitter JavaScript |
| Intermediate | AST → C code → C2MIR | AST → MIR IR directly |
| Entry function | `lambda_main()` | `js_main()` |
| Paradigm | Pure functional (expressions) | Imperative (statements + expressions) |
| Variables | Immutable bindings | Mutable `var`/`let`/`const` |
| Objects | Maps with static shapes | Maps with dynamic property addition |
| Runner | `runner.cpp` orchestrates | Bypasses runner; `main.cpp` → `transpile_js_to_mir()` |
| Type inference | Type annotations + inference | Evidence-based inference (no annotations) |
| Closure model | Closure captures (copy semantics) | Shared scope env (mutable reference semantics) |
| Error handling | `T^E` return types, `?` propagation | `try`/`catch`/`finally`, global exception state |
