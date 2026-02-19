# Lambda AST → MIR Direct Transpilation — Design Proposal

## 1. Overview

This proposal describes the completion of the **direct AST-to-MIR transpilation path** (`--mir` flag) as an alternative to the current **AST → C → C2MIR → MIR** pipeline. The goal is to eliminate the C code generation step and produce MIR IR directly from the Lambda AST, while reusing the existing C runtime functions and passing all 85 Lambda baseline tests.

### Current Architecture

```
Source (.ls)
  ├─ Tree-sitter ──────→ CST (TSTree)
  ├─ build_ast.cpp ────→ AST (AstScript)
  ├─ transpile.cpp ────→ C source (StrBuf)       ← ~5800 lines
  ├─ C2MIR ────────────→ MIR IR                  ← heavyweight C parser
  ├─ MIR codegen ──────→ Native code
  └─ Execute ──────────→ Item result
```

### Proposed Architecture

```
Source (.ls)
  ├─ Tree-sitter ──────→ CST (TSTree)
  ├─ build_ast.cpp ────→ AST (AstScript)
  ├─ transpile-mir.cpp ─→ MIR IR directly        ← no C parsing overhead
  ├─ MIR codegen ──────→ Native code
  └─ Execute ──────────→ Item result
```

### Why Direct MIR?

1. **Faster compilation**: Eliminates C2MIR's C parser overhead (~10–25% of total compile time for small scripts).
2. **Better control**: Direct MIR generation allows future optimizations (inlining decisions, phi-node placement, custom calling conventions) impossible through C text.
3. **Simpler debugging**: MIR IR is inspectable via `MIR_output()` without deciphering transpiled C.
4. **No C dialect limitations**: GCC statement expressions `({...})`, `__attribute__`, etc. are C2MIR-specific; direct MIR avoids these workarounds.

---

## 2. Current State of `transpile-mir.cpp`

The existing `transpile-mir.cpp` (~330 lines) is a proof-of-concept skeleton that handles only:

| Feature | Status | Notes |
|---------|--------|-------|
| Integer literals | Stub | Hardcoded `42` instead of parsing actual value |
| Float literals | Stub | Hardcoded `3.14` |
| Bool literals | Stub | Hardcoded `true` |
| Null literals | Partial | Returns `0` |
| Binary ops (+, -, *, /) | Partial | Uses native MIR arithmetic only, no runtime calls |
| Comparisons (==, !=, <, <=, >, >=) | Partial | Native MIR only |
| Unary (neg, not, pos) | Partial | Missing IS_ERROR |
| Identifiers | Stub | Returns hardcoded `10` |
| Script structure | Minimal | Single `main(Context*)` function, hardcoded return |

**Not implemented** (~95% of the language):
- Actual literal value extraction from AST/source
- String/symbol/binary/datetime/decimal constants
- Boxing/unboxing (Item tagged-pointer system)
- Runtime function calls (`fn_add`, `fn_eq`, `fn_strcat`, etc.)
- Variable declarations (let/pub/var)
- Collections (list, array, map, element)
- Control flow (if/else, match, for, while, break/continue)
- Functions (fn, pn, closures, TCO)
- Pipes and current-item (`~`, `~#`)
- Member/index access
- Import system
- Error handling (raise, `?` propagation)
- Module system (struct generation, `_init_mod_vars`)
- Constants loading (`const_s`, `const_k`, etc.)
- Type system operations (is, to, in)
- Spread expressions
- Path expressions
- Pattern matching

---

## 3. Design Principles

### 3.1 Reuse the C Runtime

The existing C runtime (`lambda-eval.cpp`, `lambda-data-runtime.cpp`, etc.) exports ~200+ functions via `import_resolver()` in `mir.c`. The direct MIR path must call these same functions — **not reimplement their logic**. This is the key to compatibility.

For example, the C transpiler generates:
```c
fn_add(i2it(left), i2it(right))  // boxed arithmetic
```

The MIR transpiler will emit equivalent MIR IR:
```
MIR_CALL fn_add(MIR_CALL i2it(left_reg), MIR_CALL i2it(right_reg))
```

The same runtime function pointers are resolved via `import_resolver()` at link time.

### 3.2 Same Calling Convention

All transpiled functions follow the same signature as the C path:
- **Main entry**: `Item main(Context* runtime)` — receives runtime context, returns boxed `Item`
- **User functions**: `Item _fn_name(params...)` — parameters are `Item` (boxed) for closures/optional params, native types for typed params
- **Unboxed variants**: `native_type _fn_name_u(native_params...)` — for type-specialized calls

### 3.3 Same Memory Model

- `Context* rt` stored as MIR global (BSS import `_lambda_rt`)
- Constants accessed via `rt->consts[index]`
- Heap allocation via `heap_calloc()`, `heap_alloc()`
- Closure environments as heap-allocated structs
- `frame_start()` / `frame_end()` lifecycle

### 3.4 Item-Centric Data Flow

