#include "transpiler.hpp"
#include "safety_analyzer.hpp"
#include "re2_wrapper.hpp"
#include "../lib/log.h"
#include "../lib/hashmap.h"

extern Type TYPE_ANY, TYPE_INT;
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void transpile_expr_tco(Transpiler* tp, AstNode *expr_node, bool in_tail_position);
void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer);
void define_func_unboxed(Transpiler* tp, AstFuncNode *fn_node);
void define_func_call_wrapper(Transpiler* tp, AstFuncNode *fn_node);
bool has_typed_params(AstFuncNode* fn_node);
bool can_use_unboxed_call(AstCallNode* call_node, AstFuncNode* fn_node);
void transpile_proc_content(Transpiler* tp, AstListNode *list_node);
void transpile_let_stam(Transpiler* tp, AstLetNode *let_node, bool is_global);
void transpile_proc_statements(Transpiler* tp, AstListNode *list_node);
Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol);
Type* build_lit_datetime(Transpiler* tp, TSNode node, TSSymbol symbol);
void transpile_box_capture(Transpiler* tp, CaptureInfo* cap, bool from_outer_env);

// hashmap comparison and hashing functions for func_name_map
static int func_name_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(*(const char**)a, *(const char**)b);
}

static uint64_t func_name_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const char* name = *(const char**)item;
    return hashmap_sip(name, strlen(name), seed0, seed1);
}

// Register a function name mapping: MIR name -> Lambda name
// This is called during transpilation to build the name mapping table
// For closures/anonymous functions, defer registration until transpile_fn_expr where we know the assign name
static void register_func_name(Transpiler* tp, AstFuncNode* fn_node) {
    // For anonymous functions (no fn_node->name), don't register here
    // They will be registered in transpile_fn_expr with the assign name context
    if (!fn_node->name || !fn_node->name->chars) {
        return;
    }

    // create the map if it doesn't exist
    if (!tp->func_name_map) {
        tp->func_name_map = hashmap_new(sizeof(char*[2]), 64, 0, 0, func_name_hash, func_name_cmp, NULL, NULL);
    }

    // build MIR name (internal name like _outer36)
    StrBuf* mir_name_buf = strbuf_new_cap(64);
    write_fn_name(mir_name_buf, fn_node, NULL);
    char* mir_name = strdup(mir_name_buf->str);
    strbuf_free(mir_name_buf);

    // use the function name
    const char* lambda_name = fn_node->name->chars;

    // store in map: key is MIR name, value is Lambda name (strdup both for ownership)
    char* entry[2] = { mir_name, strdup(lambda_name) };
    hashmap_set(tp->func_name_map, entry);

    log_debug("register_func_name: '%s' -> '%s'", mir_name, lambda_name);
}

// Register a closure/anonymous function name at the point where we know its contextual name
static void register_func_name_with_context(Transpiler* tp, AstFuncNode* fn_node) {
    // create the map if it doesn't exist
    if (!tp->func_name_map) {
        tp->func_name_map = hashmap_new(sizeof(char*[2]), 64, 0, 0, func_name_hash, func_name_cmp, NULL, NULL);
    }

    // build MIR name (internal name like _f317)
    StrBuf* mir_name_buf = strbuf_new_cap(64);
    write_fn_name(mir_name_buf, fn_node, NULL);
    char* mir_name = strdup(mir_name_buf->str);
    strbuf_free(mir_name_buf);

    // determine Lambda name: prefer fn name, then current_assign_name, then <anonymous>
    const char* lambda_name = NULL;
    if (fn_node->name && fn_node->name->chars) {
        lambda_name = fn_node->name->chars;
    } else if (tp->current_assign_name && tp->current_assign_name->chars) {
        lambda_name = tp->current_assign_name->chars;
    } else {
        lambda_name = "<anonymous>";
    }

    // store in map: key is MIR name, value is Lambda name (strdup both for ownership)
    char* entry[2] = { mir_name, strdup(lambda_name) };
    hashmap_set(tp->func_name_map, entry);

    log_debug("register_func_name_with_context: '%s' -> '%s'", mir_name, lambda_name);
}

void write_node_source(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

// Check if argument type is compatible with parameter type for unboxed call
// Returns true if arg can be passed directly to unboxed version (native type)
bool is_type_compatible_for_unboxed(TypeId arg_type, TypeId param_type) {
    if (arg_type == param_type) return true;
    // int can be promoted to int64 or float
    if (arg_type == LMD_TYPE_INT && (param_type == LMD_TYPE_INT64 || param_type == LMD_TYPE_FLOAT)) {
        return true;
    }
    // int64 can be promoted to float
    if (arg_type == LMD_TYPE_INT64 && param_type == LMD_TYPE_FLOAT) {
        return true;
    }
    return false;
}

// Check if both operands are numeric types that can use native C comparison
// For ordering operators (<, <=, >, >=), only numeric types are allowed
// For equality operators (==, !=), bool is also allowed
static bool can_use_native_comparison(AstBinaryNode* bi_node, bool is_equality_op) {
    if (!bi_node->left->type || !bi_node->right->type) return false;
    TypeId left_type = bi_node->left->type->type_id;
    TypeId right_type = bi_node->right->type->type_id;

    // Fast path for same types
    if (left_type == right_type) {
        if (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT) {
            return true;
        }
        // Bool only allowed for equality (==, !=), not ordering (<, <=, >, >=)
        if (is_equality_op && left_type == LMD_TYPE_BOOL) {
            return true;
        }
        return false;
    }

    // Fast path for int/int64/float combinations (C handles promotion)
    bool left_numeric = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT);
    bool right_numeric = (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_INT64 || right_type == LMD_TYPE_FLOAT);

    return left_numeric && right_numeric;
}

// Math functions that can use native C equivalents when argument is typed numeric
// Maps Lambda function name to C math function name
struct NativeMathFunc {
    const char* lambda_name;
    const char* c_name;
    bool returns_float;  // true if returns float, false if returns int
    int arg_count;       // 1 or 2 arguments
};

static const NativeMathFunc native_math_funcs[] = {
    // Single-argument functions (use C math library directly)
    {"sin", "sin", true, 1},
    {"cos", "cos", true, 1},
    {"tan", "tan", true, 1},
    {"sqrt", "sqrt", true, 1},
    {"log", "log", true, 1},
    {"log10", "log10", true, 1},
    {"exp", "exp", true, 1},
    {"abs", "fabs", true, 1},       // Note: fabs for float, but we may prefer fn_abs_i for int
    {"floor", "floor", true, 1},
    {"ceil", "ceil", true, 1},
    {"round", "round", true, 1},
    // Two-argument functions (use our unboxed wrappers)
    {"pow", "fn_pow_u", true, 2},
    {NULL, NULL, false, 0}
};

// Two-argument min/max functions - separate table since they're overloaded
struct NativeBinaryFunc {
    const char* lambda_name;
    const char* c_name_float;    // double version
    const char* c_name_int;      // int64 version (or NULL if no int version)
};

static const NativeBinaryFunc native_binary_funcs[] = {
    {"min", "fn_min2_u", NULL},
    {"max", "fn_max2_u", NULL},
    {NULL, NULL, NULL}
};

// Helper to check if type is numeric (int, int64, or float)
static inline bool is_numeric_type(TypeId t) {
    return t == LMD_TYPE_INT || t == LMD_TYPE_INT64 || t == LMD_TYPE_FLOAT;
}

// Helper to check if type is integer (int or int64)
static inline bool is_integer_type(TypeId t) {
    return t == LMD_TYPE_INT || t == LMD_TYPE_INT64;
}

// Check if a sys func call can use native C math function
// Returns the C function name if applicable, NULL otherwise
static const char* can_use_native_math(AstSysFuncNode* sys_fn_node, AstNode* arg) {
    if (!sys_fn_node || !sys_fn_node->fn_info || !arg || !arg->type) return NULL;

    // Check if argument has known numeric type
    TypeId arg_type = arg->type->type_id;
    if (!is_numeric_type(arg_type)) {
        return NULL;
    }

    // Look up the function name
    const char* fn_name = sys_fn_node->fn_info->name;
    for (int i = 0; native_math_funcs[i].lambda_name != NULL; i++) {
        if (strcmp(fn_name, native_math_funcs[i].lambda_name) == 0) {
            return native_math_funcs[i].c_name;
        }
    }
    return NULL;
}

// Check if a sys func call can use native two-arg function
// Returns info about the function if applicable, NULL otherwise
static const NativeMathFunc* can_use_native_math_binary(AstSysFuncNode* sys_fn_node, AstNode* arg1, AstNode* arg2) {
    if (!sys_fn_node || !sys_fn_node->fn_info || !arg1 || !arg2 || !arg1->type || !arg2->type) return NULL;

    TypeId type1 = arg1->type->type_id;
    TypeId type2 = arg2->type->type_id;

    if (!is_numeric_type(type1) || !is_numeric_type(type2)) {
        return NULL;
    }

    const char* fn_name = sys_fn_node->fn_info->name;
    for (int i = 0; native_math_funcs[i].lambda_name != NULL; i++) {
        if (native_math_funcs[i].arg_count == 2 && strcmp(fn_name, native_math_funcs[i].lambda_name) == 0) {
            return &native_math_funcs[i];
        }
    }
    return NULL;
}

// Check if a sys func call can use native binary func (min/max)
static const NativeBinaryFunc* can_use_native_binary_func(AstSysFuncNode* sys_fn_node, AstNode* arg1, AstNode* arg2) {
    if (!sys_fn_node || !sys_fn_node->fn_info || !arg1 || !arg2 || !arg1->type || !arg2->type) return NULL;

    TypeId type1 = arg1->type->type_id;
    TypeId type2 = arg2->type->type_id;

    if (!is_numeric_type(type1) || !is_numeric_type(type2)) {
        return NULL;
    }

    const char* fn_name = sys_fn_node->fn_info->name;
    for (int i = 0; native_binary_funcs[i].lambda_name != NULL; i++) {
        if (strcmp(fn_name, native_binary_funcs[i].lambda_name) == 0) {
            return &native_binary_funcs[i];
        }
    }
    return NULL;
}

// Check if all arguments can use the unboxed call path
// Returns true if we can call the _u version directly
bool can_use_unboxed_call(AstCallNode* call_node, AstFuncNode* fn_node) {
    if (!fn_node || !has_typed_params(fn_node)) return false;

    // Don't use unboxed for procs - no _u version is generated for them
    if (fn_node->node_type == AST_NODE_PROC) return false;

    // Don't use unboxed for TCO functions - they need the goto-based implementation
    if (should_use_tco(fn_node)) {
        return false;
    }

    // Check that the function has a specific return type (not ANY)
    // This is required so transpile_box_item can properly wrap the result
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    Type* ret_type = fn_type ? fn_type->returned : nullptr;

    log_debug("can_use_unboxed_call: fn=%.*s ret_type=%d",
        (int)fn_node->name->len, fn_node->name->chars,
        ret_type ? ret_type->type_id : -1);

    // If the boxed version already returns a native type (explicit scalar return),
    // there's no benefit to using the unboxed version - they're the same
    if (ret_type && (ret_type->type_id == LMD_TYPE_INT ||
                     ret_type->type_id == LMD_TYPE_FLOAT ||
                     ret_type->type_id == LMD_TYPE_BOOL)) {
        log_debug("can_use_unboxed_call: returning false (boxed already returns native)");
        return false;
    }

    // If return type is ANY, try to infer from body's last expression
    if ((!ret_type || ret_type->type_id == LMD_TYPE_ANY) && fn_node->body) {
        if (fn_node->body->node_type == AST_NODE_CONTENT) {
            AstListNode* content = (AstListNode*)fn_node->body;
            AstNode* last_expr = content->item;
            while (last_expr && last_expr->next) {
                last_expr = last_expr->next;
            }
            if (last_expr && last_expr->type) {
                ret_type = last_expr->type;
                log_debug("inferred ret_type from content: %d", ret_type->type_id);
            }
        } else if (fn_node->body->type) {
            ret_type = fn_node->body->type;
            log_debug("inferred ret_type from body: %d", ret_type->type_id);
        }
    }

    // Only use unboxed if return type is a specific scalar (INT for now)
    if (!ret_type || ret_type->type_id != LMD_TYPE_INT) {
        log_debug("can_use_unboxed_call: returning false (ret_type not INT)");
        return false;
    }

    log_debug("can_use_unboxed_call: checking params");

    AstNode* arg = call_node->argument;
    AstNamedNode* param = fn_node->param;

    while (arg && param) {
        TypeParam* pt = (TypeParam*)param->type;
        // Skip if param is optional (uses Item type in unboxed)
        if (pt->is_optional) {
            arg = arg->next;
            param = (AstNamedNode*)param->next;
            continue;
        }

        TypeId param_type_id = pt->type_id;
        TypeId arg_type_id = arg->type ? arg->type->type_id : LMD_TYPE_ANY;

        // If param is ANY, doesn't affect unboxed decision
        if (param_type_id == LMD_TYPE_ANY) {
            arg = arg->next;
            param = (AstNamedNode*)param->next;
            continue;
        }

        // If arg type is unknown (ANY), can't use unboxed
        if (arg_type_id == LMD_TYPE_ANY) {
            return false;
        }

        // Check type compatibility
        if (!is_type_compatible_for_unboxed(arg_type_id, param_type_id)) {
            return false;
        }

        arg = arg->next;
        param = (AstNamedNode*)param->next;
    }

    return true;
}

// Check if a function has any explicitly typed (non-any) parameters with concrete scalar C types.
// Only concrete scalar types (int, float, bool, string, etc.) are considered "typed" because
// they use native C types that differ from Item. Union types (int^), function types, container
// types, etc. are all passed as Item (or Item-compatible pointers) at runtime.
bool has_typed_params(AstFuncNode* fn_node) {
    AstNamedNode *param = fn_node->param;
    while (param) {
        TypeParam* pt = (TypeParam*)param->type;
        if (pt) {
            TypeId tid = pt->type_id;
            if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 ||
                tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
                tid == LMD_TYPE_STRING || tid == LMD_TYPE_BINARY ||
                tid == LMD_TYPE_SYMBOL || tid == LMD_TYPE_DECIMAL ||
                tid == LMD_TYPE_DTIME) {
                return true;
            }
        }
        param = (AstNamedNode*)param->next;
    }
    return false;
}

// Check if a function needs a fn_call*-compatible wrapper (_w suffix).
// fn_call* casts function pointers to Item(*)(Item,...) ABI.
// A wrapper is needed when the function's native signature doesn't match this ABI:
//   - Has typed params (native types instead of Item)
//   - Returns a native type AND has no params (e.g., fn g() { 42 })
// Functions with ALL untyped params already use Item-level operations internally,
// so their body effectively returns Items — no wrapper needed.
// Closures and can_raise functions already have Item params/return, so no wrapper needed.
bool needs_fn_call_wrapper(AstFuncNode* fn_node) {
    if (fn_node->captures) return false;  // closures already use Item ABI
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;

    // Functions with typed params need param unboxing wrapper
    if (has_typed_params(fn_node)) return true;

    // Functions with ALL untyped params: body uses Item-level ops → effectively returns Item
    // No wrapper needed (fn_call* can use the function directly)
    if (fn_node->param) return false;

    // Functions with NO params: body may return raw native values → needs wrapper
    if (!fn_type->can_raise) {
        Type *ret_type = fn_type->returned;
        if (!ret_type && fn_node->body) ret_type = fn_node->body->type;
        if (!ret_type) ret_type = &TYPE_ANY;
        TypeId ret_tid = ret_type->type_id;
        if (ret_tid == LMD_TYPE_INT || ret_tid == LMD_TYPE_INT64 ||
            ret_tid == LMD_TYPE_FLOAT || ret_tid == LMD_TYPE_BOOL ||
            ret_tid == LMD_TYPE_STRING || ret_tid == LMD_TYPE_BINARY ||
            ret_tid == LMD_TYPE_SYMBOL || ret_tid == LMD_TYPE_DECIMAL ||
            ret_tid == LMD_TYPE_DTIME) {
            return true;
        }
    }

    return false;
}

// Write function name with optional suffix for boxed/unboxed versions
// suffix: NULL for legacy names, "_b" for boxed, "_u" for unboxed
void write_fn_name_ex(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import, const char* suffix) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    strbuf_append_char(strbuf, '_');
    if (fn_node->name && fn_node->name->chars) {
        strbuf_append_str_n(strbuf, fn_node->name->chars, fn_node->name->len);
    } else {
        strbuf_append_char(strbuf, 'f');
    }
    // add suffix before offset for clarity: _square_b15 vs _square15
    if (suffix) {
        strbuf_append_str(strbuf, suffix);
    }
    // char offset ensures the fn name is unique across the script
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
}

void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import) {
    write_fn_name_ex(strbuf, fn_node, import, NULL);
}

void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    // user var name starts with '_'
    strbuf_append_char(strbuf, '_');
    strbuf_append_str_n(strbuf, asn_node->name->chars, asn_node->name->len);
}

// write closure env struct name
void write_env_name(StrBuf *strbuf, AstFuncNode* fn_node) {
    strbuf_append_str(strbuf, "Env_f");
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
}

// define closure environment struct for a function with captures
void define_closure_env(Transpiler* tp, AstFuncNode *fn_node) {
    if (!fn_node->captures) return;

    strbuf_append_str(tp->code_buf, "\ntypedef struct ");
    write_env_name(tp->code_buf, fn_node);
    strbuf_append_str(tp->code_buf, " {\n");

    // add each captured variable to the struct
    CaptureInfo* cap = fn_node->captures;
    while (cap) {
        strbuf_append_str(tp->code_buf, "  ");
        // use Item type for captured values (immutable capture by value)
        strbuf_append_str(tp->code_buf, "Item ");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        strbuf_append_str(tp->code_buf, ";\n");
        cap = cap->next;
    }

    strbuf_append_str(tp->code_buf, "} ");
    write_env_name(tp->code_buf, fn_node);
    strbuf_append_str(tp->code_buf, ";\n");
}

// check if an identifier is a captured variable in the current closure
CaptureInfo* find_capture(AstFuncNode* closure, String* name) {
    if (!closure || !closure->captures) return nullptr;
    CaptureInfo* cap = closure->captures;
    while (cap) {
        if (cap->name->len == name->len &&
            strncmp(cap->name->chars, name->chars, name->len) == 0) {
            return cap;
        }
        cap = cap->next;
    }
    return nullptr;
}

// Forward declaration for recursive call
void pre_define_closure_envs(Transpiler* tp, AstNode* node);
void forward_declare_func(Transpiler* tp, AstFuncNode *fn_node);

// Recursively traverse AST and emit closure env struct definitions
// Also emits forward declarations for closure functions
void pre_define_closure_envs(Transpiler* tp, AstNode* node) {
    if (!node) return;

    switch (node->node_type) {
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC: {
        AstFuncNode* fn = (AstFuncNode*)node;
        // Define this function's closure env if it has captures
        if (fn->captures) {
            define_closure_env(tp, fn);
            // Also emit a forward declaration for the closure function
            // so it can be referenced before its full definition
            forward_declare_func(tp, fn);
        }
        // Also check function's body and parameters for nested closures
        pre_define_closure_envs(tp, fn->body);
        AstNamedNode* param = fn->param;
        while (param) {
            pre_define_closure_envs(tp, (AstNode*)param);
            param = (AstNamedNode*)param->next;
        }
        break;
    }
    case AST_NODE_PRIMARY:
        pre_define_closure_envs(tp, ((AstPrimaryNode*)node)->expr);
        break;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        pre_define_closure_envs(tp, ((AstUnaryNode*)node)->operand);
        break;
    case AST_NODE_BINARY:
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->left);
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_PIPE:
        pre_define_closure_envs(tp, ((AstPipeNode*)node)->left);
        pre_define_closure_envs(tp, ((AstPipeNode*)node)->right);
        break;
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
        // no children to process
        break;
    case AST_NODE_IF_EXPR:  case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        pre_define_closure_envs(tp, if_node->cond);
        pre_define_closure_envs(tp, if_node->then);
        pre_define_closure_envs(tp, if_node->otherwise);
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match_node = (AstMatchNode*)node;
        pre_define_closure_envs(tp, match_node->scrutinee);
        AstMatchArm* arm = match_node->first_arm;
        while (arm) {
            if (arm->pattern) pre_define_closure_envs(tp, arm->pattern);
            pre_define_closure_envs(tp, arm->body);
            arm = (AstMatchArm*)arm->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR:  case AST_NODE_FOR_STAM: {
        AstForNode* for_node = (AstForNode*)node;
        pre_define_closure_envs(tp, (AstNode*)for_node->loop);
        pre_define_closure_envs(tp, for_node->then);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        pre_define_closure_envs(tp, while_node->cond);
        pre_define_closure_envs(tp, while_node->body);
        break;
    }
    case AST_NODE_RETURN_STAM:
        pre_define_closure_envs(tp, ((AstReturnNode*)node)->value);
        break;
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
        pre_define_closure_envs(tp, ((AstRaiseNode*)node)->value);
        break;
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM: {
        AstNode* decl = ((AstLetNode*)node)->declare;
        while (decl) {
            pre_define_closure_envs(tp, decl);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_ASSIGN:  case AST_NODE_KEY_EXPR:
        pre_define_closure_envs(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ASSIGN_STAM:
        pre_define_closure_envs(tp, ((AstAssignStamNode*)node)->value);
        break;
    case AST_NODE_PIPE_FILE_STAM:
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->left);
        pre_define_closure_envs(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_LIST:  case AST_NODE_CONTENT: {
        AstListNode* list = (AstListNode*)node;
        AstNode* decl = list->declare;
        while (decl) {
            pre_define_closure_envs(tp, decl);
            decl = decl->next;
        }
        AstNode* item = list->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_ARRAY: {
        AstNode* item = ((AstArrayNode*)node)->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_MAP:  case AST_NODE_ELEMENT: {
        AstNode* item = ((AstMapNode*)node)->item;
        while (item) {
            pre_define_closure_envs(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_CALL_EXPR: {
        pre_define_closure_envs(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            pre_define_closure_envs(tp, arg);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        pre_define_closure_envs(tp, ((AstFieldNode*)node)->object);
        pre_define_closure_envs(tp, ((AstFieldNode*)node)->field);
        break;
    case AST_NODE_PARENT_EXPR:
        pre_define_closure_envs(tp, ((AstParentNode*)node)->object);
        break;
    default:
        break;
    }
}

// check if an AST node is an optional parameter reference (already Item type at runtime)
bool is_optional_param_ref(AstNode* item) {
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            TypeParam* param_type = (TypeParam*)ident_node->entry->node->type;
            return param_type->is_optional;
        }
        item = pri->expr;
    }
    return false;
}

// check if an AST node is a parameter reference in the current closure (already Item type at runtime)
bool is_closure_param_ref(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            // Make sure this param belongs to the CURRENT closure, not an outer function
            AstNamedNode* param = (AstNamedNode*)ident_node->entry->node;
            // Check if this parameter is in the current closure's parameter list
            AstNamedNode* closure_param = tp->current_closure->param;
            while (closure_param) {
                if (closure_param == param) return true;
                closure_param = (AstNamedNode*)closure_param->next;
            }
            return false;  // This is a param from an outer function (captured)
        }
        item = pri->expr;
    }
    return false;
}

// check if an AST node is a captured variable reference (stored as Item in closure env)
bool is_captured_var_ref(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
            return cap != nullptr;
        }
        item = pri->expr;
    }
    return false;
}

// emit env access for a captured variable (without unboxing) - returns true if successful
bool emit_captured_var_item(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
            if (cap) {
                // emit _env->varname directly (this is already an Item)
                strbuf_append_str(tp->code_buf, "_env->");
                strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                return true;
            }
            return false;
        }
        item = pri->expr;
    }
    return false;
}

// emit param reference as Item (without unboxing) for optional/default/closure params
// these params are stored as Item type at runtime, so emit _paramname directly
bool emit_param_item(Transpiler* tp, AstNode* item) {
    // unwrap nested PRIMARY nodes
    while (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (!pri->expr) return false;
        if (pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
            if (!ident_node->entry || !ident_node->entry->node) return false;
            if (ident_node->entry->node->node_type != AST_NODE_PARAM) return false;
            TypeParam* param_type = (TypeParam*)ident_node->entry->node->type;
            // check if this param needs to be treated as Item (optional, default, or closure param)
            bool is_item_param = param_type->is_optional || param_type->default_value || tp->current_closure;
            if (is_item_param) {
                // emit _paramname directly (this is already an Item)
                strbuf_append_char(tp->code_buf, '_');
                strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                return true;
            }
            return false;
        }
        item = pri->expr;
    }
    return false;
}

// Check if a call expression will use fn_call* dynamic dispatch (returns Item).
// Dynamic dispatch occurs when calling through a variable, parameter, index,
// member, or chained call — i.e., anything other than a direct named function.
bool is_dynamic_fn_call(AstNode* node) {
    // Unwrap primary expression wrapper
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) node = pri->expr;
    }
    if (node->node_type != AST_NODE_CALL_EXPR) return false;
    AstCallNode* call = (AstCallNode*)node;
    // sys func calls are never dynamic dispatch
    if (call->function->node_type == AST_NODE_SYS_FUNC) return false;
    AstPrimaryNode* primary = call->function->node_type == AST_NODE_PRIMARY ?
        (AstPrimaryNode*)call->function : NULL;
    if (!primary || !primary->expr || primary->expr->node_type != AST_NODE_IDENT)
        return true;  // non-identifier callee (index, member, call expr) → dynamic
    AstIdentNode* ident = (AstIdentNode*)primary->expr;
    if (!ident->entry || !ident->entry->node) return true;  // unresolved → dynamic
    AstNodeType entry_type = ident->entry->node->node_type;
    // Direct function/proc references use direct call, everything else is dynamic
    return entry_type != AST_NODE_FUNC && entry_type != AST_NODE_FUNC_EXPR && entry_type != AST_NODE_PROC;
}

// Check if a binary expression uses Item-returning runtime functions (fn_add, fn_sub, fn_mul)
// rather than native C operators. When operands are not both concrete numeric types,
// the transpiler uses runtime functions which return Item — boxing is not needed.
static bool binary_already_returns_item(AstNode* node) {
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) node = pri->expr;
    }
    if (node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* bin_node = (AstBinaryNode*)node;
    TypeId lt = bin_node->left->type ? bin_node->left->type->type_id : LMD_TYPE_ANY;
    TypeId rt = bin_node->right->type ? bin_node->right->type->type_id : LMD_TYPE_ANY;
    bool both_numeric = (LMD_TYPE_INT <= lt && lt <= LMD_TYPE_FLOAT &&
                         LMD_TYPE_INT <= rt && rt <= LMD_TYPE_FLOAT);

    switch (bin_node->op) {
    case OPERATOR_IDIV:
    case OPERATOR_MOD:
        // Always use Item-returning fn_idiv/fn_mod
        return true;
    case OPERATOR_POW:
        // Numeric → push_d(fn_pow_u(...)) returns double* which needs boxing
        // Non-numeric → fn_pow returns Item
        return !both_numeric;
    case OPERATOR_DIV:
        // Numeric → native ((double)left/(double)right) returns double, needs boxing
        // Non-numeric → fn_div returns Item
        return !both_numeric;
    case OPERATOR_ADD:
    case OPERATOR_SUB:
    case OPERATOR_MUL: {
        // Same numeric type → native C op → returns raw value
        if (lt == rt && (lt == LMD_TYPE_INT || lt == LMD_TYPE_INT64 || lt == LMD_TYPE_FLOAT)) {
            return false;
        }
        // Both in numeric range → native C op
        if (both_numeric) return false;
        // Otherwise, fn_add/fn_sub/fn_mul are used → returns Item
        return true;
    }
    default:
        return false;
    }
}

