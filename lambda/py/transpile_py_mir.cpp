// transpile_py_mir.cpp - Direct MIR generation for Python
//
// Mirrors the JavaScript MIR transpiler architecture (transpile_js_mir.cpp).
// All Python values are boxed Items (MIR_T_I64). Runtime functions
// (py_add, py_subtract, etc.) take and return Items.

#include "py_transpiler.hpp"
#include "py_runtime.h"
#include "../lambda-data.hpp"
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

// External reference to Lambda runtime context
extern "C" Context* _lambda_rt;
extern "C" {
    void* import_resolver(const char* name);
}

extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern "C" void py_reset_module_vars();

// ============================================================================
// Constants
// ============================================================================
static const uint64_t PY_ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t PY_ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t PY_ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t PY_ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t PY_STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;
static const uint64_t PY_MASK56         = 0x00FFFFFFFFFFFFFFULL;

// ============================================================================
// Transpiler structs
// ============================================================================

struct PyMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    bool from_env;
    int env_slot;
    MIR_reg_t env_reg;
};

struct PyLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
};

struct PyFuncCollected {
    PyFunctionDefNode* node;
    char name[128];
    MIR_item_t func_item;
    int param_count;
    // capture info for closures
    char captures[32][128];
    int capture_count;
    bool has_scope_env;
    int scope_env_count;
    char scope_env_names[32][128];
};

struct PyMirTranspiler {
    PyTranspiler* tp;

    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache
    struct hashmap* import_cache;
    // Local function items
    struct hashmap* local_funcs;

    // Variable scopes: array of hashmaps
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop label stack
    PyLoopLabels loop_stack[32];
    int loop_depth;

    int reg_counter;
    int label_counter;

    // Collected functions
    PyFuncCollected func_entries[128];
    int func_count;

    // Module vars
    int module_var_count;

    // Module-level constants
    struct hashmap* module_consts;

    bool in_main;

    // Scope env for closures
    MIR_reg_t scope_env_reg;
    int scope_env_slot_count;
    int current_func_index;
};

// ============================================================================
// Hashmap helpers
// ============================================================================

struct PyImportCacheEntry {
    char name[128];
    MIR_item_t proto;
    MIR_item_t import;
};

static int py_import_cache_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((PyImportCacheEntry*)a)->name, ((PyImportCacheEntry*)b)->name);
}
static uint64_t py_import_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((PyImportCacheEntry*)item)->name,
        strlen(((PyImportCacheEntry*)item)->name), seed0, seed1);
}

struct PyVarScopeEntry {
    char name[128];
    PyMirVarEntry var;
};

static int py_var_scope_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((PyVarScopeEntry*)a)->name, ((PyVarScopeEntry*)b)->name);
}
static uint64_t py_var_scope_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((PyVarScopeEntry*)item)->name,
        strlen(((PyVarScopeEntry*)item)->name), seed0, seed1);
}

struct PyLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

static int py_local_func_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((PyLocalFuncEntry*)a)->name, ((PyLocalFuncEntry*)b)->name);
}
static uint64_t py_local_func_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((PyLocalFuncEntry*)item)->name,
        strlen(((PyLocalFuncEntry*)item)->name), seed0, seed1);
}

struct PyModuleConstEntry {
    char name[128];
    enum { MC_INT, MC_FLOAT, MC_NONE, MC_BOOL, MC_MODVAR } const_type;
    int64_t int_val;
    double float_val;
};

static int py_module_const_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((PyModuleConstEntry*)a)->name, ((PyModuleConstEntry*)b)->name);
}
static uint64_t py_module_const_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((PyModuleConstEntry*)item)->name,
        strlen(((PyModuleConstEntry*)item)->name), seed0, seed1);
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t pm_transpile_expression(PyMirTranspiler* mt, PyAstNode* expr);
static void pm_transpile_statement(PyMirTranspiler* mt, PyAstNode* stmt);

// ============================================================================
// Basic MIR helpers
// ============================================================================

static MIR_reg_t pm_new_reg(PyMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, mt->reg_counter++);
    MIR_type_t rtype = (type == MIR_T_P || type == MIR_T_F) ? MIR_T_I64 : type;
    return MIR_new_func_reg(mt->ctx, mt->current_func, rtype, name);
}

static MIR_label_t pm_new_label(PyMirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

static void pm_emit(PyMirTranspiler* mt, MIR_insn_t insn) {
    MIR_append_insn(mt->ctx, mt->current_func_item, insn);
}

static void pm_emit_label(PyMirTranspiler* mt, MIR_label_t label) {
    MIR_append_insn(mt->ctx, mt->current_func_item, label);
}

// ============================================================================
// Import management
// ============================================================================

static PyImportCacheEntry* pm_ensure_import(PyMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    PyImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    PyImportCacheEntry* found = (PyImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return found;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    PyImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.proto = proto;
    new_entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (PyImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return found;
}

// ============================================================================
// Call helpers
// ============================================================================

static MIR_reg_t pm_call_0(PyMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

static MIR_reg_t pm_call_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1));
    return res;
}

static MIR_reg_t pm_call_2(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2));
    return res;
}

static MIR_reg_t pm_call_3(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3));
    return res;
}

static MIR_reg_t pm_call_4(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4));
    return res;
}

static void pm_call_void_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1));
}

static void pm_call_void_2(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2));
}

// ============================================================================
// Scope management
// ============================================================================

static void pm_push_scope(PyMirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("py-mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(PyVarScopeEntry), 16, 0, 0,
        py_var_scope_hash, py_var_scope_cmp, NULL, NULL);
}

static void pm_pop_scope(PyMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("py-mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void pm_set_var(PyMirTranspiler* mt, const char* name, MIR_reg_t reg) {
    PyVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = MIR_T_I64;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static PyMirVarEntry* pm_find_var(PyMirTranspiler* mt, const char* name) {
    PyVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        PyVarScopeEntry* found = (PyVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// Find a var only in the current scope
static PyMirVarEntry* pm_find_var_current(PyMirTranspiler* mt, const char* name) {
    PyVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    if (!mt->var_scopes[mt->scope_depth]) return NULL;
    PyVarScopeEntry* found = (PyVarScopeEntry*)hashmap_get(mt->var_scopes[mt->scope_depth], &key);
    return found ? &found->var : NULL;
}

// ============================================================================
// Boxing helpers
// ============================================================================

static MIR_reg_t pm_emit_null(PyMirTranspiler* mt) {
    MIR_reg_t r = pm_new_reg(mt, "null", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
    return r;
}

static MIR_reg_t pm_box_int_const(PyMirTranspiler* mt, int64_t value) {
    MIR_reg_t r = pm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = PY_ITEM_INT_TAG | ((uint64_t)value & PY_MASK56);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)tagged)));
    return r;
}

static MIR_reg_t pm_box_float(PyMirTranspiler* mt, MIR_reg_t d_reg) {
    return pm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d_reg));
}

