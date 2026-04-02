// transpile_rb_mir.cpp - Direct MIR generation for Ruby
//
// Mirrors the Python MIR transpiler architecture (transpile_py_mir.cpp).
// All Ruby values are boxed Items (MIR_T_I64). Runtime functions
// (rb_add, rb_subtract, etc.) take and return Items.

#include "rb_transpiler.hpp"
#include "rb_runtime.h"
#include "../lambda-data.hpp"
#include "../module_registry.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../transpiler.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

// External references
extern "C" Context* _lambda_rt;
extern "C" {
    void* import_resolver(const char* name);
}

extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern "C" void rb_reset_module_vars();

// cross-language interop
extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_new_object();
extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" char* read_text_file(const char* filename);

// ============================================================================
// Constants
// ============================================================================
static const uint64_t RB_ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t RB_ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t RB_ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t RB_ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t RB_STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;
static const uint64_t RB_MASK56         = 0x00FFFFFFFFFFFFFFULL;

// ============================================================================
// Structures
// ============================================================================

struct RbMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;
};

struct RbLoopLabels {
    MIR_label_t continue_label; // next
    MIR_label_t break_label;    // break
};

struct RbFuncCollected {
    RbMethodDefNode* node;
    char name[128];
    MIR_item_t func_item;
    int param_count;
    int required_count;
    bool is_method;
    char class_name[128];
};

struct RbMirTranspiler {
    RbTranspiler* tp;

    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    struct hashmap* import_cache;
    struct hashmap* local_funcs;

    struct hashmap* var_scopes[64];
    int scope_depth;

    RbLoopLabels loop_stack[32];
    int loop_depth;

    int reg_counter;
    int label_counter;
    int temp_count;

    RbFuncCollected func_entries[128];
    int func_count;

    int module_var_count;
    char global_var_names[64][128];
    int global_var_indices[64];
    int global_var_count;

    bool in_main;

    const char* filename;
    Runtime* runtime;
};

// ============================================================================
// Hashmap helpers
// ============================================================================

struct RbImportCacheEntry {
    char name[128];
    MIR_item_t proto;
    MIR_item_t import;
};

static int rb_import_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((RbImportCacheEntry*)a)->name, ((RbImportCacheEntry*)b)->name);
}
static uint64_t rb_import_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((RbImportCacheEntry*)item)->name,
        strlen(((RbImportCacheEntry*)item)->name), seed0, seed1);
}

struct RbVarScopeEntry {
    char name[128];
    RbMirVarEntry var;
};

static int rb_var_scope_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((RbVarScopeEntry*)a)->name, ((RbVarScopeEntry*)b)->name);
}
static uint64_t rb_var_scope_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((RbVarScopeEntry*)item)->name,
        strlen(((RbVarScopeEntry*)item)->name), seed0, seed1);
}

struct RbLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

static int rb_local_func_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((RbLocalFuncEntry*)a)->name, ((RbLocalFuncEntry*)b)->name);
}
static uint64_t rb_local_func_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((RbLocalFuncEntry*)item)->name,
        strlen(((RbLocalFuncEntry*)item)->name), seed0, seed1);
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t rm_transpile_expression(RbMirTranspiler* mt, RbAstNode* expr);
static void rm_transpile_statement(RbMirTranspiler* mt, RbAstNode* stmt);
static MIR_reg_t rm_transpile_if_expr(RbMirTranspiler* mt, RbIfNode* ifs);
static void rm_transpile_multi_assignment(RbMirTranspiler* mt, RbMultiAssignmentNode* ma);

// ============================================================================
// Basic MIR helpers
// ============================================================================

static MIR_reg_t rm_new_reg(RbMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, mt->reg_counter++);
    MIR_type_t rtype = (type == MIR_T_P || type == MIR_T_F) ? MIR_T_I64 : type;
    return MIR_new_func_reg(mt->ctx, mt->current_func, rtype, name);
}

static MIR_label_t rm_new_label(RbMirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

static void rm_emit(RbMirTranspiler* mt, MIR_insn_t insn) {
    MIR_append_insn(mt->ctx, mt->current_func_item, insn);
}

static void rm_emit_label(RbMirTranspiler* mt, MIR_label_t label) {
    MIR_append_insn(mt->ctx, mt->current_func_item, label);
}

// ============================================================================
// Import management
// ============================================================================

static RbImportCacheEntry* rm_ensure_import(RbMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    RbImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    RbImportCacheEntry* found = (RbImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return found;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    RbImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.proto = proto;
    new_entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (RbImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return found;
}

// ============================================================================
// Call helpers — emit calls to runtime functions
// ============================================================================

static MIR_reg_t rm_call_0(RbMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = rm_new_reg(mt, fn_name, ret_type);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

static MIR_reg_t rm_call_1(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = rm_new_reg(mt, fn_name, ret_type);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1));
    return res;
}

static MIR_reg_t rm_call_2(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = rm_new_reg(mt, fn_name, ret_type);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2));
    return res;
}

static MIR_reg_t rm_call_3(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = rm_new_reg(mt, fn_name, ret_type);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3));
    return res;
}

static void rm_call_void_1(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1));
}

static void rm_call_void_2(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2));
}

// ============================================================================
// Scope management
// ============================================================================

static void rm_push_scope(RbMirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("rb-mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(RbVarScopeEntry), 16, 0, 0,
        rb_var_scope_hash, rb_var_scope_cmp, NULL, NULL);
}

static void rm_pop_scope(RbMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("rb-mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void rm_set_var(RbMirTranspiler* mt, const char* name, MIR_reg_t reg) {
    RbVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = MIR_T_I64;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static RbMirVarEntry* rm_find_var(RbMirTranspiler* mt, const char* name) {
    RbVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        RbVarScopeEntry* found = (RbVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Boxing helpers
// ============================================================================

static MIR_reg_t rm_emit_null(RbMirTranspiler* mt) {
    MIR_reg_t r = rm_new_reg(mt, "null", MIR_T_I64);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
    return r;
}

static MIR_reg_t rm_box_int_const(RbMirTranspiler* mt, int64_t value) {
    MIR_reg_t r = rm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = RB_ITEM_INT_TAG | ((uint64_t)value & RB_MASK56);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)tagged)));
    return r;
}

static MIR_reg_t rm_box_float(RbMirTranspiler* mt, MIR_reg_t d_reg) {
    return rm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d_reg));
}

static MIR_reg_t rm_box_string(RbMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = rm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    rm_emit_label(mt, l_nn);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)RB_STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    rm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t rm_box_string_literal(RbMirTranspiler* mt, const char* str, int len) {
    String* interned = name_pool_create_len(mt->tp->name_pool, str, len);
    MIR_reg_t ptr = rm_new_reg(mt, "cs", MIR_T_I64);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
    MIR_reg_t name_reg = rm_call_1(mt, "heap_create_name", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, ptr));
    return rm_box_string(mt, name_reg);
}

