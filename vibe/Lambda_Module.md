# Lambda Module System — Design & Implementation

## 1. Overview

Lambda's module system allows scripts to import functions and variables from other Lambda script files. Modules are compiled as separate JIT compilation units and linked at runtime through generated C struct indirection.

**Key design principles:**
- **Eager loading**: imports are resolved during AST building, not lazily at runtime.
- **Separate JIT contexts**: each script (main + each import) gets its own MIR JIT context.
- **Shared runtime**: all modules share a single `Context* rt` (heap, pool, allocator).
- **Module-local constants and types**: each module maintains its own `const_list` and `type_list`, injected at link time.
- **Execute-once semantics**: a module's `main()` evaluates once regardless of how many scripts import it.
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
                             ├── alias (opt)              Item(*_mod_main)();   ├── _init_mod_types()
                             └── script*                  void(*_init_vars)();  ├── main_func → _mod_main
                                  │                       Item (*_add)(...);    ├── init_vars_fn → _init_vars
                                  │                       Item _PI;             └── fn ptrs → struct
                                  └── load_script() ──→ parse → build AST → transpile → JIT compile
                                      (recursive, eager, circular-import-safe)
```

### Execution Flow at Runtime

When the main script's `main()` runs:
1. For each import: call `m{N}._mod_main(runtime)` — executes the module's `main()`
2. Module `main()` checks `_mod_executed` guard — if already executed, returns immediately
3. Module `main()` assigns all globals at top level (not inside `({...})`)
4. After module main returns: call `m{N}._init_vars(&m{N})` — copies module globals into the Mod struct
5. Main script accesses imported values via `m{N}._varname` and `m{N}._funcname()`

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
struct Script : Input {
    const char* reference;      // path (relative to main script) and name of the script
    int index;                  // unique index in scripts list (used as m0, m1, ...)
    bool is_main;               // main script vs imported module
    bool is_loading;            // true while script is being loaded (for circular import detection)
    const char* source;
    TSTree* syntax_tree;
    AstNode *ast_root;          // the AST tree
    NameScope* current_scope;   // current name scope
    ArrayList* const_list;      // module-local constant table
    // type_list is accessed via Input base: tp->type_list
    MIR_context_t jit_context;  // separate MIR JIT context for this module
    main_func_t main_func;      // compiled entry point
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

#### Module Struct Generation (`define_module_import` + `write_mod_struct_fields`)

For each import, the transpiler generates a C struct with a **fixed layout** (order matters for runtime pointer arithmetic in `init_module_import`):

```c
struct Mod1 {
    // --- fixed fields (always present) ---
    void** consts;                    // pointer to module's const_list
    Item (*_mod_main)(Context*);      // module's main() entry point
    void (*_init_vars)(void*);        // copies module globals into this struct

    // --- pass 1: function pointer fields (pub fn/pn only) ---
    Item (*_add_42)(Item, Item);      // function pointer for each pub fn

    // --- pass 2: pub variable fields ---
    Item _PI;                         // each pub variable, typed correctly
} m1;
```

The struct instance is named `m{N}` where N is the script's unique index. The two-pass field ordering (fn ptrs first, pub vars second) in `write_mod_struct_fields()` ensures the runner's pointer arithmetic can correctly walk past function pointers before reaching pub vars.

#### Module Self-Struct and `_init_mod_vars` (non-main scripts only)

Each imported module also generates a **self-struct** that mirrors the importer's `Mod{N}` struct, plus an `_init_mod_vars()` function that copies the module's global variables into the struct:

```c
// generated in the module's own transpiled code
struct Mod1 { /* same layout as importer's Mod1 */ };

void _init_mod_vars(void* _mp) {
    struct Mod1* _m = (struct Mod1*)_mp;
    _m->_PI = _PI;       // copy module global _PI into struct
}

