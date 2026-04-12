# JavaScript Transpiler v4: Direct MIR Generation

## 1. Overview

This proposal describes refactoring the JavaScript transpiler from generating **C code** (fed to C2MIR) to generating **MIR IR directly**, following the same approach the Lambda transpiler took when moving from `transpile.cpp` (C codegen) to `transpile-mir.cpp` (direct MIR).

### Current JS Architecture (v3)

```
JavaScript source (.js)
  ├─ Tree-sitter (JS grammar) ──→ CST (TSTree)
  ├─ build_js_ast.cpp ──────────→ JS AST (JsAstNode tree)
  ├─ transpile_js.cpp ──────────→ C source text (StrBuf)     ← ~1750 lines
  ├─ C2MIR ─────────────────────→ MIR IR                     ← heavyweight C parser
  ├─ MIR codegen ───────────────→ Native code
  └─ Execute js_main(ctx) ──────→ Item result
```

### Proposed Architecture (v4)

```
JavaScript source (.js)
  ├─ Tree-sitter (JS grammar) ──→ CST (TSTree)
  ├─ build_js_ast.cpp ──────────→ JS AST (JsAstNode tree)    ← unchanged
  ├─ transpile_js_mir.cpp ──────→ MIR IR directly             ← new file
  ├─ MIR codegen ───────────────→ Native code
  └─ Execute js_main(ctx) ──────→ Item result
```

### Core Principles

1. **Run under the same Lambda runtime** — all `js_*` and `fn_*` runtime functions remain callable via `import_resolver()` in `mir.c`. The GC, heap, name pool, nursery all work identically.
2. **Align with Lambda data model** — JavaScript values are represented as Lambda `Item` tagged pointers. Boxing/unboxing reuses the same `i2it`/`s2it`/`d2it`/`push_d`/`push_l` functions and inline MIR equivalents.
3. **Reuse as much Lambda code as possible** — the `MirTranspiler` infrastructure (import cache, scope management, emit helpers, box/unbox emitters) from `transpile-mir.cpp` serves as a proven template. The JS MIR transpiler should extract and share these utilities.

### Why This Change?

| Aspect | C codegen (current) | Direct MIR (proposed) |
|--------|---------------------|----------------------|
| Compilation speed | C2MIR parses ~10KB of generated C text including the embedded `lambda.h` header | Eliminates C parsing entirely; MIR IR is appended incrementally |
| Code quality | Relies on GCC statement expressions `({...})` for expression-returning blocks | MIR registers + labels cleanly express temporaries and control flow |
| Debuggability | Must read generated C in `temp/_transpiled_js.c` | MIR text dump via `MIR_output()` is structured IR |
| Maintainability | String concatenation to build C code — fragile edge cases | Structured API calls to build MIR — type-checked at C++ compile time |
| Optimization potential | C2MIR applies generic optimizations | Future: custom JS-specific optimizations at MIR level |

---

## 2. Design: JsMirTranspiler Context

Following the Lambda `MirTranspiler` pattern, introduce a `JsMirTranspiler` struct:

```cpp
struct JsMirTranspiler {
    // Input
    JsAstNode* ast_root;
    const char* source;
    Runtime* runtime;

    // MIR context (same API as Lambda MIR transpiler)
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache: name -> { proto, import }
    // Reuse the same ImportCacheEntry/hashmap pattern from transpile-mir.cpp
    struct hashmap* import_cache;

    // Local JS functions: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Variable scopes: stack of hashmaps, name -> JsMirVarEntry
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop label stack for break/continue
    struct { MIR_label_t continue_label; MIR_label_t break_label; } loop_stack[32];
    int loop_depth;

    // Counters
    int reg_counter;
    int label_counter;
    int function_counter;

    // Runtime pointer register (loaded at js_main entry)
    MIR_reg_t rt_reg;

    // JS scope integration
    JsScope* current_scope;
    JsScope* global_scope;
    Pool* ast_pool;
    NamePool* name_pool;
};

struct JsMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;   // always MIR_T_I64 for JS (all values are boxed Items)
};
```

### Key difference from Lambda's `MirTranspiler`

JavaScript is **dynamically typed** — every variable holds a boxed `Item`. There is no type-aware unboxing at compile time. This simplifies the transpiler significantly:

- **No `TypeId` tracking per variable** — every register is `MIR_T_I64` (boxed Item).
- **No native arithmetic fast paths** — all operations dispatch through `js_add`, `js_subtract`, etc. which handle type coercion at runtime.
- **No `emit_unbox` calls** — values stay boxed throughout.
- **Boxing is only needed for literals** — `emit_box_int` for integer literals, `emit_box_float` for float literals, `emit_box_string` for string literal pointers.

This is in contrast to the Lambda MIR transpiler which aggressively tracks native types and emits `MIR_ADD`/`MIR_DADD` for known int/float operands.

---