static MIR_reg_t pm_box_string(PyMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = pm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    pm_emit_label(mt, l_nn);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_box_string_literal(PyMirTranspiler* mt, const char* str, int len) {
    String* interned = name_pool_create_len(mt->tp->name_pool, str, len);
    MIR_reg_t ptr = pm_new_reg(mt, "cs", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
    MIR_reg_t name_reg = pm_call_1(mt, "heap_create_name", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, ptr));
    return pm_box_string(mt, name_reg);
}

static MIR_reg_t pm_emit_bool(PyMirTranspiler* mt, bool value) {
    MIR_reg_t r = pm_new_reg(mt, "bool", MIR_T_I64);
    uint64_t bval = value ? PY_ITEM_TRUE_VAL : PY_ITEM_FALSE_VAL;
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)bval)));
    return r;
}

// ============================================================================
// Expression transpilers
// ============================================================================

static MIR_reg_t pm_transpile_literal(PyMirTranspiler* mt, PyLiteralNode* lit) {
    switch (lit->literal_type) {
    case PY_LITERAL_INT:
        return pm_box_int_const(mt, lit->value.int_value);
    case PY_LITERAL_FLOAT: {
        MIR_reg_t d = pm_new_reg(mt, "dbl", MIR_T_D);
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_reg_op(mt->ctx, d),
            MIR_new_double_op(mt->ctx, lit->value.float_value)));
        return pm_box_float(mt, d);
    }
    case PY_LITERAL_STRING: {
        String* sv = lit->value.string_value;
        return pm_box_string_literal(mt, sv->chars, sv->len);
    }
    case PY_LITERAL_BOOLEAN:
        return pm_emit_bool(mt, lit->value.boolean_value);
    case PY_LITERAL_NONE:
        return pm_emit_null(mt);
    }
    return pm_emit_null(mt);
}

static MIR_reg_t pm_transpile_identifier(PyMirTranspiler* mt, PyIdentifierNode* id) {
    // Python builtins as names
    if (id->name->len == 4 && strncmp(id->name->chars, "None", 4) == 0) {
        return pm_emit_null(mt);
    }
    if (id->name->len == 4 && strncmp(id->name->chars, "True", 4) == 0) {
        return pm_emit_bool(mt, true);
    }
    if (id->name->len == 5 && strncmp(id->name->chars, "False", 5) == 0) {
        return pm_emit_bool(mt, false);
    }

    char vname[128];
    snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
    PyMirVarEntry* var = pm_find_var(mt, vname);
    if (var) return var->reg;

    // Check local functions
    if (mt->local_funcs) {
        PyLocalFuncEntry key;
        snprintf(key.name, sizeof(key.name), "%s", vname);
        PyLocalFuncEntry* found = (PyLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
        if (found) {
            // create a function Item from the MIR function
            for (int i = 0; i < mt->func_count; i++) {
                char fname[128];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[i].name);
                if (strcmp(fname, vname) == 0) {
                    MIR_reg_t fptr = pm_new_reg(mt, "fptr", MIR_T_I64);
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, fptr),
                        MIR_new_ref_op(mt->ctx, found->func_item)));
                    return pm_call_2(mt, "py_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, fptr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, mt->func_entries[i].param_count));
                }
            }
        }
    }

    log_debug("py-mir: undefined variable '%.*s'", (int)id->name->len, id->name->chars);
    return pm_emit_null(mt);
}

static const char* pm_binary_op_func(PyOperator op) {
    switch (op) {
    case PY_OP_ADD:        return "py_add";
    case PY_OP_SUB:        return "py_subtract";
    case PY_OP_MUL:        return "py_multiply";
    case PY_OP_DIV:        return "py_divide";
    case PY_OP_FLOOR_DIV:  return "py_floor_divide";
    case PY_OP_MOD:        return "py_modulo";
    case PY_OP_POW:        return "py_power";
    case PY_OP_LSHIFT:     return "py_lshift";
    case PY_OP_RSHIFT:     return "py_rshift";
    case PY_OP_BIT_AND:    return "py_bit_and";
    case PY_OP_BIT_OR:     return "py_bit_or";
    case PY_OP_BIT_XOR:    return "py_bit_xor";
    default:               return "py_add";
    }
}

static const char* pm_compare_op_func(PyOperator op) {
    switch (op) {
    case PY_OP_EQ:         return "py_eq";
    case PY_OP_NE:         return "py_ne";
    case PY_OP_LT:         return "py_lt";
    case PY_OP_LE:         return "py_le";
    case PY_OP_GT:         return "py_gt";
    case PY_OP_GE:         return "py_ge";
    case PY_OP_IS:         return "py_is";
    case PY_OP_IS_NOT:     return "py_is_not";
    case PY_OP_IN:         return "py_contains";
    case PY_OP_NOT_IN:     return NULL; // handled specially
    default:               return "py_eq";
    }
}

static MIR_reg_t pm_transpile_binary(PyMirTranspiler* mt, PyBinaryNode* bin) {
    MIR_reg_t left = pm_transpile_expression(mt, bin->left);
    MIR_reg_t right = pm_transpile_expression(mt, bin->right);
    const char* fn = pm_binary_op_func(bin->op);
    return pm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
}

static MIR_reg_t pm_transpile_unary(PyMirTranspiler* mt, PyUnaryNode* un) {
    MIR_reg_t operand = pm_transpile_expression(mt, un->operand);
    const char* fn;
    switch (un->op) {
    case PY_OP_NEGATE:  fn = "py_negate"; break;
    case PY_OP_POSITIVE: fn = "py_positive"; break;
    case PY_OP_BIT_NOT: fn = "py_bit_not"; break;
    default:            fn = "py_negate"; break;
    }
    return pm_call_1(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, operand));
}