All expression results flow as 64-bit `Item` values. The MIR transpiler uses `MIR_T_I64` as the primary register type (since `Item` is a 64-bit tagged union). Native types (`int64_t`, `double`) are used in unboxed contexts where type is statically known.

### 3.5 Binary ABI Compatibility with C-Transpiled Modules

**Critical requirement**: A module compiled through the C path and a module compiled through the direct MIR path must be able to call each other's functions. This means the MIR transpiler must produce functions with **identical binary signatures** to the C transpiler — same parameter order, same parameter types, same return type, same naming scheme.

---

## 4. Function Binary ABI Specification

This section defines the exact ABI contract that both transpiler paths must uphold. Any deviation breaks cross-module calls.

### 4.1 Type Mapping: Lambda → Native

The C transpiler maps Lambda types to C types via `write_type()` in `print.cpp`. The MIR transpiler must produce the identical MIR type for each:

| Lambda TypeId | C type (from `write_type`) | MIR type | Size | Notes |
|---|---|---|---|---|
| `NULL`, `ANY`, `ERROR` | `Item` | `MIR_T_I64` | 8 bytes | |
| `BOOL` | `bool` | `MIR_T_I64` | 8 bytes | C bool promoted to int in calls |
| `INT` | `int64_t` | `MIR_T_I64` | 8 bytes | Lambda Int is int56; both paths use `int64_t` to preserve full precision. |
| `INT64` | `int64_t` | `MIR_T_I64` | 8 bytes | |
| `FLOAT` | `double` | `MIR_T_D` | 8 bytes | |
| `DTIME` | `DateTime` | `MIR_T_I64` | 8 bytes | int64 struct |
| `DECIMAL` | `Decimal*` | `MIR_T_P` | 8 bytes |
| `STRING`, `BINARY` | `String*` | `MIR_T_P` | 8 bytes |
| `SYMBOL` | `Symbol*` | `MIR_T_P` | 8 bytes |
| `RANGE` | `Range*` | `MIR_T_P` | 8 bytes |
| `LIST` | `List*` | `MIR_T_P` | 8 bytes |
| `ARRAY` | `Array*` | `MIR_T_P` | 8 bytes |
| `ARRAY(Int)` | `ArrayInt*` | `MIR_T_P` | 8 bytes |
| `ARRAY(Int64)` | `ArrayInt64*` | `MIR_T_P` | 8 bytes |
| `ARRAY(Float)` | `ArrayFloat*` | `MIR_T_P` | 8 bytes |
| `MAP` | `Map*` | `MIR_T_P` | 8 bytes |
| `ELEMENT` | `Element*` | `MIR_T_P` | 8 bytes |
| `PATH` | `Path*` | `MIR_T_P` | 8 bytes |
| `FUNC` | `Function*` | `MIR_T_P` | 8 bytes |
| `TYPE` | `Type*` | `MIR_T_P` | 8 bytes |

**MIR helper**:
```cpp
static MIR_type_t type_to_mir(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_BOOL:                              return MIR_T_I64;
    case LMD_TYPE_INT:                               return MIR_T_I64;  // int56, use i64
    case LMD_TYPE_INT64:                             return MIR_T_I64;
    case LMD_TYPE_FLOAT:                             return MIR_T_D;
    case LMD_TYPE_NULL: case LMD_TYPE_ANY: case LMD_TYPE_ERROR:
    case LMD_TYPE_DTIME:                             return MIR_T_I64;
    default:                                         return MIR_T_P;
    }
}
```

### 4.2 Function Naming Contract

Both paths must use identical function name mangling. The C transpiler uses `write_fn_name()` / `write_fn_name_ex()`:

| Symbol kind | Pattern | Example |
|---|---|---|
| Named function | `_<name><byte_offset>` | `_square42` |
| Anonymous function | `_f<byte_offset>` | `_f317` |
| Unboxed variant | `_<name>_u<byte_offset>` | `_square_u42` |
| Variable | `_<name>` | `_x` |
| Imported member | `m<idx>._<name><offset>` | `m1._square42` |
| Closure environment | `Env_f<byte_offset>` | `Env_f42` |
| Module main | `main` | `main` |
| Module init vars | `_init_mod_vars` | `_init_mod_vars` |
| Module init consts | `_init_mod_consts` | `_init_mod_consts` |
| Module init types | `_init_mod_types` | `_init_mod_types` |

The MIR transpiler **must reuse** `write_fn_name()` / `write_fn_name_ex()` from `transpile.cpp` (shared via `transpiler.hpp`) to generate function names — not reimplement the naming logic.

### 4.3 Function Signature Rules

The exact function signature depends on several properties of the AST `AstFuncNode`:

#### Rule 1: Return type

```
if (is_closure || can_raise):
    return Item                    →  MIR_T_I64
else:
    return write_type(ret_type)    →  type_to_mir(ret_type)
```

#### Rule 2: Parameter list

```
Parameters are emitted left-to-right:

1. If closure:  prepend  void* _env_ptr          →  MIR_T_P
2. For each param:
   if (is_closure || param.is_optional):
       Item _paramname                            →  MIR_T_I64
   else:
       write_type(param.type) _paramname          →  type_to_mir(param.type_id)
3. If variadic:  append  List* _vargs             →  MIR_T_P
```

