# Lambda Module System — Design & Implementation

## 1. Overview

Lambda's module system allows scripts to import functions and variables from other Lambda script files. Modules are compiled as separate JIT compilation units and linked at runtime through generated C struct indirection.

**Key design principles:**
- **Eager loading**: imports are resolved during AST building, not lazily at runtime.
- **Separate JIT contexts**: each script (main + each import) gets its own MIR JIT context.
- **Shared runtime**: all modules share a single `Context* rt` (heap, pool, allocator).
- **Module-local constants and types**: each module maintains its own `const_list` and `type_list`, injected at link time.
- **Deduplication**: importing the same module path twice returns the cached `Script*`.

---

## 2. Syntax

Defined in `lambda/tree-sitter-lambda/grammar.js`:

```
import .relative.module.path       // relative import (dot-separated → file path)
import name: .relative.module      // aliased import (NOT fully implemented)
import .mod1, .mod2                // multiple imports
```

- **Relative paths** start with `.` — dots are converted to `/` with `.ls` appended.
  - `import .utils.readability` → loads `./utils/readability.ls`
- **Quoted paths** are also supported: `import "./utils/readability.ls"`
- **Absolute module paths** (no leading dot) are logged as errors — not yet supported.
- Imports must appear at the **top of the document**, before any other content.

### Visibility

Only `pub fn`, `pub pn`, and `pub let` declarations are exported from a module:

```
pub fn add(a, b) => a + b          // exported function
pub let PI = 3.14159               // exported variable
fn helper(x) => x * 2             // private — not visible to importers
```

---

## 3. Architecture

### 3.1 End-to-End Flow

```
 Source Code                 AST Building              Transpilation             Runtime Linking
 ───────────                ────────────              ─────────────             ───────────────
 import .utils.math    →    AstImportNode         →    struct Mod1 {        →   find BSS "m1"
                             ├── module path             void** consts;         ├── _init_mod_consts()
                             ├── alias (opt)              Item (*_add)(...);    ├── _init_mod_types()
                             └── script*                  Item _PI;             ├── fn ptrs → struct
                                  │                     } m1;                   └── pub vars → struct
                                  │
                                  └── load_script() ──→ parse → build AST → transpile → JIT compile
                                      (recursive, eager)
```

### 3.2 Key Data Structures

#### `AstImportNode` (`lambda/ast.hpp`)
```cpp
struct AstImportNode : AstNode {
    String* alias;      // optional alias name (grammar supports, not fully used)
    StrView module;     // module path string (e.g., "./utils/readability.ls")
    Script* script;     // pointer to the loaded/compiled imported script
    bool is_relative;   // whether path starts with '.'
};
```

#### `Script` (`lambda/ast.hpp`)
```cpp
struct Script {
    const char* reference;   // path relative to main script
    int index;               // unique index in scripts list (used as m0, m1, ...)
    bool is_main;            // main script vs imported module
    AstNode* ast_root;       // the AST tree
    void* jit_context;       // separate MIR JIT context for this module
    void* main_func;         // compiled entry point
    ArrayList* const_list;   // module-local constant table
    void* type_list;         // module-local type definitions
};
```

#### `NameEntry` (scope table)
Each name in the symbol table has an `import` field. When non-null, it indicates the name was imported from a module. The transpiler uses this to generate `m{N}.` prefixed references.

---

## 4. Compilation Pipeline

### 4.1 AST Building (`lambda/build_ast.cpp`)

When `import .utils.math` is encountered:

1. **Parse**: Extract module path from Tree-sitter CST, convert dot-notation to file path.
2. **Load**: Call `load_script()` — eagerly reads, parses, builds AST, transpiles, and JIT-compiles the imported module. This is **recursive** — if the imported module has imports of its own, those are loaded first.
3. **Register names**: Walk the imported script's AST root, find all `pub fn`/`pub pn`/`pub let` declarations, and push them into the importing script's scope table with the `import` pointer set.

### 4.2 Transpilation (`lambda/transpile.cpp`)

#### Module Struct Generation (`define_module_import`)

For each import, the transpiler generates a C struct:

```c
struct Mod1 {
    void** consts;                    // pointer to module's const_list
    Item (*_add_42)(Item, Item);      // function pointer for each pub fn
    Item _PI;                         // each pub variable
} m1;
```

The struct instance is named `m{N}` where N is the script's unique index.

#### Name Prefixing

When transpiling an identifier whose `NameEntry.import` is non-null, the name is prefixed with `m{N}.`:

```c
// Lambda:  add(1, 2)    (where add is imported from module index 1)
// C:       m1._add_42(i2it(1), i2it(2))
```

#### Module-Local Constants and Types (non-main scripts only)

Each imported module needs its own constant table and type definitions. The transpiler emits:

**Constants** — `static void** _mod_consts` with `_init_mod_consts()`:
```c
static void** _mod_consts;
void _init_mod_consts(void** consts) { _mod_consts = consts; }

// Override const macros to use module-local constants
#undef const_d2it
#undef const_s2it
// ... (all 10 const_ macros)
#define const_d2it(index)  d2it(_mod_consts[index])
#define const_s2it(index)  s2it(_mod_consts[index])
#define const_s(index)     ((String*)_mod_consts[index])
// ...
```

**Types** — `static void* _mod_type_list` with `_init_mod_types()` and wrapper functions:
```c
static void* _mod_type_list;
void _init_mod_types(void* tl) { _mod_type_list = tl; }

// Wrappers that swap rt->type_list to module's before calling real functions
static Map* _mod_map(int ti) {
    void* sv = rt->type_list;
    rt->type_list = _mod_type_list;
    Map* r = map(ti);
    rt->type_list = sv;
    return r;
}
static Element* _mod_elmt(int ti) { /* same pattern */ }
static Type* _mod_const_type(int ti) { /* same pattern */ }
static TypePattern* _mod_const_pattern(int ti) { /* same pattern */ }

// Redirect calls to use module-local wrappers
#define map(idx) _mod_map(idx)
#define elmt(idx) _mod_elmt(idx)
#define const_type(idx) _mod_const_type(idx)
#define const_pattern(idx) _mod_const_pattern(idx)
```

### 4.3 Runtime Linking (`lambda/runner.cpp` — `init_module_import`)

After JIT compilation, the runner links cross-module references:

1. **Find BSS item**: Locate the `m{N}` struct in the main script's JIT context via MIR BSS lookup.
2. **Initialize constants**: Call `_init_mod_consts()` from the imported module's JIT context, passing the module's `const_list->data` pointer.
3. **Initialize types**: Call `_init_mod_types()` from the imported module's JIT context, passing the module's `type_list` pointer.
4. **Populate function pointers**: For each `pub fn`/`pub pn`, look up the function address in the imported module's JIT context, write it into the `Mod{N}` struct.
5. **Populate public variables**: For each `pub let` variable, look up the data address in the imported module's JIT context, `memcpy` the bytes into the struct.

### 4.4 Shared Runtime

All modules share a single runtime context via:
```c
extern Context* _lambda_rt;
#define rt _lambda_rt
```

This means all modules access the same heap, pool, and allocator — they can freely pass `Item` values between each other.

---

## 5. Resolved Issues

### 5.1 Module `const_list` Sharing (Critical)

**Symptom**: Imported module functions returned garbage values for string/float/decimal constants.

**Root cause**: All modules shared the main script's `rt->consts` pointer. Each module has its own `const_list` with its own indexing (e.g., constant index 0 in module A is a different string than index 0 in module B). Without separate constant access, an imported module would index into the wrong constant list, reading garbage or crashing.

**Fix**: Added `static void** _mod_consts` and `_init_mod_consts()` in transpiled code for non-main scripts. Override all `const_*` macros to use `_mod_consts` instead of `rt->consts`. At link time, `init_module_import()` calls `_init_mod_consts(script->const_list->data)`.

**Files**: `lambda/transpile.cpp` (lines ~4794-4816), `lambda/runner.cpp` (lines ~163-170).

### 5.2 Module `type_list` Sharing (Critical)

**Symptom**: Imported module functions that create maps or elements produced corrupted structures — wrong shapes, wrong field layouts.