static MIR_reg_t rm_emit_bool(RbMirTranspiler* mt, bool value) {
    MIR_reg_t r = rm_new_reg(mt, "bool", MIR_T_I64);
    uint64_t bval = value ? RB_ITEM_TRUE_VAL : RB_ITEM_FALSE_VAL;
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)bval)));
    return r;
}

// ============================================================================
// Binary operator → runtime function mapping
// ============================================================================

static const char* rm_binary_op_func(RbOperator op) {
    switch (op) {
        case RB_OP_ADD:     return "rb_add";
        case RB_OP_SUB:     return "rb_subtract";
        case RB_OP_MUL:     return "rb_multiply";
        case RB_OP_DIV:     return "rb_divide";
        case RB_OP_MOD:     return "rb_modulo";
        case RB_OP_POW:     return "rb_power";
        case RB_OP_BIT_AND: return "rb_bit_and";
        case RB_OP_BIT_OR:  return "rb_bit_or";
        case RB_OP_BIT_XOR: return "rb_bit_xor";
        case RB_OP_LSHIFT:  return "rb_lshift";
        case RB_OP_RSHIFT:  return "rb_rshift";
        default:            return "rb_add";
    }
}

static const char* rm_comparison_func(RbOperator op) {
    switch (op) {
        case RB_OP_EQ:      return "rb_eq";
        case RB_OP_NEQ:     return "rb_ne";
        case RB_OP_LT:      return "rb_lt";
        case RB_OP_LE:      return "rb_le";
        case RB_OP_GT:      return "rb_gt";
        case RB_OP_GE:      return "rb_ge";
        case RB_OP_CMP:     return "rb_cmp";
        case RB_OP_CASE_EQ: return "rb_case_eq";
        default:            return "rb_eq";
    }
}

// ============================================================================
// Expression transpiler
// ============================================================================

static MIR_reg_t rm_transpile_literal(RbMirTranspiler* mt, RbLiteralNode* lit) {
    switch (lit->literal_type) {
        case RB_LITERAL_INT:
            return rm_box_int_const(mt, lit->value.int_value);
        case RB_LITERAL_FLOAT: {
            MIR_reg_t d = rm_new_reg(mt, "fd", MIR_T_D);
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, d),
                MIR_new_double_op(mt->ctx, lit->value.float_value)));
            return rm_box_float(mt, d);
        }
        case RB_LITERAL_STRING:
            if (lit->value.string_value) {
                return rm_box_string_literal(mt, lit->value.string_value->chars,
                    (int)lit->value.string_value->len);
            }
            return rm_box_string_literal(mt, "", 0);
        case RB_LITERAL_SYMBOL:
            if (lit->value.string_value) {
                return rm_box_string_literal(mt, lit->value.string_value->chars,
                    (int)lit->value.string_value->len);
            }
            return rm_box_string_literal(mt, "", 0);
        case RB_LITERAL_BOOLEAN:
            return rm_emit_bool(mt, lit->value.boolean_value);
        case RB_LITERAL_NIL:
            return rm_emit_null(mt);
    }
    return rm_emit_null(mt);
}

static MIR_reg_t rm_transpile_identifier(RbMirTranspiler* mt, RbIdentifierNode* id) {
    char vname[128];
    snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);

    RbMirVarEntry* var = rm_find_var(mt, vname);
    if (var) {
        MIR_reg_t r = rm_new_reg(mt, "id", MIR_T_I64);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_reg_op(mt->ctx, var->reg)));
        return r;
    }

    // check if it's a local function
    RbLocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "_rb_%.*s", (int)id->name->len, id->name->chars);
    RbLocalFuncEntry* fentry = (RbLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
    if (fentry) {
        // return function as Item (js_new_function wrapper)
        MIR_reg_t fn_ptr = rm_new_reg(mt, "fnptr", MIR_T_I64);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_ptr),
            MIR_new_ref_op(mt->ctx, fentry->func_item)));
        // find param count
        int pc = 0;
        for (int i = 0; i < mt->func_count; i++) {
            if (strcmp(mt->func_entries[i].name, id->name->chars) == 0) {
                pc = mt->func_entries[i].param_count;
                break;
            }
        }
        return rm_call_2(mt, "js_new_function", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_ptr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
    }

    // check module vars
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], vname) == 0) {
            return rm_call_1(mt, "rb_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, mt->global_var_indices[i]));
        }
    }

    log_debug("rb-mir: undefined variable '%.*s'", (int)id->name->len, id->name->chars);
    return rm_emit_null(mt);
}

static MIR_reg_t rm_transpile_binary(RbMirTranspiler* mt, RbBinaryNode* bin) {
    MIR_reg_t left = rm_transpile_expression(mt, bin->left);
    MIR_reg_t right = rm_transpile_expression(mt, bin->right);
    const char* fn = rm_binary_op_func(bin->op);
    return rm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
}

static MIR_reg_t rm_transpile_comparison(RbMirTranspiler* mt, RbBinaryNode* cmp) {
    MIR_reg_t left = rm_transpile_expression(mt, cmp->left);
    MIR_reg_t right = rm_transpile_expression(mt, cmp->right);
    const char* fn = rm_comparison_func(cmp->op);
    return rm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
}

static MIR_reg_t rm_transpile_unary(RbMirTranspiler* mt, RbUnaryNode* un) {
    MIR_reg_t operand = rm_transpile_expression(mt, un->operand);

    switch (un->op) {
        case RB_OP_NEGATE:
            return rm_call_1(mt, "rb_negate", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, operand));
        case RB_OP_BIT_NOT:
            return rm_call_1(mt, "rb_bit_not", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, operand));
        default:
            return operand;
    }
}

static MIR_reg_t rm_transpile_boolean(RbMirTranspiler* mt, RbBooleanNode* bop) {
    if (bop->op == RB_OP_NOT) {
        // unary not
        MIR_reg_t operand = rm_transpile_expression(mt, bop->left);
        MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, operand));
        MIR_reg_t result = rm_new_reg(mt, "not", MIR_T_I64);
        MIR_label_t l_false = rm_new_label(mt);
        MIR_label_t l_end = rm_new_label(mt);

        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_false),
            MIR_new_reg_op(mt->ctx, truthy)));
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_TRUE_VAL)));
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        rm_emit_label(mt, l_false);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_FALSE_VAL)));
        rm_emit_label(mt, l_end);
        return result;
    }

    // binary && or ||
    // Ruby short-circuits and returns the deciding value (not always bool)
    MIR_reg_t left = rm_transpile_expression(mt, bop->left);
    MIR_reg_t truthy_left = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left));
    MIR_reg_t result = rm_new_reg(mt, "bop", MIR_T_I64);
    MIR_label_t l_short = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);

    if (bop->op == RB_OP_AND) {
        // && : if left is falsy, return left; else evaluate right
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_short),
            MIR_new_reg_op(mt->ctx, truthy_left)));
        MIR_reg_t right = rm_transpile_expression(mt, bop->right);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, right)));
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        rm_emit_label(mt, l_short);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, left)));
        rm_emit_label(mt, l_end);
    } else {
        // || : if left is truthy, return left; else evaluate right
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_short),
            MIR_new_reg_op(mt->ctx, truthy_left)));
        MIR_reg_t right = rm_transpile_expression(mt, bop->right);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, right)));
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        rm_emit_label(mt, l_short);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, left)));
        rm_emit_label(mt, l_end);
    }

    return result;
}