// Check if a direct call targets a function whose boxed version effectively returns Item.
// Functions with ALL untyped (ANY) params use Item-level runtime operations internally,
// so their body returns Item values even though the C return type is int64_t/double/etc.
static bool direct_call_returns_item(AstNode* node) {
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) node = pri->expr;
    }
    if (node->node_type != AST_NODE_CALL_EXPR) return false;
    AstCallNode* call = (AstCallNode*)node;
    if (call->function->node_type == AST_NODE_SYS_FUNC) return false;
    AstPrimaryNode* primary = call->function->node_type == AST_NODE_PRIMARY ?
        (AstPrimaryNode*)call->function : NULL;
    if (!primary || !primary->expr || primary->expr->node_type != AST_NODE_IDENT) return false;
    AstIdentNode* ident = (AstIdentNode*)primary->expr;
    if (!ident->entry || !ident->entry->node) return false;
    AstNode* entry_node = ident->entry->node;
    if (entry_node->node_type != AST_NODE_FUNC && entry_node->node_type != AST_NODE_FUNC_EXPR)
        return false;
    AstFuncNode* fn_node = (AstFuncNode*)entry_node;
    // If function has ALL untyped params (no typed params but HAS params),
    // body uses Item-level operations → returns Item effectively
    if (fn_node->param && !has_typed_params(fn_node)) return true;
    return false;
}

