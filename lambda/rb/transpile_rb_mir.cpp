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
extern "C" void* js_function_get_ptr(Item fn_item);
extern "C" void js_runtime_set_input(void* input);
extern "C" char* read_text_file(const char* filename);

// ============================================================================
// Constants
// ============================================================================
static const uint64_t RB_ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t RB_ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
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
    bool has_block_param;
};

// Phase 2: pre-collected block/proc/lambda
struct RbBlockCollected {
    RbBlockNode* node;         // AST node pointer (used as key to find pre-compiled block)
    MIR_item_t func_item;     // compiled MIR function
    int param_count;
    int id;                    // unique block id
};

// Phase 2: class info stored during class compilation
struct RbClassInfo {
    char name[128];
    MIR_reg_t cls_reg;       // register holding the class Item
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

    RbBlockCollected block_entries[64];
    int block_count;

    int module_var_count;
    char global_var_names[64][128];
    int global_var_indices[64];
    int global_var_count;

    bool in_main;
    bool in_method;            // Phase 2: inside an instance method
    bool in_class;             // Phase 2: inside a class definition
    char current_class[128];   // Phase 2: current class name
    MIR_reg_t self_reg;        // Phase 2: register holding 'self' (first param of method)
    bool has_block_param;      // Phase 2: current function has &block parameter

    // Phase 4: exception handling
    int try_depth;
    MIR_label_t try_handler_labels[16];
    MIR_label_t try_retry_labels[16];  // retry jumps back to begin body

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
static void rm_transpile_class_def(RbMirTranspiler* mt, RbClassDefNode* cls);
static void rm_transpile_module_def(RbMirTranspiler* mt, RbModuleDefNode* mod);
static void rm_transpile_begin_rescue(RbMirTranspiler* mt, RbBeginRescueNode* br);
static void rm_emit_exc_check(RbMirTranspiler* mt, MIR_label_t handler_label);
static MIR_reg_t rm_transpile_block_as_func(RbMirTranspiler* mt, RbBlockNode* block);
static MIR_reg_t rm_transpile_yield(RbMirTranspiler* mt, RbYieldNode* yd);
static MIR_reg_t rm_transpile_proc_lambda(RbMirTranspiler* mt, RbBlockNode* block);

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

static MIR_reg_t rm_call_4(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = rm_new_reg(mt, fn_name, ret_type);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4));
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