**Root cause**: Same pattern as const_list. Functions like `map()`, `elmt()`, `const_type()`, and `const_pattern()` look up type definitions from `rt->type_list` using an integer type index. Each module has its own type indices that correspond to its own `type_list`. Without separation, module code would look up types from the main script's type_list, getting wrong struct shapes.

**Fix**: Added `static void* _mod_type_list` and `_init_mod_types()`. Added 4 wrapper functions (`_mod_map`, `_mod_elmt`, `_mod_const_type`, `_mod_const_pattern`) that **save/restore** `rt->type_list` before calling the real function. Macros redirect all calls to the wrappers. At link time, `init_module_import()` calls `_init_mod_types(script->type_list)`.

**Files**: `lambda/transpile.cpp` (lines ~4818-4838), `lambda/runner.cpp` (lines ~172-182).

### 5.3 Arena vs Heap Container Crash (`is_heap` flag)

**Symptom**: `malloc: pointer being freed was not allocated` crash when module functions return or manipulate DOM elements from parsed HTML input.

**Root cause**: Lambda has two allocation strategies:
- **Arena allocation**: for input document parsing (HTML DOM, XML, etc.) — bulk-freed at arena lifecycle end, ref_cnt is irrelevant.
- **Heap allocation**: for runtime computation — individually reference counted and freed via `free_container`.

Before this fix, `free_container` / `free_item` couldn't distinguish between the two. When `frame_end()` decremented ref_cnt on arena-allocated containers (which always have ref_cnt=0), it would trigger `free_container` on arena pointers, corrupting memory.

**Fix**: Added `uint8_t is_heap:1` bit flag to the `Container` struct in `lambda.h`. `heap_calloc()` sets `is_heap = 1` for all container types. `free_container()` returns early with `if (!cont->is_heap)`. `free_item()` and `free_map_item()` check `container->is_heap` before decrementing ref_cnt or freeing.

**Files**: `lambda/lambda.h` (Container struct), `lambda/lambda-mem.cpp` (heap_calloc, free_container, free_item, free_map_item).

### 5.4 `fn_join` Inline Array Crash

**Symptom**: `free()` crash when concatenating lists/arrays containing DOM elements.

**Root cause**: `fn_join` (the `++` operator) in `lambda-eval.cpp` allocates arrays with inline items: `result->items = (Item*)(result + 1)`. The items are part of the same memory block as the container header. When `free_container` later does `free(arr->items)`, it tries to `free()` a pointer into the middle of an already-allocated block.

**Fix**: Added inline-items detection in `free_container`: `if (arr->items && arr->items != (Item*)(arr + 1))` — only calls `free(arr->items)` for separately allocated item buffers. Same pattern for ArrayInt/ArrayInt64/ArrayFloat.

**Files**: `lambda/lambda-mem.cpp` (free_container for Array/List/ArrayInt/ArrayInt64/ArrayFloat sections).

---

## 6. Outstanding Issues

### 6.1 Aliased Imports Not Functional

**Status**: Grammar and AST support exist, but semantic analysis and transpiler don't use the alias.

**Symptom**: `import math: .utils.math` parses successfully but behaves identically to `import .utils.math` — the alias `math` is ignored.

**Root cause**: The `alias` field is parsed and stored on `AstImportNode`, but `declare_module_import()` always registers imported names under their **original names** directly into the importing scope. The alias is never referenced during name resolution or transpilation.

**What's needed**: For aliased imports to work, `declare_module_import()` would need to register the alias as a namespace prefix (e.g., `math.add` instead of `add`), and the transpiler would need to resolve qualified names through the alias to the module struct.

### 6.2 Double-For Comprehension Does Not Flatten

**Status**: Bug confirmed. Affects `find_all` pattern used in readability.ls.

**Symptom**: 
```
[for (a in [[1, 2], [3, 4]]) for (x in a) x]
// Expected: [1, 2, 3, 4]
// Actual:   [[1, 2], [3, 4]]
```