static MIR_reg_t rm_transpile_array(RbMirTranspiler* mt, RbArrayNode* arr) {
    MIR_reg_t a = rm_call_0(mt, "rb_array_new", MIR_T_I64);

    RbAstNode* elem = arr->elements;
    while (elem) {
        MIR_reg_t val = rm_transpile_expression(mt, elem);
        rm_call_void_2(mt, "rb_array_push",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, a),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        elem = elem->next;
    }

    return a;
}

static MIR_reg_t rm_transpile_hash(RbMirTranspiler* mt, RbHashNode* hash) {
    MIR_reg_t h = rm_call_0(mt, "rb_hash_new", MIR_T_I64);

    RbAstNode* pair_node = hash->pairs;
    while (pair_node) {
        if (pair_node->node_type == RB_AST_NODE_PAIR) {
            RbPairNode* pair = (RbPairNode*)pair_node;
            MIR_reg_t key = rm_transpile_expression(mt, pair->key);
            MIR_reg_t val = rm_transpile_expression(mt, pair->value);
            rm_call_3(mt, "rb_hash_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, h),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        pair_node = pair_node->next;
    }

    return h;
}

static MIR_reg_t rm_transpile_range(RbMirTranspiler* mt, RbRangeNode* rng) {
    MIR_reg_t start = rng->start ? rm_transpile_expression(mt, rng->start) : rm_emit_null(mt);
    MIR_reg_t end = rng->end ? rm_transpile_expression(mt, rng->end) : rm_emit_null(mt);
    // rb_range_new takes int exclusive (0 or 1), not Item
    return rm_call_3(mt, "rb_range_new", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, start),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, end),
        MIR_T_I64, MIR_new_int_op(mt->ctx, rng->exclusive ? 1 : 0));
}

static MIR_reg_t rm_transpile_subscript(RbMirTranspiler* mt, RbSubscriptNode* sub) {
    MIR_reg_t obj = rm_transpile_expression(mt, sub->object);
    MIR_reg_t idx = rm_transpile_expression(mt, sub->index);
    return rm_call_2(mt, "rb_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
}

static MIR_reg_t rm_transpile_string_interp(RbMirTranspiler* mt, RbStringInterpNode* si) {
    // concatenate all parts using rb_add (string + string)
    MIR_reg_t result = rm_box_string_literal(mt, "", 0);

    RbAstNode* part = si->parts;
    while (part) {
        MIR_reg_t pval = rm_transpile_expression(mt, part);
        // convert to string if needed
        MIR_reg_t str_val = rm_call_1(mt, "rb_to_s", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pval));
        result = rm_call_2(mt, "rb_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, str_val));
        part = part->next;
    }

    return result;
}

static MIR_reg_t rm_transpile_call(RbMirTranspiler* mt, RbCallNode* call) {
    // method call with receiver: obj.method(args)
    if (call->receiver) {
        MIR_reg_t recv = rm_transpile_expression(mt, call->receiver);

        // special method handling
        if (call->method_name) {
            const char* mname = call->method_name->chars;
            int mlen = (int)call->method_name->len;

            // .length / .size
            if ((mlen == 6 && strncmp(mname, "length", 6) == 0) ||
                (mlen == 4 && strncmp(mname, "size", 4) == 0)) {
                return rm_call_1(mt, "rb_array_length", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            }
            // .push / .<<
            if ((mlen == 4 && strncmp(mname, "push", 4) == 0) && call->arg_count >= 1) {
                MIR_reg_t arg = rm_transpile_expression(mt, call->args);
                rm_call_void_2(mt, "rb_array_push",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
                return recv;
            }
            // .to_s
            if (mlen == 4 && strncmp(mname, "to_s", 4) == 0) {
                return rm_call_1(mt, "rb_to_s", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            }
            // .to_i
            if (mlen == 4 && strncmp(mname, "to_i", 4) == 0) {
                return rm_call_1(mt, "rb_to_i", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            }
            // .to_f
            if (mlen == 4 && strncmp(mname, "to_f", 4) == 0) {
                return rm_call_1(mt, "rb_to_f", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            }
        }

        // generic method call via attribute access
        MIR_reg_t method_name = rm_box_string_literal(mt, call->method_name->chars,
            (int)call->method_name->len);

        // call as getattr then invoke — for now, just return nil for unsupported methods
        log_debug("rb-mir: unsupported method call '%.*s'",
            (int)call->method_name->len, call->method_name->chars);
        return rm_emit_null(mt);
    }

    // bare function call: method_name(args)
    if (call->method_name) {
        const char* fname = call->method_name->chars;
        int flen = (int)call->method_name->len;

        // built-in functions
        if (flen == 4 && strncmp(fname, "puts", 4) == 0) {
            if (call->arg_count == 0) {
                // puts with no args → print newline
                MIR_reg_t empty = rm_box_string_literal(mt, "", 0);
                return rm_call_1(mt, "rb_puts_one", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, empty));
            }
            RbAstNode* arg = call->args;
            MIR_reg_t ret = rm_emit_null(mt);
            while (arg) {
                MIR_reg_t av = rm_transpile_expression(mt, arg);
                ret = rm_call_1(mt, "rb_puts_one", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, av));
                arg = arg->next;
            }
            return ret;
        }
        if (flen == 5 && strncmp(fname, "print", 5) == 0) {
            RbAstNode* arg = call->args;
            MIR_reg_t ret = rm_emit_null(mt);
            while (arg) {
                MIR_reg_t av = rm_transpile_expression(mt, arg);
                ret = rm_call_1(mt, "rb_print_one", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, av));
                arg = arg->next;
            }
            return ret;
        }
        if (flen == 1 && fname[0] == 'p') {
            RbAstNode* arg = call->args;
            MIR_reg_t ret = rm_emit_null(mt);
            while (arg) {
                MIR_reg_t av = rm_transpile_expression(mt, arg);
                ret = rm_call_1(mt, "rb_p_one", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, av));
                arg = arg->next;
            }
            return ret;
        }

        // check for user-defined function
        char vname[128];
        snprintf(vname, sizeof(vname), "_rb_%.*s", flen, fname);
        RbLocalFuncEntry fkey;
        memset(&fkey, 0, sizeof(fkey));
        snprintf(fkey.name, sizeof(fkey.name), "%s", vname);
        RbLocalFuncEntry* fentry = (RbLocalFuncEntry*)hashmap_get(mt->local_funcs, &fkey);

        if (fentry) {
            // direct call to locally compiled function
            // find the function's full param count for padding optional args
            int call_nargs = call->arg_count;
            int func_nparams = call_nargs;
            for (int fi = 0; fi < mt->func_count; fi++) {
                if (strcmp(mt->func_entries[fi].name, fname) == 0) {
                    func_nparams = mt->func_entries[fi].param_count;
                    break;
                }
            }
            int nargs = (call_nargs < func_nparams) ? func_nparams : call_nargs;
            int nops = 3 + nargs; // proto + func_ref + result + args

            // build prototype for the call
            MIR_var_t* param_vars = nargs > 0 ? (MIR_var_t*)alloca(sizeof(MIR_var_t) * nargs) : NULL;
            for (int i = 0; i < nargs; i++) {
                param_vars[i] = {MIR_T_I64, "a", 0};
            }

            char proto_name[140];
            snprintf(proto_name, sizeof(proto_name), "call_%s_%d_p", fname, mt->temp_count++);
            MIR_type_t res_type = MIR_T_I64;
            MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &res_type, nargs, param_vars);

            MIR_reg_t result = rm_new_reg(mt, "cr", MIR_T_I64);

            // build ops array for MIR_new_insn_arr
            MIR_op_t* ops = (MIR_op_t*)alloca(sizeof(MIR_op_t) * (size_t)nops);
            ops[0] = MIR_new_ref_op(mt->ctx, proto);
            ops[1] = MIR_new_ref_op(mt->ctx, fentry->func_item);
            ops[2] = MIR_new_reg_op(mt->ctx, result);

            RbAstNode* arg = call->args;
            for (int i = 0; i < call_nargs && arg; i++) {
                MIR_reg_t av = rm_transpile_expression(mt, arg);
                ops[3 + i] = MIR_new_reg_op(mt->ctx, av);
                arg = arg->next;
            }
            // pad missing args with ITEM_NULL for default parameters
            for (int i = call_nargs; i < nargs; i++) {
                ops[3 + i] = MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL);
            }

            rm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, (size_t)nops, ops));
            return result;
        }
    }

    log_debug("rb-mir: unresolved call '%s'",
        call->method_name ? call->method_name->chars : "?");
    return rm_emit_null(mt);
}