static MIR_reg_t pm_transpile_not(PyMirTranspiler* mt, PyUnaryNode* un) {
    MIR_reg_t operand = pm_transpile_expression(mt, un->operand);
    // call py_is_truthy, negate result, box as bool
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, operand));
    // negate: result = truthy ? False : True
    MIR_reg_t result = pm_new_reg(mt, "not", MIR_T_I64);
    MIR_label_t l_true = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
        MIR_new_reg_op(mt->ctx, truthy)));
    // truthy was false → return True
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_TRUE_VAL)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    pm_emit_label(mt, l_true);
    // truthy was true → return False
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_FALSE_VAL)));
    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_boolean(PyMirTranspiler* mt, PyBooleanNode* bop) {
    // short-circuit: and/or
    MIR_reg_t left = pm_transpile_expression(mt, bop->left);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left));

    MIR_reg_t result = pm_new_reg(mt, "bop", MIR_T_I64);
    MIR_label_t l_short = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (bop->op == PY_OP_AND) {
        // and: if left is falsy, return left
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_short),
            MIR_new_reg_op(mt->ctx, truthy)));
    } else {
        // or: if left is truthy, return left
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_short),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // evaluate right
    MIR_reg_t right = pm_transpile_expression(mt, bop->right);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, right)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    pm_emit_label(mt, l_short);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, left)));
    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_compare(PyMirTranspiler* mt, PyCompareNode* cmp) {
    // chained comparisons: a < b < c → (a < b) and (b < c)
    MIR_reg_t result = pm_new_reg(mt, "cmp", MIR_T_I64);
    MIR_label_t l_false = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    MIR_reg_t left = pm_transpile_expression(mt, cmp->left);

    for (int i = 0; i < cmp->op_count; i++) {
        MIR_reg_t right = pm_transpile_expression(mt, cmp->comparators[i]);

        const char* fn = pm_compare_op_func(cmp->ops[i]);
        MIR_reg_t cmp_result;

        if (cmp->ops[i] == PY_OP_NOT_IN) {
            // not in → negate contains
            cmp_result = pm_call_2(mt, "py_contains", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left));
            // negate
            MIR_reg_t neg = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cmp_result));
            MIR_label_t l_was_true = pm_new_label(mt);
            MIR_label_t l_neg_end = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_was_true),
                MIR_new_reg_op(mt->ctx, neg)));
            cmp_result = pm_new_reg(mt, "neg", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, cmp_result),
                MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_TRUE_VAL)));
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_neg_end)));
            pm_emit_label(mt, l_was_true);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, cmp_result),
                MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_FALSE_VAL)));
            pm_emit_label(mt, l_neg_end);
        } else if (cmp->ops[i] == PY_OP_IN) {
            // in → py_contains(container, value)
            cmp_result = pm_call_2(mt, "py_contains", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left));
        } else {
            cmp_result = pm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
        }

        // check if comparison is false → short-circuit
        MIR_reg_t is_true = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cmp_result));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
            MIR_new_reg_op(mt->ctx, is_true)));

        left = right;
    }

    // all comparisons passed
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_TRUE_VAL)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    pm_emit_label(mt, l_false);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_FALSE_VAL)));

    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_call(PyMirTranspiler* mt, PyCallNode* call) {
    // check for builtin calls
    if (call->function && call->function->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
        const char* name = fn_id->name->chars;
        int name_len = fn_id->name->len;

        // print()
        if (name_len == 5 && strncmp(name, "print", 5) == 0) {
            // collect args on stack
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[16];
            while (arg && argc < 16) {
                if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    arg = arg->next; continue; // skip keyword args
                }
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }

            // allocate stack array for args
            MIR_reg_t args_ptr = pm_new_reg(mt, "args", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc * 8)));
            for (int i = 0; i < argc; i++) {
                MIR_reg_t addr = pm_new_reg(mt, "addr", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, arg_regs[i])));
            }

            return pm_call_2(mt, "py_print", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        }

        // len()
        if (name_len == 3 && strncmp(name, "len", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_len", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // type()
        if (name_len == 4 && strncmp(name, "type", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_type", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // int(), float(), str(), bool()
        if (name_len == 3 && strncmp(name, "int", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_int_const(mt, 0);
            return pm_call_1(mt, "py_builtin_int", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 5 && strncmp(name, "float", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_int_const(mt, 0);
            return pm_call_1(mt, "py_builtin_float", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "str", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_string_literal(mt, "", 0);
            return pm_call_1(mt, "py_builtin_str", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 4 && strncmp(name, "bool", 4) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_emit_bool(mt, false);
            return pm_call_1(mt, "py_builtin_bool", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // abs()
        if (name_len == 3 && strncmp(name, "abs", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_abs", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // range()
        if (name_len == 5 && strncmp(name, "range", 5) == 0) {
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[3];
            while (arg && argc < 3) {
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            MIR_reg_t args_ptr = pm_new_reg(mt, "rargs", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc * 8)));
            for (int i = 0; i < argc; i++) {
                MIR_reg_t addr = pm_new_reg(mt, "ra", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, arg_regs[i])));
            }
            return pm_call_2(mt, "py_builtin_range", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        }

        // min(), max(), sum()
        if ((name_len == 3 && strncmp(name, "min", 3) == 0) ||
            (name_len == 3 && strncmp(name, "max", 3) == 0)) {
            const char* fn_name = (name[1] == 'i') ? "py_builtin_min" : "py_builtin_max";
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[16];
            while (arg && argc < 16) {
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            MIR_reg_t args_ptr = pm_new_reg(mt, "mmargs", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc * 8)));
            for (int i = 0; i < argc; i++) {
                MIR_reg_t addr = pm_new_reg(mt, "mma", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, arg_regs[i])));
            }
            return pm_call_2(mt, fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        }

        if (name_len == 3 && strncmp(name, "sum", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_sum", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // sorted(), reversed()
        if (name_len == 6 && strncmp(name, "sorted", 6) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_sorted", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 8 && strncmp(name, "reversed", 8) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_reversed", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // enumerate()
        if (name_len == 9 && strncmp(name, "enumerate", 9) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_enumerate", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // ord(), chr()
        if (name_len == 3 && strncmp(name, "ord", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_ord", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "chr", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_chr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // input()
        if (name_len == 5 && strncmp(name, "input", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_box_string_literal(mt, "", 0);
            return pm_call_1(mt, "py_builtin_input", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // repr()
        if (name_len == 4 && strncmp(name, "repr", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_repr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // list()
        if (name_len == 4 && strncmp(name, "list", 4) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_list", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
    }

    // method call: obj.method(args)
    if (call->function && call->function->node_type == PY_AST_NODE_ATTRIBUTE) {
        PyAttributeNode* attr = (PyAttributeNode*)call->function;
        MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
        MIR_reg_t method_name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);

        // collect args
        int argc = 0;
        PyAstNode* arg = call->arguments;
        MIR_reg_t arg_regs[16];
        while (arg && argc < 16) {
            if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                arg = arg->next; continue;
            }
            arg_regs[argc++] = pm_transpile_expression(mt, arg);
            arg = arg->next;
        }

        MIR_reg_t args_ptr = pm_new_reg(mt, "margs", MIR_T_I64);
        if (argc > 0) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc * 8)));
            for (int i = 0; i < argc; i++) {
                MIR_reg_t addr = pm_new_reg(mt, "ma", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, arg_regs[i])));
            }
        } else {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, 0)));
        }

        // dispatch: try string, then list, then dict method
        // simpler approach: call py_string_method first, if returns null try list, then dict
        MIR_reg_t str_result = pm_call_4(mt, "py_string_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, argc));

        // check if string method returned non-null
        MIR_reg_t result = pm_new_reg(mt, "mcall", MIR_T_I64);
        MIR_label_t l_try_list = pm_new_label(mt);
        MIR_label_t l_try_dict = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);

        // if str_result != NULL_VAL, use it
        MIR_reg_t is_null = pm_new_reg(mt, "isnull", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_null),
            MIR_new_reg_op(mt->ctx, str_result),
            MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_list),
            MIR_new_reg_op(mt->ctx, is_null)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, str_result)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        pm_emit_label(mt, l_try_list);
        MIR_reg_t list_result = pm_call_4(mt, "py_list_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_null),
            MIR_new_reg_op(mt->ctx, list_result),
            MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_try_dict),
            MIR_new_reg_op(mt->ctx, is_null)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, list_result)));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        pm_emit_label(mt, l_try_dict);
        MIR_reg_t dict_result = pm_call_4(mt, "py_dict_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, dict_result)));

        pm_emit_label(mt, l_end);
        return result;
    }

    // generic function call: evaluate function, collect args, call py_call_function
    MIR_reg_t func = pm_transpile_expression(mt, call->function);

    int argc = 0;
    PyAstNode* arg = call->arguments;
    MIR_reg_t arg_regs[16];
    while (arg && argc < 16) {
        if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
            arg = arg->next; continue;
        }
        arg_regs[argc++] = pm_transpile_expression(mt, arg);
        arg = arg->next;
    }

    MIR_reg_t args_ptr = pm_new_reg(mt, "cargs", MIR_T_I64);
    if (argc > 0) {
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
            MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_new_int_op(mt->ctx, argc * 8)));
        for (int i = 0; i < argc; i++) {
            MIR_reg_t addr = pm_new_reg(mt, "ca", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, addr),
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, i * 8)));
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                MIR_new_reg_op(mt->ctx, arg_regs[i])));
        }
    } else {
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_new_int_op(mt->ctx, 0)));
    }

    return pm_call_3(mt, "py_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, func),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
        MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
}