static void rm_call_void_3(RbMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    RbImportCacheEntry* ie = rm_ensure_import(mt, fn_name, MIR_T_I64, 3, args, 0);
    rm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3));
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
        case RB_OP_MATCH:   return "rb_regex_test";
        case RB_OP_NOT_MATCH: return "rb_regex_test";
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
        case RB_LITERAL_REGEX:
            if (lit->value.string_value) {
                MIR_reg_t pat = rm_box_string_literal(mt, lit->value.string_value->chars,
                    (int)lit->value.string_value->len);
                return rm_call_1(mt, "rb_regex_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, pat));
            }
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

    // =~ : call rb_regex_test(right_regex, left_string)
    if (cmp->op == RB_OP_MATCH) {
        return rm_call_2(mt, "rb_regex_test", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, right),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, left));
    }
    // !~ : negate rb_regex_test result
    if (cmp->op == RB_OP_NOT_MATCH) {
        MIR_reg_t match_result = rm_call_2(mt, "rb_regex_test", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, right),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, left));
        // negate: if truthy → false, else → true
        MIR_reg_t truthy = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, match_result));
        MIR_reg_t result = rm_new_reg(mt, "nm", MIR_T_I64);
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

            // .push / .<< (returns self, handled specially)
            if ((mlen == 4 && strncmp(mname, "push", 4) == 0) && call->arg_count >= 1) {
                MIR_reg_t arg = rm_transpile_expression(mt, call->args);
                rm_call_void_2(mt, "rb_array_push",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
                return recv;
            }

            // Phase 4: obj.respond_to?(:method)
            if (mlen == 11 && strncmp(mname, "respond_to?", 11) == 0 && call->arg_count >= 1) {
                MIR_reg_t arg = rm_transpile_expression(mt, call->args);
                return rm_call_2(mt, "rb_respond_to", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
            }

            // Phase 4: obj.send(:method, args...) / obj.public_send(:method, args...)
            if ((mlen == 4 && strncmp(mname, "send", 4) == 0) ||
                (mlen == 11 && strncmp(mname, "public_send", 11) == 0)) {
                if (call->arg_count >= 1) {
                    MIR_reg_t method_arg = rm_transpile_expression(mt, call->args);
                    // collect remaining args
                    int send_argc = call->arg_count - 1;
                    MIR_reg_t send_args_ptr = rm_new_reg(mt, "sargs", MIR_T_I64);
                    if (send_argc > 0) {
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                            MIR_new_reg_op(mt->ctx, send_args_ptr),
                            MIR_new_int_op(mt->ctx, send_argc * 8)));
                        RbAstNode* sarg = call->args->next;
                        for (int i = 0; i < send_argc && sarg; i++) {
                            MIR_reg_t sv = rm_transpile_expression(mt, sarg);
                            MIR_reg_t addr = rm_new_reg(mt, "sa", MIR_T_I64);
                            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                                MIR_new_reg_op(mt->ctx, addr),
                                MIR_new_reg_op(mt->ctx, send_args_ptr),
                                MIR_new_int_op(mt->ctx, i * 8)));
                            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                                MIR_new_reg_op(mt->ctx, sv)));
                            sarg = sarg->next;
                        }
                    } else {
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, send_args_ptr),
                            MIR_new_int_op(mt->ctx, 0)));
                    }
                    return rm_call_4(mt, "rb_send", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, method_arg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, send_args_ptr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, send_argc));
                }
            }

            // Phase 4: obj.is_a?(ClassName) / obj.kind_of?(ClassName)
            if (((mlen == 5 && strncmp(mname, "is_a?", 5) == 0) ||
                 (mlen == 8 && strncmp(mname, "kind_of?", 8) == 0)) && call->arg_count >= 1) {
                // check if argument is a known class name
                MIR_reg_t cls_arg = rm_transpile_expression(mt, call->args);
                // compare get_class(obj).__name__ == cls_name
                MIR_reg_t obj_cls = rm_call_1(mt, "rb_get_class", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
                return rm_call_2(mt, "rb_eq", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_cls),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_arg));
            }

            // Phase 4: obj.nil?
            if (mlen == 4 && strncmp(mname, "nil?", 4) == 0 && call->arg_count == 0) {
                // nil? returns true only for nil
                MIR_reg_t is_nil = rm_new_reg(mt, "isnil", MIR_T_I64);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                    MIR_new_reg_op(mt->ctx, is_nil),
                    MIR_new_reg_op(mt->ctx, recv),
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
                // convert boolean to Item
                MIR_reg_t result = rm_new_reg(mt, "nilr", MIR_T_I64);
                MIR_label_t l_true = rm_new_label(mt);
                MIR_label_t l_end = rm_new_label(mt);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
                    MIR_new_reg_op(mt->ctx, is_nil)));
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_FALSE_VAL)));
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
                rm_emit_label(mt, l_true);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_TRUE_VAL)));
                rm_emit_label(mt, l_end);
                return result;
            }

            // Phase 4: obj.class
            if (mlen == 5 && strncmp(mname, "class", 5) == 0 && call->arg_count == 0) {
                return rm_call_1(mt, "rb_builtin_type", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            }

            // proc.call(args...) / lambda.call(args...)
            if (mlen == 4 && strncmp(mname, "call", 4) == 0) {
                if (call->arg_count == 0) {
                    return rm_call_1(mt, "rb_block_call_0", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
                } else if (call->arg_count == 1) {
                    MIR_reg_t a0 = rm_transpile_expression(mt, call->args);
                    return rm_call_2(mt, "rb_block_call_1", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a0));
                } else if (call->arg_count == 2) {
                    MIR_reg_t a0 = rm_transpile_expression(mt, call->args);
                    MIR_reg_t a1 = rm_transpile_expression(mt, call->args->next);
                    return rm_call_3(mt, "rb_block_call_2", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a0),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a1));
                } else if (call->arg_count == 3) {
                    MIR_reg_t a0 = rm_transpile_expression(mt, call->args);
                    MIR_reg_t a1 = rm_transpile_expression(mt, call->args->next);
                    MIR_reg_t a2 = rm_transpile_expression(mt, call->args->next->next);
                    return rm_call_4(mt, "rb_block_call_3", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a0),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a1),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, a2));
                }
                // fallback: call with first arg only
                MIR_reg_t a0 = rm_transpile_expression(mt, call->args);
                return rm_call_2(mt, "rb_block_call_1", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, a0));
            }
        }

        // Phase 2: iterator methods with blocks
        if (call->block && call->method_name) {
            const char* mname = call->method_name->chars;
            int mlen = (int)call->method_name->len;

            MIR_reg_t block_reg = rm_transpile_block_as_func(mt, (RbBlockNode*)call->block);

            // array.each { |x| }
            if (mlen == 4 && strncmp(mname, "each", 4) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_each", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.map { |x| }
            if (mlen == 3 && strncmp(mname, "map", 3) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_map", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.collect { |x| } (alias for map)
            if (mlen == 7 && strncmp(mname, "collect", 7) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_map", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.select { |x| }
            if (mlen == 6 && strncmp(mname, "select", 6) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_select", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.filter { |x| } (alias for select)
            if (mlen == 6 && strncmp(mname, "filter", 6) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_select", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.reject { |x| }
            if (mlen == 6 && strncmp(mname, "reject", 6) == 0 && call->arg_count == 0) {
                return rm_call_2(mt, "rb_array_reject", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.reduce(init) { |acc, x| }
            if (mlen == 6 && strncmp(mname, "reduce", 6) == 0 && call->arg_count >= 1) {
                MIR_reg_t init = rm_transpile_expression(mt, call->args);
                return rm_call_3(mt, "rb_array_reduce", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, init),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.inject(init) { |acc, x| } (alias for reduce)
            if (mlen == 6 && strncmp(mname, "inject", 6) == 0 && call->arg_count >= 1) {
                MIR_reg_t init = rm_transpile_expression(mt, call->args);
                return rm_call_3(mt, "rb_array_reduce", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, init),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.each_with_index { |x, i| }
            if (mlen == 15 && strncmp(mname, "each_with_index", 15) == 0) {
                return rm_call_2(mt, "rb_array_each_with_index", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.any? { |x| }
            if ((mlen == 4 && strncmp(mname, "any?", 4) == 0) ||
                (mlen == 3 && strncmp(mname, "any", 3) == 0)) {
                return rm_call_2(mt, "rb_array_any", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.all? { |x| }
            if ((mlen == 4 && strncmp(mname, "all?", 4) == 0) ||
                (mlen == 3 && strncmp(mname, "all", 3) == 0)) {
                return rm_call_2(mt, "rb_array_all", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // array.find { |x| }
            if (mlen == 4 && strncmp(mname, "find", 4) == 0) {
                return rm_call_2(mt, "rb_array_find", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // n.times { |i| }
            if (mlen == 5 && strncmp(mname, "times", 5) == 0) {
                return rm_call_2(mt, "rb_int_times", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // n.upto(m) { |i| }
            if (mlen == 4 && strncmp(mname, "upto", 4) == 0 && call->arg_count >= 1) {
                MIR_reg_t m = rm_transpile_expression(mt, call->args);
                return rm_call_3(mt, "rb_int_upto", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, m),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }
            // n.downto(m) { |i| }
            if (mlen == 6 && strncmp(mname, "downto", 6) == 0 && call->arg_count >= 1) {
                MIR_reg_t m = rm_transpile_expression(mt, call->args);
                return rm_call_3(mt, "rb_int_downto", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, m),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, block_reg));
            }

            // generic method call with block — look up method, call with block
            // (will be handled below in dynamic dispatch)
        }

        // Phase 2: ClassName.new(args) — constructor call
        if (call->method_name) {
            const char* mname = call->method_name->chars;
            int mlen = (int)call->method_name->len;

            // File class methods: File.read, File.write, File.exist?
            if (call->receiver->node_type == RB_AST_NODE_CONST) {
                RbConstNode* cn = (RbConstNode*)call->receiver;
                if (cn->name->len == 4 && strncmp(cn->name->chars, "File", 4) == 0) {
                    if (mlen == 4 && strncmp(mname, "read", 4) == 0 && call->arg_count >= 1) {
                        MIR_reg_t path_arg = rm_transpile_expression(mt, call->args);
                        return rm_call_1(mt, "rb_file_read", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, path_arg));
                    }
                    if (mlen == 5 && strncmp(mname, "write", 5) == 0 && call->arg_count >= 2) {
                        MIR_reg_t path_arg = rm_transpile_expression(mt, call->args);
                        MIR_reg_t content_arg = rm_transpile_expression(mt, call->args->next);
                        return rm_call_2(mt, "rb_file_write", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, path_arg),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, content_arg));
                    }
                    if (((mlen == 6 && strncmp(mname, "exist?", 6) == 0) ||
                         (mlen == 7 && strncmp(mname, "exists?", 7) == 0)) && call->arg_count >= 1) {
                        MIR_reg_t path_arg = rm_transpile_expression(mt, call->args);
                        return rm_call_1(mt, "rb_file_exist", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, path_arg));
                    }
                }
            }

            if (mlen == 3 && strncmp(mname, "new", 3) == 0) {
                // Proc.new { |x|... } — return block as function value
                if (call->receiver->node_type == RB_AST_NODE_CONST) {
                    RbConstNode* cn = (RbConstNode*)call->receiver;
                    if ((cn->name->len == 4 && strncmp(cn->name->chars, "Proc", 4) == 0) ||
                        (cn->name->len == 6 && strncmp(cn->name->chars, "Lambda", 6) == 0)) {
                        if (call->block && call->block->node_type == RB_AST_NODE_BLOCK) {
                            return rm_transpile_block_as_func(mt, (RbBlockNode*)call->block);
                        }
                        return rm_emit_null(mt);
                    }
                }

                // recv is the class, create instance + call initialize
                MIR_reg_t inst = rm_call_1(mt, "rb_class_new_instance", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));

                // look up initialize method
                MIR_reg_t init_name = rm_box_string_literal(mt, "initialize", 10);
                MIR_reg_t init_fn = rm_call_2(mt, "rb_method_lookup", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, init_name));

                // call initialize(self, args...) if it exists
                MIR_label_t l_no_init = rm_new_label(mt);
                MIR_label_t l_done = rm_new_label(mt);

                // check if init_fn != ITEM_NULL
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_no_init),
                    MIR_new_reg_op(mt->ctx, init_fn),
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));

                // get function pointer from init_fn
                MIR_reg_t fptr = rm_call_1(mt, "js_function_get_ptr", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, init_fn));

                // call initialize with self + args
                int nargs = 1 + call->arg_count; // self + user args
                MIR_var_t* init_params = (MIR_var_t*)alloca(sizeof(MIR_var_t) * nargs);
                for (int i = 0; i < nargs; i++) {
                    init_params[i] = {MIR_T_I64, "a", 0};
                }
                char proto_name[140];
                snprintf(proto_name, sizeof(proto_name), "init_call_%d_p", mt->temp_count++);
                MIR_type_t res_t = MIR_T_I64;
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &res_t, nargs, init_params);
                MIR_item_t imp = MIR_new_import(mt->ctx, "js_function_get_ptr");
                (void)imp; // already imported via rm_call_1

                MIR_reg_t init_result = rm_new_reg(mt, "ir", MIR_T_I64);
                int nops = 3 + nargs;
                MIR_op_t* ops = (MIR_op_t*)alloca(sizeof(MIR_op_t) * nops);
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_reg_op(mt->ctx, fptr);
                ops[2] = MIR_new_reg_op(mt->ctx, init_result);
                ops[3] = MIR_new_reg_op(mt->ctx, inst); // self

                RbAstNode* arg = call->args;
                for (int i = 0; i < call->arg_count && arg; i++) {
                    MIR_reg_t av = rm_transpile_expression(mt, arg);
                    ops[4 + i] = MIR_new_reg_op(mt->ctx, av);
                    arg = arg->next;
                }

                rm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, (size_t)nops, ops));

                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
                rm_emit_label(mt, l_no_init);
                rm_emit_label(mt, l_done);

                return inst; // .new always returns the instance
            }

            // Phase 3: built-in method dispatchers + dynamic dispatch fallback
            // Chain: rb_string_method → rb_array_method → rb_hash_method →
            //        rb_int_method → rb_float_method → rb_method_lookup
            MIR_reg_t method_name_reg = rm_box_string_literal(mt,
                call->method_name->chars, (int)call->method_name->len);

            // collect args into a stack-allocated Item* array
            int argc = 0;
            RbAstNode* arg_node = call->args;
            MIR_reg_t arg_regs[16];
            while (arg_node && argc < 16) {
                arg_regs[argc++] = rm_transpile_expression(mt, arg_node);
                arg_node = arg_node->next;
            }

            MIR_reg_t args_ptr = rm_new_reg(mt, "bargs", MIR_T_I64);
            if (argc > 0) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, argc * 8)));
                for (int i = 0; i < argc; i++) {
                    MIR_reg_t addr = rm_new_reg(mt, "ba", MIR_T_I64);
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                        MIR_new_reg_op(mt->ctx, addr),
                        MIR_new_reg_op(mt->ctx, args_ptr),
                        MIR_new_int_op(mt->ctx, i * 8)));
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                        MIR_new_reg_op(mt->ctx, arg_regs[i])));
                }
            } else {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, 0)));
            }

            MIR_reg_t builtin_result = rm_new_reg(mt, "bres", MIR_T_I64);
            MIR_reg_t is_err = rm_new_reg(mt, "iserr", MIR_T_I64);
            MIR_label_t l_try_array = rm_new_label(mt);
            MIR_label_t l_try_hash = rm_new_label(mt);
            MIR_label_t l_try_int = rm_new_label(mt);
            MIR_label_t l_try_float = rm_new_label(mt);
            MIR_label_t l_try_dynamic = rm_new_label(mt);
            MIR_label_t l_dispatch_end = rm_new_label(mt);

            // try rb_string_method
            MIR_reg_t str_res = rm_call_4(mt, "rb_string_method", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, str_res),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_ERROR_VAL)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_array),
                MIR_new_reg_op(mt->ctx, is_err)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_reg_op(mt->ctx, str_res)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            // try rb_array_method
            rm_emit_label(mt, l_try_array);
            MIR_reg_t arr_res = rm_call_4(mt, "rb_array_method", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, arr_res),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_ERROR_VAL)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_hash),
                MIR_new_reg_op(mt->ctx, is_err)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_reg_op(mt->ctx, arr_res)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            // try rb_hash_method
            rm_emit_label(mt, l_try_hash);
            MIR_reg_t hash_res = rm_call_4(mt, "rb_hash_method", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, hash_res),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_ERROR_VAL)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_int),
                MIR_new_reg_op(mt->ctx, is_err)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_reg_op(mt->ctx, hash_res)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            // try rb_int_method
            rm_emit_label(mt, l_try_int);
            MIR_reg_t int_res = rm_call_4(mt, "rb_int_method", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, int_res),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_ERROR_VAL)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_float),
                MIR_new_reg_op(mt->ctx, is_err)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_reg_op(mt->ctx, int_res)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            // try rb_float_method
            rm_emit_label(mt, l_try_float);
            MIR_reg_t float_res = rm_call_4(mt, "rb_float_method", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, float_res),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_ERROR_VAL)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_dynamic),
                MIR_new_reg_op(mt->ctx, is_err)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_reg_op(mt->ctx, float_res)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            // fallback: dynamic dispatch via rb_method_lookup (class/instance methods)
            rm_emit_label(mt, l_try_dynamic);
            MIR_reg_t method_fn = rm_call_2(mt, "rb_method_lookup", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg));
            MIR_reg_t fptr = rm_call_1(mt, "js_function_get_ptr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, method_fn));

            MIR_label_t l_found = rm_new_label(mt);
            MIR_label_t l_not_found = rm_new_label(mt);
            // default to nil
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, builtin_result),
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));

            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_found),
                MIR_new_reg_op(mt->ctx, fptr)));
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_not_found)));

            rm_emit_label(mt, l_found);
            {
                // call method(self, args..., [block])
                bool has_block = (call->block != NULL);
                int nargs = 1 + call->arg_count + (has_block ? 1 : 0);
                MIR_var_t* call_params = (MIR_var_t*)alloca(sizeof(MIR_var_t) * nargs);
                for (int i = 0; i < nargs; i++) {
                    call_params[i] = {MIR_T_I64, "a", 0};
                }
                char proto_name[140];
                snprintf(proto_name, sizeof(proto_name), "mcall_%d_p", mt->temp_count++);
                MIR_type_t res_t = MIR_T_I64;
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &res_t, nargs, call_params);

                int nops = 3 + nargs;
                MIR_op_t* ops = (MIR_op_t*)alloca(sizeof(MIR_op_t) * nops);
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_reg_op(mt->ctx, fptr);
                ops[2] = MIR_new_reg_op(mt->ctx, builtin_result);
                ops[3] = MIR_new_reg_op(mt->ctx, recv); // self

                RbAstNode* darg = call->args;
                for (int i = 0; i < call->arg_count && darg; i++) {
                    MIR_reg_t av = rm_transpile_expression(mt, darg);
                    ops[4 + i] = MIR_new_reg_op(mt->ctx, av);
                    darg = darg->next;
                }

                if (has_block) {
                    MIR_reg_t block_reg = rm_transpile_block_as_func(mt, (RbBlockNode*)call->block);
                    ops[3 + call->arg_count + 1] = MIR_new_reg_op(mt->ctx, block_reg);
                }

                rm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, (size_t)nops, ops));
            }
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_dispatch_end)));

            rm_emit_label(mt, l_not_found);
            rm_emit_label(mt, l_dispatch_end);

            return builtin_result;
        }

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

        // Phase 4: raise — exception handling
        if (flen == 5 && strncmp(fname, "raise", 5) == 0) {
            MIR_reg_t exc;
            if (call->arg_count == 0) {
                // bare raise — re-raise (pass null)
                exc = rm_emit_null(mt);
            } else if (call->arg_count == 1) {
                // raise "message" or raise ExceptionObj
                exc = rm_transpile_expression(mt, call->args);
            } else {
                // raise ExceptionType, "message"
                MIR_reg_t type_reg;
                if (call->args->node_type == RB_AST_NODE_CONST) {
                    RbConstNode* cn = (RbConstNode*)call->args;
                    type_reg = rm_box_string_literal(mt, cn->name->chars, (int)cn->name->len);
                } else {
                    type_reg = rm_transpile_expression(mt, call->args);
                }
                MIR_reg_t msg_reg = rm_transpile_expression(mt, call->args->next);
                exc = rm_call_2(mt, "rb_new_exception", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, type_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_reg));
            }
            rm_call_void_1(mt, "rb_raise",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc));
            // jump to handler if inside begin/rescue, otherwise return
            if (mt->try_depth > 0) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->try_handler_labels[mt->try_depth - 1])));
            } else {
                rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
            }
            return rm_emit_null(mt);
        }

        // Phase 4: respond_to?
        if (flen == 11 && strncmp(fname, "respond_to?", 11) == 0 && call->arg_count >= 1) {

            MIR_reg_t arg = rm_transpile_expression(mt, call->args);
            // if called without receiver, check self
            MIR_reg_t self_r = mt->in_method ? mt->self_reg : rm_emit_null(mt);
            return rm_call_2(mt, "rb_respond_to", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, self_r),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // lambda { |x| ... } or proc { |x| ... } — return block as function value
        if ((flen == 6 && strncmp(fname, "lambda", 6) == 0) ||
            (flen == 4 && strncmp(fname, "proc", 4) == 0)) {
            if (call->block && call->block->node_type == RB_AST_NODE_BLOCK) {
                return rm_transpile_block_as_func(mt, (RbBlockNode*)call->block);
            }
            return rm_emit_null(mt);
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
            bool target_has_block = false;
            for (int fi = 0; fi < mt->func_count; fi++) {
                if (strcmp(mt->func_entries[fi].name, fname) == 0) {
                    func_nparams = mt->func_entries[fi].param_count;
                    target_has_block = mt->func_entries[fi].has_block_param;
                    break;
                }
            }
            // exclude &block from user param count
            int user_nparams = func_nparams - (target_has_block ? 1 : 0);
            bool has_call_block = (call->block != NULL) && target_has_block;
            int padded_args = (call_nargs < user_nparams) ? user_nparams : call_nargs;
            int nargs = padded_args + (has_call_block ? 1 : 0);
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
            for (int i = call_nargs; i < padded_args; i++) {
                ops[3 + i] = MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL);
            }

            // compile and pass block as last argument
            if (has_call_block) {
                MIR_reg_t block_reg = rm_transpile_block_as_func(mt, (RbBlockNode*)call->block);
                ops[3 + padded_args] = MIR_new_reg_op(mt->ctx, block_reg);
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
    if (mt->in_method) {
        // Phase 2: @ivar → rb_instance_getattr(self, "name")
        MIR_reg_t name_reg = rm_box_string_literal(mt, iv->name->chars, (int)iv->name->len);
        return rm_call_2(mt, "rb_instance_getattr", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->self_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg));
    }
    // fallback: stored as local variable
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
        case RB_AST_NODE_SELF: {
            // Phase 2: return self register when inside a method
            if (mt->in_method) {
                MIR_reg_t r = rm_new_reg(mt, "self", MIR_T_I64);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, mt->self_reg)));
                return r;
            }
            return rm_emit_null(mt);
        }
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
            // assignment as expression — use statement handler which handles all target types
            RbAssignmentNode* assign = (RbAssignmentNode*)expr;
            rm_transpile_statement(mt, expr);
            // return the assigned value (re-read from variable or just emit null for ivar/attr)
            if (assign->target && assign->target->node_type == RB_AST_NODE_IDENTIFIER) {
                RbIdentifierNode* id = (RbIdentifierNode*)assign->target;
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
                RbMirVarEntry* var = rm_find_var(mt, vname);
                if (var) return var->reg;
            }
            return rm_emit_null(mt);
        }
        case RB_AST_NODE_MULTI_ASSIGNMENT: {
            rm_transpile_multi_assignment(mt, (RbMultiAssignmentNode*)expr);
            return rm_emit_null(mt);
        }
        case RB_AST_NODE_YIELD:
            return rm_transpile_yield(mt, (RbYieldNode*)expr);
        case RB_AST_NODE_PROC_LAMBDA:
        case RB_AST_NODE_BLOCK:
            return rm_transpile_proc_lambda(mt, (RbBlockNode*)expr);
        case RB_AST_NODE_BEGIN_RESCUE: {
            // begin/rescue as expression — inline rescue or begin/rescue block with value
            RbBeginRescueNode* br = (RbBeginRescueNode*)expr;
            MIR_reg_t result = rm_new_reg(mt, "brr", MIR_T_I64);
            MIR_label_t l_handler = rm_new_label(mt);
            MIR_label_t l_end = rm_new_label(mt);

            // push try handler
            if (mt->try_depth < 16) {
                mt->try_handler_labels[mt->try_depth] = l_handler;
                mt->try_retry_labels[mt->try_depth] = rm_new_label(mt); // unused for inline
                mt->try_depth++;
            }

            // emit body expression (single expression for inline rescue)
            {
                MIR_reg_t body_val = rm_transpile_expression(mt, br->body);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, body_val)));
                // check for exception
                rm_emit_exc_check(mt, l_handler);
            }

            if (mt->try_depth > 0) mt->try_depth--;

            // no exception → jump to end
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, l_end)));

            // handler
            rm_emit_label(mt, l_handler);
            {
                MIR_reg_t exc = rm_call_0(mt, "rb_clear_exception", MIR_T_I64);
                (void)exc;
                // evaluate rescue handler expression
                if (br->rescues && br->rescues->node_type == RB_AST_NODE_RESCUE) {
                    RbRescueNode* resc = (RbRescueNode*)br->rescues;
                    if (resc->body) {
                        MIR_reg_t handler_val = rm_transpile_expression(mt, resc->body);
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, result),
                            MIR_new_reg_op(mt->ctx, handler_val)));
                    }
                }
            }

            rm_emit_label(mt, l_end);
            return result;
        }
        case RB_AST_NODE_ATTRIBUTE: {
            // obj.attr — use rb_instance_getattr for instances
            RbAttributeNode* attr = (RbAttributeNode*)expr;
            MIR_reg_t obj = rm_transpile_expression(mt, attr->object);
            MIR_reg_t name_reg = rm_box_string_literal(mt, attr->attr_name->chars,
                (int)attr->attr_name->len);
            return rm_call_2(mt, "rb_instance_getattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg));
        }
        case RB_AST_NODE_DEFINED: {
            // defined?(expr) — compile-time analysis of operand type
            RbDefinedNode* def = (RbDefinedNode*)expr;
            if (!def->operand) return rm_emit_null(mt);
            switch (def->operand->node_type) {
                case RB_AST_NODE_IDENTIFIER: {
                    // check if variable is defined at compile-time
                    RbIdentifierNode* id = (RbIdentifierNode*)def->operand;
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
                    RbMirVarEntry* var = rm_find_var(mt, vname);
                    if (var) {
                        // variable exists — but might be nil at runtime, emit runtime check
                        return rm_call_1(mt, "rb_defined", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
                    }
                    return rm_emit_null(mt);
                }
                case RB_AST_NODE_IVAR:
                    return rm_box_string_literal(mt, "instance-variable", 17);
                case RB_AST_NODE_GVAR:
                    return rm_box_string_literal(mt, "global-variable", 15);
                case RB_AST_NODE_CONST:
                    return rm_box_string_literal(mt, "constant", 8);
                case RB_AST_NODE_LITERAL: {
                    RbLiteralNode* lit = (RbLiteralNode*)def->operand;
                    switch (lit->literal_type) {
                        case RB_LITERAL_NIL: return rm_box_string_literal(mt, "expression", 10);
                        case RB_LITERAL_BOOLEAN:
                            return rm_box_string_literal(mt,
                                lit->value.boolean_value ? "true" : "false",
                                lit->value.boolean_value ? 4 : 5);
                        default: return rm_box_string_literal(mt, "expression", 10);
                    }
                }
                case RB_AST_NODE_CALL:
                    return rm_box_string_literal(mt, "method", 6);
                case RB_AST_NODE_SELF:
                    return rm_box_string_literal(mt, "self", 4);
                case RB_AST_NODE_YIELD:
                    return rm_box_string_literal(mt, "yield", 5);
                default:
                    return rm_box_string_literal(mt, "expression", 10);
            }
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
        if (mt->in_method) {
            // Phase 2: @ivar = val → rb_instance_setattr(self, "name", val)
            MIR_reg_t name_reg = rm_box_string_literal(mt, iv->name->chars, (int)iv->name->len);
            rm_call_void_3(mt, "rb_instance_setattr",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->self_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else {
            // fallback: store as local variable
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
    } else if (assign->target && assign->target->node_type == RB_AST_NODE_ATTRIBUTE) {
        // Phase 2: obj.attr = val → rb_instance_setattr(obj, "attr", val)
        RbAttributeNode* attr = (RbAttributeNode*)assign->target;
        MIR_reg_t obj = rm_transpile_expression(mt, attr->object);
        MIR_reg_t name_reg = rm_box_string_literal(mt, attr->attr_name->chars,
            (int)attr->attr_name->len);
        rm_call_void_3(mt, "rb_instance_setattr",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
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
// Phase 4: begin/rescue/ensure — exception handling
// ============================================================================

// emit exception check after a statement inside a try body
static void rm_emit_exc_check(RbMirTranspiler* mt, MIR_label_t handler_label) {
    MIR_reg_t chk = rm_call_0(mt, "rb_check_exception", MIR_T_I64);
    MIR_reg_t chk_t = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, chk));
    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, handler_label),
        MIR_new_reg_op(mt->ctx, chk_t)));
}

static void rm_transpile_begin_rescue(RbMirTranspiler* mt, RbBeginRescueNode* br) {
    MIR_label_t l_handler = rm_new_label(mt);
    MIR_label_t l_end = rm_new_label(mt);
    MIR_label_t l_else = br->else_body ? rm_new_label(mt) : l_end;
    MIR_label_t l_finally = br->ensure_body ? rm_new_label(mt) : l_end;
    MIR_label_t l_retry = rm_new_label(mt);

    // push try handler
    if (mt->try_depth < 16) {
        mt->try_handler_labels[mt->try_depth] = l_handler;
        mt->try_retry_labels[mt->try_depth] = l_retry;
        mt->try_depth++;
    }

    // retry label — retry jumps back here
    rm_emit_label(mt, l_retry);

    // emit try body — check exception after each statement
    {
        RbAstNode* s = br->body;
        while (s) {
            rm_transpile_statement(mt, s);
            rm_emit_exc_check(mt, l_handler);
            s = s->next;
        }
    }

    // pop try depth
    int saved_try_depth = mt->try_depth;
    MIR_label_t saved_retry = l_retry;
    if (mt->try_depth > 0) mt->try_depth--;

    // no exception → jump past handlers to else or finally/end
    if (br->else_body) {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_else)));
    } else {
        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_finally)));
    }

    // handler label
    rm_emit_label(mt, l_handler);

    // push retry label back so `retry` in handler can find it
    // but handler label points to outer handler (not self) to avoid infinite loops
    if (mt->try_depth < 16) {
        mt->try_retry_labels[mt->try_depth] = saved_retry;
        mt->try_handler_labels[mt->try_depth] = (mt->try_depth > 0)
            ? mt->try_handler_labels[mt->try_depth - 1] : l_handler; // fallback
        mt->try_depth++;
    }

    if (br->rescues) {
        // clear exception and get the value
        MIR_reg_t exc_val = rm_call_0(mt, "rb_clear_exception", MIR_T_I64);

        RbAstNode* handler = br->rescues;
        while (handler) {
            if (handler->node_type == RB_AST_NODE_RESCUE) {
                RbRescueNode* resc = (RbRescueNode*)handler;
                MIR_label_t l_next_handler = rm_new_label(mt);

                if (resc->exception_classes) {
                    // typed rescue: rescue ExceptionType => var
                    bool catch_all = false;
                    RbAstNode* eclass = resc->exception_classes;
                    MIR_label_t l_body = rm_new_label(mt);

                    while (eclass) {
                        // check if this catches StandardError or Exception (catch-all)
                        if (eclass->node_type == RB_AST_NODE_CONST) {
                            RbConstNode* cn = (RbConstNode*)eclass;
                            if ((cn->name->len == 9 && strncmp(cn->name->chars, "Exception", 9) == 0) ||
                                (cn->name->len == 13 && strncmp(cn->name->chars, "StandardError", 13) == 0) ||
                                (cn->name->len == 12 && strncmp(cn->name->chars, "RuntimeError", 12) == 0)) {
                                catch_all = true;
                            }
                        }

                        if (!catch_all) {
                            // get exception type name
                            MIR_reg_t exc_type = rm_call_1(mt, "rb_exception_get_type", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));

                            // get expected type name string
                            MIR_reg_t expected;
                            if (eclass->node_type == RB_AST_NODE_CONST) {
                                RbConstNode* cn = (RbConstNode*)eclass;
                                expected = rm_box_string_literal(mt, cn->name->chars, (int)cn->name->len);
                            } else {
                                expected = rm_transpile_expression(mt, eclass);
                            }

                            // compare
                            MIR_reg_t match = rm_call_2(mt, "rb_eq", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_type),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, expected));
                            MIR_reg_t match_t = rm_call_1(mt, "rb_is_truthy", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, match));
                            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                                MIR_new_label_op(mt->ctx, l_body),
                                MIR_new_reg_op(mt->ctx, match_t)));
                        }
                        eclass = eclass->next;
                    }

                    if (!catch_all) {
                        // no match → try next handler
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                            MIR_new_label_op(mt->ctx, l_next_handler)));
                    }

                    rm_emit_label(mt, l_body);
                }
                // bare rescue (no type) catches everything

                // bind exception to variable if present
                if (resc->variable_name) {
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_rb_%.*s",
                        (int)resc->variable_name->len, resc->variable_name->chars);
                    RbMirVarEntry* var = rm_find_var(mt, vname);
                    if (var) {
                        // get message for simple display
                        MIR_reg_t msg = rm_call_1(mt, "rb_exception_get_message", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, var->reg),
                            MIR_new_reg_op(mt->ctx, msg)));
                    } else {
                        MIR_reg_t reg = rm_new_reg(mt, vname, MIR_T_I64);
                        MIR_reg_t msg = rm_call_1(mt, "rb_exception_get_message", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));
                        rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, msg)));
                        rm_set_var(mt, vname, reg);
                    }
                }

                // emit handler body
                RbAstNode* bs = resc->body;
                while (bs) {
                    rm_transpile_statement(mt, bs);
                    bs = bs->next;
                }
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, l_finally)));

                rm_emit_label(mt, l_next_handler);
            }
            handler = handler->next;
        }

        // no handler matched → re-raise
        rm_call_void_1(mt, "rb_raise",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));
        // pop the temporary retry depth before re-raising
        if (mt->try_depth > 0) mt->try_depth--;
        if (mt->try_depth > 0) {
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->try_handler_labels[mt->try_depth - 1])));
        } else {
            rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL)));
        }
    } else {
        // no rescue handlers — pop the temporary retry depth
        if (mt->try_depth > 0) mt->try_depth--;
    }

    // else body (runs when no exception occurred)
    if (br->else_body) {
        rm_emit_label(mt, l_else);
        RbAstNode* es = br->else_body;
        while (es) {
            rm_transpile_statement(mt, es);
            es = es->next;
        }
    }

    // ensure/finally (always runs)
    if (br->ensure_body) {
        rm_emit_label(mt, l_finally);
        RbAstNode* fs = br->ensure_body;
        while (fs) {
            rm_transpile_statement(mt, fs);
            fs = fs->next;
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
            rm_transpile_class_def(mt, (RbClassDefNode*)stmt);
            break;
        case RB_AST_NODE_MODULE_DEF:
            // module support: compile as a class to hold methods
            rm_transpile_module_def(mt, (RbModuleDefNode*)stmt);
            break;
        case RB_AST_NODE_BEGIN_RESCUE:
            rm_transpile_begin_rescue(mt, (RbBeginRescueNode*)stmt);
            break;
        case RB_AST_NODE_RETRY:
            // retry — jump back to begin body start
            if (mt->try_depth > 0) {
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->try_retry_labels[mt->try_depth - 1])));
            }
            break;
        default:
            // treat as expression statement
            rm_transpile_expression(mt, stmt);
            break;
    }
}

