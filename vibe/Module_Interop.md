# Module Interop: Cross-Language Module System

**Status**: Phases 1–4 Implemented, Phase 6 (Python) Implemented  
**Date**: 2026-03-25  
**Last Updated**: 2026-07-18

## 1. Existing Module Systems

### 1.1 Lambda Module System

Lambda uses a **static, compile-time** module system built on struct-based function pointer slots.

#### Import Syntax

```lambda
import .relative_module          // relative to current file
import .path.to.module           // nested relative path
import module_name               // relative to project root
import alias: .module            // aliased — functions accessed as alias.funcname
```

Modules export public symbols with the `pub` keyword:

```lambda
pub fn add(a, b) => a + b
pub let PI = 3.14159
pub type Point = { x: float, y: float }
```

#### Compilation Pipeline

```
Source: import .helpers
  │
  ├─ Parse: AstImportNode { module: ".helpers", alias: NULL, script: → compiled AstScript }
  │
  ├─ Name Registration: declare_module_import() adds each pub symbol to scope
  │   └─ NameEntry { name: "add", node: AstFuncNode*, import: AstImportNode* }
  │
  ├─ Code Generation: write_fn_name_ex() mangles cross-module references
  │   └─ Local:    _add_1234
  │   └─ Imported: m3._add_1234          (m<script_index>.<name>_<byte_offset>)
  │   └─ Boxed:    m3._add_b_1234        (wrapper for fn_call dispatch)
  │
  ├─ Module Struct: define_module_import() emits a global struct per import
  │   struct Mod3 {
  │       void** consts;                  // constant array pointer
  │       Item (*_mod_main)(Context*);    // module entry point
  │       void (*_init_vars)(void*);      // variable initializer
  │       Item (*_add_1234)(Context*, Item, Item);  // pub function slot
  │       Item (*_add_b_1234)(Context*, Item);      // boxed wrapper slot
  │       Item _PI;                       // pub variable slot
  │   } m3;
  │
  └─ Linking: init_module_import() in runner.cpp wires pointers
      ├─ find_func(module_ctx, "_add_1234") → writes native pointer into struct slot
      ├─ find_func(module_ctx, "_init_mod_consts") → initializes constants
      └─ find_func(module_ctx, "_init_mod_types") → initializes types
```

#### Runtime Call Path

When Lambda calls an imported function `add(3, 4)`:

```
MIR import reference: m3._add_1234
  → import_resolver() resolves to struct slot address
  → direct native function pointer call
  → zero runtime indirection
```

#### Key Properties

| Property | Detail |
|----------|--------|
| Resolution | Compile-time via name mangling |
| Call overhead | Zero — direct native pointer |
| Module storage | BSS struct per import (m0, m1, ...) |
| Symbol visibility | `pub` keyword gates export; import sees full pub API |
| MIR context | One per module; function pointers copied across contexts |
| Naming | User functions: `_name_offset`; system functions: `fn_name` |

### 1.2 JavaScript Module System

JS uses a **dynamic, runtime** module system built on namespace objects and property lookup.

#### Import Syntax

```javascript
import { add, PI } from './utils.js';           // named imports
import greet from './utils.js';                  // default import
import { square as sq } from './math.js';       // aliased import
import * as utils from './utils.js';             // namespace import
```

Modules export with standard ES module syntax:

```javascript
export function add(a, b) { return a + b; }
export const PI = 3.14159;
export default function greet(name) { return "Hello, " + name; }
```

#### Compilation Pipeline