static MIR_reg_t pm_transpile_attribute(PyMirTranspiler* mt, PyAttributeNode* attr) {
    MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
    MIR_reg_t name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);
    return pm_call_2(mt, "py_getattr", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, name));
}

static MIR_reg_t pm_transpile_subscript(PyMirTranspiler* mt, PySubscriptNode* sub) {
    MIR_reg_t obj = pm_transpile_expression(mt, sub->object);
    MIR_reg_t key = pm_transpile_expression(mt, sub->index);
    return pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
}

static MIR_reg_t pm_transpile_list(PyMirTranspiler* mt, PySequenceNode* seq) {
    MIR_reg_t list = pm_call_1(mt, "py_list_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

    PyAstNode* elem = seq->elements;
    while (elem) {
        MIR_reg_t val = pm_transpile_expression(mt, elem);
        pm_call_2(mt, "py_list_append", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, list),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        elem = elem->next;
    }
    return list;
}

static MIR_reg_t pm_transpile_tuple(PyMirTranspiler* mt, PySequenceNode* seq) {
    int length = seq->length;
    MIR_reg_t tuple = pm_call_1(mt, "py_tuple_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, length));

    PyAstNode* elem = seq->elements;
    int i = 0;
    while (elem) {
        MIR_reg_t val = pm_transpile_expression(mt, elem);
        pm_call_3(mt, "py_tuple_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, tuple),
            MIR_T_I64, MIR_new_int_op(mt->ctx, i),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        elem = elem->next;
        i++;
    }
    return tuple;
}

static MIR_reg_t pm_transpile_dict(PyMirTranspiler* mt, PyDictNode* dict) {
    MIR_reg_t d = pm_call_0(mt, "py_dict_new", MIR_T_I64);

    PyAstNode* pair_node = dict->pairs;
    while (pair_node) {
        if (pair_node->node_type == PY_AST_NODE_PAIR) {
            PyPairNode* pair = (PyPairNode*)pair_node;
            MIR_reg_t key = pm_transpile_expression(mt, pair->key);
            MIR_reg_t val = pm_transpile_expression(mt, pair->value);
            pm_call_3(mt, "py_dict_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, d),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        pair_node = pair_node->next;
    }
    return d;
}

static MIR_reg_t pm_transpile_conditional(PyMirTranspiler* mt, PyConditionalNode* cond) {
    MIR_reg_t test = pm_transpile_expression(mt, cond->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));

    MIR_reg_t result = pm_new_reg(mt, "cond", MIR_T_I64);
    MIR_label_t l_else = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, truthy)));

    MIR_reg_t then_val = pm_transpile_expression(mt, cond->body);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, then_val)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    pm_emit_label(mt, l_else);
    MIR_reg_t else_val = pm_transpile_expression(mt, cond->else_body);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, else_val)));

    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_fstring(PyMirTranspiler* mt, PyFStringNode* fstr) {
    // f-string: concatenate parts via py_to_str + py_add
    MIR_reg_t result = pm_box_string_literal(mt, "", 0);
    PyAstNode* part = fstr->parts;
    while (part) {
        MIR_reg_t val;
        if (part->node_type == PY_AST_NODE_LITERAL) {
            val = pm_transpile_literal(mt, (PyLiteralNode*)part);
        } else {
            MIR_reg_t expr = pm_transpile_expression(mt, part);
            val = pm_call_1(mt, "py_to_str", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, expr));
        }
        result = pm_call_2(mt, "py_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        part = part->next;
    }
    return result;
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static MIR_reg_t pm_transpile_expression(PyMirTranspiler* mt, PyAstNode* expr) {
    if (!expr) return pm_emit_null(mt);

    switch (expr->node_type) {
    case PY_AST_NODE_LITERAL:
        return pm_transpile_literal(mt, (PyLiteralNode*)expr);
    case PY_AST_NODE_IDENTIFIER:
        return pm_transpile_identifier(mt, (PyIdentifierNode*)expr);
    case PY_AST_NODE_BINARY_OP:
        return pm_transpile_binary(mt, (PyBinaryNode*)expr);
    case PY_AST_NODE_UNARY_OP:
        return pm_transpile_unary(mt, (PyUnaryNode*)expr);
    case PY_AST_NODE_NOT:
        return pm_transpile_not(mt, (PyUnaryNode*)expr);
    case PY_AST_NODE_BOOLEAN_OP:
        return pm_transpile_boolean(mt, (PyBooleanNode*)expr);
    case PY_AST_NODE_COMPARE:
        return pm_transpile_compare(mt, (PyCompareNode*)expr);
    case PY_AST_NODE_CALL:
        return pm_transpile_call(mt, (PyCallNode*)expr);
    case PY_AST_NODE_ATTRIBUTE:
        return pm_transpile_attribute(mt, (PyAttributeNode*)expr);
    case PY_AST_NODE_SUBSCRIPT:
        return pm_transpile_subscript(mt, (PySubscriptNode*)expr);
    case PY_AST_NODE_LIST:
        return pm_transpile_list(mt, (PySequenceNode*)expr);
    case PY_AST_NODE_TUPLE:
        return pm_transpile_tuple(mt, (PySequenceNode*)expr);
    case PY_AST_NODE_SET:
        return pm_transpile_list(mt, (PySequenceNode*)expr); // sets as lists for now
    case PY_AST_NODE_DICT:
        return pm_transpile_dict(mt, (PyDictNode*)expr);
    case PY_AST_NODE_CONDITIONAL_EXPR:
        return pm_transpile_conditional(mt, (PyConditionalNode*)expr);
    case PY_AST_NODE_FSTRING:
        return pm_transpile_fstring(mt, (PyFStringNode*)expr);
    default:
        log_error("py-mir: unsupported expression type %d", expr->node_type);
        return pm_emit_null(mt);
    }
}

// ============================================================================
// Statement transpilers
// ============================================================================

static void pm_transpile_assignment(PyMirTranspiler* mt, PyAssignmentNode* assign) {
    MIR_reg_t val = pm_transpile_expression(mt, assign->value);

    // walk targets
    PyAstNode* target = assign->targets;
    while (target) {
        if (target->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* id = (PyIdentifierNode*)target;
            char vname[128];
            snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);

            PyMirVarEntry* var = pm_find_var(mt, vname);
            if (var) {
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            } else {
                // new variable
                MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, reg),
                    MIR_new_reg_op(mt->ctx, val)));
                pm_set_var(mt, vname, reg);
            }
        } else if (target->node_type == PY_AST_NODE_SUBSCRIPT) {
            PySubscriptNode* sub = (PySubscriptNode*)target;
            MIR_reg_t obj = pm_transpile_expression(mt, sub->object);
            MIR_reg_t key = pm_transpile_expression(mt, sub->index);
            pm_call_3(mt, "py_subscript_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else if (target->node_type == PY_AST_NODE_ATTRIBUTE) {
            PyAttributeNode* attr = (PyAttributeNode*)target;
            MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
            MIR_reg_t name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);
            pm_call_3(mt, "py_setattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else if (target->node_type == PY_AST_NODE_TUPLE_UNPACK || target->node_type == PY_AST_NODE_TUPLE) {
            // tuple unpacking: a, b = expr
            PySequenceNode* tup = (PySequenceNode*)target;
            PyAstNode* elem = tup->elements;
            int idx = 0;
            while (elem) {
                MIR_reg_t item_val = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, pm_box_int_const(mt, idx)));

                if (elem->node_type == PY_AST_NODE_IDENTIFIER) {
                    PyIdentifierNode* eid = (PyIdentifierNode*)elem;
                    char evname[128];
                    snprintf(evname, sizeof(evname), "_py_%.*s", (int)eid->name->len, eid->name->chars);
                    PyMirVarEntry* evar = pm_find_var(mt, evname);
                    if (evar) {
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, evar->reg),
                            MIR_new_reg_op(mt->ctx, item_val)));
                    } else {
                        MIR_reg_t reg = pm_new_reg(mt, evname, MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, item_val)));
                        pm_set_var(mt, evname, reg);
                    }
                }
                elem = elem->next;
                idx++;
            }
        }
        target = target->next;
    }
}