#### Rule 3: Unboxed variant (`_u` suffix)

Generated when: `!is_closure && !is_proc && has_typed_params(fn_node)` and the boxed version doesn't already return a native scalar.

```
Parameters: same as boxed, BUT non-optional params always use native type
Return: native type (from body type inference)
```

#### Examples

| Lambda source | C signature | MIR func signature |
|---|---|---|
| `fn square(x: Int) = x * x` | `int64_t _square42(int64_t _x)` | `func _square42(i64 _x -> i64)` |
| `fn add(a, b) = a + b` | `Item _add50(Item _a, Item _b)` | `func _add50(i64 _a, i64 _b -> i64)` |
| `fn greet(name: Str) = "Hi " + name` | `String* _greet80(String* _name)` | `func _greet80(p _name -> p)` |
| Closure `let f = fn(x) = x + y` | `Item _f120(void* _env_ptr, Item _x)` | `func _f120(p _env_ptr, i64 _x -> i64)` |
| `fn div(a, b)^Err = ...` | `Item _div200(Item _a, Item _b)` | `func _div200(i64 _a, i64 _b -> i64)` |
| `fn sum(*args) = ...` | `Item _sum300(List* _vargs)` | `func _sum300(p _vargs -> i64)` |
| `fn process(x, y: Int = 0) = ...` | `Item _process400(Item _x, Item _y)` | `func _process400(i64 _x, i64 _y -> i64)` |

### 4.4 Module Struct Layout (BSS)

When module A imports module B, a BSS blob `m<B.index>` is allocated. The layout is **sequential** with fixed field order:

```
Offset 0:   void**       consts          (8 bytes)
Offset 8:   main_func_t  _mod_main       (8 bytes)  = Item (*)(Context*)
Offset 16:  init_vars_fn _init_vars      (8 bytes)  = void (*)(void*)
Offset 24:  fn_ptr       pub_func_1      (8 bytes)  — first public function (AST order)
Offset 32:  fn_ptr       pub_func_2      (8 bytes)  — second public function
...
Offset N:   <type>       pub_var_1       (sizeof)   — first pub variable
Offset N+s: <type>       pub_var_2       (sizeof)   — second pub variable
...
```

**Critical ordering rules** (from `write_mod_struct_fields()`):
1. Fixed fields first: `consts`, `_mod_main`, `_init_vars`
2. Then all **public function pointers**, in AST traversal order (walking `AstScript->child` linked list)
3. Then all **pub variable fields**, also in AST traversal order

The sizes of pub variables match `write_type()`: `int64_t` = 8 bytes, `Item` = 8 bytes, pointers = 8 bytes, etc., with natural alignment.

**MIR implementation**: This struct is a BSS item with total size computed from the AST:

```cpp
int mod_size = 3 * sizeof(void*);  // consts + _mod_main + _init_vars
// + sizeof(void*) per public function
// + sizeof(type) per pub variable
MIR_new_bss(ctx, mod_bss_name, mod_size);
```

Field access in MIR uses `MIR_new_mem_op()` with computed byte offsets instead of C struct field names.

### 4.5 Cross-Path Module Interop

The `init_module_import()` function in `runner.cpp` populates module BSS blobs **by pointer arithmetic** — it walks the AST and writes function pointers sequentially at computed offsets. This mechanism is path-agnostic: it uses `find_func(script->jit_context, mangled_name)` to get function addresses, regardless of whether the function was compiled via C2MIR or direct MIR.

For cross-path interop to work:

1. **Same function names**: MIR path must use `write_fn_name()` to produce identical mangled names
2. **Same BSS layout**: MIR path must compute the same struct size and field offsets for `m<N>` BSS items
3. **Same well-known exports**: Each module must export `main`, `_init_mod_consts`, `_init_mod_types`, `_init_mod_vars` — with the same signatures
4. **Same `_lambda_rt` import**: Both paths resolve the shared context pointer through the same BSS import name

**Scenario**: Module `math.ls` compiled via C path, imported by `app.ls` compiled via MIR path:

```
app.ls (MIR path):
  BSS m1 { consts, _mod_main, _init_vars, _add_fn_ptr, ... }

  In main():
    CALL m1._mod_main(runtime)     // calls C-compiled math.ls main
    CALL m1._init_vars(&m1)        // copies pub vars into BSS
    ...
    CALL [m1 + 24](args...)         // calls C-compiled _add function
```

The function pointers stored in the BSS are native addresses — the caller doesn't know (or care) which path produced them.

### 4.6 Boxing/Unboxing at ABI Boundaries

When function return types or parameter types differ between caller and callee, boxing/unboxing must happen at the call site — same as the C path:

| Caller has | Callee expects | Action at call site |
|---|---|---|
| `int64_t` | `Item` | `CALL i2it(val)` |
| `double` | `Item` | `CALL push_d(val)` then `CALL d2it(ptr)` |
| `String*` | `Item` | `CALL s2it(ptr)` |
| `Item` | `int64_t` | `CALL it2i(item)` |
| `Item` | `double` | `CALL it2f(item)` |
| Container ptr | `Item` | Direct cast (high bits zero, `type_id` in `Container.type_id`) |

This boxing logic must match `transpile_box_item()` / `transpile_call_argument()` from the C transpiler exactly.

### 4.7 Closure Calling Convention

Closures are called through the runtime's `fn_call0..3()` / `fn_call()` dispatch, which extracts the function pointer and env pointer from the `Function` struct and calls:

```c
fn_ptr(env_ptr, arg1, arg2, ...)
```

Both paths must generate closure functions with this exact signature:
```
Item closure_fn(void* _env_ptr, Item _param1, Item _param2, ...)
```

The `Function` struct created by `to_closure_named()` stores the native function pointer — it doesn't care which path compiled it. So a closure created by the MIR path and called by C-compiled code (or vice versa) works as long as the signature matches.

### 4.8 ABI Compatibility Verification

To ensure ABI compatibility, add a dedicated test suite:

1. **Cross-compilation test**: Compile `module_a.ls` via C path, `module_b.ls` via MIR path. `module_b` imports and calls functions from `module_a`. Verify correct results.
2. **Reverse direction**: Compile `module_a.ls` via MIR path, `module_b.ls` via C path. Same verification.
3. **Signature fuzzing**: For each public function in baseline tests, compare the MIR function signature (via `MIR_output()`) against the C-generated prototype. They must match in parameter count, types, and return type.

---

## 5. Implementation Plan

### Phase 1: Infrastructure & Literals (Foundation)

**Goal**: Complete literal handling + correct value extraction + `_lambda_rt` import + constant loading.

#### 5.1 MIR Module Structure

Generate the same module shape as the C transpiler's `main()`:

```
module "lambda_script":
  import _lambda_rt    // extern Context* _lambda_rt (BSS import)
  import i2it          // Item i2it(int64_t)
  import d2it          // Item d2it(double*)
  import s2it          // Item s2it(String*)
  import push_d        // double* push_d(double)
  import push_l        // int64_t* push_l(int64_t)
  // ... all needed runtime imports

  func main(rt: i64 -> i64):
    // set _lambda_rt = rt
    // body expressions...
    // ret result
```

**Key tasks:**
- [ ] Import `_lambda_rt` as BSS and set it at function entry (analogous to `_lambda_rt = runtime;` in C)
- [ ] Extract actual literal values from AST source text (parse `int`, `int64`, `float`, `bool`, `null` from `ts_node_start_byte`/`ts_node_end_byte`)
- [ ] Implement constant loading: `const_s(index)` → load from `rt->consts[index]` with appropriate tagging
- [ ] Box integers via `i2it()` call or inline INT56 packing
- [ ] Box floats via `push_d()` + `d2it()` calls
- [ ] Box strings/symbols/datetimes/decimals via `const_s2it(index)` patterns
- [ ] Handle `ITEM_NULL`, `ITEM_TRUE`, `ITEM_FALSE` as known constants

#### 5.2 Literal Value Extraction

The C transpiler simply copies source text into C code (e.g., `42` becomes `42` in C). For MIR, we must parse the values:

```cpp
// In transpile_mir_primary_expr, for integer constants:
int start = ts_node_start_byte(pri_node->base.node);
int end = ts_node_end_byte(pri_node->base.node);
const char* text = source + start;
int len = end - start;
int64_t value = str_to_int64(text, len);
```

This logic can be shared with `build_ast.cpp` which already parses literals for type inference.

#### 5.3 Runtime Function Calling Convention

MIR calls to runtime functions follow this pattern:

```cpp
// Declare import prototype once per module
MIR_item_t fn_add_proto = MIR_new_proto_arr(ctx, "fn_add_p", 1, &ret_type, 2, args);
MIR_item_t fn_add_import = MIR_new_import(ctx, "fn_add");

// At call site:
MIR_append_insn(ctx, func_item, MIR_new_call_insn(ctx, 5,
    MIR_new_ref_op(ctx, fn_add_proto),
    MIR_new_ref_op(ctx, fn_add_import),
    MIR_new_reg_op(ctx, result_reg),     // output
    MIR_new_reg_op(ctx, left_reg),       // arg1
    MIR_new_reg_op(ctx, right_reg)));    // arg2
```

**Import management**: Create a `MirImports` struct to track all emitted imports/prototypes:

```cpp
struct MirImports {
    MIR_item_t fn_add_proto, fn_add_import;
    MIR_item_t fn_sub_proto, fn_sub_import;
    MIR_item_t i2it_proto, i2it_import;
    MIR_item_t push_d_proto, push_d_import;
    // ... ~200 entries
    // Use lazy initialization: create proto+import on first use
};
```

---

### Phase 2: Expressions & Variables

**Goal**: Complete all expression types + variable declarations.

#### 5.4 Binary Expressions