## 3. Shared Infrastructure from Lambda MIR Transpiler

The following components from `transpile-mir.cpp` can be directly reused or adapted:

### 3.1 Import Management (reuse as-is)

```cpp
// Same pattern: lazy proto + import creation
static MirImportEntry* ensure_import(JsMirTranspiler* jmt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres);

// Convenience wrappers
static MirImportEntry* ensure_import_ii_i(JsMirTranspiler* jmt, const char* name);  // Item(Item, Item)
static MirImportEntry* ensure_import_i_i(JsMirTranspiler* jmt, const char* name);   // Item(Item)
```

All `js_*` runtime functions are already registered in `mir.c`'s `func_list[]` and resolved by `import_resolver()`. The direct MIR path calls them the same way Lambda's MIR transpiler calls `fn_add`, `fn_sub`, etc.

### 3.2 Emit Helpers (reuse as-is)

```cpp
static MIR_reg_t new_reg(JsMirTranspiler* jmt, const char* prefix, MIR_type_t type);
static MIR_label_t new_label(JsMirTranspiler* jmt);
static void emit_insn(JsMirTranspiler* jmt, MIR_insn_t insn);
static void emit_label(JsMirTranspiler* jmt, MIR_label_t label);

// Call helpers
static MIR_reg_t emit_call_0(JsMirTranspiler* jmt, const char* fn, MIR_type_t ret);
static MIR_reg_t emit_call_1(JsMirTranspiler* jmt, const char* fn, MIR_type_t ret,
                              MIR_type_t a1t, MIR_op_t a1);
static MIR_reg_t emit_call_2(JsMirTranspiler* jmt, const char* fn, MIR_type_t ret,
                              MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2);
static MIR_reg_t emit_call_3(JsMirTranspiler* jmt, const char* fn, MIR_type_t ret,
                              MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
                              MIR_type_t a3t, MIR_op_t a3);
static void emit_call_void_1(JsMirTranspiler* jmt, const char* fn,
                              MIR_type_t a1t, MIR_op_t a1);
```

### 3.3 Boxing Helpers (reuse subset)

Since JS uses all-boxed `Item` values, only a subset of boxing is needed:

| Lambda MIR Transpiler | JS MIR Transpiler | Notes |
|----------------------|-------------------|-------|
| `emit_box_int` | **Reuse** | Integer literal → `Item` |
| `emit_box_float` | **Reuse** (call `push_d`) | Float literal → `Item` |
| `emit_box_bool` | **Reuse** | Boolean literal → `Item` |
| `emit_box_string` | **Reuse** | String pointer → `Item` |
| `emit_box_container` | **Reuse** | Array/Map/Object pointer → `Item` |
| `emit_box_int64` | Not needed | JS doesn't have int64 type |
| `emit_box_dtime` | Not needed | JS doesn't have datetime type |
| `emit_box_decimal` | Not needed | JS doesn't have decimal type |
| `emit_box_symbol` | Not needed | JS doesn't have symbol type (in Lambda sense) |
| `emit_unbox_*` | Not needed | JS keeps values boxed |

### 3.4 Scope Management (adapt from Lambda)

```cpp
static void push_scope(JsMirTranspiler* jmt);
static void pop_scope(JsMirTranspiler* jmt);
static void set_var(JsMirTranspiler* jmt, const char* name, MIR_reg_t reg);
static JsMirVarEntry* find_var(JsMirTranspiler* jmt, const char* name);
```

Simpler than Lambda's version: no `TypeId` or `env_offset` for closures (initially).

---

## 4. Expression Transpilation

### 4.1 Literals

```cpp
static MIR_reg_t transpile_js_literal_mir(JsMirTranspiler* jmt, JsLiteralNode* lit) {
    switch (lit->literal_type) {
    case JS_LITERAL_NUMBER: {
        double val = lit->value.number_value;
        if (val == (double)(int64_t)val && val >= INT32_MIN && val <= INT32_MAX) {
            // Integer range — use inline box
            MIR_reg_t r = new_reg(jmt, "int", MIR_T_I64);
            emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
                MIR_new_reg_op(jmt->ctx, r),
                MIR_new_int_op(jmt->ctx, (int64_t)val)));
            return emit_box_int(jmt, r);  // reuse Lambda's inline i2it
        } else {
            // Float — allocate via push_d
            MIR_reg_t d = new_reg(jmt, "flt", MIR_T_D);
            emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_DMOV,
                MIR_new_reg_op(jmt->ctx, d),
                MIR_new_double_op(jmt->ctx, val)));
            return emit_call_1(jmt, "push_d", MIR_T_I64, MIR_T_D,
                MIR_new_reg_op(jmt->ctx, d));
        }
    }
    case JS_LITERAL_STRING: {
        // Load String* pointer from name pool (compiled into binary)
        String* str = lit->value.string_value;
        MIR_reg_t ptr = new_reg(jmt, "str", MIR_T_I64);
        emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
            MIR_new_reg_op(jmt->ctx, ptr),
            MIR_new_int_op(jmt->ctx, (int64_t)(uintptr_t)str)));
        return emit_box_string(jmt, ptr);  // reuse Lambda's inline s2it
    }
    case JS_LITERAL_BOOLEAN: {
        MIR_reg_t r = new_reg(jmt, "bool", MIR_T_I64);
        emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
            MIR_new_reg_op(jmt->ctx, r),
            MIR_new_int_op(jmt->ctx, lit->value.boolean_value ? 1 : 0)));
        return emit_box_bool(jmt, r);
    }
    case JS_LITERAL_NULL:
    case JS_LITERAL_UNDEFINED: {
        MIR_reg_t r = new_reg(jmt, "null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
            MIR_new_reg_op(jmt->ctx, r),
            MIR_new_int_op(jmt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    }
}
```

