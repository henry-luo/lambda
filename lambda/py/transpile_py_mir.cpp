// transpile_py_mir.cpp - Direct MIR generation for Python
//
// Mirrors the JavaScript MIR transpiler architecture (transpile_js_mir.cpp).
// All Python values are boxed Items (MIR_T_I64). Runtime functions
// (py_add, py_subtract, etc.) take and return Items.

#include "py_transpiler.hpp"
#include "py_runtime.h"
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

// External reference to Lambda runtime context
extern "C" Context* _lambda_rt;
extern "C" {
    void* import_resolver(const char* name);
}

extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern "C" void py_reset_module_vars();

// Forward declarations for cross-language module interop
extern "C" Item js_property_get(Item object, Item key);
extern "C" int js_function_get_arity(Item fn_item);
extern "C" Item js_new_object();
extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" char* read_text_file(const char* filename);
extern "C" void js_runtime_set_input(void* input);

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
    int required_count;  // params without defaults
    int default_count;   // params with defaults
    // capture info for closures
    char captures[32][128];  // variable names captured (with _py_ prefix)
    bool capture_is_nonlocal[32]; // true if this capture was declared nonlocal
    int capture_count;
    int parent_index;    // index in func_entries of parent function (-1 if top-level)
    bool is_closure;     // true if this function captures variables
    // shared env for nonlocal children
    char shared_env_names[32][128]; // vars to put in shared env (with _py_ prefix)
    int shared_env_count;           // number of shared env slots
    bool has_nonlocal_children;     // true if any child uses nonlocal
    // global declarations
    char global_names[16][128];     // vars declared global (with _py_ prefix)
    int global_name_count;
    // *args variadic parameter
    bool has_star_args;             // true if function has *args parameter
    char star_args_name[128];       // name of *args parameter (with _py_ prefix)
    int star_args_index;            // index of *args in param list (regular params before it)
};

struct PyLambdaCollected {
    PyLambdaNode* node;
    char name[64];
    MIR_item_t func_item;
    int param_count;
    char captures[32][128];
    int capture_count;
    int parent_index;    // index in func_entries of enclosing function (-1 if top-level)
    bool is_closure;
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
    // Global variable name → module var index mapping
    char global_var_names[64][128];  // variable names (with _py_ prefix)
    int global_var_indices[64];      // module var index for each
    int global_var_count;

    // Module-level constants
    struct hashmap* module_consts;

    bool in_main;

    // Lambda counter
    int lambda_count;

    // Collected lambdas
    PyLambdaCollected lambda_entries[64];

    // Scope env for closures
    MIR_reg_t scope_env_reg;
    int scope_env_slot_count;
    int current_func_index;

    // Try/except stack
    int try_depth;
    MIR_label_t try_handler_labels[16];

    // Source filename for relative import resolution
    const char* filename;

    // Runtime pointer for cross-language module loading
    Runtime* runtime;
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

static MIR_reg_t pm_call_5(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5) {
    MIR_var_t args[5] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}, {a5t, "e", 0}};
    PyImportCacheEntry* ie = pm_ensure_import(mt, fn_name, ret_type, 5, args, 1);
    MIR_reg_t res = pm_new_reg(mt, fn_name, ret_type);
    pm_emit(mt, MIR_new_call_insn(mt->ctx, 8,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4, a5));
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

static void pm_set_env_var(PyMirTranspiler* mt, const char* name, MIR_reg_t env_reg, int slot) {
    PyVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.from_env = true;
    entry.var.env_reg = env_reg;
    entry.var.env_slot = slot;
    entry.var.mir_type = MIR_T_I64;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static MIR_reg_t pm_load_env_slot(PyMirTranspiler* mt, MIR_reg_t env_reg, int slot) {
    MIR_reg_t val = pm_new_reg(mt, "eload", MIR_T_I64);
    MIR_reg_t addr = pm_new_reg(mt, "ea", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_reg_op(mt->ctx, env_reg),
        MIR_new_int_op(mt->ctx, slot * 8)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, val),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1)));
    return val;
}

static void pm_store_env_slot(PyMirTranspiler* mt, MIR_reg_t env_reg, int slot, MIR_reg_t val_reg) {
    MIR_reg_t addr = pm_new_reg(mt, "ea", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_reg_op(mt->ctx, env_reg),
        MIR_new_int_op(mt->ctx, slot * 8)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
        MIR_new_reg_op(mt->ctx, val_reg)));
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