static void pm_transpile_aug_assignment(PyMirTranspiler* mt, PyAugAssignmentNode* aug) {
    // convert op to binary op
    PyOperator bin_op;
    switch (aug->op) {
    case PY_OP_ADD_ASSIGN:       bin_op = PY_OP_ADD; break;
    case PY_OP_SUB_ASSIGN:       bin_op = PY_OP_SUB; break;
    case PY_OP_MUL_ASSIGN:       bin_op = PY_OP_MUL; break;
    case PY_OP_DIV_ASSIGN:       bin_op = PY_OP_DIV; break;
    case PY_OP_FLOOR_DIV_ASSIGN: bin_op = PY_OP_FLOOR_DIV; break;
    case PY_OP_MOD_ASSIGN:       bin_op = PY_OP_MOD; break;
    case PY_OP_POW_ASSIGN:       bin_op = PY_OP_POW; break;
    case PY_OP_LSHIFT_ASSIGN:    bin_op = PY_OP_LSHIFT; break;
    case PY_OP_RSHIFT_ASSIGN:    bin_op = PY_OP_RSHIFT; break;
    case PY_OP_BIT_AND_ASSIGN:   bin_op = PY_OP_BIT_AND; break;
    case PY_OP_BIT_OR_ASSIGN:    bin_op = PY_OP_BIT_OR; break;
    case PY_OP_BIT_XOR_ASSIGN:   bin_op = PY_OP_BIT_XOR; break;
    default:                     bin_op = PY_OP_ADD; break;
    }

    MIR_reg_t left = pm_transpile_expression(mt, aug->target);
    MIR_reg_t right = pm_transpile_expression(mt, aug->value);
    const char* fn = pm_binary_op_func(bin_op);
    MIR_reg_t val = pm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));

    // assign back
    if (aug->target->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)aug->target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, val)));
        }
    } else if (aug->target->node_type == PY_AST_NODE_SUBSCRIPT) {
        PySubscriptNode* sub = (PySubscriptNode*)aug->target;
        MIR_reg_t obj = pm_transpile_expression(mt, sub->object);
        MIR_reg_t key = pm_transpile_expression(mt, sub->index);
        pm_call_3(mt, "py_subscript_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }
}

static void pm_transpile_if(PyMirTranspiler* mt, PyIfNode* ifn) {
    MIR_reg_t test = pm_transpile_expression(mt, ifn->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));

    MIR_label_t l_else = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, truthy)));

    // then body
    if (ifn->body) {
        PyAstNode* s;
        if (ifn->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)ifn->body)->statements;
        } else {
            s = ifn->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    pm_emit_label(mt, l_else);

    // elif clauses
    PyAstNode* elif = ifn->elif_clauses;
    while (elif) {
        if (elif->node_type == PY_AST_NODE_ELIF) {
            PyIfNode* elif_node = (PyIfNode*)elif;
            MIR_reg_t elif_test = pm_transpile_expression(mt, elif_node->test);
            MIR_reg_t elif_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, elif_test));
            MIR_label_t l_next = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_next),
                MIR_new_reg_op(mt->ctx, elif_truthy)));

            if (elif_node->body) {
                PyAstNode* s;
                if (elif_node->body->node_type == PY_AST_NODE_BLOCK) {
                    s = ((PyBlockNode*)elif_node->body)->statements;
                } else {
                    s = elif_node->body;
                }
                while (s) { pm_transpile_statement(mt, s); s = s->next; }
            }
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            pm_emit_label(mt, l_next);
        }
        elif = elif->next;
    }

    // else body
    if (ifn->else_body) {
        PyAstNode* s;
        if (ifn->else_body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)ifn->else_body)->statements;
        } else {
            s = ifn->else_body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit_label(mt, l_end);
}