### 4.2 Identifiers

```cpp
static MIR_reg_t transpile_js_identifier_mir(JsMirTranspiler* jmt, JsIdentifierNode* id) {
    JsMirVarEntry* var = find_var(jmt, id->name->chars);
    if (var) return var->reg;

    // Undefined variable → ITEM_NULL (or could be a global lookup)
    log_error("js-mir: undefined variable: %.*s", (int)id->name->len, id->name->chars);
    MIR_reg_t r = new_reg(jmt, "undef", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_reg_op(jmt->ctx, r),
        MIR_new_int_op(jmt->ctx, (int64_t)NULL_VAL)));
    return r;
}
```

### 4.3 Binary Expressions

The current C codegen emits `js_add(left, right)` as string concatenation. Direct MIR emits the same call:

```cpp
static MIR_reg_t transpile_js_binary_mir(JsMirTranspiler* jmt, JsBinaryNode* bin) {
    MIR_reg_t left = transpile_js_expr_mir(jmt, bin->left);
    MIR_reg_t right = transpile_js_expr_mir(jmt, bin->right);

    MIR_op_t l_op = MIR_new_reg_op(jmt->ctx, left);
    MIR_op_t r_op = MIR_new_reg_op(jmt->ctx, right);

    switch (bin->op) {
    case JS_OP_ADD:
        return emit_call_2(jmt, "js_add", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    case JS_OP_SUB:
        return emit_call_2(jmt, "js_subtract", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    case JS_OP_MUL:
        return emit_call_2(jmt, "js_multiply", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    case JS_OP_DIV:
        return emit_call_2(jmt, "js_divide", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    case JS_OP_MOD:
        return emit_call_2(jmt, "js_modulo", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    case JS_OP_EXP:
        return emit_call_2(jmt, "js_power", MIR_T_I64, MIR_T_I64, l_op, MIR_T_I64, r_op);
    // ... (all other operators follow the same pattern)
    case JS_OP_AND:
        // Short-circuit: evaluate left; if falsy, skip right
        return transpile_js_logical_and_mir(jmt, bin);
    case JS_OP_OR:
        return transpile_js_logical_or_mir(jmt, bin);
    default:
        log_error("js-mir: unknown binary op %d", bin->op);
        return emit_null_item(jmt);
    }
}
```

**Short-circuit logical operators** now use proper MIR labels instead of relying on C's `&&`/`||` semantics (which the C codegen couldn't easily use either — it was calling `js_logical_and` as a function):

```cpp
static MIR_reg_t transpile_js_logical_and_mir(JsMirTranspiler* jmt, JsBinaryNode* bin) {
    MIR_reg_t result = new_reg(jmt, "and", MIR_T_I64);
    MIR_label_t l_skip = new_label(jmt);
    MIR_label_t l_end = new_label(jmt);

    // Evaluate left
    MIR_reg_t left = transpile_js_expr_mir(jmt, bin->left);
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_reg_op(jmt->ctx, result), MIR_new_reg_op(jmt->ctx, left)));

    // If left is falsy, short-circuit (result = left)
    MIR_reg_t truthy = emit_call_1(jmt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(jmt->ctx, left));
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_BF,
        MIR_new_label_op(jmt->ctx, l_end), MIR_new_reg_op(jmt->ctx, truthy)));

    // Left is truthy → evaluate right, result = right
    MIR_reg_t right = transpile_js_expr_mir(jmt, bin->right);
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_reg_op(jmt->ctx, result), MIR_new_reg_op(jmt->ctx, right)));

    emit_label(jmt, l_end);
    return result;
}
```

This is an immediate semantic improvement over the current C codegen, which calls `js_logical_and(left, right)` eagerly evaluating both sides.

### 4.4 Unary Expressions

Same pattern — call runtime functions with MIR call instructions:

```cpp
static MIR_reg_t transpile_js_unary_mir(JsMirTranspiler* jmt, JsUnaryNode* un) {
    MIR_reg_t operand = transpile_js_expr_mir(jmt, un->operand);
    MIR_op_t op = MIR_new_reg_op(jmt->ctx, operand);

    switch (un->op) {
    case JS_OP_NOT:
        return emit_call_1(jmt, "js_logical_not", MIR_T_I64, MIR_T_I64, op);
    case JS_OP_BIT_NOT:
        return emit_call_1(jmt, "js_bitwise_not", MIR_T_I64, MIR_T_I64, op);
    case JS_OP_TYPEOF:
        return emit_call_1(jmt, "js_typeof", MIR_T_I64, MIR_T_I64, op);
    case JS_OP_PLUS: case JS_OP_ADD:
        return emit_call_1(jmt, "js_unary_plus", MIR_T_I64, MIR_T_I64, op);
    case JS_OP_MINUS: case JS_OP_SUB:
        return emit_call_1(jmt, "js_unary_minus", MIR_T_I64, MIR_T_I64, op);
    case JS_OP_INCREMENT: {
        // ++x: x = js_add(x, i2it(1))
        if (un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            JsMirVarEntry* var = find_var(jmt, id->name->chars);
            if (var) {
                MIR_reg_t one = emit_box_int_const(jmt, 1);
                MIR_reg_t result = emit_call_2(jmt, "js_add", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(jmt->ctx, var->reg),
                    MIR_T_I64, MIR_new_reg_op(jmt->ctx, one));
                emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
                    MIR_new_reg_op(jmt->ctx, var->reg),
                    MIR_new_reg_op(jmt->ctx, result)));
                return result;
            }
        }
        // fallback: non-lvalue increment
        MIR_reg_t one = emit_box_int_const(jmt, 1);
        return emit_call_2(jmt, "js_add", MIR_T_I64,
            MIR_T_I64, op, MIR_T_I64, MIR_new_reg_op(jmt->ctx, one));
    }
    // JS_OP_DECREMENT: analogous
    default:
        return emit_null_item(jmt);
    }
}
```

---

## 5. Statement Transpilation

### 5.1 Variable Declarations

```cpp
static void transpile_js_var_decl_mir(JsMirTranspiler* jmt, JsVariableDeclarationNode* decl) {
    JsAstNode* declarator = decl->declarations;
    while (declarator) {
        if (declarator->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)declarator;
            if (vd->id && vd->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)vd->id;
                MIR_reg_t reg = new_reg(jmt, id->name->chars, MIR_T_I64);

                if (vd->init) {
                    MIR_reg_t val = transpile_js_expr_mir(jmt, vd->init);
                    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
                        MIR_new_reg_op(jmt->ctx, reg),
                        MIR_new_reg_op(jmt->ctx, val)));
                } else {
                    // Uninitialized → undefined (ITEM_NULL)
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
                        MIR_new_reg_op(jmt->ctx, reg),
                        MIR_new_int_op(jmt->ctx, (int64_t)NULL_VAL)));
                }

                set_var(jmt, id->name->chars, reg);
            }
        }
        declarator = declarator->next;
    }
}
```

### 5.2 If Statements

```cpp
static void transpile_js_if_mir(JsMirTranspiler* jmt, JsIfNode* if_node) {
    MIR_reg_t test = transpile_js_expr_mir(jmt, if_node->test);
    MIR_reg_t truthy = emit_call_1(jmt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(jmt->ctx, test));

    MIR_label_t l_else = new_label(jmt);
    MIR_label_t l_end = new_label(jmt);

    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_BF,
        MIR_new_label_op(jmt->ctx, l_else),
        MIR_new_reg_op(jmt->ctx, truthy)));

    // Consequent
    transpile_js_stmt_mir(jmt, if_node->consequent);
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_JMP,
        MIR_new_label_op(jmt->ctx, l_end)));

    // Alternate
    emit_label(jmt, l_else);
    if (if_node->alternate) {
        transpile_js_stmt_mir(jmt, if_node->alternate);
    }
    emit_label(jmt, l_end);
}
```

### 5.3 While / For Loops