// ============================================================================
// Phase 2: Block / Proc / Lambda compilation
// ============================================================================

// Look up a pre-compiled block and return a register holding it as an Item.
// Blocks are pre-compiled in Phase 3 before rb_main.
static MIR_reg_t rm_transpile_block_as_func(RbMirTranspiler* mt, RbBlockNode* block) {
    if (!block) return rm_emit_null(mt);

    // find the pre-compiled block by matching the AST node pointer
    for (int i = 0; i < mt->block_count; i++) {
        if (mt->block_entries[i].node == block) {
            RbBlockCollected* bc = &mt->block_entries[i];
            int nparams = bc->param_count;
            MIR_reg_t fn_ptr = rm_new_reg(mt, "bfptr", MIR_T_I64);
            rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_ptr),
                MIR_new_ref_op(mt->ctx, bc->func_item)));
            return rm_call_2(mt, "js_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, nparams));
        }
    }

    log_error("rb-mir: block not pre-compiled (node=%p)", (void*)block);
    return rm_emit_null(mt);
}

// Check if a node type is a statement (not an expression)
static bool rm_is_statement_node(RbAstNodeType t) {
    switch (t) {
        case RB_AST_NODE_ASSIGNMENT:
        case RB_AST_NODE_OP_ASSIGNMENT:
        case RB_AST_NODE_MULTI_ASSIGNMENT:
        case RB_AST_NODE_IF:
        case RB_AST_NODE_UNLESS:
        case RB_AST_NODE_WHILE:
        case RB_AST_NODE_UNTIL:
        case RB_AST_NODE_FOR:
        case RB_AST_NODE_RETURN:
        case RB_AST_NODE_CASE:
        case RB_AST_NODE_BREAK:
        case RB_AST_NODE_NEXT:
        case RB_AST_NODE_CLASS_DEF:
        case RB_AST_NODE_MODULE_DEF:
        case RB_AST_NODE_METHOD_DEF:
            return true;
        default:
            return false;
    }
}