static int _mod_executed = 0;  // execute-once guard
```

#### Module `main()` Generation

For modules (non-main scripts), the transpiled `main()` has special structure:

```c
Item main(Context *runtime) {
    _lambda_rt = runtime;
    if (_mod_executed) return ITEM_NULL;  // execute-once guard
    _mod_executed = 1;

    // hoisted global assignments (outside ({...}) to avoid MIR optimization issues)
    _PI = ((double)(3.14159));
    _greeting = fn_join(const_s2it(0), const_s2it(1));

    // content block (may be empty for pure-definition modules)
    Item result = ({({List* ls = list(); list_end(ls);});});
    return result;
}
```

**Critical**: Module global assignments are **hoisted to the top level** of `main()`, not inside the content block's `({...})` statement expression. This prevents MIR JIT's SSA/GVN optimization from eliminating writes to global BSS variables (see §5.5).

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

After JIT compilation, the runner links cross-module references by populating the `Mod{N}` struct:

1. **Find BSS item**: Locate the `m{N}` struct in the main script's JIT context via MIR BSS lookup.
2. **Initialize constants**: Call `_init_mod_consts()` from the imported module's JIT context, passing the module's `const_list->data` pointer.
3. **Initialize types**: Call `_init_mod_types()` from the imported module's JIT context, passing the module's `type_list` pointer.
4. **Populate fixed fields** (pointer arithmetic, matching struct layout):
   - Skip `consts` pointer (sizeof void**)
   - Write `_mod_main` = module's `main_func`
   - Write `_init_vars` = address of `_init_mod_vars` from module's JIT
5. **Populate function pointers**: For each `pub fn`/`pub pn`, look up the function address in the imported module's JIT context, write it into the `Mod{N}` struct.
6. **Pub vars**: Skipped at link time — they are populated at runtime when `_init_mod_vars` is called from the importing script's `main()`.

### 4.4 Module Initialization Order (Runtime)

In the importing script's `main()`, module initialization happens in import declaration order:

```c
// generated in main script's main()
m1._mod_main(runtime);                      // execute module 1's main()
if (m1._init_vars) m1._init_vars(&m1);      // copy module 1's globals → struct
m2._mod_main(runtime);                      // execute module 2's main()
if (m2._init_vars) m2._init_vars(&m2);      // copy module 2's globals → struct
// ... then main script body
```

This ensures:
- Module code runs **before** the importing script accesses any imported values
- The `_mod_executed` guard prevents double-execution when multiple scripts import the same module
- Pub var values are snapshots taken **after** the module has fully executed

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

### 5.5 MIR JIT Optimization of Module Global Assignments (Critical)

**Symptom**: Complex pub var assignments (string concatenation, function call results like `fn_join`, `string()`, array constructors) returned `null` when accessed from importing scripts. Simple scalars (int, float) and string literal references worked fine.

**Root cause**: Module file-scope `pub let` and `let` assignments were generated inside a **content block** wrapped in a GCC statement expression `({...})`:

```c
Item result = ({({
  List* ls = list();
  _greeting = fn_join(const_s2it(0), const_s2it(1));  // inside ({...})
  list_end(ls);
});});
```

MIR's SSA/GVN optimization pass can eliminate writes to global BSS variables when the write is inside a `({...})` statement expression and the variable is not read within the same expression. For simple types (int, float), the optimizer preserved the write, but for function call results (Items returned from `fn_join`, `string()`, etc.), the write was optimized away.

**Discovery path**: The initial attempt was to use the `*(&var)=expr` anti-optimization pattern (already used for while-loop swap variables). This was added to `assign_global_var()` but had no effect. Root cause analysis revealed that the content block calls `transpile_content_expr()` → `transpile_let_stam()` → `transpile_assign_expr()` — a completely different code path that never reaches `assign_global_var()`.

**Fix**: Hoisted module global assignments to the **top level of `main()`**, before the content block:

```c
Item main(Context *runtime) {
    _lambda_rt = runtime;
    if (_mod_executed) return ITEM_NULL;
    _mod_executed = 1;
    // hoisted assignments — at top level, NOT inside ({...})
    _greeting = fn_join(const_s2it(0), const_s2it(1));
    _items = ({ArrayInt* arr = array_int(); array_int_fill(arr,3,10,20,30); });
    // content block (now has no let/pub assignments)
    Item result = ({({List* ls = list(); list_end(ls);});});
    return result;
}
```

Three changes in `lambda/transpile.cpp`:
1. In `transpile_content_expr()`: Skip `LET_STAM`/`PUB_STAM` for modules (`is_global && !tp->is_main`) — they're handled separately.
2. In `transpile_ast_root()`: Added a pre-pass before `Item result = ({...})` that calls `assign_global_var()` for all file-scope LET/PUB vars (including those inside CONTENT nodes).
3. `assign_global_var()`: Simple direct assignment (no `*(&var)=` workaround needed since code is at top level).

**Files**: `lambda/transpile.cpp` (transpile_content_expr, transpile_ast_root main() generation).

### 5.6 Pub Var Byte Offset Mismatch (Critical)

**Symptom**: Importing scripts crashed or read garbage values for pub vars when using the old `memcpy` approach.

**Root cause**: The old implementation used `memcpy` with `type_info[type_id].byte_size` to copy pub var data from the module's BSS into the Mod struct at link time. However, the byte sizes in `type_info[]` did not match the actual C types used in the struct. For example:
- `INT` type_info says 8 bytes, but `write_type()` emits `int32_t` (4 bytes)
- `NULL` type_info says 1 byte, but `write_type()` emits `Item` (8 bytes)
- Pointer types had similar mismatches

Additionally, BSS data addresses found via `find_data()` pointed to uninitialized memory (module `main()` hadn't run yet), so the values were always zero/null.

**Fix**: Replaced the `memcpy`-at-link-time approach entirely with `_init_mod_vars()`:
- Module generates `_init_mod_vars()` which copies globals using proper C type assignments
- `_init_mod_vars()` is called from the importing script's `main()` **after** the module's `main()` has executed and populated the globals
- No more `type_info` byte-size lookups or pointer arithmetic for pub vars

**Files**: `lambda/transpile.cpp` (_init_mod_vars generation), `lambda/runner.cpp` (removed memcpy, added _init_vars fn ptr).

### 5.7 Circular Import Detection

**Symptom**: Circular imports caused infinite recursion in `load_script()` → AST building → `load_script()`, leading to stack overflow.

**Root cause**: `load_script()` is called during AST building (eager loading), which can trigger recursive `load_script()` for nested imports. No cycle detection existed.

**Fix**: Added `bool is_loading` flag to the `Script` struct. Set to `true` before `transpile_script()` and `false` after. In `load_script()`, if a script is found in the cache with `is_loading == true`, it's a circular import — log error, print to stderr, return NULL.

**Files**: `lambda/ast.hpp` (is_loading field), `lambda/runner.cpp` (load_script).

### 5.8 Module Error Propagation

**Symptom**: If a module failed to compile (e.g., type errors), the importing script would crash trying to access the module's functions.

**Root cause**: After `transpile_script()` failed for a module, `load_script()` continued and returned the Script with `jit_context == NULL`. The importing script would then try to look up functions in a null JIT context.

**Fix**: Added check in `load_script()`: if `!new_script->jit_context` after transpilation, log error and return NULL.

**Files**: `lambda/runner.cpp` (load_script).

---

## 6. Outstanding Issues & Known Concerns

### 6.1 Aliased Imports Not Functional

**Status**: Grammar and AST support exist, but semantic analysis and transpiler don't use the alias.

**Symptom**: `import math: .utils.math` parses successfully but behaves identically to `import .utils.math` — the alias `math` is ignored.

**Root cause**: The `alias` field is parsed and stored on `AstImportNode`, but `declare_module_import()` always registers imported names under their **original names** directly into the importing scope. The alias is never referenced during name resolution or transpilation.

**What's needed**: For aliased imports to work, `declare_module_import()` would need to register the alias as a namespace prefix (e.g., `math.add` instead of `add`), and the transpiler would need to resolve qualified names through the alias to the module struct.

### 6.2 `let a^err = ...` — Error Variable Not Declared Globally

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

### 6.3 Absolute Module Paths Not Supported

**Status**: Stub only — logs error and fails.

**Symptom**: `import std.math` (no leading dot) fails with an error log.

**Root cause**: Only relative imports (starting with `.`) are implemented. The module path resolution code in `build_ast.cpp` only handles dot-to-slash conversion for relative paths. There is no standard library path, package registry, or module resolution algorithm for non-relative paths.

---

## 7. Design Notes

1. **Circular import detection**: `load_script()` tracks `is_loading` state per script. If a script is re-entered while still loading, a circular import error is reported and NULL is returned.

2. **Script deduplication**: `load_script()` checks the runtime's `scripts` ArrayList by path. Importing the same module from two different scripts returns the same `Script*`, avoiding redundant compilation.

3. **Single-threaded assumption**: The type_list swap pattern (`void* sv = rt->type_list; rt->type_list = _mod_type_list; ... rt->type_list = sv;`) is not thread-safe. This is acceptable since Lambda is single-threaded.

4. **Module functions are indirect calls**: Imported functions go through a function pointer in the `Mod{N}` struct, while local functions are direct calls. This adds one level of indirection but is negligible in practice.

5. **No dynamic re-exports**: If module A imports from module B, module A's users cannot access module B's exports through A. Each import is a direct link to the source module.

6. **Public variables are snapshots**: At runtime, `_init_mod_vars()` copies module globals into the importing script's `Mod{N}` struct after the module's `main()` has executed. The importer gets a snapshot — mutations to the original are not reflected (Lambda is pure functional, so this is moot for immutable values).

7. **Execute-once semantics**: Module `main()` has a `static int _mod_executed` guard. The first call executes and sets the guard; subsequent calls return immediately. This is important when multiple scripts import the same module — only the first `m{N}._mod_main(runtime)` call does work.

---

## 8. Type List Concern

### Current State

Each module has its own `type_list` — an array of type definitions (TypeArray, TypeMap, TypeFunc, etc.) built during AST construction. Type objects (`Type*`) are allocated in the module's AST builder memory, which lives in the `Script` struct's allocator.

At runtime, functions like `map()`, `elmt()`, `const_type()`, and `const_pattern()` look up type definitions from `rt->type_list` using integer type indices. The module wrapper functions (`_mod_map`, `_mod_elmt`, etc.) temporarily swap `rt->type_list` to the module's own type_list before calling the real function.

### The Concern

**Cross-module type pointer sharing**: When module A's functions operate on data structures whose types are defined in module B's `type_list`, or when types from module B are referenced in module A's scope, the `Type*` pointers stored in AST nodes and Items point into module B's memory.

This is currently safe because:
1. All `Script` objects persist for the entire program lifetime
2. Type memory is never freed until program exit
3. Lambda is single-threaded, so the type_list swap is race-free

However, this could become **fragile** if:
- **Module hot-reloading** is implemented: reloading a module would invalidate `Type*` pointers held by other modules
- **Module unloading** is added: freeing a module's memory would leave dangling `Type*` pointers in importers
- **Multi-threading** is introduced: the save/restore pattern for `rt->type_list` is inherently racy
- **Type comparison across modules**: Two modules may define structurally identical types (e.g., both have `{name: string, age: int}` maps), but they'll have different `Type*` pointers and different type indices, potentially causing unexpected type mismatches

### Potential Future Solutions

1. **Global type registry**: Deduplicate types across all modules into a single shared `type_list`. Each module's type indices would be remapped to the global registry. This eliminates the swap pattern entirely.
2. **Structural type equality**: When comparing types cross-module, use structural comparison rather than pointer identity or type index comparison.
3. **Type arena**: Allocate all types from a single long-lived arena rather than per-module allocators, ensuring pointers remain valid regardless of module lifecycle.

For now, the current approach works correctly for Lambda's single-threaded, load-once model.