```cpp
static void transpile_js_while_mir(JsMirTranspiler* jmt, JsWhileNode* while_node) {
    MIR_label_t l_continue = new_label(jmt);  // loop head
    MIR_label_t l_break = new_label(jmt);     // after loop

    // Push loop labels for break/continue
    jmt->loop_stack[jmt->loop_depth].continue_label = l_continue;
    jmt->loop_stack[jmt->loop_depth].break_label = l_break;
    jmt->loop_depth++;

    emit_label(jmt, l_continue);

    // Test condition
    MIR_reg_t test = transpile_js_expr_mir(jmt, while_node->test);
    MIR_reg_t truthy = emit_call_1(jmt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(jmt->ctx, test));
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_BF,
        MIR_new_label_op(jmt->ctx, l_break),
        MIR_new_reg_op(jmt->ctx, truthy)));

    // Body
    transpile_js_stmt_mir(jmt, while_node->body);

    // Jump back to test
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_JMP,
        MIR_new_label_op(jmt->ctx, l_continue)));

    emit_label(jmt, l_break);
    jmt->loop_depth--;
}

static void transpile_js_for_mir(JsMirTranspiler* jmt, JsForNode* for_node) {
    push_scope(jmt);

    // Init
    if (for_node->init) transpile_js_stmt_mir(jmt, for_node->init);

    MIR_label_t l_test = new_label(jmt);
    MIR_label_t l_update = new_label(jmt);  // continue target
    MIR_label_t l_break = new_label(jmt);

    jmt->loop_stack[jmt->loop_depth].continue_label = l_update;
    jmt->loop_stack[jmt->loop_depth].break_label = l_break;
    jmt->loop_depth++;

    // Test
    emit_label(jmt, l_test);
    if (for_node->test) {
        MIR_reg_t test = transpile_js_expr_mir(jmt, for_node->test);
        MIR_reg_t truthy = emit_call_1(jmt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, test));
        emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_BF,
            MIR_new_label_op(jmt->ctx, l_break),
            MIR_new_reg_op(jmt->ctx, truthy)));
    }

    // Body
    transpile_js_stmt_mir(jmt, for_node->body);

    // Update
    emit_label(jmt, l_update);
    if (for_node->update) transpile_js_expr_mir(jmt, for_node->update);

    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_JMP,
        MIR_new_label_op(jmt->ctx, l_test)));

    emit_label(jmt, l_break);
    jmt->loop_depth--;
    pop_scope(jmt);
}
```

### 5.4 Return / Break / Continue

```cpp
static void transpile_js_return_mir(JsMirTranspiler* jmt, JsReturnNode* ret) {
    MIR_reg_t val;
    if (ret->argument) {
        val = transpile_js_expr_mir(jmt, ret->argument);
    } else {
        val = emit_null_item(jmt);
    }
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_RET,
        MIR_new_reg_op(jmt->ctx, val)));
}

// break → JMP to loop_stack[loop_depth-1].break_label
// continue → JMP to loop_stack[loop_depth-1].continue_label
```

---

## 6. Function Declarations and Calls

### 6.1 Function Declarations

Similar to Lambda's `transpile_func_def`:

```cpp
static void transpile_js_func_def_mir(JsMirTranspiler* jmt, JsFunctionNode* func) {
    // Save current function context
    MIR_item_t saved_func_item = jmt->current_func_item;
    MIR_func_t saved_func = jmt->current_func;
    int saved_reg_counter = jmt->reg_counter;

    // Count params
    int param_count = 0;
    JsAstNode* p = func->params;
    while (p) { param_count++; p = p->next; }

    // Create MIR function: Item _js_funcname_offset(Item param1, Item param2, ...)
    MIR_var_t* params = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
    p = func->params;
    for (int i = 0; i < param_count; i++) {
        char pname[64];
        if (p->node_type == JS_AST_NODE_IDENTIFIER)
            snprintf(pname, sizeof(pname), "%s", ((JsIdentifierNode*)p)->name->chars);
        else
            snprintf(pname, sizeof(pname), "p%d", i);
        params[i] = {MIR_T_I64, strdup(pname), 0};
        p = p->next;
    }

    // Generate function name
    char fn_name[128];
    if (func->name)
        snprintf(fn_name, sizeof(fn_name), "_js_%s_%d",
                 func->name->chars, ts_node_start_byte(func->base.node));
    else
        snprintf(fn_name, sizeof(fn_name), "_js_anon_%d", jmt->function_counter++);

    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t func_item = MIR_new_func_arr(jmt->ctx, fn_name, 1, &ret_type,
                                              param_count, params);
    jmt->current_func_item = func_item;
    jmt->current_func = MIR_get_item_func(jmt->ctx, func_item);
    jmt->reg_counter = 0;

    // Register in local_funcs
    register_local_func(jmt, fn_name, func_item);

    // Push scope for function body
    push_scope(jmt);

    // Bind parameters to registers
    p = func->params;
    for (int i = 0; i < param_count; i++) {
        if (p->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)p;
            MIR_reg_t preg = MIR_reg(jmt->ctx, id->name->chars, jmt->current_func);
            set_var(jmt, id->name->chars, preg);
        }
        p = p->next;
    }

    // Transpile body
    if (func->body) {
        if (func->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* block = (JsBlockNode*)func->body;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_stmt_mir(jmt, stmt);
                stmt = stmt->next;
            }
        } else {
            // Arrow function with expression body: return the expression
            MIR_reg_t val = transpile_js_expr_mir(jmt, func->body);
            emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_RET,
                MIR_new_reg_op(jmt->ctx, val)));
        }
    }

    // Default return ITEM_NULL (if no explicit return)
    MIR_reg_t null_ret = emit_null_item(jmt);
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_RET,
        MIR_new_reg_op(jmt->ctx, null_ret)));

    pop_scope(jmt);
    MIR_finish_func(jmt->ctx);

    // Restore context
    jmt->current_func_item = saved_func_item;
    jmt->current_func = saved_func;
    jmt->reg_counter = saved_reg_counter;
}
```