static void pm_transpile_while(PyMirTranspiler* mt, PyWhileNode* wh) {
    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_start;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    pm_emit_label(mt, l_start);

    MIR_reg_t test = pm_transpile_expression(mt, wh->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, truthy)));

    if (wh->body) {
        PyAstNode* s;
        if (wh->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)wh->body)->statements;
        } else {
            s = wh->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start)));
    pm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

static void pm_transpile_for(PyMirTranspiler* mt, PyForNode* forn) {
    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, forn->iter);

    // get length
    MIR_reg_t length = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));

    // loop index
    MIR_reg_t idx = pm_new_reg(mt, "idx", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, (int64_t)(PY_ITEM_INT_TAG | 0))));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_continue;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    pm_emit_label(mt, l_start);

    // check idx < length
    MIR_reg_t cmp = pm_call_2(mt, "py_lt", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, length));
    MIR_reg_t cmp_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cmp));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp_truthy)));

    // get current item
    MIR_reg_t item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));

    // assign to loop variable
    if (forn->target->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)forn->target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, item)));
        } else {
            MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, reg),
                MIR_new_reg_op(mt->ctx, item)));
            pm_set_var(mt, vname, reg);
        }
    } else if (forn->target->node_type == PY_AST_NODE_TUPLE ||
               forn->target->node_type == PY_AST_NODE_TUPLE_UNPACK) {
        // tuple unpacking in for loop: for a, b in list_of_tuples
        PySequenceNode* tup = (PySequenceNode*)forn->target;
        PyAstNode* elem = tup->elements;
        int i = 0;
        while (elem) {
            MIR_reg_t sub_item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, item),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, pm_box_int_const(mt, i)));
            if (elem->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* eid = (PyIdentifierNode*)elem;
                char evname[128];
                snprintf(evname, sizeof(evname), "_py_%.*s", (int)eid->name->len, eid->name->chars);
                PyMirVarEntry* evar = pm_find_var(mt, evname);
                if (evar) {
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, evar->reg),
                        MIR_new_reg_op(mt->ctx, sub_item)));
                } else {
                    MIR_reg_t reg = pm_new_reg(mt, evname, MIR_T_I64);
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, reg),
                        MIR_new_reg_op(mt->ctx, sub_item)));
                    pm_set_var(mt, evname, reg);
                }
            }
            elem = elem->next;
            i++;
        }
    }

    // body
    if (forn->body) {
        PyAstNode* s;
        if (forn->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)forn->body)->statements;
        } else {
            s = forn->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit_label(mt, l_continue);

    // increment idx
    MIR_reg_t one = pm_box_int_const(mt, 1);
    MIR_reg_t new_idx = pm_call_2(mt, "py_add", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, new_idx)));

    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start)));
    pm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

static void pm_transpile_return(PyMirTranspiler* mt, PyReturnNode* ret) {
    MIR_reg_t val;
    if (ret->value) {
        val = pm_transpile_expression(mt, ret->value);
    } else {
        val = pm_emit_null(mt);
    }
    pm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
}

static void pm_transpile_block(PyMirTranspiler* mt, PyAstNode* body) {
    if (!body) return;
    PyAstNode* s;
    if (body->node_type == PY_AST_NODE_BLOCK) {
        s = ((PyBlockNode*)body)->statements;
    } else {
        s = body;
    }
    while (s) { pm_transpile_statement(mt, s); s = s->next; }
}

// ============================================================================
// Statement dispatcher
// ============================================================================