// Pre-compile a block as a standalone MIR function (called before rb_main).
static void rm_compile_block(RbMirTranspiler* mt, RbBlockCollected* bc) {
    RbBlockNode* block = bc->node;
    int nparams = block->param_count;

    MIR_var_t* params = nparams > 0 ? (MIR_var_t*)alloca(sizeof(MIR_var_t) * nparams) : NULL;
    RbAstNode* p = block->params;
    for (int i = 0; i < nparams && p; i++) {
        char pname[128];
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            snprintf(pname, sizeof(pname), "_rb_%.*s",
                pp->name ? (int)pp->name->len : 0,
                pp->name ? pp->name->chars : "bp");
        } else if (p->node_type == RB_AST_NODE_IDENTIFIER) {
            RbIdentifierNode* id = (RbIdentifierNode*)p;
            snprintf(pname, sizeof(pname), "_rb_%.*s", (int)id->name->len, id->name->chars);
        } else {
            snprintf(pname, sizeof(pname), "_rb_bp%d", i);
        }
        char* stable_name = (char*)pool_alloc(mt->tp->ast_pool, strlen(pname) + 1);
        strcpy(stable_name, pname);
        params[i] = {MIR_T_I64, stable_name, 0};
        p = p->next;
    }

    char fn_name[260];
    snprintf(fn_name, sizeof(fn_name), "rbu_block_%d", bc->id);

    MIR_type_t res_type = MIR_T_I64;
    bc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type, nparams, params);

    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    bool saved_in_method = mt->in_method;
    MIR_reg_t saved_self_reg = mt->self_reg;

    mt->current_func_item = bc->func_item;
    mt->current_func = bc->func_item->u.func;
    mt->in_method = false;

    rm_push_scope(mt);

    p = block->params;
    for (int i = 0; i < nparams && p; i++) {
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->name) {
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)pp->name->len, pp->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, params[i].name, mt->current_func);
                rm_set_var(mt, vname, preg);
            }
        } else if (p->node_type == RB_AST_NODE_IDENTIFIER) {
            RbIdentifierNode* id = (RbIdentifierNode*)p;
            char vname[128];
            snprintf(vname, sizeof(vname), "_rb_%.*s", (int)id->name->len, id->name->chars);
            MIR_reg_t preg = MIR_reg(mt->ctx, params[i].name, mt->current_func);
            rm_set_var(mt, vname, preg);
        }
        p = p->next;
    }

    MIR_reg_t last_result = rm_emit_null(mt);
    RbAstNode* s = block->body;
    while (s) {
        if (!s->next && !rm_is_statement_node(s->node_type)) {
            last_result = rm_transpile_expression(mt, s);
        } else {
            rm_transpile_statement(mt, s);
        }
        s = s->next;
    }

    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, last_result)));
    MIR_finish_func(mt->ctx);
    rm_pop_scope(mt);

    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;
    mt->in_method = saved_in_method;
    mt->self_reg = saved_self_reg;
}