```
Source: import { add } from './utils.js'
  │
  ├─ Parse: JsImportNode { source: "./utils.js", specifiers: [{ remote: "add", local: "add" }] }
  │
  ├─ Module Loading: jm_load_imports()
  │   ├─ resolve path relative to importing file
  │   ├─ check js_modules[] registry (skip if already loaded)
  │   ├─ register placeholder namespace (circular import guard)
  │   └─ transpile_js_module_to_mir() → recursively compile + execute
  │
  ├─ Module Execution: js_main() runs module code
  │   ├─ namespace_reg = js_new_object()
  │   ├─ for each export: js_property_set(namespace_reg, "add", fn_item)
  │   └─ return namespace_reg
  │
  ├─ Registration: js_module_register(path, namespace_obj)
  │   └─ stored in static js_modules[64] array
  │
  └─ Import Binding: MIR code generated at import site
      ├─ ns  = js_module_get("./utils.js")         // linear search in registry
      ├─ key = js_box_string_literal("add")
      ├─ _js_add = js_property_get(ns, key)         // hash lookup in namespace
      └─ store _js_add in local variable scope
```

#### Runtime Call Path

When JS calls an imported function `add(3, 4)`:

```
load _js_add from local variable
  → js_call_function(_js_add, this, [arg1, arg2], 2)
  → type check (LMD_TYPE_FUNC)
  → handle bound functions (if applicable)
  → js_invoke_fn(fn, args, argc)
  → call native function pointer
  → 2–3 levels of indirection
```

#### Key Properties

| Property | Detail |
|----------|--------|
| Resolution | Runtime via `js_module_get()` + `js_property_get()` |
| Call overhead | 2–3 indirections (lookup + unbox + dispatch) |
| Module storage | `js_modules[64]` static array; linear search by path |
| Symbol visibility | `export` keyword; default + named exports |
| MIR context | One per module; namespace object bridges contexts |
| Naming | All JS vars prefixed `_js_`; module entry: `js_main` |

### 1.3 Comparison

| Aspect | Lambda | JavaScript |
|--------|--------|------------|
| Export mechanism | `pub` declarations → struct slots | `export` statements → namespace object properties |
| Import binding | Compile-time mangled symbol | Runtime property extraction |
| Cross-module call | Direct native pointer | Indirect via `js_call_function()` |
| Module registry | None (AST-level wiring) | Global `js_modules[64]` array |
| Circular imports | Not supported | Placeholder-based guard |
| Variable access | Direct struct field read | `js_property_get()` lookup |
| Parallel compilation | `precompile_imports()` in runner.cpp | `jm_precompile_js_imports()` in transpile_js_mir.cpp |

---

## 2. Shared Architecture

Despite different surface designs, Lambda and JS share critical infrastructure that enables cross-language interop.

### 2.1 Unified Data Representation

Both languages compile to the same `Item` type — a 64-bit tagged value defined in `lambda-data.hpp`:

```
┌──────────────────────────────────────────────────────────────────┐
│  Item (64-bit)                                                   │
│  ┌─────────┬────────────────────────────────────────────────────┐│
│  │ TypeId  │  Payload (pointer or packed scalar)               ││
│  │ (8 bits)│  (56 bits)                                        ││
│  └─────────┴────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────┘
```

- **Simple scalars** (null, bool, int): packed directly with TypeId
- **Compound scalars** (float, string, symbol): tagged pointers to GC-managed heap
- **Containers** (array, map, element): direct pointers to `struct Container`
- **Functions**: pointer to `Function` struct with `fn_ptr`, `closure_env`, `arity`

Both Lambda-compiled and JS-compiled code produce and consume `Item` values. No conversion is needed at the data level.

### 2.2 MIR Compilation

Both languages compile to MIR (Medium Intermediate Representation) and JIT to native code:

```
Lambda AST ──→ transpile-mir.cpp ──→ MIR module ──→ MIR_link() ──→ native code
JS AST     ──→ transpile_js_mir.cpp ──→ MIR module ──→ MIR_link() ──→ native code
```

Both use `import_resolver()` (defined in `mir.c`) for symbol resolution at link time:

```
import_resolver(name)
  ├─ 1. Check dynamic_import_map (thread-local, cross-module symbols) → O(1) hashmap
  ├─ 2. Check func_map (static runtime functions: fn_len, fn_type, ...) → O(1) hashmap
  └─ 3. Fail: log_error("failed to resolve native fn/pn: %s")
```

### 2.3 System Function Registry

