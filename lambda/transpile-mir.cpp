#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/hashmap.h"
#include "validator/validator.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern Type TYPE_ANY, TYPE_INT;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern void heap_destroy();
extern void frame_start();
extern void frame_end();
extern Url* get_current_dir();

// Forward declare Runner helper functions from runner.cpp
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);

// Forward declare import resolver from mir.c
extern "C" {
    void *import_resolver(const char *name);
}

// ============================================================================
// MIR Transpiler Context
// ============================================================================

struct MirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
};

// Variable entry in scope
struct MirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId type_id;
};

// Loop label pair for break/continue
struct LoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
};

struct MirTranspiler {
    // Input
    AstScript* script;
    const char* source;
    Runtime* runtime;
    bool is_main;
    int script_index;

    // MIR context
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache: name -> MirImportEntry
    struct hashmap* import_cache;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Variable scopes: array of hashmaps, each mapping name -> MirVarEntry
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop label stack
    LoopLabels loop_stack[32];
    int loop_depth;

    // Counters
    int reg_counter;
    int label_counter;

    // Runtime pointer register (loaded at function entry)
    MIR_reg_t rt_reg;

    // Consts pointer register
    MIR_reg_t consts_reg;

    // Current pipe context
    MIR_reg_t pipe_item_reg;
    MIR_reg_t pipe_index_reg;
    bool in_pipe;

    // TCO
    AstFuncNode* tco_func;
    MIR_label_t tco_label;

    // Closure
    AstFuncNode* current_closure;
    MIR_reg_t env_reg;
};

// ============================================================================
// Hashmap helpers for import_cache and var_scopes
// ============================================================================

struct ImportCacheEntry {
    char name[128];
    MirImportEntry entry;
};

static int import_cache_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((ImportCacheEntry*)a)->name, ((ImportCacheEntry*)b)->name);
}
static uint64_t import_cache_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const ImportCacheEntry* e = (const ImportCacheEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

struct VarScopeEntry {
    char name[128];
    MirVarEntry var;
};

static int var_scope_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((VarScopeEntry*)a)->name, ((VarScopeEntry*)b)->name);
}
static uint64_t var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const VarScopeEntry* e = (const VarScopeEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

struct LocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

static int local_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((LocalFuncEntry*)a)->name, ((LocalFuncEntry*)b)->name);
}
static uint64_t local_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const LocalFuncEntry* e = (const LocalFuncEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

// ============================================================================
// Helpers
// ============================================================================

static MIR_type_t type_to_mir(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT:
        return MIR_T_D;
    default:
        // MIR registers only support I64, F, D, LD - use I64 for all non-float types
        // (including pointers, which are stored as I64 in MIR registers)
        return MIR_T_I64;
    }
}

// Convert type for MIR register allocation (no MIR_T_P allowed for registers)
static MIR_type_t reg_type(MIR_type_t t) {
    return (t == MIR_T_P || t == MIR_T_F) ? MIR_T_I64 : t;
}

static MIR_type_t node_mir_type(AstNode* node) {
    if (!node || !node->type) return MIR_T_I64;
    return type_to_mir(node->type->type_id);
}

static MIR_reg_t new_reg(MirTranspiler* mt, const char* prefix, MIR_type_t type) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, mt->reg_counter++);
    return MIR_new_func_reg(mt->ctx, mt->current_func, reg_type(type), name);
}

static MIR_label_t new_label(MirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

static void emit_insn(MirTranspiler* mt, MIR_insn_t insn) {
    MIR_append_insn(mt->ctx, mt->current_func_item, insn);
}

static void emit_label(MirTranspiler* mt, MIR_label_t label) {
    MIR_append_insn(mt->ctx, mt->current_func_item, label);
}

// ============================================================================
// Scope management
// ============================================================================

static void push_scope(MirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(VarScopeEntry), 16, 0, 0,
        var_scope_hash, var_scope_cmp, NULL, NULL);
}

static void pop_scope(MirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void set_var(MirTranspiler* mt, const char* name, MIR_reg_t reg, MIR_type_t mir_type, TypeId type_id) {
    VarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static MirVarEntry* find_var(MirTranspiler* mt, const char* name) {
    VarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        VarScopeEntry* found = (VarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Import management (lazy proto + import creation)
// ============================================================================

// Look up a locally defined function
static MIR_item_t find_local_func(MirTranspiler* mt, const char* name) {
    LocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    LocalFuncEntry* found = (LocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
    return found ? found->func_item : NULL;
}

static void register_local_func(MirTranspiler* mt, const char* name, MIR_item_t func_item) {
    LocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.func_item = func_item;
    hashmap_set(mt->local_funcs, &entry);
}

// Get or create import + proto for a runtime function
static MirImportEntry* ensure_import(MirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    ImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    ImportCacheEntry* found = (ImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return &found->entry;

    // Create proto and import
    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    ImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (ImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return &found->entry;
}

// Convenience: import a function with signature Item(Item, Item)
static MirImportEntry* ensure_import_ii_i(MirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// Item(Item)
static MirImportEntry* ensure_import_i_i(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(void) - no args
static MirImportEntry* ensure_import_v_i(MirTranspiler* mt, const char* name) {
    return ensure_import(mt, name, MIR_T_I64, 0, NULL, 1);
}

// void(void)
static MirImportEntry* ensure_import_v_v(MirTranspiler* mt, const char* name) {
    return ensure_import(mt, name, MIR_T_I64, 0, NULL, 0);
}

// int64_t(Item)
static MirImportEntry* ensure_import_i_l(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(double)
static MirImportEntry* ensure_import_d_i(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_D, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(int64_t)
static MirImportEntry* ensure_import_l_i(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Bool(Item)
static MirImportEntry* ensure_import_i_b(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// ptr(void) - returns pointer
static MirImportEntry* ensure_import_v_p(MirTranspiler* mt, const char* name) {
    return ensure_import(mt, name, MIR_T_P, 0, NULL, 1);
}

// void(ptr)
static MirImportEntry* ensure_import_p_v(MirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_P, "a", 0}};
    return ensure_import(mt, name, MIR_T_I64, 1, args, 0);
}

// Item(ptr, Item)
static MirImportEntry* ensure_import_pi_v(MirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_P, "a", 0}, {MIR_T_I64, "b", 0}};
    return ensure_import(mt, name, MIR_T_I64, 2, args, 0);
}

// int64_t(Item, int)
static MirImportEntry* ensure_import_ii_l(MirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// ============================================================================
// Emit runtime function calls
// ============================================================================

static MIR_reg_t emit_call_0(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type) {
    MirImportEntry* ie = ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = new_reg(mt, fn_name, ret_type);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

static MIR_reg_t emit_call_1(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t arg1_type, MIR_op_t arg1) {
    MIR_var_t args[1] = {{arg1_type, "a", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = new_reg(mt, fn_name, ret_type);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res),
        arg1));
    return res;
}

static MIR_reg_t emit_call_2(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = new_reg(mt, fn_name, ret_type);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res),
        a1, a2));
    return res;
}

static MIR_reg_t emit_call_3(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = new_reg(mt, fn_name, ret_type);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res),
        a1, a2, a3));
    return res;
}

// Call with no return value
static void emit_call_void_1(MirTranspiler* mt, const char* fn_name,
    MIR_type_t arg1_type, MIR_op_t arg1) {
    MIR_var_t args[1] = {{arg1_type, "a", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        arg1));
}

static void emit_call_void_2(MirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        a1, a2));
}

// ============================================================================
// Boxing/Unboxing Helpers (emit inline MIR instructions)
// ============================================================================

// Box int64_t -> Item (inline i2it macro equivalent)
// i2it(v) = (v <= INT56_MAX && v >= INT56_MIN) ? (ITEM_INT | (v & MASK56)) : ITEM_ERROR
static MIR_reg_t emit_box_int(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxi", MIR_T_I64);
    MIR_reg_t masked = new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t in_range = new_reg(mt, "rng", MIR_T_I64);
    MIR_reg_t le_max = new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = new_reg(mt, "ge", MIR_T_I64);

    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;
    uint64_t MASK56 = 0x00FFFFFFFFFFFFFFULL;
    uint64_t ITEM_INT_TAG = (uint64_t)LMD_TYPE_INT << 56;

    // le_max = val <= INT56_MAX
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LE, MIR_new_reg_op(mt->ctx, le_max),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, INT56_MAX_VAL)));
    // ge_min = val >= INT56_MIN
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, ge_min),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, INT56_MIN_VAL)));
    // in_range = le_max & ge_min
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range),
        MIR_new_reg_op(mt->ctx, le_max), MIR_new_reg_op(mt->ctx, ge_min)));
    // masked = val & MASK56
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, (int64_t)MASK56)));
    // tagged = ITEM_INT | masked
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, tagged),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_INT_TAG), MIR_new_reg_op(mt->ctx, masked)));

    // result = in_range ? tagged : ITEM_ERROR
    MIR_label_t l_ok = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, in_range)));
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_ok);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tagged)));
    emit_label(mt, l_end);
    return result;
}