void transpile_box_item(Transpiler* tp, AstNode *item) {
    if (!item->type) {
        log_debug("transpile box item: NULL type, node_type: %d", item->node_type);
        return;
    }

    // fn_call* dispatch always returns Item — skip boxing to avoid double-boxing
    // Also skip for binary exprs and direct calls that already return Item
    if (is_dynamic_fn_call(item) || binary_already_returns_item(item) || direct_call_returns_item(item)) {
        transpile_expr(tp, item);
        return;
    }

    // Handle single-value CONTENT blocks: emit declarations + box last value.
    // Must be done before the type switch because a CONTENT node's type_id can be
    // LIST, ANY, BOOL, etc. — all need the same "declarations; box(last_val)" pattern.
    if (item->node_type == AST_NODE_CONTENT) {
        AstListNode* content = (AstListNode*)item;
        // count declarations vs value items directly (not depending on list_type)
        int decl_count = 0;
        int value_count = 0;
        AstNode* last_val = nullptr;
        AstNode* scan = content->item;
        while (scan) {
            if (scan->node_type == AST_NODE_LET_STAM || scan->node_type == AST_NODE_PUB_STAM ||
                scan->node_type == AST_NODE_TYPE_STAM || scan->node_type == AST_NODE_FUNC ||
                scan->node_type == AST_NODE_FUNC_EXPR || scan->node_type == AST_NODE_PROC ||
                scan->node_type == AST_NODE_STRING_PATTERN || scan->node_type == AST_NODE_SYMBOL_PATTERN) {
                decl_count++;
            } else {
                value_count++;
                last_val = scan;
            }
            scan = scan->next;
        }
        if (value_count == 1 && last_val && decl_count > 0) {
            // single-value block expression: emit declarations, then box the last value
            strbuf_append_str(tp->code_buf, "({");
            AstNode* ci = content->item;
            while (ci) {
                if (ci->node_type == AST_NODE_LET_STAM || ci->node_type == AST_NODE_PUB_STAM || ci->node_type == AST_NODE_TYPE_STAM) {
                    transpile_let_stam(tp, (AstLetNode*)ci, false);
                }
                ci = ci->next;
            }
            strbuf_append_char(tp->code_buf, '\n');
            transpile_box_item(tp, last_val);
            strbuf_append_str(tp->code_buf, ";})");
            return;
        }
    }

    switch (item->type->type_id) {
    case LMD_TYPE_NULL:
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        } else {
            // Non-literal NULL type means a variable initialized to null but may hold any value
            // It's stored as Item type, so just emit the expression directly
            transpile_expr(tp, item);
        }
        break;
    case LMD_TYPE_BOOL:
        // Check if this is a closure parameter (already Item type at runtime)
        if (is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        strbuf_append_str(tp->code_buf, "b2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_INT: {
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }

        strbuf_append_str(tp->code_buf, "i2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    }
    case LMD_TYPE_INT64:
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_l2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            log_enter();
            log_debug("transpile_box_item: push_l");
            strbuf_append_str(tp->code_buf, "push_l(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            log_leave();
        }
        break;
    case LMD_TYPE_FLOAT:
        // Check if this is an optional parameter or closure parameter (already Item type at runtime)
        if (is_optional_param_ref(item) || is_closure_param_ref(tp, item)) {
            emit_param_item(tp, item);  // emit _paramname directly (already an Item)
            break;
        }
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_d2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            strbuf_append_str(tp->code_buf, "push_d(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_DTIME:
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_k2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal datetime expression
            strbuf_append_str(tp->code_buf, "push_k(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_DECIMAL:
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_c2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal decimal expression
            strbuf_append_str(tp->code_buf, "c2it(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_NUMBER:
        // NUMBER type is a union of int/float - transpile the expression directly
        transpile_expr(tp, item);
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // Check if this is a captured variable reference (already Item in closure env)
        if (is_captured_var_ref(tp, item)) {
            emit_captured_var_item(tp, item);  // emit _env->varname directly (already an Item)
            break;
        }
        char t = item->type->type_id == LMD_TYPE_STRING ? 's' :
            item->type->type_id == LMD_TYPE_SYMBOL ? 'y' :
            item->type->type_id == LMD_TYPE_BINARY ? 'x':'k';
        if (item->type->is_literal) {
            strbuf_append_format(tp->code_buf, "const_%c2it(", t);
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_format(tp->code_buf, "%c2it(", t);
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    }
    case LMD_TYPE_LIST:
        // Single-value CONTENT blocks are handled above (before the switch).
        // Multi-value content/list: list_end() already returns Item.
        transpile_expr(tp, item);
        break;
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
        // All container types including Function*, Path*, and Type* (patterns, type expressions) are direct pointers
        strbuf_append_str(tp->code_buf, "(Item)(");
        transpile_expr(tp, item);  // raw pointer treated as Item
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
        // For call expressions, check if the actual function has a typed return
        // This handles forward-referenced functions where the type wasn't known at AST build time
        {
            AstNode* check_item = item;
            // unwrap primary node if present
            if (check_item->node_type == AST_NODE_PRIMARY) {
                AstPrimaryNode* pri = (AstPrimaryNode*)check_item;
                if (pri->expr) check_item = pri->expr;
            }
            if (check_item->node_type == AST_NODE_CALL_EXPR) {
                AstCallNode* call_node = (AstCallNode*)check_item;
                AstNode* fn_expr = call_node->function;
                // unwrap primary node if present
                if (fn_expr->node_type == AST_NODE_PRIMARY) {
                    AstPrimaryNode* pri = (AstPrimaryNode*)fn_expr;
                    if (pri->expr) fn_expr = pri->expr;
                }
                // check if this is an identifier referencing a function
                if (fn_expr->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident_node = (AstIdentNode*)fn_expr;
                    AstNode* entry_node = ident_node->entry ? ident_node->entry->node : nullptr;
                    if (entry_node && (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC)) {
                        AstFuncNode* fn_node = (AstFuncNode*)entry_node;
                        TypeFunc* fn_type = (TypeFunc*)fn_node->type;
                        // If function can raise errors, it returns Item - no boxing needed
                        if (fn_type && fn_type->can_raise) {
                            log_debug("transpile_box_item: function '%.*s' can_raise, returns Item - no boxing",
                                (int)fn_node->name->len, fn_node->name->chars);
                            transpile_expr(tp, item);
                            break;
                        }
                        if (fn_type && fn_type->returned && fn_type->returned->type_id != LMD_TYPE_ANY) {
                            // function has a typed return - need to box it appropriately
                            TypeId ret_type_id = fn_type->returned->type_id;
                            log_debug("transpile_box_item: forward-ref call to '%.*s' with return type %d",
                                (int)fn_node->name->len, fn_node->name->chars, ret_type_id);
                            if (ret_type_id == LMD_TYPE_FLOAT) {
                                strbuf_append_str(tp->code_buf, "push_d(");
                                transpile_expr(tp, item);
                                strbuf_append_char(tp->code_buf, ')');
                                break;
                            } else if (ret_type_id == LMD_TYPE_INT) {
                                strbuf_append_str(tp->code_buf, "i2it(");
                                transpile_expr(tp, item);
                                strbuf_append_char(tp->code_buf, ')');
                                break;
                            } else if (ret_type_id == LMD_TYPE_INT64) {
                                strbuf_append_str(tp->code_buf, "push_l(");
                                transpile_expr(tp, item);
                                strbuf_append_char(tp->code_buf, ')');
                                break;
                            } else if (ret_type_id == LMD_TYPE_BOOL) {
                                strbuf_append_str(tp->code_buf, "b2it(");
                                transpile_expr(tp, item);
                                strbuf_append_char(tp->code_buf, ')');
                                break;
                            } else if (ret_type_id == LMD_TYPE_STRING || ret_type_id == LMD_TYPE_SYMBOL || ret_type_id == LMD_TYPE_BINARY) {
                                char t = ret_type_id == LMD_TYPE_STRING ? 's' :
                                    ret_type_id == LMD_TYPE_SYMBOL ? 'y' : 'x';
                                strbuf_append_format(tp->code_buf, "%c2it(", t);
                                transpile_expr(tp, item);
                                strbuf_append_char(tp->code_buf, ')');
                                break;
                            }
                            // for other types (containers), fall through to default behavior
                        }
                    }
                }
            }
        }
        // ANY and ERROR types are already Item at runtime - no boxing needed
        transpile_expr(tp, item);
        break;
    default:
        log_debug("unknown box item type: %d", item->type->type_id);
    }
}

void transpile_push_items(Transpiler* tp, AstNode *item, bool is_elmt) {
    while (item) {
        // skip let declaration and pattern definitions
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM ||
            item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;  continue;
        }
        // use list_push_spread to automatically spread spreadable arrays from for-expressions
        strbuf_append_format(tp->code_buf, "\n list_push_spread(%s, ", is_elmt ? "el" : "ls");
        transpile_box_item(tp, item);
        strbuf_append_str(tp->code_buf, ");");
        item = item->next;
    }
    strbuf_append_format(tp->code_buf, "\n list_end(%s);})", is_elmt ? "el" : "ls");
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    if (pri_node->expr) {
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri_node->expr;
            log_debug("transpile_primary_expr: identifier %.*s, type: %d",
                (int)ident_node->name->len, ident_node->name->chars, pri_node->type->type_id);

            // check if this is a captured variable in the current closure
            if (tp->current_closure) {
                CaptureInfo* cap = find_capture(tp->current_closure, ident_node->name);
                if (cap) {
                    // access captured variable via env
                    // env stores Items, so we may need to unbox depending on usage
                    TypeId type_id = pri_node->type->type_id;
                    if (type_id == LMD_TYPE_INT) {
                        strbuf_append_str(tp->code_buf, "it2i(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_INT64) {
                        strbuf_append_str(tp->code_buf, "it2l(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_FLOAT) {
                        strbuf_append_str(tp->code_buf, "it2f(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_BOOL) {
                        strbuf_append_str(tp->code_buf, "it2b(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_BINARY) {
                        strbuf_append_str(tp->code_buf, "it2s(_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        // for Item or container types, return directly
                        strbuf_append_str(tp->code_buf, "_env->");
                        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                    }
                    return;
                }
            }

            AstNode* entry_node = ident_node->entry ? ident_node->entry->node : nullptr;

            // check if this is an optional/default parameter (needs unboxing since it's passed as Item)
            if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                TypeParam* param_type = (TypeParam*)entry_node->type;
                // Unbox if: (1) it's in a closure, OR (2) it has default value or is optional
                bool needs_unboxing = tp->current_closure || param_type->is_optional || param_type->default_value;

                if (needs_unboxing) {
                    // parameter passed as Item - needs unboxing to actual type
                    TypeId type_id = pri_node->type->type_id;
                    if (type_id == LMD_TYPE_INT) {
                        strbuf_append_str(tp->code_buf, "it2i(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_INT64) {
                        strbuf_append_str(tp->code_buf, "it2l(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_FLOAT) {
                        strbuf_append_str(tp->code_buf, "it2f(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_BOOL) {
                        strbuf_append_str(tp->code_buf, "it2b(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_BINARY) {
                        strbuf_append_str(tp->code_buf, "it2s(_");
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        // for Item or container types, return directly (already Item)
                        strbuf_append_char(tp->code_buf, '_');
                        strbuf_append_str_n(tp->code_buf, ident_node->name->chars, ident_node->name->len);
                    }
                    return;
                }
            }

            if (entry_node) {
                if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_FUNC_EXPR || entry_node->node_type == AST_NODE_PROC) {
                    // Function reference - check if it's a closure
                    AstFuncNode* fn_node = (AstFuncNode*)entry_node;
                    TypeFunc* fn_type = fn_node->type ? (TypeFunc*)fn_node->type : nullptr;
                    int arity = fn_type ? fn_type->param_count : 0;

                    if (fn_node->captures) {
                        // This is a closure - need to create the env and use to_closure
                        // Generate same code as transpile_fn_expr for closures
                        strbuf_append_str(tp->code_buf, "({ ");
                        write_env_name(tp->code_buf, fn_node);
                        strbuf_append_str(tp->code_buf, "* _closure_env = heap_calloc(sizeof(");
                        write_env_name(tp->code_buf, fn_node);
                        strbuf_append_str(tp->code_buf, "), 0);\n");

                        // populate captured variables
                        CaptureInfo* cap = fn_node->captures;
                        while (cap) {
                            strbuf_append_str(tp->code_buf, "  _closure_env->");
                            strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
                            strbuf_append_str(tp->code_buf, " = ");

                            // box the captured variable
                            bool from_outer = false;
                            if (tp->current_closure) {
                                CaptureInfo* outer_cap = find_capture(tp->current_closure, cap->name);
                                from_outer = (outer_cap != nullptr);
                            }
                            transpile_box_capture(tp, cap, from_outer);
                            strbuf_append_str(tp->code_buf, ";\n");
                            cap = cap->next;
                        }

                        strbuf_append_str(tp->code_buf, "  to_closure_named(");
                        write_fn_name(tp->code_buf, fn_node, (AstImportNode*)ident_node->entry->import);
                        strbuf_append_format(tp->code_buf, ",%d,_closure_env,", arity);
                        // pass function name as string literal for stack traces
                        // prefer named function, fall back to assignment variable name, then <anonymous>
                        if (fn_node->name && fn_node->name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else {
                            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
                        }
                        strbuf_append_str(tp->code_buf, "); })");
                    } else {
                        // Regular function without captures - use to_fn_named for stack traces
                        strbuf_append_format(tp->code_buf, "to_fn_named(");
                        // use _w wrapper for functions with non-Item ABI so fn_call* works correctly
                        if (needs_fn_call_wrapper(fn_node)) {
                            write_fn_name_ex(tp->code_buf, fn_node,
                                (AstImportNode*)ident_node->entry->import, "_w");
                        } else {
                            write_fn_name(tp->code_buf, fn_node,
                                (AstImportNode*)ident_node->entry->import);
                        }
                        strbuf_append_format(tp->code_buf, ",%d,", arity);
                        // pass function name as string literal for stack traces
                        if (fn_node->name && fn_node->name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
                            strbuf_append_char(tp->code_buf, '"');
                            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
                            strbuf_append_char(tp->code_buf, '"');
                        } else {
                            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
                        }
                        strbuf_append_char(tp->code_buf, ')');
                    }
                }
                else if (entry_node->node_type == AST_NODE_STRING_PATTERN || entry_node->node_type == AST_NODE_SYMBOL_PATTERN) {
                    // Pattern reference - emit const_pattern call
                    AstPatternDefNode* pattern_def = (AstPatternDefNode*)entry_node;
                    TypePattern* pattern_type = (TypePattern*)pattern_def->type;
                    log_debug("transpile_primary_expr: pattern reference '%.*s', index=%d",
                        (int)ident_node->name->len, ident_node->name->chars, pattern_type->pattern_index);
                    strbuf_append_format(tp->code_buf, "const_pattern(%d)", pattern_type->pattern_index);
                }
                else {
                    log_debug("transpile_primary_expr: writing var name for %.*s, entry type: %d",
                        (int)ident_node->name->len, ident_node->name->chars,
                        ident_node->entry->node->type->type_id);

                    // For decimal identifiers, we need to convert the pointer to an Item
                    if (ident_node->entry->node->type->type_id == LMD_TYPE_DECIMAL) {
                        strbuf_append_str(tp->code_buf, "c2it(");
                        write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node,
                            (AstImportNode*)ident_node->entry->import);
                        strbuf_append_char(tp->code_buf, ')');
                    } else {
                        write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node,
                            (AstImportNode*)ident_node->entry->import);
                    }
                }
            }
            else {
                // handle the case where entry is null - identifier is undefined
                // emit error value instead of raw identifier name to prevent crash
                log_error("Error: undefined identifier '%.*s'", (int)ident_node->name->len, ident_node->name->chars);
                tp->error_count++;
                strbuf_append_str(tp->code_buf, "ItemError");
            }
        } else {
            transpile_expr(tp, pri_node->expr);
        }
    } else { // const
        log_debug("transpile_primary_expr: const");
        if (pri_node->type->is_literal) {  // literal
            if (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL ||
                pri_node->type->type_id == LMD_TYPE_BINARY) {
                // loads the const string without boxing
                strbuf_append_str(tp->code_buf, "const_s(");
                TypeString *str_type = (TypeString*)pri_node->type;
                strbuf_append_int(tp->code_buf, str_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_DTIME) {
                // loads the const datetime without boxing
                strbuf_append_str(tp->code_buf, "const_k(");
                TypeDateTime *dt_type = (TypeDateTime*)pri_node->type;
                strbuf_append_int(tp->code_buf, dt_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT) {
                write_node_source(tp, pri_node->node);
                // int32 literals don't use 'L' suffix
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT64) {
                write_node_source(tp, pri_node->node);
                strbuf_append_char(tp->code_buf, 'L');  // add 'L' to ensure it is a long
            }
            else if (pri_node->type->type_id == LMD_TYPE_FLOAT) {
                TypeFloat *f_type = (TypeFloat*)pri_node->type;
                double val = f_type->double_val;
                if (__builtin_isinf(val)) {
                    // inf keyword: emit C constant expression for infinity
                    strbuf_append_str(tp->code_buf, "(1.0/0.0)");
                } else if (__builtin_isnan(val)) {
                    // nan keyword: emit C constant expression for NaN
                    strbuf_append_str(tp->code_buf, "(0.0/0.0)");
                } else {
                    // regular numeric float literal: emit source text directly
                    strbuf_append_str(tp->code_buf, "((double)(");
                    write_node_source(tp, pri_node->node);
                    strbuf_append_str(tp->code_buf, "))");
                }
            }
            else if (pri_node->type->type_id == LMD_TYPE_DECIMAL) {
                // loads the const decimal without boxing
                strbuf_append_str(tp->code_buf, "const_c2it(");
                TypeDecimal *dec_type = (TypeDecimal*)pri_node->type;
                strbuf_append_int(tp->code_buf, dec_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_NULL) {
                // null literals (including empty strings "") should be transpiled as ITEM_NULL
                strbuf_append_str(tp->code_buf, "ITEM_NULL");
            }
            else { // bool, float
                write_node_source(tp, pri_node->node);
            }
        } else {
            write_node_source(tp, pri_node->node);
        }
    }
}

void transpile_unary_expr(Transpiler* tp, AstUnaryNode *unary_node) {
    log_debug("transpile unary expr");
    // TCO: unary operand is NOT in tail position
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    if (unary_node->op == OPERATOR_NOT) {
        TypeId operand_type = unary_node->operand->type->type_id;
        if (operand_type == LMD_TYPE_BOOL) {
            // direct C negation for boolean values
            strbuf_append_str(tp->code_buf, "!");
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_not(");
            transpile_box_item(tp, unary_node->operand);
            strbuf_append_str(tp->code_buf, ")");
        }
    }
    else if (unary_node->op == OPERATOR_IS_ERROR) {
        // ^expr shorthand for (expr is error) — checks if item is error type
        strbuf_append_str(tp->code_buf, "(item_type_id(");
        transpile_box_item(tp, unary_node->operand);
        strbuf_append_str(tp->code_buf, ")==LMD_TYPE_ERROR)");
    }
    else if (unary_node->op == OPERATOR_POS || unary_node->op == OPERATOR_NEG) {
        TypeId operand_type = unary_node->operand->type->type_id;

        // Fast path for numeric types that can be handled directly by C
        if (operand_type == LMD_TYPE_INT || operand_type == LMD_TYPE_INT64 ||
            operand_type == LMD_TYPE_FLOAT) {
            // Direct C operator application for primitive numeric types
            if (unary_node->op == OPERATOR_POS) {
                // Unary + can be optimized away for numeric types
                strbuf_append_char(tp->code_buf, '(');
                transpile_expr(tp, unary_node->operand);
                strbuf_append_char(tp->code_buf, ')');
            } else { // OPERATOR_NEG
                // two brackets to prevent '-' joining into '--'
                strbuf_append_str(tp->code_buf, "(-(");
                transpile_expr(tp, unary_node->operand);
                strbuf_append_str(tp->code_buf, "))");
            }
        }
        else {
            // Runtime function call for other types (ANY, DECIMAL, etc.)
            if (unary_node->op == OPERATOR_POS) {
                strbuf_append_str(tp->code_buf, "fn_pos(");
            } else { // OPERATOR_NEG
                strbuf_append_str(tp->code_buf, "fn_neg(");
            }
            transpile_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else {
        // Fallback for unknown operators (should not happen with proper AST)
        log_error("Error: transpile_unary_expr unknown operator %d", unary_node->op);
        strbuf_append_str(tp->code_buf, "null");
    }

    tp->in_tail_position = prev_in_tail;
}

// transpile spread expression: *expr
// wraps the expression with item_spread() to mark it as spreadable
void transpile_spread_expr(Transpiler* tp, AstUnaryNode *spread_node) {
    log_debug("transpile spread expr");
    strbuf_append_str(tp->code_buf, "item_spread(");
    transpile_box_item(tp, spread_node->operand);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    // TCO: operands of binary expressions are NOT in tail position
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    TypeId left_type = bi_node->left->type->type_id;
    TypeId right_type = bi_node->right->type->type_id;
    if (bi_node->op == OPERATOR_AND || bi_node->op == OPERATOR_OR) {
        // Check if we need type error checking for mixed types
        if (left_type != LMD_TYPE_BOOL || right_type != LMD_TYPE_BOOL) {
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "fn_and(");
            } else {
                strbuf_append_str(tp->code_buf, "fn_or(");
            }
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // slightly faster path for bool && bool
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "op_and(");
            } else {
                strbuf_append_str(tp->code_buf, "op_or(");
            }
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_POW) {
        // Use unboxed pow when both operands are numeric
        if (is_numeric_type(left_type) && is_numeric_type(right_type)) {
            strbuf_append_str(tp->code_buf, "push_d(fn_pow_u((double)(");
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, "),(double)(");
            transpile_expr(tp, bi_node->right);
            strbuf_append_str(tp->code_buf, ")))");
        } else {
            // Fall back to boxed version for non-numeric or unknown types
            strbuf_append_str(tp->code_buf, "fn_pow(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_ADD) {
        if (left_type == right_type) {
            if (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, '+');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            // else let fn_add() handle it
        }
        else if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '+');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_add()
        strbuf_append_str(tp->code_buf, "fn_add(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_SUB) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '-');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_sub()
        strbuf_append_str(tp->code_buf, "fn_sub(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_MUL) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '*');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_mul()
        strbuf_append_str(tp->code_buf, "fn_mul(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_MOD) {
        // Always use boxed fn_mod() for proper division-by-zero error handling
        // Cannot use unboxed fn_mod_i since it cannot return error for b == 0
        strbuf_append_str(tp->code_buf, "fn_mod(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_DIV) {
        if (LMD_TYPE_INT <= left_type && left_type <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= right_type && right_type <= LMD_TYPE_FLOAT) {
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, ")/(double)(");
            transpile_expr(tp, bi_node->right);
            strbuf_append_str(tp->code_buf, "))");
            return;
        }
        // call runtime fn_div()
        strbuf_append_str(tp->code_buf, "fn_div(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_IDIV) {
        // Always use boxed fn_idiv() for proper division-by-zero error handling
        // Cannot use unboxed fn_idiv_i since it cannot return error for b == 0
        strbuf_append_str(tp->code_buf, "fn_idiv(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_IS) {
        // Check if right operand is a constrained type for inline constraint evaluation
        AstNode* right = bi_node->right;
        // Unwrap primary node
        if (right->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)right;
            if (pri->expr) right = pri->expr;
        }

        // Check if the type is a constrained type (directly or via TypeType wrapper)
        AstConstrainedTypeNode* constrained_node = nullptr;
        if (right->node_type == AST_NODE_CONSTRAINED_TYPE) {
            constrained_node = (AstConstrainedTypeNode*)right;
        } else if (right->type && right->type->kind == TYPE_KIND_CONSTRAINED) {
            // The identifier's type is directly the TypeConstrained
            // We need the AST node to get the constraint expression
            // Look up the original type definition
            if (right->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident = (AstIdentNode*)right;
                if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
                    if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                        constrained_node = (AstConstrainedTypeNode*)type_def->as;
                    }
                }
            }
        } else if (right->type && right->type->type_id == LMD_TYPE_TYPE) {
            // Check if it's a TypeType wrapping a TypeConstrained
            TypeType* type_type = (TypeType*)right->type;
            if (type_type->type && type_type->type->kind == TYPE_KIND_CONSTRAINED) {
                // Look up the original type definition to get constraint AST
                if (right->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident = (AstIdentNode*)right;
                    if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
                        AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
                        if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                            constrained_node = (AstConstrainedTypeNode*)type_def->as;
                        }
                    }
                }
            }
        }

        if (constrained_node) {
            // Inline constrained type check: (base_type_check && constraint_check)
            TypeConstrained* constrained = (TypeConstrained*)constrained_node->type;

            strbuf_append_str(tp->code_buf, "({\n");
            strbuf_append_str(tp->code_buf, "  Item _ct_value = ");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, ";\n");
            strbuf_append_str(tp->code_buf, "  Item _pipe_item = _ct_value;\n");  // for ~ in constraint

            // Check base type using base_type() function
            strbuf_append_str(tp->code_buf, "  Bool _ct_result = (item_type_id(_ct_value) == ");
            strbuf_append_int(tp->code_buf, constrained->base->type_id);
            strbuf_append_str(tp->code_buf, ");\n");

            // If base type matches, evaluate constraint
            strbuf_append_str(tp->code_buf, "  if (_ct_result) {\n");
            strbuf_append_str(tp->code_buf, "    _ct_result = is_truthy(");
            transpile_box_item(tp, constrained_node->constraint);
            strbuf_append_str(tp->code_buf, ") ? BOOL_TRUE : BOOL_FALSE;\n");
            strbuf_append_str(tp->code_buf, "  }\n");
            strbuf_append_str(tp->code_buf, "  _ct_result;\n");
            strbuf_append_str(tp->code_buf, "})");
        } else {
            // Standard fn_is call
            strbuf_append_str(tp->code_buf, "fn_is(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_IN) {
        strbuf_append_str(tp->code_buf, "fn_in(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_TO) {
        strbuf_append_str(tp->code_buf, "fn_to(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_EQ) {
        // Use native C == for numeric/bool types, fn_eq for others (strings, containers, etc.)
        if (can_use_native_comparison(bi_node, true)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " == ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_eq(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_NE) {
        // Use native C != for numeric/bool types, fn_ne for others
        if (can_use_native_comparison(bi_node, true)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " != ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_ne(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LT) {
        // Use native C < for numeric types (not bool), fn_lt for others
        if (can_use_native_comparison(bi_node, false)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " < ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_lt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LE) {
        // Use native C <= for numeric types (not bool), fn_le for others
        if (can_use_native_comparison(bi_node, false)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " <= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_le(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GT) {
        // Use native C > for numeric types (not bool), fn_gt for others
        if (can_use_native_comparison(bi_node, false)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " > ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_gt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GE) {
        // Use native C >= for numeric types (not bool), fn_ge for others
        if (can_use_native_comparison(bi_node, false)) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " >= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_ge(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_JOIN) {
        strbuf_append_str(tp->code_buf, "fn_join(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else {
        log_error("Error: unknown binary operator %d", bi_node->op);
        strbuf_append_str(tp->code_buf, "null");  // should be error
    }

    tp->in_tail_position = prev_in_tail;
}

// for both if_expr and if_stam
void transpile_if(Transpiler* tp, AstIfNode *if_node) {
    log_debug("transpile if expr");

    // Check types for proper conditional expression handling
    Type* if_type = if_node->type;
    Type* then_type = if_node->then ? if_node->then->type : nullptr;
    Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;

    // TCO: condition is NOT in tail position, branches inherit tail position
    bool prev_in_tail = tp->in_tail_position;

    strbuf_append_str(tp->code_buf, "(");
    // For boolean-typed conditions (comparisons, etc.), use expression directly since
    // comparison functions like fn_le return Bool which can be used as C boolean.
    // For non-boolean expressions, use is_truthy() to extract boolean from Item
    // (Direct Item booleans would fail because Item(false) = 0x200000000000000 is non-zero in C)
    tp->in_tail_position = false;  // condition is not tail
    if (if_node->cond->type && if_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, if_node->cond);
    }
    else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, if_node->cond);
        strbuf_append_str(tp->code_buf, ")");
    }
    tp->in_tail_position = prev_in_tail;  // restore for branches
    strbuf_append_str(tp->code_buf, " ? ");

    // Determine if branches have incompatible types that need coercion
    bool need_boxing = true;
    if (then_type && else_type && (then_type->type_id == else_type->type_id) && then_type->type_id != LMD_TYPE_ANY) {
        need_boxing = false;
    }
    if (need_boxing) {
        log_debug("transpile if expr with boxing");
        if (if_node->then) {
            transpile_box_item(tp, if_node->then);
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        }
        strbuf_append_str(tp->code_buf, " : ");
        if (if_node->otherwise) {
            transpile_box_item(tp, if_node->otherwise);
        } else {
            // otherwise is optional
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_str(tp->code_buf, ")");
    } else {
        // fast path without boxing
        log_debug("transpile if expr without boxing");
        transpile_expr(tp, if_node->then);
        strbuf_append_char(tp->code_buf, ':');
        if (if_node->otherwise) {
            transpile_expr(tp, if_node->otherwise);
        } else {
            log_warn("Warning: if_stam missing else clause");
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_char(tp->code_buf, ')');
    }
    log_debug("end if expr");
}

void transpile_assign_expr(Transpiler* tp, AstNamedNode *asn_node, bool is_global = false) {
    log_debug("transpile assign expr");
    // defensive validation: ensure all required pointers and components are valid
    if (!asn_node || !asn_node->type || !asn_node->as) {
        log_error("Error: asn_node is invalid");
        strbuf_append_str(tp->code_buf, "error");
        return;
    }

    // set assignment name context for closure naming
    String* prev_assign_name = tp->current_assign_name;
    tp->current_assign_name = asn_node->name;

    // error destructuring: let name^err_name = expr
    if (asn_node->error_name) {
        int tmp_id = tp->temp_var_counter++;
        // emit temp variable for the full result (may be value or error)
        strbuf_append_format(tp->code_buf, "\n Item _et%d=", tmp_id);
        transpile_expr(tp, asn_node->as);
        strbuf_append_char(tp->code_buf, ';');

        // emit value variable: null if error, value otherwise
        strbuf_append_str(tp->code_buf, "\n ");
        if (!is_global) { strbuf_append_str(tp->code_buf, "Item "); }
        write_var_name(tp->code_buf, asn_node, NULL);
        strbuf_append_format(tp->code_buf, "=(item_type_id(_et%d)==LMD_TYPE_ERROR)?ITEM_NULL:_et%d;", tmp_id, tmp_id);

        // emit error variable: error if error, null otherwise
        strbuf_append_str(tp->code_buf, "\n ");
        if (!is_global) { strbuf_append_str(tp->code_buf, "Item "); }
        strbuf_append_str(tp->code_buf, "_");
        strbuf_append_str_n(tp->code_buf, asn_node->error_name->chars, asn_node->error_name->len);
        strbuf_append_format(tp->code_buf, "=(item_type_id(_et%d)==LMD_TYPE_ERROR)?_et%d:ITEM_NULL;", tmp_id, tmp_id);

        tp->current_assign_name = prev_assign_name;
        return;
    }

    // declare the variable type
    Type *var_type = asn_node->type;
    strbuf_append_str(tp->code_buf, "\n ");
    if (!is_global) {
        write_type(tp->code_buf, var_type);
        strbuf_append_char(tp->code_buf, ' ');
    }
    write_var_name(tp->code_buf, asn_node, NULL);
    strbuf_append_char(tp->code_buf, '=');

    // coerce Item → native scalar when variable type is scalar but RHS returns Item
    TypeId var_tid = var_type->type_id;
    TypeId rhs_tid = asn_node->as->type ? asn_node->as->type->type_id : LMD_TYPE_ANY;
    const char* unbox_fn = NULL;
    if (var_tid != rhs_tid && (rhs_tid == LMD_TYPE_ANY || rhs_tid == LMD_TYPE_NULL)) {
        if (var_tid == LMD_TYPE_FLOAT) unbox_fn = "it2d(";
        else if (var_tid == LMD_TYPE_INT) unbox_fn = "it2i(";
        else if (var_tid == LMD_TYPE_INT64) unbox_fn = "it2l(";
        else if (var_tid == LMD_TYPE_BOOL) unbox_fn = "it2b(";
    }
    if (unbox_fn) strbuf_append_str(tp->code_buf, unbox_fn);
    transpile_expr(tp, asn_node->as);
    if (unbox_fn) strbuf_append_char(tp->code_buf, ')');

    // restore previous context
    tp->current_assign_name = prev_assign_name;

    strbuf_append_char(tp->code_buf, ';');
}

// Transpile decomposition: let a, b = expr OR let a, b at expr
void transpile_decompose_expr(Transpiler* tp, AstDecomposeNode *dec_node, bool is_global = false) {
    log_debug("transpile decompose expr, name_count=%d, is_named=%d", dec_node->name_count, dec_node->is_named);

    if (!dec_node || !dec_node->as || dec_node->name_count == 0) {
        log_error("Error: invalid decompose node");
        return;
    }

    // For local scope: declare variables first, then use nested scope for temp
    if (!is_global) {
        // Declare all variables outside the helper scope
        for (int i = 0; i < dec_node->name_count; i++) {
            String* name = dec_node->names[i];
            strbuf_append_str(tp->code_buf, "\n Item _");
            strbuf_append_str_n(tp->code_buf, name->chars, name->len);
            strbuf_append_str(tp->code_buf, ";");
        }
    }

    // Use a nested scope to avoid _dec_src redeclaration conflicts
    strbuf_append_str(tp->code_buf, "\n {Item _dec_src=");
    transpile_box_item(tp, dec_node->as);
    strbuf_append_str(tp->code_buf, ";");

    // Assign values inside the nested scope
    for (int i = 0; i < dec_node->name_count; i++) {
        String* name = dec_node->names[i];
        strbuf_append_str(tp->code_buf, "\n _");
        strbuf_append_str_n(tp->code_buf, name->chars, name->len);
        strbuf_append_char(tp->code_buf, '=');

        if (dec_node->is_named) {
            // Named decomposition: get attribute by name
            strbuf_append_str(tp->code_buf, "item_attr(_dec_src,\"");
            strbuf_append_str_n(tp->code_buf, name->chars, name->len);
            strbuf_append_str(tp->code_buf, "\");");
        } else {
            // Positional decomposition: get by index
            strbuf_append_format(tp->code_buf, "item_at(_dec_src,%d);", i);
        }
    }
    strbuf_append_str(tp->code_buf, "}");  // close the nested scope
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node, bool is_global = false) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!let_node) { log_error("Error: missing let_node");  return; }

    AstNode *declare = let_node->declare;
    while (declare) {
        // Handle both regular assignments and decompositions
        if (declare->node_type == AST_NODE_ASSIGN) {
            transpile_assign_expr(tp, (AstNamedNode*)declare, is_global);
        } else if (declare->node_type == AST_NODE_DECOMPOSE) {
            transpile_decompose_expr(tp, (AstDecomposeNode*)declare, is_global);
        } else {
            log_error("Error: transpile_let_stam found unexpected node type %d in declare chain", declare->node_type);
        }
        declare = declare->next;
    }
}

void transpile_loop_expr(Transpiler* tp, AstLoopNode *loop_node, AstNode* then, bool use_array) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!loop_node || !loop_node->as || !loop_node->as->type || !then) {
        log_error("Error: invalid loop_node");
        return;
    }
    Type * expr_type = loop_node->as->type;
    Type *item_type = nullptr;
    bool is_named = loop_node->is_named;  // 'at' keyword for attribute/named iteration

    // determine loop item type based on expression type and iteration mode
    if (is_named) {
        // 'at' iteration: iterating over attributes/fields
        item_type = &TYPE_ANY;  // attribute values are always ANY
    } else if (expr_type->type_id == LMD_TYPE_ARRAY) {
        TypeArray* array_type = (TypeArray*)expr_type;
        // Validate that the cast is safe by checking the nested pointer
        if (array_type && array_type->nested && (uintptr_t)array_type->nested > 0x1000) {
            item_type = array_type->nested;
        } else {
            log_warn("Warning: Invalid nested type in array, using TYPE_ANY");
            item_type = &TYPE_ANY;
        }
    } else if (expr_type->type_id == LMD_TYPE_RANGE) {
        item_type = &TYPE_INT;
    } else {
        item_type = &TYPE_ANY;
    }

    if (!item_type) {
        log_error("Error: transpile_loop_expr failed to determine item type");
        item_type = &TYPE_ANY; // fallback to ANY type for safety
    }

    // Helper: generate index variable declaration if present
    bool has_index = loop_node->index_name != nullptr;

    if (is_named) {
        // 'at' iteration: iterate over attributes/fields
        // for k, v at expr -> k is key name, v is value
        // for v at expr -> v is key name (single variable form)
        strbuf_append_str(tp->code_buf, " Item it=");
        transpile_box_item(tp, loop_node->as);
        strbuf_append_str(tp->code_buf, ";\n ArrayList* _attr_keys=item_keys(it);\n");
        strbuf_append_str(tp->code_buf, " for (int _ki=0; _attr_keys && _ki<_attr_keys->length; _ki++) {\n");

        if (has_index) {
            // Two-variable form: for k, v at expr
            // k (index_name) = key name, v (name) = value
            strbuf_append_str(tp->code_buf, "  String* _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");

            strbuf_append_str(tp->code_buf, "  Item _");
            strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
            strbuf_append_str(tp->code_buf, "=item_attr(it, _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "->chars);\n");
        } else {
            // Single-variable form: for v at expr -> v is key name
            strbuf_append_str(tp->code_buf, "  String* _");
            strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
            strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");
        }
    } else {
        // 'in' iteration: standard indexed iteration
        // Check if it's any array type (typed or generic)
        // Note: AST uses LMD_TYPE_ARRAY with nested field for typed arrays
        bool is_generic_array = (expr_type->type_id == LMD_TYPE_ARRAY);
        TypeId nested_type_id = LMD_TYPE_ANY;
        if (is_generic_array) {
            TypeArray* arr_type = (TypeArray*)expr_type;
            if (arr_type && arr_type->nested) {
                nested_type_id = arr_type->nested->type_id;
            }
        }

        bool is_typed_array = (expr_type->type_id == LMD_TYPE_ARRAY_INT ||
                               expr_type->type_id == LMD_TYPE_ARRAY_INT64 ||
                               expr_type->type_id == LMD_TYPE_ARRAY_FLOAT);
        bool is_any_array = is_typed_array || is_generic_array;

        // Select the appropriate array pointer type
        const char* arr_decl;
        if (expr_type->type_id == LMD_TYPE_RANGE) {
            arr_decl = " Range *rng=";
        } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT || nested_type_id == LMD_TYPE_INT) {
            arr_decl = " ArrayInt *arr=";
        } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT64 || nested_type_id == LMD_TYPE_INT64) {
            arr_decl = " ArrayInt64 *arr=";
        } else if (expr_type->type_id == LMD_TYPE_ARRAY_FLOAT || nested_type_id == LMD_TYPE_FLOAT) {
            arr_decl = " ArrayFloat *arr=";
        } else if (is_generic_array) {
            arr_decl = " Array *arr=";
        } else {
            arr_decl = " Item it=";
        }
        strbuf_append_str(tp->code_buf, arr_decl);
        transpile_expr(tp, loop_node->as);

        // start the loop
        strbuf_append_str(tp->code_buf,
            expr_type->type_id == LMD_TYPE_RANGE ? ";\n if (!rng) { array_push(arr_out, ITEM_ERROR); } else { for (long _idx=rng->start; _idx<=rng->end; _idx++) {\n " :
            is_any_array ? ";\n if (!arr) { array_push(arr_out, ITEM_ERROR); } else { for (int _idx=0; _idx<arr->length; _idx++) {\n " :
            ";\n int ilen = fn_len(it);\n for (int _idx=0; _idx<ilen; _idx++) {\n ");

        // generate index variable if present (for i, v in expr)
        if (has_index) {
            strbuf_append_str(tp->code_buf, "  long _");
            strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
            strbuf_append_str(tp->code_buf, "=_idx;\n");
        }

        // construct loop variable
        write_type(tp->code_buf, item_type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
        if (expr_type->type_id == LMD_TYPE_RANGE) {
            strbuf_append_str(tp->code_buf, "=_idx;\n");
        }
        else if (is_any_array) {
            if (item_type->type_id == LMD_TYPE_STRING) {
                strbuf_append_str(tp->code_buf, "=fn_string(arr->items[_idx]);\n");
            }
            else {
                strbuf_append_str(tp->code_buf, "=arr->items[_idx];\n");
            }
        }
        else {
            strbuf_append_str(tp->code_buf, "=item_at(it,_idx);\n");
        }
    }

    // nested loop variables
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        log_debug("transpile nested loop");
        log_enter();
        transpile_loop_expr(tp, (AstLoopNode*)next_loop, then, use_array);
        log_leave();
    }
    else { // loop body
        Type *then_type = then->type;
        // push to array (spreadable) or list
        if (use_array) {
            strbuf_append_str(tp->code_buf, " array_push(arr_out,");
        } else {
            strbuf_append_str(tp->code_buf, " list_push(ls,");
        }
        transpile_box_item(tp, then);
        strbuf_append_str(tp->code_buf, ");");
    }
    // end the loop
    bool is_any_array_type = (expr_type->type_id == LMD_TYPE_ARRAY_INT ||
                              expr_type->type_id == LMD_TYPE_ARRAY_INT64 ||
                              expr_type->type_id == LMD_TYPE_ARRAY_FLOAT ||
                              expr_type->type_id == LMD_TYPE_ARRAY);
    if (!is_named && (expr_type->type_id == LMD_TYPE_RANGE || is_any_array_type)) {
        strbuf_append_char(tp->code_buf, '}');
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

// Helper: transpile a where condition check
void transpile_where_check(Transpiler* tp, AstNode* where_expr) {
    strbuf_append_str(tp->code_buf, "  if (!is_truthy(");
    transpile_box_item(tp, where_expr);
    strbuf_append_str(tp->code_buf, ")) continue;\n");
}

// Helper: transpile let clause bindings
// Generate typed variables like for-loop variables to allow proper arithmetic
void transpile_let_clauses(Transpiler* tp, AstNode* let_clause) {
    AstNode* current = let_clause;
    while (current) {
        AstNamedNode* let_node = (AstNamedNode*)current;
        if (!let_node->as) {
            log_error("transpile_let_clauses: let_node->as is null for %.*s",
                (int)let_node->name->len, let_node->name->chars);
            current = current->next;
            continue;
        }
        Type* value_type = let_node->as->type ? let_node->as->type : &TYPE_ANY;

        // Generate typed variable declaration like for-loop does
        strbuf_append_str(tp->code_buf, "  ");
        write_type(tp->code_buf, value_type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, let_node->name->chars, let_node->name->len);
        strbuf_append_str(tp->code_buf, " = ");
        transpile_expr(tp, let_node->as);  // unboxed expression
        strbuf_append_str(tp->code_buf, ";\n");
        current = current->next;
    }
}

// Helper: count order specs for comparison function
int count_order_specs(AstNode* order) {
    int count = 0;
    AstNode* current = order;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}

// both for_expr and for_stam
void transpile_for(Transpiler* tp, AstForNode *for_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!for_node || !for_node->then || !for_node->then->type) {
        log_error("Error: invalid for_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    Type *then_type = for_node->then->type;

    bool has_where = for_node->where != NULL;
    bool has_order = for_node->order != NULL;
    bool has_group = for_node->group != NULL;
    bool has_limit = for_node->limit != NULL;
    bool has_offset = for_node->offset != NULL;
    bool has_let = for_node->let_clause != NULL;

    // GROUP BY not yet fully implemented
    if (has_group) {
        log_error("Error: GROUP BY clause not yet implemented");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    // init a spreadable array for for-expression results
    strbuf_append_str(tp->code_buf, "({\n Array* arr_out=array_spreadable(); \n");

    // If order by is present, allocate keys array AFTER array_spreadable()
    // inside the frame so it gets cleaned up by frame_end() in array_end()
    // fn_sort_by_keys runs before array_end, so arr_keys is still alive when needed
    if (has_order) {
        strbuf_append_str(tp->code_buf, " Array* arr_keys=array_plain();\n");
    }

    // No GROUP BY - simpler path with optional where/let
    AstNode *loop = for_node->loop;
        if (loop) {
            // Generate loop iterations
            AstLoopNode* loop_node = (AstLoopNode*)loop;
            Type* expr_type = loop_node->as->type ? loop_node->as->type : &TYPE_ANY;
            bool is_named = loop_node->is_named;

            if (is_named) {
                // 'at' iteration: iterate over attributes/fields
                strbuf_append_str(tp->code_buf, " Item it=");
                transpile_box_item(tp, loop_node->as);
                strbuf_append_str(tp->code_buf, ";\n ArrayList* _attr_keys=item_keys(it);\n");
                strbuf_append_str(tp->code_buf, " for (int _ki=0; _attr_keys && _ki<_attr_keys->length; _ki++) {\n");

                if (loop_node->index_name) {
                    // Two-variable form: for k, v at expr
                    // k (index_name) = key name (String*), v (name) = value (Item)
                    strbuf_append_str(tp->code_buf, "  String* _");
                    strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
                    strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");

                    strbuf_append_str(tp->code_buf, "  Item _");
                    strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
                    strbuf_append_str(tp->code_buf, "=item_attr(it, _");
                    strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
                    strbuf_append_str(tp->code_buf, "->chars);\n");
                } else {
                    // Single-variable form: for v at expr -> v is key name
                    strbuf_append_str(tp->code_buf, "  String* _");
                    strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
                    strbuf_append_str(tp->code_buf, "=_attr_keys->data[_ki];\n");
                }
            } else {
                // 'in' iteration: standard indexed iteration
                // Check if it's any array type (typed or generic)
                // Note: AST uses LMD_TYPE_ARRAY with nested field for typed arrays
                bool is_generic_array = (expr_type->type_id == LMD_TYPE_ARRAY);
                TypeId nested_type_id = LMD_TYPE_ANY;
                if (is_generic_array) {
                    TypeArray* arr_type = (TypeArray*)expr_type;
                    if (arr_type && arr_type->nested) {
                        nested_type_id = arr_type->nested->type_id;
                    }
                }

                bool is_typed_array = (expr_type->type_id == LMD_TYPE_ARRAY_INT ||
                                       expr_type->type_id == LMD_TYPE_ARRAY_INT64 ||
                                       expr_type->type_id == LMD_TYPE_ARRAY_FLOAT);
                // Also check if it's a generic array with typed nested elements
                bool is_nested_typed = is_generic_array && (nested_type_id == LMD_TYPE_INT ||
                                                            nested_type_id == LMD_TYPE_INT64 ||
                                                            nested_type_id == LMD_TYPE_FLOAT);
                bool is_any_array = is_typed_array || is_generic_array;

                // Select the appropriate array pointer type
                const char* arr_decl;
                if (expr_type->type_id == LMD_TYPE_RANGE) {
                    arr_decl = " Range *rng=";
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT || nested_type_id == LMD_TYPE_INT) {
                    arr_decl = " ArrayInt *arr=";
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT64 || nested_type_id == LMD_TYPE_INT64) {
                    arr_decl = " ArrayInt64 *arr=";
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_FLOAT || nested_type_id == LMD_TYPE_FLOAT) {
                    arr_decl = " ArrayFloat *arr=";
                } else if (is_generic_array) {
                    arr_decl = " Array *arr=";
                } else {
                    arr_decl = " Item it=";
                }
                strbuf_append_str(tp->code_buf, arr_decl);
                transpile_expr(tp, loop_node->as);

                // Start the loop
                strbuf_append_str(tp->code_buf,
                    expr_type->type_id == LMD_TYPE_RANGE ? ";\n if (!rng) { array_push(arr_out, ITEM_ERROR); } else { for (long _idx=rng->start; _idx<=rng->end; _idx++) {\n " :
                    is_any_array ? ";\n if (!arr) { array_push(arr_out, ITEM_ERROR); } else { for (int _idx=0; _idx<arr->length; _idx++) {\n " :
                    ";\n int ilen = fn_len(it);\n for (int _idx=0; _idx<ilen; _idx++) {\n ");

                // Index variable if present
                if (loop_node->index_name) {
                    strbuf_append_str(tp->code_buf, "  long _");
                    strbuf_append_str_n(tp->code_buf, loop_node->index_name->chars, loop_node->index_name->len);
                    strbuf_append_str(tp->code_buf, "=_idx;\n");
                }

                // Construct loop variable type
                Type* item_type = &TYPE_ANY;
                if (expr_type->type_id == LMD_TYPE_ARRAY) {
                    TypeArray* array_type = (TypeArray*)expr_type;
                    if (array_type && array_type->nested) item_type = array_type->nested;
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_FLOAT) {
                    item_type = &TYPE_FLOAT;
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT) {
                    item_type = &TYPE_INT;
                } else if (expr_type->type_id == LMD_TYPE_ARRAY_INT64) {
                    item_type = &TYPE_INT64;
                } else if (expr_type->type_id == LMD_TYPE_RANGE) {
                    item_type = &TYPE_INT;
                }

                write_type(tp->code_buf, item_type);
                strbuf_append_str(tp->code_buf, " _");
                strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
                if (expr_type->type_id == LMD_TYPE_RANGE) {
                    strbuf_append_str(tp->code_buf, "=_idx;\n");
                } else if (is_any_array) {
                    strbuf_append_str(tp->code_buf, "=arr->items[_idx];\n");
                } else {
                    strbuf_append_str(tp->code_buf, "=item_at(it,_idx);\n");
                }
            }

            // Handle nested loops
            AstNode* next_loop = loop_node->next;
            while (next_loop) {
                AstLoopNode* nl = (AstLoopNode*)next_loop;
                Type* nl_expr_type = nl->as->type ? nl->as->type : &TYPE_ANY;

                strbuf_append_str(tp->code_buf, " Item _nl_src=");
                transpile_box_item(tp, nl->as);
                strbuf_append_str(tp->code_buf, ";\n int _nl_len=fn_len(_nl_src);\n");
                strbuf_append_str(tp->code_buf, " for (int _nidx=0; _nidx<_nl_len; _nidx++) {\n");

                if (nl->index_name) {
                    strbuf_append_str(tp->code_buf, "  long _");
                    strbuf_append_str_n(tp->code_buf, nl->index_name->chars, nl->index_name->len);
                    strbuf_append_str(tp->code_buf, "=_nidx;\n");
                }

                strbuf_append_str(tp->code_buf, "  Item _");
                strbuf_append_str_n(tp->code_buf, nl->name->chars, nl->name->len);
                strbuf_append_str(tp->code_buf, "=item_at(_nl_src,_nidx);\n");

                next_loop = next_loop->next;
            }

            // Transpile let clauses
            if (has_let) {
                transpile_let_clauses(tp, for_node->let_clause);
            }

            // Transpile where clause
            if (has_where) {
                transpile_where_check(tp, for_node->where);
            }

            // Body - push to array (use spread to flatten nested spreadable arrays)
            strbuf_append_str(tp->code_buf, " array_push_spread(arr_out,");
            transpile_box_item(tp, for_node->then);
            strbuf_append_str(tp->code_buf, ");");

            // If order by is present, also push the sort key value
            if (has_order) {
                AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
                strbuf_append_str(tp->code_buf, " array_push(arr_keys,");
                transpile_box_item(tp, first_spec->expr);
                strbuf_append_str(tp->code_buf, ");");
            }

            // Close nested loops
            next_loop = loop_node->next;
            while (next_loop) {
                strbuf_append_str(tp->code_buf, " }\n");
                next_loop = next_loop->next;
            }

            // Close main loop - 'at' iteration only has single brace, 'in' may have extra
            bool is_any_array_type = (expr_type->type_id == LMD_TYPE_ARRAY_INT ||
                                      expr_type->type_id == LMD_TYPE_ARRAY_INT64 ||
                                      expr_type->type_id == LMD_TYPE_ARRAY_FLOAT ||
                                      expr_type->type_id == LMD_TYPE_ARRAY);
            if (!is_named && (expr_type->type_id == LMD_TYPE_RANGE || is_any_array_type)) {
                strbuf_append_char(tp->code_buf, '}');
            }
            strbuf_append_str(tp->code_buf, " }\n");
        }

    // Track if we've applied any post-processing that converts Array to List
    bool has_post_processing = has_order || has_offset || has_limit;

    // Apply ORDER BY if present - sort arr_out in-place by key expression
    if (has_order) {
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        // Sort arr_out in-place by the collected keys, with ascending/descending flag
        strbuf_append_str(tp->code_buf, " fn_sort_by_keys((Item)arr_out, (Item)arr_keys, ");
        strbuf_append_str(tp->code_buf, first_spec->descending ? "1" : "0");
        strbuf_append_str(tp->code_buf, ");\n");
    }

    if (has_order && (has_offset || has_limit)) {
        // When order by is present with offset/limit, apply them in-place on arr_out
        // This avoids fn_drop/fn_take which convert Array to List (Lists get flattened)
        if (has_offset) {
            strbuf_append_str(tp->code_buf, " array_drop_inplace(arr_out, ");
            transpile_box_item(tp, for_node->offset);
            strbuf_append_str(tp->code_buf, " & 0x00FFFFFFFFFFFFFF);\n");
        }
        if (has_limit) {
            strbuf_append_str(tp->code_buf, " array_limit_inplace(arr_out, ");
            transpile_box_item(tp, for_node->limit);
            strbuf_append_str(tp->code_buf, " & 0x00FFFFFFFFFFFFFF);\n");
        }
        // Finalize via array_end (handles frame_end + returns Item)
        strbuf_append_str(tp->code_buf, " array_end(arr_out);})");
    } else if (has_offset || has_limit) {
        // Without order by, use fn_drop/fn_take which return spreadable Lists
        // (frame_end first so results are allocated in outer frame)
        strbuf_append_str(tp->code_buf, " frame_end();\n");

        // Apply OFFSET if present
        if (has_offset) {
            strbuf_append_str(tp->code_buf, " Item _offset = fn_drop((Item)arr_out, ");
            transpile_box_item(tp, for_node->offset);
            strbuf_append_str(tp->code_buf, ");\n");
        }

        // Apply LIMIT if present
        if (has_limit) {
            if (has_offset) {
                strbuf_append_str(tp->code_buf, " Item _limited = fn_take(_offset, ");
            } else {
                strbuf_append_str(tp->code_buf, " Item _limited = fn_take((Item)arr_out, ");
            }
            transpile_box_item(tp, for_node->limit);
            strbuf_append_str(tp->code_buf, ");\n");
        }

        // return the result
        if (has_limit) {
            strbuf_append_str(tp->code_buf, " _limited;})");
        } else {
            strbuf_append_str(tp->code_buf, " _offset;})");
        }
    } else {
        // No offset/limit - use array_end to finalize the Array
        strbuf_append_str(tp->code_buf, " array_end(arr_out);})");
    }
}

// forward declaration
bool has_current_item_ref(AstNode* node);

// pipe expression: data | transform or data where condition
// Semantics:
// - With ~ in right side: auto-map over collection (or apply to scalar)
// - Without ~: pass whole collection as first argument to function
// - where: filter items where condition is truthy
// Implementation: inline the iteration loop similar to for-expression

// Helper: get call node if right side is a call expression (unwrapping primary if needed)
static AstCallNode* get_pipe_call_node(AstNode* right) {
    if (!right) return NULL;
    if (right->node_type == AST_NODE_CALL_EXPR) {
        return (AstCallNode*)right;
    }
    if (right->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)right;
        if (primary->expr && primary->expr->node_type == AST_NODE_CALL_EXPR) {
            return (AstCallNode*)primary->expr;
        }
    }
    return NULL;
}

// Helper: transpile a pipe with call injection: data | func(args) -> func(data, args)
// For calls with pipe_inject=true, the AST already looked up the correct N+1 arg function
static void transpile_pipe_call_inject(Transpiler* tp, AstNode* left, AstCallNode* call_node) {
    AstNode* fn_node = call_node->function;
    bool is_sys_func = (fn_node->node_type == AST_NODE_SYS_FUNC);

    if (is_sys_func) {
        AstSysFuncNode* sys_fn = (AstSysFuncNode*)fn_node;

        // If pipe_inject is set, the fn_info already points to the N+1 arg version
        // Just emit the call with left as first arg
        strbuf_append_str(tp->code_buf, sys_fn->fn_info->is_proc ? "pn_" : "fn_");
        strbuf_append_str(tp->code_buf, sys_fn->fn_info->name);
        if (sys_fn->fn_info->is_overloaded) {
            strbuf_append_int(tp->code_buf, sys_fn->fn_info->arg_count);
        }
        strbuf_append_char(tp->code_buf, '(');

        // First argument: the piped data
        transpile_box_item(tp, left);

        // Remaining arguments from original call
        AstNode* arg = call_node->argument;
        while (arg) {
            strbuf_append_str(tp->code_buf, ", ");
            transpile_box_item(tp, arg);
            arg = arg->next;
        }

        strbuf_append_char(tp->code_buf, ')');
    } else {
        // For user-defined functions, fall back to fn_pipe_call
        strbuf_append_str(tp->code_buf, "fn_pipe_call(");
        transpile_box_item(tp, left);
        strbuf_append_str(tp->code_buf, ", ");
        transpile_box_item(tp, (AstNode*)call_node);
        strbuf_append_char(tp->code_buf, ')');
    }
}

void transpile_pipe_expr(Transpiler* tp, AstPipeNode *pipe_node) {
    log_debug("transpile pipe expr");
    if (!pipe_node || !pipe_node->left || !pipe_node->right) {
        log_error("Error: invalid pipe_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    TypeId left_type = pipe_node->left->type ? pipe_node->left->type->type_id : LMD_TYPE_ANY;
    bool uses_current_item = has_current_item_ref(pipe_node->right);

    if (!uses_current_item && pipe_node->op == OPERATOR_PIPE) {
        // aggregate pipe: pass whole collection as first argument
        // Check if right side is a call expression - if so, inject left as first arg
        AstCallNode* call_node = get_pipe_call_node(pipe_node->right);
        if (call_node) {
            transpile_pipe_call_inject(tp, pipe_node->left, call_node);
            return;
        }

        // Otherwise, use runtime fn_pipe_call
        strbuf_append_str(tp->code_buf, "fn_pipe_call(");
        transpile_box_item(tp, pipe_node->left);
        strbuf_append_str(tp->code_buf, ", ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }

    // Uses ~ or ~# - need to iterate
    // Generate inline loop using statement expression
    // Use array instead of list to avoid string merging behavior
    strbuf_append_str(tp->code_buf, "({\n");
    strbuf_append_str(tp->code_buf, "  Item _pipe_collection = ");
    transpile_box_item(tp, pipe_node->left);
    strbuf_append_str(tp->code_buf, ";\n");
    strbuf_append_str(tp->code_buf, "  TypeId _pipe_type = item_type_id(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "  Array* _pipe_result = array();\n");

    // Check if collection type - if not, apply to single item
    strbuf_append_str(tp->code_buf, "  if (_pipe_type == LMD_TYPE_ARRAY || _pipe_type == LMD_TYPE_LIST || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_RANGE || _pipe_type == LMD_TYPE_MAP || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_ARRAY_INT || _pipe_type == LMD_TYPE_ARRAY_INT64 || ");
    strbuf_append_str(tp->code_buf, "_pipe_type == LMD_TYPE_ARRAY_FLOAT || _pipe_type == LMD_TYPE_ELEMENT) {\n");

    // Map case - iterate over key-value pairs
    strbuf_append_str(tp->code_buf, "    if (_pipe_type == LMD_TYPE_MAP) {\n");
    strbuf_append_str(tp->code_buf, "      ArrayList* _pipe_keys = item_keys(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "      if (_pipe_keys) {\n");
    strbuf_append_str(tp->code_buf, "        for (int64_t _pipe_i = 0; _pipe_i < _pipe_keys->length; _pipe_i++) {\n");
    strbuf_append_str(tp->code_buf, "          String* _key_str = (String*)_pipe_keys->data[_pipe_i];\n");
    strbuf_append_str(tp->code_buf, "          Item _pipe_index = s2it(_key_str);\n");
    strbuf_append_str(tp->code_buf, "          Item _pipe_item = item_attr(_pipe_collection, _key_str->chars);\n");

    if (pipe_node->op == OPERATOR_WHERE) {
        // filter - only keep if condition is truthy
        strbuf_append_str(tp->code_buf, "          if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "            array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "          }\n");
    } else {
        // map - transform and collect
        strbuf_append_str(tp->code_buf, "          array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "        }\n");
    strbuf_append_str(tp->code_buf, "        // Note: _pipe_keys memory managed by heap GC\n");
    strbuf_append_str(tp->code_buf, "      }\n");
    strbuf_append_str(tp->code_buf, "    } else {\n");

    // Array/List/Range case - iterate with numeric index
    strbuf_append_str(tp->code_buf, "      int64_t _pipe_len = fn_len(_pipe_collection);\n");
    strbuf_append_str(tp->code_buf, "      for (int64_t _pipe_i = 0; _pipe_i < _pipe_len; _pipe_i++) {\n");
    strbuf_append_str(tp->code_buf, "        Item _pipe_index = i2it(_pipe_i);\n");
    strbuf_append_str(tp->code_buf, "        Item _pipe_item = item_at(_pipe_collection, (int)_pipe_i);\n");

    if (pipe_node->op == OPERATOR_WHERE) {
        // filter
        strbuf_append_str(tp->code_buf, "        if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "          array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "        }\n");
    } else {
        // map
        strbuf_append_str(tp->code_buf, "        array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "      }\n");
    strbuf_append_str(tp->code_buf, "    }\n");
    strbuf_append_str(tp->code_buf, "  } else {\n");

    // Scalar case - apply transform once
    strbuf_append_str(tp->code_buf, "    Item _pipe_item = _pipe_collection;\n");
    strbuf_append_str(tp->code_buf, "    Item _pipe_index = ITEM_NULL;\n");

    if (pipe_node->op == OPERATOR_WHERE) {
        strbuf_append_str(tp->code_buf, "    if (is_truthy(");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ")) {\n");
        strbuf_append_str(tp->code_buf, "      array_push(_pipe_result, _pipe_item);\n");
        strbuf_append_str(tp->code_buf, "    }\n");
    } else {
        strbuf_append_str(tp->code_buf, "    array_push(_pipe_result, ");
        transpile_box_item(tp, pipe_node->right);
        strbuf_append_str(tp->code_buf, ");\n");
    }
    strbuf_append_str(tp->code_buf, "  }\n");

    // Return result - array_end finalizes and returns as Item
    strbuf_append_str(tp->code_buf, "  array_end(_pipe_result);\n");
    strbuf_append_str(tp->code_buf, "})");
}

// while statement (procedural only)
void transpile_while(Transpiler* tp, AstWhileNode *while_node) {
    log_debug("transpile while stam");
    if (!while_node || !while_node->cond || !while_node->body) {
        log_error("Error: invalid while_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    strbuf_append_str(tp->code_buf, "\nwhile (");
    // For boolean-typed conditions (comparisons, etc.), use expression directly since
    // comparison functions like fn_le return Bool which can be used as C boolean.
    // For non-boolean expressions, use is_truthy() to extract boolean from Item.
    if (while_node->cond->type && while_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, while_node->cond);
    } else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, while_node->cond);
        strbuf_append_str(tp->code_buf, ")");
    }
    strbuf_append_str(tp->code_buf, ") {");
    // MIR JIT workaround: track while loop depth so that native variable
    // assignments inside while loops use *(&x)=v pattern, preventing MIR's
    // optimizer from mishandling SSA destruction of swap patterns (see Mir_Bug.md)
    tp->while_depth++;
    // use procedural statements (no wrapper) for while body
    if (while_node->body->node_type == AST_NODE_CONTENT) {
        transpile_proc_statements(tp, (AstListNode*)while_node->body);
    } else {
        strbuf_append_str(tp->code_buf, "\n ");
        transpile_expr(tp, while_node->body);
        strbuf_append_char(tp->code_buf, ';');
    }
    tp->while_depth--;
    strbuf_append_str(tp->code_buf, "\n}");
}

// procedural if statement - generates C-style if/else blocks
// unlike transpile_if which generates ternary expressions, this supports
// statements like break, continue, return in the branches
void transpile_if_stam(Transpiler* tp, AstIfNode *if_node) {
    log_debug("transpile if stam (procedural)");
    if (!if_node || !if_node->cond) {
        log_error("Error: invalid if_node");
        return;
    }

    // TCO: condition is NOT in tail position
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    strbuf_append_str(tp->code_buf, "if (");
    // For boolean-typed conditions (comparisons, etc.), use expression directly since
    // comparison functions like fn_le return Bool which can be used as C boolean.
    // For non-boolean expressions, use is_truthy() to extract boolean from Item.
    if (if_node->cond->type && if_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, if_node->cond);
    } else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, if_node->cond);
        strbuf_append_char(tp->code_buf, ')');
    }

    // Restore tail position for branches (they inherit from parent)
    tp->in_tail_position = prev_in_tail;
    strbuf_append_str(tp->code_buf, ") {");

    // transpile then branch as procedural statements (no wrapper needed)
    if (if_node->then) {
        if (if_node->then->node_type == AST_NODE_CONTENT) {
            transpile_proc_statements(tp, (AstListNode*)if_node->then);
        } else if (if_node->then->node_type == AST_NODE_IF_STAM) {
            // nested if in then branch
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)if_node->then);
        } else {
            // single expression - just execute it
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_expr(tp, if_node->then);
            strbuf_append_char(tp->code_buf, ';');
        }
    }
    strbuf_append_str(tp->code_buf, "\n}");

    // transpile else branch if present
    if (if_node->otherwise) {
        strbuf_append_str(tp->code_buf, " else {");
        if (if_node->otherwise->node_type == AST_NODE_CONTENT) {
            transpile_proc_statements(tp, (AstListNode*)if_node->otherwise);
        } else if (if_node->otherwise->node_type == AST_NODE_IF_STAM) {
            // else if chain
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)if_node->otherwise);
        } else {
            // single expression
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_expr(tp, if_node->otherwise);
            strbuf_append_char(tp->code_buf, ';');
        }
        strbuf_append_str(tp->code_buf, "\n}");
    }
}

// helper: generate the C condition for a match arm pattern
// type patterns (LMD_TYPE_TYPE, etc.) → fn_is(scrutinee, type)
// value patterns (literals, variables) → fn_eq(scrutinee, value)
// constrained types → base type check + inline constraint evaluation
static void transpile_match_condition(Transpiler* tp, AstNode* pattern) {
    if (!pattern || !pattern->type) {
        strbuf_append_str(tp->code_buf, "1"); // fallback: always true
        return;
    }

    // union patterns (T | U): recursively generate OR conditions for each side
    // handles mixed patterns: type | type, range | range, literal | literal, etc.
    if (pattern->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* bi = (AstBinaryNode*)pattern;
        if (bi->op == OPERATOR_UNION) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_match_condition(tp, bi->left);
            strbuf_append_str(tp->code_buf, " || ");
            transpile_match_condition(tp, bi->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
    }

    // Check for constrained type (directly or via identifier)
    AstConstrainedTypeNode* constrained_node = nullptr;
    if (pattern->node_type == AST_NODE_CONSTRAINED_TYPE) {
        constrained_node = (AstConstrainedTypeNode*)pattern;
    } else if (pattern->node_type == AST_NODE_IDENT && pattern->type && pattern->type->kind == TYPE_KIND_CONSTRAINED) {
        // Identifier referencing a constrained type - look up the AST node
        AstIdentNode* ident = (AstIdentNode*)pattern;
        if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
            if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                constrained_node = (AstConstrainedTypeNode*)type_def->as;
            }
        }
    }

    if (constrained_node) {
        // Inline constrained type check: (base_type_check && constraint_check)
        TypeConstrained* constrained = (TypeConstrained*)constrained_node->type;

        strbuf_append_str(tp->code_buf, "({\n");
        // Note: _pipe_item already set by the match expression to the scrutinee value
        // Check base type first
        strbuf_append_str(tp->code_buf, "    Bool _ct_result = (item_type_id(_pipe_item) == ");
        strbuf_append_int(tp->code_buf, constrained->base->type_id);
        strbuf_append_str(tp->code_buf, ");\n");

        // If base type matches, evaluate constraint
        strbuf_append_str(tp->code_buf, "    if (_ct_result) {\n");
        strbuf_append_str(tp->code_buf, "      _ct_result = is_truthy(");
        transpile_box_item(tp, constrained_node->constraint);
        strbuf_append_str(tp->code_buf, ") ? BOOL_TRUE : BOOL_FALSE;\n");
        strbuf_append_str(tp->code_buf, "    }\n");
        strbuf_append_str(tp->code_buf, "    _ct_result;\n");
        strbuf_append_str(tp->code_buf, "  })");
        return;
    }

    TypeId pattern_type = pattern->type->type_id;

    // type patterns use fn_is (matches runtime type checking)
    if (pattern_type == LMD_TYPE_TYPE) {
        strbuf_append_str(tp->code_buf, "fn_is(_pipe_item, ");
        transpile_box_item(tp, pattern);
        strbuf_append_char(tp->code_buf, ')');
    }
    // range patterns use fn_in (membership/containment check)
    else if (pattern_type == LMD_TYPE_RANGE) {
        strbuf_append_str(tp->code_buf, "fn_in(_pipe_item, ");
        transpile_box_item(tp, pattern);
        strbuf_append_char(tp->code_buf, ')');
    }
    // value patterns use fn_eq (equality check)
    else {
        strbuf_append_str(tp->code_buf, "fn_eq(_pipe_item, ");
        transpile_box_item(tp, pattern);
        strbuf_append_str(tp->code_buf, ") == BOOL_TRUE");
    }
}

// match expression — generates C statement expression ({...}) with if-else chain
// handles both expression arms (case T: expr) and statement arms (case T { stmts })
void transpile_match(Transpiler* tp, AstMatchNode *match_node) {
    log_debug("transpile match expr");
    if (!match_node || !match_node->scrutinee || !match_node->first_arm) {
        log_error("Error: invalid match_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    // statement expression: evaluate scrutinee, chain if-else, produce result
    strbuf_append_str(tp->code_buf, "({\n");
    strbuf_append_str(tp->code_buf, "  Item _pipe_item = ");
    transpile_box_item(tp, match_node->scrutinee);
    strbuf_append_str(tp->code_buf, ";\n");
    strbuf_append_str(tp->code_buf, "  Item _match_result = ITEM_NULL;\n");

    bool first = true;
    AstMatchArm* arm = match_node->first_arm;
    while (arm) {
        if (arm->pattern) {
            // regular case arm
            if (first) {
                strbuf_append_str(tp->code_buf, "  if (");
                first = false;
            } else {
                strbuf_append_str(tp->code_buf, " else if (");
            }
            transpile_match_condition(tp, arm->pattern);
            strbuf_append_str(tp->code_buf, ") {\n");
            // handle both expression and statement arms
            if (arm->body && arm->body->node_type == AST_NODE_CONTENT) {
                transpile_proc_statements(tp, (AstListNode*)arm->body);
            } else {
                strbuf_append_str(tp->code_buf, "    _match_result = ");
                transpile_box_item(tp, arm->body);
                strbuf_append_str(tp->code_buf, ";\n");
            }
            strbuf_append_str(tp->code_buf, "  }");
        } else {
            // default arm
            if (first) {
                if (arm->body && arm->body->node_type == AST_NODE_CONTENT) {
                    transpile_proc_statements(tp, (AstListNode*)arm->body);
                } else {
                    strbuf_append_str(tp->code_buf, "  _match_result = ");
                    transpile_box_item(tp, arm->body);
                    strbuf_append_str(tp->code_buf, ";\n");
                }
            } else {
                strbuf_append_str(tp->code_buf, " else {\n");
                if (arm->body && arm->body->node_type == AST_NODE_CONTENT) {
                    transpile_proc_statements(tp, (AstListNode*)arm->body);
                } else {
                    strbuf_append_str(tp->code_buf, "    _match_result = ");
                    transpile_box_item(tp, arm->body);
                    strbuf_append_str(tp->code_buf, ";\n");
                }
                strbuf_append_str(tp->code_buf, "  }");
            }
        }
        arm = (AstMatchArm*)arm->next;
    }

    strbuf_append_str(tp->code_buf, "\n  _match_result;\n})");
    log_debug("end transpile match expr");
}

// match statement — generates C if-else chain inside a block scope
void transpile_match_stam(Transpiler* tp, AstMatchNode *match_node) {
    log_debug("transpile match stam");
    if (!match_node || !match_node->scrutinee || !match_node->first_arm) {
        log_error("Error: invalid match_node");
        return;
    }

    strbuf_append_str(tp->code_buf, "\n{");
    strbuf_append_str(tp->code_buf, "\n  Item _pipe_item = ");
    transpile_box_item(tp, match_node->scrutinee);
    strbuf_append_str(tp->code_buf, ";");

    bool first = true;
    AstMatchArm* arm = match_node->first_arm;
    while (arm) {
        if (arm->pattern) {
            if (first) {
                strbuf_append_str(tp->code_buf, "\n  if (");
                first = false;
            } else {
                strbuf_append_str(tp->code_buf, " else if (");
            }
            transpile_match_condition(tp, arm->pattern);
            strbuf_append_str(tp->code_buf, ") {");

            // transpile arm body as procedural statements
            if (arm->body && arm->body->node_type == AST_NODE_CONTENT) {
                transpile_proc_statements(tp, (AstListNode*)arm->body);
            } else if (arm->body) {
                strbuf_append_str(tp->code_buf, "\n    ");
                transpile_expr(tp, arm->body);
                strbuf_append_char(tp->code_buf, ';');
            }
            strbuf_append_str(tp->code_buf, "\n  }");
        } else {
            // default arm
            if (!first) {
                strbuf_append_str(tp->code_buf, " else {");
            } else {
                strbuf_append_str(tp->code_buf, "\n  {");
            }
            if (arm->body && arm->body->node_type == AST_NODE_CONTENT) {
                transpile_proc_statements(tp, (AstListNode*)arm->body);
            } else if (arm->body) {
                strbuf_append_str(tp->code_buf, "\n    ");
                transpile_expr(tp, arm->body);
                strbuf_append_char(tp->code_buf, ';');
            }
            strbuf_append_str(tp->code_buf, "\n  }");
        }
        arm = (AstMatchArm*)arm->next;
    }

    strbuf_append_str(tp->code_buf, "\n}");
    log_debug("end transpile match stam");
}

// return statement (procedural only)
void transpile_return(Transpiler* tp, AstReturnNode *return_node) {
    log_debug("transpile return stam");

    // TCO: return value is in tail position — if this is a recursive call,
    // it can be converted to a goto jump instead of a function call.
    bool prev_in_tail = tp->in_tail_position;
    if (tp->tco_func) {
        tp->in_tail_position = true;
    }

    strbuf_append_str(tp->code_buf, "\nreturn ");
    if (return_node->value) {
        // check if enclosing function returns native type (not Item)
        // if so, don't box — the raw value is what the C function returns
        bool func_returns_native = false;
        if (tp->current_func_node) {
            TypeFunc* fn_type = (TypeFunc*)tp->current_func_node->type;
            Type *ret = fn_type ? fn_type->returned : nullptr;
            if (ret && !tp->current_func_node->captures && !fn_type->can_raise) {
                TypeId rt = ret->type_id;
                func_returns_native = (rt == LMD_TYPE_INT || rt == LMD_TYPE_INT64 ||
                                      rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_BOOL);
            }
        }
        if (func_returns_native) {
            transpile_expr(tp, return_node->value);
        } else {
            transpile_box_item(tp, return_node->value);
        }
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    strbuf_append_char(tp->code_buf, ';');

    tp->in_tail_position = prev_in_tail;
}

// raise statement - returns an error value from function
// For now, raise expr simply returns expr (the error value)
// Future: validate expr is error type, set error metadata
void transpile_raise(Transpiler* tp, AstRaiseNode *raise_node) {
    log_debug("transpile raise stam");
    strbuf_append_str(tp->code_buf, "\nreturn ");
    if (raise_node->value) {
        transpile_box_item(tp, raise_node->value);
    } else {
        // raise with no value - return generic error
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
    }
    strbuf_append_char(tp->code_buf, ';');
}

// pipe-to-file statement (procedural only): |> and |>>
void transpile_pipe_file_stam(Transpiler* tp, AstBinaryNode *pipe_node) {
    log_debug("transpile pipe file stam");
    if (!pipe_node || !pipe_node->left || !pipe_node->right) {
        log_error("Error: invalid pipe_file_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    if (pipe_node->op == OPERATOR_PIPE_APPEND) {
        // Use pn_output_append for |>> (append mode)
        strbuf_append_str(tp->code_buf, "pn_output_append(");
        transpile_box_item(tp, pipe_node->left);  // source data
        strbuf_append_str(tp->code_buf, ", ");
        transpile_box_item(tp, pipe_node->right);  // file path
        strbuf_append_char(tp->code_buf, ')');
    } else {
        // Use pn_output2 for |> (write mode, default)
        strbuf_append_str(tp->code_buf, "pn_output2(");
        transpile_box_item(tp, pipe_node->left);  // source data
        strbuf_append_str(tp->code_buf, ", ");
        transpile_box_item(tp, pipe_node->right);  // file path
        strbuf_append_char(tp->code_buf, ')');
    }
}

// assignment statement for mutable variables (procedural only)
void transpile_assign_stam(Transpiler* tp, AstAssignStamNode *assign_node) {
    log_debug("transpile assign stam");
    if (!assign_node || !assign_node->target || !assign_node->value) {
        log_error("Error: invalid assign_node");
        return;
    }

    // MIR JIT workaround: inside while loops, use _store_i64(&_var, value) or
    // _store_f64(&_var, value) for native scalar types. These are external runtime
    // functions that MIR can't inline or reorder, preventing the lost-copy SSA bug.
    bool use_store_func = false;
    const char* store_fn = NULL;
    if (tp->while_depth > 0 && assign_node->target_node && assign_node->target_node->type) {
        TypeId tid = assign_node->target_node->type->type_id;
        if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_BOOL) {
            use_store_func = true;
            store_fn = "_store_i64";
        } else if (tid == LMD_TYPE_FLOAT) {
            use_store_func = true;
            store_fn = "_store_f64";
        }
    }

    if (use_store_func) {
        strbuf_append_format(tp->code_buf, "\n %s(&_", store_fn);
        strbuf_append_str_n(tp->code_buf, assign_node->target->chars, assign_node->target->len);
        strbuf_append_str(tp->code_buf, ",");
    } else {
        strbuf_append_str(tp->code_buf, "\n _");  // add underscore prefix for variable name
        strbuf_append_str_n(tp->code_buf, assign_node->target->chars, assign_node->target->len);
        strbuf_append_char(tp->code_buf, '=');
    }

    // If target variable has Item type (e.g., was declared with var x = null),
    // we need to box the value properly
    bool needs_boxing = false;
    if (assign_node->target_node && assign_node->target_node->type) {
        TypeId target_type = assign_node->target_node->type->type_id;
        // NULL type variables become Item type in C, so need to box values assigned to them
        if (target_type == LMD_TYPE_NULL || target_type == LMD_TYPE_ANY) {
            needs_boxing = true;
        }
    }

    if (needs_boxing) {
        transpile_box_item(tp, assign_node->value);
    } else {
        // coerce Item → native scalar when target type is scalar but value returns Item
        const char* unbox_fn = NULL;
        if (assign_node->target_node && assign_node->target_node->type && assign_node->value->type) {
            TypeId target_tid = assign_node->target_node->type->type_id;
            TypeId val_tid = assign_node->value->type->type_id;
            if (target_tid != val_tid && (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL)) {
                if (target_tid == LMD_TYPE_FLOAT) unbox_fn = "it2d(";
                else if (target_tid == LMD_TYPE_INT) unbox_fn = "it2i(";
                else if (target_tid == LMD_TYPE_INT64) unbox_fn = "it2l(";
                else if (target_tid == LMD_TYPE_BOOL) unbox_fn = "it2b(";
            }
        }
        if (unbox_fn) strbuf_append_str(tp->code_buf, unbox_fn);
        transpile_expr(tp, assign_node->value);
        if (unbox_fn) strbuf_append_char(tp->code_buf, ')');
    }
    // close with ) for store function call, or ; for regular assignment
    strbuf_append_str(tp->code_buf, use_store_func ? ");" : ";");
}

// compound assignment: arr[i] = val → fn_array_set(arr, i, val)
void transpile_index_assign_stam(Transpiler* tp, AstCompoundAssignNode *node) {
    log_debug("transpile index assign stam");
    if (!node || !node->object || !node->key || !node->value) {
        log_error("Error: invalid index assign node");
        return;
    }

    // check if object type is a typed array with known element type
    TypeId obj_type = node->object->type ? node->object->type->type_id : LMD_TYPE_ANY;

    strbuf_append_str(tp->code_buf, "\n fn_array_set((Array*)(");
    transpile_expr(tp, node->object);
    strbuf_append_str(tp->code_buf, "),(int)(");
    transpile_expr(tp, node->key);
    strbuf_append_str(tp->code_buf, "),");
    transpile_box_item(tp, node->value);
    strbuf_append_str(tp->code_buf, ");");
}

// compound assignment: obj.field = val → fn_map_set(obj, key, val)
void transpile_member_assign_stam(Transpiler* tp, AstCompoundAssignNode *node) {
    log_debug("transpile member assign stam");
    if (!node || !node->object || !node->key || !node->value) {
        log_error("Error: invalid member assign node");
        return;
    }

    strbuf_append_str(tp->code_buf, "\n fn_map_set(");
    transpile_box_item(tp, node->object);
    strbuf_append_str(tp->code_buf, ",");
    // key is an AstIdentNode — emit as string constant
    if (node->key->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node->key;
        String* key_name = ident->name;
        // find or add to const_list
        int const_index = -1;
        for (int i = 0; i < tp->const_list->length; i++) {
            String* s = (String*)tp->const_list->data[i];
            if (s == key_name) {
                const_index = i;
                break;
            }
        }
        if (const_index < 0) {
            arraylist_append(tp->const_list, key_name);
            const_index = tp->const_list->length - 1;
        }
        strbuf_append_format(tp->code_buf, "const_s2it(%d)", const_index);
    } else {
        transpile_box_item(tp, node->key);
    }
    strbuf_append_str(tp->code_buf, ",");
    transpile_box_item(tp, node->value);
    strbuf_append_str(tp->code_buf, ");");
}

void transpile_items(Transpiler* tp, AstNode *item) {
    bool is_first = true;
    while (item) {
        // skip let declaration and pattern definitions
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM ||
            item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;  continue;
        }
        if (is_first) { is_first = false; }
        else { strbuf_append_str(tp->code_buf, ", "); }
        transpile_box_item(tp, item);
        item = item->next;
    }
}

// check if any item in the list/array needs spreading (for-expression or spread operator)
static bool has_spreadable_item(AstNode *item) {
    while (item) {
        if (item->node_type == AST_NODE_FOR_EXPR || item->node_type == AST_NODE_SPREAD) {
            return true;
        }
        item = item->next;
    }
    return false;
}

void transpile_array_expr(Transpiler* tp, AstArrayNode *array_node) {
    TypeArray *type = (TypeArray*)array_node->type;
    bool is_int_array = type->nested && type->nested->type_id == LMD_TYPE_INT;
    bool is_int64_array = type->nested && type->nested->type_id == LMD_TYPE_INT64;
    bool is_float_array = type->nested && type->nested->type_id == LMD_TYPE_FLOAT;

    // for arrays with spreadable items (for-expressions), use push path
    if (!is_int_array && !is_int64_array && !is_float_array && has_spreadable_item(array_node->item)) {
        strbuf_append_str(tp->code_buf, "({\n Array* arr = array();\n");
        AstNode* item = array_node->item;
        while (item) {
            strbuf_append_str(tp->code_buf, " array_push_spread(arr, ");
            transpile_box_item(tp, item);
            strbuf_append_str(tp->code_buf, ");\n");
            item = item->next;
        }
        // return arr as Array* (consistent with non-spreadable path which returns array_fill result)
        // boxing to Item will be done by transpile_box_item when needed
        strbuf_append_str(tp->code_buf, " arr; })");
        return;
    }

    if (is_int_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt* arr = array_int(); array_int_fill(arr,");
    } else if (is_int64_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt64* arr = array_int64(); array_int64_fill(arr,");
    } else if (is_float_array) {
        strbuf_append_str(tp->code_buf, "({ArrayFloat* arr = array_float(); array_float_fill(arr,");
    } else {
        strbuf_append_str(tp->code_buf, "({Array* arr = array(); array_fill(arr,");
    }

    strbuf_append_int(tp->code_buf, type->length);
    // only add comma if there are items to follow
    if (array_node->item) {
        strbuf_append_char(tp->code_buf, ',');
    }

    if (is_int_array || is_int64_array || is_float_array) {
        AstNode *item = array_node->item;
        while (item) {
            // transpile as unboxed items
            transpile_expr(tp, item);
            if (item->next) {
                strbuf_append_char(tp->code_buf, ',');
            }
            item = item->next;
        }
    } else {
        // transpile as boxed items
        transpile_items(tp, array_node->item);
    }
    strbuf_append_str(tp->code_buf, "); })");
}

void transpile_list_expr(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile list expr: dec - %p, itm - %p", list_node->declare, list_node->item);
    // Defensive validation: ensure all required pointers and components are valid
    if (!list_node || !list_node->type || !list_node->list_type) {
        log_error("Error: invalid list_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    TypeArray *type = list_node->list_type;
    log_debug("transpile_list_expr: type->length = %ld", type->length);

    // block expression optimization: when there's exactly one value expression
    // and declarations (let bindings), emit as statement expression that evaluates
    // to the single value directly, instead of wrapping in a list.
    // e.g., (let x = 10, x) → ({ int64_t _x = 10; _x; })
    // This is critical for typed functions where returning List* as int64_t causes errors.
    if (type->length == 1 && list_node->declare) {
        strbuf_append_str(tp->code_buf, "({\n");
        // emit declarations
        AstNode *declare = list_node->declare;
        while (declare) {
            if (declare->node_type != AST_NODE_ASSIGN) {
                log_error("Error: transpile_list_expr found non-assign node in declare chain");
                declare = declare->next;
                continue;
            }
            transpile_assign_expr(tp, (AstNamedNode*)declare);
            strbuf_append_str(tp->code_buf, "\n");
            declare = declare->next;
        }
        // emit the single value expression as the result
        // use transpile_box_item to ensure proper Item boxing, since list expressions
        // are expected to return Item-typed values
        AstNode *item = list_node->item;
        if (item) {
            transpile_box_item(tp, item);
            strbuf_append_str(tp->code_buf, ";})");
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_NULL;})");
        }
        return;
    }

    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();\n");
    // let declare first
    AstNode *declare = list_node->declare;
    while (declare) {
        if (declare->node_type != AST_NODE_ASSIGN) {
            log_error("Error: transpile_list_expr found non-assign node in declare chain");
            // Skip invalid node - defensive recovery
            declare = declare->next;
            continue;
        }
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        strbuf_append_str(tp->code_buf, "\n");  // ensure declarations are on separate lines
        declare = declare->next;
    }
    if (type->length == 0) {
        log_debug("transpile_list_expr: type->length is 0, outputting null");
        strbuf_append_str(tp->code_buf, " list_end(ls);})");
        return;
    }
    // use push path if there are spreadable items (for-expressions) or many items
    if (type->length < 10 && !has_spreadable_item(list_node->item)) {
        strbuf_append_str(tp->code_buf, "\n list_fill(ls,");
        strbuf_append_int(tp->code_buf, type->length);
        strbuf_append_char(tp->code_buf, ',');
        transpile_items(tp, list_node->item);
        strbuf_append_str(tp->code_buf, ");})");
    }
    else {
        transpile_push_items(tp, list_node->item, false);
    }
}

// Helper to transpile procedural content as statements (no statement expression wrapper)
// Used for if-else blocks where we don't need a return value
void transpile_proc_statements(Transpiler* tp, AstListNode *list_node) {
    if (!list_node) return;

    AstNode *item = list_node->item;
    while (item) {
        // handle declarations
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_VAR_STAM) {
            transpile_let_stam(tp, (AstLetNode*)item, false);
        }
        else if (item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM ||
                 item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR ||
                 item->node_type == AST_NODE_PROC ||
                 item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            // skip - already handled globally or pattern definitions
        }
        else if (item->node_type == AST_NODE_WHILE_STAM) {
            transpile_while(tp, (AstWhileNode*)item);
        }
        else if (item->node_type == AST_NODE_BREAK_STAM) {
            strbuf_append_str(tp->code_buf, "\n break;");
        }
        else if (item->node_type == AST_NODE_CONTINUE_STAM) {
            strbuf_append_str(tp->code_buf, "\n continue;");
        }
        else if (item->node_type == AST_NODE_RETURN_STAM) {
            transpile_return(tp, (AstReturnNode*)item);
        }
        else if (item->node_type == AST_NODE_RAISE_STAM) {
            transpile_raise(tp, (AstRaiseNode*)item);
        }
        else if (item->node_type == AST_NODE_ASSIGN_STAM) {
            transpile_assign_stam(tp, (AstAssignStamNode*)item);
        }
        else if (item->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            transpile_index_assign_stam(tp, (AstCompoundAssignNode*)item);
        }
        else if (item->node_type == AST_NODE_MEMBER_ASSIGN_STAM) {
            transpile_member_assign_stam(tp, (AstCompoundAssignNode*)item);
        }
        else if (item->node_type == AST_NODE_FOR_STAM) {
            transpile_for(tp, (AstForNode*)item);
        }
        else if (item->node_type == AST_NODE_PIPE_FILE_STAM) {
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_pipe_file_stam(tp, (AstBinaryNode*)item);
            strbuf_append_char(tp->code_buf, ';');
        }
        else if (item->node_type == AST_NODE_IF_STAM) {
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)item);
        }
        else if (item->node_type == AST_NODE_MATCH_EXPR) {
            transpile_match_stam(tp, (AstMatchNode*)item);
        }
        else {
            // other expressions - just execute for side effects
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ';');
        }
        item = item->next;
    }
}

void transpile_proc_content(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile proc content");
    if (!list_node) { log_error("Error: missing list_node");  return; }

    // TCO: procedural content is NOT in tail position by default.
    // Only return statement values are in tail position.
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    AstNode *item = list_node->item;
    AstNode *last_item = NULL;

    // find the last non-declaration item for return value
    AstNode *scan = item;
    while (scan) {
        if (scan->node_type != AST_NODE_LET_STAM && scan->node_type != AST_NODE_PUB_STAM &&
            scan->node_type != AST_NODE_TYPE_STAM && scan->node_type != AST_NODE_FUNC &&
            scan->node_type != AST_NODE_FUNC_EXPR && scan->node_type != AST_NODE_PROC &&
            scan->node_type != AST_NODE_VAR_STAM &&
            scan->node_type != AST_NODE_STRING_PATTERN && scan->node_type != AST_NODE_SYMBOL_PATTERN) {
            last_item = scan;
        }
        scan = scan->next;
    }

    // determine if enclosing function returns native type (not Item)
    bool returns_native = false;
    if (tp->current_func_node) {
        TypeFunc* fn_type = (TypeFunc*)tp->current_func_node->type;
        Type *ret = fn_type ? fn_type->returned : nullptr;
        if (ret && !tp->current_func_node->captures && !fn_type->can_raise) {
            TypeId rt = ret->type_id;
            returns_native = (rt == LMD_TYPE_INT || rt == LMD_TYPE_INT64 ||
                              rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_BOOL);
        }
    }

    strbuf_append_str(tp->code_buf, "({\n ");
    if (returns_native) {
        // use the native C type for result variable
        TypeFunc* fn_type = (TypeFunc*)tp->current_func_node->type;
        write_type(tp->code_buf, fn_type->returned);
        strbuf_append_str(tp->code_buf, " result = 0;");
    } else {
        strbuf_append_str(tp->code_buf, "Item result = ITEM_NULL;");
    }

    while (item) {
        // handle declarations
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_VAR_STAM) {
            transpile_let_stam(tp, (AstLetNode*)item, false);
        }
        else if (item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM ||
                 item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR ||
                 item->node_type == AST_NODE_PROC ||
                 item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            // skip - already handled globally or pattern definitions
        }
        else if (item->node_type == AST_NODE_WHILE_STAM) {
            transpile_while(tp, (AstWhileNode*)item);
        }
        else if (item->node_type == AST_NODE_BREAK_STAM) {
            strbuf_append_str(tp->code_buf, "\n break;");
        }
        else if (item->node_type == AST_NODE_CONTINUE_STAM) {
            strbuf_append_str(tp->code_buf, "\n continue;");
        }
        else if (item->node_type == AST_NODE_RETURN_STAM) {
            transpile_return(tp, (AstReturnNode*)item);
        }
        else if (item->node_type == AST_NODE_RAISE_STAM) {
            transpile_raise(tp, (AstRaiseNode*)item);
        }
        else if (item->node_type == AST_NODE_ASSIGN_STAM) {
            transpile_assign_stam(tp, (AstAssignStamNode*)item);
        }
        else if (item->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            transpile_index_assign_stam(tp, (AstCompoundAssignNode*)item);
        }
        else if (item->node_type == AST_NODE_MEMBER_ASSIGN_STAM) {
            transpile_member_assign_stam(tp, (AstCompoundAssignNode*)item);
        }
        else if (item->node_type == AST_NODE_FOR_STAM) {
            transpile_for(tp, (AstForNode*)item);
        }
        else if (item->node_type == AST_NODE_IF_STAM) {
            // procedural if statement - use C-style if/else blocks
            strbuf_append_str(tp->code_buf, "\n ");
            transpile_if_stam(tp, (AstIfNode*)item);
        }
        else if (item->node_type == AST_NODE_MATCH_EXPR) {
            transpile_match_stam(tp, (AstMatchNode*)item);
        }
        else if (item->node_type == AST_NODE_IF_EXPR) {
            // if expression - wrap value (ternary style)
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
            }
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ';');
        }
        else if (item->node_type == AST_NODE_CALL_EXPR) {
            // call expression - capture result if last
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
            }
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ';');
        }
        else {
            // other expressions - capture if last
            if (item == last_item) {
                strbuf_append_str(tp->code_buf, "\n result = ");
                if (returns_native) {
                    transpile_expr(tp, item);
                } else {
                    transpile_box_item(tp, item);
                }
                strbuf_append_char(tp->code_buf, ';');
            } else {
                strbuf_append_str(tp->code_buf, "\n ");
                transpile_expr(tp, item);
                strbuf_append_char(tp->code_buf, ';');
            }
        }
        item = item->next;
    }

    strbuf_append_str(tp->code_buf, "\n result;})" );

    tp->in_tail_position = prev_in_tail;
}

void transpile_content_expr(Transpiler* tp, AstListNode *list_node, bool is_global = false) {
    log_debug("transpile content expr");
    TypeArray *type = list_node->list_type;

    // count effective (non-declaration) items first
    AstNode *item = list_node->item;
    int effective_length = type->length;
    AstNode *last_value_item = nullptr;
    AstNode *scan = list_node->item;
    while (scan) {
        if (scan->node_type == AST_NODE_LET_STAM || scan->node_type == AST_NODE_PUB_STAM || scan->node_type == AST_NODE_TYPE_STAM ||
            scan->node_type == AST_NODE_FUNC || scan->node_type == AST_NODE_FUNC_EXPR || scan->node_type == AST_NODE_PROC ||
            scan->node_type == AST_NODE_STRING_PATTERN || scan->node_type == AST_NODE_SYMBOL_PATTERN) {
            // declaration, not a value item
        } else {
            last_value_item = scan;
        }
        scan = scan->next;
    }

    // when there is exactly one value expression (block expression with let bindings),
    // emit as a statement expression that evaluates to that single value directly,
    // instead of wrapping in a list. This is critical for typed functions:
    // fn f() int { (let x = 10, x) } should return int64_t, not List*
    item = list_node->item;
    int decl_count = 0;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
            decl_count++;
        } else if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC) {
            decl_count++;
        }
        item = item->next;
    }
    effective_length = type->length - decl_count;

    if (effective_length == 1 && last_value_item && decl_count > 0 && !is_global
        && last_value_item->node_type != AST_NODE_FOR_EXPR) {
        // block expression: (let x = 10, x) → ({ int64_t _x = 10; _x; })
        // NOT applied at global scope (root content goes into Item result, needs list wrapping)
        // NOT applied for for-expressions (which produce spreadable arrays requiring list_push_spread)
        strbuf_append_str(tp->code_buf, "({");
        // emit declarations
        item = list_node->item;
        while (item) {
            if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
                if (is_global && !tp->is_main &&
                    (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM)) {
                    // skip - assignments generated in transpile_ast_root pre-pass
                } else {
                    transpile_let_stam(tp, (AstLetNode*)item, is_global);
                }
            }
            item = item->next;
        }
        // emit the single value expression as the result
        // use transpile_expr here (not transpile_box_item) because this is the
        // "native" path — for typed function returns, we need the raw scalar value.
        // when Item boxing is needed, the caller ensures it via transpile_box_item
        // on the content node, which is handled in transpile_box_item's LMD_TYPE_LIST case.
        strbuf_append_char(tp->code_buf, '\n');
        transpile_expr(tp, last_value_item);
        strbuf_append_str(tp->code_buf, ";})");
        return;
    }

    // multi-value list: create list to contain all the items
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();");
    // let declare first
    item = list_node->item;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
            // For modules: LET/PUB_STAM assignments are hoisted to top level of main()
            // to avoid MIR JIT optimizing away writes inside ({...}) statement expressions
            if (is_global && !tp->is_main &&
                (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM)) {
                // skip - assignments generated in transpile_ast_root pre-pass
            } else {
                transpile_let_stam(tp, (AstLetNode*)item, is_global);
            }
        }
        item = item->next;
    }
    if (effective_length == 0) {
        strbuf_append_str(tp->code_buf, "list_end(ls);})");
        return;
    }
    transpile_push_items(tp, list_node->item, false);
}

void transpile_map_expr(Transpiler* tp, AstMapNode *map_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!map_node) {
        log_error("Error: transpile_map_expr called with null map node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!map_node->type) {
        log_error("Error: transpile_map_expr missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    strbuf_append_str(tp->code_buf, "({Map* m = map(");
    strbuf_append_int(tp->code_buf, ((TypeMap*)map_node->type)->type_index);
    strbuf_append_str(tp->code_buf, ");");
    AstNode *item = map_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n map_fill(m,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_box_item(tp, key_expr->as);  // use box_item to wrap with i2it() etc.
                } else {
                    log_error("Error: transpile_map_expr key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_ERROR");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }
    else {
        strbuf_append_str(tp->code_buf, "m;");
    }
    strbuf_append_str(tp->code_buf, "})");
}

void transpile_element(Transpiler* tp, AstElementNode *elmt_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!elmt_node) {
        log_error("Error: transpile_element called with null element node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!elmt_node->type) {
        log_error("Error: transpile_element missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    strbuf_append_str(tp->code_buf, "\n({Element* el=elmt(");
    TypeElmt* type = (TypeElmt*)elmt_node->type;
    strbuf_append_int(tp->code_buf, type->type_index);
    strbuf_append_str(tp->code_buf, ");");

    // transpile the attributes
    AstNode *item = elmt_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n elmt_fill(el,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_box_item(tp, key_expr->as);  // use box_item to wrap with i2it() etc.
                } else {
                    log_error("Error: transpile_element key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_ERROR");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }

    // transpile the content items
    if (type->content_length) {
        if (type->content_length < 10) {
            strbuf_append_str(tp->code_buf, "\n list_fill(el,");
            strbuf_append_int(tp->code_buf, type->content_length);
            strbuf_append_char(tp->code_buf, ',');
            if (elmt_node->content) {
                transpile_items(tp, ((AstListNode*)elmt_node->content)->item);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
                strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            }
            strbuf_append_str(tp->code_buf, ");})");
        } else {
            if (elmt_node->content) {
                transpile_push_items(tp, ((AstListNode*)elmt_node->content)->item, true);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
                strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            }
        }
    }
    else { // no content
        if (elmt_node->item) {
            strbuf_append_str(tp->code_buf, " list_end(el);})");
        }
        else { // and no attr, thus no frame_end
            strbuf_append_str(tp->code_buf, " el;})");
        }
    }
}

// helper function to transpile a single argument with type coercion
void transpile_call_argument(Transpiler* tp, AstNode* arg, TypeParam* param_type, bool is_sys_func) {
    if (!arg) {
        // use default value if available
        if (param_type && param_type->default_value) {
            log_debug("using default value for param type=%d, default type=%d",
                param_type->type_id, param_type->default_value->type->type_id);
            // for optional params with default, box since function expects Item type
            if (param_type->is_optional) {
                transpile_box_item(tp, param_type->default_value);
            } else if (param_type->type_id == LMD_TYPE_ANY) {
                transpile_box_item(tp, param_type->default_value);
            } else {
                transpile_expr(tp, param_type->default_value);
            }
        } else {
            // No default value - use null/zero for missing parameters
            // For typed params, use native zero values to match the function's native param types
            if (param_type && !param_type->is_optional) {
                switch (param_type->type_id) {
                case LMD_TYPE_INT:
                case LMD_TYPE_INT64:
                    strbuf_append_str(tp->code_buf, "0");
                    break;
                case LMD_TYPE_FLOAT:
                    strbuf_append_str(tp->code_buf, "0.0");
                    break;
                case LMD_TYPE_BOOL:
                    strbuf_append_str(tp->code_buf, "0");
                    break;
                default:
                    strbuf_append_str(tp->code_buf, "ITEM_NULL");
                    break;
                }
            } else {
                strbuf_append_str(tp->code_buf, "ITEM_NULL");
            }
        }
        return;
    }

    // For named arguments, get the actual value
    AstNode* value = arg;
    if (arg->node_type == AST_NODE_NAMED_ARG) {
        AstNamedNode* named_arg = (AstNamedNode*)arg;
        value = named_arg->as;
    }

    log_debug("transpile_call_argument: type=%d, node_type=%d",
        value->type ? value->type->type_id : -1, value->node_type);

    // For system functions, box DateTime arguments
    if (is_sys_func && value->type->type_id == LMD_TYPE_DTIME) {
        log_debug("transpile_call_argument: BOXING DateTime for sys func");
        transpile_box_item(tp, value);
    }
    // for optional params, always box to Item since function expects Item type
    else if (param_type && param_type->is_optional) {
        transpile_box_item(tp, value);
    }
    // boxing based on arg type and fn definition type
    else if (param_type) {
        if (param_type->type_id == value->type->type_id) {
            transpile_expr(tp, value);
        }
        else if (param_type->type_id == LMD_TYPE_FLOAT) {
            if ((value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64 ||
                value->type->type_id == LMD_TYPE_FLOAT)) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2d(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT64) {
            if (value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int64_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2l(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int64 parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT) {
            if (value->type->type_id == LMD_TYPE_INT) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_INT64 || value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int64_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2i(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else {
            transpile_box_item(tp, value);
        }
    }
    else {
        transpile_box_item(tp, value);
    }
}

// find parameter by name in a function's parameter list
AstNamedNode* find_param_by_name(AstFuncNode* fn_node, String* name) {
    if (!name) return NULL;
    AstNamedNode* param = fn_node->param;
    while (param) {
        if (param->name && strcmp(param->name->chars, name->chars) == 0) {
            return param;
        }
        param = (AstNamedNode*)param->next;
    }
    return NULL;
}

// get parameter at index
AstNamedNode* get_param_at_index(AstFuncNode* fn_node, int index) {
    AstNamedNode* param = fn_node->param;
    for (int i = 0; i < index && param; i++) {
        param = (AstNamedNode*)param->next;
    }
    return param;
}

/**
 * Transpile a tail-recursive call as a goto statement.
 * Transforms: factorial(n-1, acc*n) into:
 *   { int _t0 = _n - 1; int _t1 = _acc * _n; _n = _t0; _acc = _t1; goto _tco_start; }
 *
 * Note: We use temporary variables to handle cases like f(b, a) where args are swapped.
 */
void transpile_tail_call(Transpiler* tp, AstCallNode* call_node, AstFuncNode* tco_func) {
    log_debug("transpile_tail_call: converting recursive call to goto");

    // Arguments are NOT in tail position (they're evaluated before the goto)
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    // Generate: ({ temp assignments; param = temp; ...; goto _tco_start; ITEM_NULL; })
    // The ITEM_NULL at end is never reached but satisfies the expression type
    strbuf_append_str(tp->code_buf, "({ ");

    // Count params and args
    int param_count = 0;
    AstNamedNode* param = tco_func->param;
    while (param) { param_count++; param = (AstNamedNode*)param->next; }

    // First pass: assign arguments to temporary variables
    // This handles cases like factorial(b, a) where we swap values
    AstNode* arg = call_node->argument;
    param = tco_func->param;
    int arg_idx = 0;

    while (arg && param) {
        // Generate: TypeX _tN = <arg>;
        Type* param_type = param->type;
        write_type(tp->code_buf, param_type);
        strbuf_append_format(tp->code_buf, " _tco_tmp%d = ", arg_idx);
        // For Item (untyped) parameters, use box_item to ensure proper boxing
        // (e.g., literal 1 must become i2it(1) for Item parameters)
        if (!param_type || param_type->type_id == LMD_TYPE_ANY) {
            transpile_box_item(tp, arg);
        } else {
            // For typed parameters, check if the argument's type is Item/ANY
            // and needs unboxing to match the parameter's native type
            TypeId arg_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            TypeId param_tid = param_type->type_id;
            if (arg_tid != param_tid && (arg_tid == LMD_TYPE_ANY || arg_tid == LMD_TYPE_NULL)) {
                // argument is Item but parameter is typed — unbox
                if (param_tid == LMD_TYPE_INT) {
                    strbuf_append_str(tp->code_buf, "it2i(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_INT64) {
                    strbuf_append_str(tp->code_buf, "it2l(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_FLOAT) {
                    strbuf_append_str(tp->code_buf, "it2d(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_BOOL) {
                    strbuf_append_str(tp->code_buf, "it2b(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else {
                    transpile_expr(tp, arg);
                }
            } else {
                transpile_expr(tp, arg);
            }
        }
        strbuf_append_str(tp->code_buf, "; ");

        arg = arg->next;
        param = (AstNamedNode*)param->next;
        arg_idx++;
    }

    // Second pass: assign temporaries to parameters
    param = tco_func->param;
    for (int i = 0; i < arg_idx; i++) {
        if (!param) break;
        strbuf_append_str(tp->code_buf, "_");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        strbuf_append_format(tp->code_buf, " = _tco_tmp%d; ", i);
        param = (AstNamedNode*)param->next;
    }

    // Restore tail position before generating the goto
    tp->in_tail_position = prev_in_tail;

    // Jump back to function start
    strbuf_append_str(tp->code_buf, "goto _tco_start; ");

    // The expression type - never reached but keeps C happy
    Type* ret_type = ((TypeFunc*)tco_func->type)->returned;
    if (!ret_type) ret_type = &TYPE_ANY;
    if (ret_type->type_id == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "0; })");
    } else if (ret_type->type_id == LMD_TYPE_FLOAT) {
        strbuf_append_str(tp->code_buf, "0.0; })");
    } else if (ret_type->type_id == LMD_TYPE_BOOL) {
        strbuf_append_str(tp->code_buf, "false; })");
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL; })");
    }
}

/**
 * Check if a call expression is a tail-recursive call to the TCO function.
 */
bool is_tco_tail_call(Transpiler* tp, AstCallNode* call_node) {
    if (!tp->tco_func || !tp->in_tail_position) return false;
    return is_recursive_call(call_node, tp->tco_func);
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    log_debug("transpile call expr");
    // Defensive validation: ensure all required pointers and components are valid
    if (!call_node || !call_node->function || !call_node->function->type) {
        log_error("Error: invalid call_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    // '?' propagation: emit opening wrapper before the call
    int prop_id = -1;
    if (call_node->propagate) {
        prop_id = tp->temp_var_counter;  // will be incremented at the end
        strbuf_append_format(tp->code_buf, "({Item _ep%d=", prop_id);
    }

    // TCO: Check if this is a tail-recursive call that should be transformed to goto
    if (is_tco_tail_call(tp, call_node)) {
        transpile_tail_call(tp, call_node, tp->tco_func);
        return;
    }

    // For non-TCO calls, arguments are NOT in tail position
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    // write the function name/ptr
    TypeFunc *fn_type = NULL;
    AstFuncNode* fn_node = NULL;  // used for named args lookup
    bool is_sys_func = (call_node->function->node_type == AST_NODE_SYS_FUNC);
    bool is_fn_variable = false;  // true if calling through a function variable (need fn_call)
    bool is_direct_call = true;   // true if we can use direct function call
    bool use_unboxed = false;     // true if calling the unboxed version (need to rebox result)
    TypeId unboxed_return_type = LMD_TYPE_ANY;  // return type of unboxed version for boxing

    if (is_sys_func) {
        AstSysFuncNode* sys_fn_node = (AstSysFuncNode*)call_node->function;
        AstNode* first_arg = call_node->argument;
        AstNode* second_arg = first_arg ? first_arg->next : NULL;
        const char* fn_name = sys_fn_node->fn_info->name;

        // ==== PRIORITY 1: Integer-specific unboxed functions ====
        // These take precedence over generic float-based native math
        if (first_arg && !first_arg->next && first_arg->type) {
            TypeId arg_type = first_arg->type->type_id;

            // Integer abs (prefer fn_abs_i over fabs for integers)
            if (strcmp(fn_name, "abs") == 0 && is_integer_type(arg_type)) {
                strbuf_append_str(tp->code_buf, "i2it(fn_abs_i((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, ")))");
                return;
            }

            // Sign function with integer arg
            if (strcmp(fn_name, "sign") == 0 && is_integer_type(arg_type)) {
                strbuf_append_str(tp->code_buf, "i2it(fn_sign_i((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, ")))");
                return;
            }

            // Integer floor/ceil/round (identity for integers)
            if ((strcmp(fn_name, "floor") == 0 || strcmp(fn_name, "ceil") == 0 ||
                 strcmp(fn_name, "round") == 0) && is_integer_type(arg_type)) {
                // For integers, floor/ceil/round is identity - just return the value boxed
                strbuf_append_str(tp->code_buf, "i2it((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }

            // bnot(a) — unary bitwise NOT
            if (strcmp(fn_name, "bnot") == 0) {
                strbuf_append_str(tp->code_buf, "fn_bnot((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
        }

        // ==== Bitwise binary functions: band, bor, bxor, shl, shr ====
        if (first_arg && second_arg && !second_arg->next) {
            if (strcmp(fn_name, "band") == 0) {
                strbuf_append_str(tp->code_buf, "fn_band((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "),(int64_t)(");
                transpile_expr(tp, second_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
            if (strcmp(fn_name, "bor") == 0) {
                strbuf_append_str(tp->code_buf, "fn_bor((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "),(int64_t)(");
                transpile_expr(tp, second_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
            if (strcmp(fn_name, "bxor") == 0) {
                strbuf_append_str(tp->code_buf, "fn_bxor((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "),(int64_t)(");
                transpile_expr(tp, second_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
            if (strcmp(fn_name, "shl") == 0) {
                strbuf_append_str(tp->code_buf, "fn_shl((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "),(int64_t)(");
                transpile_expr(tp, second_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
            if (strcmp(fn_name, "shr") == 0) {
                strbuf_append_str(tp->code_buf, "fn_shr((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "),(int64_t)(");
                transpile_expr(tp, second_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
        }

        // ==== PRIORITY 2: Native C math functions (single argument, double-based) ====
        const char* native_func = can_use_native_math(sys_fn_node, first_arg);

        if (native_func && first_arg && !first_arg->next) {
            // Use native C math: push_d(c_func((double)arg))
            // The result needs to be boxed since native math returns double
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, native_func);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, first_arg);
            strbuf_append_str(tp->code_buf, ")))");
            return;  // done - skip normal argument handling
        }

        // Check if we can use native two-argument math function (pow)
        const NativeMathFunc* native_binary_math = can_use_native_math_binary(sys_fn_node, first_arg, second_arg);
        if (native_binary_math && first_arg && second_arg && !second_arg->next) {
            // Use native binary math: push_d(fn_pow_u((double)arg1, (double)arg2))
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, native_binary_math->c_name);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, first_arg);
            strbuf_append_str(tp->code_buf, "),(double)(");
            transpile_expr(tp, second_arg);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }

        // Check if we can use native binary func (min/max with 2 args)
        const NativeBinaryFunc* native_binary = can_use_native_binary_func(sys_fn_node, first_arg, second_arg);
        if (native_binary && first_arg && second_arg && !second_arg->next) {
            // min/max with two numeric args: push_d(fn_min2_u((double)arg1, (double)arg2))
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, native_binary->c_name_float);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, first_arg);
            strbuf_append_str(tp->code_buf, "),(double)(");
            transpile_expr(tp, second_arg);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }

        // Check for remaining single-arg unboxed functions (neg, float sign)
        // Note: abs, int sign, and floor/ceil/round for int are handled in PRIORITY 1 above
        if (first_arg && !first_arg->next && first_arg->type) {
            TypeId arg_type = first_arg->type->type_id;

            // Negation (not in native_math_funcs)
            if (strcmp(fn_name, "neg") == 0) {
                if (is_integer_type(arg_type)) {
                    strbuf_append_str(tp->code_buf, "i2it(fn_neg_i((int64_t)(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_str(tp->code_buf, ")))");
                    return;
                } else if (arg_type == LMD_TYPE_FLOAT) {
                    strbuf_append_str(tp->code_buf, "push_d(fn_neg_f(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_str(tp->code_buf, "))");
                    return;
                }
            }

            // Float sign (integer sign handled in PRIORITY 1)
            if (strcmp(fn_name, "sign") == 0 && arg_type == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "i2it(fn_sign_f(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
        }

        // ==== VMap: map() and m.set(k, v) ====
        if (strcmp(fn_name, "map") == 0 && sys_fn_node->fn_info->fn == SYSFUNC_VMAP_NEW) {
            if (!first_arg) {
                // map() → vmap_new()
                strbuf_append_str(tp->code_buf, "vmap_new()");
            } else {
                // map([k1, v1, k2, v2, ...]) → vmap_from_array(arr)
                strbuf_append_str(tp->code_buf, "vmap_from_array(");
                transpile_box_item(tp, first_arg);
                strbuf_append_char(tp->code_buf, ')');
            }
            return;
        }
        if (sys_fn_node->fn_info->fn == SYSPROC_VMAP_SET) {
            // m.set(k, v) → vmap_set(m, k, v)
            strbuf_append_str(tp->code_buf, "vmap_set(");
            transpile_box_item(tp, first_arg);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, second_arg);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, second_arg->next);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }

        // Use the sys func name from fn_info, not from TSNode source
        // This correctly handles method-style calls (obj.method()) which are desugared to sys func calls
        strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->is_proc ? "pn_" : "fn_");
        strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->name);
        if (sys_fn_node->fn_info->is_overloaded) { strbuf_append_int(tp->code_buf, sys_fn_node->fn_info->arg_count); }
    }
    else {
        TypeId callee_type_id = call_node->function->type ? call_node->function->type->type_id : LMD_TYPE_NULL;

        // Check if callee is a parameter or variable that should be treated as a function
        // even if it doesn't have explicit function type (e.g., let add10 = make_adder(10); add10(5))
        bool is_callable_param = false;
        bool is_callable_variable = false;  // variable that might hold a function at runtime
        bool is_callable_call_expr = false; // callee is a call expression (e.g., make_adder(10)(5))
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ?
            (AstPrimaryNode*)call_node->function : null;

        // Early check for undefined function call - prevents crash
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            if (!ident_node->entry) {
                // Calling an undefined identifier - emit error
                log_error("Error: call to undefined function '%.*s'",
                    (int)ident_node->name->len, ident_node->name->chars);
                tp->error_count++;
                strbuf_append_str(tp->code_buf, "ItemError");
                return;
            }
        }

        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
            if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                // This is a parameter being used as a callable
                is_callable_param = true;
            }
            else if (entry_node && entry_node->node_type == AST_NODE_ASSIGN) {
                // This is a let variable being called - might hold a function at runtime
                // We can't statically know it's a function, but we'll use fn_call dynamically
                is_callable_variable = true;
                log_debug("callable variable (let) detected: %.*s",
                    (int)ident_node->name->len, ident_node->name->chars);
            }
        }

        // Check if callee is a call expression (chained call like make_adder(10)(5))
        // Note: call expressions may be wrapped in a primary expression
        if (call_node->function->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (direct)");
        }
        else if (primary_fn_node && primary_fn_node->expr &&
                 primary_fn_node->expr->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (wrapped in primary)");
        }

        if (callee_type_id == LMD_TYPE_FUNC || is_callable_param || is_callable_variable || is_callable_call_expr) {
            if (callee_type_id == LMD_TYPE_FUNC) {
                fn_type = (TypeFunc*)call_node->function->type;
            }
            if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
                AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
                if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
                    entry_node->node_type == AST_NODE_FUNC_EXPR || entry_node->node_type == AST_NODE_PROC)) {
                    // Direct function reference - use direct call
                    fn_node = (AstFuncNode*)entry_node;  // save for named args lookup
                    // Check if we can use the unboxed version
                    use_unboxed = can_use_unboxed_call(call_node, fn_node);
                    if (use_unboxed && fn_type) {
                        // Get return type to verify we can handle it
                        Type* ret_type = fn_type->returned;
                        if (!ret_type || ret_type->type_id == LMD_TYPE_ANY) {
                            // Try to infer from body's last expression
                            if (fn_node->body) {
                                AstNode* last_expr = fn_node->body;
                                while (last_expr->next) last_expr = last_expr->next;
                                ret_type = last_expr->type;
                            }
                        }
                        unboxed_return_type = ret_type ? ret_type->type_id : LMD_TYPE_ANY;
                        // Only use unboxed for int return type (most common case)
                        // Boxing is handled by transpile_box_item when the result is used
                        if (unboxed_return_type != LMD_TYPE_INT) {
                            // For other types, fall back to boxed version for now
                            use_unboxed = false;
                        }
                    }
                    // Wrap unboxed INT call with i2it() for proper boxing
                    // But skip if we're inside an unboxed body (already working with native types)
                    bool need_box_wrapper = use_unboxed && unboxed_return_type == LMD_TYPE_INT && !tp->in_unboxed_body;
                    if (need_box_wrapper) {
                        strbuf_append_str(tp->code_buf, "i2it(");
                    }
                    write_fn_name_ex(tp->code_buf, fn_node,
                        (AstImportNode*)ident_node->entry->import,
                        use_unboxed ? "_u" : NULL);
                } else if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                    // Function parameter - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("callable parameter detected: %.*s",
                        (int)ident_node->name->len, ident_node->name->chars);
                } else {
                    // Variable holding a function - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("function variable detected: %.*s",
                        (int)ident_node->name->len, ident_node->name->chars);
                }
            }
            else if (call_node->function->node_type == AST_NODE_INDEX_EXPR ||
                     call_node->function->node_type == AST_NODE_MEMBER_EXPR) {
                // Function from array/map access - need dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from index/member expression");
            }
            else {
                // Other expression returning function - use dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from expression");
            }

            // For anonymous functions referenced directly (not through variable), use ->ptr
            if (fn_type && fn_type->is_anonymous && !is_fn_variable) {
                transpile_expr(tp, call_node->function);
                strbuf_append_str(tp->code_buf, "->ptr");
            }
        } else { // handle Item
            log_debug("call function type is not func");
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            return;
        }
    }

    // count arguments and check for named arguments
    int arg_count = 0;
    bool has_named_args = false;
    AstNode* arg = call_node->argument;
    while (arg) {
        if (arg->node_type == AST_NODE_NAMED_ARG) {
            has_named_args = true;
        }
        arg_count++;
        arg = arg->next;
    }

    int expected_count = fn_type ? fn_type->param_count : -1;
    bool is_variadic = fn_type ? fn_type->is_variadic : false;

    // For function variables, use fn_callN() for efficiency or fn_call() for many args
    if (is_fn_variable) {
        // Get the function pointer expression
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ?
            (AstPrimaryNode*)call_node->function : null;

        // Check if callee is a parameter with Item type (needs extraction from tagged pointer)
        bool needs_item_extraction = false;
        AstNode* callee_entry_node = nullptr;
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            callee_entry_node = ident_node->entry ? ident_node->entry->node : nullptr;
            if (callee_entry_node && callee_entry_node->node_type == AST_NODE_PARAM) {
                AstNamedNode* param_node = (AstNamedNode*)callee_entry_node;
                // If parameter doesn't have explicit function type, it's Item type
                if (!param_node->type || param_node->type->type_id != LMD_TYPE_FUNC) {
                    needs_item_extraction = true;
                }
            }
        }

        if (arg_count <= 3 && !is_variadic) {
            // Use specialized fn_callN for common cases (avoids list allocation)
            strbuf_append_format(tp->code_buf, "fn_call%d(", arg_count);

            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }

            // Emit arguments
            arg = call_node->argument;
            while (arg) {
                strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
            }
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use fn_call with List for more arguments
            strbuf_append_str(tp->code_buf, "fn_call(");

            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }

            // Build argument list
            strbuf_append_str(tp->code_buf, ",({Item _fa[]={");
            arg = call_node->argument;
            bool first = true;
            while (arg) {
                if (!first) strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
                first = false;
            }
            strbuf_append_format(tp->code_buf,
                "}; List _fl={.type_id=%d,.items=_fa,.length=%d,.capacity=%d}; &_fl;}))",
                LMD_TYPE_LIST, arg_count, arg_count);
        }
        return;  // Done with function variable call
    }

    // Direct function call (original logic)
    // transpile the params
    strbuf_append_str(tp->code_buf, "(");
    bool has_output_arg = false;

    if (has_named_args && fn_node) {
        // named arguments: need to reorder arguments based on parameter names
        log_debug("handling named arguments");

        // create array to hold resolved arguments for each param position
        // using stack allocation for reasonable param counts
        #define MAX_PARAMS 32
        AstNode* resolved_args[MAX_PARAMS] = {0};

        // first pass: collect positional args and named args
        int positional_index = 0;
        arg = call_node->argument;
        while (arg) {
            if (arg->node_type == AST_NODE_NAMED_ARG) {
                // find the parameter by name
                AstNamedNode* named_arg = (AstNamedNode*)arg;
                AstNamedNode* param = find_param_by_name(fn_node, named_arg->name);
                if (param) {
                    // find the index of this param
                    int param_index = 0;
                    AstNamedNode* p = fn_node->param;
                    while (p && p != param) {
                        p = (AstNamedNode*)p->next;
                        param_index++;
                    }
                    if (param_index < MAX_PARAMS) {
                        if (resolved_args[param_index]) {
                            log_error("Error: duplicate argument for parameter '%s'", named_arg->name->chars);
                        }
                        resolved_args[param_index] = arg;
                        log_debug("named arg '%s' -> param index %d", named_arg->name->chars, param_index);
                    }
                } else {
                    log_error("Error: unknown parameter name '%s'", named_arg->name->chars);
                }
            } else {
                // positional argument
                if (positional_index < MAX_PARAMS && !resolved_args[positional_index]) {
                    resolved_args[positional_index] = arg;
                    log_debug("positional arg -> param index %d", positional_index);
                }
                positional_index++;
            }
            arg = arg->next;
        }

        // output arguments in parameter order
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        for (int i = 0; i < expected_count && i < MAX_PARAMS; i++) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            AstNode* resolved_arg = resolved_args[i];
            transpile_call_argument(tp, resolved_arg, param_type, is_sys_func);
            param_type = param_type ? param_type->next : NULL;
        }

        // add variadic args list for variadic functions
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');
            // collect any named args that exceed param_count into vargs
            strbuf_append_str(tp->code_buf, "null");  // TODO: named variadic args not supported yet
            has_output_arg = true;
        }
        #undef MAX_PARAMS
    }
    else {
        // no named arguments: simple positional processing
        arg = call_node->argument;
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        int arg_index = 0;

        // first pass: output regular parameters
        while (arg && (expected_count < 0 || arg_index < expected_count)) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            transpile_call_argument(tp, arg, param_type, is_sys_func);

            arg = arg->next;
            param_type = param_type ? param_type->next : NULL;
            arg_index++;
        }

        // Fill missing arguments with default values or ITEM_NULL
        while (param_type) {
            log_debug("filling missing argument with default/null for param");
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            transpile_call_argument(tp, NULL, param_type, is_sys_func);
            has_output_arg = true;
            param_type = param_type->next;
        }

        // handle variadic arguments
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');

            // count variadic args
            int varg_count = 0;
            AstNode* varg = arg;
            while (varg) { varg_count++; varg = varg->next; }

            if (varg_count == 0) {
                strbuf_append_str(tp->code_buf, "null");  // no variadic args
            } else {
                // build inline list: ({Item _va[]={...}; List _vl={...}; &_vl;})
                strbuf_append_str(tp->code_buf, "({Item _va[]={");
                bool first_varg = true;
                while (arg) {
                    if (!first_varg) strbuf_append_char(tp->code_buf, ',');
                    transpile_box_item(tp, arg);
                    arg = arg->next;
                    first_varg = false;
                }
                strbuf_append_format(tp->code_buf,
                    "}; List _vl={.type_id=%d,.items=_va,.length=%d,.capacity=%d}; &_vl;})",
                    LMD_TYPE_LIST, varg_count, varg_count);
            }
            has_output_arg = true;
        } else {
            // discard extra arguments with warning (non-variadic case)
            while (arg) {
                log_warn("param_mismatch: discarding extra argument %d (function expects %d params)",
                    arg_index + 1, expected_count);
                arg = arg->next;
                arg_index++;
            }
        }
    }

    strbuf_append_char(tp->code_buf, ')');

    // Close the i2it() wrapper for unboxed INT calls (but not inside unboxed body)
    if (use_unboxed && unboxed_return_type == LMD_TYPE_INT && !tp->in_unboxed_body) {
        strbuf_append_char(tp->code_buf, ')');
    }

    // '?' propagation: wrap call result in error check
    if (call_node->propagate) {
        // use unique counter for temp variable to avoid name collisions
        int prop_id = tp->temp_var_counter++;
        // close the statement expression: check error, return if error, else yield value
        strbuf_append_format(tp->code_buf, "; if(item_type_id(_ep%d)==LMD_TYPE_ERROR) return _ep%d; _ep%d;})", prop_id, prop_id, prop_id);
    }

    tp->in_tail_position = prev_in_tail;
}

void transpile_index_expr(Transpiler* tp, AstFieldNode *field_node) {
    // Defensive validation: ensure all required pointers and types are valid
    if (!field_node) {
        log_error("Error: transpile_index_expr called with null field_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!field_node->object || !field_node->field) {
        log_error("Error: transpile_index_expr missing object or field");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }
    if (!field_node->object->type || !field_node->field->type) {
        log_error("Error: transpile_index_expr missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    TypeId object_type = field_node->object->type->type_id;
    TypeId field_type = field_node->field->type->type_id;

    // Check if field type is numeric (addressing the TODO comment)
    if (field_type != LMD_TYPE_INT && field_type != LMD_TYPE_INT64 && field_type != LMD_TYPE_FLOAT) {
        // Non-numeric index, must use generic fn_index
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }

    // Fast path optimizations for specific type combinations
    if (object_type == LMD_TYPE_ARRAY_INT && field_type == LMD_TYPE_INT) {
        // transpile_expr(tp, field_node->object);
        // strbuf_append_str(tp->code_buf, "->items[");
        // transpile_expr(tp, field_node->field);
        // strbuf_append_str(tp->code_buf, "]");
        // for safety, we have to call array_int_get
        strbuf_append_str(tp->code_buf, "array_int_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_INT64 && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_int64_get
        strbuf_append_str(tp->code_buf, "array_int64_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_FLOAT && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_float_get
        strbuf_append_str(tp->code_buf, "array_float_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY && field_type == LMD_TYPE_INT) {
        TypeArray* arr_type = (TypeArray*)field_node->object->type;
        if (arr_type->nested) {
            switch (arr_type->nested->type_id) {
                case LMD_TYPE_INT:
                    strbuf_append_str(tp->code_buf, "array_int_get(");  break;
                case LMD_TYPE_INT64:
                    strbuf_append_str(tp->code_buf, "array_int64_get(");  break;
                case LMD_TYPE_FLOAT:
                    strbuf_append_str(tp->code_buf, "array_float_get(");  break;
                default:
                    strbuf_append_str(tp->code_buf, "array_get(");
            }
        }
        else {
            strbuf_append_str(tp->code_buf, "array_get(");
        }
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_LIST && field_type == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "list_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else {
        // Generic fallback for all other cases - box both arguments
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_box_item(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
}

// transpile path expression like file.etc.hosts to runtime path construction
// Uses path_new() + path_extend()/path_wildcard()/path_wildcard_recursive() chain
void transpile_path_expr(Transpiler* tp, AstPathNode *path_node) {
    log_debug("transpile_path_expr: scheme=%d, segments=%d", path_node->scheme, path_node->segment_count);

    int seg_count = path_node->segment_count;

    if (seg_count == 0) {
        // just the scheme root
        strbuf_append_str(tp->code_buf, "path_new(rt->pool,");
        strbuf_append_int(tp->code_buf, (int)path_node->scheme);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }

    // Build path using chained path_extend/path_wildcard/path_wildcard_recursive calls
    // Start with opening all the nested calls
    for (int i = seg_count - 1; i >= 0; i--) {
        AstPathSegment* seg = &path_node->segments[i];
        switch (seg->type) {
            case LPATH_SEG_WILDCARD:
                strbuf_append_str(tp->code_buf, "path_wildcard(rt->pool,");
                break;
            case LPATH_SEG_WILDCARD_REC:
                strbuf_append_str(tp->code_buf, "path_wildcard_recursive(rt->pool,");
                break;
            default:  // LPATH_SEG_NORMAL
                strbuf_append_str(tp->code_buf, "path_extend(rt->pool,");
                break;
        }
    }

    // The innermost call is path_new for the scheme
    strbuf_append_str(tp->code_buf, "path_new(rt->pool,");
    strbuf_append_int(tp->code_buf, (int)path_node->scheme);
    strbuf_append_char(tp->code_buf, ')');

    // Now close each call in order with the segment arguments
    for (int i = 0; i < seg_count; i++) {
        AstPathSegment* seg = &path_node->segments[i];
        if (seg->type == LPATH_SEG_NORMAL) {
            // Normal segment: pass the string literal
            strbuf_append_str(tp->code_buf, ",\"");
            if (seg->name) {
                // Escape the string content
                for (size_t j = 0; j < seg->name->len; j++) {
                    char c = seg->name->chars[j];
                    if (c == '"' || c == '\\') {
                        strbuf_append_char(tp->code_buf, '\\');
                    }
                    strbuf_append_char(tp->code_buf, c);
                }
            }
            strbuf_append_str(tp->code_buf, "\")");
        } else {
            // Wildcard: no additional argument needed, just close
            strbuf_append_char(tp->code_buf, ')');
        }
    }
}

// transpile path index expression like path[expr] to runtime path extension
// This extends a path with a dynamic segment computed at runtime
void transpile_path_index_expr(Transpiler* tp, AstPathIndexNode *node) {
    log_debug("transpile_path_index_expr");

    // Generate: path_extend(rt->pool, base_path, to_cstr(segment_expr))
    // to_cstr converts the expression to a C string for the segment name

    strbuf_append_str(tp->code_buf, "path_extend(rt->pool,");
    transpile_expr(tp, node->base_path);
    strbuf_append_str(tp->code_buf, ",fn_to_cstr(");
    transpile_box_item(tp, node->segment_expr);
    strbuf_append_str(tp->code_buf, "))");
}

void transpile_member_expr(Transpiler* tp, AstFieldNode *field_node) {
    // defensive check: if object or field is null, emit error and skip
    if (!field_node->object || !field_node->field) {
        log_error("transpile_member_expr: null object or field");
        strbuf_append_str(tp->code_buf, "ItemError /* null member expr */");
        return;
    }
    if (!field_node->object->type) {
        log_error("transpile_member_expr: object missing type");
        strbuf_append_str(tp->code_buf, "ItemError /* missing type */");
        return;
    }

    if (field_node->object->type->type_id == LMD_TYPE_MAP) {
        strbuf_append_str(tp->code_buf, "map_get(");
        transpile_expr(tp, field_node->object);
    }
    else if (field_node->object->type->type_id == LMD_TYPE_ELEMENT) {
        strbuf_append_str(tp->code_buf, "elmt_get(");
        transpile_expr(tp, field_node->object);
    }
    else if (field_node->object->type->type_id == LMD_TYPE_PATH) {
        // For paths, check if the field is a known property (name, is_dir, is_file, is_link)
        // In that case use item_attr; otherwise use fn_member to extend the path
        bool is_property = false;
        if (field_node->field->node_type == AST_NODE_IDENT) {
            // extract the identifier text to check against known properties
            TSNode field_ts_node = field_node->field->node;
            uint32_t len = ts_node_end_byte(field_ts_node) - ts_node_start_byte(field_ts_node);
            const char* text = tp->source + ts_node_start_byte(field_ts_node);
            if ((len == 4 && strncmp(text, "name", 4) == 0) ||
                (len == 6 && strncmp(text, "is_dir", 6) == 0) ||
                (len == 7 && strncmp(text, "is_file", 7) == 0) ||
                (len == 7 && strncmp(text, "is_link", 7) == 0) ||
                (len == 4 && strncmp(text, "size", 4) == 0) ||
                (len == 8 && strncmp(text, "modified", 8) == 0)) {
                is_property = true;
            }
        }
        if (is_property) {
            // use item_attr for property access - pass key as const char*
            strbuf_append_str(tp->code_buf, "item_attr(");
            transpile_box_item(tp, field_node->object);
            strbuf_append_str(tp->code_buf, ",\"");
            TSNode field_ts_node = field_node->field->node;
            uint32_t len = ts_node_end_byte(field_ts_node) - ts_node_start_byte(field_ts_node);
            const char* text = tp->source + ts_node_start_byte(field_ts_node);
            strbuf_append_str_n(tp->code_buf, text, len);
            strbuf_append_str(tp->code_buf, "\")");
            return;  // early return - we've handled this case completely
        } else {
            strbuf_append_str(tp->code_buf, "fn_member(");
            transpile_expr(tp, field_node->object);
        }
    }
    else {
        strbuf_append_str(tp->code_buf, "fn_member(");
        transpile_box_item(tp, field_node->object);
    }
    strbuf_append_char(tp->code_buf, ',');
    if (field_node->field->node_type == AST_NODE_IDENT) {
        // For identifier fields (like m.a), create a string constant from the identifier name
        AstIdentNode* id_node = (AstIdentNode*)field_node->field;
        String* name = id_node->name;
        // Create a TypeString for the field name
        TypeString* str_type = (TypeString*)alloc_type(tp->pool, LMD_TYPE_STRING, sizeof(TypeString));
        str_type->is_const = 1;
        str_type->is_literal = 1;
        str_type->string = name;  // reuse the pooled name string
        arraylist_append(tp->const_list, name);
        str_type->const_index = tp->const_list->length - 1;
        strbuf_append_format(tp->code_buf, "const_s2it(%d)", str_type->const_index);
    }
    else {
        transpile_box_item(tp, field_node->field);
    }
    strbuf_append_char(tp->code_buf, ')');
}

// transpile parent access: expr.. → fn_member(expr, "parent")
// expr.._.. → fn_member(fn_member(expr, "parent"), "parent")
void transpile_parent_expr(Transpiler* tp, AstParentNode *parent_node) {
    if (!parent_node->object) {
        log_error("transpile_parent_expr: null object");
        strbuf_append_str(tp->code_buf, "ItemError /* null parent expr */");
        return;
    }

    // register "parent" as a constant string if not already done
    String* parent_name = name_pool_create_len(tp->name_pool, "parent", 6);
    // find or add to const_list
    int parent_const_index = -1;
    for (int i = 0; i < tp->const_list->length; i++) {
        String* s = (String*)tp->const_list->data[i];
        if (s == parent_name) {
            parent_const_index = i;
            break;
        }
    }
    if (parent_const_index < 0) {
        arraylist_append(tp->const_list, parent_name);
        parent_const_index = tp->const_list->length - 1;
    }

    // emit nested fn_member calls: fn_member(...fn_member(obj, "parent")..., "parent")
    for (int i = 0; i < parent_node->depth; i++) {
        strbuf_append_str(tp->code_buf, "fn_member(");
    }
    transpile_box_item(tp, parent_node->object);
    for (int i = 0; i < parent_node->depth; i++) {
        strbuf_append_format(tp->code_buf, ",const_s2it(%d))", parent_const_index);
    }
}

// Emit a forward declaration for a function (just signature, no body)
void forward_declare_func(Transpiler* tp, AstFuncNode *fn_node) {
    bool is_closure = (fn_node->captures != nullptr);
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;

    strbuf_append_char(tp->code_buf, '\n');
    // closures must return Item to be compatible with fn_call*
    // functions that can raise errors must also return Item
    if (is_closure || fn_type->can_raise) {
        strbuf_append_str(tp->code_buf, "Item");
    } else if (fn_node->param && !has_typed_params(fn_node)) {
        // Functions with ALL untyped params use Item-level runtime ops internally
        // Force Item return so fn_call* and direct calls work correctly
        strbuf_append_str(tp->code_buf, "Item");
    } else {
        Type *ret_type = fn_type->returned ? fn_type->returned : (fn_node->body ? fn_node->body->type : &TYPE_ANY);
        if (!ret_type) {
            ret_type = &TYPE_ANY;
        }
        write_type(tp->code_buf, ret_type);
    }
    strbuf_append_char(tp->code_buf, ' ');
    write_fn_name(tp->code_buf, fn_node, NULL);
    strbuf_append_char(tp->code_buf, '(');

    // for closures, add hidden env parameter as first param
    bool has_params = false;
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "void* _env_ptr");
        has_params = true;
    }

    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        TypeParam* param_type = (TypeParam*)param->type;
        // closures receive all params as Item from fn_call*
        if (is_closure || param_type->is_optional) {
            strbuf_append_str(tp->code_buf, "Item");
        } else {
            write_type(tp->code_buf, param->type);
        }
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }

    if (fn_type && fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }

    strbuf_append_str(tp->code_buf, ");\n");
}

void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer) {
    bool is_closure = (fn_node->captures != nullptr);

    // Register function name mapping for stack traces (MIR name -> Lambda name)
    register_func_name(tp, fn_node);

    // Note: closure env struct is defined in pre_define_closure_envs()

    strbuf_append_char(tp->code_buf, '\n');
    // closures must return Item to be compatible with fn_call*
    // Functions that can raise errors must also return Item (since they return either value or error)
    Type *ret_type = ((TypeFunc*)fn_node->type)->returned;
    TypeFunc* fn_type_check = (TypeFunc*)fn_node->type;
    if (!ret_type && fn_node->body) {
        ret_type = fn_node->body->type;
    }
    if (!ret_type) {
        ret_type = &TYPE_ANY;
    }
    if (is_closure || fn_type_check->can_raise) {
        // Functions that can raise errors must return Item
        // (since they can return either the success value or an error)
        strbuf_append_str(tp->code_buf, "Item");
    } else if (fn_node->param && !has_typed_params(fn_node)) {
        // Functions with ALL untyped params use Item-level runtime ops internally
        // Force Item return so fn_call* and direct calls work correctly
        strbuf_append_str(tp->code_buf, "Item");
    } else {
        write_type(tp->code_buf, ret_type);
    }

    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, as_pointer ? " (*" :" ");
    write_fn_name(tp->code_buf, fn_node, NULL);
    if (as_pointer) strbuf_append_char(tp->code_buf, ')');

    // write the params
    strbuf_append_char(tp->code_buf, '(');

    // for closures, add hidden env parameter as first param
    bool has_params = false;
    if (is_closure) {
        strbuf_append_str(tp->code_buf, "void* _env_ptr");
        has_params = true;
    }

    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        // closures receive all params as Item from fn_call*
        TypeParam* param_type = (TypeParam*)param->type;
        if (is_closure || param_type->is_optional) {
            strbuf_append_str(tp->code_buf, "Item");
        } else {
            write_type(tp->code_buf, param->type);
        }
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }

    // add hidden _vargs parameter for variadic functions
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    if (fn_type && fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }

    if (as_pointer) {
        strbuf_append_str(tp->code_buf, ");\n");  return;
    }
    // write fn body
    strbuf_append_str(tp->code_buf, "){\n");

    // for closures, cast and extract captured variables from env
    if (is_closure) {
        strbuf_append_str(tp->code_buf, " ");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "* _env = (");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "*)_env_ptr;\n");
    }

    // Check if this function should use Tail Call Optimization
    // TCO converts tail-recursive calls to goto jumps, eliminating stack growth.
    // Non-tail recursive calls within the same function remain as normal calls.
    bool use_tco = should_use_tco(fn_node);

    // Phase 2: No per-function stack check — signal handler catches overflow at OS level

    // TCO: Add loop label at start of function body for goto target
    if (use_tco) {
        strbuf_append_str(tp->code_buf, " _tco_start:;\n");
    }

    // set vargs before function body for variadic functions
    if (fn_type && fn_type->is_variadic) {
        strbuf_append_str(tp->code_buf, " set_vargs(_vargs);\n");
    }

    // set current_closure context for body transpilation
    AstFuncNode* prev_closure = tp->current_closure;
    if (is_closure) {
        tp->current_closure = fn_node;
    }

    // TCO context setup
    AstFuncNode* prev_tco_func = tp->tco_func;
    bool prev_in_tail = tp->in_tail_position;
    if (use_tco) {
        tp->tco_func = fn_node;
        tp->in_tail_position = true;  // Function body is in tail position
    }

    // set current function context for proc return type checking
    AstFuncNode* prev_func_node = tp->current_func_node;
    tp->current_func_node = fn_node;

    // for procedures, use procedural content transpilation
    bool is_proc = (fn_node->node_type == AST_NODE_PROC);
    if (is_proc && fn_node->body->node_type == AST_NODE_CONTENT) {
        strbuf_append_str(tp->code_buf, " return ");
        transpile_proc_content(tp, (AstListNode*)fn_node->body);
        strbuf_append_str(tp->code_buf, ";\n}\n");
    } else if (fn_node->body->node_type == AST_NODE_RAISE_STAM) {
        // raise statement already generates return, don't add another
        transpile_raise(tp, (AstRaiseNode*)fn_node->body);
        strbuf_append_str(tp->code_buf, "\n}\n");
    } else if (is_proc && fn_node->body->node_type == AST_NODE_RETURN_STAM) {
        // return statement already generates return, don't add another
        transpile_return(tp, (AstReturnNode*)fn_node->body);
        strbuf_append_str(tp->code_buf, "\n}\n");
    } else {
        strbuf_append_str(tp->code_buf, " return ");
        // Check if we need to box the return value
        // Box if: (1) it's a closure, OR (2) can_raise is true, OR (3) return type is Item but body type is a scalar
        // OR (4) function has ALL untyped params (returns Item, body may produce native values)
        bool needs_boxing = is_closure || fn_type_check->can_raise ||
                           (fn_node->param && !has_typed_params(fn_node));
        if (!needs_boxing && ret_type->type_id == LMD_TYPE_ANY && fn_node->body->type) {
            TypeId body_type_id = fn_node->body->type->type_id;
            // Box scalar types when returning as Item
            needs_boxing = (body_type_id == LMD_TYPE_INT || body_type_id == LMD_TYPE_INT64 ||
                           body_type_id == LMD_TYPE_FLOAT || body_type_id == LMD_TYPE_BOOL ||
                           body_type_id == LMD_TYPE_STRING || body_type_id == LMD_TYPE_SYMBOL ||
                           body_type_id == LMD_TYPE_BINARY || body_type_id == LMD_TYPE_DECIMAL ||
                           body_type_id == LMD_TYPE_DTIME);
            // Also box for CONTENT blocks — our single-value optimization may produce
            // raw scalars even when body type says LIST
            if (!needs_boxing && fn_node->body->node_type == AST_NODE_CONTENT) {
                needs_boxing = true;
            }
        }

        if (needs_boxing) {
            transpile_box_item(tp, fn_node->body);
        } else {
            transpile_expr(tp, fn_node->body);
        }
        strbuf_append_str(tp->code_buf, ";\n}\n");
    }

    // Restore TCO context
    tp->tco_func = prev_tco_func;
    tp->in_tail_position = prev_in_tail;

    // restore previous closure context
    tp->current_closure = prev_closure;

    // restore current function context
    tp->current_func_node = prev_func_node;

    // For typed functions (non-closure, non-proc), generate an unboxed version
    // The unboxed version uses native types for params AND return (no boxing overhead)
    if (!is_closure && !is_proc && !as_pointer && has_typed_params(fn_node)) {
        define_func_unboxed(tp, fn_node);
    }
    // Generate fn_call*-compatible wrapper for functions with non-Item ABI
    // This covers typed params AND/OR native return types
    if (!is_closure && !is_proc && !as_pointer && needs_fn_call_wrapper(fn_node)) {
        define_func_call_wrapper(tp, fn_node);
    }
}

// Generate unboxed version of a typed function
// This version uses native C types for all params and return value
// Named with _u suffix: _square_u15
void define_func_unboxed(Transpiler* tp, AstFuncNode *fn_node) {
    // Determine the native return type
    // For content blocks { expr }, get the type from the last expression
    Type *ret_type = ((TypeFunc*)fn_node->type)->returned;

    // If the boxed version already returns a native type (explicit scalar return),
    // there's no need to generate an unboxed version - they would be identical
    if (ret_type && (ret_type->type_id == LMD_TYPE_INT ||
                     ret_type->type_id == LMD_TYPE_FLOAT ||
                     ret_type->type_id == LMD_TYPE_BOOL)) {
        return;
    }

    // If return type is ANY (implicit), try to infer from function body
    if ((!ret_type || ret_type->type_id == LMD_TYPE_ANY) && fn_node->body) {
        // If body is a content block, get the type of the last item
        if (fn_node->body->node_type == AST_NODE_CONTENT) {
            AstListNode* content = (AstListNode*)fn_node->body;
            // Find the last item in the content list
            AstNode* last_item = content->item;
            while (last_item && last_item->next) {
                last_item = last_item->next;
            }
            if (last_item && last_item->type && last_item->type->type_id != LMD_TYPE_ANY) {
                ret_type = last_item->type;
            }
        } else if (fn_node->body->type && fn_node->body->type->type_id != LMD_TYPE_ANY) {
            ret_type = fn_node->body->type;
        }
    }

    if (!ret_type || ret_type->type_id == LMD_TYPE_ANY) {
        // Cannot determine a specific return type, skip generating unboxed version
        return;
    }

    strbuf_append_char(tp->code_buf, '\n');
    write_type(tp->code_buf, ret_type);
    strbuf_append_char(tp->code_buf, ' ');
    write_fn_name_ex(tp->code_buf, fn_node, NULL, "_u");

    // Write params with native types
    strbuf_append_char(tp->code_buf, '(');
    bool has_params = false;
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        TypeParam* param_type = (TypeParam*)param->type;
        // For optional params, still use Item (need to check for null)
        if (param_type->is_optional) {
            strbuf_append_str(tp->code_buf, "Item");
        } else {
            write_type(tp->code_buf, param->type);
        }
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }

    // Handle variadic
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    if (fn_type && fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }

    strbuf_append_str(tp->code_buf, "){\n");

    // Variadic setup
    if (fn_type && fn_type->is_variadic) {
        strbuf_append_str(tp->code_buf, " set_vargs(_vargs);\n");
    }

    // Function body - return without boxing
    // Set flag so recursive calls don't get wrapped with i2it()
    bool prev_in_unboxed = tp->in_unboxed_body;
    tp->in_unboxed_body = true;

    strbuf_append_str(tp->code_buf, " return ");
    transpile_expr(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\n}\n");

    tp->in_unboxed_body = prev_in_unboxed;
}

// Generate fn_call*-compatible wrapper for typed functions.
// fn_call0..3() cast function pointers to Item(*)(Item,...) and pass boxed Items as args.
// If the original function has native-typed params (int64_t, double, etc.), the wrapper
// accepts Items, unboxes them, calls the original, and boxes the return if needed.
// Named with _w suffix: _square_w15
void define_func_call_wrapper(Transpiler* tp, AstFuncNode *fn_node) {
    bool is_closure = (fn_node->captures != nullptr);
    if (is_closure) return;  // closures already use Item params

    if (!needs_fn_call_wrapper(fn_node)) return;  // already matches fn_call* ABI

    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    Type *ret_type = fn_type->returned;
    if (!ret_type && fn_node->body) ret_type = fn_node->body->type;
    if (!ret_type) ret_type = &TYPE_ANY;

    // Determine the boxed version's return type to know if we need to box the result
    TypeId boxed_ret_tid;
    if (fn_type->can_raise) {
        boxed_ret_tid = LMD_TYPE_ANY;  // returns Item
    } else {
        boxed_ret_tid = ret_type->type_id;
    }

    strbuf_append_char(tp->code_buf, '\n');
    // wrapper always returns Item
    strbuf_append_str(tp->code_buf, "Item ");
    write_fn_name_ex(tp->code_buf, fn_node, NULL, "_w");
    strbuf_append_char(tp->code_buf, '(');

    // all params are Item in the wrapper
    bool has_params = false;
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "Item _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
        has_params = true;
    }
    if (fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "List* _vargs");
    }
    strbuf_append_str(tp->code_buf, "){\n return ");

    // box the return value if the original function returns a native type
    const char* box_prefix = NULL;
    const char* box_suffix = ")";
    switch (boxed_ret_tid) {
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
        box_prefix = "i2it("; break;
    case LMD_TYPE_FLOAT:
        box_prefix = "push_d("; break;
    case LMD_TYPE_BOOL:
        box_prefix = "b2it("; break;
    case LMD_TYPE_STRING:
    case LMD_TYPE_BINARY:
        box_prefix = "s2it("; break;
    case LMD_TYPE_SYMBOL:
        box_prefix = "y2it("; break;
    case LMD_TYPE_DTIME:
        box_prefix = "push_k("; break;
    case LMD_TYPE_DECIMAL:
        box_prefix = "c2it("; break;
    default:
        break;  // returns Item, no boxing needed
    }
    if (box_prefix) strbuf_append_str(tp->code_buf, box_prefix);

    // call the original (boxed) function
    write_fn_name(tp->code_buf, fn_node, NULL);
    strbuf_append_char(tp->code_buf, '(');

    // unbox each param from Item to native type
    has_params = false;
    param = fn_node->param;
    while (param) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        TypeParam* param_type = (TypeParam*)param->type;
        if (param_type->is_optional || param_type->type_id == LMD_TYPE_ANY) {
            // pass Item through (already the right type)
            strbuf_append_char(tp->code_buf, '_');
            strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        } else {
            // unbox based on declared type
            const char* unbox_fn = NULL;
            switch (param_type->type_id) {
            case LMD_TYPE_INT:    unbox_fn = "it2i("; break;
            case LMD_TYPE_INT64:  unbox_fn = "it2l("; break;
            case LMD_TYPE_FLOAT:  unbox_fn = "it2d("; break;
            case LMD_TYPE_BOOL:   unbox_fn = "it2b("; break;
            case LMD_TYPE_STRING:
            case LMD_TYPE_BINARY: unbox_fn = "it2s("; break;
            default:
                // pointer types: cast from Item
                strbuf_append_str(tp->code_buf, "(void*)_");
                strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
                param = (AstNamedNode*)param->next;
                has_params = true;
                continue;
            }
            strbuf_append_str(tp->code_buf, unbox_fn);
            strbuf_append_char(tp->code_buf, '_');
            strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
            strbuf_append_char(tp->code_buf, ')');
        }
        param = (AstNamedNode*)param->next;
        has_params = true;
    }
    if (fn_type->is_variadic) {
        if (has_params) strbuf_append_str(tp->code_buf, ",");
        strbuf_append_str(tp->code_buf, "_vargs");
    }
    strbuf_append_char(tp->code_buf, ')');

    if (box_prefix) strbuf_append_str(tp->code_buf, box_suffix);
    strbuf_append_str(tp->code_buf, ";\n}\n");
}

void transpile_box_capture(Transpiler* tp, CaptureInfo* cap, bool from_outer_env) {
    Type* type = cap->entry && cap->entry->node ? cap->entry->node->type : nullptr;
    TypeId type_id = type ? type->type_id : LMD_TYPE_ANY;

    if (from_outer_env) {
        // already boxed in outer env, just copy
        strbuf_append_str(tp->code_buf, "_env->");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        return;
    }

    // box based on type
    switch (type_id) {
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "i2it(_");
        break;
    case LMD_TYPE_INT64:
        strbuf_append_str(tp->code_buf, "l2it(&_");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "d2it(&_");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "b2it(_");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "s2it(_");
        break;
    case LMD_TYPE_SYMBOL:
        strbuf_append_str(tp->code_buf, "y2it(_");
        break;
    case LMD_TYPE_BINARY:
        strbuf_append_str(tp->code_buf, "x2it(_");
        break;
    case LMD_TYPE_DECIMAL:
        strbuf_append_str(tp->code_buf, "c2it(_");
        break;
    case LMD_TYPE_DTIME:
        strbuf_append_str(tp->code_buf, "k2it(&_");
        break;
    default:
        // for container types (List*, Map*, etc.) and Item, cast to Item
        strbuf_append_str(tp->code_buf, "(Item)_");
        strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
        return;
    }
    strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_fn_expr(Transpiler* tp, AstFuncNode *fn_node) {
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;
    int arity = fn_type ? fn_type->param_count : 0;

    // Register closure name mapping for stack traces (MIR name -> Lambda name)
    // This is done here because at this point we have access to current_assign_name
    register_func_name_with_context(tp, fn_node);

    if (fn_node->captures) {
        // closure: allocate env, populate captured values, call to_closure
        // generate: ({ Env_fXXX* env = heap_calloc(sizeof(Env_fXXX), 0);
        //              env->var1 = i2it(_var1); env->var2 = s2it(_var2); ...
        //              to_closure_named(_fXXX, arity, env, "name"); })
        strbuf_append_str(tp->code_buf, "({ ");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "* _closure_env = heap_calloc(sizeof(");
        write_env_name(tp->code_buf, fn_node);
        strbuf_append_str(tp->code_buf, "), 0);\n");

        // populate captured variables
        CaptureInfo* cap = fn_node->captures;
        while (cap) {
            strbuf_append_str(tp->code_buf, "  _closure_env->");
            strbuf_append_str_n(tp->code_buf, cap->name->chars, cap->name->len);
            strbuf_append_str(tp->code_buf, " = ");

            // box the captured variable to Item
            // the captured variable may be in our own closure env if we're a nested closure
            bool from_outer = false;
            if (tp->current_closure) {
                CaptureInfo* outer_cap = find_capture(tp->current_closure, cap->name);
                from_outer = (outer_cap != nullptr);
            }
            transpile_box_capture(tp, cap, from_outer);
            strbuf_append_str(tp->code_buf, ";\n");
            cap = cap->next;
        }

        strbuf_append_str(tp->code_buf, "  to_closure_named(");
        write_fn_name(tp->code_buf, fn_node, NULL);
        strbuf_append_format(tp->code_buf, ",%d,_closure_env,", arity);
        // pass function name as string literal for stack traces
        // prefer named function, fall back to assignment variable name, then <anonymous>
        if (fn_node->name && fn_node->name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else {
            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
        }
        strbuf_append_str(tp->code_buf, "); })");
    } else {
        // regular function without captures - use to_fn_named for stack traces
        strbuf_append_format(tp->code_buf, "to_fn_named(");
        // use _w wrapper for functions with non-Item ABI so fn_call* works correctly
        if (needs_fn_call_wrapper(fn_node)) {
            write_fn_name_ex(tp->code_buf, fn_node, NULL, "_w");
        } else {
            write_fn_name(tp->code_buf, fn_node, NULL);
        }
        strbuf_append_format(tp->code_buf, ",%d,", arity);
        // pass function name as string literal for stack traces
        if (fn_node->name && fn_node->name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, fn_node->name->chars, fn_node->name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else if (tp->current_assign_name && tp->current_assign_name->chars) {
            strbuf_append_char(tp->code_buf, '"');
            strbuf_append_str_n(tp->code_buf, tp->current_assign_name->chars, tp->current_assign_name->len);
            strbuf_append_char(tp->code_buf, '"');
        } else {
            strbuf_append_str(tp->code_buf, "\"<anonymous>\"");
        }
        strbuf_append_char(tp->code_buf, ')');
    }
}

void transpile_base_type(Transpiler* tp, AstTypeNode* type_node) {
    TypeType* type_type = (TypeType*)type_node->type;
    // for datetime sub-types (date, time), we need to preserve the specific Type pointer
    // since TYPE_DATE, TYPE_TIME, and TYPE_DTIME all share type_id = LMD_TYPE_DTIME
    if (type_type->type == &TYPE_DATE || type_type->type == &TYPE_TIME) {
        arraylist_append(tp->type_list, (void*)type_type);
        int type_index = tp->type_list->length - 1;
        strbuf_append_format(tp->code_buf, "const_type(%d)", type_index);
    } else {
        strbuf_append_format(tp->code_buf, "base_type(%d)", type_type->type->type_id);
    }
}

void transpile_binary_type(Transpiler* tp, AstBinaryNode* bin_node) {
    TypeBinary* binary_type = (TypeBinary*)((TypeType*)bin_node->type)->type;
    strbuf_append_format(tp->code_buf, "const_type(%d)", binary_type->type_index);
}

void transpile_unary_type(Transpiler* tp, AstUnaryNode* unary_node) {
    TypeUnary* unary_type = (TypeUnary*)((TypeType*)unary_node->type)->type;
    strbuf_append_format(tp->code_buf, "const_type(%d)", unary_type->type_index);
}

void transpile_expr(Transpiler* tp, AstNode *expr_node) {
    if (!expr_node) { log_error("missing expression node"); return; }
    // get the function name
    switch (expr_node->node_type) {
    case AST_NODE_PRIMARY:
        transpile_primary_expr(tp, (AstPrimaryNode*)expr_node);
        break;
    case AST_NODE_UNARY:
        transpile_unary_expr(tp, (AstUnaryNode*)expr_node);
        break;
    case AST_NODE_SPREAD:
        transpile_spread_expr(tp, (AstUnaryNode*)expr_node);
        break;
    case AST_NODE_BINARY:
        transpile_binary_expr(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_PIPE:
        transpile_pipe_expr(tp, (AstPipeNode*)expr_node);
        break;
    case AST_NODE_CURRENT_ITEM:
        // ~ references the current pipe context item
        strbuf_append_str(tp->code_buf, "_pipe_item");
        break;
    case AST_NODE_CURRENT_INDEX:
        // ~# references the current pipe context key/index
        strbuf_append_str(tp->code_buf, "_pipe_index");
        break;
    case AST_NODE_IF_EXPR:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_IF_STAM:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_MATCH_EXPR:
        transpile_match(tp, (AstMatchNode*)expr_node);
        break;
    case AST_NODE_FOR_EXPR:
        transpile_for(tp, (AstForNode*)expr_node);
        break;
    case AST_NODE_FOR_STAM:
        transpile_for(tp, (AstForNode*)expr_node);
        break;
    case AST_NODE_WHILE_STAM:
        transpile_while(tp, (AstWhileNode*)expr_node);
        break;
    case AST_NODE_BREAK_STAM:
        strbuf_append_str(tp->code_buf, "break");
        break;
    case AST_NODE_CONTINUE_STAM:
        strbuf_append_str(tp->code_buf, "continue");
        break;
    case AST_NODE_RETURN_STAM:
        transpile_return(tp, (AstReturnNode*)expr_node);
        break;
    case AST_NODE_RAISE_STAM:
        transpile_raise(tp, (AstRaiseNode*)expr_node);
        break;
    case AST_NODE_RAISE_EXPR:
        // raise as expression - evaluates to the error value (for use in if-then-else, etc.)
        // Unlike raise_stam which returns from function, raise_expr just evaluates to the error value
        if (((AstRaiseNode*)expr_node)->value) {
            transpile_box_item(tp, ((AstRaiseNode*)expr_node)->value);
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        }
        break;
    case AST_NODE_VAR_STAM:
        transpile_let_stam(tp, (AstLetNode*)expr_node, false);  // reuse let transpiler
        break;
    case AST_NODE_ASSIGN_STAM:
        transpile_assign_stam(tp, (AstAssignStamNode*)expr_node);
        break;
    case AST_NODE_PIPE_FILE_STAM:
        transpile_pipe_file_stam(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_ASSIGN:
        transpile_assign_expr(tp, (AstNamedNode*)expr_node);
        break;
    case AST_NODE_ARRAY:
        transpile_array_expr(tp, (AstArrayNode*)expr_node);
        break;
    case AST_NODE_LIST:
        transpile_list_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_CONTENT:
        transpile_content_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_MAP:
        transpile_map_expr(tp, (AstMapNode*)expr_node);
        break;
    case AST_NODE_ELEMENT:
        transpile_element(tp, (AstElementNode*)expr_node);
        break;
    case AST_NODE_MEMBER_EXPR:
        transpile_member_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_INDEX_EXPR:
        transpile_index_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_PATH_EXPR:
        transpile_path_expr(tp, (AstPathNode*)expr_node);
        break;
    case AST_NODE_PATH_INDEX_EXPR:
        transpile_path_index_expr(tp, (AstPathIndexNode*)expr_node);
        break;
    case AST_NODE_PARENT_EXPR:
        transpile_parent_expr(tp, (AstParentNode*)expr_node);
        break;
    case AST_NODE_CALL_EXPR:
        transpile_call_expr(tp, (AstCallNode*)expr_node);
        break;
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
    case AST_NODE_FUNC:  case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN:
        // already transpiled or pattern definitions (handled at compile time)
        break;
    case AST_NODE_FUNC_EXPR:
        transpile_fn_expr(tp, (AstFuncNode*)expr_node);
        break;
    case AST_NODE_TYPE:
        transpile_base_type(tp, (AstTypeNode*)expr_node);
        break;
    case AST_NODE_LIST_TYPE: {
        TypeType* list_type = (TypeType*)((AstListNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)",
            ((TypeList*)list_type->type)->type_index);
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        TypeType* array_type = (TypeType*)((AstArrayNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)",
            ((TypeArray*)array_type->type)->type_index);
        break;
    }
    case AST_NODE_MAP_TYPE: {
        TypeType* map_type = (TypeType*)((AstMapNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)",
            ((TypeMap*)map_type->type)->type_index);
        break;
    }
    case AST_NODE_ELMT_TYPE: {
        TypeType* elmt_type = (TypeType*)((AstElementNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)",
            ((TypeElmt*)elmt_type->type)->type_index);
        break;
    }
    case AST_NODE_FUNC_TYPE: {
        TypeType* fn_type = (TypeType*)((AstFuncNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)",
            ((TypeFunc*)fn_type->type)->type_index);
        break;
    }
    case AST_NODE_BINARY_TYPE:
        transpile_binary_type(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_UNARY_TYPE:
        transpile_unary_type(tp, (AstUnaryNode*)expr_node);
        break;
    case AST_NODE_CONSTRAINED_TYPE: {
        // Transpile constrained type: emit const_type(index) for runtime lookup
        AstConstrainedTypeNode* constrained_node = (AstConstrainedTypeNode*)expr_node;
        // ast_node->type is directly the TypeConstrained* (not wrapped in TypeType)
        TypeConstrained* constrained = (TypeConstrained*)constrained_node->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", constrained->type_index);
        break;
    }
    case AST_NODE_IMPORT:
        log_debug("import module");
        break;
    default:
        log_debug("unknown expression type: %d!!!", expr_node->node_type);
        break;
    }
}

// helper: write the fields of a Mod struct for a module's public interface
// Used by both define_module_import (importing script) and self-struct generation (module itself)
// Field order: fixed fields → function pointers → pub var fields
// This ensures runner's pointer arithmetic (for fn ptrs) works without needing pub var sizes
void write_mod_struct_fields(Transpiler* tp, AstNode *ast_root) {
    assert(ast_root->node_type == AST_SCRIPT);
    AstNode *node = ((AstScript*)ast_root)->child;
    // fixed fields: consts pointer, module main, and init_vars
    strbuf_append_str(tp->code_buf, "void** consts;\n");
    strbuf_append_str(tp->code_buf, "Item (*_mod_main)(Context*);\n");
    strbuf_append_str(tp->code_buf, "void (*_init_vars)(void*);\n");
    // first pass: function pointer fields
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;
            continue;
        }
        else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
            AstFuncNode *func_node = (AstFuncNode*)node;
            if (((TypeFunc*)func_node->type)->is_public) {
                define_func(tp, func_node, true);
                // also add _w wrapper function pointer if this function needs fn_call* wrapper
                if (node->node_type != AST_NODE_PROC && needs_fn_call_wrapper(func_node)) {
                    strbuf_append_str(tp->code_buf, "Item (*");
                    write_fn_name_ex(tp->code_buf, func_node, NULL, "_w");
                    strbuf_append_str(tp->code_buf, ")(");
                    bool has_w_params = false;
                    AstNamedNode *param = func_node->param;
                    while (param) {
                        if (has_w_params) strbuf_append_str(tp->code_buf, ",");
                        strbuf_append_str(tp->code_buf, "Item");
                        param = (AstNamedNode*)param->next;
                        has_w_params = true;
                    }
                    TypeFunc* fn_type = (TypeFunc*)func_node->type;
                    if (fn_type && fn_type->is_variadic) {
                        if (has_w_params) strbuf_append_str(tp->code_buf, ",");
                        strbuf_append_str(tp->code_buf, "List*");
                    }
                    strbuf_append_str(tp->code_buf, ");\n");
                }
            }
        }
        node = node->next;
    }
    // second pass: pub var fields
    node = ((AstScript*)ast_root)->child;
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;
            continue;
        }
        else if (node->node_type == AST_NODE_PUB_STAM) {
            AstNode *declare = ((AstLetNode*)node)->declare;
            while (declare) {
                AstNamedNode *asn_node = (AstNamedNode*)declare;
                write_type(tp->code_buf, asn_node->type);
                strbuf_append_char(tp->code_buf, ' ');
                write_var_name(tp->code_buf, asn_node, NULL);
                strbuf_append_str(tp->code_buf, ";\n");
                declare = declare->next;
            }
        }
        node = node->next;
    }
}

void define_module_import(Transpiler* tp, AstImportNode *import_node) {
    log_debug("define import module");
    // import module
    if (!import_node->script) { log_error("Error: missing script for import");  return; }
    log_debug("script reference: %s", import_node->script->reference);
    AstNode *node = import_node->script->ast_root;
    if (!node) { log_error("Error: Missing root node in module_import");  return; }
    strbuf_append_format(tp->code_buf, "struct Mod%d {\n", import_node->script->index);
    write_mod_struct_fields(tp, node);
    strbuf_append_format(tp->code_buf, "} m%d;\n", import_node->script->index);
}

void define_ast_node(Transpiler* tp, AstNode *node) {
    // get the function name
    log_debug("define_ast_node: node %p, type %d", node, node ? node->node_type : -1);
    if (!node) return;
    switch(node->node_type) {
    case AST_NODE_IDENT:  case AST_NODE_PARAM:
        break;
    case AST_NODE_PRIMARY:
        if (((AstPrimaryNode*)node)->expr) {
            define_ast_node(tp, ((AstPrimaryNode*)node)->expr);
        }
        break;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        define_ast_node(tp, ((AstUnaryNode*)node)->operand);
        break;
    case AST_NODE_BINARY:
        define_ast_node(tp, ((AstBinaryNode*)node)->left);
        define_ast_node(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match_node = (AstMatchNode*)node;
        define_ast_node(tp, match_node->scrutinee);
        AstMatchArm* arm = match_node->first_arm;
        while (arm) {
            if (arm->pattern) define_ast_node(tp, arm->pattern);
            define_ast_node(tp, arm->body);
            arm = (AstMatchArm*)arm->next;
        }
        break;
    }
    case AST_NODE_LET_STAM:  case AST_NODE_TYPE_STAM: {
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN: {
        // Pattern definitions - compile the pattern and store in type_list
        AstPatternDefNode* pattern_def = (AstPatternDefNode*)node;
        TypePattern* pattern_type = (TypePattern*)pattern_def->type;

        // Compile pattern to regex if not already compiled
        if (pattern_type->re2 == nullptr && pattern_def->as != nullptr) {
            const char* error_msg = nullptr;
            TypePattern* compiled = compile_pattern_ast(tp->pool, pattern_def->as,
                pattern_def->is_symbol, &error_msg);
            if (compiled) {
                // Copy compiled info to existing type
                pattern_type->re2 = compiled->re2;
                pattern_type->source = compiled->source;
                // Add to type_list for runtime access
                arraylist_append(tp->type_list, pattern_type);
                pattern_type->pattern_index = tp->type_list->length - 1;
                log_debug("compiled pattern '%.*s' to regex, index=%d",
                    (int)pattern_def->name->len, pattern_def->name->chars, pattern_type->pattern_index);
            } else {
                log_error("failed to compile pattern '%.*s': %s",
                    (int)pattern_def->name->len, pattern_def->name->chars,
                    error_msg ? error_msg : "unknown error");
            }
        }
        break;
    }
    case AST_NODE_PUB_STAM: {
        // pub vars are declared at file scope by declare_global_var and assigned in main() by assign_global_var
        // just recursively process declarations for nested closures/patterns
        AstNode *decl = ((AstLetNode*)node)->declare;
        while (decl) {
            define_ast_node(tp, decl);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_FOR_STAM: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_WHILE_STAM: {
        AstWhileNode* while_node = (AstWhileNode*)node;
        define_ast_node(tp, while_node->cond);
        define_ast_node(tp, while_node->body);
        break;
    }
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
        // no child nodes to define
        break;
    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret_node = (AstReturnNode*)node;
        if (ret_node->value) {
            define_ast_node(tp, ret_node->value);
        }
        break;
    }
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR: {
        AstRaiseNode* raise_node = (AstRaiseNode*)node;
        if (raise_node->value) {
            define_ast_node(tp, raise_node->value);
        }
        break;
    }
    case AST_NODE_VAR_STAM: {
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        define_ast_node(tp, assign->value);
        break;
    }
    case AST_NODE_PIPE_FILE_STAM: {
        AstBinaryNode* pipe_file = (AstBinaryNode*)node;
        define_ast_node(tp, pipe_file->left);
        define_ast_node(tp, pipe_file->right);
        break;
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* assign = (AstNamedNode*)node;
        define_ast_node(tp, assign->as);
        break;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        define_ast_node(tp, key->as);
        break;
    }
    case AST_NODE_LOOP:
        define_ast_node(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ARRAY: {
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            define_ast_node(tp, item);
            item = item->next;
        }
        break;
    }
    case AST_NODE_LIST:  case AST_NODE_CONTENT: {
        AstNode *ld = ((AstListNode*)node)->declare;
        while (ld) {
            define_ast_node(tp, ld);
            ld = ld->next;
        }
        AstNode *li = ((AstListNode*)node)->item;
        while (li) {
            define_ast_node(tp, li);
            li = li->next;
        }
        break;
    }
    case AST_NODE_MAP:  case AST_NODE_ELEMENT: {
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            define_ast_node(tp, nm_item);
            nm_item = nm_item->next;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        define_ast_node(tp, ((AstFieldNode*)node)->object);
        define_ast_node(tp, ((AstFieldNode*)node)->field);
        break;
    case AST_NODE_PARENT_EXPR:
        define_ast_node(tp, ((AstParentNode*)node)->object);
        break;
    case AST_NODE_CALL_EXPR: {
        define_ast_node(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            define_ast_node(tp, arg);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC: {
        // func needs to be brought to global scope in C
        define_func(tp, (AstFuncNode*)node, false);
        AstFuncNode* func = (AstFuncNode*)node;
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            define_ast_node(tp, fn_param);
            fn_param = fn_param->next;
        }
        define_ast_node(tp, func->body);
        break;
    }
    case AST_NODE_IMPORT:
        define_module_import(tp, (AstImportNode*)node);
        break;
    case AST_NODE_SYS_FUNC:
        // todo: should define its params
        break;
    case AST_NODE_TYPE:  case AST_NODE_LIST_TYPE:  case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:  case AST_NODE_ELMT_TYPE:  case AST_NODE_BINARY_TYPE:
    case AST_NODE_UNARY_TYPE:
        // nothing to define at the moment
        break;
    default:
        log_debug("unknown expression type: %d", node->node_type);
        break;
    }
}

void declare_global_var(Transpiler* tp, AstLetNode *let_node) {
    // declare global vars
    AstNode *decl = let_node->declare;
    while (decl) {
        if (decl->node_type == AST_NODE_DECOMPOSE) {
            // Handle decomposition - declare all variables as Item
            AstDecomposeNode* dec_node = (AstDecomposeNode*)decl;
            for (int i = 0; i < dec_node->name_count; i++) {
                String* name = dec_node->names[i];
                strbuf_append_str(tp->code_buf, "Item _");
                strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                strbuf_append_str(tp->code_buf, ";\n");
            }
        } else {
            AstNamedNode *asn_node = (AstNamedNode*)decl;
            Type *var_type = asn_node->type;
            write_type(tp->code_buf, var_type);
            strbuf_append_char(tp->code_buf, ' ');
            write_var_name(tp->code_buf, asn_node, NULL);
            strbuf_append_str(tp->code_buf, ";\n");
        }
        decl = decl->next;
    }
}

void assign_global_var(Transpiler* tp, AstLetNode *let_node) {
    // declare global vars
    AstNode *decl = let_node->declare;
    while (decl) {
        if (decl->node_type == AST_NODE_DECOMPOSE) {
            // Handle decomposition at global level using a nested scope
            AstDecomposeNode* dec_node = (AstDecomposeNode*)decl;
            strbuf_append_str(tp->code_buf, "\n {Item _dec_src=");
            transpile_box_item(tp, dec_node->as);
            strbuf_append_str(tp->code_buf, ";");

            // Decompose into individual variables (global - no type declaration)
            for (int i = 0; i < dec_node->name_count; i++) {
                String* name = dec_node->names[i];
                strbuf_append_str(tp->code_buf, "\n  _");
                strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                strbuf_append_char(tp->code_buf, '=');

                if (dec_node->is_named) {
                    strbuf_append_str(tp->code_buf, "item_attr(_dec_src,\"");
                    strbuf_append_str_n(tp->code_buf, name->chars, name->len);
                    strbuf_append_str(tp->code_buf, "\");");
                } else {
                    strbuf_append_format(tp->code_buf, "item_at(_dec_src,%d);", i);
                }
            }
            strbuf_append_str(tp->code_buf, "}");  // close nested scope
        } else {
            AstNamedNode *asn_node = (AstNamedNode*)decl;
            strbuf_append_str(tp->code_buf, "\n  ");
            write_var_name(tp->code_buf, asn_node, NULL);
            strbuf_append_char(tp->code_buf, '=');
            transpile_expr(tp, asn_node->as);
            strbuf_append_char(tp->code_buf, ';');
        }
        decl = decl->next;
    }
}

// include lambda-embed.h to get the lambda header file content as a string
#include "lambda-embed.h"

// Phase 2: Stack overflow protection is handled by signal handler (sigaltstack/SEH)
// installed in lambda_stack_init(). No per-call check code is emitted into transpiled
// output — the OS catches stack overflow at the hardware/MMU level with zero overhead.

void transpile_ast_root(Transpiler* tp, AstScript *script) {
    strbuf_append_str_n(tp->code_buf, (const char*)lambda_lambda_h, lambda_lambda_h_len);
    // Phase 2: No stack check code emitted — signal handler handles overflow
    // all (nested) function definitions need to be hoisted to global level
    log_debug("define_ast_node ...");
    // Import shared runtime context pointer from native runtime
    // This ensures all modules (main + imports) share the same rt
    // _lambda_rt is a Context* in the native runtime, and its address is exported
    // MIR import resolves to the address of _lambda_rt, so we declare it as Context*
    strbuf_append_str(tp->code_buf, "\nextern Context* _lambda_rt;\n");
    strbuf_append_str(tp->code_buf, "#define rt _lambda_rt\n");

    // For imported modules, add module-local constants pointer and override const macros
    // This allows each module to access its own const_list instead of the main script's
    if (!tp->is_main) {
        log_debug("Transpiling imported module - adding module-local constants");
        strbuf_append_str(tp->code_buf, "\n// Module-local constants pointer\n");
        strbuf_append_str(tp->code_buf, "static void** _mod_consts;\n");
        strbuf_append_str(tp->code_buf, "void _init_mod_consts(void** consts) { _mod_consts = consts; }\n");
        // Override const macros to use module-local constants
        strbuf_append_str(tp->code_buf, "#undef const_d2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_l2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_c2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_s2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_y2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_k2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_x2it\n");
        strbuf_append_str(tp->code_buf, "#undef const_s\n");
        strbuf_append_str(tp->code_buf, "#undef const_c\n");
        strbuf_append_str(tp->code_buf, "#undef const_k\n");
        strbuf_append_str(tp->code_buf, "#define const_d2it(index)    d2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_l2it(index)    l2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_c2it(index)    c2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_s2it(index)    s2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_y2it(index)    y2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_k2it(index)    k2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_x2it(index)    x2it(_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_s(index)      ((String*)_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_c(index)      ((Decimal*)_mod_consts[index])\n");
        strbuf_append_str(tp->code_buf, "#define const_k(index)      (*(DateTime*)_mod_consts[index])\n");

        // Module-local type_list pointer and wrapper functions
        // This allows each module to access its own type_list instead of the main script's
        strbuf_append_str(tp->code_buf, "\n// Module-local type_list pointer\n");
        strbuf_append_str(tp->code_buf, "static void* _mod_type_list;\n");
        strbuf_append_str(tp->code_buf, "void _init_mod_types(void* tl) { _mod_type_list = tl; }\n");
        // Define wrapper functions that swap rt->type_list to module's before calling real functions
        strbuf_append_str(tp->code_buf, "static Map* _mod_map(int ti) { void* sv=rt->type_list; rt->type_list=_mod_type_list; Map* r=map(ti); rt->type_list=sv; return r; }\n");
        strbuf_append_str(tp->code_buf, "static Element* _mod_elmt(int ti) { void* sv=rt->type_list; rt->type_list=_mod_type_list; Element* r=elmt(ti); rt->type_list=sv; return r; }\n");
        strbuf_append_str(tp->code_buf, "static Type* _mod_const_type(int ti) { void* sv=rt->type_list; rt->type_list=_mod_type_list; Type* r=const_type(ti); rt->type_list=sv; return r; }\n");
        strbuf_append_str(tp->code_buf, "static TypePattern* _mod_const_pattern(int ti) { void* sv=rt->type_list; rt->type_list=_mod_type_list; TypePattern* r=const_pattern(ti); rt->type_list=sv; return r; }\n");
        // Redirect calls to use module-local wrappers
        strbuf_append_str(tp->code_buf, "#define map(idx) _mod_map(idx)\n");
        strbuf_append_str(tp->code_buf, "#define elmt(idx) _mod_elmt(idx)\n");
        strbuf_append_str(tp->code_buf, "#define const_type(idx) _mod_const_type(idx)\n");
        strbuf_append_str(tp->code_buf, "#define const_pattern(idx) _mod_const_pattern(idx)\n");
    }

    // Pre-define all closure environment structs before any function definitions
    // This ensures structs are available when functions reference them
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstNode* item = ((AstListNode*)child)->item;
            while (item) {
                pre_define_closure_envs(tp, item);
                item = item->next;
            }
        } else {
            pre_define_closure_envs(tp, child);
        }
        child = child->next;
    }

    // Forward declare all top-level functions to support out-of-order definitions
    child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstNode* item = ((AstListNode*)child)->item;
            while (item) {
                if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC) {
                    forward_declare_func(tp, (AstFuncNode*)item);
                }
                item = item->next;
            }
            child = child->next;
        } else if (child->node_type == AST_NODE_FUNC || child->node_type == AST_NODE_PROC) {
            forward_declare_func(tp, (AstFuncNode*)child);
            child = child->next;
        } else {
            child = child->next;
        }
    }

    // declare global vars, types, define fns, etc.
    child = script->child;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_CONTENT:
            child = ((AstListNode*)child)->item;
            continue;  // restart the loop with the first content item
        // declare global vars, types
        case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
            declare_global_var(tp, (AstLetNode*)child);
        default:
            define_ast_node(tp, child);
        }
        child = child->next;
    }

    // for imported modules: generate self-struct and _init_mod_vars
    // _init_mod_vars copies module's global pub vars into the importer's Mod struct
    if (!tp->is_main) {
        strbuf_append_str(tp->code_buf, "\n// module self-struct (mirrors importer's Mod struct)\n");
        strbuf_append_format(tp->code_buf, "struct Mod%d {\n", tp->index);
        write_mod_struct_fields(tp, (AstNode*)script);
        strbuf_append_str(tp->code_buf, "};\n");

        // generate _init_mod_vars: copies each pub var into the Mod struct
        strbuf_append_str(tp->code_buf, "void _init_mod_vars(void* _mp) {\n");
        strbuf_append_format(tp->code_buf, " struct Mod%d* _m = (struct Mod%d*)_mp;\n", tp->index, tp->index);
        child = script->child;
        while (child) {
            if (child->node_type == AST_NODE_CONTENT) {
                child = ((AstListNode*)child)->item;
                continue;
            }
            else if (child->node_type == AST_NODE_PUB_STAM) {
                AstNode *declare = ((AstLetNode*)child)->declare;
                while (declare) {
                    AstNamedNode *asn_node = (AstNamedNode*)declare;
                    strbuf_append_str(tp->code_buf, " _m->");
                    write_var_name(tp->code_buf, asn_node, NULL);
                    strbuf_append_str(tp->code_buf, " = ");
                    write_var_name(tp->code_buf, asn_node, NULL);
                    strbuf_append_str(tp->code_buf, ";\n");
                    declare = declare->next;
                }
            }
            child = child->next;
        }
        strbuf_append_str(tp->code_buf, "}\n");

        // guard flag to ensure module main() executes only once
        strbuf_append_str(tp->code_buf, "static int _mod_executed = 0;\n");
    }

    // global evaluation, wrapped inside main()
    log_debug("transpile main() ...");
    strbuf_append_str(tp->code_buf, "\nItem main(Context *runtime) {\n _lambda_rt = runtime;\n");

    // for imported modules: early return if already executed (evaluate once)
    if (!tp->is_main) {
        strbuf_append_str(tp->code_buf, " if (_mod_executed) return ITEM_NULL;\n _mod_executed = 1;\n");
    }

    // initialize imported modules: call each module's main() then copy pub vars
    child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* imp = (AstImportNode*)child;
            if (imp->script) {
                strbuf_append_format(tp->code_buf, " m%d._mod_main(runtime);\n", imp->script->index);
                strbuf_append_format(tp->code_buf, " if (m%d._init_vars) m%d._init_vars(&m%d);\n",
                    imp->script->index, imp->script->index, imp->script->index);
            }
        }
        child = child->next;
    }

    // For modules: hoist LET/PUB_STAM assignments to top level of main()
    // MIR JIT may optimize away writes to global BSS variables when assignments
    // are inside GCC statement expressions ({...}). Moving them outside avoids this.
    if (!tp->is_main) {
        child = script->child;
        while (child) {
            if (child->node_type == AST_NODE_LET_STAM || child->node_type == AST_NODE_PUB_STAM) {
                assign_global_var(tp, (AstLetNode*)child);
            } else if (child->node_type == AST_NODE_CONTENT) {
                AstNode *item = ((AstListNode*)child)->item;
                while (item) {
                    if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM) {
                        assign_global_var(tp, (AstLetNode*)item);
                    }
                    item = item->next;
                }
            }
            child = child->next;
        }
        strbuf_append_str(tp->code_buf, "\n");
    }

    // transpile body content
    strbuf_append_str(tp->code_buf, " Item result = ({");
    child = script->child;
    bool has_content = false;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:  case AST_NODE_TYPE_STAM:
            assign_global_var(tp, (AstLetNode*)child);
            break;  // already handled
        case AST_NODE_IMPORT:  case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:  case AST_NODE_PROC:
        case AST_NODE_STRING_PATTERN:  case AST_NODE_SYMBOL_PATTERN:
            break;  // skip global definition nodes
        case AST_NODE_CONTENT:
            transpile_content_expr(tp, (AstListNode*)child, true);
            has_content = true;
            break;  // already handled
        default:
            // AST_NODE_PRIMARY, AST_NODE_BINARY, etc.
            log_debug("transpile main(): boxing child, node_type=%d, type=%d", child->node_type, child->type ? child->type->type_id : -1);
            transpile_box_item(tp, child);
            has_content = true;
        }
        child = child->next;
    }
    if (!has_content) { strbuf_append_str(tp->code_buf, "ITEM_NULL"); }
    strbuf_append_str(tp->code_buf, ";});\n");

    // transpile invocation of main procedure if defined
    log_debug("transpiling main proc (if any)...");
    child = script->child;
    bool has_main = false;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_PROC: {
            AstFuncNode* proc_node = (AstFuncNode*)child;
            log_debug("got global proc: %.*s", (int)proc_node->name->len, proc_node->name->chars);
            if (strcmp(proc_node->name->chars, "main") == 0) {
                strbuf_append_str(tp->code_buf, " if (rt->run_main) result = ");
                write_fn_name(tp->code_buf, proc_node, NULL);
                // todo: pass command line args
                strbuf_append_str(tp->code_buf, "();\n");
                has_main = true;
            }
            break;
        }
        case AST_NODE_CONTENT: {
            child = ((AstListNode*)child)->item;
            continue;  // restart the loop with the first content item
        }
        default:
            log_debug("not a proc: %d", child->node_type);
            break;  // skip other nodes
        }
        child = child->next;
    }
    log_debug("done transpiling main proc, has_main: %d", has_main);

    // return the result
    strbuf_append_str(tp->code_buf, " return result;\n}\n");
}