All runtime functions are registered in `sys_func_registry.c` and loaded into `func_map` at `jit_init()`. Both Lambda and JS MIR code resolve calls to system functions (e.g., `heap_create_name`, `fn_len`, `fn_call_1`) through the same `import_resolver`.

### 2.4 What Already Works

| Capability | Status |
|------------|--------|
| Same `Item` data type across languages | ✅ Works |
| Same MIR backend and JIT | ✅ Works |
| Same `import_resolver` for symbol lookup | ✅ Works |
| Same system function registry | ✅ Works |
| Same GC/heap for all allocations | ✅ Works |
| Function pointers from either language can be stored as `Item` | ✅ Works |
| Cross-context function pointer calls (pure functions) | ✅ Works |

### 2.5 What's Missing

| Capability | Status |
|------------|--------|
| Unified module registry across languages | ❌ Separate systems |
| Lambda importing a JS module | ❌ Not implemented |
| JS importing a Lambda module | ❌ Not implemented |
| Unified function naming convention | ❌ Lambda: `_name_offset`, JS: `_js_name` |
| Cross-language function call dispatch | ❌ Different calling conventions for closures/wrappers |
| Shared module discovery and resolution | ❌ Lambda uses AST-level; JS uses runtime registry |

---

## 3. Unified Module System Proposal

### 3.1 Design Goals

1. **Language-agnostic**: any language that compiles to MIR on the Lambda runtime can export and import modules
2. **Unified naming**: consistent symbol conventions across all languages
3. **Minimal overhead**: cross-language calls should be as close to same-language calls as possible
4. **Incremental**: can be adopted without breaking existing same-language imports
5. **Future-proof**: design supports adding Python, TypeScript, or other language frontends

### 3.2 Unified Module Registry

Replace the separate Lambda (AST-level wiring) and JS (`js_modules[64]`) registries with a single runtime module registry.

#### Module Descriptor

```c
// in a new file: lambda/module_registry.h

typedef struct {
    const char* path;           // resolved absolute path (unique key)
    const char* source_lang;    // "lambda", "js", "python", ...
    Item namespace_obj;         // namespace Item (map of exported symbols)
    MIR_context_t mir_ctx;      // compiled MIR context (for function lookup)
    bool initialized;           // true after module code has executed
} ModuleDescriptor;
```

#### Registry API

```c
// Register a compiled + executed module
void module_register(const char* path, const char* lang, Item namespace_obj, MIR_context_t ctx);

// Look up a module by resolved path (O(1) hashmap)
ModuleDescriptor* module_get(const char* path);

// Check if a module is already loaded
bool module_is_loaded(const char* path);
```

The registry uses a Robin Hood hashmap (like `func_map`) keyed by resolved absolute path. This replaces:
- Lambda's AST-level `AstImportNode::script` linkage for cross-language imports
- JS's `js_modules[64]` static array

Same-language imports can continue using their optimized paths (Lambda struct wiring, JS namespace objects) as an optimization, falling through to the unified registry only for cross-language imports.

### 3.3 Unified Naming Convention

All symbols in the unified module system follow Lambda's naming philosophy:

| Category | Convention | Examples |
|----------|-----------|----------|
| System functions | Start with alpha letter | `len`, `type`, `str`, `map`, `filter` |
| User functions | Start with `_` | `_add`, `_greet`, `_process_data` |
| Module-qualified | `m<idx>._name` | `m3._add`, `m5._transform` |
| Boxed wrappers | `_name_b` suffix | `_add_b`, `_greet_b` |

#### Language-Specific Mapping

When a JS module is imported into the unified system, its exported names are mapped:

```
JS export name          →  Unified name
────────────────────────────────────────
add                     →  _add
greet                   →  _greet
processData             →  _processData
PI                      →  _PI
default                 →  _default
```

Rule: prepend `_` to all user-defined export names. System functions (registered in `sys_func_registry`) retain their alpha-prefixed names.

When Lambda exports are accessed from JS:

```
Lambda pub name         →  JS-visible name
────────────────────────────────────────
pub fn add              →  add           (strip the _)
pub let PI              →  PI
```

Rule: strip the leading `_` for JS consumption, matching JS naming expectations.

This mapping is applied at the **namespace object level** — the registry stores symbols under unified names, and language-specific import transpilers apply the appropriate mapping when binding to local scope.

### 3.4 Namespace Object as Universal Interface

The key insight is that both languages already converge on the concept of a **namespace object** — a Lambda `Item` (map) containing exported symbols:

```
Module "utils.js" or "utils.ls"
  └─ namespace_obj (Item, type=MAP)
       ├─ "_add"  → Item (type=FUNC, fn_ptr=0x..., arity=2)
       ├─ "_mul"  → Item (type=FUNC, fn_ptr=0x..., arity=2)
       └─ "_PI"   → Item (type=FLOAT, value=3.14159)
```

JS already produces namespace objects. Lambda needs to produce them too — this is the primary new work for Lambda modules.

#### Lambda Module: Generating Namespace Object

After a Lambda module is compiled and its struct is wired, generate a namespace `Item` from the public API:

```c
// In runner.cpp, after init_module_import():
Item build_module_namespace(AstScript* script) {
    Item ns = create_map();     // empty Lambda map

    // walk AST for pub functions
    for each pub fn in script->ast {
        // create Function Item wrapping the compiled fn_ptr
        Function* f = gc_alloc(sizeof(Function));
        f->ptr = find_func(script->jit_context, fn_name);
        f->arity = fn_node->param_count;
        f->closure_env = NULL;

        String* key = heap_create_name(unified_name);   // "_add"
        map_set(ns, key, wrap_function(f));
    }

    // walk AST for pub variables
    for each pub var in script->ast {
        String* key = heap_create_name(unified_name);   // "_PI"
        Item val = /* read from module struct slot */;
        map_set(ns, key, val);
    }

    return ns;
}
```

This namespace object is then registered in the unified module registry.

#### JS Module: Adapting Namespace Object

JS already produces namespace objects. The only change is to re-key exports with unified `_`-prefixed names before registration:

```c
// After transpile_js_module_to_mir() executes js_main():
Item adapt_js_namespace(Item js_ns) {
    Item unified_ns = create_map();

    // for each property in js_ns:
    //   "add" → map_set(unified_ns, "_add", value)
    //   "PI"  → map_set(unified_ns, "_PI", value)
    //   "default" → map_set(unified_ns, "_default", value)

    return unified_ns;
}
```

### 3.5 Cross-Language Import: Lambda → JS

#### Lambda Source

```lambda
import js: "./utils.js"           // new syntax: language prefix

js.add(3, 4)                      // calls JS-compiled add function
js.PI                              // accesses JS-exported constant
```

#### Transpilation Flow

```
import js: "./utils.js"
  │
  ├─ 1. Detect language prefix "js:" → cross-language import
  │
  ├─ 2. Resolve module path → check unified registry
  │     └─ If not loaded: invoke JS transpiler to compile + execute module
  │
  ├─ 3. Get namespace from registry: module_get("./utils.js")->namespace_obj
  │
  ├─ 4. For each referenced symbol (js.add, js.PI):
  │     ├─ Look up "_add" in namespace → Item (type=FUNC)
  │     ├─ Extract fn_ptr and arity
  │     └─ Wire into Lambda module struct slot (same as Lambda-to-Lambda)
  │
  └─ 5. Generate call: m3._add_1234 → direct native pointer
```

The critical optimization: once the function pointer is extracted from the namespace object, it is wired into the module struct **the same way** as a Lambda-to-Lambda import. Cross-language calls become zero-overhead after initialization.

This works because:
- JS-compiled functions already return `Item` values
- JS-compiled functions are native code (MIR → JIT)
- The function pointer ABI is compatible (both go through `fn_call` dispatch or direct call)

#### Boxed Wrapper Requirement