// forward declarations — global variable routing
static bool pm_is_global_in_current_func(PyMirTranspiler* mt, const char* vname);
static int pm_get_global_var_index(PyMirTranspiler* mt, const char* vname);

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

    // check if variable is declared global in current function
    if (pm_is_global_in_current_func(mt, vname)) {
        int idx = pm_get_global_var_index(mt, vname);
        if (idx >= 0) {
            return pm_call_1(mt, "py_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx));
        }
    }

    PyMirVarEntry* var = pm_find_var(mt, vname);
    if (var) {
        if (var->from_env) return pm_load_env_slot(mt, var->env_reg, var->env_slot);
        return var->reg;
    }

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
            // check for sep= and end= keyword arguments
            MIR_reg_t sep_reg = pm_emit_null(mt);
            MIR_reg_t end_reg = pm_emit_null(mt);
            bool has_kwargs = false;
            {
                PyAstNode* ka = call->arguments;
                while (ka) {
                    if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                        if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "sep", 3) == 0) {
                            sep_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        } else if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "end", 3) == 0) {
                            end_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        }
                    }
                    ka = ka->next;
                }
            }

            // collect positional args on stack
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

            if (has_kwargs) {
                return pm_call_4(mt, "py_print_ex", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, argc),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sep_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, end_reg));
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
            // check for key= and reverse= kwargs
            MIR_reg_t key_reg = pm_emit_null(mt);
            MIR_reg_t rev_reg = pm_emit_null(mt);
            bool has_kwargs = false;
            {
                PyAstNode* ka = call->arguments;
                while (ka) {
                    if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                        if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "key", 3) == 0) {
                            key_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        } else if (kw->key && kw->key->len == 7 && strncmp(kw->key->chars, "reverse", 7) == 0) {
                            rev_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        }
                    }
                    ka = ka->next;
                }
            }
            if (has_kwargs) {
                return pm_call_3(mt, "py_builtin_sorted_ex", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, rev_reg));
            }
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

        // map(func, iterable)
        if (name_len == 3 && strncmp(name, "map", 3) == 0 && call->arg_count >= 2) {
            MIR_reg_t func_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t iter_arg = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_map", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, func_arg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, iter_arg));
        }

        // filter(func, iterable)
        if (name_len == 6 && strncmp(name, "filter", 6) == 0 && call->arg_count >= 2) {
            MIR_reg_t func_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t iter_arg = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_filter", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, func_arg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, iter_arg));
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

        // round()
        if (name_len == 5 && strncmp(name, "round", 5) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t ndigits = call->arguments->next ?
                pm_transpile_expression(mt, call->arguments->next) : pm_emit_null(mt);
            return pm_call_2(mt, "py_builtin_round", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ndigits));
        }

        // all()
        if (name_len == 3 && strncmp(name, "all", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_all", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // any()
        if (name_len == 3 && strncmp(name, "any", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_any", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // bin(), oct(), hex()
        if (name_len == 3 && strncmp(name, "bin", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_bin", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "oct", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_oct", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "hex", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_hex", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // divmod()
        if (name_len == 6 && strncmp(name, "divmod", 6) == 0 && call->arg_count >= 2) {
            MIR_reg_t a = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t b = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_divmod", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, b));
        }

        // pow()
        if (name_len == 3 && strncmp(name, "pow", 3) == 0 && call->arg_count >= 2) {
            MIR_reg_t base = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t exp = pm_transpile_expression(mt, call->arguments->next);
            MIR_reg_t mod = (call->arguments->next->next) ?
                pm_transpile_expression(mt, call->arguments->next->next) : pm_emit_null(mt);
            return pm_call_3(mt, "py_builtin_pow", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, base),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, exp),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mod));
        }

        // callable()
        if (name_len == 8 && strncmp(name, "callable", 8) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_callable", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // isinstance()
        if (name_len == 10 && strncmp(name, "isinstance", 10) == 0 && call->arg_count >= 2) {
            MIR_reg_t obj = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t cls = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_isinstance", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls));
        }

        // hash(), id()
        if (name_len == 4 && strncmp(name, "hash", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_hash", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 2 && strncmp(name, "id", 2) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // set(), tuple(), dict()
        if (name_len == 3 && strncmp(name, "set", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 5 && strncmp(name, "tuple", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_tuple", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }
        if (name_len == 4 && strncmp(name, "dict", 4) == 0) {
            // dict() with no args → empty dict
            return pm_call_0(mt, "py_dict_new", MIR_T_I64);
        }

        // zip()
        if (name_len == 3 && strncmp(name, "zip", 3) == 0) {
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[16];
            while (arg && argc < 16) {
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            MIR_reg_t args_ptr = pm_new_reg(mt, "zargs", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc * 8)));
            for (int i = 0; i < argc; i++) {
                MIR_reg_t addr = pm_new_reg(mt, "za", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, args_ptr),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, arg_regs[i])));
            }
            return pm_call_2(mt, "py_builtin_zip", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
        }

        // iter()
        if (name_len == 4 && strncmp(name, "iter", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_get_iterator", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // next()
        if (name_len == 4 && strncmp(name, "next", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_iterator_next", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
        }

        // open()
        if (name_len == 4 && strncmp(name, "open", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t path_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t mode_arg = (call->arguments->next) ?
                pm_transpile_expression(mt, call->arguments->next) :
                pm_box_string_literal(mt, "r", 1);
            return pm_call_2(mt, "py_builtin_open", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, path_arg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mode_arg));
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

    // direct call to known user function — resolve kwargs at compile time
    if (call->function && call->function->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
        // look up in func_entries
        PyFuncCollected* target_fc = NULL;
        for (int i = 0; i < mt->func_count; i++) {
            if ((int)fn_id->name->len == (int)strlen(mt->func_entries[i].name) &&
                strncmp(fn_id->name->chars, mt->func_entries[i].name, fn_id->name->len) == 0) {
                target_fc = &mt->func_entries[i];
                break;
            }
        }
        if (target_fc && target_fc->func_item) {
            int total_params = target_fc->param_count;
            // build ordered argument array: init all slots to null
            MIR_reg_t ordered_args[16];
            bool arg_filled[16];
            for (int i = 0; i < total_params && i < 16; i++) {
                ordered_args[i] = pm_emit_null(mt);
                arg_filled[i] = false;
            }

            // first pass: fill positional arguments
            int pos = 0;
            PyAstNode* a = call->arguments;
            while (a && pos < total_params) {
                if (a->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    a = a->next;
                    continue;
                }
                ordered_args[pos] = pm_transpile_expression(mt, a);
                arg_filled[pos] = true;
                pos++;
                a = a->next;
            }

            // second pass: fill keyword arguments by matching param names
            a = call->arguments;
            while (a) {
                if (a->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    PyKeywordArgNode* kw = (PyKeywordArgNode*)a;
                    if (kw->key) {
                        // find matching parameter by name
                        PyAstNode* pp = target_fc->node->params;
                        int pi = 0;
                        while (pp && pi < total_params) {
                            String* pname = NULL;
                            if (pp->node_type == PY_AST_NODE_PARAMETER ||
                                pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                                pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                                pname = ((PyParamNode*)pp)->name;
                            }
                            if (pname && pname->len == kw->key->len &&
                                strncmp(pname->chars, kw->key->chars, pname->len) == 0) {
                                ordered_args[pi] = pm_transpile_expression(mt, kw->value);
                                arg_filled[pi] = true;
                                break;
                            }
                            pp = pp->next;
                            pi++;
                        }
                    }
                }
                a = a->next;
            }

            // emit direct call to local function with ordered args
            char fn_name[140];
            snprintf(fn_name, sizeof(fn_name), "_%s", target_fc->name);

            // determine total MIR params (regular + optional varargs pair)
            int mir_total = total_params + (target_fc->has_star_args ? 2 : 0);

            // build proto for the call
            MIR_var_t pvars[20];
            for (int i = 0; i < mir_total && i < 20; i++) {
                char* pb = (char*)alloca(8);
                snprintf(pb, 8, "p%d", i);
                pvars[i] = {MIR_T_I64, pb, 0};
            }

            // check import cache for proto (avoid duplicate proto names)
            char proto_key[148];
            snprintf(proto_key, sizeof(proto_key), "%s_local", fn_name);
            PyImportCacheEntry key;
            memset(&key, 0, sizeof(key));
            snprintf(key.name, sizeof(key.name), "%s", proto_key);
            PyImportCacheEntry* cached = (PyImportCacheEntry*)hashmap_get(mt->import_cache, &key);
            MIR_item_t proto;
            if (cached) {
                proto = cached->proto;
            } else {
                char proto_name[160];
                snprintf(proto_name, sizeof(proto_name), "%s_lp", fn_name);
                MIR_type_t res_types[1] = { MIR_T_I64 };
                proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, res_types, mir_total, pvars);
                PyImportCacheEntry new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
                snprintf(new_entry.name, sizeof(new_entry.name), "%s", proto_key);
                new_entry.proto = proto;
                new_entry.import = NULL;
                hashmap_set(mt->import_cache, &new_entry);
            }

            MIR_reg_t res = pm_new_reg(mt, "dcall", MIR_T_I64);

            if (target_fc->has_star_args) {
                // collect all positional args beyond regular params as excess
                int excess_count = 0;
                MIR_reg_t excess_regs[16];
                int positional = 0;
                PyAstNode* pa = call->arguments;
                while (pa) {
                    if (pa->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) { pa = pa->next; continue; }
                    positional++;
                    if (positional > total_params) {
                        // excess positional arg → *args
                        excess_regs[excess_count++] = pm_transpile_expression(mt, pa);
                    }
                    pa = pa->next;
                }

                // build stack array for excess args
                MIR_reg_t va_ptr = pm_new_reg(mt, "vaptr", MIR_T_I64);
                if (excess_count > 0) {
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
                        MIR_new_reg_op(mt->ctx, va_ptr),
                        MIR_new_int_op(mt->ctx, excess_count * 8)));
                    for (int i = 0; i < excess_count; i++) {
                        MIR_reg_t addr = pm_new_reg(mt, "va", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                            MIR_new_reg_op(mt->ctx, addr),
                            MIR_new_reg_op(mt->ctx, va_ptr),
                            MIR_new_int_op(mt->ctx, i * 8)));
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                            MIR_new_reg_op(mt->ctx, excess_regs[i])));
                    }
                } else {
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, va_ptr),
                        MIR_new_int_op(mt->ctx, 0)));
                }

                int nops = 3 + mir_total;
                MIR_op_t ops[23];
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_ref_op(mt->ctx, target_fc->func_item);
                ops[2] = MIR_new_reg_op(mt->ctx, res);
                for (int i = 0; i < total_params && i < 16; i++) {
                    ops[3 + i] = MIR_new_reg_op(mt->ctx, ordered_args[i]);
                }
                ops[3 + total_params] = MIR_new_reg_op(mt->ctx, va_ptr);
                ops[3 + total_params + 1] = MIR_new_int_op(mt->ctx, excess_count);
                pm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            } else {
                int nops = 3 + total_params;
                MIR_op_t ops[19]; // proto + func_ref + result + up to 16 args
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_ref_op(mt->ctx, target_fc->func_item);
                ops[2] = MIR_new_reg_op(mt->ctx, res);
                for (int i = 0; i < total_params && i < 16; i++) {
                    ops[3 + i] = MIR_new_reg_op(mt->ctx, ordered_args[i]);
                }
                pm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            }
            return res;
        }
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

    // check if index is a slice node
    if (sub->index && sub->index->node_type == PY_AST_NODE_SLICE) {
        PySliceNode* slice = (PySliceNode*)sub->index;
        MIR_reg_t start = slice->start ? pm_transpile_expression(mt, slice->start) : pm_emit_null(mt);
        MIR_reg_t stop = slice->stop ? pm_transpile_expression(mt, slice->stop) : pm_emit_null(mt);
        MIR_reg_t step = slice->step ? pm_transpile_expression(mt, slice->step) : pm_emit_null(mt);
        return pm_call_4(mt, "py_slice_get", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, start),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, stop),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step));
    }

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
        } else if (part->node_type == PY_AST_NODE_FSTRING_EXPR) {
            // f-string expression with format spec: f"{expr:spec}"
            PyFStringExprNode* fse = (PyFStringExprNode*)part;
            MIR_reg_t expr_val = pm_transpile_expression(mt, fse->expression);
            MIR_reg_t spec_val = pm_box_string_literal(mt, fse->format_spec->chars, fse->format_spec->len);
            val = pm_call_2(mt, "py_format_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, expr_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, spec_val));
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
// Comprehensions
// ============================================================================

// helper: assign a loop element to the target variable(s) in comprehension
static void pm_assign_comp_target(PyMirTranspiler* mt, PyAstNode* target, MIR_reg_t item) {
    if (target->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)target;
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
    } else if (target->node_type == PY_AST_NODE_TUPLE ||
               target->node_type == PY_AST_NODE_TUPLE_UNPACK) {
        PySequenceNode* tup = (PySequenceNode*)target;
        PyAstNode* elem = tup->elements;
        int i = 0;
        while (elem) {
            MIR_reg_t sub_item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, item),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, pm_box_int_const(mt, i)));
            pm_assign_comp_target(mt, elem, sub_item);
            elem = elem->next;
            i++;
        }
    }
}

static MIR_reg_t pm_transpile_list_comprehension(PyMirTranspiler* mt, PyComprehensionNode* comp) {
    // create result list
    MIR_reg_t result = pm_call_1(mt, "py_list_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, comp->iter);
    MIR_reg_t length = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));

    // loop index
    MIR_reg_t idx = pm_new_reg(mt, "comp_idx", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, (int64_t)(PY_ITEM_INT_TAG | 0))));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);

    pm_emit_label(mt, l_start);

    // check idx < length
    MIR_reg_t cmp = pm_call_2(mt, "py_lt", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, length));
    MIR_reg_t cmp_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cmp));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp_truthy)));

    // get current item and assign to target
    MIR_reg_t item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    pm_assign_comp_target(mt, comp->target, item);

    // evaluate conditions (if any)
    MIR_label_t l_skip = NULL;
    if (comp->conditions) {
        l_skip = pm_new_label(mt);
        PyAstNode* cond = comp->conditions;
        while (cond) {
            MIR_reg_t cond_val = pm_transpile_expression(mt, cond);
            MIR_reg_t cond_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cond_val));
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_skip),
                MIR_new_reg_op(mt->ctx, cond_truthy)));
            cond = cond->next;
        }
    }

    // evaluate element expression and append to result
    MIR_reg_t elem = pm_transpile_expression(mt, comp->element);
    pm_call_2(mt, "py_list_append", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, elem));

    if (l_skip) pm_emit_label(mt, l_skip);
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
    return result;
}

static MIR_reg_t pm_transpile_dict_comprehension(PyMirTranspiler* mt, PyComprehensionNode* comp) {
    // create result dict
    MIR_reg_t result = pm_call_0(mt, "py_dict_new", MIR_T_I64);

    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, comp->iter);
    MIR_reg_t length = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));

    MIR_reg_t idx = pm_new_reg(mt, "comp_idx", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, (int64_t)(PY_ITEM_INT_TAG | 0))));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);

    pm_emit_label(mt, l_start);

    MIR_reg_t cmp = pm_call_2(mt, "py_lt", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, length));
    MIR_reg_t cmp_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cmp));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp_truthy)));

    MIR_reg_t item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    pm_assign_comp_target(mt, comp->target, item);

    // evaluate conditions
    MIR_label_t l_skip = NULL;
    if (comp->conditions) {
        l_skip = pm_new_label(mt);
        PyAstNode* cond = comp->conditions;
        while (cond) {
            MIR_reg_t cond_val = pm_transpile_expression(mt, cond);
            MIR_reg_t cond_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cond_val));
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_skip),
                MIR_new_reg_op(mt->ctx, cond_truthy)));
            cond = cond->next;
        }
    }

    // for dict comprehension, element is a pair node with key and value
    if (comp->element && comp->element->node_type == PY_AST_NODE_PAIR) {
        PyPairNode* pair = (PyPairNode*)comp->element;
        MIR_reg_t key_val = pm_transpile_expression(mt, pair->key);
        MIR_reg_t val_val = pm_transpile_expression(mt, pair->value);
        pm_call_3(mt, "py_dict_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val_val));
    }

    if (l_skip) pm_emit_label(mt, l_skip);
    pm_emit_label(mt, l_continue);

    MIR_reg_t one = pm_box_int_const(mt, 1);
    MIR_reg_t new_idx = pm_call_2(mt, "py_add", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, new_idx)));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start)));

    pm_emit_label(mt, l_end);
    return result;
}