### 6.2 Function Calls

```cpp
static MIR_reg_t transpile_js_call_mir(JsMirTranspiler* jmt, JsCallNode* call) {
    // Special cases: console.log, Math.*, document.*
    if (is_console_log_call(call)) return transpile_js_console_log_mir(jmt, call);
    if (is_math_call(call))        return transpile_js_math_call_mir(jmt, call);
    if (is_document_call(call))    return transpile_js_document_call_mir(jmt, call);

    // Count args
    int argc = count_js_args(call->arguments);

    // Known function call (direct call via MIR_CALL — no dynamic dispatch)
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
        MIR_item_t fn = find_local_func(jmt, /* lookup name */);
        if (fn) {
            // Direct MIR call — no overhead of js_call_function
            // Build args array, emit MIR_CALL with proto
            // ...
            return result;
        }
    }

    // Generic dynamic call: js_call_function(callee, this, args[], argc)
    MIR_reg_t callee = transpile_js_expr_mir(jmt, call->callee);

    // Allocate stack frame for args (MIR_ALLOCA or use fixed-size local array)
    // ... emit args into contiguous memory ...
    // return emit_call_4(jmt, "js_call_function", ...);
}
```

### 6.3 Direct vs Dynamic Dispatch

A key advantage over the C codegen path: the MIR transpiler can emit **direct MIR_CALL instructions** to locally-defined functions. The C codegen had to create function pointers and go through `js_call_function` for everything. In MIR:

```
// Direct call (known function):
CALL _js_fibonacci_p, result, arg1

// Dynamic call (unknown callee):
CALL js_call_function_p, result, callee_item, this_item, args_ptr, argc
```

This eliminates function pointer indirection overhead for the common case.

---

## 7. Special Constructs

### 7.1 Assignment Expressions

For simple variable assignment, direct MIR register copy. For member assignment (`obj.prop = val`), delegate to `js_property_set`:

```cpp
static MIR_reg_t transpile_js_assign_mir(JsMirTranspiler* jmt, JsAssignmentNode* assign) {
    if (assign->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)assign->left;
        JsMirVarEntry* var = find_var(jmt, id->name->chars);
        MIR_reg_t val;
        if (assign->op == JS_OP_ASSIGN) {
            val = transpile_js_expr_mir(jmt, assign->right);
        } else {
            // Compound: x += y → x = js_add(x, y)
            MIR_reg_t rhs = transpile_js_expr_mir(jmt, assign->right);
            const char* fn = compound_op_to_js_fn(assign->op);
            val = emit_call_2(jmt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(jmt->ctx, var->reg),
                MIR_T_I64, MIR_new_reg_op(jmt->ctx, rhs));
        }
        emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
            MIR_new_reg_op(jmt->ctx, var->reg),
            MIR_new_reg_op(jmt->ctx, val)));
        return var->reg;
    }

    if (assign->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        // obj.prop = val → js_property_set(obj, key, val)
        JsMemberNode* member = (JsMemberNode*)assign->left;
        MIR_reg_t obj = transpile_js_expr_mir(jmt, member->object);
        MIR_reg_t key = transpile_js_member_key_mir(jmt, member);
        MIR_reg_t val = transpile_js_expr_mir(jmt, assign->right);
        return emit_call_3(jmt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, val));
    }

    return emit_null_item(jmt);
}
```

### 7.2 Array / Object Literals

```cpp
static MIR_reg_t transpile_js_array_mir(JsMirTranspiler* jmt, JsArrayNode* arr) {
    // js_array_new(length) → Item
    MIR_reg_t array = emit_call_1(jmt, "js_array_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(jmt->ctx, arr->length));

    JsAstNode* elem = arr->elements;
    int index = 0;
    while (elem) {
        MIR_reg_t val = transpile_js_expr_mir(jmt, elem);
        MIR_reg_t idx = emit_box_int_const(jmt, index);
        emit_call_void_3(jmt, "js_array_set",
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, array),
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, idx),
            MIR_T_I64, MIR_new_reg_op(jmt->ctx, val));
        elem = elem->next;
        index++;
    }
    return array;
}
```

### 7.3 Template Literals

```cpp
static MIR_reg_t transpile_js_template_mir(JsMirTranspiler* jmt, JsTemplateLiteralNode* tmpl) {
    // Call stringbuf_new(pool) to create buffer
    // For each quasi+expression pair: append string, convert expr to string and append
    // Call stringbuf_to_string → String* → emit_box_string
    // All stringbuf functions are already in import_resolver
    // ...
}
```