Lambda functions use either **direct call** (typed parameters) or **boxed call** (all-`Item` parameters via `fn_call_N`). JS functions always use boxed `Item` parameters.

For cross-language calls, the Lambda transpiler should always generate boxed calls to imported JS functions, since JS functions accept `Item` arguments:

```c
// Lambda calling JS add(3, 4):
Item arg1 = box_int(3);
Item arg2 = box_int(4);
Item result = fn_call_2(ctx, js_add_ptr, arg1, arg2);
```

### 3.6 Cross-Language Import: JS → Lambda

#### JS Source

```javascript
import { add, PI } from './utils.ls';    // .ls extension triggers Lambda compilation
```

#### Transpilation Flow

```
import { add, PI } from './utils.ls'
  │
  ├─ 1. Detect .ls extension → cross-language import
  │
  ├─ 2. Resolve module path → check unified registry
  │     └─ If not loaded: invoke Lambda transpiler to compile + execute module
  │
  ├─ 3. Get namespace from registry: module_get("./utils.ls")->namespace_obj
  │
  ├─ 4. Standard JS import binding (same as JS-to-JS):
  │     ├─ ns  = js_module_get("./utils.ls")       // or module_get()
  │     ├─ key = "add"                               // strip _ prefix for JS
  │     ├─ _js_add = js_property_get(ns, key)
  │     └─ store in local scope
  │
  └─ 5. Call: add(3, 4) → js_call_function(_js_add, ...)
```

For JS→Lambda, the existing JS import mechanism works as-is — the namespace object contains `Function` items with valid native pointers. The `js_call_function` dispatcher handles calling them correctly.

### 3.7 Function Call Compatibility

Both Lambda and JS function items use the `Function` struct:

```c
struct Function {
    fn_ptr ptr;           // native code pointer (from MIR JIT)
    void* closure_env;    // NULL for most module-level functions
    uint8_t arity;        // parameter count
};
```

**Lambda functions** may have two variants:
- **Direct**: `Item _add(Context*, Item a, Item b)` — typed parameters, used for same-language calls
- **Boxed**: `Item _add_b(Context*, Item args_array)` — single array argument, used for `fn_call` dispatch

**JS functions** always use the boxed pattern (all `Item` args).

For cross-language interop, functions in the namespace object should always expose the **boxed** variant. This ensures any language can call any function through the same `fn_call_N` or `js_call_function` dispatch:

```
                        ┌─────────────────────┐
                        │ Namespace Object     │
                        │                      │
  Lambda function ───→  │ "_add" → Function {  │  ←─── JS consumer uses
  (boxed wrapper)       │   ptr: _add_b        │       js_call_function()
                        │   arity: 2           │
                        │ }                    │
                        │                      │
  JS function ───────→  │ "_greet" → Function {│  ←─── Lambda consumer uses
  (already boxed)       │   ptr: js_greet      │       fn_call_1()
                        │   arity: 1           │
                        │ }                    │
                        └─────────────────────┘
```

### 3.8 Module Discovery and Language Detection

The unified system detects the source language from context:

| Signal | Language |
|--------|----------|
| `.ls` file extension | Lambda |
| `.js` file extension | JavaScript |
| `.py` file extension | Python |
| Explicit prefix `js:` in Lambda import | JavaScript |
| Explicit prefix `lambda:` in JS import | Lambda |

Path resolution follows existing rules:
1. Relative to importing file's directory (`.` prefix or `./` prefix)
2. Relative to project root (no prefix)
3. `realpath()` normalization prevents duplicate compilation

### 3.9 Module Loading Order

Cross-language imports must handle the bootstrapping problem: when Lambda imports JS (or vice versa), the target language's transpiler must be available.

```
Lambda main.ls
  ├─ import .helpers          → Lambda transpiler compiles helpers.ls
  ├─ import js: ./utils.js    → JS transpiler compiles utils.js
  │   └─ utils.js imports ./math.js → JS transpiler compiles math.js (same-language)
  └─ import js: ./app.js      → JS transpiler compiles app.js
      └─ app.js imports ./data.ls → Lambda transpiler compiles data.ls (cross-language)
```