// Box bool -> Item (inline b2it)
static MIR_reg_t emit_box_bool(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxb", MIR_T_I64);
    uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    return result;
}

// Box float (double) -> Item via push_d runtime call
static MIR_reg_t emit_box_float(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box int64 -> Item via push_l runtime call
static MIR_reg_t emit_box_int64(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_l", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box string pointer -> Item (inline s2it)
// s2it(ptr) = ptr ? (STRING_TAG | (uint64_t)ptr) : ITEM_NULL
static MIR_reg_t emit_box_string(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxs", MIR_T_I64);
    uint64_t STR_TAG = (uint64_t)LMD_TYPE_STRING << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;

    MIR_label_t l_notnull = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_notnull),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_notnull);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box symbol pointer -> Item (inline y2it)
static MIR_reg_t emit_box_symbol(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxy", MIR_T_I64);
    uint64_t SYM_TAG = (uint64_t)LMD_TYPE_SYMBOL << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;

    MIR_label_t l_notnull = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_notnull),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_notnull);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)SYM_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box a container pointer (Array*, List*, Map*, Element*, etc.) -> Item
// Containers have their type_id in their first struct field, so just cast ptr to Item
static MIR_reg_t emit_box_container(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxc", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    return result;
}

// Generic box: given a value register and its TypeId, emit the appropriate boxing
static MIR_reg_t emit_box(MirTranspiler* mt, MIR_reg_t val_reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:
        return emit_box_int(mt, val_reg);
    case LMD_TYPE_FLOAT:
        return emit_box_float(mt, val_reg);
    case LMD_TYPE_BOOL:
        return emit_box_bool(mt, val_reg);
    case LMD_TYPE_INT64:
        return emit_box_int64(mt, val_reg);
    case LMD_TYPE_STRING:
        return emit_box_string(mt, val_reg);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, val_reg);
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_LIST: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_RANGE: case LMD_TYPE_FUNC:
    case LMD_TYPE_TYPE: case LMD_TYPE_PATH:
        return emit_box_container(mt, val_reg);
    case LMD_TYPE_NULL:
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
    default:
        // Already boxed Item or NULL
        return val_reg;
    }
}

// Unbox Item -> native type
static MIR_reg_t emit_unbox(MirTranspiler* mt, MIR_reg_t item_reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:
        return emit_call_1(mt, "it2i", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_FLOAT:
        return emit_call_1(mt, "it2d", MIR_T_D, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_BOOL:
        return emit_call_1(mt, "it2b", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_STRING:
        return emit_call_1(mt, "it2s", MIR_T_P, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_INT64:
        return emit_call_1(mt, "it2l", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    default:
        return item_reg;
    }
}

// ============================================================================
// Literal value extraction from source text
// ============================================================================

static int64_t parse_int_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    int end = ts_node_end_byte(node);
    const char* text = source + start;
    int len = end - start;

    // Copy to null-terminated buffer
    char buf[128];
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';

    // Handle hex (0x), octal (0o), binary (0b)
    if (len > 2 && buf[0] == '0') {
        if (buf[1] == 'x' || buf[1] == 'X') return strtoll(buf, NULL, 16);
        if (buf[1] == 'o' || buf[1] == 'O') return strtoll(buf + 2, NULL, 8);
        if (buf[1] == 'b' || buf[1] == 'B') return strtoll(buf + 2, NULL, 2);
    }

    // Remove underscores (1_000_000 -> 1000000)
    char clean[128];
    int ci = 0;
    for (int i = 0; i < len && ci < (int)sizeof(clean) - 1; i++) {
        if (buf[i] != '_') clean[ci++] = buf[i];
    }
    clean[ci] = '\0';

    return strtoll(clean, NULL, 10);
}

static double parse_float_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    int end = ts_node_end_byte(node);
    const char* text = source + start;
    int len = end - start;

    char buf[128];
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    // Remove underscores
    int ci = 0;
    for (int i = 0; i < len && ci < (int)sizeof(buf) - 1; i++) {
        if (text[i] != '_') buf[ci++] = text[i];
    }
    buf[ci] = '\0';
    return strtod(buf, NULL);
}

static bool parse_bool_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    return source[start] == 't';
}

// ============================================================================
// Load constant from rt->consts[index]
// ============================================================================

static MIR_reg_t emit_load_const(MirTranspiler* mt, int const_index, MIR_type_t as_type) {
    // consts_reg points to rt->consts (void**)
    // Load consts[index] = *(consts_reg + index*8)
    MIR_reg_t ptr = new_reg(mt, "cptr", MIR_T_P);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_P, const_index * 8, mt->consts_reg, 0, 1)));
    return ptr;
}

// Load const and box as Item based on TypeId
static MIR_reg_t emit_load_const_boxed(MirTranspiler* mt, int const_index, TypeId type_id) {
    MIR_reg_t ptr = emit_load_const(mt, const_index, MIR_T_P);
    switch (type_id) {
    case LMD_TYPE_STRING:
        return emit_box_string(mt, ptr);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, ptr);
    case LMD_TYPE_FLOAT: {
        // d2it(ptr) = ptr ? (FLOAT_TAG | (uint64_t)ptr) : ITEM_NULL
        MIR_reg_t result = new_reg(mt, "boxd", MIR_T_I64);
        uint64_t FLOAT_TAG = (uint64_t)LMD_TYPE_FLOAT << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)FLOAT_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_INT64: {
        // l2it(ptr) = ptr ? (INT64_TAG | (uint64_t)ptr) : ITEM_NULL
        MIR_reg_t result = new_reg(mt, "boxl", MIR_T_I64);
        uint64_t INT64_TAG = (uint64_t)LMD_TYPE_INT64 << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)INT64_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_DTIME: {
        // k2it(ptr)
        MIR_reg_t result = new_reg(mt, "boxk", MIR_T_I64);
        uint64_t DTIME_TAG = (uint64_t)LMD_TYPE_DTIME << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)DTIME_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_DECIMAL: {
        MIR_reg_t result = new_reg(mt, "boxdc", MIR_T_I64);
        uint64_t DEC_TAG = (uint64_t)LMD_TYPE_DECIMAL << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)DEC_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_BINARY: {
        MIR_reg_t result = new_reg(mt, "boxx", MIR_T_I64);
        uint64_t BIN_TAG = (uint64_t)LMD_TYPE_BINARY << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)BIN_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    default:
        // Direct cast for containers
        return ptr;
    }
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node);
static MIR_reg_t transpile_box_item(MirTranspiler* mt, AstNode* node);
static void transpile_let_stam(MirTranspiler* mt, AstLetNode* let_node);
static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node);

// ============================================================================
// Expression transpilation
// ============================================================================