static MIR_reg_t rm_transpile_ternary(RbMirTranspiler* mt, RbTernaryNode* tern) {
    MIR_reg_t cond = rm_transpile_expression(mt, tern->condition);
    MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cond));

    MIR_reg_t result = rm_new_reg(mt, "tern", MIR_T_I64);
    MIR_label_t l_else = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);

    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, truthy)));
    MIR_reg_t tv = rm_transpile_expression(mt, tern->true_expr);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tv)));
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    rm_emit_label(mt, l_else);
    MIR_reg_t fv = rm_transpile_expression(mt, tern->false_expr);
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, fv)));
    rm_emit_label(mt, l_end);

    return result;
}

static MIR_reg_t rm_transpile_symbol(RbMirTranspiler* mt, RbSymbolNode* sym) {
    return rm_box_string_literal(mt, sym->name->chars, (int)sym->name->len);
}

static MIR_reg_t rm_transpile_ivar(RbMirTranspiler* mt, RbIvarNode* iv) {
    // instance variables stored as module vars for now
    char vname[128];
    snprintf(vname, sizeof(vname), "_rb_ivar_%.*s", (int)iv->name->len, iv->name->chars);
    RbMirVarEntry* var = rm_find_var(mt, vname);
    if (var) {
        MIR_reg_t r = rm_new_reg(mt, "iv", MIR_T_I64);
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_reg_op(mt->ctx, var->reg)));
        return r;
    }
    return rm_emit_null(mt);
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static MIR_reg_t rm_transpile_expression(RbMirTranspiler* mt, RbAstNode* expr) {
    if (!expr) return rm_emit_null(mt);

    switch (expr->node_type) {
        case RB_AST_NODE_LITERAL:
            return rm_transpile_literal(mt, (RbLiteralNode*)expr);
        case RB_AST_NODE_IDENTIFIER:
            return rm_transpile_identifier(mt, (RbIdentifierNode*)expr);
        case RB_AST_NODE_SELF:
            return rm_emit_null(mt); // self → nil for now
        case RB_AST_NODE_BINARY_OP:
            return rm_transpile_binary(mt, (RbBinaryNode*)expr);
        case RB_AST_NODE_COMPARISON:
            return rm_transpile_comparison(mt, (RbBinaryNode*)expr);
        case RB_AST_NODE_UNARY_OP:
            return rm_transpile_unary(mt, (RbUnaryNode*)expr);
        case RB_AST_NODE_BOOLEAN_OP:
            return rm_transpile_boolean(mt, (RbBooleanNode*)expr);
        case RB_AST_NODE_CALL:
            return rm_transpile_call(mt, (RbCallNode*)expr);
        case RB_AST_NODE_SUBSCRIPT:
            return rm_transpile_subscript(mt, (RbSubscriptNode*)expr);
        case RB_AST_NODE_ARRAY:
            return rm_transpile_array(mt, (RbArrayNode*)expr);
        case RB_AST_NODE_HASH:
            return rm_transpile_hash(mt, (RbHashNode*)expr);
        case RB_AST_NODE_RANGE:
            return rm_transpile_range(mt, (RbRangeNode*)expr);
        case RB_AST_NODE_STRING_INTERPOLATION:
            return rm_transpile_string_interp(mt, (RbStringInterpNode*)expr);
        case RB_AST_NODE_TERNARY:
            return rm_transpile_ternary(mt, (RbTernaryNode*)expr);
        case RB_AST_NODE_SYMBOL:
            return rm_transpile_symbol(mt, (RbSymbolNode*)expr);
        case RB_AST_NODE_IVAR:
            return rm_transpile_ivar(mt, (RbIvarNode*)expr);
        case RB_AST_NODE_CVAR:
        case RB_AST_NODE_GVAR:
        case RB_AST_NODE_CONST:
            // treat as identifier for now — look up by name
            if (expr->node_type == RB_AST_NODE_GVAR) {
                RbGvarNode* gv = (RbGvarNode*)expr;
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_gvar_%.*s", (int)gv->name->len, gv->name->chars);
                RbMirVarEntry* var = rm_find_var(mt, vname);
                if (var) {
                    MIR_reg_t r = rm_new_reg(mt, "gv", MIR_T_I64);
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                        MIR_new_reg_op(mt->ctx, var->reg)));
                    return r;
                }
            } else if (expr->node_type == RB_AST_NODE_CONST) {
                RbConstNode* cn = (RbConstNode*)expr;
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)cn->name->len, cn->name->chars);
                RbMirVarEntry* var = rm_find_var(mt, vname);
                if (var) {
                    MIR_reg_t r = rm_new_reg(mt, "cn", MIR_T_I64);
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                        MIR_new_reg_op(mt->ctx, var->reg)));
                    return r;
                }
            }
            return rm_emit_null(mt);
        case RB_AST_NODE_IF:
        case RB_AST_NODE_UNLESS:
            return rm_transpile_if_expr(mt, (RbIfNode*)expr);
        case RB_AST_NODE_ASSIGNMENT: {
            // assignment as expression — transpile and return the value
            RbAssignmentNode* assign = (RbAssignmentNode*)expr;
            MIR_reg_t val = rm_transpile_expression(mt, assign->value);
            if (assign->target && assign->target->node_type == RB_AST_NODE_IDENTIFIER) {
                RbIdentifierNode* id = (RbIdentifierNode*)assign->target;
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
                RbMirVarEntry* var = rm_find_var(mt, vname);
                if (var) {
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                        MIR_new_reg_op(mt->ctx, val)));
                } else {
                    MIR_reg_t r = rm_new_reg(mt, vname, MIR_T_I64);
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                        MIR_new_reg_op(mt->ctx, val)));
                    rm_set_var(mt, vname, r);
                }
            }
            return val;
        }
        case RB_AST_NODE_MULTI_ASSIGNMENT: {
            rm_transpile_multi_assignment(mt, (RbMultiAssignmentNode*)expr);
            return rm_emit_null(mt);
        }
        default:
            log_debug("rb-mir: unhandled expression type: %d", expr->node_type);
            return rm_emit_null(mt);
    }
}