// ============================================================================
// Lambda expressions
// ============================================================================

static MIR_reg_t pm_transpile_lambda(PyMirTranspiler* mt, PyLambdaNode* lam) {
    // find the pre-compiled lambda entry by matching node pointer
    for (int i = 0; i < mt->lambda_count; i++) {
        if (mt->lambda_entries[i].node == lam) {
            PyLambdaCollected* lc = &mt->lambda_entries[i];
            MIR_reg_t fptr = pm_new_reg(mt, "lam_ptr", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, fptr),
                MIR_new_ref_op(mt->ctx, lc->func_item)));

            if (lc->is_closure) {
                // allocate env and store captured vars
                MIR_reg_t env = pm_call_1(mt, "py_alloc_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, lc->capture_count));
                for (int ci = 0; ci < lc->capture_count; ci++) {
                    PyMirVarEntry* cvar = pm_find_var(mt, lc->captures[ci]);
                    if (cvar) {
                        MIR_reg_t addr = pm_new_reg(mt, "lea", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                            MIR_new_reg_op(mt->ctx, addr),
                            MIR_new_reg_op(mt->ctx, env),
                            MIR_new_int_op(mt->ctx, ci * 8)));
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
                            MIR_new_reg_op(mt->ctx, cvar->reg)));
                    }
                }
                return pm_call_4(mt, "py_new_closure", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fptr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, lc->param_count),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, lc->capture_count));
            }

            return pm_call_2(mt, "py_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, fptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, lc->param_count));
        }
    }
    log_error("py-mir: lambda not found in pre-compiled entries");
    return pm_emit_null(mt);
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
    case PY_AST_NODE_LIST_COMPREHENSION:
        return pm_transpile_list_comprehension(mt, (PyComprehensionNode*)expr);
    case PY_AST_NODE_DICT_COMPREHENSION:
        return pm_transpile_dict_comprehension(mt, (PyComprehensionNode*)expr);
    case PY_AST_NODE_SET_COMPREHENSION:
        return pm_transpile_list_comprehension(mt, (PyComprehensionNode*)expr); // sets as lists
    case PY_AST_NODE_LAMBDA:
        return pm_transpile_lambda(mt, (PyLambdaNode*)expr);
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

            // check if variable is declared global in current function
            if (pm_is_global_in_current_func(mt, vname)) {
                int idx = pm_get_global_var_index(mt, vname);
                if (idx >= 0) {
                    pm_call_void_2(mt, "py_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    target = target->next;
                    continue;
                }
            }

            PyMirVarEntry* var = pm_find_var(mt, vname);
            if (var) {
                if (var->from_env) {
                    pm_store_env_slot(mt, var->env_reg, var->env_slot, val);
                } else {
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var->reg),
                        MIR_new_reg_op(mt->ctx, val)));
                }
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
            if (sub->index && sub->index->node_type == PY_AST_NODE_SLICE) {
                // slice assignment: obj[start:stop:step] = val
                PySliceNode* slice = (PySliceNode*)sub->index;
                MIR_reg_t start = slice->start ? pm_transpile_expression(mt, slice->start) : pm_emit_null(mt);
                MIR_reg_t stop  = slice->stop  ? pm_transpile_expression(mt, slice->stop)  : pm_emit_null(mt);
                MIR_reg_t step  = slice->step  ? pm_transpile_expression(mt, slice->step)  : pm_emit_null(mt);
                pm_call_5(mt, "py_slice_set", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, start),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, stop),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, step),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            } else {
                MIR_reg_t key = pm_transpile_expression(mt, sub->index);
                pm_call_3(mt, "py_subscript_set", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
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

        // check if variable is declared global in current function
        if (pm_is_global_in_current_func(mt, vname)) {
            int idx = pm_get_global_var_index(mt, vname);
            if (idx >= 0) {
                pm_call_void_2(mt, "py_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                return;
            }
        }

        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            if (var->from_env) {
                pm_store_env_slot(mt, var->env_reg, var->env_slot, val);
            } else {
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            }
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
    case PY_AST_NODE_FUNCTION_DEF: {
        // for closures: emit env allocation + closure creation
        // for non-closures in nested context: emit function reference
        PyFunctionDefNode* fdef = (PyFunctionDefNode*)stmt;
        PyFuncCollected* fc_target = NULL;
        for (int fi = 0; fi < mt->func_count; fi++) {
            if (mt->func_entries[fi].node == fdef) {
                fc_target = &mt->func_entries[fi];
                break;
            }
        }
        if (fc_target && fc_target->is_closure && fc_target->func_item) {
            // check if ALL captures are nonlocal — if so, pass parent's shared env directly
            bool all_nonlocal = true;
            for (int ci = 0; ci < fc_target->capture_count; ci++) {
                if (!fc_target->capture_is_nonlocal[ci]) { all_nonlocal = false; break; }
            }

            MIR_reg_t env;
            if (all_nonlocal && mt->scope_env_reg != 0) {
                // pass the parent's shared env directly — no new alloc needed
                env = mt->scope_env_reg;
            } else {
                // allocate env with captured values
                env = pm_call_1(mt, "py_alloc_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, fc_target->capture_count));
                // store each captured variable into env slots
                for (int ci = 0; ci < fc_target->capture_count; ci++) {
                    PyMirVarEntry* cvar = pm_find_var(mt, fc_target->captures[ci]);
                    if (cvar) {
                        if (cvar->from_env) {
                            // load from parent's env first, then store into child env
                            MIR_reg_t loaded = pm_load_env_slot(mt, cvar->env_reg, cvar->env_slot);
                            pm_store_env_slot(mt, env, ci, loaded);
                        } else {
                            pm_store_env_slot(mt, env, ci, cvar->reg);
                        }
                    }
                }
            }
            // create closure: py_new_closure(func_ptr, param_count, env, env_size)
            MIR_reg_t fptr = pm_new_reg(mt, "cfp", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, fptr),
                MIR_new_ref_op(mt->ctx, fc_target->func_item)));
            MIR_reg_t closure = pm_call_4(mt, "py_new_closure", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, fptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, fc_target->param_count),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                MIR_T_I64, MIR_new_int_op(mt->ctx, fc_target->capture_count));
            // assign to local variable
            char vname[128];
            snprintf(vname, sizeof(vname), "_py_%s", fc_target->name);
            pm_set_var(mt, vname, closure);
        }
        // non-closures are already registered in local_funcs by pm_compile_function
        break;
    }
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
    case PY_AST_NODE_RAISE: {
        PyRaiseNode* rn = (PyRaiseNode*)stmt;
        if (rn->exception) {
            MIR_reg_t exc = pm_transpile_expression(mt, rn->exception);
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc));
        } else {
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL));
        }
        // if inside a try block, jump to handler; otherwise return from function
        if (mt->try_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->try_handler_labels[mt->try_depth - 1])));
        } else {
            pm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
        }
        break;
    }
    case PY_AST_NODE_TRY: {
        PyTryNode* tn = (PyTryNode*)stmt;
        MIR_label_t l_handler = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);
        MIR_label_t l_finally = tn->finally_body ? pm_new_label(mt) : l_end;

        // push try handler label so raise can jump here
        if (mt->try_depth < 16) {
            mt->try_handler_labels[mt->try_depth] = l_handler;
            mt->try_depth++;
        }

        // emit try body — after each statement, check for exception
        {
            PyAstNode* body = tn->body;
            if (body && body->node_type == PY_AST_NODE_BLOCK) {
                PyAstNode* s = ((PyBlockNode*)body)->statements;
                while (s) {
                    pm_transpile_statement(mt, s);
                    // check exception after each statement
                    MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                    MIR_reg_t chk_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, chk));
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, l_handler),
                        MIR_new_reg_op(mt->ctx, chk_t)));
                    s = s->next;
                }
            } else {
                pm_transpile_block(mt, body);
                // single check at the end
                MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                MIR_reg_t chk_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, chk));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, l_handler),
                    MIR_new_reg_op(mt->ctx, chk_t)));
            }
        }

        // pop try depth (we either fell through normally or will jump to handler)
        if (mt->try_depth > 0) mt->try_depth--;

        // no exception → jump to finally/end
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, l_finally)));

        // handler label
        pm_emit_label(mt, l_handler);

        if (tn->handlers) {
            // clear exception
            MIR_reg_t exc_val = pm_call_0(mt, "py_clear_exception", MIR_T_I64);

            // handle each except clause
            MIR_label_t l_no_match = pm_new_label(mt); // fallback: re-raise if no handler matches
            PyAstNode* handler = tn->handlers;
            while (handler) {
                if (handler->node_type == PY_AST_NODE_EXCEPT) {
                    PyExceptNode* en = (PyExceptNode*)handler;
                    MIR_label_t l_next_handler = pm_new_label(mt);

                    // if except has a type filter (e.g., except ValueError),
                    // check if exception type matches
                    if (en->type) {
                        // "except Exception" catches all — skip type check
                        bool catch_all = false;
                        if (en->type->node_type == PY_AST_NODE_IDENTIFIER) {
                            PyIdentifierNode* tid = (PyIdentifierNode*)en->type;
                            if (tid->name->len == 9 && strncmp(tid->name->chars, "Exception", 9) == 0) {
                                catch_all = true;
                            }
                        }

                        if (!catch_all) {
                            // get the exception's type string
                            MIR_reg_t exc_type = pm_call_1(mt, "py_exception_get_type", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));

                            // get the expected type name as a string
                            MIR_reg_t expected_type;
                            if (en->type->node_type == PY_AST_NODE_IDENTIFIER) {
                                PyIdentifierNode* tid = (PyIdentifierNode*)en->type;
                                expected_type = pm_box_string_literal(mt, tid->name->chars, tid->name->len);
                            } else {
                                expected_type = pm_transpile_expression(mt, en->type);
                            }

                            // compare: py_eq(exc_type, expected_type)
                            MIR_reg_t match = pm_call_2(mt, "py_eq", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_type),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, expected_type));
                            MIR_reg_t match_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, match));
                            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                                MIR_new_label_op(mt->ctx, l_next_handler),
                                MIR_new_reg_op(mt->ctx, match_truthy)));
                        }
                    }
                    // bind exception to name if present
                    if (en->name) {
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_py_%.*s", (int)en->name->len, en->name->chars);
                        PyMirVarEntry* var = pm_find_var(mt, vname);
                        if (var) {
                            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, var->reg),
                                MIR_new_reg_op(mt->ctx, exc_val)));
                        } else {
                            MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
                            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, reg),
                                MIR_new_reg_op(mt->ctx, exc_val)));
                            pm_set_var(mt, vname, reg);
                        }
                    }

                    // emit handler body
                    pm_transpile_block(mt, en->body);
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                        MIR_new_label_op(mt->ctx, l_finally)));

                    pm_emit_label(mt, l_next_handler);
                }
                handler = handler->next;
            }

            // no handler matched → re-raise
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, exc_val));
            pm_emit_label(mt, l_no_match);
        }

        // finally
        if (tn->finally_body) {
            pm_emit_label(mt, l_finally);
            pm_transpile_block(mt, tn->finally_body);
        }

        pm_emit_label(mt, l_end);
        break;
    }
    case PY_AST_NODE_IMPORT:
        // bare "import module" — no-op for now (module-as-namespace not yet supported)
        log_debug("py-mir: bare import statement (not yet supported for cross-lang)");
        break;
    case PY_AST_NODE_IMPORT_FROM: {
        // from ./module import name1, name2
        PyImportNode* imp = (PyImportNode*)stmt;
        if (!imp->module_name) break;

        const char* mod_name = imp->module_name->chars;
        int mod_len = (int)imp->module_name->len;

        // resolve module path relative to the source file's directory
        StrBuf* path_buf = strbuf_new();
        if (mod_name[0] == '.') {
            // relative import
            const char* dir = mt->filename;
            if (dir) {
                // extract directory from filename
                const char* last_slash = strrchr(dir, '/');
                if (last_slash) {
                    strbuf_append_format(path_buf, "%.*s/", (int)(last_slash - dir), dir);
                } else {
                    strbuf_append_str(path_buf, "./");
                }
            } else {
                strbuf_append_str(path_buf, "./");
            }
            // append the module path (skip leading dot, convert dots to slashes)
            for (int i = 1; i < mod_len; i++) {
                strbuf_append_char(path_buf, mod_name[i] == '.' ? '/' : mod_name[i]);
            }
        } else {
            // absolute module name — convert dots to slashes
            strbuf_append_str(path_buf, "./");
            for (int i = 0; i < mod_len; i++) {
                strbuf_append_char(path_buf, mod_name[i] == '.' ? '/' : mod_name[i]);
            }
        }

        // try loading as .py, .js, .ls
        Item ns = ItemNull;
        const char* base_path = path_buf->str;
        int base_len = (int)path_buf->length;

        // try .py first
        StrBuf* try_buf = strbuf_new();
        strbuf_append_format(try_buf, "%s.py", base_path);
        ns = load_py_module(mt->runtime, try_buf->str);

        if (ns.item == ItemNull.item) {
            // try .js
            strbuf_reset(try_buf);
            strbuf_append_format(try_buf, "%s.js", base_path);
            ns = load_js_module(mt->runtime, try_buf->str);
        }

        if (ns.item == ItemNull.item) {
            // try .ls — load Lambda script and build namespace
            strbuf_reset(try_buf);
            strbuf_append_format(try_buf, "%s.ls", base_path);
            Script* script = load_script(mt->runtime, try_buf->str, NULL, true);
            if (script && script->ast_root) {
                ns = module_build_lambda_namespace(script);
            }
        }

        strbuf_free(try_buf);
        strbuf_free(path_buf);

        if (ns.item == ItemNull.item) {
            log_error("py-mir: failed to load module '%.*s'", mod_len, mod_name);
            break;
        }

        // for each imported name, extract from namespace and store as variable
        PyAstNode* name_node = imp->names;
        while (name_node) {
            if (name_node->node_type == PY_AST_NODE_IMPORT) {
                PyImportNode* name_imp = (PyImportNode*)name_node;
                if (name_imp->module_name) {
                    const char* sym_name = name_imp->module_name->chars;
                    int sym_len = (int)name_imp->module_name->len;

                    // get function from namespace
                    Item key = {.item = s2it(heap_create_name(sym_name, sym_len))};
                    Item value = js_property_get(ns, key);

                    if (value.item != ItemNull.item) {
                        // use alias if provided, otherwise original name
                        const char* local_name = sym_name;
                        int local_len = sym_len;
                        if (name_imp->alias) {
                            local_name = name_imp->alias->chars;
                            local_len = (int)name_imp->alias->len;
                        }

                        // store as a variable in current scope with _py_ prefix
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_py_%.*s", local_len, local_name);
                        MIR_reg_t val_reg = pm_new_reg(mt, "imp", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, val_reg),
                            MIR_new_int_op(mt->ctx, (int64_t)value.item)));
                        pm_set_var(mt, vname, val_reg);
                        log_debug("py-mir: imported '%.*s' as '%s'", sym_len, sym_name, vname);
                    } else {
                        log_error("py-mir: name '%.*s' not found in module '%.*s'",
                            sym_len, sym_name, mod_len, mod_name);
                    }
                }
            }
            name_node = name_node->next;
        }
        break;
    }
    default:
        log_debug("py-mir: unsupported statement type %d", stmt->node_type);
        break;
    }
}

