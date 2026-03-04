# Lambda Transpiler — Developer Guide

The Lambda transpiler converts a Lambda AST into C source code, which is then compiled at runtime via the MIR JIT backend. This guide covers the architecture, coding conventions, and pitfalls for anyone modifying the transpiler.

---

## Pipeline Overview

```
Lambda Source
    │
    ▼
Tree-sitter Parser  (grammar.js → parser.c)
    │
    ▼
AST Builder          (build_ast.cpp → typed AST nodes)
    │
    ▼
C Transpiler         (transpile.cpp → C code string in StrBuf)
    │
    ▼
MIR JIT              (mir.c / transpile-mir.cpp → native code)
    │
    ▼
Execution            (function pointer call)
```

**Key files:**

| File | Role |
|------|------|
| `lambda/transpile.cpp` | Core transpiler — AST → C code generation |
| `lambda/transpile-mir.cpp` | MIR JIT orchestration, module linking |
| `lambda/mir.c` | Low-level MIR ctx init, compilation, func lookup |
| `lambda/build_ast.cpp` | Tree-sitter CST → typed AST |
| `lambda/ast.hpp` | AST node types, `Transpiler` struct definition |
| `lambda/transpiler.hpp` | Transpiler API, runtime and heap declarations |

---

## The `Transpiler` Struct

Defined in `ast.hpp`, `Transpiler` extends `Script` and holds all state during code generation:

```cpp
typedef struct Transpiler : Script {
    StrBuf* code_buf;           // output buffer — all generated C goes here
    Runtime* runtime;
    AstFuncNode* current_closure;  // non-null inside a closure body
    AstFuncNode* tco_func;        // non-null inside a TCO-enabled function
    bool in_tail_position;         // tail position tracking for TCO
    int pipe_inject_args;          // pipe context: extra arg count
    int temp_var_counter;          // unique counter for generated temps
    AstObjectTypeNode* method_owner;  // non-null inside method body
    // ... error tracking, namespace, assignment context, etc.
} Transpiler;
```

All code generation writes to `tp->code_buf` using `strbuf_append_*` functions.

---

## Code Generation Pattern

The transpiler recursively walks AST nodes, emitting C code as string fragments:

```cpp
// Example: transpiling an if expression
void transpile_if(Transpiler* tp, AstIfNode *if_node) {
    strbuf_append_str(tp->code_buf, "(");
    transpile_expr(tp, if_node->cond);    // emit condition
    strbuf_append_str(tp->code_buf, " ? ");
    transpile_expr(tp, if_node->then);    // emit then-branch
    strbuf_append_str(tp->code_buf, " : ");
    transpile_expr(tp, if_node->otherwise); // emit else-branch
    strbuf_append_str(tp->code_buf, ")");
}
```

**Common emit helpers:**

| Function | Purpose |
|----------|---------|
| `strbuf_append_str(buf, "...")` | Append a string literal |
| `strbuf_append_format(buf, fmt, ...)` | Printf-style append |
| `strbuf_append_char(buf, c)` | Append a single character |
| `strbuf_append_int(buf, n)` | Append an integer |
| `strbuf_append_str_n(buf, s, len)` | Append N bytes from a string |

**AST traversal entry point:** `transpile_expr()` (line ~6864) is the main dispatch — it switches on `expr_node->node_type` and calls the appropriate `transpile_*` function.

---

## Naming Conventions in Generated Code

### CRITICAL: The `_` Prefix Rule

User-defined Lambda variables are emitted with an `_` (underscore) prefix via `write_var_name()`:

```cpp
// Lambda: let count = 10
// Generated C: Item _count = i2it(10);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, ...) {
    strbuf_append_char(strbuf, '_');                        // ← underscore prefix
    strbuf_append_str_n(strbuf, asn_node->name->chars, ...);
}
```

**⚠️ Internal transpiler variables MUST NOT start with `_`** — they would collide with any user variable of the same base name.

| DO | DON'T |
|----|-------|
| `"int64_t idx = 0;"` | `"int64_t _idx = 0;"` ← collides with user `idx` |
| `"Item pipe_item = ..."` | `"Item _pipe_item = ..."` ← collides with user `pipe_item` |
| `"int tco_count = 0;"` | `"int _tco_count = 0;"` ← collides with user `tco_count` |