**Root cause**: The second `for` is parsed as the **body expression** of the first `for` (not as a second loop clause). The outer for-expression pushes its body result with `array_push(arr_out, <body>)` at `transpile.cpp` line ~2012. The body is itself a for-expression that returns a spreadable array. But `array_push` does NOT spread — it pushes the inner array as a single element.

The transpiled C code shows this clearly:
```c
// Outer for
Array* arr_out = array_spreadable();
for (int _idx = 0; _idx < arr->length; _idx++) {
    ArrayInt* _a = arr->items[_idx];
    array_push(arr_out, ({          // ← plain push, NOT spread
        // Inner for
        Array* arr_out = array_spreadable();
        for (int _idx = 0; _idx < arr->length; _idx++) {
            int32_t _x = arr->items[_idx];
            array_push(arr_out, i2it(_x));
        }
        array_end(arr_out);         // ← returns spreadable array as Item
    }));
}
array_end(arr_out);
```

**Impact**: The `find_all_helper` function in `utils/readability.ls` uses this pattern:
```
let child_matches = [for (i in 0 to n-1) for (m in find_all_helper(e[i], target)) m]
```
The inner `find_all_helper` returns an array, which gets wrapped in another array layer instead of being flattened. This causes `find_all(doc, "h1")` to return `[array]` instead of `[element]`, breaking title extraction.

**Likely fix**: In `transpile_for`, change `array_push(arr_out, ...)` to `array_push_spread(arr_out, ...)` for the body push. The `array_push_spread` function already checks the `is_spreadable` flag and spreads spreadable arrays while leaving non-spreadable items as-is.

**Note**: This is distinct from the multi-clause loop syntax (`for (a in xs, b in ys) ...`) which IS handled correctly by `transpile_for` via `loop_node->next` chaining.

### 6.3 `let a^err = ...` — Error Variable Not Declared Globally

**Status**: Workaround available (`?` operator), root cause identified.

**Symptom**: `let doc^err = input(...)` at top-level causes MIR compilation failure: "undeclared identifier `_err`".

**Root cause**: The error destructuring `let a^err = expr` transpiles to:
```c
Item _etN = <expr>;
_a = (item_type_id(_etN) == LMD_TYPE_ERROR) ? ITEM_NULL : _etN;
_err = (item_type_id(_etN) == LMD_TYPE_ERROR) ? _etN : ITEM_NULL;
```
The `_a` variable is declared as a global, but `_err` is not declared. The transpiler generates the assignment to `_err` without ensuring it was previously declared.

**Workaround**: Use `let doc = input(...)?` with the error propagation operator `?` instead of error destructuring.

### 6.4 Absolute Module Paths Not Supported

**Status**: Stub only — logs error and fails.

**Symptom**: `import std.math` (no leading dot) fails with an error log.

**Root cause**: Only relative imports (starting with `.`) are implemented. The module path resolution code in `build_ast.cpp` only handles dot-to-slash conversion for relative paths. There is no standard library path, package registry, or module resolution algorithm for non-relative paths.

---

## 7. Design Notes

1. **No circular imports**: Since imports are eagerly loaded during AST building (the parser is not yet done when the import is processed), circular imports would cause infinite recursion. There is no cycle detection.

2. **Script deduplication**: `load_script()` checks the runtime's `scripts` ArrayList by path. Importing the same module from two different scripts returns the same `Script*`, avoiding redundant compilation.

3. **Single-threaded assumption**: The type_list swap pattern (`void* sv = rt->type_list; rt->type_list = _mod_type_list; ... rt->type_list = sv;`) is not thread-safe. This is acceptable since Lambda is single-threaded.

4. **Module functions are indirect calls**: Imported functions go through a function pointer in the `Mod{N}` struct, while local functions are direct calls. This adds one level of indirection but is negligible in practice.

5. **No dynamic re-exports**: If module A imports from module B, module A's users cannot access module B's exports through A. Each import is a direct link to the source module.

6. **Public variables are copied**: At link time, `pub let` values are `memcpy`'d into the importing script's `Mod{N}` struct. This means the importer gets a snapshot — mutations to the original are not reflected (though Lambda is pure functional, so this is moot for immutable values).
