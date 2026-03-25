# LambdaPy Runtime — Design Document

## Overview

LambdaPy is Lambda's embedded Python engine (~7.8K LOC across 9 source files). It compiles Python to native machine code through a four-stage pipeline reusing Lambda's type system, memory management, and JIT infrastructure. Python programs execute within the same `Item`-based runtime as Lambda scripts, enabling direct interop with Lambda's input parsers, output formatters, and string internment system.

### Design Goals

1. **Unified runtime** — Python values are Lambda `Item` values. No conversion boundaries.
2. **Near-native performance** — MIR JIT compilation to native machine code.
3. **Python semantics** — Floor division, chained comparisons, truthiness, negative indexing, LEGB scoping.
4. **Reuse over reimplementation** — String internment, GC heap, shape system, formatters all delegate to existing Lambda subsystems.

---

## 1. Architecture

### 1.1 Compilation Pipeline

```
Python Source (.py)
    │
    ▼
Tree-sitter Parser     (tree-sitter-python grammar)
    │
    ▼
Python AST Builder     (build_py_ast.cpp → typed PyAstNode tree)
    │
    ▼
MIR Transpiler         (transpile_py_mir.cpp → MIR IR instructions)
    │
    ▼
MIR JIT Compiler       (MIR_link + MIR_gen → native machine code)
    │
    ▼
Execution              (py_main() function pointer call → Item result)
```

### 1.2 Unified Runtime Architecture

LambdaPy shares the same runtime layer as Lambda scripts and LambdaJS:

```
┌──────────────────────────────────────────────────────────────────┐
│                      CLI (main.cpp)                              │
│              ./lambda.exe py script.py                           │
├──────────────┬──────────────────┬────────────────────────────────┤
│  Lambda Path │  JavaScript Path │        Python Path             │
│  (.ls files) │  (.js files)     │        (.py files)             │
│              │                  │                                │
│  build_ast   │  build_js_ast    │  build_py_ast.cpp              │
│  transpile   │  transpile_js_mir│  transpile_py_mir.cpp          │
│              │                  │                                │
├──────────────┴──────────────────┴────────────────────────────────┤
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
└──────────────────────────────────────────────────────────────────┘
```

**Key insight:** Python bypasses Lambda's runner (`runner.cpp`). The CLI detects the `py` command and calls `transpile_py_to_mir()` directly, which handles parsing, compilation, and execution in one call.

### 1.3 Execution Entry Point

```
main.cpp                           transpile_py_mir.cpp
────────                           ────────────────────
argv[1] == "py"
  │
  ├─ runtime_init(&runtime)        // shared Runtime struct
  ├─ lambda_stack_init()           // GC stack bounds
  ├─ read_text_file(py_file)       // read Python source
  │
  └─ transpile_py_to_mir()  ──────→  py_transpiler_create()
                                      py_transpiler_parse()        // TS parse
                                      build_py_ast()               // TS CST → Py AST
                                      heap_init()                  // GC heap
                                      jit_init(2)                  // MIR ctx
                                      pm_transpile_ast()           // AST → MIR IR
                                      MIR_load_module()            // prepare for linking
                                      MIR_link(import_resolver)    // link imports
                                      find_func(ctx, "py_main")   // locate entry
                                      py_main(&eval_context)       // execute
                                      ← return Item result
```

---

## 2. Python Data Types ↔ Lambda Runtime Types

All Python values are represented as Lambda `Item` (64-bit tagged value). There is no conversion overhead between Python and Lambda — they share the same representation.

### 2.1 Type Mapping Table