Mirror the C transpiler's type-dispatch logic:

| Condition | C transpiler emits | MIR equivalent |
|-----------|-------------------|----------------|
| `int + int` | `left + right` (native C) | `MIR_ADD` |
| `int + float` | `(double)left + right` | `MIR_I2D` + `MIR_DADD` |
| `any + any` | `fn_add(box(left), box(right))` | `CALL fn_add(CALL i2it(left), CALL i2it(right))` |
| `str + str` | `fn_strcat(left, right)` | `CALL fn_strcat(left_reg, right_reg)` |

The type-aware dispatch remains the same — check `bi_node->left->type->type_id` and `bi_node->right->type->type_id` at transpile time to choose native vs. boxed path.

#### 5.5 Variable Declarations

Lambda's `let` bindings in the C path become:
```c
int64_t _name = expr;    // typed
Item _name = box(expr);  // untyped
```

In MIR:
```
reg _name_i64 : i64 = <expr>       // typed
reg _name_item : i64 = CALL box()  // untyped (Item is i64)
```

**Variable lookup**: Build a `HashMap<String*, MIR_reg_t>` per function scope. On identifier reference, look up the register.

#### 5.6 Unary Expressions

| Operator | Typed (int) | Typed (float) | Generic |
|----------|-------------|---------------|---------|
| `-x` | `MIR_NEG` | `MIR_DNEG` | `fn_neg(box(x))` |
| `not x` | `MIR_EQ(x, 0)` | — | `fn_not(box(x))` |
| `^x` | — | — | `is_error(x)` (tag check) |

#### 5.7 Boxing/Unboxing Helpers

Central helper functions to emit boxing MIR instructions based on `TypeId`:

```cpp
// Emit instructions to box a typed value into Item
void emit_box(MirCtx* mc, MIR_reg_t src, TypeId type_id, MIR_reg_t* dst) {
    switch (type_id) {
    case LMD_TYPE_INT:
        emit_call(mc, "i2it", dst, src);       // Item i2it(int64_t)
        break;
    case LMD_TYPE_FLOAT:
        emit_call(mc, "push_d", &tmp, src);    // double* push_d(double)
        emit_call(mc, "d2it", dst, tmp);        // Item d2it(double*)
        break;
    case LMD_TYPE_BOOL:
        emit_call(mc, "b2it", dst, src);        // Item b2it(int64_t)
        break;
    case LMD_TYPE_STRING:
        emit_call(mc, "s2it", dst, src);        // Item s2it(String*)
        break;
    // ... other types
    default:
        // Already an Item (LMD_TYPE_ANY, container types)
        *dst = src;
    }
}

// Emit instructions to unbox Item to a typed value
void emit_unbox(MirCtx* mc, MIR_reg_t src, TypeId type_id, MIR_reg_t* dst) {
    switch (type_id) {
    case LMD_TYPE_INT:
        emit_call(mc, "it2i", dst, src);
        break;
    case LMD_TYPE_FLOAT:
        emit_call(mc, "it2f", dst, src);
        break;
    // ...
    }
}
```

---

### Phase 3: Control Flow

**Goal**: if/else, match, for, while, break/continue.

#### 5.8 If/Else Expressions

The C transpiler uses GCC ternary or statement expressions. MIR uses basic blocks with branches:

```
  // if cond then A else B
  BT cond_reg, label_then
  JMP label_else
label_then:
  <transpile A> → result_reg
  JMP label_end
label_else:
  <transpile B> → result_reg
  JMP label_end
label_end:
  // result_reg holds the value
```

For `if` expressions returning a value, both branches write to the same `result_reg`.

#### 5.9 For Expressions (Loops)

For loops require careful handling of:
- Iterator variable binding
- Collection type dispatch (Range, Array, ArrayInt, List, etc.)
- Where-clause filtering
- Result accumulation (`array_push` / `list_push`)
- Let-clauses, group/order/limit/offset

Basic structure:
```
  // for x in collection: body
  CALL fn_len(collection) → len_reg
  MOV idx_reg, 0
label_loop:
  BGE idx_reg, len_reg, label_end    // exit when idx >= len
  CALL item_at(collection, idx_reg) → x_reg
  // [where clause: BF condition, label_continue]
  <transpile body> → item_result
  CALL array_push(output, item_result)
label_continue:
  ADD idx_reg, idx_reg, 1
  JMP label_loop
label_end:
  CALL array_end(output) → result_reg
```

Type-specialized loops (Range → native counter, ArrayInt → direct `items[i]` access) follow the same patterns as the C transpiler.

#### 5.10 While Loops (Procedural)

```
label_while:
  <transpile condition> → cond_reg
  BF cond_reg, label_end
  <transpile body>
  JMP label_while
label_end:
```

`break` → `JMP label_end`, `continue` → `JMP label_while`. Requires a stack of label pairs for nested loops.

#### 5.11 Match Expressions