The unified registry prevents circular cross-language imports using the same placeholder pattern JS already uses:

1. Before compilation: register placeholder namespace for the path
2. Compile and execute the module
3. Replace placeholder with real namespace

### 3.10 Execution Model

Modules are compiled and executed in dependency order:

```
Phase 1: Discovery
  Walk import graph across all languages
  Build unified dependency graph with language annotations
  Compute topological depth

Phase 2: Parallel Compilation (per depth level)
  For each module at current depth:
    - Dispatch to appropriate language transpiler
    - Compile to MIR + JIT (thread-safe, independent contexts)

Phase 3: Serial Execution (per depth level)
  For each compiled module at current depth:
    - Execute module initialization (produces namespace object)
    - Apply naming convention mapping
    - Register in unified module registry

Phase 4: Main Script
  All imports resolved → compile and execute main script
```

This extends the existing parallel compilation infrastructure (Lambda's `precompile_imports()` and JS's `jm_precompile_js_imports()`) to work across languages.

---

## 4. Implementation Plan

### Phase 1: Unified Module Registry ✅

1. Created `lambda/module_registry.h` and `lambda/module_registry.cpp`
2. Implemented `ModuleDescriptor`, hashmap-based registry, API functions
3. Register system functions in `func_map` remain unchanged
4. Modified JS module loading to also register in unified registry
5. Modified Lambda module loading to produce namespace objects and register

### Phase 2: Lambda → JS Import ✅

1. Lambda import resolution falls back to `.js` extension when `.ls` not found
2. In Lambda transpiler: detect `.js` file, invoke JS compilation via `load_js_module()`
3. Extract function pointers from namespace object into Lambda module struct
4. Cross-language function calls use `JsFunction` wrapper with boxed `Item` ABI
5. Unit tests: Lambda script importing JS module (passing)

### Phase 3: JS → Lambda Import ✅

1. In JS transpiler: detect `.ls` extension, invoke Lambda compilation
2. Generate namespace object from Lambda pub declarations
3. Register Lambda namespace in unified registry
4. JS import binding works via namespace property lookup
5. Unit tests: JS script importing Lambda module (passing)

### Phase 4: Naming Convention Unification ✅

1. Defined mapping rules — all user functions use `_` prefix in MIR
2. Applied `_` prefix to user functions in Lambda, JS, and Python transpilers
3. Cross-language calls resolve functions by `_` + function name
4. No collision with system function names (system functions are unprefixed)
5. Tests for name mapping edge cases (passing)

### Phase 5: Unified Parallel Compilation

1. Merge Lambda and JS import graph discovery into single pass
2. Language-annotated dependency graph with mixed-language topological sort
3. Dispatch to appropriate transpiler per node
4. Shared thread pool across languages

### Phase 6: Python Language Support ✅

Python is the first additional language added beyond Lambda and JS, validating the adapter pattern described in the original design.

#### What was implemented

1. **Parser**: Upgraded `lambda/tree-sitter-python/` from a stub (returning NULL) to real tree-sitter-python v0.23.5 (parser.c + scanner.c → 494KB static library)
2. **AST builder**: Existing `build_py_ast()` produces Python AST from parse tree
3. **MIR transpiler**: Existing `transpile_py_to_mir()` compiles Python AST → MIR. Updated naming convention from `pyf_` prefix to `_` prefix to match unified convention.
4. **Module adapter**: `load_py_module()` in `transpile_py_mir.cpp` (~120 lines):
   - Reads `.py` source file via `read_text_file()`
   - Parses with tree-sitter-python and builds Python AST
   - Compiles to MIR and JIT-executes `py_main` (module initialization)
   - Iterates `func_entries` from compilation, creates `JsFunction` wrappers via `js_new_function()`
   - Builds namespace object via `js_new_object()` + `js_property_set()`
   - Registers in unified module registry with `MODULE_LANG_PYTHON` tag