// if/unless as expression — returns the last value from the taken branch
static MIR_reg_t rm_transpile_if_expr(RbMirTranspiler* mt, RbIfNode* ifs) {
    MIR_reg_t result = rm_new_reg(mt, "ifr", MIR_T_I64);
    // initialize to nil
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));

    MIR_reg_t cond = rm_transpile_expression(mt, ifs->condition);
    MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cond));

    MIR_label_t l_else = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);

    if (ifs->is_unless) {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_else),
            MIR_new_reg_op(mt->ctx, truthy)));
    } else {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // then body — last expression is the value
    {
        MIR_reg_t last = rm_emit_null(mt);
        RbAstNode* stmt = ifs->then_body;
        while (stmt) {
            if (!stmt->next) {
                last = rm_transpile_expression(mt, stmt);
            } else {
                rm_transpile_statement(mt, stmt);
            }
            stmt = stmt->next;
        }
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, last)));
    }
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    rm_emit_label(mt, l_else);

    // elsif chain
    if (ifs->elsif_chain) {
        RbAstNode* elsif = ifs->elsif_chain;
        while (elsif) {
            if (elsif->node_type == RB_AST_NODE_IF || elsif->node_type == RB_AST_NODE_UNLESS) {
                MIR_reg_t ev = rm_transpile_if_expr(mt, (RbIfNode*)elsif);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, ev)));
            }
            elsif = elsif->next;
        }
    }

    // else body
    if (ifs->else_body) {
        MIR_reg_t last = rm_emit_null(mt);
        RbAstNode* es = ifs->else_body;
        while (es) {
            if (!es->next) {
                last = rm_transpile_expression(mt, es);
            } else {
                rm_transpile_statement(mt, es);
            }
            es = es->next;
        }
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, last)));
    }

    rm_emit_label(mt, l_end);
    return result;
}

// ============================================================================
// Statement transpiler
// ============================================================================

static void rm_transpile_assignment(RbMirTranspiler* mt, RbAssignmentNode* assign) {
    MIR_reg_t val = rm_transpile_expression(mt, assign->value);

    if (assign->target && assign->target->node_type == RB_AST_NODE_IDENTIFIER) {
        RbIdentifierNode* id = (RbIdentifierNode*)assign->target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);

        RbMirVarEntry* var = rm_find_var(mt, vname);
        if (var) {
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, val)));
        } else {
            MIR_reg_t r = rm_new_reg(mt, vname, MIR_T_I64);
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, val)));
            rm_set_var(mt, vname, r);
        }
    } else if (assign->target && assign->target->node_type == RB_AST_NODE_SUBSCRIPT) {
        RbSubscriptNode* sub = (RbSubscriptNode*)assign->target;
        MIR_reg_t obj = rm_transpile_expression(mt, sub->object);
        MIR_reg_t idx = rm_transpile_expression(mt, sub->index);
        rm_call_3(mt, "rb_subscript_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    } else if (assign->target && assign->target->node_type == RB_AST_NODE_IVAR) {
        RbIvarNode* iv = (RbIvarNode*)assign->target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_rb_ivar_%.*s", (int)iv->name->len, iv->name->chars);
        RbMirVarEntry* var = rm_find_var(mt, vname);
        if (var) {
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, val)));
        } else {
            MIR_reg_t r = rm_new_reg(mt, vname, MIR_T_I64);
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, val)));
            rm_set_var(mt, vname, r);
        }
    }
}

static void rm_transpile_op_assignment(RbMirTranspiler* mt, RbOpAssignmentNode* opas) {
    // x += val → x = x <op> val
    MIR_reg_t old_val = rm_transpile_expression(mt, opas->target);
    MIR_reg_t rhs = rm_transpile_expression(mt, opas->value);
    const char* fn = rm_binary_op_func(opas->op);
    MIR_reg_t new_val = rm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, old_val),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, rhs));

    if (opas->target && opas->target->node_type == RB_AST_NODE_IDENTIFIER) {
        RbIdentifierNode* id = (RbIdentifierNode*)opas->target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
        RbMirVarEntry* var = rm_find_var(mt, vname);
        if (var) {
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, new_val)));
        }
    }
}

static void rm_transpile_multi_assignment(RbMirTranspiler* mt, RbMultiAssignmentNode* ma) {
    // evaluate all values first into temp regs
    int nvals = ma->value_count;
    MIR_reg_t* val_regs = (MIR_reg_t*)alloca(sizeof(MIR_reg_t) * (nvals > 0 ? nvals : 1));
    RbAstNode* val = ma->values;
    for (int i = 0; i < nvals && val; i++) {
        val_regs[i] = rm_transpile_expression(mt, val);
        val = val->next;
    }

    // assign to each target
    RbAstNode* target = ma->targets;
    for (int i = 0; i < ma->target_count && target; i++) {
        MIR_reg_t v = (i < nvals) ? val_regs[i] : rm_emit_null(mt);

        if (target->node_type == RB_AST_NODE_IDENTIFIER) {
            RbIdentifierNode* id = (RbIdentifierNode*)target;
            char vname[128];
            snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
            RbMirVarEntry* var = rm_find_var(mt, vname);
            if (var) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, v)));
            } else {
                MIR_reg_t r = rm_new_reg(mt, vname, MIR_T_I64);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, v)));
                rm_set_var(mt, vname, r);
            }
        }
        target = target->next;
    }
}