Generate an if-else chain from match arms:
```
  <transpile scrutinee> → scrut_reg
  // arm 1: case Int
  CALL fn_is(scrut_reg, INT_TYPE) → match_reg
  BF match_reg, label_arm2
  <transpile arm1 body> → result_reg
  JMP label_end
label_arm2:
  // arm 2: case "hello"
  CALL fn_eq(scrut_reg, literal) → match_reg
  BF match_reg, label_arm3
  ...
label_end:
```

---

### Phase 4: Functions & Closures

**Goal**: Function definitions, calls, closures, TCO.

#### 5.12 Function Definitions

Each Lambda function becomes a MIR function in the same module:

```
func _fn_square(x: i64 -> i64):
  MUL result, x, x
  RET result
```

For functions that take/return `Item`:
```
func _fn_process(x: i64 -> i64):   // Item is i64
  CALL fn_mul(x, x) → result
  RET result
```

**Dual-generation**: Like the C path, generate both boxed (`_fn_name`) and unboxed (`_fn_name_u`) variants for typed functions.

#### 5.13 Function Calls

Call dispatch mirrors the C transpiler:

| Call type | MIR emission |
|-----------|-------------|
| Known fn, typed args | Direct `CALL _fn_name_u(native_args...)` |
| Known fn, boxed args | `CALL _fn_name(boxed_args...)` |
| System function | `CALL fn_xxx(args...)` via import |
| Unknown/dynamic | `CALL fn_call(func_item, args...)` |
| Closure | `CALL fn_call(closure_item, args...)` |

**Argument boxing**: When calling boxed function with typed local values, emit boxing before call.

#### 5.14 Closures

Closure environment allocation follows the C transpiler pattern:

```
// Allocate env struct
CALL heap_calloc(env_size, TYPE_MAP) → env_ptr
// Store captured vars
MOV [env_ptr + offset_0], captured_var_0
MOV [env_ptr + offset_1], captured_var_1
// Create closure Item
CALL to_closure_named(fn_ptr, arity, env_ptr, name_str) → closure_item
```

Inside the closure body, captured variables are accessed via `env_ptr + offset`.

**Challenge**: The C transpiler defines `struct Env_fn_name { Item cap0; Item cap1; ... }` and accesses fields by name. In MIR, we compute byte offsets manually: each captured variable is an `Item` (8 bytes), so `offset = field_index * 8`.

#### 5.15 Tail Call Optimization (TCO)

The C transpiler emits `goto _tco_start` for tail-recursive calls. MIR equivalent: `JMP label_tco_start` after reassigning parameters.

```
func _fn_factorial(n: i64, acc: i64 -> i64):
label_tco_start:
  BLE n, 1, label_base
  // temp vars to avoid swap issues
  SUB t0, n, 1
  MUL t1, acc, n
  MOV n, t0
  MOV acc, t1
  JMP label_tco_start
label_base:
  RET acc
```

---

### Phase 5: Collections & Complex Types

**Goal**: List, array, map, element construction + access.

#### 5.16 Collection Construction

Collections are built via runtime calls (same as C path):

```
// [1, 2, 3]
CALL array_new(3) → arr_reg
CALL i2it(1) → item1
CALL array_push(arr_reg, item1)
CALL i2it(2) → item2
CALL array_push(arr_reg, item2)
CALL i2it(3) → item3
CALL array_push(arr_reg, item3)
CALL array_end(arr_reg) → result_reg
```

Maps/elements use `map(type_index)`, `map_set_field()` patterns.

#### 5.17 Member/Index Access

```
// obj.name → fn_member(obj, "name")
CALL fn_member(obj_reg, name_string) → result
// arr[i] → fn_index(arr, i)
CALL fn_index(arr_reg, index_item) → result
```

---

### Phase 6: Pipes, Spread & Advanced Features

#### 5.18 Pipe Expressions

**Aggregate pipe** (`data | transform`): Inject data as first argument to transform call.

**Iterating pipe** (`data | ~transform`): Generate inline loop (same as for-expression iteration pattern):

```
  CALL fn_len(data) → len
  CALL array_new(len) → output
  MOV idx, 0
label_pipe:
  BGE idx, len, label_pipe_end
  CALL item_at(data, idx) → current_item
  <transpile transform with current_item bound to ~> → mapped
  CALL array_push(output, mapped)
  ADD idx, idx, 1
  JMP label_pipe
label_pipe_end:
  CALL array_end(output) → result
```

#### 5.19 Spread Expressions

`*expr` → `CALL item_spread(expr)` to mark spreadable, then `list_push_spread()` / `array_push_spread()` handles flattening at collection build time.

#### 5.20 Error Handling

`raise expr` → box expr, return it as function result (error propagation via return value).

`?` propagation: Generate inline error check after call:
```
  CALL fn_xxx(...) → result
  CALL get_type_id(result) → tid
  EQ is_err, tid, LMD_TYPE_ERROR
  BT is_err, label_propagate
  JMP label_continue
label_propagate:
  RET result   // propagate error up
label_continue:
  // continue with result
```

---

### Phase 7: Import System & Modules

#### 5.21 Module Imports