// ============================================================================
// Function collection and compilation
// ============================================================================

static void pm_collect_functions_r(PyMirTranspiler* mt, PyAstNode* node, int parent_index) {
    if (!node) return;

    switch (node->node_type) {
    case PY_AST_NODE_MODULE: {
        PyModuleNode* mod = (PyModuleNode*)node;
        PyAstNode* s = mod->body;
        while (s) { pm_collect_functions_r(mt, s, parent_index); s = s->next; }
        break;
    }
    case PY_AST_NODE_BLOCK: {
        PyBlockNode* blk = (PyBlockNode*)node;
        PyAstNode* s = blk->statements;
        while (s) { pm_collect_functions_r(mt, s, parent_index); s = s->next; }
        break;
    }
    case PY_AST_NODE_FUNCTION_DEF: {
        PyFunctionDefNode* fn = (PyFunctionDefNode*)node;
        if (mt->func_count >= 128) break;

        int my_index = mt->func_count;
        PyFuncCollected* fc = &mt->func_entries[my_index];
        memset(fc, 0, sizeof(PyFuncCollected));
        fc->node = fn;
        fc->parent_index = parent_index;
        snprintf(fc->name, sizeof(fc->name), "%.*s", (int)fn->name->len, fn->name->chars);

        // count params, detect *args
        int pc = 0;
        int dc = 0;
        PyAstNode* p = fn->params;
        while (p) {
            if (p->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER) {
                // *args parameter — don't count as regular param
                PyParamNode* sp = (PyParamNode*)p;
                fc->has_star_args = true;
                fc->star_args_index = pc;
                if (sp->name) {
                    snprintf(fc->star_args_name, sizeof(fc->star_args_name),
                        "_py_%.*s", (int)sp->name->len, sp->name->chars);
                } else {
                    snprintf(fc->star_args_name, sizeof(fc->star_args_name), "_py_args");
                }
                p = p->next;
                continue;
            }
            if (p->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                // **kwargs — skip for now (not counted as regular param)
                p = p->next;
                continue;
            }
            if (p->node_type == PY_AST_NODE_DEFAULT_PARAMETER) dc++;
            pc++;
            p = p->next;
        }
        fc->param_count = pc;
        fc->required_count = pc - dc;
        fc->default_count = dc;

        mt->func_count++;

        // recurse into body for nested functions — they are children of this func
        pm_collect_functions_r(mt, fn->body, my_index);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* ifn = (PyIfNode*)node;
        pm_collect_functions_r(mt, ifn->body, parent_index);
        PyAstNode* elif = ifn->elif_clauses;
        while (elif) { pm_collect_functions_r(mt, elif, parent_index); elif = elif->next; }
        pm_collect_functions_r(mt, ifn->else_body, parent_index);
        break;
    }
    case PY_AST_NODE_ELIF: {
        PyIfNode* elif = (PyIfNode*)node;
        pm_collect_functions_r(mt, elif->body, parent_index);
        break;
    }
    case PY_AST_NODE_WHILE: {
        PyWhileNode* wh = (PyWhileNode*)node;
        pm_collect_functions_r(mt, wh->body, parent_index);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* fn = (PyForNode*)node;
        pm_collect_functions_r(mt, fn->body, parent_index);
        break;
    }
    default:
        break;
    }
}

static void pm_collect_functions(PyMirTranspiler* mt, PyAstNode* node) {
    pm_collect_functions_r(mt, node, -1);
}

// ============================================================================
// Capture analysis for closures
// ============================================================================