static MIR_reg_t transpile_primary(MirTranspiler* mt, AstPrimaryNode* pri) {
    if (pri->expr) {
        return transpile_expr(mt, pri->expr);
    }

    AstNode* node = (AstNode*)pri;
    if (!node->type) {
        log_error("mir: primary node has null type");
        MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }

    TypeId tid = node->type->type_id;

    if (node->type->is_literal) {
        switch (tid) {
        case LMD_TYPE_INT: {
            int64_t val = parse_int_literal(mt->source, node->node);
            MIR_reg_t r = new_reg(mt, "int", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, val)));
            return r;
        }
        case LMD_TYPE_FLOAT: {
            // Float literals are stored in const_list, load from there
            TypeConst* tc = (TypeConst*)node->type;
            MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
            // Dereference the double* to get the actual double value
            MIR_reg_t r = new_reg(mt, "flt", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LDMOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_mem_op(mt->ctx, MIR_T_D, 0, ptr, 0, 1)));
            return r;
        }
        case LMD_TYPE_BOOL: {
            bool val = parse_bool_literal(mt->source, node->node);
            MIR_reg_t r = new_reg(mt, "bool", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, val ? 1 : 0)));
            return r;
        }
        case LMD_TYPE_NULL: {
            MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
        case LMD_TYPE_STRING: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_SYMBOL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_INT64: {
            TypeConst* tc = (TypeConst*)node->type;
            MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
            // Dereference to get int64_t value
            MIR_reg_t r = new_reg(mt, "i64", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ptr, 0, 1)));
            return r;
        }
        case LMD_TYPE_DTIME: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_DECIMAL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_BINARY: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        default: {
            log_error("mir: unhandled literal type %d", tid);
            MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        }
    }

    // Non-literal primary (shouldn't happen - primaries are either literal or have expr)
    log_error("mir: non-literal primary without expr, type %d", tid);
    MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, 0)));
    return r;
}

static MIR_reg_t transpile_ident(MirTranspiler* mt, AstIdentNode* ident) {
    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)ident->name->len, ident->name->chars);

    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        return var->reg;
    }

    // Check if this is a reference to a function (for first-class function usage)
    if (ident->entry && ident->entry->node) {
        AstNode* entry_node = ident->entry->node;
        if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR) {
            // Function reference - return null for now (closures handled later)
            log_debug("mir: function reference '%s' - not yet fully supported", name_buf);
        }
    }

    log_error("mir: undefined variable '%s'", name_buf);
    MIR_reg_t r = new_reg(mt, "undef", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, 0)));
    return r;
}

// ============================================================================
// Binary expressions
// ============================================================================

static MIR_reg_t transpile_binary(MirTranspiler* mt, AstBinaryNode* bi) {
    TypeId left_tid = bi->left->type ? bi->left->type->type_id : LMD_TYPE_ANY;
    TypeId right_tid = bi->right->type ? bi->right->type->type_id : LMD_TYPE_ANY;
    TypeId result_tid = ((AstNode*)bi)->type ? ((AstNode*)bi)->type->type_id : LMD_TYPE_ANY;

    // Type-dispatch: if both sides are native types, use native MIR ops
    bool left_int = (left_tid == LMD_TYPE_INT);
    bool right_int = (right_tid == LMD_TYPE_INT);
    bool left_float = (left_tid == LMD_TYPE_FLOAT);
    bool right_float = (right_tid == LMD_TYPE_FLOAT);
    bool both_int = left_int && right_int;
    bool both_float = left_float && right_float;
    bool int_float = (left_int && right_float) || (left_float && right_int);

    // Arithmetic ops with native types
    if (both_int || both_float || int_float) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);

        bool use_float = both_float || int_float;
        MIR_reg_t fl = left, fr = right;

        // Convert int to float if needed
        if (int_float) {
            if (left_int && right_float) {
                fl = new_reg(mt, "i2d", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fl),
                    MIR_new_reg_op(mt->ctx, left)));
                fr = right;
            } else {
                fl = left;
                fr = new_reg(mt, "i2d", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fr),
                    MIR_new_reg_op(mt->ctx, right)));
            }
        }

        MIR_type_t rtype = use_float ? MIR_T_D : MIR_T_I64;

        switch (bi->op) {
        case OPERATOR_ADD: {
            MIR_reg_t r = new_reg(mt, "add", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DADD : MIR_ADD,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_SUB: {
            MIR_reg_t r = new_reg(mt, "sub", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DSUB : MIR_SUB,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_MUL: {
            MIR_reg_t r = new_reg(mt, "mul", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DMUL : MIR_MUL,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_DIV: {
            if (both_int) {
                // int / int -> float in Lambda
                MIR_reg_t fl2 = new_reg(mt, "i2d", MIR_T_D);
                MIR_reg_t fr2 = new_reg(mt, "i2d", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fl2),
                    MIR_new_reg_op(mt->ctx, left)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fr2),
                    MIR_new_reg_op(mt->ctx, right)));
                MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, fl2), MIR_new_reg_op(mt->ctx, fr2)));
                return r;
            }
            MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_IDIV: {
            if (both_int) {
                MIR_reg_t r = new_reg(mt, "idiv", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, left), MIR_new_reg_op(mt->ctx, right)));
                return r;
            }
            // Boxed fallback
            break;
        }
        case OPERATOR_MOD: {
            if (both_int) {
                MIR_reg_t r = new_reg(mt, "mod", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOD, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, left), MIR_new_reg_op(mt->ctx, right)));
                return r;
            }
            break;
        }
        case OPERATOR_POW: {
            if (both_int) {
                // Use fn_pow_u for int^int
                return emit_call_2(mt, "fn_pow_u", MIR_T_D, MIR_T_D,
                    MIR_new_reg_op(mt->ctx, fl), MIR_T_D, MIR_new_reg_op(mt->ctx, fr));
            }
            break;
        }
        // Comparison operators
        case OPERATOR_EQ: {
            MIR_reg_t r = new_reg(mt, "eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DEQ : MIR_EQ,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_NE: {
            MIR_reg_t r = new_reg(mt, "ne", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DNE : MIR_NE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_LT: {
            MIR_reg_t r = new_reg(mt, "lt", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLT : MIR_LT,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_LE: {
            MIR_reg_t r = new_reg(mt, "le", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLE : MIR_LE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_GT: {
            MIR_reg_t r = new_reg(mt, "gt", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGT : MIR_GT,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_GE: {
            MIR_reg_t r = new_reg(mt, "ge", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGE : MIR_GE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        default:
            break;  // fall through to boxed path
        }
    }

    // String concatenation
    if (bi->op == OPERATOR_JOIN && left_tid == LMD_TYPE_STRING && right_tid == LMD_TYPE_STRING) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);
        return emit_call_2(mt, "fn_strcat", MIR_T_P, MIR_T_P,
            MIR_new_reg_op(mt->ctx, left), MIR_T_P, MIR_new_reg_op(mt->ctx, right));
    }

    // Short-circuit AND/OR
    if (bi->op == OPERATOR_AND) {
        MIR_reg_t result = new_reg(mt, "and", MIR_T_I64);
        MIR_reg_t left_val = transpile_expr(mt, bi->left);

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            // native bool AND
            MIR_label_t l_false = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
                MIR_new_reg_op(mt->ctx, left_val)));
            MIR_reg_t right_val = transpile_expr(mt, bi->right);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, right_val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            emit_label(mt, l_false);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            emit_label(mt, l_end);
            return result;
        }
        // Boxed AND: use op_and(left, right) — but op_and already short-circuits in the runtime
        // Actually this won't short-circuit; for boxed, use is_truthy + branch
        MIR_reg_t boxed_left = emit_box(mt, left_val, left_tid);
        MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
        MIR_label_t l_false = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t right_val = transpile_expr(mt, bi->right);
        MIR_reg_t boxed_right = emit_box(mt, right_val, right_tid);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_right)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_false);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_left)));
        emit_label(mt, l_end);
        return result;
    }

    if (bi->op == OPERATOR_OR) {
        MIR_reg_t result = new_reg(mt, "or", MIR_T_I64);
        MIR_reg_t left_val = transpile_expr(mt, bi->left);

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            MIR_label_t l_true = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
                MIR_new_reg_op(mt->ctx, left_val)));
            MIR_reg_t right_val = transpile_expr(mt, bi->right);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, right_val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            emit_label(mt, l_true);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 1)));
            emit_label(mt, l_end);
            return result;
        }
        MIR_reg_t boxed_left = emit_box(mt, left_val, left_tid);
        MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
        MIR_label_t l_true = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t right_val = transpile_expr(mt, bi->right);
        MIR_reg_t boxed_right = emit_box(mt, right_val, right_tid);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_right)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_true);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_left)));
        emit_label(mt, l_end);
        return result;
    }

    // Range operator (a to b)
    if (bi->op == OPERATOR_TO) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);
        MIR_reg_t boxl = emit_box(mt, left, left_tid);
        MIR_reg_t boxr = emit_box(mt, right, right_tid);
        // fn_range3(start, end, step=1) for simple ranges
        MIR_reg_t step = new_reg(mt, "step", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, step),
            MIR_new_int_op(mt->ctx, (int64_t)((uint64_t)LMD_TYPE_NULL << 56))));
        return emit_call_3(mt, "fn_range3", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step));
    }

    // Type operators
    if (bi->op == OPERATOR_IS) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);
        MIR_reg_t boxl = emit_box(mt, left, left_tid);
        MIR_reg_t boxr = emit_box(mt, right, right_tid);
        return emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }
    if (bi->op == OPERATOR_IN) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);
        MIR_reg_t boxl = emit_box(mt, left, left_tid);
        MIR_reg_t boxr = emit_box(mt, right, right_tid);
        return emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // Fallback: box both sides and call runtime function
    MIR_reg_t left = transpile_expr(mt, bi->left);
    MIR_reg_t right = transpile_expr(mt, bi->right);
    MIR_reg_t boxl = emit_box(mt, left, left_tid);
    MIR_reg_t boxr = emit_box(mt, right, right_tid);

    const char* fn_name = NULL;
    switch (bi->op) {
    case OPERATOR_ADD: fn_name = "fn_add"; break;
    case OPERATOR_JOIN: fn_name = "fn_strcat"; break;
    case OPERATOR_SUB: fn_name = "fn_sub"; break;
    case OPERATOR_MUL: fn_name = "fn_mul"; break;
    case OPERATOR_DIV: fn_name = "fn_div"; break;
    case OPERATOR_IDIV: fn_name = "fn_idiv"; break;
    case OPERATOR_MOD: fn_name = "fn_mod"; break;
    case OPERATOR_POW: fn_name = "fn_pow"; break;
    case OPERATOR_EQ: fn_name = "fn_eq"; break;
    case OPERATOR_NE: fn_name = "fn_ne"; break;
    case OPERATOR_LT: fn_name = "fn_lt"; break;
    case OPERATOR_LE: fn_name = "fn_le"; break;
    case OPERATOR_GT: fn_name = "fn_gt"; break;
    case OPERATOR_GE: fn_name = "fn_ge"; break;
    default:
        log_error("mir: unhandled binary op %d", bi->op);
        return boxl;
    }

    return emit_call_2(mt, fn_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
}