| Python Type | Lambda TypeId | Representation | Boxing |
|-------------|--------------|----------------|--------|
| `int` | `LMD_TYPE_INT` | Tag bits + 56-bit signed value inline | Inline (no allocation) |
| `float` | `LMD_TYPE_FLOAT` | GC nursery-allocated `double*` | Pointer to nursery |
| `str` | `LMD_TYPE_STRING` | `String*` via `heap_create_name()` | GC heap allocated |
| `bool` | `LMD_TYPE_BOOL` | Tag bits + 0/1 inline | Inline (no allocation) |
| `None` | `LMD_TYPE_NULL` | Tag-only sentinel | Inline |
| `list` | `LMD_TYPE_ARRAY` | Lambda `Array` (dynamic `Item*` buffer) | GC heap allocated |
| `tuple` | `LMD_TYPE_ARRAY` | Lambda `Array` (immutable semantics) | GC heap allocated |
| `dict` | `LMD_TYPE_MAP` | Lambda `Map` with `ShapeEntry` field chain | GC heap allocated |
| `function` | `LMD_TYPE_FUNC` | Pool-allocated `Function` struct | Pool allocated |

### 2.2 Tagged Pointer Constants

```c
PY_ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56      // None
PY_ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56)|1  // True
PY_ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56)|0  // False
PY_ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56       // int tag
PY_STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56     // str tag
PY_MASK56         = 0x00FFFFFFFFFFFFFFULL                // value mask
```

**Integers** are packed directly into 56 bits (range ±2^55). Arithmetic operations check for overflow and promote to int64 heap allocation when needed.

**Strings** are interned via `heap_create_name()` — the name pool deduplicates identical strings, enabling O(1) pointer-equality comparisons.

### 2.3 Reuse of Lambda System Functions

LambdaPy reuses Lambda's existing subsystems rather than reimplementing them:

| Capability | Lambda Subsystem | Py Usage |
|-----------|-----------------|----------|
| String interning | `name_pool.hpp` → `heap_create_name()` | All string literals, string operations |
| GC heap | `lambda-mem.cpp` → `heap_alloc()`, `gc_nursery` | Object/string/float allocation |
| Memory pools | `lib/mempool.h` → `pool_create()` | AST nodes, temporary allocations |
| Shape system | `shape_builder.hpp` | Dict property layout |
| Array | `lambda-data.hpp` → `Array` | Lists, tuples |
| Map | `lambda-data.hpp` → `Map` | Dicts, objects |
| Format output | `format/format.h` | Result formatting |
| MIR JIT | `mir.c` → `jit_init()` | Code generation and execution |
| Import resolver | `sys_func_registry.c` | Linking runtime functions |

---

## 3. Python-Specific Transpiler & Runtime Design

### 3.1 Transpiler Architecture (`transpile_py_mir.cpp`)

The Python transpiler emits MIR IR directly (no intermediate C code), following the same approach as the JS transpiler. The core struct is `PyMirTranspiler`:

```c
struct PyMirTranspiler {
    MIR_context_t ctx;              // MIR JIT context
    MIR_module_t module;            // current MIR module
    MIR_item_t current_func_item;   // MIR function item being emitted
    MIR_func_t current_func;        // MIR function being emitted
    PyTranspiler* tp;               // Python parser/AST context

    struct hashmap* import_cache;   // runtime function import deduplication
    struct hashmap* local_funcs;    // user-defined functions

    struct hashmap* var_scopes[64]; // variable scope stack
    int scope_depth;

    PyLoopLabels loop_stack[32];    // break/continue label stack
    int loop_depth;

    int reg_counter;                // MIR register counter
    int label_counter;              // MIR label counter

    PyFuncCollected func_entries[128]; // collected function definitions
    int func_count;

    struct hashmap* module_consts;  // module-level constants (reserved)
    int module_var_count;
    bool in_main;

    // Closure support
    MIR_reg_t scope_env_reg;
    int scope_env_slot_count;
    int current_func_index;
};
```

### 3.2 Multi-Phase Compilation