// Compile proc { } or lambda { } or -> { }
static MIR_reg_t rm_transpile_proc_lambda(RbMirTranspiler* mt, RbBlockNode* block) {
    return rm_transpile_block_as_func(mt, block);
}

// Compile yield — calls the block parameter
static MIR_reg_t rm_transpile_yield(RbMirTranspiler* mt, RbYieldNode* yd) {
    // &block is passed as the last parameter named "_rb__block"
    RbMirVarEntry* block_var = rm_find_var(mt, "_rb__block");
    if (!block_var) {
        log_debug("rb-mir: yield outside block context");
        return rm_emit_null(mt);
    }

    int argc = yd->arg_count;
    if (argc == 0) {
        return rm_call_1(mt, "rb_block_call_1", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, block_var->reg));
    } else if (argc == 1) {
        MIR_reg_t a0 = rm_transpile_expression(mt, yd->args);
        return rm_call_2(mt, "rb_block_call_1", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, block_var->reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, a0));
    } else if (argc == 2) {
        MIR_reg_t a0 = rm_transpile_expression(mt, yd->args);
        MIR_reg_t a1 = rm_transpile_expression(mt, yd->args->next);
        return rm_call_3(mt, "rb_block_call_2", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, block_var->reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, a0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, a1));
    }
    // fallback for more args — just use first arg
    MIR_reg_t a0 = rm_transpile_expression(mt, yd->args);
    return rm_call_2(mt, "rb_block_call_1", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, block_var->reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, a0));
}

// ============================================================================
// Phase 2: Class definition transpiler
// ============================================================================