// collect all identifiers referenced in an AST subtree (not descending into nested functions)
static void pm_collect_referenced_ids(PyAstNode* node, char refs[][128], int* ref_count, int max_refs) {
    if (!node || *ref_count >= max_refs) return;

    switch (node->node_type) {
    case PY_AST_NODE_IDENTIFIER: {
        PyIdentifierNode* id = (PyIdentifierNode*)node;
        char name[128];
        snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
        // skip builtins
        if (strcmp(name, "_py_None") == 0 || strcmp(name, "_py_True") == 0 ||
            strcmp(name, "_py_False") == 0) break;
        // check if already in refs
        for (int i = 0; i < *ref_count; i++) {
            if (strcmp(refs[i], name) == 0) return;
        }
        if (*ref_count < max_refs) {
            snprintf(refs[*ref_count], 128, "%s", name);
            (*ref_count)++;
        }
        break;
    }
    case PY_AST_NODE_BINARY_OP: {
        PyBinaryNode* b = (PyBinaryNode*)node;
        pm_collect_referenced_ids(b->left, refs, ref_count, max_refs);
        pm_collect_referenced_ids(b->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_UNARY_OP:
    case PY_AST_NODE_NOT:
        pm_collect_referenced_ids(((PyUnaryNode*)node)->operand, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_COMPARE: {
        PyCompareNode* c = (PyCompareNode*)node;
        pm_collect_referenced_ids(c->left, refs, ref_count, max_refs);
        for (int i = 0; i < c->op_count; i++)
            pm_collect_referenced_ids(c->comparators[i], refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_BOOLEAN_OP: {
        PyBooleanNode* b = (PyBooleanNode*)node;
        pm_collect_referenced_ids(b->left, refs, ref_count, max_refs);
        pm_collect_referenced_ids(b->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_CALL: {
        PyCallNode* c = (PyCallNode*)node;
        pm_collect_referenced_ids(c->function, refs, ref_count, max_refs);
        PyAstNode* arg = c->arguments;
        while (arg) { pm_collect_referenced_ids(arg, refs, ref_count, max_refs); arg = arg->next; }
        break;
    }
    case PY_AST_NODE_KEYWORD_ARGUMENT:
        pm_collect_referenced_ids(((PyKeywordArgNode*)node)->value, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_ATTRIBUTE:
        pm_collect_referenced_ids(((PyAttributeNode*)node)->object, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_SUBSCRIPT: {
        PySubscriptNode* s = (PySubscriptNode*)node;
        pm_collect_referenced_ids(s->object, refs, ref_count, max_refs);
        pm_collect_referenced_ids(s->index, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_LIST:
    case PY_AST_NODE_TUPLE:
    case PY_AST_NODE_SET: {
        PyAstNode* e = ((PySequenceNode*)node)->elements;
        while (e) { pm_collect_referenced_ids(e, refs, ref_count, max_refs); e = e->next; }
        break;
    }
    case PY_AST_NODE_DICT: {
        PyAstNode* p = ((PyDictNode*)node)->pairs;
        while (p) { pm_collect_referenced_ids(p, refs, ref_count, max_refs); p = p->next; }
        break;
    }
    case PY_AST_NODE_PAIR: {
        PyPairNode* p = (PyPairNode*)node;
        pm_collect_referenced_ids(p->key, refs, ref_count, max_refs);
        pm_collect_referenced_ids(p->value, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_CONDITIONAL_EXPR: {
        PyConditionalNode* c = (PyConditionalNode*)node;
        pm_collect_referenced_ids(c->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(c->body, refs, ref_count, max_refs);
        pm_collect_referenced_ids(c->else_body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_RETURN:
        pm_collect_referenced_ids(((PyReturnNode*)node)->value, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        pm_collect_referenced_ids(((PyExpressionStatementNode*)node)->expression, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_ASSIGNMENT: {
        PyAssignmentNode* a = (PyAssignmentNode*)node;
        pm_collect_referenced_ids(a->value, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT: {
        PyAugAssignmentNode* a = (PyAugAssignmentNode*)node;
        pm_collect_referenced_ids(a->target, refs, ref_count, max_refs);
        pm_collect_referenced_ids(a->value, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* i = (PyIfNode*)node;
        pm_collect_referenced_ids(i->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(i->body, refs, ref_count, max_refs);
        PyAstNode* e = i->elif_clauses;
        while (e) { pm_collect_referenced_ids(e, refs, ref_count, max_refs); e = e->next; }
        pm_collect_referenced_ids(i->else_body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_ELIF: {
        PyIfNode* e = (PyIfNode*)node;
        pm_collect_referenced_ids(e->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(e->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_WHILE: {
        PyWhileNode* w = (PyWhileNode*)node;
        pm_collect_referenced_ids(w->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(w->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* f = (PyForNode*)node;
        pm_collect_referenced_ids(f->iter, refs, ref_count, max_refs);
        pm_collect_referenced_ids(f->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_BLOCK: {
        PyAstNode* s = ((PyBlockNode*)node)->statements;
        while (s) { pm_collect_referenced_ids(s, refs, ref_count, max_refs); s = s->next; }
        break;
    }
    case PY_AST_NODE_FSTRING: {
        PyAstNode* p = ((PyFStringNode*)node)->parts;
        while (p) { pm_collect_referenced_ids(p, refs, ref_count, max_refs); p = p->next; }
        break;
    }
    case PY_AST_NODE_LAMBDA: {
        // for lambdas inside a function, collect refs from the lambda body too
        PyLambdaNode* lam = (PyLambdaNode*)node;
        pm_collect_referenced_ids(lam->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_ASSERT: {
        PyAssertNode* a = (PyAssertNode*)node;
        pm_collect_referenced_ids(a->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(a->message, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_FUNCTION_DEF:
        // do NOT descend into nested function defs — they have their own scope
        break;
    default:
        break;
    }
}

// forward declaration — collects nonlocal names from function body
static void pm_collect_nonlocal_names(PyAstNode* body, char names[][128], int* count, int max);

// collect locally defined variable names (params + assignment targets + for targets)
// excludes names declared nonlocal
static void pm_collect_local_defs(PyFunctionDefNode* fn, char locals[][128], int* local_count, int max_locals) {
    // first collect nonlocal names to exclude them
    char nonlocals[32][128];
    int nonlocal_count = 0;
    pm_collect_nonlocal_names(fn->body, nonlocals, &nonlocal_count, 32);

    // params
    PyAstNode* p = fn->params;
    while (p) {
        String* pn = NULL;
        if (p->node_type == PY_AST_NODE_PARAMETER ||
            p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
            p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
            pn = ((PyParamNode*)p)->name;
        }
        if (pn && *local_count < max_locals) {
            snprintf(locals[*local_count], 128, "_py_%.*s", (int)pn->len, pn->chars);
            (*local_count)++;
        }
        p = p->next;
    }

    // walk the body for assignments and for-loop targets
    PyAstNode* s = fn->body ? (fn->body->node_type == PY_AST_NODE_BLOCK ?
        ((PyBlockNode*)fn->body)->statements : fn->body) : NULL;
    while (s) {
        if (s->node_type == PY_AST_NODE_ASSIGNMENT) {
            PyAssignmentNode* a = (PyAssignmentNode*)s;
            if (a->targets && a->targets->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* id = (PyIdentifierNode*)a->targets;
                char name[128];
                snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
                // check if declared nonlocal
                bool is_nl = false;
                for (int ni = 0; ni < nonlocal_count; ni++) {
                    if (strcmp(nonlocals[ni], name) == 0) { is_nl = true; break; }
                }
                if (is_nl) { s = s->next; continue; }
                // check duplicate
                bool dup = false;
                for (int i = 0; i < *local_count; i++) {
                    if (strcmp(locals[i], name) == 0) { dup = true; break; }
                }
                if (!dup && *local_count < max_locals) {
                    snprintf(locals[*local_count], 128, "%s", name);
                    (*local_count)++;
                }
            }
        } else if (s->node_type == PY_AST_NODE_FOR) {
            PyForNode* f = (PyForNode*)s;
            if (f->target && f->target->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* id = (PyIdentifierNode*)f->target;
                char name[128];
                snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
                bool dup = false;
                for (int i = 0; i < *local_count; i++) {
                    if (strcmp(locals[i], name) == 0) { dup = true; break; }
                }
                if (!dup && *local_count < max_locals) {
                    snprintf(locals[*local_count], 128, "%s", name);
                    (*local_count)++;
                }
            }
        }
        s = s->next;
    }
}

// analyze captures for all collected functions
static void pm_analyze_captures(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        if (fc->parent_index < 0) continue;  // top-level function, no captures

        // collect all identifiers referenced in this function body
        char refs[128][128];
        int ref_count = 0;
        pm_collect_referenced_ids(fc->node->body, refs, &ref_count, 128);

        // collect locally defined names
        char locals[64][128];
        int local_count = 0;
        pm_collect_local_defs(fc->node, locals, &local_count, 64);

        // also exclude names of known functions (they are resolved separately)
        // check which referenced names are NOT local
        for (int ri = 0; ri < ref_count; ri++) {
            bool is_local = false;
            for (int li = 0; li < local_count; li++) {
                if (strcmp(refs[ri], locals[li]) == 0) { is_local = true; break; }
            }
            if (is_local) continue;

            // check if it's a known function name
            bool is_func = false;
            for (int fi2 = 0; fi2 < mt->func_count; fi2++) {
                char fname[140];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[fi2].name);
                if (strcmp(refs[ri], fname) == 0) { is_func = true; break; }
            }
            if (is_func) continue;

            // check if this variable exists in the parent function's scope
            PyFuncCollected* parent = &mt->func_entries[fc->parent_index];
            // check parent params
            bool in_parent = false;
            PyAstNode* pp = parent->node->params;
            while (pp) {
                String* pn = NULL;
                if (pp->node_type == PY_AST_NODE_PARAMETER ||
                    pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                    pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                    pn = ((PyParamNode*)pp)->name;
                }
                if (pn) {
                    char pname[128];
                    snprintf(pname, sizeof(pname), "_py_%.*s", (int)pn->len, pn->chars);
                    if (strcmp(refs[ri], pname) == 0) { in_parent = true; break; }
                }
                pp = pp->next;
            }
            // also check parent's local assignments
            if (!in_parent) {
                char plocs[64][128];
                int ploc_count = 0;
                pm_collect_local_defs(parent->node, plocs, &ploc_count, 64);
                for (int pli = 0; pli < ploc_count; pli++) {
                    if (strcmp(refs[ri], plocs[pli]) == 0) { in_parent = true; break; }
                }
            }
            if (!in_parent) continue;

            // this is a captured variable
            if (fc->capture_count < 32) {
                snprintf(fc->captures[fc->capture_count], 128, "%s", refs[ri]);
                fc->capture_count++;
                fc->is_closure = true;
                log_debug("py-mir: function '%s' captures '%s' from parent '%s'",
                    fc->name, refs[ri], parent->name);
            }
        }
    }
}

// analyze captures for lambdas — similar to functions
static void pm_analyze_lambda_captures(PyMirTranspiler* mt) {
    for (int li = 0; li < mt->lambda_count; li++) {
        PyLambdaCollected* lc = &mt->lambda_entries[li];
        if (lc->parent_index < 0) continue;

        char refs[128][128];
        int ref_count = 0;
        pm_collect_referenced_ids(lc->node->body, refs, &ref_count, 128);

        // lambda's own params are local
        char locals[32][128];
        int local_count = 0;
        PyAstNode* p = lc->node->params;
        while (p) {
            String* pn = NULL;
            if (p->node_type == PY_AST_NODE_PARAMETER ||
                p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)p)->name;
            }
            if (pn && local_count < 32) {
                snprintf(locals[local_count], 128, "_py_%.*s", (int)pn->len, pn->chars);
                local_count++;
            }
            p = p->next;
        }

        for (int ri = 0; ri < ref_count; ri++) {
            bool is_local = false;
            for (int loi = 0; loi < local_count; loi++) {
                if (strcmp(refs[ri], locals[loi]) == 0) { is_local = true; break; }
            }
            if (is_local) continue;

            // check if it's a known function
            bool is_func = false;
            for (int fi = 0; fi < mt->func_count; fi++) {
                char fname[140];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[fi].name);
                if (strcmp(refs[ri], fname) == 0) { is_func = true; break; }
            }
            if (is_func) continue;

            // check parent function's scope
            PyFuncCollected* parent = &mt->func_entries[lc->parent_index];
            bool in_parent = false;
            PyAstNode* pp = parent->node->params;
            while (pp) {
                String* pn = NULL;
                if (pp->node_type == PY_AST_NODE_PARAMETER ||
                    pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                    pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                    pn = ((PyParamNode*)pp)->name;
                }
                if (pn) {
                    char pname[128];
                    snprintf(pname, sizeof(pname), "_py_%.*s", (int)pn->len, pn->chars);
                    if (strcmp(refs[ri], pname) == 0) { in_parent = true; break; }
                }
                pp = pp->next;
            }
            if (!in_parent) {
                char plocs[64][128];
                int ploc_count = 0;
                pm_collect_local_defs(parent->node, plocs, &ploc_count, 64);
                for (int pli = 0; pli < ploc_count; pli++) {
                    if (strcmp(refs[ri], plocs[pli]) == 0) { in_parent = true; break; }
                }
            }
            if (!in_parent) continue;

            if (lc->capture_count < 32) {
                snprintf(lc->captures[lc->capture_count], 128, "%s", refs[ri]);
                lc->capture_count++;
                lc->is_closure = true;
                log_debug("py-mir: lambda '%s' captures '%s' from parent '%s'",
                    lc->name, refs[ri], parent->name);
            }
        }
    }
}

// collect nonlocal declarations from a function's body (not recursing into nested functions)
static void pm_collect_nonlocal_names(PyAstNode* body, char names[][128], int* count, int max) {
    if (!body) return;
    if (body->node_type == PY_AST_NODE_NONLOCAL) {
        PyGlobalNonlocalNode* nl = (PyGlobalNonlocalNode*)body;
        for (int i = 0; i < nl->name_count && *count < max; i++) {
            snprintf(names[*count], 128, "_py_%.*s", (int)nl->names[i]->len, nl->names[i]->chars);
            (*count)++;
        }
        return;
    }
    if (body->node_type == PY_AST_NODE_BLOCK) {
        PyAstNode* s = ((PyBlockNode*)body)->statements;
        while (s) { pm_collect_nonlocal_names(s, names, count, max); s = s->next; }
        return;
    }
    // check first-level statements in if/while/for bodies, but don't recurse into nested functions
    if (body->node_type == PY_AST_NODE_IF) {
        PyIfNode* ifn = (PyIfNode*)body;
        pm_collect_nonlocal_names(ifn->body, names, count, max);
        pm_collect_nonlocal_names(ifn->else_body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_WHILE) {
        pm_collect_nonlocal_names(((PyWhileNode*)body)->body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_FOR) {
        pm_collect_nonlocal_names(((PyForNode*)body)->body, names, count, max);
        return;
    }
    // don't descend into nested function defs
}

// analyze nonlocal declarations: mark child captures as nonlocal and parent's shared env
static void pm_analyze_nonlocal(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        if (fc->parent_index < 0) continue;

        // collect nonlocal names from this function
        char nl_names[32][128];
        int nl_count = 0;
        pm_collect_nonlocal_names(fc->node->body, nl_names, &nl_count, 32);
        if (nl_count == 0) continue;

        PyFuncCollected* parent = &mt->func_entries[fc->parent_index];

        for (int ni = 0; ni < nl_count; ni++) {
            // mark this capture as nonlocal
            for (int ci = 0; ci < fc->capture_count; ci++) {
                if (strcmp(fc->captures[ci], nl_names[ni]) == 0) {
                    fc->capture_is_nonlocal[ci] = true;
                    break;
                }
            }
            // add to parent's shared env
            bool already = false;
            for (int si = 0; si < parent->shared_env_count; si++) {
                if (strcmp(parent->shared_env_names[si], nl_names[ni]) == 0) {
                    already = true;
                    break;
                }
            }
            if (!already && parent->shared_env_count < 32) {
                snprintf(parent->shared_env_names[parent->shared_env_count], 128, "%s", nl_names[ni]);
                parent->shared_env_count++;
                parent->has_nonlocal_children = true;
                log_debug("py-mir: parent '%s' shares env var '%s' for nonlocal",
                    parent->name, nl_names[ni]);
            }
        }
    }
}

// collect global declarations from a function's body (not recursing into nested functions)
static void pm_collect_global_names(PyAstNode* body, char names[][128], int* count, int max) {
    if (!body) return;
    if (body->node_type == PY_AST_NODE_GLOBAL) {
        PyGlobalNonlocalNode* gn = (PyGlobalNonlocalNode*)body;
        for (int i = 0; i < gn->name_count && *count < max; i++) {
            snprintf(names[*count], 128, "_py_%.*s", (int)gn->names[i]->len, gn->names[i]->chars);
            (*count)++;
        }
        return;
    }
    if (body->node_type == PY_AST_NODE_BLOCK) {
        PyAstNode* s = ((PyBlockNode*)body)->statements;
        while (s) { pm_collect_global_names(s, names, count, max); s = s->next; }
        return;
    }
    if (body->node_type == PY_AST_NODE_IF) {
        PyIfNode* ifn = (PyIfNode*)body;
        pm_collect_global_names(ifn->body, names, count, max);
        pm_collect_global_names(ifn->else_body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_WHILE) {
        pm_collect_global_names(((PyWhileNode*)body)->body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_FOR) {
        pm_collect_global_names(((PyForNode*)body)->body, names, count, max);
        return;
    }
    // don't descend into nested function defs
}

// analyze global declarations: collect global names into func_entries and allocate module var indices
static void pm_analyze_globals(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        // collect global names from this function's body
        pm_collect_global_names(fc->node->body, fc->global_names, &fc->global_name_count, 16);
        // allocate module var indices for each global name
        for (int gi = 0; gi < fc->global_name_count; gi++) {
            // check if already allocated
            bool found = false;
            for (int vi = 0; vi < mt->global_var_count; vi++) {
                if (strcmp(mt->global_var_names[vi], fc->global_names[gi]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && mt->global_var_count < 64) {
                int idx = mt->module_var_count++;
                snprintf(mt->global_var_names[mt->global_var_count], 128, "%s", fc->global_names[gi]);
                mt->global_var_indices[mt->global_var_count] = idx;
                mt->global_var_count++;
                log_debug("py-mir: global var '%s' → module_var[%d]", fc->global_names[gi], idx);
            }
        }
    }
}

// check if a variable name is declared global in the current function,
// or if at module level, check if any function declared it global
static bool pm_is_global_in_current_func(PyMirTranspiler* mt, const char* vname) {
    if (mt->current_func_index >= 0) {
        // inside a function: check if this function has a `global` declaration for this name
        PyFuncCollected* fc = &mt->func_entries[mt->current_func_index];
        for (int i = 0; i < fc->global_name_count; i++) {
            if (strcmp(fc->global_names[i], vname) == 0) return true;
        }
        return false;
    }
    // at module level: check if any function declared this variable as global
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], vname) == 0) return true;
    }
    return false;
}

// get the module var index for a global variable name
static int pm_get_global_var_index(PyMirTranspiler* mt, const char* vname) {
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], vname) == 0) return mt->global_var_indices[i];
    }
    return -1;
}

// walk all AST nodes to find lambda expressions, pre-collect them
static void pm_collect_lambdas_r(PyMirTranspiler* mt, PyAstNode* node, int parent_func_index) {
    if (!node) return;

    if (node->node_type == PY_AST_NODE_LAMBDA) {
        if (mt->lambda_count < 64) {
            PyLambdaCollected* lc = &mt->lambda_entries[mt->lambda_count];
            memset(lc, 0, sizeof(PyLambdaCollected));
            lc->node = (PyLambdaNode*)node;
            lc->parent_index = parent_func_index;
            snprintf(lc->name, sizeof(lc->name), "__lambda_%d", mt->lambda_count);
            int pc = 0;
            PyAstNode* p = lc->node->params;
            while (p) { pc++; p = p->next; }
            lc->param_count = pc;
            mt->lambda_count++;
        }
        pm_collect_lambdas_r(mt, ((PyLambdaNode*)node)->body, parent_func_index);
        return;
    }

    switch (node->node_type) {
    case PY_AST_NODE_MODULE:
        { PyAstNode* s = ((PyModuleNode*)node)->body; while (s) { pm_collect_lambdas_r(mt, s, parent_func_index); s = s->next; } } break;
    case PY_AST_NODE_BLOCK:
        { PyAstNode* s = ((PyBlockNode*)node)->statements; while (s) { pm_collect_lambdas_r(mt, s, parent_func_index); s = s->next; } } break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        pm_collect_lambdas_r(mt, ((PyExpressionStatementNode*)node)->expression, parent_func_index); break;
    case PY_AST_NODE_ASSIGNMENT:
        { PyAssignmentNode* a = (PyAssignmentNode*)node; pm_collect_lambdas_r(mt, a->value, parent_func_index); } break;
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT:
        { PyAugAssignmentNode* a = (PyAugAssignmentNode*)node; pm_collect_lambdas_r(mt, a->value, parent_func_index); } break;
    case PY_AST_NODE_BINARY_OP:
        { PyBinaryNode* b = (PyBinaryNode*)node; pm_collect_lambdas_r(mt, b->left, parent_func_index); pm_collect_lambdas_r(mt, b->right, parent_func_index); } break;
    case PY_AST_NODE_UNARY_OP:
    case PY_AST_NODE_NOT:
        pm_collect_lambdas_r(mt, ((PyUnaryNode*)node)->operand, parent_func_index); break;
    case PY_AST_NODE_COMPARE:
        { PyCompareNode* c = (PyCompareNode*)node; pm_collect_lambdas_r(mt, c->left, parent_func_index);
          for (int i = 0; i < c->op_count; i++) pm_collect_lambdas_r(mt, c->comparators[i], parent_func_index); } break;
    case PY_AST_NODE_BOOLEAN_OP:
        { PyBooleanNode* b = (PyBooleanNode*)node; pm_collect_lambdas_r(mt, b->left, parent_func_index); pm_collect_lambdas_r(mt, b->right, parent_func_index); } break;
    case PY_AST_NODE_CALL:
        { PyCallNode* c = (PyCallNode*)node; pm_collect_lambdas_r(mt, c->function, parent_func_index);
          PyAstNode* arg = c->arguments; while (arg) { pm_collect_lambdas_r(mt, arg, parent_func_index); arg = arg->next; } } break;
    case PY_AST_NODE_KEYWORD_ARGUMENT:
        pm_collect_lambdas_r(mt, ((PyKeywordArgNode*)node)->value, parent_func_index); break;
    case PY_AST_NODE_ATTRIBUTE:
        pm_collect_lambdas_r(mt, ((PyAttributeNode*)node)->object, parent_func_index); break;
    case PY_AST_NODE_SUBSCRIPT:
        { PySubscriptNode* s = (PySubscriptNode*)node; pm_collect_lambdas_r(mt, s->object, parent_func_index); pm_collect_lambdas_r(mt, s->index, parent_func_index); } break;
    case PY_AST_NODE_LIST:
    case PY_AST_NODE_TUPLE:
    case PY_AST_NODE_SET:
        { PyAstNode* e = ((PySequenceNode*)node)->elements; while (e) { pm_collect_lambdas_r(mt, e, parent_func_index); e = e->next; } } break;
    case PY_AST_NODE_DICT:
        { PyAstNode* p = ((PyDictNode*)node)->pairs; while (p) { pm_collect_lambdas_r(mt, p, parent_func_index); p = p->next; } } break;
    case PY_AST_NODE_PAIR:
        { PyPairNode* p = (PyPairNode*)node; pm_collect_lambdas_r(mt, p->key, parent_func_index); pm_collect_lambdas_r(mt, p->value, parent_func_index); } break;
    case PY_AST_NODE_CONDITIONAL_EXPR:
        { PyConditionalNode* c = (PyConditionalNode*)node; pm_collect_lambdas_r(mt, c->test, parent_func_index); pm_collect_lambdas_r(mt, c->body, parent_func_index); pm_collect_lambdas_r(mt, c->else_body, parent_func_index); } break;
    case PY_AST_NODE_IF:
        { PyIfNode* i = (PyIfNode*)node; pm_collect_lambdas_r(mt, i->test, parent_func_index); pm_collect_lambdas_r(mt, i->body, parent_func_index);
          PyAstNode* e = i->elif_clauses; while (e) { pm_collect_lambdas_r(mt, e, parent_func_index); e = e->next; }
          pm_collect_lambdas_r(mt, i->else_body, parent_func_index); } break;
    case PY_AST_NODE_ELIF:
        { PyIfNode* e = (PyIfNode*)node; pm_collect_lambdas_r(mt, e->test, parent_func_index); pm_collect_lambdas_r(mt, e->body, parent_func_index); } break;
    case PY_AST_NODE_WHILE:
        { PyWhileNode* w = (PyWhileNode*)node; pm_collect_lambdas_r(mt, w->test, parent_func_index); pm_collect_lambdas_r(mt, w->body, parent_func_index); } break;
    case PY_AST_NODE_FOR:
        { PyForNode* f = (PyForNode*)node; pm_collect_lambdas_r(mt, f->iter, parent_func_index); pm_collect_lambdas_r(mt, f->body, parent_func_index); } break;
    case PY_AST_NODE_RETURN:
        pm_collect_lambdas_r(mt, ((PyReturnNode*)node)->value, parent_func_index); break;
    case PY_AST_NODE_FUNCTION_DEF: {
        PyFunctionDefNode* fdef = (PyFunctionDefNode*)node;
        int func_idx = parent_func_index;
        for (int ii = 0; ii < mt->func_count; ii++) {
            if (mt->func_entries[ii].node == fdef) { func_idx = ii; break; }
        }
        pm_collect_lambdas_r(mt, fdef->body, func_idx);
        break;
    }
    case PY_AST_NODE_LIST_COMPREHENSION:
    case PY_AST_NODE_DICT_COMPREHENSION:
    case PY_AST_NODE_SET_COMPREHENSION:
        { PyComprehensionNode* c = (PyComprehensionNode*)node;
          pm_collect_lambdas_r(mt, c->element, parent_func_index); pm_collect_lambdas_r(mt, c->iter, parent_func_index);
          pm_collect_lambdas_r(mt, c->conditions, parent_func_index); pm_collect_lambdas_r(mt, c->inner, parent_func_index); } break;
    case PY_AST_NODE_FSTRING:
        { PyAstNode* p = ((PyFStringNode*)node)->parts; while (p) { pm_collect_lambdas_r(mt, p, parent_func_index); p = p->next; } } break;
    default:
        break;
    }
}

static void pm_compile_lambda(PyMirTranspiler* mt, PyLambdaCollected* lc) {
    PyLambdaNode* lam = lc->node;

    int mir_param_count = lc->param_count + (lc->is_closure ? 1 : 0);
    MIR_var_t params[17];
    char* param_name_bufs[17];
    int offset = 0;

    if (lc->is_closure) {
        param_name_bufs[0] = (char*)alloca(128);
        snprintf(param_name_bufs[0], 128, "_py__env");
        params[0] = {MIR_T_I64, param_name_bufs[0], 0};
        offset = 1;
    }

    PyAstNode* p = lam->params;
    for (int i = 0; i < lc->param_count && i < 16; i++) {
        param_name_bufs[i + offset] = (char*)alloca(128);
        String* pn = NULL;
        if (p) {
            if (p->node_type == PY_AST_NODE_PARAMETER ||
                p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)p)->name;
            }
            p = p->next;
        }
        if (pn) {
            snprintf(param_name_bufs[i + offset], 128, "_py_%.*s", (int)pn->len, pn->chars);
        } else {
            snprintf(param_name_bufs[i + offset], 128, "_py_p%d", i);
        }
        params[i + offset] = {MIR_T_I64, param_name_bufs[i + offset], 0};
    }

    MIR_type_t res_type = MIR_T_I64;
    lc->func_item = MIR_new_func_arr(mt->ctx, lc->name, 1, &res_type,
        mir_param_count, params);

    MIR_item_t old_func_item = mt->current_func_item;
    MIR_func_t old_func = mt->current_func;
    int old_scope_depth = mt->scope_depth;
    MIR_reg_t old_env_reg = mt->scope_env_reg;
    int old_env_slot_count = mt->scope_env_slot_count;

    mt->current_func_item = lc->func_item;
    mt->current_func = lc->func_item->u.func;

    pm_push_scope(mt);

    for (int i = 0; i < lc->param_count && i < 16; i++) {
        MIR_reg_t preg = MIR_reg(mt->ctx, param_name_bufs[i + offset], mt->current_func);
        pm_set_var(mt, param_name_bufs[i + offset], preg);
    }

    // if this is a closure, load captured variables from env
    if (lc->is_closure) {
        MIR_reg_t env_reg = MIR_reg(mt->ctx, "_py__env", mt->current_func);
        mt->scope_env_reg = env_reg;
        mt->scope_env_slot_count = lc->capture_count;
        for (int i = 0; i < lc->capture_count; i++) {
            MIR_reg_t var_reg = pm_new_reg(mt, "cvar", MIR_T_I64);
            MIR_reg_t addr = pm_new_reg(mt, "eaddr", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, addr),
                MIR_new_reg_op(mt->ctx, env_reg),
                MIR_new_int_op(mt->ctx, i * 8)));
            pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1)));
            pm_set_var(mt, lc->captures[i], var_reg);
        }
    } else {
        mt->scope_env_reg = 0;
        mt->scope_env_slot_count = 0;
    }

    MIR_reg_t body_val = pm_transpile_expression(mt, lam->body);
    pm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, body_val)));

    MIR_finish_func(mt->ctx);

    pm_pop_scope(mt);
    mt->current_func_item = old_func_item;
    mt->current_func = old_func;
    mt->scope_depth = old_scope_depth;
    mt->scope_env_reg = old_env_reg;
    mt->scope_env_slot_count = old_env_slot_count;
}

static void pm_compile_function(PyMirTranspiler* mt, PyFuncCollected* fc) {
    char fn_name[140];
    snprintf(fn_name, sizeof(fn_name), "_%s", fc->name);

    // build parameter list; closures get _py__env as hidden first param
    // *args functions get _py__varargs (ptr) + _py__varargc (count) as extra params
    int mir_param_count = fc->param_count + (fc->is_closure ? 1 : 0) + (fc->has_star_args ? 2 : 0);
    MIR_var_t params[20];
    char* param_name_bufs[20];
    int offset = 0;

    if (fc->is_closure) {
        param_name_bufs[0] = (char*)alloca(128);
        snprintf(param_name_bufs[0], 128, "_py__env");
        params[0] = {MIR_T_I64, param_name_bufs[0], 0};
        offset = 1;
    }

    PyAstNode* pnode = fc->node->params;
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        param_name_bufs[i + offset] = (char*)alloca(128);
        String* pn = NULL;
        while (pnode) {
            if (pnode->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                pnode = pnode->next;
                continue;
            }
            if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)pnode)->name;
            }
            pnode = pnode->next;
            break;
        }
        if (pn) {
            snprintf(param_name_bufs[i + offset], 128, "_py_%.*s", (int)pn->len, pn->chars);
        } else {
            snprintf(param_name_bufs[i + offset], 128, "_py_p%d", i);
        }
        params[i + offset] = {MIR_T_I64, param_name_bufs[i + offset], 0};
    }

    // add *args extra params after regular params
    int varargs_param_offset = fc->param_count + offset;
    if (fc->has_star_args) {
        param_name_bufs[varargs_param_offset] = (char*)alloca(128);
        snprintf(param_name_bufs[varargs_param_offset], 128, "_py__varargs");
        params[varargs_param_offset] = {MIR_T_I64, param_name_bufs[varargs_param_offset], 0};
        param_name_bufs[varargs_param_offset + 1] = (char*)alloca(128);
        snprintf(param_name_bufs[varargs_param_offset + 1], 128, "_py__varargc");
        params[varargs_param_offset + 1] = {MIR_T_I64, param_name_bufs[varargs_param_offset + 1], 0};
    }

    MIR_type_t res_type = MIR_T_I64;
    fc->func_item = MIR_new_func_arr(mt->ctx, fn_name, 1, &res_type,
        mir_param_count, params);

    // save parent state
    MIR_item_t old_func_item = mt->current_func_item;
    MIR_func_t old_func = mt->current_func;
    int old_scope_depth = mt->scope_depth;
    int old_func_index = mt->current_func_index;
    MIR_reg_t old_env_reg = mt->scope_env_reg;
    int old_env_slot_count = mt->scope_env_slot_count;

    mt->current_func_item = fc->func_item;
    mt->current_func = fc->func_item->u.func;

    // find this function's index in func_entries
    for (int i = 0; i < mt->func_count; i++) {
        if (&mt->func_entries[i] == fc) { mt->current_func_index = i; break; }
    }

    // push function scope
    pm_push_scope(mt);

    // register params as variables
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        MIR_reg_t preg = MIR_reg(mt->ctx, param_name_bufs[i + offset], mt->current_func);
        pm_set_var(mt, param_name_bufs[i + offset], preg);
    }

    // if function has *args, build a list from the varargs pointer + count
    if (fc->has_star_args) {
        MIR_reg_t va_ptr = MIR_reg(mt->ctx, "_py__varargs", mt->current_func);
        MIR_reg_t va_cnt = MIR_reg(mt->ctx, "_py__varargc", mt->current_func);
        // call py_build_list_from_args(ptr, count) → list Item
        MIR_reg_t star_list = pm_call_2(mt, "py_build_list_from_args", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, va_ptr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, va_cnt));
        MIR_reg_t star_reg = pm_new_reg(mt, fc->star_args_name, MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, star_reg),
            MIR_new_reg_op(mt->ctx, star_list)));
        pm_set_var(mt, fc->star_args_name, star_reg);
    }

    // if this is a closure, load captured variables from env
    if (fc->is_closure) {
        MIR_reg_t env_reg = MIR_reg(mt->ctx, "_py__env", mt->current_func);
        mt->scope_env_reg = env_reg;
        mt->scope_env_slot_count = fc->capture_count;
        for (int i = 0; i < fc->capture_count; i++) {
            if (fc->capture_is_nonlocal[i]) {
                // nonlocal: reads and writes go through env slot directly
                pm_set_env_var(mt, fc->captures[i], env_reg, i);
            } else {
                // read-only: load once into a local register
                MIR_reg_t var_reg = pm_new_reg(mt, "cvar", MIR_T_I64);
                MIR_reg_t addr = pm_new_reg(mt, "eaddr", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, addr),
                    MIR_new_reg_op(mt->ctx, env_reg),
                    MIR_new_int_op(mt->ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var_reg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1)));
                pm_set_var(mt, fc->captures[i], var_reg);
            }
        }
    } else {
        mt->scope_env_reg = 0;
        mt->scope_env_slot_count = 0;
    }

    // if this function has nonlocal children, allocate a shared env and route
    // shared vars through it — both this function and its children access the same env
    if (fc->has_nonlocal_children) {
        MIR_reg_t shared_env = pm_call_1(mt, "py_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, fc->shared_env_count));
        mt->scope_env_reg = shared_env;
        mt->scope_env_slot_count = fc->shared_env_count;
        for (int si = 0; si < fc->shared_env_count; si++) {
            // initialize the env slot from current param/local value if it exists
            PyMirVarEntry* existing = pm_find_var(mt, fc->shared_env_names[si]);
            if (existing && !existing->from_env) {
                pm_store_env_slot(mt, shared_env, si, existing->reg);
            }
            // re-register this var as env-backed
            pm_set_env_var(mt, fc->shared_env_names[si], shared_env, si);
        }
    }

    // emit default-value initialization for parameters with defaults:
    // if param == PY_ITEM_NULL_VAL, assign the default expression
    {
        PyAstNode* dp = fc->node->params;
        int pi = 0;
        while (dp && pi < fc->param_count && pi < 16) {
            if (dp->node_type == PY_AST_NODE_DEFAULT_PARAMETER) {
                PyParamNode* pp = (PyParamNode*)dp;
                if (pp->default_value) {
                    MIR_reg_t preg = MIR_reg(mt->ctx, param_name_bufs[pi + offset], mt->current_func);
                    MIR_reg_t is_null = pm_new_reg(mt, "dnull", MIR_T_I64);
                    MIR_label_t l_no_default = pm_new_label(mt);
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_NE,
                        MIR_new_reg_op(mt->ctx, is_null),
                        MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_int_op(mt->ctx, (int64_t)PY_ITEM_NULL_VAL)));
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, l_no_default),
                        MIR_new_reg_op(mt->ctx, is_null)));
                    // param was null → evaluate default expression and assign
                    MIR_reg_t def_val = pm_transpile_expression(mt, pp->default_value);
                    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_reg_op(mt->ctx, def_val)));
                    pm_emit_label(mt, l_no_default);
                }
            }
            dp = dp->next;
            pi++;
        }
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
    mt->current_func_index = old_func_index;
    mt->scope_env_reg = old_env_reg;
    mt->scope_env_slot_count = old_env_slot_count;

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

    // Phase 1b: collect lambda expressions
    pm_collect_lambdas_r(mt, root, -1);
    log_debug("py-mir: collected %d lambdas", mt->lambda_count);

    // Phase 1c: analyze captures for closures
    pm_analyze_captures(mt);
    pm_analyze_lambda_captures(mt);

    // Phase 1d: analyze nonlocal declarations → shared env
    pm_analyze_nonlocal(mt);

    // Phase 1e: analyze global declarations → module var indices
    pm_analyze_globals(mt);

    // Phase 2: scan module-level variables
    pm_scan_module_vars(mt, root);

    // Phase 3a: compile all lambdas first (functions reference lambda func_items)
    for (int i = 0; i < mt->lambda_count; i++) {
        pm_compile_lambda(mt, &mt->lambda_entries[i]);
    }

    // Phase 3b: compile all functions (reverse order so nested functions compile first)
    for (int i = mt->func_count - 1; i >= 0; i--) {
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
    mt->filename = filename;
    mt->runtime = runtime;
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

// ============================================================================
// Deferred MIR context cleanup for Python modules
// ============================================================================

#define PM_MAX_MODULE_CONTEXTS 64
static MIR_context_t pm_module_mir_contexts[PM_MAX_MODULE_CONTEXTS];
static int pm_module_mir_context_count = 0;

static void pm_defer_mir_cleanup(MIR_context_t ctx) {
    if (pm_module_mir_context_count < PM_MAX_MODULE_CONTEXTS) {
        pm_module_mir_contexts[pm_module_mir_context_count++] = ctx;
    } else {
        log_error("py-mir: exceeded max deferred MIR contexts (%d)", PM_MAX_MODULE_CONTEXTS);
        MIR_finish(ctx);
    }
}

// ============================================================================
// Load Python module for cross-language import
// ============================================================================

Item load_py_module(Runtime* runtime, const char* py_path) {
    log_info("py-mir: loading Python module '%s' for cross-language import", py_path);

    // check if already loaded in module registry
    ModuleDescriptor* cached = module_get(py_path);
    if (cached && cached->initialized) {
        log_debug("py-mir: module '%s' already loaded, returning cached namespace", py_path);
        return cached->namespace_obj;
    }

    char* source = read_text_file(py_path);
    if (!source) {
        log_debug("py-mir: cannot read Python file '%s'", py_path);
        return ItemNull;
    }

    // ensure a heap context exists for module loading
    if (!context || !context->heap) {
        EvalContext* temp_ctx = (EvalContext*)calloc(1, sizeof(EvalContext));
        temp_ctx->pool = pool_create();
        temp_ctx->result = ItemNull;
        temp_ctx->nursery = gc_nursery_create(0);
        context = temp_ctx;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
        _lambda_rt = (Context*)context;

        Input* py_input = Input::create(context->pool);
        py_runtime_set_input(py_input);
        log_debug("py-mir: created persistent heap for cross-language module loading");
    }

    // ensure JS runtime Input is initialized (needed for js_property_set / map_put)
    {
        Input* js_inp = Input::create(context->pool);
        js_runtime_set_input(js_inp);
    }

    // create Python transpiler (parsing + AST building)
    PyTranspiler* tp = py_transpiler_create(runtime);
    if (!tp) {
        log_error("py-mir: module: failed to create transpiler for '%s'", py_path);
        free(source);
        return ItemNull;
    }

    if (!py_transpiler_parse(tp, source, strlen(source))) {
        log_error("py-mir: module: parse failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        free(source);
        return ItemNull;
    }

    TSNode root = ts_tree_root_node(tp->tree);
    PyAstNode* ast = build_py_ast(tp, root);
    if (!ast) {
        log_error("py-mir: module: AST build failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        free(source);
        return ItemNull;
    }

    MIR_context_t ctx = jit_init(2);
    if (!ctx) {
        log_error("py-mir: module: MIR context init failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        free(source);
        return ItemNull;
    }

    PyMirTranspiler* mt = (PyMirTranspiler*)malloc(sizeof(PyMirTranspiler));
    if (!mt) {
        log_error("py-mir: module: failed to allocate transpiler for '%s'", py_path);
        MIR_finish(ctx);
        py_transpiler_destroy(tp);
        free(source);
        return ItemNull;
    }
    memset(mt, 0, sizeof(PyMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->filename = py_path;
    mt->runtime = runtime;
    mt->import_cache = hashmap_new(sizeof(PyImportCacheEntry), 64, 0, 0,
        py_import_cache_hash, py_import_cache_cmp, NULL, NULL);
    mt->local_funcs = hashmap_new(sizeof(PyLocalFuncEntry), 32, 0, 0,
        py_local_func_hash, py_local_func_cmp, NULL, NULL);
    mt->var_scopes[0] = hashmap_new(sizeof(PyVarScopeEntry), 16, 0, 0,
        py_var_scope_hash, py_var_scope_cmp, NULL, NULL);
    mt->scope_depth = 0;
    mt->current_func_index = -1;

    mt->module = MIR_new_module(ctx, "py_module");

    // transpile AST to MIR (compiles all functions + py_main)
    pm_transpile_ast(mt, ast);

    MIR_finish_module(mt->ctx);
    MIR_load_module(mt->ctx, mt->module);
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // execute py_main to run module-level code (assignments, etc.)
    typedef Item (*py_main_func_t)(Context*);
    py_main_func_t py_main = (py_main_func_t)find_func(ctx, "py_main");
    if (py_main) {
        py_reset_module_vars();
        py_main((Context*)context);
    }

    // build namespace from top-level compiled functions
    Item ns = js_new_object();
    for (int i = 0; i < mt->func_count; i++) {
        PyFuncCollected* fc = &mt->func_entries[i];
        if (fc->parent_index != -1) continue;  // skip nested functions

        // get the JIT-compiled function pointer by name
        char mir_name[140];
        snprintf(mir_name, sizeof(mir_name), "_%s", fc->name);
        void* func_ptr = find_func(ctx, mir_name);

        if (func_ptr) {
            Item val = js_new_function(func_ptr, fc->param_count);
            Item key = {.item = s2it(heap_create_name(fc->name))};
            js_property_set(ns, key, val);
            log_debug("py-mir: module export fn '%s' arity=%d", fc->name, fc->param_count);
        }
    }

    // register in unified module registry
    module_register(py_path, "python", ns, ctx);

    // cleanup transpiler state but DEFER MIR context cleanup
    hashmap_free(mt->import_cache);
    hashmap_free(mt->local_funcs);
    if (mt->module_consts) hashmap_free(mt->module_consts);
    for (int i = 0; i <= mt->scope_depth; i++) {
        if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
    }
    free(mt);
    pm_defer_mir_cleanup(ctx);
    py_transpiler_destroy(tp);
    free(source);

    log_info("py-mir: module '%s' loaded successfully", py_path);
    return ns;
}