| Phase | Name | Description |
|-------|------|-------------|
| 1 | Function Collection | Walk AST to collect all `def` declarations, record name and param count |
| 2 | Module Variable Scan | Reserved for future constant folding (currently no-op; all variables are scope-managed) |
| 3 | Function Compilation | Emit MIR for each user-defined function (`pyf_<name>`) |
| 4 | Entry Point Creation | Create `py_main()` — transpile top-level statements, skip already-compiled function defs |

### 3.3 Code Generation Design

All Python values are boxed `Item` values (`MIR_T_I64`). Expressions are emitted as sequences of MIR instructions that produce `Item` results in registers:

```
Python:  x = a + b * 2

MIR:     boxi_1 = OR(PY_ITEM_INT_TAG, 2)     // box literal 2
         py_multiply_2 = CALL py_multiply(reg_b, boxi_1)
         py_add_3 = CALL py_add(reg_a, py_multiply_2)
         MOV _py_x, py_add_3                  // assign to variable register
```

**String literals** are compiled as interned constants:
```
Python:  s = "hello"

MIR:     cs_1 = MOV <namepool_address>       // compile-time interned string
         heap_create_name_2 = CALL heap_create_name(cs_1)
         boxs_3 = OR(PY_STR_TAG, heap_create_name_2)
         MOV _py_s, boxs_3
```

### 3.4 Function Compilation

User-defined functions are compiled as separate MIR functions with the `pyf_` prefix (to avoid collision with runtime function names like `py_add`):

```
Python:  def add(a, b):
             return a + b

MIR:     pyf_add: func i64, i64:_py_a, i64:_py_b
             py_add_0 = CALL py_add(_py_a, _py_b)
             RET py_add_0
             RET PY_ITEM_NULL_VAL    // implicit None return
         endfunc
```

**Key implementation details:**
- Each parameter gets a unique MIR register name (`_py_<paramname>`) — MIR requires unique names
- Parameters are registered in the function's scope via `pm_set_var()`
- A trailing `RET PY_ITEM_NULL_VAL` is emitted after the body to ensure Python's implicit `return None`
- Functions are collected in a pre-pass (Phase 1) so they can be called before their definition appears textually

### 3.5 Builtin Call Optimization

When the transpiler encounters a call to a known builtin, it emits a direct runtime call instead of going through the generic `py_call_function` dispatch:

| Builtin | Runtime Function | Notes |
|---------|-----------------|-------|
| `print(...)` | `py_print(args[], argc)` | Stack-allocated arg array |
| `len(x)` | `py_builtin_len(x)` | Direct call |
| `type(x)` | `py_builtin_type(x)` | Returns `<class 'int'>` etc. |
| `isinstance(x, t)` | `py_builtin_isinstance(x, t)` | Type check |
| `int(x)` | `py_builtin_int(x)` | Type conversion |
| `float(x)` | `py_builtin_float(x)` | Type conversion |
| `str(x)` | `py_builtin_str(x)` | Type conversion |
| `bool(x)` | `py_builtin_bool(x)` | Type conversion |
| `abs(x)` | `py_builtin_abs(x)` | Works on int/float |
| `range(...)` | `py_builtin_range(args[], argc)` | 1/2/3 arg variants |
| `min(...)` | `py_builtin_min(args[], argc)` | Variadic |
| `max(...)` | `py_builtin_max(args[], argc)` | Variadic |
| `sum(x)` | `py_builtin_sum(x)` | Iterable accumulation |
| `sorted(x)` | `py_builtin_sorted(x)` | Returns new list |
| `reversed(x)` | `py_builtin_reversed(x)` | Returns new list |
| `enumerate(x)` | `py_builtin_enumerate(x)` | Returns list of (i, val) tuples |
| `zip(...)` | `py_builtin_zip(args[], argc)` | Parallel iteration |
| `map(f, iter)` | `py_builtin_map(f, iter)` | Applies function to iterable |
| `filter(f, iter)` | `py_builtin_filter(f, iter)` | Filters iterable by predicate |
| `ord(c)` | `py_builtin_ord(c)` | Character → int |
| `chr(n)` | `py_builtin_chr(n)` | Int → character |
| `input(prompt)` | `py_builtin_input(prompt)` | Stdin read |
| `repr(x)` | `py_builtin_repr(x)` | String representation |
| `hash(x)` | `py_builtin_hash(x)` | Hash value |
| `id(x)` | `py_builtin_id(x)` | Object identity |
| `list(x)` | `py_builtin_list(x)` | Collection constructor |
| `dict(...)` | `py_builtin_dict(args[], argc)` | Dict constructor |
| `set(x)` | `py_builtin_set(x)` | Set constructor |
| `tuple(x)` | `py_builtin_tuple(x)` | Tuple constructor |