// Compile a method inside a class as a MIR function with self as first param
static void rm_compile_class_method(RbMirTranspiler* mt, RbFuncCollected* fc,
                                     MIR_reg_t cls_reg) {
    RbMethodDefNode* meth = fc->node;

    // methods get self as first parameter, plus user params, plus optional &block
    bool method_has_block = fc->has_block_param;
    int user_params = fc->param_count - (method_has_block ? 1 : 0);
    int total_params = 1 + user_params + (method_has_block ? 1 : 0);

    MIR_var_t* params = (MIR_var_t*)alloca(sizeof(MIR_var_t) * total_params);

    // first param: self
    char* self_name = (char*)pool_alloc(mt->tp->ast_pool, 16);
    strcpy(self_name, "_rb_self");
    params[0] = {MIR_T_I64, self_name, 0};

    // user params
    RbAstNode* p = meth->params;
    for (int i = 0; i < user_params && p; i++) {
        char pname[128];
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->is_block) { p = p->next; continue; } // &block handled separately
            snprintf(pname, sizeof(pname), "_rb_%.*s",
                pp->name ? (int)pp->name->len : 0,
                pp->name ? pp->name->chars : "p");
        } else {
            snprintf(pname, sizeof(pname), "_rb_p%d", i);
        }
        char* stable_name = (char*)pool_alloc(mt->tp->ast_pool, strlen(pname) + 1);
        strcpy(stable_name, pname);
        params[1 + i] = {MIR_T_I64, stable_name, 0};
        p = p->next;
    }

    // &block parameter
    if (method_has_block) {
        char* block_name = (char*)pool_alloc(mt->tp->ast_pool, 16);
        strcpy(block_name, "_rb__block");
        params[total_params - 1] = {MIR_T_I64, block_name, 0};
    }

    // function name: rbu_ClassName_method_name
    char fn_name[260];
    snprintf(fn_name, sizeof(fn_name), "rbu_%s_%s", fc->class_name, fc->name);

    MIR_type_t res_type = MIR_T_I64;
    fc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type, total_params, params);

    // save parent state
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    bool saved_in_method = mt->in_method;
    MIR_reg_t saved_self_reg = mt->self_reg;
    bool saved_has_block = mt->has_block_param;

    mt->current_func_item = fc->func_item;
    mt->current_func = fc->func_item->u.func;
    mt->in_method = true;
    mt->has_block_param = method_has_block;

    rm_push_scope(mt);

    // register self
    mt->self_reg = MIR_reg(mt->ctx, "_rb_self", mt->current_func);
    rm_set_var(mt, "_rb_self", mt->self_reg);

    // register user params
    p = meth->params;
    for (int i = 0; i < user_params && p; i++) {
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->is_block) { p = p->next; continue; }
            if (pp->name) {
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)pp->name->len, pp->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, params[1 + i].name, mt->current_func);
                rm_set_var(mt, vname, preg);
            }
        }
        p = p->next;
    }

    // register &block param
    if (method_has_block) {
        MIR_reg_t block_reg = MIR_reg(mt->ctx, "_rb__block", mt->current_func);
        rm_set_var(mt, "_rb__block", block_reg);
    }

    // emit default parameter assignments
    p = meth->params;
    for (int i = 0; i < user_params && p; i++) {
        if (p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->name && pp->default_value) {
                MIR_reg_t preg = MIR_reg(mt->ctx, params[1 + i].name, mt->current_func);
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
        if (!s->next && !rm_is_statement_node(s->node_type)) {
            last_result = rm_transpile_expression(mt, s);
        } else {
            rm_transpile_statement(mt, s);
        }
        s = s->next;
    }

    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, last_result)));
    MIR_finish_func(mt->ctx);
    rm_pop_scope(mt);

    // restore parent state
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;
    mt->in_method = saved_in_method;
    mt->self_reg = saved_self_reg;
    mt->has_block_param = saved_has_block;
}

static void rm_transpile_class_def(RbMirTranspiler* mt, RbClassDefNode* cls_node) {
    const char* cname = cls_node->name ? cls_node->name->chars : "AnonClass";
    int clen = cls_node->name ? (int)cls_node->name->len : 9;

    log_debug("rb-mir: compiling class '%.*s'", clen, cname);

    // save class context
    bool saved_in_class = mt->in_class;
    char saved_class[128];
    strncpy(saved_class, mt->current_class, sizeof(saved_class));
    mt->in_class = true;
    snprintf(mt->current_class, sizeof(mt->current_class), "%.*s", clen, cname);

    // evaluate superclass expression (or null)
    MIR_reg_t super_reg;
    if (cls_node->superclass) {
        super_reg = rm_transpile_expression(mt, cls_node->superclass);
    } else {
        super_reg = rm_emit_null(mt);
    }

    // create class: cls = rb_class_create(name_item, superclass)
    MIR_reg_t name_reg = rm_box_string_literal(mt, cname, clen);
    MIR_reg_t cls_reg = rm_call_2(mt, "rb_class_create", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, super_reg));

    // store class in variable scope (so ClassName can be referenced)
    char cls_varname[128];
    snprintf(cls_varname, sizeof(cls_varname), "_rb_%.*s", clen, cname);
    rm_set_var(mt, cls_varname, cls_reg);

    // also store as module var if in main
    if (mt->in_main) {
        // register as global var
        bool found = false;
        for (int i = 0; i < mt->global_var_count; i++) {
            if (strcmp(mt->global_var_names[i], cls_varname) == 0) {
                found = true;
                rm_call_void_2(mt, "rb_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, mt->global_var_indices[i]),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_reg));
                break;
            }
        }
        if (!found && mt->global_var_count < 64) {
            int idx = mt->module_var_count++;
            snprintf(mt->global_var_names[mt->global_var_count], sizeof(mt->global_var_names[0]),
                "%s", cls_varname);
            mt->global_var_indices[mt->global_var_count] = idx;
            mt->global_var_count++;
            rm_call_void_2(mt, "rb_set_module_var",
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_reg));
        }
    }

    // register pre-compiled class methods
    RbAstNode* body = cls_node->body;
    while (body) {
        if (body->node_type == RB_AST_NODE_METHOD_DEF) {
            RbMethodDefNode* meth = (RbMethodDefNode*)body;
            if (meth->name) {
                // find the pre-compiled method in func_entries
                RbFuncCollected* fc = NULL;
                for (int fi = 0; fi < mt->func_count; fi++) {
                    if (mt->func_entries[fi].is_method &&
                        mt->func_entries[fi].node == meth) {
                        fc = &mt->func_entries[fi];
                        break;
                    }
                }
                if (!fc) {
                    log_error("rb-mir: method '%.*s' not pre-compiled for class '%.*s'",
                        (int)meth->name->len, meth->name->chars, clen, cname);
                    body = body->next;
                    continue;
                }

                // register method on class: rb_class_add_method(cls, name, func_item)
                MIR_reg_t method_name_reg = rm_box_string_literal(mt,
                    meth->name->chars, (int)meth->name->len);
                // wrap the method as an Item (function object)
                int user_params = fc->param_count - (fc->has_block_param ? 1 : 0);
                int mir_param_count = 1 + user_params + (fc->has_block_param ? 1 : 0);
                MIR_reg_t fn_ptr = rm_new_reg(mt, "mfptr", MIR_T_I64);
                rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_ptr),
                    MIR_new_ref_op(mt->ctx, fc->func_item)));
                MIR_reg_t fn_item = rm_call_2(mt, "js_new_function", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_ptr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, mir_param_count));

                rm_call_void_3(mt, "rb_class_add_method",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
            }
        } else if (body->node_type == RB_AST_NODE_CALL) {
            // attr_reader, attr_writer, attr_accessor
            RbCallNode* call = (RbCallNode*)body;
            if (call->method_name) {
                const char* fname = call->method_name->chars;
                int flen = (int)call->method_name->len;

                // include ModuleName — copy module methods to this class
                if (!call->receiver && flen == 7 && strncmp(fname, "include", 7) == 0) {
                    RbAstNode* arg = call->args;
                    while (arg) {
                        MIR_reg_t mod_reg = rm_transpile_expression(mt, arg);
                        rm_call_void_2(mt, "rb_module_include",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_reg),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, mod_reg));
                        arg = arg->next;
                    }
                    body = body->next;
                    continue;
                }

                const char* attr_fn = NULL;
                if (flen == 11 && strncmp(fname, "attr_reader", 11) == 0) attr_fn = "rb_attr_reader";
                else if (flen == 11 && strncmp(fname, "attr_writer", 11) == 0) attr_fn = "rb_attr_writer";
                else if (flen == 13 && strncmp(fname, "attr_accessor", 13) == 0) attr_fn = "rb_attr_accessor";

                if (attr_fn) {
                    // each argument is a symbol name
                    RbAstNode* arg = call->args;
                    while (arg) {
                        MIR_reg_t sym_reg;
                        if (arg->node_type == RB_AST_NODE_SYMBOL) {
                            RbSymbolNode* sym = (RbSymbolNode*)arg;
                            sym_reg = rm_box_string_literal(mt, sym->name->chars, (int)sym->name->len);
                        } else if (arg->node_type == RB_AST_NODE_LITERAL) {
                            RbLiteralNode* lit = (RbLiteralNode*)arg;
                            if (lit->literal_type == RB_LITERAL_SYMBOL && lit->value.string_value) {
                                sym_reg = rm_box_string_literal(mt,
                                    lit->value.string_value->chars,
                                    (int)lit->value.string_value->len);
                            } else {
                                sym_reg = rm_transpile_expression(mt, arg);
                            }
                        } else {
                            sym_reg = rm_transpile_expression(mt, arg);
                        }
                        rm_call_void_2(mt, attr_fn,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_reg),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, sym_reg));
                        arg = arg->next;
                    }
                }
            }
        }
        body = body->next;
    }

    // restore class context
    mt->in_class = saved_in_class;
    strncpy(mt->current_class, saved_class, sizeof(mt->current_class));
}