Each imported module is compiled as a separate MIR module. The importing module accesses exported functions/vars via a `Mod` struct (BSS).

```
// Importing module declares BSS for module struct
bss m0 (sizeof(ModN))

// In main():
CALL m0._mod_main(runtime)        // initialize module
CALL m0._init_vars(&m0)           // copy pub vars
// Access: m0.pub_var, m0.fn_ptr
```

This requires:
- Module compilation ordering (same as current `load_script` handles)
- BSS struct layout matching between modules
- Constants/type_list initialization per module

---

## 6. MIR Transpiler Context Structure

```cpp
struct MirTranspiler {
    // Input
    AstScript* script;
    const char* source;           // source text for literal extraction
    Runtime* runtime;
    bool is_main;
    int script_index;

    // MIR context
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Imports (lazy-initialized proto + import pairs)
    HashMap* import_cache;        // name → MirImportEntry { proto, import }

    // Variable tracking
    HashMap* global_vars;         // name → MIR_item_t (BSS)
    HashMap* local_vars;          // name → MIR_reg_t     (per-function scope stack)
    ArrayList* scope_stack;       // stack of HashMaps for nested scopes

    // Control flow
    ArrayList* loop_labels;       // stack of (continue_label, break_label) pairs

    // Closure support
    AstFuncNode* current_closure;
    int env_field_counter;

    // TCO
    AstFuncNode* tco_func;
    MIR_label_t tco_label;

    // Counters
    int reg_counter;
    int label_counter;
    int temp_counter;

    // Debug info
    hashmap* func_name_map;       // MIR name → Lambda name for stack traces
};
```

---

## 7. Implementation Order & Test Strategy

### Incremental Milestone Plan

Each milestone should pass an increasing subset of the 85 baseline tests:

| Milestone | Features | Target Tests |
|-----------|----------|-------------|
| **M1** | Literals (int, float, bool, null, string), boxing, `_lambda_rt` setup, constant loading | `expr` (partial), basic literal scripts |
| **M2** | Binary/unary expressions with type dispatch, native arithmetic, boxed fallback | `expr`, `comp_expr`, `chained_comparisons` |
| **M3** | Let/pub declarations, identifier lookup, scoping | `expr_stam`, `func_param` |
| **M4** | If/else expressions, ternary, match | `comp_expr_edge`, `constrained_type` |
| **M5** | Function definitions (fn), calls, forward references | `func`, `func_param`, `func_param2`, `forward_ref` |
| **M6** | For expressions, while, break/continue, ranges | `for_clauses_test`, `for_decompose` |
| **M7** | Arrays, lists, collection operations | `array_float`, `box_unbox` |
| **M8** | Maps, elements, member/index access | Various |
| **M9** | Closures, first-class functions, TCO | `closure`, `closure_advanced`, `first_class_fn` |
| **M10** | Pipes, spread, error handling | `error_handling`, `error_propagation` |
| **M11** | String operations, patterns | `Lambda_Type_String` tests |
| **M12** | Imports, module system | `import` and multi-file tests |
| **M13** | Procedural mode (pn, main(), var, assignment) | `test/lambda/proc/*.ls` tests |
| **M14** | Remaining edge cases, full baseline pass | All 85 baseline tests |

### Testing Approach

1. **Reuse existing test infrastructure**: `test_lambda_gtest.exe` already runs `./lambda.exe <script>` and compares output. Add a parallel test suite `test_lambda_mir_gtest.exe` that runs `./lambda.exe --mir <script>` with the same expected outputs.

2. **Upgrade `test_mir_gtest.cpp`**: Replace current smoke tests with actual value-correctness checks. Use the same `*.txt` expected-output files as the C2MIR path.

3. **Add Makefile target**: `make test-lambda-mir-baseline` to run all baseline tests through the `--mir` path.

4. **Differential testing**: For each test, run both paths and diff the outputs to catch divergence.

---

## 8. Key Technical Challenges

### 8.1 GCC Statement Expressions

The C transpiler heavily uses `({...})` (GCC statement expressions) for inline value-producing blocks. MIR doesn't have this concept. Instead, every "expression block" must be linearized into a sequence of instructions that writes to a result register.

**Strategy**: Each `transpile_mir_*` function takes a `MIR_reg_t* result_reg` output parameter. Complex expressions write to this register through nested calls.

### 8.2 Type-Aware Code Generation

The C transpiler checks `node->type->type_id` at transpile time to choose between native C operators and boxed runtime calls. The MIR transpiler must replicate this same logic — the type information is available in the AST after type inference.

### 8.3 Constant Pool Access

Constants (strings, symbols, datetimes, decimals) are stored in `Script.const_list` and accessed at runtime via `rt->consts[index]`. In the C path:
```c
const_s(3)   →  ((String*)rt->consts[3])
const_s2it(3) → s2it(rt->consts[3])
```

In MIR:
```
// Load rt->consts pointer
MOV consts_ptr, [rt + CONSTS_OFFSET]
// Load const at index
MOV str_ptr, [consts_ptr + index * 8]
// Tag as string Item
CALL s2it(str_ptr) → result
```