Unrecognized function calls go through the generic path: evaluate the callable, collect arguments into a stack array, and call `py_call_function(func, args[], argc)`.

### 3.6 Method Call Dispatch

Method calls (`obj.method(args)`) use a three-tier dispatch strategy with early exit:

```
pm_transpile_call() → if call.function is ATTRIBUTE:
    obj = emit(attr.object)
    method_name = box_string(attr.attribute)
    args[] = emit_all(arguments)
    │
    ├─ result = py_string_method(obj, name, args, argc)
    │   └─ if result != None → return result
    │
    ├─ result = py_list_method(obj, name, args, argc)
    │   └─ if result != None → return result
    │
    └─ result = py_dict_method(obj, name, args, argc)
        └─ return result (None if no match)
```

Each dispatcher checks the object's `TypeId` and matches the method name against its known methods. The dispatchers return `None` to signal "not my type," allowing the next dispatcher to try.

### 3.7 Scope & Variable Management

Variables are managed through a stack of hashmap-based scopes:

- **Module scope** (depth 0) — Top-level variables
- **Function scope** — Created on function entry, destroyed on exit
- **Block scope** — Created for compound statements (if/while/for blocks)

**Variable lookup** walks the scope chain from innermost to outermost (`pm_find_var`), implementing Python's LEGB (Local → Enclosing → Global → Built-in) resolution order.

**Variable assignment** creates a new MIR register in the current scope if the variable doesn't exist yet:

```c
// First assignment: x = 42
MIR_reg_t reg = pm_new_reg(mt, "_py_x", MIR_T_I64);
MIR_MOV(reg, boxed_42);
pm_set_var(mt, "_py_x", reg);

// Subsequent use: reads the same register
PyMirVarEntry* var = pm_find_var(mt, "_py_x");
// use var->reg
```

### 3.8 Control Flow

**If/elif/else:**
```
Python:                     MIR:
if cond1:                   cond1_reg = emit(cond1)
    body1                   truthy = CALL py_is_truthy(cond1_reg)
elif cond2:                 BF L_elif, truthy
    body2                   emit(body1); JMP L_end
else:                       L_elif:
    body3                   cond2_reg = emit(cond2)
                            truthy2 = CALL py_is_truthy(cond2_reg)
                            BF L_else, truthy2
                            emit(body2); JMP L_end
                            L_else: emit(body3)
                            L_end:
```

**For loop** (index-based iteration over lists/ranges):
```
Python:                     MIR:
for x in iterable:          iter = emit(iterable)
    body                    length = CALL py_builtin_len(iter)
                            idx = BOX_INT(0)
                            L_loop:
                            lt_reg = CALL py_lt(idx, length)
                            truthy = CALL py_is_truthy(lt_reg)
                            BF L_end, truthy
                            _py_x = CALL py_subscript_get(iter, idx)
                            emit(body)
                            L_continue:
                            idx = CALL py_add(idx, BOX_INT(1))
                            JMP L_loop
                            L_end:
```

**While loop:**
```
Python:                     MIR:
while cond:                 L_loop:
    body                    cond_reg = emit(cond)
                            truthy = CALL py_is_truthy(cond_reg)
                            BF L_end, truthy
                            emit(body)
                            L_continue:
                            JMP L_loop
                            L_end:
```