// Compile module definition — like a class, stores methods as a class object
static void rm_transpile_module_def(RbMirTranspiler* mt, RbModuleDefNode* mod_node) {
    const char* mname = mod_node->name ? mod_node->name->chars : "AnonModule";
    int mlen = mod_node->name ? (int)mod_node->name->len : 10;

    log_debug("rb-mir: compiling module '%.*s'", mlen, mname);

    // create module as a class object (so it can hold methods)
    MIR_reg_t name_reg = rm_box_string_literal(mt, mname, mlen);
    MIR_reg_t mod_reg = rm_call_2(mt, "rb_class_create", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)RB_ITEM_NULL_VAL));

    // store module in variable scope
    char mod_varname[128];
    snprintf(mod_varname, sizeof(mod_varname), "_rb_%.*s", mlen, mname);
    rm_set_var(mt, mod_varname, mod_reg);

    if (mt->in_main) {
        bool found = false;
        for (int i = 0; i < mt->global_var_count; i++) {
            if (strcmp(mt->global_var_names[i], mod_varname) == 0) {
                found = true;
                rm_call_void_2(mt, "rb_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, mt->global_var_indices[i]),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, mod_reg));
                break;
            }
        }
        if (!found && mt->global_var_count < 64) {
            int idx = mt->module_var_count++;
            snprintf(mt->global_var_names[mt->global_var_count], sizeof(mt->global_var_names[0]),
                "%s", mod_varname);
            mt->global_var_indices[mt->global_var_count] = idx;
            mt->global_var_count++;
            rm_call_void_2(mt, "rb_set_module_var",
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mod_reg));
        }
    }

    // register pre-compiled methods on the module
    RbAstNode* body = mod_node->body;
    while (body) {
        if (body->node_type == RB_AST_NODE_METHOD_DEF) {
            RbMethodDefNode* meth = (RbMethodDefNode*)body;
            if (meth->name) {
                RbFuncCollected* fc = NULL;
                for (int fi = 0; fi < mt->func_count; fi++) {
                    if (mt->func_entries[fi].is_method &&
                        mt->func_entries[fi].node == meth) {
                        fc = &mt->func_entries[fi];
                        break;
                    }
                }
                if (fc) {
                    MIR_reg_t method_name_reg = rm_box_string_literal(mt,
                        meth->name->chars, (int)meth->name->len);
                    int user_params = fc->param_count - (fc->has_block_param ? 1 : 0);
                    int mir_param_count = 1 + user_params + (fc->has_block_param ? 1 : 0);
                    MIR_reg_t fn_ptr = rm_new_reg(mt, "modfptr", MIR_T_I64);
                    rm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_ptr),
                        MIR_new_ref_op(mt->ctx, fc->func_item)));
                    MIR_reg_t fn_item = rm_call_2(mt, "js_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_ptr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, mir_param_count));
                    rm_call_void_3(mt, "rb_class_add_method",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, mod_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                }
            }
        }
        body = body->next;
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
                    fc->has_block_param = meth->has_block_param;
                    mt->func_count++;
                }
            }
            // Phase 2: collect class methods as func_entries
            if (s->node_type == RB_AST_NODE_CLASS_DEF) {
                RbClassDefNode* cls = (RbClassDefNode*)s;
                const char* cname = cls->name ? cls->name->chars : "AnonClass";
                int clen = cls->name ? (int)cls->name->len : 9;

                RbAstNode* body = cls->body;
                while (body) {
                    if (body->node_type == RB_AST_NODE_METHOD_DEF) {
                        RbMethodDefNode* meth = (RbMethodDefNode*)body;
                        if (mt->func_count < 128 && meth->name) {
                            RbFuncCollected* fc = &mt->func_entries[mt->func_count];
                            memset(fc, 0, sizeof(RbFuncCollected));
                            fc->node = meth;
                            snprintf(fc->name, sizeof(fc->name), "%.*s",
                                (int)meth->name->len, meth->name->chars);
                            fc->param_count = meth->param_count;
                            fc->required_count = meth->required_count;
                            fc->has_block_param = meth->has_block_param;
                            fc->is_method = true;
                            snprintf(fc->class_name, sizeof(fc->class_name), "%.*s", clen, cname);
                            mt->func_count++;
                        }
                    }
                    body = body->next;
                }
            }
            // collect module methods as func_entries (like class methods)
            if (s->node_type == RB_AST_NODE_MODULE_DEF) {
                RbModuleDefNode* mod = (RbModuleDefNode*)s;
                const char* mname = mod->name ? mod->name->chars : "AnonModule";
                int mlen = mod->name ? (int)mod->name->len : 10;

                RbAstNode* body = mod->body;
                while (body) {
                    if (body->node_type == RB_AST_NODE_METHOD_DEF) {
                        RbMethodDefNode* meth = (RbMethodDefNode*)body;
                        if (mt->func_count < 128 && meth->name) {
                            RbFuncCollected* fc = &mt->func_entries[mt->func_count];
                            memset(fc, 0, sizeof(RbFuncCollected));
                            fc->node = meth;
                            snprintf(fc->name, sizeof(fc->name), "%.*s",
                                (int)meth->name->len, meth->name->chars);
                            fc->param_count = meth->param_count;
                            fc->required_count = meth->required_count;
                            fc->has_block_param = meth->has_block_param;
                            fc->is_method = true;
                            snprintf(fc->class_name, sizeof(fc->class_name), "%.*s", mlen, mname);
                            mt->func_count++;
                        }
                    }
                    body = body->next;
                }
            }
            s = s->next;
        }
    }
}