### Current Internal Variable Names

These are the transpiler's internal generated-code variables (no `_` prefix):

| Variable | Used In | Purpose |
|----------|---------|---------|
| `idx` | `transpile_loop_expr`, `transpile_for` | Loop counter |
| `pipe_collection`, `pipe_type`, `pipe_result`, `pipe_keys`, `pipe_i`, `pipe_index`, `pipe_item`, `pipe_len`, `key_str` | `transpile_pipe_expr` | Pipe operator iteration |
| `match_result` | `transpile_match` | Match expression result |
| `ct_value`, `ct_result` | Constrained type checks | Type narrowing temps |
| `attr_keys`, `ki` | For..at loops | Key iteration |
| `nl_src`, `nl_len`, `nidx` | Nested loops | Nested iteration |
| `offset_v`, `limited_v` | For offset/limit | Offset/limit results |
| `dec_src` | Decomposition | Destructuring source |
| `et`N, `ep`N | Error handling | Error temps (numbered) |
| `spread_src` | Object spread | Spread source |
| `fa`, `fl` | Inline function call args | Arg array + list |
| `va`, `vl` | Variadic args | Variadic array + list |
| `tco_tmp`N, `tco_count`, `tco_start` | TCO | Tail call optimization |
| `saved_pipe`, `fval` | Constraints | Constraint validation |
| `self_ptr`, `self_item`, `self_data` | Methods | Method self access |
| `env_ptr`, `cenv`, `closure_env` | Closures | Closure environment |
| `vargs` | Variadic params | Variadic parameter |
| `mod_p`, `mod_v` | Module init | Module init locals |

### Function Names

Lambda functions are emitted with `_` prefix + name + byte offset for uniqueness:

```cpp
// Lambda: fn square(x) = x * x
// Generated C: int32_t _square42(int32_t _x) { return _x * _x; }
//              ^prefix  ^offset         ^user var
```

`write_fn_name()` handles this. Imported functions get a module prefix: `m0._square42`.

### Module-Level Static Names

Module-level statics use `_` prefix — this is **safe** because they live at file scope and cannot collide with user variables which are always function-local:

- `_mod_consts`, `_mod_type_list`, `_mod_map`, `_mod_elmt`, `_mod_executed`
- `_init_mod_consts()`, `_init_mod_types()`, `_init_mod_vars()`
- `_lambda_rt`, `_type_*`, `_constraint_*`

---

## Key Transpiler Functions

### Expression Dispatch

`transpile_expr()` (~line 6864) — Central switch dispatching on `AstNodeType`:
- `AST_NODE_LITERAL` → inline constant
- `AST_NODE_IDENT` → variable reference (via `write_var_name`)
- `AST_NODE_BINARY` → `transpile_binary_expr()`
- `AST_NODE_CALL` → `transpile_call_expr()`
- `AST_NODE_IF` → `transpile_if()`
- `AST_NODE_PIPE` → `transpile_pipe_expr()`
- `AST_NODE_MATCH` → `transpile_match()`
- `AST_NODE_FOR` → `transpile_for()`
- `AST_NODE_FUNC` → `transpile_fn_expr()`
- etc.

### Function Definition

`define_func()` (~line 6148) — Generates a complete C function:
1. Writes return type (Item for closures/methods, native type otherwise)
2. Writes function name via `write_fn_name()`
3. Writes parameter list (hidden `self_ptr` for methods, `env_ptr` for closures)
4. Emits closure env extraction (`cenv = (EnvType*)env_ptr`)
5. Emits method field loading (`self_item`, `self_data`, field locals)
6. Emits TCO scaffolding if needed (`tco_count`, `tco_start` label)
7. Transpiles function body

### Pipe Operator

`transpile_pipe_expr()` (~line 2776) — Generates iteration code for `|`, `|>`, `|?`:
- Checks collection type at runtime (`pipe_type`)
- Maps/objects: iterates keys via `pipe_keys`
- Arrays/lists: iterates by index via `pipe_i`
- Scalars: applies to single item

### Closures

Closures use a captured-variable struct (`Env_f<offset>`) defined by `define_closure_env()`:
```c
// Generated for: let add = fn(x) { fn(y) { x + y } }
typedef struct Env_f100 { Item x; } Env_f100;
Item _f120(void* env_ptr, Item _y) {
    Env_f100* cenv = (Env_f100*)env_ptr;
    return cenv->x + _y;
}
```