**Break/continue** emit `JMP` to the nearest loop's `break_label` or `continue_label` from the loop stack.

### 3.9 Chained Comparisons

Python's chained comparisons (`a < b < c`) compile to short-circuit evaluation:

```
Python:  a < b < c

MIR:     left = emit(a)
         right0 = emit(b)
         cmp0 = CALL py_lt(left, right0)
         truthy0 = CALL py_is_truthy(cmp0)
         BF L_false, truthy0          // short-circuit if a < b is false
         right1 = emit(c)
         cmp1 = CALL py_lt(right0, right1)
         truthy1 = CALL py_is_truthy(cmp1)
         BF L_false, truthy1
         result = PY_ITEM_TRUE_VAL
         JMP L_end
         L_false:
         result = PY_ITEM_FALSE_VAL
         L_end:
```

### 3.10 Boolean Short-Circuit

`and`/`or` operators implement Python's value-returning semantics:

```
Python:  a and b    →  a if a is falsy, else b
Python:  a or b     →  a if a is truthy, else b

MIR (and):
         left = emit(a)
         truthy = CALL py_is_truthy(left)
         BF L_short, truthy       // if a falsy, result = a
         right = emit(b)
         MOV result, right
         JMP L_end
         L_short:
         MOV result, left
         L_end:
```

### 3.11 Exception Handling

Exceptions use global thread state (same pattern as LambdaJS):

```c
static bool py_exception_pending;
static Item py_exception_value;

// Factory for typed exceptions:
Item py_new_exception(Item type_name, Item message);
```

- `raise expr` → `py_raise(val)` sets the pending exception
- `py_check_exception()` tests the flag
- `py_clear_exception()` retrieves and clears the exception value
- Assert: `assert test[, msg]` → emit test, branch to `py_raise("AssertionError", msg)` on failure

### 3.12 MIR Import Resolution

Python runtime functions are registered in `sys_func_registry.c` alongside Lambda and JS functions. The `import_resolver()` function (defined in `mir.c`) resolves symbol names to native function pointers:

| Category | Examples |
|----------|---------|
| Type conversion | `py_to_int`, `py_to_float`, `py_to_str`, `py_to_bool`, `py_is_truthy` |
| Arithmetic | `py_add`, `py_subtract`, `py_multiply`, `py_divide`, `py_floor_divide`, `py_modulo`, `py_power`, `py_negate`, `py_positive`, `py_bit_not` |
| Bitwise | `py_bit_and`, `py_bit_or`, `py_bit_xor`, `py_lshift`, `py_rshift` |
| Comparison | `py_eq`, `py_ne`, `py_lt`, `py_le`, `py_gt`, `py_ge`, `py_is`, `py_is_not`, `py_contains` |
| Objects | `py_getattr`, `py_setattr`, `py_new_object` |
| Collections | `py_list_new`, `py_list_append`, `py_list_get`, `py_list_set`, `py_list_length`, `py_dict_new`, `py_dict_get`, `py_dict_set`, `py_tuple_new`, `py_tuple_set` |
| Subscript | `py_subscript_get`, `py_subscript_set` |
| Iterators | `py_get_iterator`, `py_iterator_next`, `py_range_new`, `py_stop_iteration`, `py_is_stop_iteration` |
| Closures | `py_new_function`, `py_new_closure`, `py_alloc_env`, `py_call_function` |
| Builtins | `py_print`, `py_builtin_len`, `py_builtin_range`, `py_builtin_type`, `py_builtin_isinstance`, ... (29 total) |
| Methods | `py_string_method`, `py_list_method`, `py_dict_method` |
| Exceptions | `py_raise`, `py_check_exception`, `py_clear_exception`, `py_new_exception` |
| Module vars | `py_set_module_var`, `py_get_module_var`, `py_reset_module_vars` |
| Runtime init | `py_runtime_set_input` |