5. **Import resolution**: Added `.py` fallback in `build_ast.cpp` — when `.ls` and `.js` not found, tries `.py` extension (both relative and absolute imports)

#### Key implementation details

- **Function struct type**: Cross-language namespace must use `js_new_function()` (creates `JsFunction` with `func_ptr` at offset 8), NOT `to_fn_named()` (creates Lambda `Function` with different layout). This is because `js_function_get_ptr()` casts to `JsFunction`.
- **Runtime context**: `load_py_module()` initializes both `py_runtime_set_input()` AND `js_runtime_set_input()` — the latter is required because `js_property_set()` internally calls `map_put()` which needs `js_input` context.
- **Naming convention**: Python user functions use `_` prefix in MIR (was `pyf_`), Python variables use `_py_` prefix. Lambda functions match this with `_` prefix.
- **Module discovery**: `.py` extension detected automatically in import resolution chain: `.ls` → `.js` → `.py`

#### Test coverage

- `test/lambda/py_helper.py` — Python module exporting `add(a,b)`, `multiply(a,b)`, `square(x)`
- `test/lambda/import_py.ls` — Lambda script importing Python module: `import .py_helper`
- Expected output verified: `add(3,4)=7`, `multiply(5,6)=30`, `square(8)=64`
- All 684/684 tests pass (241/241 Lambda Runtime tests)

```
                ┌────────────────────────────────────────┐
                │       Unified Module Registry          │
                │                                        │
                │  path → ModuleDescriptor {             │
                │    lang, namespace_obj, mir_ctx         │
                │  }                                     │
                │                                        │
                └──────┬────────────┬────────────┬───────┘
                       │            │            │
          ┌────────────┘            │            └────────────┐
          │                         │                         │
┌─────────┴──────────┐   ┌─────────┴──────────┐   ┌─────────┴──────────┐
│ Lambda Frontend    │   │ JavaScript Frontend │   │ Python Frontend    │
│                    │   │                     │   │                    │
│ grammar.js → AST   │   │ tree-sitter-js → AST│   │ tree-sitter-py→AST │
│ → transpile-mir    │   │ → transpile_js_mir  │   │ → transpile_py_mir │
│ → namespace Item   │   │ → namespace Item    │   │ → namespace Item   │
│                    │   │                     │   │                    │
└────────────────────┘   └─────────────────────┘   └────────────────────┘
          │                         │                         │
          └────────────┬────────────┴─────────────────────────┘
                       │
              ┌────────┴────────┐
              │  MIR Backend    │
              │  import_resolver│
              │  func_map       │
              │  JIT → native   │
              └────────┬────────┘
                       │
              ┌────────┴────────┐
              │  Lambda Runtime  │
              │  Item, GC, Heap  │
              │  fn_call dispatch│
              └─────────────────┘
```

---

## 5. Open Questions

1. **Closure environment compatibility**: Lambda closures use `closure_env` pointer; JS closures capture via `js_module_vars[]` slots. Can a cross-language function call correctly invoke a closure from the other language? Likely yes for module-level functions (no closure env), but closures may need a wrapper.

2. **Mutable module variables**: Lambda pub variables are stored in struct slots. JS module variables use `js_module_vars[]`. When a cross-language consumer reads a mutable variable, should it see live updates? The namespace object has a snapshot of the value at registration time. Live bindings would require a getter function in the namespace rather than a direct value.

3. **Type information**: Lambda has a rich type system (union types, type patterns). Should the namespace object carry type metadata for cross-language type checking?

4. **Error handling**: Lambda uses `T^E` error returns; JS uses exceptions (not yet implemented in the runtime). How should errors propagate across language boundaries?

5. **`this` binding**: JS functions may depend on `this` (e.g., method calls). Lambda has no `this` concept. Cross-language calls to JS methods would need to pass `ItemNull` as `this`, or the namespace object itself.

6. **Hot reloading**: If a module is recompiled (e.g., during development), should the namespace object in the registry be updated? How do cached function pointers in Lambda struct slots get invalidated?