### 7.4 Try/Catch

The current C codegen uses `setjmp`/`longjmp`. The direct MIR approach can either:

**Option A: Continue using setjmp/longjmp** — call them as imported functions. This is the simplest approach and matches current behavior.

**Option B: Use a global exception mechanism** — set/check a global `js_exception_value` variable. Simpler to implement in MIR (no `jmp_buf` structures on stack).

Recommendation: Start with Option A for compatibility, refactor to Option B later if needed.

---

## 8. Entry Point: `js_main`

The generated module contains a `js_main(Context* ctx)` function, same as currently:

```cpp
static void transpile_js_ast_root_mir(JsMirTranspiler* jmt, JsProgramNode* program) {
    jmt->module = MIR_new_module(jmt->ctx, "js_script");

    // Import _lambda_rt
    MIR_item_t rt_import = MIR_new_import(jmt->ctx, "_lambda_rt");

    // Pre-pass: declare all top-level functions
    JsAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION)
            transpile_js_func_def_mir(jmt, (JsFunctionNode*)stmt);
        stmt = stmt->next;
    }

    // Create js_main(Context* ctx) → Item
    MIR_var_t main_var = {MIR_T_P, "ctx", 0};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(jmt->ctx, "js_main", 1, &main_ret, 1, &main_var);
    jmt->current_func_item = main_item;
    jmt->current_func = MIR_get_item_func(jmt->ctx, main_item);

    // Store ctx → _lambda_rt
    MIR_reg_t ctx_reg = MIR_reg(jmt->ctx, "ctx", jmt->current_func);
    jmt->rt_reg = ctx_reg;
    MIR_reg_t rt_addr = new_reg(jmt, "rt_addr", MIR_T_I64);
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_reg_op(jmt->ctx, rt_addr),
        MIR_new_ref_op(jmt->ctx, rt_import)));
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_mem_op(jmt->ctx, MIR_T_I64, 0, rt_addr, 0, 1),
        MIR_new_reg_op(jmt->ctx, ctx_reg)));

    // Initialize result register
    MIR_reg_t result = new_reg(jmt, "result", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_MOV,
        MIR_new_reg_op(jmt->ctx, result),
        MIR_new_int_op(jmt->ctx, (int64_t)NULL_VAL)));

    // Push global scope
    push_scope(jmt);

    // Transpile top-level statements
    stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            stmt = stmt->next;
            continue;  // already defined in pre-pass
        }
        transpile_js_stmt_mir(jmt, stmt);
        stmt = stmt->next;
    }

    // Return result
    emit_insn(jmt, MIR_new_insn(jmt->ctx, MIR_RET,
        MIR_new_reg_op(jmt->ctx, result)));

    pop_scope(jmt);
    MIR_finish_func(jmt->ctx);
    MIR_finish_module(jmt->ctx);
}
```

### Compilation Pipeline in `js_transpiler_compile`

The updated compile function replaces C codegen + C2MIR with direct MIR:

```cpp
Item js_transpiler_compile_v4(JsTranspiler* tp, Runtime* runtime) {
    // 1. Build JS AST (unchanged from v3)
    JsAstNode* js_ast = build_js_ast(tp, root);

    // 2. Initialize MIR context
    MIR_context_t ctx = jit_init(2);

    // 3. Transpile JS AST → MIR directly (NEW)
    JsMirTranspiler jmt;
    memset(&jmt, 0, sizeof(jmt));
    jmt.ctx = ctx;
    jmt.ast_root = js_ast;
    jmt.runtime = runtime;
    jmt.ast_pool = tp->ast_pool;
    jmt.name_pool = tp->name_pool;
    // ... initialize import_cache, local_funcs, etc.

    transpile_js_ast_root_mir(&jmt, (JsProgramNode*)js_ast);

    // 4. Dump MIR for debugging
    FILE* f = fopen("temp/mir_js_dump.txt", "w");
    if (f) { MIR_output(ctx, f); fclose(f); }

    // 5. Link & generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // 6. Find and execute js_main
    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, "js_main");

    // 7. Set up context and execute (same as v3)
    // ...
}
```

---

## 9. What Changes, What Stays the Same

### Unchanged (reuse entirely)

| Component | File | Notes |
|-----------|------|-------|
| JS AST node types | `js_ast.hpp` | All node structs unchanged |
| AST builder | `build_js_ast.cpp` | Tree-sitter CST → JS AST |
| JS runtime functions | `js_runtime.cpp` | Called via MIR import instead of C extern |
| JS DOM functions | `js_dom.cpp`, `js_dom.h` | Same — called via import |
| Import resolver entries | `mir.c` `func_list[]` | All js_* entries remain |
| Scope management | `js_scope.cpp` | Lifecycle functions unchanged |
| JS printer | `js_print.cpp` | Debug/diagnostic tool |

### Replaced