---

## 4. Runtime Library

### 4.1 Arithmetic Semantics (`py_runtime.cpp`)

Python arithmetic follows Python semantics, not C semantics:

| Operation | Python Semantics | Implementation |
|-----------|-----------------|----------------|
| `+` | Numeric add, string concat, list concat | Type dispatch: int/float arithmetic, `String*` concatenation via `heap_create_name`, array merge |
| `*` | Numeric multiply, string/list repetition | `"ab" * 3` → `"ababab"` via buffer build |
| `/` | True division (always float) | `py_divide()` promotes to double |
| `//` | Floor division (rounds toward −∞) | `q = a/b; if ((a^b)<0 && a%b!=0) q--` |
| `%` | Modulo (sign of divisor) | `r = a%b; if (r!=0 && (r^b)<0) r += b` |
| `**` | Power | Square-and-multiply for integer exponents |
| `-x` | Negation | `py_negate()` — int inline, float heap |

**Overflow handling:** 56-bit inline integers overflow to 64-bit heap-allocated `int64_t*` values. Arithmetic checks value ranges after operations.

### 4.2 String Methods (`py_string_method`)

The string method dispatcher matches method names and delegates to implementations:

| Method | Implementation |
|--------|---------------|
| `upper()`, `lower()` | Character-by-character transform, `heap_create_name()` result |
| `strip()`, `lstrip()`, `rstrip()` | Whitespace trim |
| `split(sep)` | Scan for separator, build array of strings |
| `join(iterable)` | Concatenate with separator |
| `replace(old, new)` | Substring replacement |
| `startswith(prefix)`, `endswith(suffix)` | Prefix/suffix check |
| `find(sub)` | Substring search, returns -1 on miss |
| `count(sub)` | Count non-overlapping occurrences |
| `isdigit()`, `isalpha()` | Character class tests |
| `title()` | Title-case conversion |
| `capitalize()` | Capitalize first character |
| `format(...)` | String formatting (stub — returns string unchanged) |

### 4.3 List Methods (`py_list_method`)

| Method | Implementation |
|--------|---------------|
| `append(x)` | `array_push()` — dynamic array growth |
| `pop([i])` | Remove and return element (default: last) |
| `insert(i, x)` | Shift elements, insert at index |
| `remove(x)` | Linear scan, remove first occurrence |
| `extend(iterable)` | Append all elements |
| `index(x)` | Linear search, return index |
| `count(x)` | Count occurrences |
| `sort()` | Insertion sort |
| `reverse()` | In-place reverse |
| `copy()` | Shallow copy |
| `clear()` | Reset array length |

**Negative indexing:** `list[-1]` is translated to `list[len-1]` by `py_subscript_get`.

### 4.4 Dict Methods (`py_dict_method`)

| Method | Implementation |
|--------|---------------|
| `get(key[, default])` | Lookup with optional default |
| `keys()` | Return array of key strings |
| `values()` | Return array of values |
| `items()` | Return array of (key, value) tuples |
| `pop(key)` | Remove and return value |
| `clear()` | Remove all entries |
| `update(other)` | Merge entries from another dict |

Dicts are implemented as Lambda `Map` structs with linked `ShapeEntry` chains. Property names are interned via the name pool.

### 4.5 Truthiness (`py_is_truthy`)

Follows Python's truthiness rules:

| Value | Truthy? |
|-------|---------|
| `None` | `False` |
| `False` | `False` |
| `0`, `0.0` | `False` |
| `""` (empty string) | `False` |
| `[]` (empty list) | `False` |
| `{}` (empty dict) | `False` |
| Everything else | `True` |

---

## 5. AST & Parser

### 5.1 AST Node Types (`py_ast.hpp`)

60 node types organized by category (+ `NULL` sentinel and `NODE_COUNT`):

**Program:** `MODULE`