// ============================================================================
// Unary expressions
// ============================================================================

static MIR_reg_t transpile_unary(MirTranspiler* mt, AstUnaryNode* un) {
    TypeId operand_tid = un->operand->type ? un->operand->type->type_id : LMD_TYPE_ANY;

    MIR_reg_t operand = transpile_expr(mt, un->operand);

    switch (un->op) {
    case OPERATOR_NEG: {
        if (operand_tid == LMD_TYPE_INT) {
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_NEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        if (operand_tid == LMD_TYPE_FLOAT) {
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DNEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        MIR_reg_t boxed = emit_box(mt, operand, operand_tid);
        return emit_call_1(mt, "fn_neg", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_NOT: {
        if (operand_tid == LMD_TYPE_BOOL) {
            MIR_reg_t r = new_reg(mt, "not", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand), MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        MIR_reg_t boxed = emit_box(mt, operand, operand_tid);
        return emit_call_1(mt, "fn_not", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_POS:
        return operand;
    case OPERATOR_IS_ERROR: {
        // ^expr: check if Item type is LMD_TYPE_ERROR
        MIR_reg_t boxed = emit_box(mt, operand, operand_tid);
        MIR_reg_t type_reg = new_reg(mt, "tid", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, type_reg),
            MIR_new_reg_op(mt->ctx, boxed), MIR_new_int_op(mt->ctx, 56)));
        MIR_reg_t r = new_reg(mt, "iserr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, r),
            MIR_new_reg_op(mt->ctx, type_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
        return r;
    }
    default:
        log_error("mir: unhandled unary op %d", un->op);
        return operand;
    }
}

// ============================================================================
// Spread expression
// ============================================================================

static MIR_reg_t transpile_spread(MirTranspiler* mt, AstUnaryNode* spread) {
    MIR_reg_t operand = transpile_expr(mt, spread->operand);
    TypeId tid = spread->operand->type ? spread->operand->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed = emit_box(mt, operand, tid);
    return emit_call_1(mt, "item_spread", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
}

// ============================================================================
// If/else expressions
// ============================================================================

static MIR_reg_t transpile_if(MirTranspiler* mt, AstIfNode* if_node) {
    TypeId cond_tid = if_node->cond->type ? if_node->cond->type->type_id : LMD_TYPE_ANY;

    MIR_reg_t cond = transpile_expr(mt, if_node->cond);

    // For non-bool condition, use is_truthy
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }

    MIR_type_t result_type = node_mir_type((AstNode*)if_node);
    MIR_reg_t result = new_reg(mt, "if_res", result_type);
    MIR_label_t l_else = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Then branch
    if (if_node->then) {
        MIR_reg_t then_val = transpile_expr(mt, if_node->then);
        if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        }
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Else branch
    emit_label(mt, l_else);
    if (if_node->otherwise) {
        MIR_reg_t else_val = transpile_expr(mt, if_node->otherwise);
        if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        }
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }

    emit_label(mt, l_end);
    return result;
}

// ============================================================================
// Match expression
// ============================================================================

static MIR_reg_t transpile_match(MirTranspiler* mt, AstMatchNode* match_node) {
    MIR_reg_t scrutinee = transpile_expr(mt, match_node->scrutinee);
    TypeId scrut_tid = match_node->scrutinee->type ? match_node->scrutinee->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_scrut = emit_box(mt, scrutinee, scrut_tid);

    MIR_reg_t result = new_reg(mt, "match", MIR_T_I64);
    MIR_label_t l_end = new_label(mt);

    AstMatchArm* arm = match_node->first_arm;
    while (arm) {
        if (arm->pattern) {
            // Test: fn_is(scrutinee, pattern)
            MIR_reg_t pattern = transpile_expr(mt, arm->pattern);
            TypeId pat_tid = arm->pattern->type ? arm->pattern->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_pat = emit_box(mt, pattern, pat_tid);

            MIR_reg_t match_test = emit_call_2(mt, "fn_is", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat));

            MIR_label_t l_next = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_next),
                MIR_new_reg_op(mt->ctx, match_test)));

            // Body
            MIR_reg_t body = transpile_expr(mt, arm->body);
            TypeId body_tid = arm->body->type ? arm->body->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_body = emit_box(mt, body, body_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed_body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            emit_label(mt, l_next);
        } else {
            // Default arm
            MIR_reg_t body = transpile_expr(mt, arm->body);
            TypeId body_tid = arm->body->type ? arm->body->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_body = emit_box(mt, body, body_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed_body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        }
        arm = (AstMatchArm*)arm->next;
    }

    // No match - return ITEM_NULL
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));

    emit_label(mt, l_end);
    return result;
}

// ============================================================================
// For expressions
// ============================================================================

static MIR_reg_t transpile_for(MirTranspiler* mt, AstForNode* for_node) {
    push_scope(mt);

    AstLoopNode* loop = (AstLoopNode*)for_node->loop;
    if (!loop) {
        log_error("mir: for without loop");
        pop_scope(mt);
        MIR_reg_t r = new_reg(mt, "fornull", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }

    // Evaluate collection
    MIR_reg_t collection = transpile_expr(mt, loop->as);
    TypeId coll_tid = loop->as->type ? loop->as->type->type_id : LMD_TYPE_ANY;

    // Box the collection for fn_len / item_at
    MIR_reg_t boxed_coll = emit_box(mt, collection, coll_tid);

    // Get length
    MIR_reg_t len = emit_call_1(mt, "fn_len", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));

    // Create output array
    MIR_reg_t output = emit_call_0(mt, "array", MIR_T_P);

    // Index counter
    MIR_reg_t idx = new_reg(mt, "idx", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_continue = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    // Push loop labels for break/continue
    if (mt->loop_depth < 31) {
        mt->loop_stack[mt->loop_depth].continue_label = l_continue;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    emit_label(mt, l_loop);
    // Exit when idx >= len
    MIR_reg_t cmp = new_reg(mt, "cmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get current item: item_at(collection, idx)
    MIR_reg_t current_item = emit_call_2(mt, "item_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));

    // Bind loop variable
    char var_name[128];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)loop->name->len, loop->name->chars);
    TypeId var_tid = loop->type ? loop->type->type_id : LMD_TYPE_ANY;

    // The item_at returns Item, unbox if needed
    MIR_reg_t var_reg;
    if (var_tid == LMD_TYPE_INT || var_tid == LMD_TYPE_FLOAT || var_tid == LMD_TYPE_BOOL || var_tid == LMD_TYPE_STRING) {
        var_reg = emit_unbox(mt, current_item, var_tid);
    } else {
        var_reg = current_item;
        var_tid = LMD_TYPE_ANY;
    }
    MIR_type_t var_mir = type_to_mir(var_tid);
    set_var(mt, var_name, var_reg, var_mir, var_tid);

    // Bind index variable if present
    if (loop->index_name) {
        char idx_name[128];
        snprintf(idx_name, sizeof(idx_name), "%.*s", (int)loop->index_name->len, loop->index_name->chars);
        set_var(mt, idx_name, idx, MIR_T_I64, LMD_TYPE_INT);
    }

    // Where clause
    if (for_node->where) {
        MIR_reg_t where_val = transpile_expr(mt, for_node->where);
        TypeId where_tid = for_node->where->type ? for_node->where->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t where_test = where_val;
        if (where_tid != LMD_TYPE_BOOL) {
            MIR_reg_t boxw = emit_box(mt, where_val, where_tid);
            where_test = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxw));
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_continue),
            MIR_new_reg_op(mt->ctx, where_test)));
    }

    // Body expression
    MIR_reg_t body_result = transpile_expr(mt, for_node->then);
    TypeId body_tid = for_node->then->type ? for_node->then->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_result = emit_box(mt, body_result, body_tid);

    // Push to output
    emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

    // Continue: increment index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);

    // Finalize array
    MIR_reg_t final = emit_call_1(mt, "array_end", MIR_T_P, MIR_T_P, MIR_new_reg_op(mt->ctx, output));

    if (mt->loop_depth > 0) mt->loop_depth--;
    pop_scope(mt);
    return final;
}