// Phase 2: recursively collect all blocks in the AST
static void rm_collect_blocks_r(RbMirTranspiler* mt, RbAstNode* node) {
    if (!node) return;

    // proc/lambda/block standalone — collect as block for pre-compilation
    if (node->node_type == RB_AST_NODE_PROC_LAMBDA || node->node_type == RB_AST_NODE_BLOCK) {
        RbBlockNode* block = (RbBlockNode*)node;
        if (mt->block_count < 64) {
            RbBlockCollected* bc = &mt->block_entries[mt->block_count];
            memset(bc, 0, sizeof(RbBlockCollected));
            bc->node = block;
            bc->param_count = block->param_count;
            bc->id = mt->block_count;
            mt->block_count++;
        }
        // recurse into block body
        RbAstNode* bs = block->body;
        while (bs) { rm_collect_blocks_r(mt, bs); bs = bs->next; }
        return;
    }

    // check if this node is a call with a block
    if (node->node_type == RB_AST_NODE_CALL) {
        RbCallNode* call = (RbCallNode*)node;
        if (call->block && call->block->node_type == RB_AST_NODE_BLOCK) {
            RbBlockNode* block = (RbBlockNode*)call->block;
            if (mt->block_count < 64) {
                RbBlockCollected* bc = &mt->block_entries[mt->block_count];
                memset(bc, 0, sizeof(RbBlockCollected));
                bc->node = block;
                bc->param_count = block->param_count;
                bc->id = mt->block_count;
                mt->block_count++;
            }
            // recurse into block body
            RbAstNode* bs = block->body;
            while (bs) { rm_collect_blocks_r(mt, bs); bs = bs->next; }
        }
        // recurse into receiver and args
        rm_collect_blocks_r(mt, call->receiver);
        RbAstNode* arg = call->args;
        while (arg) { rm_collect_blocks_r(mt, arg); arg = arg->next; }
        return;
    }

    // recurse into common node types
    switch (node->node_type) {
        case RB_AST_NODE_PROGRAM: {
            RbProgramNode* prog = (RbProgramNode*)node;
            RbAstNode* s = prog->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_METHOD_DEF: {
            RbMethodDefNode* meth = (RbMethodDefNode*)node;
            RbAstNode* s = meth->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_CLASS_DEF: {
            RbClassDefNode* cls = (RbClassDefNode*)node;
            RbAstNode* s = cls->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_IF:
        case RB_AST_NODE_UNLESS: {
            RbIfNode* ifn = (RbIfNode*)node;
            rm_collect_blocks_r(mt, ifn->condition);
            RbAstNode* s = ifn->then_body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            s = ifn->else_body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_WHILE:
        case RB_AST_NODE_UNTIL: {
            RbWhileNode* wn = (RbWhileNode*)node;
            rm_collect_blocks_r(mt, wn->condition);
            RbAstNode* s = wn->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_FOR: {
            RbForNode* fn = (RbForNode*)node;
            rm_collect_blocks_r(mt, fn->collection);
            RbAstNode* s = fn->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_ASSIGNMENT: {
            RbAssignmentNode* an = (RbAssignmentNode*)node;
            rm_collect_blocks_r(mt, an->value);
            break;
        }
        case RB_AST_NODE_OP_ASSIGNMENT: {
            RbOpAssignmentNode* oa = (RbOpAssignmentNode*)node;
            rm_collect_blocks_r(mt, oa->value);
            break;
        }
        case RB_AST_NODE_RETURN: {
            RbReturnNode* rn = (RbReturnNode*)node;
            rm_collect_blocks_r(mt, rn->value);
            break;
        }
        case RB_AST_NODE_BINARY_OP: {
            RbBinaryNode* bn = (RbBinaryNode*)node;
            rm_collect_blocks_r(mt, bn->left);
            rm_collect_blocks_r(mt, bn->right);
            break;
        }
        case RB_AST_NODE_UNARY_OP: {
            RbUnaryNode* un = (RbUnaryNode*)node;
            rm_collect_blocks_r(mt, un->operand);
            break;
        }
        case RB_AST_NODE_CASE: {
            RbCaseNode* cn = (RbCaseNode*)node;
            rm_collect_blocks_r(mt, cn->subject);
            RbAstNode* w = cn->whens;
            while (w) { rm_collect_blocks_r(mt, w); w = w->next; }
            RbAstNode* s = cn->else_body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_WHEN: {
            RbWhenNode* wn = (RbWhenNode*)node;
            RbAstNode* p = wn->patterns;
            while (p) { rm_collect_blocks_r(mt, p); p = p->next; }
            RbAstNode* s = wn->body;
            while (s) { rm_collect_blocks_r(mt, s); s = s->next; }
            break;
        }
        case RB_AST_NODE_ARRAY: {
            RbArrayNode* arr = (RbArrayNode*)node;
            RbAstNode* e = arr->elements;
            while (e) { rm_collect_blocks_r(mt, e); e = e->next; }
            break;
        }
        case RB_AST_NODE_HASH: {
            RbHashNode* h = (RbHashNode*)node;
            RbAstNode* p = h->pairs;
            while (p) { rm_collect_blocks_r(mt, p); p = p->next; }
            break;
        }
        case RB_AST_NODE_PAIR: {
            RbPairNode* pair = (RbPairNode*)node;
            rm_collect_blocks_r(mt, pair->key);
            rm_collect_blocks_r(mt, pair->value);
            break;
        }
        case RB_AST_NODE_STRING_INTERPOLATION: {
            RbStringInterpNode* si = (RbStringInterpNode*)node;
            RbAstNode* p = si->parts;
            while (p) { rm_collect_blocks_r(mt, p); p = p->next; }
            break;
        }
        case RB_AST_NODE_TERNARY: {
            RbTernaryNode* tn = (RbTernaryNode*)node;
            rm_collect_blocks_r(mt, tn->condition);
            rm_collect_blocks_r(mt, tn->true_expr);
            rm_collect_blocks_r(mt, tn->false_expr);
            break;
        }
        case RB_AST_NODE_SUBSCRIPT: {
            RbSubscriptNode* sn = (RbSubscriptNode*)node;
            rm_collect_blocks_r(mt, sn->object);
            rm_collect_blocks_r(mt, sn->index);
            break;
        }
        case RB_AST_NODE_ATTRIBUTE: {
            RbAttributeNode* an = (RbAttributeNode*)node;
            rm_collect_blocks_r(mt, an->object);
            break;
        }
        default:
            break;
    }
}

static void rm_compile_function(RbMirTranspiler* mt, RbFuncCollected* fc) {
    RbMethodDefNode* meth = fc->node;

    // build MIR parameter list — exclude &block from user params, add separately
    bool func_has_block = fc->has_block_param;
    int user_params = fc->param_count - (func_has_block ? 1 : 0);
    int nparams = user_params + (func_has_block ? 1 : 0);
    MIR_var_t* params = nparams > 0 ? (MIR_var_t*)alloca(sizeof(MIR_var_t) * nparams) : NULL;

    RbAstNode* p = meth->params;
    int pi = 0;
    for (int i = 0; i < fc->param_count && p; i++) {
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->is_block) { p = p->next; continue; } // &block handled separately
            char pname[128];
            snprintf(pname, sizeof(pname), "_rb_%.*s",
                pp->name ? (int)pp->name->len : 0,
                pp->name ? pp->name->chars : "p");
            char* stable_name = (char*)pool_alloc(mt->tp->ast_pool, strlen(pname) + 1);
            strcpy(stable_name, pname);
            params[pi] = {MIR_T_I64, stable_name, 0};
            pi++;
        } else {
            char pname[128];
            snprintf(pname, sizeof(pname), "_rb_p%d", pi);
            char* stable_name = (char*)pool_alloc(mt->tp->ast_pool, strlen(pname) + 1);
            strcpy(stable_name, pname);
            params[pi] = {MIR_T_I64, stable_name, 0};
            pi++;
        }
        p = p->next;
    }

    // &block parameter at the end
    if (func_has_block) {
        char* block_name = (char*)pool_alloc(mt->tp->ast_pool, 16);
        strcpy(block_name, "_rb__block");
        params[nparams - 1] = {MIR_T_I64, block_name, 0};
    }

    // create MIR function
    char fn_name[260];
    snprintf(fn_name, sizeof(fn_name), "rbu_%s", fc->name);

    MIR_type_t res_type = MIR_T_I64;
    fc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type, nparams, params);

    // save parent state
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    bool saved_has_block = mt->has_block_param;

    mt->current_func_item = fc->func_item;
    mt->current_func = fc->func_item->u.func;
    mt->has_block_param = func_has_block;

    rm_push_scope(mt);

    // register user params as variables
    p = meth->params;
    pi = 0;
    for (int i = 0; i < fc->param_count && p; i++) {
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->is_block) { p = p->next; continue; }
            if (pp->name) {
                char vname[128];
                snprintf(vname, sizeof(vname), "_rb_%.*s", (int)pp->name->len, pp->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, params[pi].name, mt->current_func);
                rm_set_var(mt, vname, preg);
            }
            pi++;
        }
        p = p->next;
    }

    // register &block param
    if (func_has_block) {
        MIR_reg_t block_reg = MIR_reg(mt->ctx, "_rb__block", mt->current_func);
        rm_set_var(mt, "_rb__block", block_reg);
    }

    // emit default parameter assignments: if param is null, set to default value
    p = meth->params;
    pi = 0;
    for (int i = 0; i < fc->param_count && p; i++) {
        if (p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (pp->is_block) { p = p->next; continue; }
            if (pp->name && pp->default_value) {
                MIR_reg_t preg = MIR_reg(mt->ctx, params[pi].name, mt->current_func);

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
        if (p->node_type == RB_AST_NODE_PARAMETER || p->node_type == RB_AST_NODE_DEFAULT_PARAMETER) {
            RbParamNode* pp = (RbParamNode*)p;
            if (!pp->is_block) pi++;
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
        if (!s->next && !rm_is_statement_node(s->node_type)) {
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
    mt->has_block_param = saved_has_block;

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

    // Phase 1a: collect functions (including class methods)
    rm_collect_functions(mt, root);
    log_debug("rb-mir: collected %d functions (incl. class methods)", mt->func_count);

    // Phase 1b: collect all blocks in the AST
    rm_collect_blocks_r(mt, root);
    log_debug("rb-mir: collected %d blocks", mt->block_count);

    // Phase 2: scan module-level variables
    rm_scan_module_vars(mt, root);

    // Phase 3a: forward-declare all module-level functions (not class methods)
    for (int i = 0; i < mt->func_count; i++) {
        RbFuncCollected* fc = &mt->func_entries[i];
        if (fc->is_method) continue; // class methods don't need forward declarations
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

    // Phase 3b: compile all blocks (before any functions that reference them)
    for (int i = 0; i < mt->block_count; i++) {
        rm_compile_block(mt, &mt->block_entries[i]);
    }

    // Phase 3c: compile all functions — class methods first, then free functions
    // (reverse order for nested support within free functions)
    for (int i = mt->func_count - 1; i >= 0; i--) {
        RbFuncCollected* fc = &mt->func_entries[i];
        if (fc->is_method) {
            rm_compile_class_method(mt, fc, (MIR_reg_t)0); // cls_reg not needed during pre-compilation
        } else {
            rm_compile_function(mt, fc);
        }
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
            s->node_type == RB_AST_NODE_NEXT ||
            s->node_type == RB_AST_NODE_CLASS_DEF ||
            s->node_type == RB_AST_NODE_MODULE_DEF ||
            s->node_type == RB_AST_NODE_BEGIN_RESCUE ||
            s->node_type == RB_AST_NODE_RETRY) {
            rm_transpile_statement(mt, s);
        } else {
            // expression statement
            rm_transpile_expression(mt, s);
        }

        s = s->next;
    }

    // return null — Ruby scripts produce output via puts/print
    MIR_reg_t ret_null = rm_emit_null(mt);
    rm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, ret_null)));

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

    // also set JS input context — js_new_function and js_function_get_ptr use it
    js_runtime_set_input(input);

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