**Statements (26):** `EXPRESSION_STATEMENT`, `ASSIGNMENT`, `AUGMENTED_ASSIGNMENT`, `RETURN`, `IF`, `ELIF`, `ELSE`, `WHILE`, `FOR`, `BREAK`, `CONTINUE`, `PASS`, `FUNCTION_DEF`, `CLASS_DEF`, `IMPORT`, `IMPORT_FROM`, `GLOBAL`, `NONLOCAL`, `DEL`, `ASSERT`, `RAISE`, `TRY`, `EXCEPT`, `FINALLY`, `WITH`, `BLOCK`

**Expressions (32):** `IDENTIFIER`, `LITERAL`, `FSTRING`, `BINARY_OP`, `UNARY_OP`, `NOT`, `BOOLEAN_OP`, `COMPARE`, `CALL`, `ATTRIBUTE`, `SUBSCRIPT`, `SLICE`, `STARRED`, `LIST`, `TUPLE`, `DICT`, `SET`, `LIST_COMPREHENSION`, `DICT_COMPREHENSION`, `SET_COMPREHENSION`, `GENERATOR_EXPRESSION`, `CONDITIONAL_EXPR`, `LAMBDA`, `KEYWORD_ARGUMENT`, `PARAMETER`, `DEFAULT_PARAMETER`, `TYPED_PARAMETER`, `DICT_SPLAT_PARAMETER`, `LIST_SPLAT_PARAMETER`, `TUPLE_UNPACK`, `PAIR`, `DECORATOR`

### 5.2 Operator Enum (42 operators + sentinel)

- **Arithmetic:** `+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`
- **Bitwise:** `&`, `|`, `^`, `~`, `<<`, `>>`
- **Comparison:** `==`, `!=`, `<`, `<=`, `>`, `>=`, `is`, `is not`, `in`, `not in`
- **Logical:** `and`, `or`, `not`
- **Unary:** `negate` (−x), `positive` (+x)
- **Augmented assignment:** `+=`, `-=`, `*=`, `/=`, `//=`, `%=`, `**=`, `@=`, `<<=`, `>>=`, `&=`, `|=`, `^=`

### 5.3 Key AST Structures

```c
PyCompareNode {                  // Chained: a < b < c
    PyAstNode base;
    PyAstNode* left;             // first operand
    PyOperator* ops;             // dynamic array of comparison operators
    PyAstNode** comparators;     // dynamic array of comparison operands
    int op_count;
}

PyFunctionDefNode {
    PyAstNode base;
    String* name;
    PyAstNode* params;           // linked list of PyParamNode
    PyAstNode* body;             // function body (block)
    PyAstNode* decorators;
    PyAstNode* return_annotation;
}

PyIfNode {
    PyAstNode base;
    PyAstNode* test;
    PyAstNode* body;
    PyAstNode* elif_clauses;     // linked list of elif nodes
    PyAstNode* else_body;
}
```

### 5.4 AST Builder (`build_py_ast.cpp`)

Converts Tree-sitter Python CST nodes to typed Lambda AST nodes. Key design points:

- **Pool-allocated:** All AST nodes are allocated from `tp->ast_pool` — single bulk free on cleanup
- **Linked lists:** Children (params, arguments, statements) are linked via `node->next`
- **String interning:** Identifier names and string literals go through `pool_strcpy()` / `heap_create_name()`
- **Scope registration:** `build_py_assignment()` calls `py_scope_define()` for the target variable
- **Escape sequences:** `py_decode_escape()` handles `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\0`, `\a`, `\b`, `\f`, `\v`
- **Assignment ambiguity:** Tree-sitter Python may produce `assignment` nodes either inside or outside `expression_statement`; both paths are handled

---

## 6. Scope System

### 6.1 LEGB Scoping (`py_scope.cpp`, `py_transpiler.hpp`)

Python's LEGB (Local → Enclosing → Global → Built-in) scope resolution:

```c
typedef enum {
    PY_SCOPE_MODULE,         // global scope
    PY_SCOPE_FUNCTION,       // local function scope
    PY_SCOPE_CLASS,          // class body
    PY_SCOPE_COMPREHENSION   // comprehension scope
} PyScopeType;

typedef enum {
    PY_VAR_LOCAL,     // assigned in current scope
    PY_VAR_GLOBAL,    // global declaration → module_scope
    PY_VAR_NONLOCAL,  // nonlocal declaration → nearest enclosing function
    PY_VAR_FREE,      // captured from enclosing scope
    PY_VAR_CELL,      // local, captured by inner function
    PY_VAR_MODULE     // module-level variable
} PyVarKind;
```

- `global x` → defines `x` directly in the module scope
- `nonlocal x` → walks to the nearest enclosing function scope
- Normal assignment → defines in current scope
- Lookup → walks chain from inner to outer scope

### 6.2 MIR Variable Mapping

At the MIR level, variables map to named registers within each function:

- Variable naming convention: `_py_<python_name>` (e.g., `_py_x`, `_py_total`)
- Function parameters use the same convention: `_py_a`, `_py_b`
- Scope is managed via a stack of hashmaps (`var_scopes[0..scope_depth]`)
- Variable lookup walks from `scope_depth` down to 0

---

## 7. File Layout

| File | Lines | Purpose |
|------|-------|---------|
| `transpile_py_mir.cpp` | ~2,500 | Core MIR transpiler: AST → MIR IR, function compilation, builtin dispatch |
| `build_py_ast.cpp` | ~2,050 | AST builder: Tree-sitter Python CST → typed PyAstNode tree |
| `py_builtins.cpp` | ~1,150 | Built-in functions (29), method dispatchers (string/list/dict) |
| `py_runtime.cpp` | ~1,090 | Runtime library: operators, type coercion, collections, exceptions |
| `py_ast.hpp` | ~475 | AST node types: 60 node types, 42 operators, 41 struct definitions |
| `py_print.cpp` | ~175 | Debug AST printer |
| `py_scope.cpp` | ~150 | Scope management: LEGB resolution, symbol tables |
| `py_runtime.h` | ~120 | Runtime C API: 73 function declarations callable from JIT code |
| `py_transpiler.hpp` | ~70 | Transpiler context struct, scope types, lifecycle functions |

**Total:** ~7,780 LOC across 9 source files.

---

## Appendix: Comparison with JS Transpiler

| Aspect | JS Transpiler | Python Transpiler |
|--------|---------------|-------------------|
| Source grammar | Tree-sitter JavaScript | Tree-sitter Python |
| Entry function | `js_main()` | `py_main()` |
| User function prefix | inline naming | `pyf_<name>` prefix |
| Variables | `var`/`let`/`const` with hoisting | Assignment-based, LEGB scoping |
| Scoping | Function-scoped `var`, block-scoped `let`/`const` | LEGB: Local → Enclosing → Global → Built-in |
| Closures | Shared scope env (mutable reference semantics) | Planned (not yet implemented) |
| Type inference | Evidence-based (arithmetic patterns → int/float) | None (all values boxed) |
| Native fast path | Dual compilation for typed functions | None (all operators are runtime calls) |
| Division | JS `/` (float) | `//` floor division, `/` true division |
| Comparisons | Binary only | Chained: `a < b < c` |
| Boolean ops | Truthy/falsy | Value-returning: `a or b` returns `a` if truthy |
| Exception model | `try`/`catch`/`finally` | `try`/`except`/`finally` with `raise` |
| DOM integration | Full DOM bridge via Radiant | None |
| Method dispatch | Prototype chain | Three-tier: string → list → dict |
| Module vars | `js_module_vars[256]` indexed array | Scope-based (all variables in scope hashmap) |
| Class system | Prototype-based with constructor shape pre-alloc | Not yet implemented |
| LOC | ~22K | ~7.8K |