// ============================================================================
// While statement
// ============================================================================

static MIR_reg_t transpile_while(MirTranspiler* mt, AstWhileNode* while_node) {
    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    if (mt->loop_depth < 31) {
        mt->loop_stack[mt->loop_depth].continue_label = l_loop;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    push_scope(mt);

    emit_label(mt, l_loop);

    // Condition
    MIR_reg_t cond = transpile_expr(mt, while_node->cond);
    TypeId cond_tid = while_node->cond->type ? while_node->cond->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Body
    transpile_expr(mt, while_node->body);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));
    emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
    pop_scope(mt);

    MIR_reg_t r = new_reg(mt, "while_null", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    return r;
}

// ============================================================================
// Let/pub statements
// ============================================================================

static void transpile_let_stam(MirTranspiler* mt, AstLetNode* let_node) {
    AstNode* declare = let_node->declare;
    while (declare) {
        if (declare->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* asn = (AstNamedNode*)declare;
            if (asn->as) {
                MIR_reg_t val = transpile_expr(mt, asn->as);
                char name_buf[128];
                snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                TypeId tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
                MIR_type_t mtype = type_to_mir(tid);

                // Store in current scope
                set_var(mt, name_buf, val, mtype, tid);
            }
        } else if (declare->node_type == AST_NODE_FUNC || declare->node_type == AST_NODE_FUNC_EXPR ||
                   declare->node_type == AST_NODE_PROC) {
            // Function definition inside let - handled separately
            // For now, skip
        }
        declare = declare->next;
    }
}

// ============================================================================
// Array expressions
// ============================================================================

static MIR_reg_t transpile_array(MirTranspiler* mt, AstArrayNode* arr_node) {
    // Determine array type
    TypeId elem_tid = LMD_TYPE_ANY;
    if (arr_node->type && arr_node->type->type_id == LMD_TYPE_ARRAY_INT) {
        elem_tid = LMD_TYPE_INT;
    }

    MIR_reg_t arr = emit_call_0(mt, "array", MIR_T_P);

    AstNode* item = arr_node->item;
    while (item) {
        MIR_reg_t val = transpile_expr(mt, item);
        TypeId val_tid = item->type ? item->type->type_id : LMD_TYPE_ANY;

        if (item->node_type == AST_NODE_SPREAD) {
            // Spread: use array_push_spread
            emit_call_void_2(mt, "array_push_spread",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else {
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_call_void_2(mt, "array_push",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        }
        item = item->next;
    }

    return emit_call_1(mt, "array_end", MIR_T_P, MIR_T_P, MIR_new_reg_op(mt->ctx, arr));
}

// ============================================================================
// List/content expressions
// ============================================================================

static MIR_reg_t transpile_list(MirTranspiler* mt, AstListNode* list_node) {
    MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);

    AstNode* item = list_node->item;
    while (item) {
        // Skip declarations
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_FUNC ||
            item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;
            continue;
        }
        MIR_reg_t val = transpile_expr(mt, item);
        TypeId val_tid = item->type ? item->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        emit_call_void_2(mt, "list_push_spread",
            MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        item = item->next;
    }

    return emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
}

static MIR_reg_t transpile_content(MirTranspiler* mt, AstListNode* list_node) {
    // Count effective (non-declaration) items
    AstNode* scan = list_node->item;
    int decl_count = 0, value_count = 0;
    AstNode* last_value = nullptr;
    while (scan) {
        if (scan->node_type == AST_NODE_LET_STAM || scan->node_type == AST_NODE_PUB_STAM ||
            scan->node_type == AST_NODE_TYPE_STAM || scan->node_type == AST_NODE_FUNC ||
            scan->node_type == AST_NODE_FUNC_EXPR || scan->node_type == AST_NODE_PROC ||
            scan->node_type == AST_NODE_STRING_PATTERN || scan->node_type == AST_NODE_SYMBOL_PATTERN) {
            decl_count++;
        } else {
            value_count++;
            last_value = scan;
        }
        scan = scan->next;
    }

    // Single value with declarations: block expression
    if (value_count == 1 && last_value && decl_count > 0) {
        push_scope(mt);
        // Process declarations
        AstNode* item = list_node->item;
        while (item) {
            if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
                transpile_let_stam(mt, (AstLetNode*)item);
            }
            item = item->next;
        }
        MIR_reg_t result = transpile_box_item(mt, last_value);
        pop_scope(mt);
        return result;
    }

    // Single value without declarations: just return it boxed
    if (value_count == 1 && last_value && decl_count == 0) {
        return transpile_box_item(mt, last_value);
    }

    // Multiple values: build a list
    push_scope(mt);

    // Process declarations first
    AstNode* item = list_node->item;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || item->node_type == AST_NODE_TYPE_STAM) {
            transpile_let_stam(mt, (AstLetNode*)item);
        }
        item = item->next;
    }

    // If no value items, return empty list
    if (value_count == 0) {
        MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
        MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
        pop_scope(mt);
        return result;
    }

    // Build list with values
    MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
    item = list_node->item;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_FUNC ||
            item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;
            continue;
        }
        MIR_reg_t val = transpile_box_item(mt, item);
        emit_call_void_2(mt, "list_push_spread",
            MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        item = item->next;
    }

    MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
    pop_scope(mt);
    return result;
}

// ============================================================================
// Map expressions
// ============================================================================

static MIR_reg_t transpile_map(MirTranspiler* mt, AstMapNode* map_node) {
    int type_index = ((TypeMap*)map_node->type)->type_index;

    MIR_reg_t m = emit_call_1(mt, "map", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, type_index));

    // Fill map with key:value pairs
    // map_fill takes (Map*, Item val1, Item val2, ..., 0) — but variadic is hard in MIR
    // Instead use fn_map_set for each field
    AstNode* item = map_node->item;
    while (item) {
        if (item->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* key_expr = (AstNamedNode*)item;
            if (key_expr->as) {
                MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                // Use the key name from the AST - fn_map_set(map, key_str, value)
                // Actually the C transpiler uses map_fill(m, val1, val2, ...) for fixed maps
                // For now, call map_fill approach later. Use raw pointer stores as fallback.
                // TODO: implement proper map filling
                // For now, push values sequentially (map_fill pattern with variadic)
                emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, m),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
        } else {
            MIR_reg_t val = transpile_box_item(mt, item);
            emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, m),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        item = item->next;
    }

    return m;
}

// ============================================================================
// Member/Index access
// ============================================================================