static void pm_transpile_statement(PyMirTranspiler* mt, PyAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
    case PY_AST_NODE_EXPRESSION_STATEMENT: {
        PyExpressionStatementNode* es = (PyExpressionStatementNode*)stmt;
        if (es->expression) {
            pm_transpile_expression(mt, es->expression);
        }
        break;
    }
    case PY_AST_NODE_ASSIGNMENT:
        pm_transpile_assignment(mt, (PyAssignmentNode*)stmt);
        break;
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT:
        pm_transpile_aug_assignment(mt, (PyAugAssignmentNode*)stmt);
        break;
    case PY_AST_NODE_IF:
        pm_transpile_if(mt, (PyIfNode*)stmt);
        break;
    case PY_AST_NODE_WHILE:
        pm_transpile_while(mt, (PyWhileNode*)stmt);
        break;
    case PY_AST_NODE_FOR:
        pm_transpile_for(mt, (PyForNode*)stmt);
        break;
    case PY_AST_NODE_RETURN:
        pm_transpile_return(mt, (PyReturnNode*)stmt);
        break;
    case PY_AST_NODE_BREAK:
        if (mt->loop_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        break;
    case PY_AST_NODE_CONTINUE:
        if (mt->loop_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        break;
    case PY_AST_NODE_PASS:
        // no-op
        break;
    case PY_AST_NODE_FUNCTION_DEF:
        // already handled in pre-pass
        break;
    case PY_AST_NODE_BLOCK: {
        pm_push_scope(mt);
        PyBlockNode* blk = (PyBlockNode*)stmt;
        PyAstNode* s = blk->statements;
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
        pm_pop_scope(mt);
        break;
    }
    case PY_AST_NODE_ASSERT: {
        PyAssertNode* assert_n = (PyAssertNode*)stmt;
        MIR_reg_t test = pm_transpile_expression(mt, assert_n->test);
        MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
        MIR_label_t l_ok = pm_new_label(mt);
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
            MIR_new_reg_op(mt->ctx, truthy)));
        // assert failed → raise
        MIR_reg_t exc_type = pm_box_string_literal(mt, "AssertionError", 14);
        MIR_reg_t exc_msg;
        if (assert_n->message) {
            exc_msg = pm_transpile_expression(mt, assert_n->message);
        } else {
            exc_msg = pm_box_string_literal(mt, "assertion failed", 16);
        }
        MIR_reg_t exc = pm_call_2(mt, "py_new_exception", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_type),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_msg));
        pm_call_void_1(mt, "py_raise",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, exc));
        pm_emit_label(mt, l_ok);
        break;
    }
    case PY_AST_NODE_GLOBAL:
    case PY_AST_NODE_NONLOCAL:
        // handled in scope analysis
        break;
    case PY_AST_NODE_DEL:
        // simplified: no-op
        break;
    default:
        log_debug("py-mir: unsupported statement type %d", stmt->node_type);
        break;
    }
}

// ============================================================================
// Function collection and compilation
// ============================================================================

static void pm_collect_functions(PyMirTranspiler* mt, PyAstNode* node) {
    if (!node) return;

    switch (node->node_type) {
    case PY_AST_NODE_MODULE: {
        PyModuleNode* mod = (PyModuleNode*)node;
        PyAstNode* s = mod->body;
        while (s) { pm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case PY_AST_NODE_BLOCK: {
        PyBlockNode* blk = (PyBlockNode*)node;
        PyAstNode* s = blk->statements;
        while (s) { pm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case PY_AST_NODE_FUNCTION_DEF: {
        PyFunctionDefNode* fn = (PyFunctionDefNode*)node;
        if (mt->func_count >= 128) break;

        PyFuncCollected* fc = &mt->func_entries[mt->func_count];
        memset(fc, 0, sizeof(PyFuncCollected));
        fc->node = fn;
        snprintf(fc->name, sizeof(fc->name), "%.*s", (int)fn->name->len, fn->name->chars);

        // count params
        int pc = 0;
        PyAstNode* p = fn->params;
        while (p) { pc++; p = p->next; }
        fc->param_count = pc;

        mt->func_count++;

        // recurse into body for nested functions
        pm_collect_functions(mt, fn->body);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* ifn = (PyIfNode*)node;
        pm_collect_functions(mt, ifn->body);
        PyAstNode* elif = ifn->elif_clauses;
        while (elif) { pm_collect_functions(mt, elif); elif = elif->next; }
        pm_collect_functions(mt, ifn->else_body);
        break;
    }
    case PY_AST_NODE_ELIF: {
        PyIfNode* elif = (PyIfNode*)node;
        pm_collect_functions(mt, elif->body);
        break;
    }
    case PY_AST_NODE_WHILE: {
        PyWhileNode* wh = (PyWhileNode*)node;
        pm_collect_functions(mt, wh->body);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* fn = (PyForNode*)node;
        pm_collect_functions(mt, fn->body);
        break;
    }
    default:
        break;
    }
}

static void pm_compile_function(PyMirTranspiler* mt, PyFuncCollected* fc) {
    char fn_name[140];
    snprintf(fn_name, sizeof(fn_name), "pyf_%s", fc->name);

    // create MIR function: each param needs a unique name
    MIR_var_t params[16];
    char* param_name_bufs[16];
    PyAstNode* pnode = fc->node->params;
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        param_name_bufs[i] = (char*)alloca(128);
        String* pn = NULL;
        if (pnode) {
            if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)pnode)->name;
            }
            pnode = pnode->next;
        }
        if (pn) {
            snprintf(param_name_bufs[i], 128, "_py_%.*s", (int)pn->len, pn->chars);
        } else {
            snprintf(param_name_bufs[i], 128, "_py_p%d", i);
        }
        params[i] = {MIR_T_I64, param_name_bufs[i], 0};
    }

    MIR_type_t res_type = MIR_T_I64;
    fc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type,
        fc->param_count, params);

    // save parent state
    MIR_item_t old_func_item = mt->current_func_item;
    MIR_func_t old_func = mt->current_func;
    int old_scope_depth = mt->scope_depth;

    mt->current_func_item = fc->func_item;
    mt->current_func = fc->func_item->u.func;

    // push function scope
    pm_push_scope(mt);

    // register params as variables — use MIR_reg to look up the register
    // that MIR_new_func_arr already created for each named parameter
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        MIR_reg_t preg = MIR_reg(mt->ctx, param_name_bufs[i], mt->current_func);
        pm_set_var(mt, param_name_bufs[i], preg);
    }

    // compile body
    pm_transpile_block(mt, fc->node->body);

    // ensure function ends with a return
    pm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));

    MIR_finish_func(mt->ctx);

    // restore parent state
    pm_pop_scope(mt);
    mt->current_func_item = old_func_item;
    mt->current_func = old_func;
    mt->scope_depth = old_scope_depth;

    // register in local_funcs
    PyLocalFuncEntry lfe;
    memset(&lfe, 0, sizeof(lfe));
    snprintf(lfe.name, sizeof(lfe.name), "_py_%s", fc->name);
    lfe.func_item = fc->func_item;
    hashmap_set(mt->local_funcs, &lfe);
}