Offsets must match the `Context` struct layout. Use `offsetof(Context, consts)` computed at transpile time.

### 8.4 Import Resolver Completeness

The `func_list[]` in `mir.c` already maps ~200+ function names to native addresses. The direct MIR path uses the same `import_resolver` — no changes needed. However, any new runtime functions added must be registered in `func_list[]`.

### 8.5 Closure Environment Layout

C transpiler generates named structs. MIR transpiler must compute offsets manually:
```
Env layout: [Item cap0 | Item cap1 | Item cap2 | ...]
             offset 0    offset 8    offset 16
```

All captured variables are stored as `Item` (8 bytes each). The MIR transpiler accesses them via pointer arithmetic: `MIR_MEM_OP(env_ptr, cap_index * 8, MIR_T_I64)`.

### 8.6 Module-Level Static Variables

The C transpiler uses `static` globals for module state (`_mod_consts`, `_mod_type_list`, `_mod_executed`). In MIR, these become BSS items:
```
bss _mod_consts (8)      // void**
bss _mod_type_list (8)   // void*
bss _mod_executed (4)    // int
```

---

## 9. CLI Integration

The `--mir` flag already exists in `main.cpp` and routes to `run_script_mir()`. No new CLI argument is needed. Current handling:

```
./lambda.exe --mir script.ls          # Functional script via direct MIR
./lambda.exe run --mir script.ls      # Procedural script via direct MIR
```

Both paths call `run_script_mir()` in `transpile-mir.cpp`, which:
1. Calls `load_script()` (parse + AST build only, **skipping C transpilation**)
2. Creates MIR context via `jit_init()`
3. Calls `transpile_mir_ast()` to generate MIR IR directly
4. Links and generates native code
5. Executes via `execute_script_and_create_output()`

**Note**: The current `load_script()` performs full C transpilation + C2MIR compilation. For the `--mir` path, we need a lighter-weight loader that only does parsing + AST building. Options:

- **Option A (Recommended)**: Add a `load_script_ast_only()` function that parses + builds AST but skips `transpile_ast_root()` and C2MIR. This is cleanest.
- **Option B**: Add a flag to `load_script()` to skip transpilation when `--mir` is set.

`run_script_mir()` should also be updated to properly handle imports by recursively loading imported modules through the MIR path.

---

## 10. File Organization

```
lambda/
  transpile-mir.cpp      // Main MIR transpiler (expand from ~330 → ~3000+ lines)
  mir-imports.h           // Import/prototype management helpers (new)
  mir-imports.cpp         // Lazy import creation (new)
  mir.c                   // JIT utilities (existing, no changes needed)
  transpiler.hpp          // Shared types (add MirTranspiler struct)
```

The implementation should remain in a single primary file (`transpile-mir.cpp`) with helpers factored into `mir-imports.*`. This parallels the C transpiler's organization (single `transpile.cpp`).

---

## 11. Estimated Effort

| Phase | Scope | Estimate |
|-------|-------|----------|
| M1–M2 | Literals + expressions | 3–4 days |
| M3–M4 | Variables + control flow | 3–4 days |
| M5–M6 | Functions + loops | 4–5 days |
| M7–M8 | Collections | 3–4 days |
| M9 | Closures + TCO | 3–4 days |
| M10–M11 | Pipes + errors + strings | 3–4 days |
| M12–M13 | Imports + procedural | 4–5 days |
| M14 | Polish + full baseline pass | 2–3 days |
| **Total** | | **~25–33 days** |

---

## 12. Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| MIR API surface differences from C2MIR | MIR is well-documented; direct IR is simpler than C parsing |
| Closure env layout mismatch | Use consistent `Item` (8-byte) layout; verify with unit tests |
| Module import ordering | Reuse existing `load_script` dependency resolution |
| Performance regression | Benchmark both paths; MIR direct should be >= C2MIR performance |
| Boxing/unboxing correctness | Port the C transpiler's type-dispatch logic verbatim; test with `box_unbox*.ls` |
| ABI incompatibility between C/MIR modules | Reuse `write_fn_name()` and `write_type()` from transpile.cpp; share `type_to_mir()` mapping; add cross-compilation integration tests |
| Module struct layout divergence | Compute BSS sizes using same AST traversal order as `write_mod_struct_fields()`; add sizeof assertions |

---

## 13. Success Criteria

1. `./lambda.exe --mir script.ls` produces identical output to `./lambda.exe script.ls` for all 85 baseline tests.
2. `make test-lambda-mir-baseline` passes 100%.
3. No new runtime functions needed — full reuse of existing C runtime.
4. Compilation speed is >= C2MIR path (measured on baseline suite).
5. Stack traces work correctly via `build_debug_info_table()` + `func_name_map`.
6. **Binary ABI compatibility**: A module compiled via the C path can be imported and called by a module compiled via the MIR path, and vice versa, producing correct results.
7. **Cross-path import tests** pass: dedicated tests verify mixed C/MIR module compilation.