static MIR_reg_t transpile_member(MirTranspiler* mt, AstFieldNode* field_node) {
    MIR_reg_t obj = transpile_expr(mt, field_node->object);
    TypeId obj_tid = field_node->object->type ? field_node->object->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_obj = emit_box(mt, obj, obj_tid);

    // Get field name - field_node->field is an ident
    MIR_reg_t field = transpile_expr(mt, field_node->field);
    TypeId field_tid = field_node->field->type ? field_node->field->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_field = emit_box(mt, field, field_tid);

    return emit_call_2(mt, "fn_member", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_field));
}

static MIR_reg_t transpile_index(MirTranspiler* mt, AstFieldNode* field_node) {
    MIR_reg_t obj = transpile_expr(mt, field_node->object);
    TypeId obj_tid = field_node->object->type ? field_node->object->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_obj = emit_box(mt, obj, obj_tid);

    MIR_reg_t idx = transpile_expr(mt, field_node->field);
    TypeId idx_tid = field_node->field->type ? field_node->field->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_idx = emit_box(mt, idx, idx_tid);

    return emit_call_2(mt, "fn_index", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
}

// ============================================================================
// Call expressions
// ============================================================================

static MIR_reg_t transpile_call(MirTranspiler* mt, AstCallNode* call_node) {
    AstNode* fn_expr = call_node->function;

    // Check for system function calls
    if (fn_expr->node_type == AST_NODE_SYS_FUNC) {
        AstSysFuncNode* sys = (AstSysFuncNode*)fn_expr;
        SysFuncInfo* info = sys->fn_info;

        // Count arguments
        AstNode* arg = call_node->argument;
        int arg_count = 0;
        while (arg) { arg_count++; arg = arg->next; }

        // Build runtime function name: "fn_" or "pn_" + name + optional arg count for overloaded
        char sys_fn_name[128];
        if (info->is_overloaded) {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s%d",
                info->is_proc ? "pn_" : "fn_", info->name, arg_count);
        } else {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s",
                info->is_proc ? "pn_" : "fn_", info->name);
        }

        // For 0-arg functions like datetime(), date() etc
        if (arg_count == 0) {
            return emit_call_0(mt, sys_fn_name, MIR_T_I64);
        }

        // For 1-arg system functions
        if (arg_count == 1) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);
            return emit_call_1(mt, sys_fn_name, MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
        }

        // For 2-arg system functions
        if (arg_count == 2) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);

            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a2 = emit_box(mt, a2, a2_tid);

            return emit_call_2(mt, sys_fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
        }

        // 3-arg system functions
        if (arg_count == 3) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);

            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a2 = emit_box(mt, a2, a2_tid);

            arg = arg->next;
            MIR_reg_t a3 = transpile_expr(mt, arg);
            TypeId a3_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a3 = emit_box(mt, a3, a3_tid);

            return emit_call_3(mt, sys_fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
        }

        // Fallback for more args: not yet supported
        log_error("mir: system function with %d args not yet supported: %s", arg_count, sys_fn_name);
        MIR_reg_t r = new_reg(mt, "sys_err", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }

    // User-defined function call
    if (fn_expr->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)fn_expr;
        if (pri->expr) fn_expr = pri->expr;
    }

    if (fn_expr->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)fn_expr;
        AstNode* entry_node = ident->entry ? ident->entry->node : nullptr;

        if (entry_node && (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC)) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Build function name using write_fn_name
            StrBuf* name_buf = strbuf_new_cap(64);
            write_fn_name(name_buf, fn_node, ident->entry->import);
            const char* fn_mangled = name_buf->str;

            // Count args and build arg list
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) { arg_count++; arg = arg->next; }

            // Evaluate arguments - always box for user functions (all params are Item)
            MIR_op_t arg_ops[16];
            MIR_var_t arg_vars[16];
            int ai = 0;
            arg = call_node->argument;
            while (arg && ai < 16) {
                MIR_reg_t val = transpile_expr(mt, arg);
                TypeId arg_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
                val = emit_box(mt, val, arg_tid);
                arg_ops[ai] = MIR_new_reg_op(mt->ctx, val);
                arg_vars[ai] = {MIR_T_I64, "p", 0};
                ai++;
                arg = arg->next;
            }

            // Return type is always Item (MIR_T_I64) for user functions
            MIR_type_t ret_type = MIR_T_I64;

            // Check if this is a local function (defined in same module)
            MIR_item_t local_func = find_local_func(mt, fn_mangled);

            // Create proto for the call (unique name per call site)
            char proto_name[160];
            snprintf(proto_name, sizeof(proto_name), "%s_cp%d", fn_mangled, mt->label_counter++);
            MIR_type_t res_types[1] = { ret_type };
            MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, res_types, ai, arg_vars);

            MIR_op_t func_op;
            if (local_func) {
                // Local function: use direct reference
                func_op = MIR_new_ref_op(mt->ctx, local_func);
            } else {
                // External function: create import
                MIR_item_t imp = MIR_new_import(mt->ctx, fn_mangled);
                func_op = MIR_new_ref_op(mt->ctx, imp);
            }

            // Build call instruction
            int nops = 3 + ai;
            MIR_op_t ops[19]; // 3 + max 16 args
            ops[0] = MIR_new_ref_op(mt->ctx, proto);
            ops[1] = func_op;
            MIR_reg_t result = new_reg(mt, "call", ret_type);
            ops[2] = MIR_new_reg_op(mt->ctx, result);
            for (int i = 0; i < ai; i++) ops[3 + i] = arg_ops[i];

            emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));

            strbuf_free(name_buf);
            return result;
        }
    }

    // Dynamic call via fn_call
    log_debug("mir: dynamic call - using fn_call");
    MIR_reg_t fn_val = transpile_expr(mt, call_node->function);
    TypeId fn_tid = call_node->function->type ? call_node->function->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_fn = emit_box(mt, fn_val, fn_tid);

    // Count and box args
    AstNode* arg = call_node->argument;
    int arg_count = 0;
    while (arg) { arg_count++; arg = arg->next; }

    const char* call_fn = NULL;
    switch (arg_count) {
    case 0: call_fn = "fn_call0"; break;
    case 1: call_fn = "fn_call1"; break;
    case 2: call_fn = "fn_call2"; break;
    case 3: call_fn = "fn_call3"; break;
    default: call_fn = "fn_call"; break;
    }

    if (arg_count == 0) {
        return emit_call_1(mt, call_fn, MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn));
    }

    if (arg_count <= 3) {
        MIR_reg_t args[3];
        arg = call_node->argument;
        for (int i = 0; i < arg_count; i++) {
            MIR_reg_t v = transpile_expr(mt, arg);
            TypeId tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            args[i] = emit_box(mt, v, tid);
            arg = arg->next;
        }
        if (arg_count == 1) {
            return emit_call_2(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]));
        }
        if (arg_count == 2) {
            return emit_call_3(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[1]));
        }
        // 3 args
        MIR_var_t avars[4] = {{MIR_T_I64,"f",0},{MIR_T_I64,"a",0},{MIR_T_I64,"b",0},{MIR_T_I64,"c",0}};
        MirImportEntry* ie = ensure_import(mt, call_fn, MIR_T_I64, 4, avars, 1);
        MIR_reg_t result = new_reg(mt, "call3", MIR_T_I64);
        emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_fn),
            MIR_new_reg_op(mt->ctx, args[0]),
            MIR_new_reg_op(mt->ctx, args[1]),
            MIR_new_reg_op(mt->ctx, args[2])));
        return result;
    }

    // More than 3 args: use fn_call with list
    log_error("mir: calls with >3 args not yet fully supported");
    return boxed_fn;
}

// ============================================================================
// Pipe expressions
// ============================================================================