static void rm_transpile_if(RbMirTranspiler* mt, RbIfNode* ifs) {
    MIR_reg_t cond = rm_transpile_expression(mt, ifs->condition);
    MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cond));

    MIR_label_t l_else = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);

    if (ifs->is_unless) {
        // unless: branch to then when falsy
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_else),
            MIR_new_reg_op(mt->ctx, truthy)));
    } else {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // then body
    RbAstNode* stmt = ifs->then_body;
    while (stmt) {
        rm_transpile_statement(mt, stmt);
        stmt = stmt->next;
    }
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    rm_emit_label(mt, l_else);

    // elsif chain
    if (ifs->elsif_chain) {
        RbAstNode* elsif = ifs->elsif_chain;
        while (elsif) {
            if (elsif->node_type == RB_AST_NODE_IF || elsif->node_type == RB_AST_NODE_UNLESS) {
                rm_transpile_if(mt, (RbIfNode*)elsif);
            }
            elsif = elsif->next;
        }
    }

    // else body
    if (ifs->else_body) {
        RbAstNode* es = ifs->else_body;
        while (es) {
            rm_transpile_statement(mt, es);
            es = es->next;
        }
    }

    rm_emit_label(mt, l_end);
}

static void rm_transpile_while(RbMirTranspiler* mt, RbWhileNode* wh) {
    MIR_label_t l_top = rm_new_label(mt);
    MIR_label_t l_break = rm_new_label(mt);

    // push loop labels
    mt->loop_stack[mt->loop_depth].continue_label = l_top;
    mt->loop_stack[mt->loop_depth].break_label = l_break;
    mt->loop_depth++;

    rm_emit_label(mt, l_top);

    MIR_reg_t cond = rm_transpile_expression(mt, wh->condition);
    MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cond));

    if (wh->is_until) {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_break),
            MIR_new_reg_op(mt->ctx, truthy)));
    } else {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_break),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // body
    RbAstNode* stmt = wh->body;
    while (stmt) {
        rm_transpile_statement(mt, stmt);
        stmt = stmt->next;
    }

    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_top)));
    rm_emit_label(mt, l_break);

    mt->loop_depth--;
}

static void rm_transpile_for(RbMirTranspiler* mt, RbForNode* f) {
    MIR_label_t l_top = rm_new_label(mt);
    MIR_label_t l_break = rm_new_label(mt);

    mt->loop_stack[mt->loop_depth].continue_label = l_top;
    mt->loop_stack[mt->loop_depth].break_label = l_break;
    mt->loop_depth++;

    MIR_reg_t collection = rm_transpile_expression(mt, f->collection);
    MIR_reg_t iter = rm_call_1(mt, "rb_get_iterator", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, collection));

    rm_emit_label(mt, l_top);

    MIR_reg_t item = rm_call_1(mt, "rb_iterator_next", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iter));
    MIR_reg_t is_stop = rm_call_1(mt, "rb_is_stop_iteration", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, item));
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_break),
        MIR_new_reg_op(mt->ctx, is_stop)));

    // assign iteration variable
    if (f->variable && f->variable->node_type == RB_AST_NODE_IDENTIFIER) {
        RbIdentifierNode* id = (RbIdentifierNode*)f->variable;
        char vname[128];
        snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
        RbMirVarEntry* var = rm_find_var(mt, vname);
        if (var) {
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, item)));
        } else {
            MIR_reg_t r = rm_new_reg(mt, vname, MIR_T_I64);
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, item)));
            rm_set_var(mt, vname, r);
        }
    }

    // body
    RbAstNode* stmt = f->body;
    while (stmt) {
        rm_transpile_statement(mt, stmt);
        stmt = stmt->next;
    }

    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_top)));
    rm_emit_label(mt, l_break);

    mt->loop_depth--;
}

static void rm_transpile_return(RbMirTranspiler* mt, RbReturnNode* ret) {
    MIR_reg_t val = ret->value ? rm_transpile_expression(mt, ret->value) : rm_emit_null(mt);
    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
}

static void rm_transpile_case(RbMirTranspiler* mt, RbCaseNode* cs) {
    MIR_reg_t subject = cs->subject ? rm_transpile_expression(mt, cs->subject) : rm_emit_null(mt);
    MIR_label_t l_end = rm_new_label(mt);

    RbAstNode* when_node = cs->whens;
    while (when_node) {
        if (when_node->node_type == RB_AST_NODE_WHEN) {
            RbWhenNode* w = (RbWhenNode*)when_node;
            MIR_label_t l_next_when = rm_new_label(mt);

            // check patterns
            RbAstNode* pat = w->patterns;
            MIR_label_t l_match = rm_new_label(mt);
            while (pat) {
                MIR_reg_t pat_val = rm_transpile_expression(mt, pat);
                MIR_reg_t eq = rm_call_2(mt, "rb_case_eq", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, pat_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, subject));
                MIR_reg_t t = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, eq));
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_match),
                    MIR_new_reg_op(mt->ctx, t)));
                pat = pat->next;
            }
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_next_when)));

            rm_emit_label(mt, l_match);
            RbAstNode* ws = w->body;
            while (ws) {
                rm_transpile_statement(mt, ws);
                ws = ws->next;
            }
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            rm_emit_label(mt, l_next_when);
        }
        when_node = when_node->next;
    }

    // else
    if (cs->else_body) {
        RbAstNode* es = cs->else_body;
        while (es) {
            rm_transpile_statement(mt, es);
            es = es->next;
        }
    }

    rm_emit_label(mt, l_end);
}

// ============================================================================
// Statement dispatcher
// ============================================================================

static void rm_transpile_statement(RbMirTranspiler* mt, RbAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
        case RB_AST_NODE_ASSIGNMENT:
            rm_transpile_assignment(mt, (RbAssignmentNode*)stmt);
            break;
        case RB_AST_NODE_OP_ASSIGNMENT:
            rm_transpile_op_assignment(mt, (RbOpAssignmentNode*)stmt);
            break;
        case RB_AST_NODE_MULTI_ASSIGNMENT:
            rm_transpile_multi_assignment(mt, (RbMultiAssignmentNode*)stmt);
            break;
        case RB_AST_NODE_IF:
        case RB_AST_NODE_UNLESS:
            rm_transpile_if(mt, (RbIfNode*)stmt);
            break;
        case RB_AST_NODE_WHILE:
        case RB_AST_NODE_UNTIL:
            rm_transpile_while(mt, (RbWhileNode*)stmt);
            break;
        case RB_AST_NODE_FOR:
            rm_transpile_for(mt, (RbForNode*)stmt);
            break;
        case RB_AST_NODE_RETURN:
            rm_transpile_return(mt, (RbReturnNode*)stmt);
            break;
        case RB_AST_NODE_CASE:
            rm_transpile_case(mt, (RbCaseNode*)stmt);
            break;
        case RB_AST_NODE_BREAK:
            if (mt->loop_depth > 0) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
            }
            break;
        case RB_AST_NODE_NEXT:
            if (mt->loop_depth > 0) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
            }
            break;
        case RB_AST_NODE_METHOD_DEF:
            // skip — compiled separately in Phase 3
            break;
        case RB_AST_NODE_CLASS_DEF:
        case RB_AST_NODE_MODULE_DEF:
            // phase 1: skip class/module compilation
            break;
        default:
            // treat as expression statement
            rm_transpile_expression(mt, stmt);
            break;
    }
}