### Tail Call Optimization (TCO)

`transpile_tail_call()` (~line 4596) converts tail-recursive calls to `goto`:
```c
// Generated for: fn fact(n, acc) = if n <= 1 then acc else fact(n-1, acc*n)
Item _fact42(Item _n, Item _acc) {
    int tco_count = 0;
    tco_start:;
    if (++tco_count > LAMBDA_TCO_MAX_ITERATIONS) { ... }
    // tail call becomes:
    { Item tco_tmp0 = _n-1; Item tco_tmp1 = _acc*_n; _n = tco_tmp0; _acc = tco_tmp1; goto tco_start; }
}
```

---

## Adding New AST Node Transpilation

1. **Define the AST node type** in `ast.hpp` (add to `AstNodeType` enum + struct)
2. **Build it** in `build_ast.cpp` (CST → AST conversion)
3. **Add a `transpile_*` function** in `transpile.cpp`:
   ```cpp
   void transpile_my_expr(Transpiler* tp, AstMyNode *node) {
       strbuf_append_str(tp->code_buf, "...");
       transpile_expr(tp, node->child);
       strbuf_append_str(tp->code_buf, "...");
   }
   ```
4. **Register in `transpile_expr()`** — add a `case AST_NODE_MY_EXPR:` branch
5. **Add tests** — both `.ls` script tests and optionally `.transpile` pattern tests

---

## Testing Transpiler Changes

### Unit Tests

```bash
make test-lambda-baseline    # Must pass 100% — 563+ tests
```

### Transpile Pattern Tests

Files like `test/lambda/tail_call.transpile` verify that specific C code patterns appear (or don't appear) in the generated output:

```json
{
    "expect": ["tco_start:", "goto tco_start"],
    "forbid": ["recursive_call"]
}
```

When renaming internal variables, **update `.transpile` files** that reference the old names.

### Debugging Generated Code

```bash
# Dump generated C code to see what the transpiler produces:
LAMBDA_DUMP_C=1 ./lambda.exe script.ls 2>generated.c

# Or check log.txt for transpilation details:
cat log.txt | grep -i transpil
```

---

## Common Pitfalls

### 1. Variable Name Collisions
Internal generated-code variables must **never** start with `_`. User variables always get `_` prefix, so any internal `_foo` will collide with a user variable named `foo`.

### 2. Statement Expressions
The transpiler heavily uses GCC statement expressions `({ ... })` for complex expressions that need local variables. These create a new scope — the last expression is the return value:
```c
({ Item pipe_collection = ...; Array* pipe_result = array(); ...; (Item)pipe_result; })
```

### 3. Boxing/Unboxing
Lambda's `Item` is a 64-bit tagged value. Conversion between native C types and `Item`:
- **Box**: `i2it(int)`, `d2it(double)`, `b2it(bool)`, `s2it(String*)`
- **Unbox**: `it2i(Item)`, `it2d(Item)`, `it2b(Item)`, `it2s(Item)`

Use `transpile_box_item()` to emit boxing code that checks the expression's type.

### 4. Redeclaration in Loops
Generated loop variables can cause C redeclaration errors if the same name appears in nested scopes. Use `({ ... })` blocks to isolate scope, or use numbered temps (`tp->temp_var_counter++`).

### 5. MIR JIT Quirks
- MIR's optimizer can mishandle SSA destruction of swap patterns inside loops. The `while_depth` counter triggers a `*(&x)=v` workaround for variable assignments inside while loops.
- MIR requires all functions called across modules to be registered via `register_dynamic_import()`.

### 6. Closure Captures
Closures capture variables **by value** as `Item`. The `CaptureInfo` linked list on `AstFuncNode` tracks what's captured. If adding new expression types that reference outer variables, ensure `find_captures()` in `build_ast.cpp` discovers them.

### 7. Module-Level Code
Module statics (`_mod_consts`, `_lambda_rt`, etc.) live at file scope in the generated C. Init functions (`_init_mod_consts`, `_init_mod_types`, `_init_mod_vars`) are called by the MIR orchestrator during module loading. Don't confuse these with function-local generated variables.