// ============================================================================
// Module-level variable scan
// ============================================================================

static void pm_scan_module_vars(PyMirTranspiler* mt, PyAstNode* root) {
    // no-op: all variables are managed through the scope system
    (void)mt; (void)root;
}

// ============================================================================
// Main transpilation pipeline
// ============================================================================

static void pm_transpile_ast(PyMirTranspiler* mt, PyAstNode* root) {
    if (!root || root->node_type != PY_AST_NODE_MODULE) {
        log_error("py-mir: expected module node");
        return;
    }

    PyModuleNode* program = (PyModuleNode*)root;

    // Phase 1: collect functions
    pm_collect_functions(mt, root);
    log_debug("py-mir: collected %d functions", mt->func_count);

    // Phase 2: scan module-level variables
    pm_scan_module_vars(mt, root);

    // Phase 3: compile all functions first
    for (int i = 0; i < mt->func_count; i++) {
        pm_compile_function(mt, &mt->func_entries[i]);
    }

    // Phase 4: create py_main function
    MIR_type_t res_type = MIR_T_I64;
    MIR_var_t main_param = {MIR_T_I64, "ctx", 0};
    mt->current_func_item = MIR_new_func_arr(mt->ctx, "py_main", 1, &res_type, 1, &main_param);
    mt->current_func = mt->current_func_item->u.func;
    mt->in_main = true;

    pm_push_scope(mt);

    // transpile top-level statements
    MIR_reg_t last_result = pm_emit_null(mt);
    PyAstNode* s = program->body;
    while (s) {
        if (s->node_type == PY_AST_NODE_FUNCTION_DEF) {
            // skip — already compiled
            s = s->next;
            continue;
        }
        if (s->node_type == PY_AST_NODE_EXPRESSION_STATEMENT) {
            PyExpressionStatementNode* es = (PyExpressionStatementNode*)s;
            if (es->expression) {
                last_result = pm_transpile_expression(mt, es->expression);
            }
        } else {
            pm_transpile_statement(mt, s);
        }
        s = s->next;
    }

    // return last result
    pm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
        MIR_new_reg_op(mt->ctx, last_result)));

    MIR_finish_func(mt->ctx);
    pm_pop_scope(mt);
    mt->in_main = false;
}

// ============================================================================
// Entry point
// ============================================================================

Item transpile_py_to_mir(Runtime* runtime, const char* py_source, const char* filename) {
    log_debug("py-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");

    // create Python transpiler (parsing + AST building)
    PyTranspiler* tp = py_transpiler_create(runtime);
    if (!tp) {
        log_error("py-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // parse source
    if (!py_transpiler_parse(tp, py_source, strlen(py_source))) {
        log_error("py-mir: parse failed");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // build AST
    PyAstNode* ast = build_py_ast(tp, root);
    if (!ast) {
        log_error("py-mir: AST build failed");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // set up evaluation context
    EvalContext py_context;
    memset(&py_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = gc_nursery_create(0);
        }
    } else {
        py_context.nursery = gc_nursery_create(0);
        context = &py_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
    }

    _lambda_rt = (Context*)context;

    // create Input context for Python runtime
    Input* input = Input::create(context->pool);
    py_runtime_set_input(input);

    // init MIR context
    MIR_context_t ctx = jit_init(2);
    if (!ctx) {
        log_error("py-mir: MIR context init failed");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // allocate transpiler
    PyMirTranspiler* mt = (PyMirTranspiler*)malloc(sizeof(PyMirTranspiler));
    if (!mt) {
        log_error("py-mir: failed to allocate PyMirTranspiler");
        MIR_finish(ctx);
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    memset(mt, 0, sizeof(PyMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->import_cache = hashmap_new(sizeof(PyImportCacheEntry), 64, 0, 0,
        py_import_cache_hash, py_import_cache_cmp, NULL, NULL);
    mt->local_funcs = hashmap_new(sizeof(PyLocalFuncEntry), 32, 0, 0,
        py_local_func_hash, py_local_func_cmp, NULL, NULL);
    mt->var_scopes[0] = hashmap_new(sizeof(PyVarScopeEntry), 16, 0, 0,
        py_var_scope_hash, py_var_scope_cmp, NULL, NULL);
    mt->scope_depth = 0;
    mt->current_func_index = -1;

    // create module
    mt->module = MIR_new_module(ctx, "py_script");

    // transpile AST to MIR
    pm_transpile_ast(mt, ast);

    // finalize module
    MIR_finish_module(mt->ctx);

    // load module for linking
    MIR_load_module(mt->ctx, mt->module);

#ifndef NDEBUG
    // dump MIR for debugging
    FILE* mir_dump = fopen("temp/py_mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }
#endif

    // link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // find py_main
    typedef Item (*py_main_func_t)(Context*);
    py_main_func_t py_main = (py_main_func_t)find_func(ctx, "py_main");

    if (!py_main) {
        log_error("py-mir: failed to find py_main");
        hashmap_free(mt->import_cache);
        hashmap_free(mt->local_funcs);
        if (mt->module_consts) hashmap_free(mt->module_consts);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        free(mt);
        MIR_finish(ctx);
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // execute
    log_notice("py-mir: executing JIT compiled Python code");
    py_reset_module_vars();
    Item result = py_main((Context*)context);

    // cleanup
    hashmap_free(mt->import_cache);
    hashmap_free(mt->local_funcs);
    if (mt->module_consts) hashmap_free(mt->module_consts);
    for (int i = 0; i <= mt->scope_depth; i++) {
        if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
    }
    free(mt);

    MIR_finish(ctx);
    py_transpiler_destroy(tp);

    if (!reusing_context) {
        context = old_context;
    }

    return result;
}