// ============================================================================
// Function collection and compilation
// ============================================================================

static void rm_collect_functions(RbMirTranspiler* mt, RbAstNode* root) {
    if (!root) return;

    if (root->node_type == RB_AST_NODE_PROGRAM) {
        RbProgramNode* prog = (RbProgramNode*)root;
        RbAstNode* s = prog->body;
        while (s) {
            if (s->node_type == RB_AST_NODE_METHOD_DEF) {
                RbMethodDefNode* meth = (RbMethodDefNode*)s;
                if (mt->func_count < 128 && meth->name) {
                    RbFuncCollected* fc = &mt->func_entries[mt->func_count];
                    memset(fc, 0, sizeof(RbFuncCollected));
                    fc->node = meth;
                    snprintf(fc->name, sizeof(fc->name), "%.*s",
                        (int)meth->name->len, meth->name->chars);
                    fc->param_count = meth->param_count;
                    fc->required_count = meth->required_count;
                    mt->func_count++;
                }
            }
            s = s->next;
        }
    }
}

static void rm_compile_function(RbMirTranspiler* mt, RbFuncCollected* fc) {
    RbMethodDefNode* meth = fc->node;

    // build MIR parameter list
    int nparams = fc->param_count;
    MIR_var_t* params = nparams > 0 ? (MIR_var_t*)alloca(sizeof(MIR_var_t) * nparams) : NULL;

    RbAstNode* p = meth->params;
    for (int i = 0; i < nparams && p; i++) {
        char pname[128];
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            snprintf(pname, sizeof(pname), "_rb_%.*s",
                pp->name ? (int)pp->name->len : 0,
                pp->name ? pp->name->chars : "p");
        } else {
            snprintf(pname, sizeof(pname), "_rb_p%d", i);
        }
        // allocate a stable string for param name
        char* stable_name = (char*)pool_alloc(mt->tp->ast_pool, strlen(pname) + 1);
        strcpy(stable_name, pname);
        params[i] = {MIR_T_I64, stable_name, 0};
        p = p->next;
    }

    // create MIR function
    char fn_name[260];
    snprintf(fn_name, sizeof(fn_name), "rbu_%s", fc->name);

    MIR_type_t res_type = MIR_T_I64;
    fc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type, nparams, params);

    // save parent state
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;

    mt->current_func_item = fc->func_item;
    mt->current_func = fc->func_item->u.func;

    rm_push_scope(mt);

    // register params as variables
    p = meth->params;
    for (int i = 0; i < nparams && p; i++) {
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->name) {
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)pp->name->len, pp->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, params[i].name, mt->current_func);
                rm_set_var(mt, vname, preg);
            }
        }
        p = p->next;
    }

    // emit default parameter assignments: if param is null, set to default value
    p = meth->params;
    for (int i = 0; i < nparams && p; i++) {
        if (p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->name && pp->default_value) {
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)pp->name->len, pp->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, params[i].name, mt->current_func);

                // if (param == ITEM_NULL) param = default_value
                MIR_label_t l_skip = rm_new_label(mt);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, l_skip),
                    MIR_new_reg_op(mt->ctx, preg),
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
                MIR_reg_t def_val = rm_transpile_expression(mt, pp->default_value);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, preg),
                    MIR_new_reg_op(mt->ctx, def_val)));
                rm_emit(mt, l_skip);
            }
        }
        p = p->next;
    }

    // transpile body
    MIR_reg_t last_result = rm_emit_null(mt);
    RbAstNode* s = meth->body;
    while (s) {
        if (s->node_type == RB_AST_NODE_RETURN) {
            rm_transpile_return(mt, (RbReturnNode*)s);
            s = s->next;
            continue;
        }
        // for the last statement, capture its result
        if (!s->next) {
            // last statement — implicit return
            last_result = rm_transpile_expression(mt, s);
        } else {
            rm_transpile_statement(mt, s);
        }
        s = s->next;
    }

    // emit default return
    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, last_result)));

    MIR_finish_func(mt->ctx);
    rm_pop_scope(mt);

    // restore parent state
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;

    // register in local_funcs
    RbLocalFuncEntry lfe;
    memset(&lfe, 0, sizeof(lfe));
    snprintf(lfe.name, sizeof(lfe.name), "_rb_%s", fc->name);
    lfe.func_item = fc->func_item;
    hashmap_set(mt->local_funcs, &lfe);
}

// ============================================================================
// Module-level variable scanning
// ============================================================================