static MIR_reg_t transpile_pipe(MirTranspiler* mt, AstPipeNode* pipe_node) {
    MIR_reg_t left = transpile_expr(mt, pipe_node->left);
    TypeId left_tid = pipe_node->left->type ? pipe_node->left->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_left = emit_box(mt, left, left_tid);

    // Check if right side is a simple function call that should get data injected
    if (pipe_node->op == OPERATOR_PIPE) {
        // Aggregate pipe: data | func  ->  func(data)
        MIR_reg_t right = transpile_expr(mt, pipe_node->right);
        TypeId right_tid = pipe_node->right->type ? pipe_node->right->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed_right = emit_box(mt, right, right_tid);

        // Use fn_pipe_call(data, func)
        return emit_call_2(mt, "fn_pipe_call", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    if (pipe_node->op == OPERATOR_WHERE) {
        // where filter: data where predicate
        MIR_reg_t right = transpile_expr(mt, pipe_node->right);
        TypeId right_tid = pipe_node->right->type ? pipe_node->right->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed_right = emit_box(mt, right, right_tid);

        return emit_call_2(mt, "fn_pipe_where", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Default
    MIR_reg_t right = transpile_expr(mt, pipe_node->right);
    TypeId right_tid = pipe_node->right->type ? pipe_node->right->type->type_id : LMD_TYPE_ANY;
    return emit_box(mt, right, right_tid);
}

// ============================================================================
// Raise expressions
// ============================================================================

static MIR_reg_t transpile_raise(MirTranspiler* mt, AstRaiseNode* raise_node) {
    if (raise_node->value) {
        MIR_reg_t val = transpile_expr(mt, raise_node->value);
        TypeId val_tid = raise_node->value->type ? raise_node->value->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        // Convert to error
        return emit_call_1(mt, "fn_error", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    MIR_reg_t r = new_reg(mt, "err", MIR_T_I64);
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    return r;
}

// ============================================================================
// Return statement
// ============================================================================

static MIR_reg_t transpile_return(MirTranspiler* mt, AstReturnNode* ret_node) {
    if (ret_node->value) {
        MIR_reg_t val = transpile_expr(mt, ret_node->value);
        TypeId val_tid = ret_node->value->type ? ret_node->value->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed)));
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }
    MIR_reg_t r = new_reg(mt, "ret_dummy", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, 0)));
    return r;
}

// ============================================================================
// Assignment statement (procedural)
// ============================================================================

static MIR_reg_t transpile_assign_stam(MirTranspiler* mt, AstAssignStamNode* assign) {
    MIR_reg_t val = transpile_expr(mt, assign->value);

    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)assign->target->len, assign->target->chars);

    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        // Re-assign in same register
        if (var->mir_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, val)));
        }
    } else {
        log_error("mir: assignment to undefined variable '%s'", name_buf);
    }

    return val;
}

// ============================================================================
// Box item: emit boxing for an expression node (produces Item from any type)
// ============================================================================

static MIR_reg_t transpile_box_item(MirTranspiler* mt, AstNode* node) {
    if (!node || !node->type) {
        MIR_reg_t val = transpile_expr(mt, node);
        return val;
    }

    TypeId tid = node->type->type_id;

    // For literals of constant types (string, symbol, etc), use const boxing
    if (node->type->is_literal) {
        switch (tid) {
        case LMD_TYPE_NULL: {
            MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
        case LMD_TYPE_BOOL: {
            MIR_reg_t val = transpile_expr(mt, node);
            return emit_box_bool(mt, val);
        }
        case LMD_TYPE_INT: {
            MIR_reg_t val = transpile_expr(mt, node);
            return emit_box_int(mt, val);
        }
        case LMD_TYPE_INT64: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_INT64);
        }
        case LMD_TYPE_FLOAT: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_FLOAT);
        }
        case LMD_TYPE_STRING: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_STRING);
        }
        case LMD_TYPE_SYMBOL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_SYMBOL);
        }
        case LMD_TYPE_DTIME: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_DTIME);
        }
        case LMD_TYPE_DECIMAL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_DECIMAL);
        }
        case LMD_TYPE_BINARY: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_BINARY);
        }
        default:
            break;
        }
    }

    // Evaluate expression then box
    MIR_reg_t val = transpile_expr(mt, node);

    // If result is already an Item type (ANY, ERROR, LIST that came from list_end, etc.), return as-is
    if (tid == LMD_TYPE_ANY || tid == LMD_TYPE_ERROR || tid == LMD_TYPE_NULL ||
        tid == LMD_TYPE_NUMBER) {
        return val;
    }

    // For LIST type, list_end already returns Item - return as-is
    if (tid == LMD_TYPE_LIST && node->node_type == AST_NODE_CONTENT) {
        return val;
    }

    return emit_box(mt, val, tid);
}

// ============================================================================
// Base type expressions (for match patterns, type checks)
// ============================================================================

static MIR_reg_t transpile_base_type(MirTranspiler* mt, AstTypeNode* type_node) {
    // base_type(type_id) returns a Type* for runtime type checking
    TypeId tid = type_node->type ? type_node->type->type_id : LMD_TYPE_ANY;

    // If this is a TypeType, get the actual type
    if (type_node->type && type_node->type->type_id == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)type_node->type;
        if (tt->type) {
            tid = tt->type->type_id;
        }
    }

    return emit_call_1(mt, "base_type", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, tid));
}

// ============================================================================
// Main expression dispatcher
// ============================================================================