| Component | Old | New |
|-----------|-----|-----|
| C code generation | `transpile_js.cpp` (~1750 lines) | `transpile_js_mir.cpp` (estimated ~1200 lines) |
| C extern declarations | ~80 lines of `strbuf_append_str(... "extern ...")` | MIR proto+import created lazily by `ensure_import()` |
| Embedded `lambda.h` header | C code starts with `lambda_lambda_h[]` blob | Not needed — MIR has no C parser |
| GCC statement expressions | `({...})` for expression-returning blocks | MIR registers + labels |
| String-based code building | `strbuf_append_str(tp->code_buf, ...)` | `MIR_new_insn()` / `MIR_new_call_insn()` API |

### Estimated Reduction

The C codegen's ~1750 lines consist of:
- ~80 lines of extern declarations → eliminated
- ~200 lines of boxing wrapper code → replaced by `emit_box_*` calls
- ~300 lines of binary expression string building → replaced by `emit_call_2` calls
- ~200 lines of GCC `({...})` expression blocks → replaced by MIR temp registers
- ~200 lines of string escaping / literal output → replaced by direct pointer loads

The MIR version should be **~30% shorter** (~1200 lines) because:
1. No string escaping/concatenation boilerplate
2. No extern declarations block
3. `ensure_import` handles proto creation lazily
4. Reusable `emit_call_N` helpers reduce per-call code

---

## 10. Implementation Plan

### Phase 1: Infrastructure (estimated 1 day)

1. Create `lambda/js/transpile_js_mir.cpp`
2. Extract shared MIR helpers into `lambda/mir_helpers.h` (or inline copy from `transpile-mir.cpp`):
   - `ensure_import`, `emit_call_0..3`, `emit_call_void_0..3`
   - `emit_box_int`, `emit_box_float`, `emit_box_bool`, `emit_box_string`, `emit_box_container`
   - `new_reg`, `new_label`, `emit_insn`, `emit_label`
   - Scope management (push/pop/set_var/find_var)
3. Define `JsMirTranspiler` struct
4. Implement `transpile_js_ast_root_mir` with empty `js_main` that returns `ITEM_NULL`
5. Wire into `js_transpiler_compile` with a `--mir` flag check (or always use MIR)

### Phase 2: Expressions (estimated 2 days)

1. Literals (number, string, boolean, null/undefined)
2. Identifiers
3. Binary expressions (all operators → `js_*` runtime calls)
4. Unary expressions (all operators including ++/--)
5. Assignment expressions (simple and compound)
6. Conditional expressions (ternary)
7. Member expressions (`obj.prop`, `obj[key]`)
8. Array/Object literal expressions

### Phase 3: Statements (estimated 1 day)

1. Variable declarations (var/let/const)
2. Expression statements
3. If/else
4. While / For loops (with break/continue)
5. Return
6. Block statements

### Phase 4: Functions (estimated 2 days)

1. Function declarations (named, at module level)
2. Function expressions and arrow functions
3. Direct function calls (known callee → direct MIR CALL)
4. Dynamic function calls (`js_call_function`)
5. Special calls: console.log, Math.*, document.*
6. Method calls on objects/strings/arrays (`js_string_method`, `js_array_method`, etc.)

### Phase 5: Advanced Features (estimated 1 day)

1. Template literals (via stringbuf)
2. Try/catch/finally (setjmp/longjmp)
3. Throw
4. Class declarations (basic constructor generation)

### Phase 6: Testing & Cleanup (estimated 1 day)

1. Run all existing JS tests and verify identical output
2. Add MIR dump output for debugging
3. Remove old `transpile_js.cpp` (or keep behind a flag for comparison)
4. Update `js_transpiler_compile` to use MIR path by default

**Total estimated effort: ~8 days**

---

## 11. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| MIR register allocation for deeply nested expressions | MIR's register allocator handles unlimited virtual registers; no risk |
| `setjmp`/`longjmp` in MIR for try/catch | Import as regular functions; test with nested try blocks |
| Stack-allocated argument arrays for variadic calls | Use `MIR_ALLOCA` or fixed-size locals; Lambda transpiler already does this |
| Function expressions as values | Use `js_new_function(fn_ptr, arity)` — works identically since the function pointer is resolved at link time |
| Breaking existing tests | Keep old C codegen path available via flag during transition |

---

## 12. Future Optimizations (Post-v4)

Once direct MIR generation is stable:

1. **Type inference for numeric fast paths** — when both operands are known number literals, emit `MIR_ADD`/`MIR_DADD` directly instead of calling `js_add`
2. **Inline `js_is_truthy`** — common pattern: extract type tag, check against null/false/0/empty-string inline
3. **Constant folding** — evaluate constant expressions at compile time
4. **Dead code elimination** — skip unreachable code after return/break
5. **Direct property access** — for known object shapes, replace `js_property_access` with computed field offset loads