static void rm_scan_module_vars(RbMirTranspiler* mt, RbAstNode* root) {
    if (!root || root->node_type != RB_AST_NODE_PROGRAM) return;
    RbProgramNode* prog = (RbProgramNode*)root;

    RbAstNode* s = prog->body;
    while (s) {
        if (s->node_type == RB_AST_NODE_ASSIGNMENT) {
            RbAssignmentNode* assign = (RbAssignmentNode*)s;
            if (assign->target && assign->target->node_type == RB_AST_NODE_IDENTIFIER) {
                RbIdentifierNode* id = (RbIdentifierNode*)assign->target;
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);

                // check if already registered
                bool found = false;
                for (int i = 0; i < mt->global_var_count; i++) {
                    if (strcmp(mt->global_var_names[i], vname) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found && mt->global_var_count < 64) {
                    snprintf(mt->global_var_names[mt->global_var_count], sizeof(mt->global_var_names[0]), "%s", vname);
                    mt->global_var_indices[mt->global_var_count] = mt->module_var_count++;
                    mt->global_var_count++;
                }
            }
        } else if (s->node_type == RB_AST_NODE_MULTI_ASSIGNMENT) {
            RbMultiAssignmentNode* ma = (RbMultiAssignmentNode*)s;
            RbAstNode* target = ma->targets;
            while (target) {
                if (target->node_type == RB_AST_NODE_IDENTIFIER) {
                    RbIdentifierNode* id = (RbIdentifierNode*)target;
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
                    bool found = false;
                    for (int i = 0; i < mt->global_var_count; i++) {
                        if (strcmp(mt->global_var_names[i], vname) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && mt->global_var_count < 64) {
                        snprintf(mt->global_var_names[mt->global_var_count], sizeof(mt->global_var_names[0]), "%s", vname);
                        mt->global_var_indices[mt->global_var_count] = mt->module_var_count++;
                        mt->global_var_count++;
                    }
                }
                target = target->next;
            }
        }
        s = s->next;
    }
}

// ============================================================================
// AST orchestration
// ============================================================================

static void rm_transpile_ast(RbMirTranspiler* mt, RbAstNode* root) {
    if (!root || root->node_type != RB_AST_NODE_PROGRAM) {
        log_error("rb-mir: expected program node");
        return;
    }

    RbProgramNode* program = (RbProgramNode*)root;

    // Phase 1: collect functions
    rm_collect_functions(mt, root);
    log_debug("rb-mir: collected %d functions", mt->func_count);

    // Phase 2: scan module-level variables
    rm_scan_module_vars(mt, root);

    // Phase 3a: forward-declare all module-level functions
    for (int i = 0; i < mt->func_count; i++) {
        RbFuncCollected* fc = &mt->func_entries[i];
        char fn_name[260];
        snprintf(fn_name, sizeof(fn_name), "rbu_%s", fc->name);
        MIR_item_t fwd = MIR_new_forward(mt->ctx, fn_name);
        fc->func_item = fwd;
        RbLocalFuncEntry lfe;
        memset(&lfe, 0, sizeof(lfe));
        snprintf(lfe.name, sizeof(lfe.name), "_rb_%s", fc->name);
        lfe.func_item = fwd;
        hashmap_set(mt->local_funcs, &lfe);
    }

    // Phase 3b: compile all functions (reverse order for nested support)
    for (int i = mt->func_count - 1; i >= 0; i--) {
        rm_compile_function(mt, &mt->func_entries[i]);
    }

    // Phase 4: create rb_main function
    MIR_type_t res_type = MIR_T_I64;
    MIR_var_t main_param = {MIR_T_I64, "ctx", 0};
    mt->current_func_item = MIR_new_func_arr(mt->ctx, "rb_main", 1, &res_type, 1, &main_param);
    mt->current_func = mt->current_func_item->u.func;
    mt->in_main = true;

    rm_push_scope(mt);

    // transpile top-level statements
    MIR_reg_t last_result = rm_emit_null(mt);
    RbAstNode* s = program->body;
    while (s) {
        if (s->node_type == RB_AST_NODE_METHOD_DEF) {
            s = s->next;
            continue; // already compiled
        }

        // use statement transpiler for assignment, control flow, etc.
        if (s->node_type == RB_AST_NODE_ASSIGNMENT ||
            s->node_type == RB_AST_NODE_OP_ASSIGNMENT ||
            s->node_type == RB_AST_NODE_IF ||
            s->node_type == RB_AST_NODE_UNLESS ||
            s->node_type == RB_AST_NODE_WHILE ||
            s->node_type == RB_AST_NODE_UNTIL ||
            s->node_type == RB_AST_NODE_FOR ||
            s->node_type == RB_AST_NODE_RETURN ||
            s->node_type == RB_AST_NODE_CASE ||
            s->node_type == RB_AST_NODE_BREAK ||
            s->node_type == RB_AST_NODE_NEXT) {
            rm_transpile_statement(mt, s);
        } else {
            // expression statement — capture last result
            last_result = rm_transpile_expression(mt, s);
            if (last_result == 0) {
                last_result = rm_emit_null(mt);
            }
        }

        s = s->next;
    }

    // return last result
    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, last_result)));

    MIR_finish_func(mt->ctx);
    rm_pop_scope(mt);
    mt->in_main = false;
}

// ============================================================================
// Entry point
// ============================================================================

Item transpile_rb_to_mir(Runtime* runtime, const char* rb_source, const char* filename) {
    log_debug("rb-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");

    // create Ruby transpiler
    RbTranspiler* tp = rb_transpiler_create(runtime);
    if (!tp) {
        log_error("rb-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // parse source
    if (!rb_transpiler_parse(tp, rb_source, strlen(rb_source))) {
        log_error("rb-mir: parse failed");
        rb_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // build AST
    RbAstNode* ast = build_rb_ast(tp, root);
    if (!ast) {
        log_error("rb-mir: AST build failed");
        rb_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // set up evaluation context
    EvalContext rb_context;
    memset(&rb_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = gc_nursery_create(0);
        }
    } else {
        rb_context.nursery = gc_nursery_create(0);
        context = &rb_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
    }

    _lambda_rt = (Context*)context;

    // create Input context for Ruby runtime
    Input* input = Input::create(context->pool);
    rb_runtime_set_input(input);

    // init MIR context
    MIR_context_t ctx = jit_init(2);
    if (!ctx) {
        log_error("rb-mir: MIR context init failed");
        rb_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // allocate transpiler
    RbMirTranspiler* mt = (RbMirTranspiler*)malloc(sizeof(RbMirTranspiler));
    if (!mt) {
        log_error("rb-mir: failed to allocate RbMirTranspiler");
        MIR_finish(ctx);
        rb_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    memset(mt, 0, sizeof(RbMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->filename = filename;
    mt->runtime = runtime;

    mt->import_cache = hashmap_new(sizeof(RbImportCacheEntry), 64, 0, 0,
        rb_import_cache_hash, rb_import_cache_cmp, NULL, NULL);
    mt->local_funcs = hashmap_new(sizeof(RbLocalFuncEntry), 32, 0, 0,
        rb_local_func_hash, rb_local_func_cmp, NULL, NULL);
    mt->var_scopes[0] = hashmap_new(sizeof(RbVarScopeEntry), 16, 0, 0,
        rb_var_scope_hash, rb_var_scope_cmp, NULL, NULL);
    mt->scope_depth = 0;

    // create module
    mt->module = MIR_new_module(ctx, "rb_script");

    // transpile AST to MIR
    rm_transpile_ast(mt, ast);

    // finalize module
    MIR_finish_module(mt->ctx);

    // load module for linking
    MIR_load_module(mt->ctx, mt->module);

#ifndef NDEBUG
    FILE* mir_dump = fopen("temp/rb_mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }
#endif

    // link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // find rb_main
    typedef Item (*rb_main_func_t)(Context*);
    rb_main_func_t rb_main = (rb_main_func_t)find_func(ctx, "rb_main");

    if (!rb_main) {
        log_error("rb-mir: failed to find rb_main");
        hashmap_free(mt->import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        free(mt);
        MIR_finish(ctx);
        rb_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // execute
    log_notice("rb-mir: executing JIT compiled Ruby code");
    rb_reset_module_vars();
    Item result = rb_main((Context*)context);

    // cleanup
    hashmap_free(mt->import_cache);
    hashmap_free(mt->local_funcs);
    for (int i = 0; i <= mt->scope_depth; i++) {
        if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
    }
    free(mt);

    MIR_finish(ctx);
    rb_transpiler_destroy(tp);

    if (!reusing_context) {
        context = old_context;
    }

    return result;
}