static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node) {
    if (!node) {
        log_error("mir: null expression node");
        MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }

    switch (node->node_type) {
    case AST_NODE_PRIMARY:
        return transpile_primary(mt, (AstPrimaryNode*)node);
    case AST_NODE_IDENT:
        return transpile_ident(mt, (AstIdentNode*)node);
    case AST_NODE_BINARY:
        return transpile_binary(mt, (AstBinaryNode*)node);
    case AST_NODE_UNARY:
        return transpile_unary(mt, (AstUnaryNode*)node);
    case AST_NODE_SPREAD:
        return transpile_spread(mt, (AstUnaryNode*)node);
    case AST_NODE_IF_EXPR:
    case AST_NODE_IF_STAM:
        return transpile_if(mt, (AstIfNode*)node);
    case AST_NODE_MATCH_EXPR:
        return transpile_match(mt, (AstMatchNode*)node);
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
        return transpile_for(mt, (AstForNode*)node);
    case AST_NODE_WHILE_STAM:
        return transpile_while(mt, (AstWhileNode*)node);
    case AST_NODE_BREAK_STAM: {
        if (mt->loop_depth > 0) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        MIR_reg_t r = new_reg(mt, "brk", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_CONTINUE_STAM: {
        if (mt->loop_depth > 0) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        MIR_reg_t r = new_reg(mt, "cont", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_RETURN_STAM:
        return transpile_return(mt, (AstReturnNode*)node);
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
        return transpile_raise(mt, (AstRaiseNode*)node);
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_TYPE_STAM:
        transpile_let_stam(mt, (AstLetNode*)node);
        {
            MIR_reg_t r = new_reg(mt, "let_null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
    case AST_NODE_VAR_STAM:
        transpile_let_stam(mt, (AstLetNode*)node);
        {
            MIR_reg_t r = new_reg(mt, "var_null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
    case AST_NODE_ASSIGN_STAM:
        return transpile_assign_stam(mt, (AstAssignStamNode*)node);
    case AST_NODE_ASSIGN: {
        AstNamedNode* asn = (AstNamedNode*)node;
        if (asn->as) {
            MIR_reg_t val = transpile_expr(mt, asn->as);
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
            TypeId tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
            set_var(mt, name_buf, val, type_to_mir(tid), tid);
            return val;
        }
        MIR_reg_t r = new_reg(mt, "asn", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_ARRAY:
        return transpile_array(mt, (AstArrayNode*)node);
    case AST_NODE_LIST:
        return transpile_list(mt, (AstListNode*)node);
    case AST_NODE_CONTENT:
        return transpile_content(mt, (AstListNode*)node);
    case AST_NODE_MAP:
        return transpile_map(mt, (AstMapNode*)node);
    case AST_NODE_MEMBER_EXPR:
        return transpile_member(mt, (AstFieldNode*)node);
    case AST_NODE_INDEX_EXPR:
        return transpile_index(mt, (AstFieldNode*)node);
    case AST_NODE_CALL_EXPR:
        return transpile_call(mt, (AstCallNode*)node);
    case AST_NODE_PIPE:
        return transpile_pipe(mt, (AstPipeNode*)node);
    case AST_NODE_CURRENT_ITEM: {
        if (mt->in_pipe) return mt->pipe_item_reg;
        MIR_reg_t r = new_reg(mt, "pipe_item", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_CURRENT_INDEX: {
        if (mt->in_pipe) return mt->pipe_index_reg;
        MIR_reg_t r = new_reg(mt, "pipe_idx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_TYPE:
        return transpile_base_type(mt, (AstTypeNode*)node);
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_IMPORT:
        // Definitions are handled in the root pass
        {
            MIR_reg_t r = new_reg(mt, "def", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
    default:
        log_error("mir: unhandled node type %d", node->node_type);
        {
            MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
    }
}

// ============================================================================
// User-defined function transpilation
// ============================================================================

static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node) {
    // Build function name
    StrBuf* name_buf = strbuf_new_cap(64);
    write_fn_name(name_buf, fn_node, NULL);

    // Determine return type (always Item for safety)
    MIR_type_t ret_type = MIR_T_I64;

    // Build parameter list (all params as Item/boxed for consistency with C transpiler)
    MIR_var_t params[32];
    int param_count = 0;

    AstNamedNode* param = fn_node->param;
    while (param && param_count < 32) {
        char pname[64];
        snprintf(pname, sizeof(pname), "_%.*s", (int)param->name->len, param->name->chars);

        params[param_count] = {MIR_T_I64, strdup(pname), 0};
        param_count++;
        param = (AstNamedNode*)param->next;
    }

    // Save current function context
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;

    // Save original strdup pointers before MIR overwrites them
    char* param_name_copies[32];
    for (int i = 0; i < param_count; i++) param_name_copies[i] = (char*)params[i].name;

    // Create function (MIR replaces params[i].name with internal copies)
    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, name_buf->str, 1, &ret_type, param_count, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);
    mt->current_func_item = func_item;
    mt->current_func = func;

    // Free our strdup copies (MIR made its own)
    for (int i = 0; i < param_count; i++) free(param_name_copies[i]);

    // Register as local function early (before body transpilation for recursion)
    register_local_func(mt, name_buf->str, func_item);

    // Set up parameter scope
    push_scope(mt);

    // Bind parameters as local variables
    param = fn_node->param;
    int pi = 0;
    while (param) {
        char pname[64];
        snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);

        // Function parameter register is already created by MIR with the prefixed name
        char prefixed[68];
        snprintf(prefixed, sizeof(prefixed), "_%s", pname);
        MIR_reg_t preg = MIR_reg(mt->ctx, prefixed, func);

        // Parameters arrive as boxed Items (MIR_T_I64).
        // For typed params, unbox to native type so binary handler can use native ops.
        TypeId tid = param->type ? param->type->type_id : LMD_TYPE_ANY;
        if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
            tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT64) {
            MIR_reg_t unboxed = emit_unbox(mt, preg, tid);
            MIR_type_t mtype = type_to_mir(tid);
            set_var(mt, pname, unboxed, mtype, tid);
        } else {
            // Untyped or complex type: keep as boxed Item
            set_var(mt, pname, preg, MIR_T_I64, LMD_TYPE_ANY);
        }

        pi++;
        param = (AstNamedNode*)param->next;
    }

    // Transpile body - use transpile_box_item to ensure result is boxed Item
    MIR_reg_t body_result = transpile_box_item(mt, fn_node->body);

    // Return boxed result
    emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, body_result)));

    pop_scope(mt);

    MIR_finish_func(mt->ctx);

    // Restore function context
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;

    strbuf_free(name_buf);
}

// ============================================================================
// AST root transpilation
// ============================================================================

void transpile_mir_ast(MIR_context_t ctx, AstScript *script, const char* source) {
    log_notice("transpile AST to MIR (direct)");

    MirTranspiler mt;
    memset(&mt, 0, sizeof(mt));
    mt.ctx = ctx;
    mt.script = script;
    mt.source = source;
    mt.is_main = true;

    // Init import cache
    mt.import_cache = hashmap_new(sizeof(ImportCacheEntry), 128, 0, 0,
        import_cache_hash, import_cache_cmp, NULL, NULL);
    mt.local_funcs = hashmap_new(sizeof(LocalFuncEntry), 32, 0, 0,
        local_func_hash, local_func_cmp, NULL, NULL);

    // Create module
    mt.module = MIR_new_module(ctx, "lambda_script");

    // Import _lambda_rt (shared context pointer)
    MIR_item_t rt_import = MIR_new_import(ctx, "_lambda_rt");

    // First pass: define all top-level functions
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            AstNode* item = ((AstListNode*)child)->item;
            while (item) {
                if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC) {
                    transpile_func_def(&mt, (AstFuncNode*)item);
                }
                item = item->next;
            }
        } else if (child->node_type == AST_NODE_FUNC || child->node_type == AST_NODE_PROC) {
            transpile_func_def(&mt, (AstFuncNode*)child);
        }
        child = child->next;
    }

    // Create main function: Item main(Context* runtime)
    MIR_var_t main_vars[] = {{MIR_T_P, "runtime", 0}};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(ctx, "main", 1, &main_ret, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(ctx, main_item);
    mt.current_func_item = main_item;
    mt.current_func = main_func;

    // Get the runtime parameter register
    MIR_reg_t runtime_reg = MIR_reg(ctx, "runtime", main_func);

    // Store runtime to _lambda_rt: *(&_lambda_rt) = runtime
    // import_resolver("_lambda_rt") returns &_lambda_rt (a Context**)
    // We load this address into a register, then store runtime through it
    mt.rt_reg = runtime_reg;
    MIR_reg_t rt_addr = new_reg(&mt, "rt_addr", MIR_T_I64);
    // Load the address that the import resolves to (= &_lambda_rt)
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, rt_addr),
        MIR_new_ref_op(ctx, rt_import)));
    // Store runtime pointer at that address: *(&_lambda_rt) = runtime
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_mem_op(ctx, MIR_T_I64, 0, rt_addr, 0, 1),
        MIR_new_reg_op(ctx, runtime_reg)));

    // Load consts pointer: rt->consts (at offset 8)
    mt.consts_reg = new_reg(&mt, "consts", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, mt.consts_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, offsetof(Context, consts), runtime_reg, 0, 1)));

    // Set up variable scope for main body
    push_scope(&mt);

    // Transpile body: walk children, emit content
    MIR_reg_t result = new_reg(&mt, "result", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
        MIR_new_int_op(ctx, (int64_t)NULL_VAL)));

    child = script->child;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_CONTENT: {
            MIR_reg_t content_val = transpile_content(&mt, (AstListNode*)child);
            emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                MIR_new_reg_op(ctx, content_val)));
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
            transpile_let_stam(&mt, (AstLetNode*)child);
            break;
        case AST_NODE_IMPORT:
        case AST_NODE_FUNC:
        case AST_NODE_FUNC_EXPR:
        case AST_NODE_PROC:
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN:
            // Skip - handled in pre-pass
            break;
        default: {
            // Expression: box it as the result
            MIR_reg_t val = transpile_box_item(&mt, child);
            emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                MIR_new_reg_op(ctx, val)));
            break;
        }
        }
        child = child->next;
    }

    pop_scope(&mt);

    // Return result
    emit_insn(&mt, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result)));

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    // Load module for linking (required before MIR_link)
    MIR_load_module(ctx, mt.module);

    // Cleanup
    hashmap_free(mt.import_cache);
    hashmap_free(mt.local_funcs);
}

// ============================================================================
// Main entry point for MIR compilation
// ============================================================================

Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main) {
    log_notice("Running script with MIR JIT compilation (direct)");

    // Initialize runner
    Runner runner;
    runner_init(runtime, &runner);

    // Load and parse script (includes AST build, type inference, const allocation)
    if (source) {
        runner.script = load_script(runtime, script_path, source, false);
    } else {
        runner.script = load_script(runtime, script_path, NULL, false);
    }

    if (!runner.script || !runner.script->ast_root) {
        log_error("Failed to parse script");
        Pool* error_pool = pool_create();
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    // Initialize MIR context
    unsigned int opt_level = runtime ? runtime->optimize_level : 2;
    MIR_context_t ctx = jit_init(opt_level);

    // Transpile AST to MIR directly
    transpile_mir_ast(ctx, (AstScript*)runner.script->ast_root, runner.script->source);

    // Link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // Find the main function
    runner.script->main_func = (main_func_t)find_func(ctx, "main");
    if (!runner.script->main_func) {
        log_error("Failed to find main function");
        jit_cleanup(ctx);
        Pool* error_pool = pool_create();
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    // Execute
    Input* output = execute_script_and_create_output(&runner, run_main);

    // Cleanup MIR context
    jit_cleanup(ctx);

    return output;
}
