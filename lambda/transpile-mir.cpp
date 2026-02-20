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
#include <alloca.h>

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

// Forward declare has_current_item_ref from build_ast.cpp
bool has_current_item_ref(AstNode* node);

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
// Variadic call helpers for map_fill, elmt_fill, list_fill
// ============================================================================

// Emit a variadic call: fn_name(mandatory_arg, vararg1, vararg2, ...)
// mandatory_args + varargs are passed as ops. proto is created with vararg flag.
static MIR_reg_t emit_vararg_call(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, int nres,
    MIR_type_t mandatory_type, MIR_op_t mandatory_op,
    int n_varargs, MIR_op_t* vararg_ops) {
    // Create unique vararg proto name per call site
    char proto_name[160];
    snprintf(proto_name, sizeof(proto_name), "%s_vp%d", fn_name, mt->label_counter++);

    // Mandatory arg
    MIR_var_t mandatory_var = {mandatory_type, "m", 0};
    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, nres, res_types, 1, &mandatory_var);

    // Import
    ImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", fn_name);
    ImportCacheEntry* found = (ImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    MIR_item_t imp;
    if (found) {
        imp = found->entry.import;
    } else {
        imp = MIR_new_import(mt->ctx, fn_name);
        // Cache only the import, not proto (proto is unique per call)
        ImportCacheEntry new_entry;
        memset(&new_entry, 0, sizeof(new_entry));
        snprintf(new_entry.name, sizeof(new_entry.name), "%s", fn_name);
        new_entry.entry.import = imp;
        new_entry.entry.proto = proto; // store last proto
        hashmap_set(mt->import_cache, &new_entry);
    }

    // Build call: proto, import, [result], mandatory, varargs...
    int nops = (nres > 0 ? 3 : 2) + 1 + n_varargs; // +1 for mandatory arg
    MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
    ops[oi++] = MIR_new_ref_op(mt->ctx, imp);
    MIR_reg_t result = 0;
    if (nres > 0) {
        result = new_reg(mt, fn_name, ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, result);
    }
    ops[oi++] = mandatory_op;
    for (int i = 0; i < n_varargs; i++) {
        ops[oi++] = vararg_ops[i];
    }

    emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, oi, ops));
    return result;
}

// Emit vararg call with 2 mandatory args (for list_fill: List*, int count, ...)
static MIR_reg_t emit_vararg_call_2(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, int nres,
    MIR_type_t m1_type, MIR_op_t m1_op,
    MIR_type_t m2_type, MIR_op_t m2_op,
    int n_varargs, MIR_op_t* vararg_ops) {
    char proto_name[160];
    snprintf(proto_name, sizeof(proto_name), "%s_vp%d", fn_name, mt->label_counter++);

    MIR_var_t mandatory_vars[2] = {{m1_type, "m1", 0}, {m2_type, "m2", 0}};
    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, nres, res_types, 2, mandatory_vars);

    ImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", fn_name);
    ImportCacheEntry* found = (ImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    MIR_item_t imp;
    if (found) {
        imp = found->entry.import;
    } else {
        imp = MIR_new_import(mt->ctx, fn_name);
        ImportCacheEntry new_entry;
        memset(&new_entry, 0, sizeof(new_entry));
        snprintf(new_entry.name, sizeof(new_entry.name), "%s", fn_name);
        new_entry.entry.import = imp;
        new_entry.entry.proto = proto;
        hashmap_set(mt->import_cache, &new_entry);
    }

    int nops = (nres > 0 ? 3 : 2) + 2 + n_varargs; // +2 for m1 and m2
    MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
    ops[oi++] = MIR_new_ref_op(mt->ctx, imp);
    MIR_reg_t result = 0;
    if (nres > 0) {
        result = new_reg(mt, fn_name, ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, result);
    }
    ops[oi++] = m1_op;
    ops[oi++] = m2_op;
    for (int i = 0; i < n_varargs; i++) {
        ops[oi++] = vararg_ops[i];
    }

    emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, oi, ops));
    return result;
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
static void emit_call_void_0(MirTranspiler* mt, const char* fn_name) {
    MirImportEntry* ie = ensure_import(mt, fn_name, MIR_T_I64, 0, nullptr, 0);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 2,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import)));
}

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
    // Bool can be BOOL_FALSE(0), BOOL_TRUE(1), or BOOL_ERROR(2)
    // If val >= BOOL_ERROR (2), return ITEM_ERROR instead of boxing as bool
    MIR_reg_t is_error = new_reg(mt, "berr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, is_error),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, 2)));
    MIR_label_t l_error = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    MIR_reg_t result = new_reg(mt, "boxb", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_error),
        MIR_new_reg_op(mt->ctx, is_error)));
    // Normal bool: box as (BOOL_TAG | val)
    uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    // Error case: return ITEM_ERROR
    emit_label(mt, l_error);
    uint64_t ERROR_TAG = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ERROR_TAG)));
    emit_label(mt, l_end);
    return result;
}

// Box float (double) -> Item via push_d runtime call
static MIR_reg_t emit_box_float(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, val_reg));
}

// Load a C string literal pointer into a register
// Uses the raw host pointer since name pool strings persist for program lifetime
static MIR_reg_t emit_load_string_literal(MirTranspiler* mt, const char* str) {
    MIR_reg_t r = new_reg(mt, "strp", MIR_T_P);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)str)));
    return r;
}

// Box int64 -> Item via push_l runtime call
static MIR_reg_t emit_box_int64(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_l", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box DateTime -> Item via push_k runtime call
static MIR_reg_t emit_box_dtime(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_k", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
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
    case LMD_TYPE_STRING:
        return emit_box_string(mt, val_reg);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, val_reg);
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_LIST: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_RANGE: case LMD_TYPE_FUNC:
    case LMD_TYPE_TYPE: case LMD_TYPE_PATH:
        return emit_box_container(mt, val_reg);
    // DTIME, INT64, DECIMAL, BINARY values from transpile_expr are always
    // already boxed Items (tagged pointers). emit_load_const_boxed uses inline
    // OR tagging, and function calls return tagged Items. Do NOT re-box via
    // push_k/push_l/push_dc which would double-box and corrupt the value.
    case LMD_TYPE_INT64:
    case LMD_TYPE_DTIME:
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_BINARY:
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
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_INT64);
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
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Count arity from param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Look up the MIR function item
            StrBuf* nm_buf = strbuf_new_cap(64);
            write_fn_name(nm_buf, fn_node, ident->entry->import);
            MIR_item_t func_item = find_local_func(mt, nm_buf->str);
            if (!func_item) func_item = find_local_func(mt, name_buf);

            if (func_item) {
                // Get function address via MIR ref
                MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_new_ref_op(mt->ctx, func_item)));

                // Create Function* via to_fn_n(fn_ptr, arity)
                MIR_reg_t fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arity));

                strbuf_free(nm_buf);
                return fn_obj;  // Function* is a container
            }
            strbuf_free(nm_buf);
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

    // IDIV and MOD always go through runtime for correct error handling
    // (div-by-zero → error, float operands → error, etc.)
    if (bi->op == OPERATOR_IDIV || bi->op == OPERATOR_MOD) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        const char* fn_name = (bi->op == OPERATOR_IDIV) ? "fn_idiv" : "fn_mod";
        return emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

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
            // int/int division promotes to float
            if (both_int) {
                MIR_reg_t dl = new_reg(mt, "i2d_l", MIR_T_D);
                MIR_reg_t dr = new_reg(mt, "i2d_r", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dl),
                    MIR_new_reg_op(mt->ctx, left)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dr),
                    MIR_new_reg_op(mt->ctx, right)));
                MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, dl), MIR_new_reg_op(mt->ctx, dr)));
                return r;
            } else {
                MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
                return r;
            }
        }
        case OPERATOR_POW:
            // POW has AST type ANY (not FLOAT/INT), so native handling would
            // return MIR_T_D which conflicts with ANY expectation of boxed Item.
            // Let all POW fall through to boxed runtime path.
            break;
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

    // String/symbol concatenation: fn_join(Item, Item) -> Item
    if (bi->op == OPERATOR_JOIN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_join", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // Short-circuit AND/OR
    if (bi->op == OPERATOR_AND) {
        MIR_reg_t result = new_reg(mt, "and", MIR_T_I64);

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            // native bool AND
            MIR_reg_t left_val = transpile_expr(mt, bi->left);
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
        // Boxed AND: use is_truthy + branch with short-circuit
        // Use transpile_box_item to ensure sub-expressions are properly boxed
        // (transpile_expr + emit_box(val, ANY) fails when inner expr returns native values)
        MIR_reg_t boxed_left = transpile_box_item(mt, bi->left);
        MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
        MIR_label_t l_false = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t boxed_right = transpile_box_item(mt, bi->right);
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

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            MIR_reg_t left_val = transpile_expr(mt, bi->left);
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
        // Boxed OR: use is_truthy + branch with short-circuit
        MIR_reg_t boxed_left = transpile_box_item(mt, bi->left);
        MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
        MIR_label_t l_true = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t boxed_right = transpile_box_item(mt, bi->right);
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
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        // fn_to(start, end) returns Range* (a container pointer)
        return emit_call_2(mt, "fn_to", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // Type operators
    if (bi->op == OPERATOR_IS) {
        // Check if right operand is a constrained type for inline constraint evaluation
        AstNode* right_node = bi->right;
        // Unwrap primary node
        if (right_node->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)right_node;
            if (pri->expr) right_node = pri->expr;
        }

        AstConstrainedTypeNode* constrained_node = nullptr;
        if (right_node->node_type == AST_NODE_CONSTRAINED_TYPE) {
            constrained_node = (AstConstrainedTypeNode*)right_node;
        } else if (right_node->type && right_node->type->kind == TYPE_KIND_CONSTRAINED) {
            if (right_node->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident = (AstIdentNode*)right_node;
                if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
                    if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                        constrained_node = (AstConstrainedTypeNode*)type_def->as;
                    }
                }
            }
        } else if (right_node->type && right_node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* type_type = (TypeType*)right_node->type;
            if (type_type->type && type_type->type->kind == TYPE_KIND_CONSTRAINED) {
                if (right_node->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident = (AstIdentNode*)right_node;
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
            // Inline constrained type check: base_type_check && constraint_check
            TypeConstrained* constrained = (TypeConstrained*)constrained_node->type;

            MIR_reg_t ct_value = transpile_box_item(mt, bi->left);
            MIR_reg_t result = new_reg(mt, "ct_res", MIR_T_I64);

            // Check base type: item_type_id(ct_value) == constrained->base->type_id
            MIR_reg_t tid_reg = emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ct_value));
            MIR_reg_t base_match = new_reg(mt, "base_eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, base_match),
                MIR_new_reg_op(mt->ctx, tid_reg),
                MIR_new_int_op(mt->ctx, constrained->base->type_id)));

            // If base doesn't match, result = false
            MIR_label_t lbl_check = new_label(mt);
            MIR_label_t lbl_end = new_label(mt);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));  // default false
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, lbl_check),
                MIR_new_reg_op(mt->ctx, base_match)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, lbl_end)));

            // Base type matches — evaluate constraint with ~ bound to ct_value
            emit_label(mt, lbl_check);
            MIR_reg_t old_pipe_item = mt->pipe_item_reg;
            bool old_in_pipe = mt->in_pipe;
            mt->pipe_item_reg = ct_value;
            mt->in_pipe = true;

            MIR_reg_t constraint_val = transpile_box_item(mt, constrained_node->constraint);
            MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, truthy)));

            mt->pipe_item_reg = old_pipe_item;
            mt->in_pipe = old_in_pipe;

            emit_label(mt, lbl_end);
            return result;
        }

        // Standard fn_is call for non-constrained types
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }
    if (bi->op == OPERATOR_IN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // Fallback: box both sides and call runtime function
    // Use transpile_box_item to correctly box sub-expressions that may return native values
    MIR_reg_t boxl = transpile_box_item(mt, bi->left);
    MIR_reg_t boxr = transpile_box_item(mt, bi->right);

    const char* fn_name = NULL;
    switch (bi->op) {
    case OPERATOR_ADD: fn_name = "fn_add"; break;
    // OPERATOR_JOIN handled above via fn_join
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

    switch (un->op) {
    case OPERATOR_NEG: {
        if (operand_tid == LMD_TYPE_INT) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_NEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        if (operand_tid == LMD_TYPE_FLOAT) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DNEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        return emit_call_1(mt, "fn_neg", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_NOT: {
        if (operand_tid == LMD_TYPE_BOOL) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "not", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand), MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        return emit_call_1(mt, "fn_not", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_POS: {
        if (operand_tid == LMD_TYPE_INT || operand_tid == LMD_TYPE_INT64 || operand_tid == LMD_TYPE_FLOAT) {
            // No-op for numeric types
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            return operand;
        }
        // Runtime fn_pos for string-to-number casting etc.
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        return emit_call_1(mt, "fn_pos", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_IS_ERROR: {
        // ^expr: check if Item type is LMD_TYPE_ERROR
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
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
        MIR_reg_t operand = transpile_expr(mt, un->operand);
        return operand;
    }
}

// ============================================================================
// Spread expression
// ============================================================================

static MIR_reg_t transpile_spread(MirTranspiler* mt, AstUnaryNode* spread) {
    MIR_reg_t boxed = transpile_box_item(mt, spread->operand);
    return emit_call_1(mt, "item_spread", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
}

// ============================================================================
// If/else expressions
// ============================================================================

static MIR_reg_t transpile_if(MirTranspiler* mt, AstIfNode* if_node) {
    TypeId cond_tid = if_node->cond->type ? if_node->cond->type->type_id : LMD_TYPE_ANY;

    MIR_reg_t cond = transpile_expr(mt, if_node->cond);

    // For non-bool condition, check truthiness
    // Lambda semantics: only null, error, and false are falsy.
    // Int (even 0), Float (even 0.0), strings, lists, etc. are all truthy.
    MIR_reg_t cond_val = cond;
    if (cond_tid == LMD_TYPE_INT || cond_tid == LMD_TYPE_INT64 || cond_tid == LMD_TYPE_FLOAT ||
        cond_tid == LMD_TYPE_STRING || cond_tid == LMD_TYPE_SYMBOL) {
        // These types are always truthy in Lambda — no comparison needed
        MIR_reg_t always_true = new_reg(mt, "true", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, always_true),
            MIR_new_int_op(mt->ctx, 1)));
        cond_val = always_true;
    } else if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }

    // Determine result type - check if branches could produce different MIR types
    TypeId if_tid = ((AstNode*)if_node)->type ? ((AstNode*)if_node)->type->type_id : LMD_TYPE_ANY;
    TypeId then_tid = if_node->then && if_node->then->type ? if_node->then->type->type_id : LMD_TYPE_ANY;
    TypeId else_tid = if_node->otherwise && if_node->otherwise->type ? if_node->otherwise->type->type_id : LMD_TYPE_ANY;
    MIR_type_t then_mir = type_to_mir(then_tid);
    MIR_type_t else_mir = type_to_mir(else_tid);

    // If branches have different MIR types or the node type is ANY, always box to Item (I64)
    bool need_boxing = (if_tid == LMD_TYPE_ANY || if_tid == LMD_TYPE_NUMBER ||
                        then_mir != else_mir ||
                        then_mir != type_to_mir(if_tid));

    MIR_type_t result_type = need_boxing ? MIR_T_I64 : type_to_mir(if_tid);
    MIR_reg_t result = new_reg(mt, "if_res", result_type);
    MIR_label_t l_else = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Then branch
    if (if_node->then) {
        MIR_reg_t then_val = transpile_expr(mt, if_node->then);
        if (need_boxing) {
            MIR_reg_t boxed = emit_box(mt, then_val, then_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
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
        if (need_boxing) {
            MIR_reg_t boxed = emit_box(mt, else_val, else_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
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

// Emit a single pattern test (fn_is/fn_in/fn_eq) and branch to l_fail on mismatch
static void emit_single_pattern_test(MirTranspiler* mt, AstNode* pattern, MIR_reg_t boxed_scrut, MIR_label_t l_fail) {
    // Handle constrained type patterns: case int that (~ > 0)
    if (pattern->node_type == AST_NODE_CONSTRAINED_TYPE) {
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)pattern;
        TypeConstrained* constrained = (TypeConstrained*)ct->type;
        if (constrained) {
            // Check base type
            MIR_reg_t tid_reg = emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut));
            MIR_reg_t base_match = new_reg(mt, "base_eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, base_match),
                MIR_new_reg_op(mt->ctx, tid_reg),
                MIR_new_int_op(mt->ctx, constrained->base->type_id)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
                MIR_new_reg_op(mt->ctx, base_match)));

            // Base type matches — evaluate constraint with ~ bound to scrutinee
            MIR_reg_t old_pipe_item = mt->pipe_item_reg;
            bool old_in_pipe = mt->in_pipe;
            mt->pipe_item_reg = boxed_scrut;
            mt->in_pipe = true;

            MIR_reg_t constraint_val = transpile_box_item(mt, ct->constraint);
            MIR_reg_t truthy = emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val));

            mt->pipe_item_reg = old_pipe_item;
            mt->in_pipe = old_in_pipe;

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
                MIR_new_reg_op(mt->ctx, truthy)));
            return;
        }
    }

    MIR_reg_t pat = transpile_expr(mt, pattern);
    TypeId pat_tid = pattern->type ? pattern->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_pat = emit_box(mt, pat, pat_tid);

    MIR_reg_t match_test;
    if (pat_tid == LMD_TYPE_TYPE) {
        match_test = emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat));
    } else if (pat_tid == LMD_TYPE_RANGE) {
        match_test = emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat));
    } else {
        match_test = emit_call_2(mt, "fn_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat));
    }

    MIR_reg_t is_match = new_reg(mt, "ismatch", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_match),
        MIR_new_reg_op(mt->ctx, match_test), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
        MIR_new_reg_op(mt->ctx, is_match)));
}

// Emit pattern test handling union (or-) patterns: branches to l_fail only if ALL alternatives fail
static void emit_match_pattern_test(MirTranspiler* mt, AstNode* pattern, MIR_reg_t boxed_scrut, MIR_label_t l_fail) {
    // Handle union patterns (A | B): try left, if matches jump to success, else try right
    if (pattern->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* bi = (AstBinaryNode*)pattern;
        if (bi->op == OPERATOR_UNION) {
            MIR_label_t l_success = new_label(mt);
            MIR_label_t l_try_right = new_label(mt);

            // Try left alternative
            emit_match_pattern_test(mt, bi->left, boxed_scrut, l_try_right);
            // Left matched - jump to success
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_success)));

            // Try right alternative
            emit_label(mt, l_try_right);
            emit_match_pattern_test(mt, bi->right, boxed_scrut, l_fail);
            // Right matched - fall through to success

            emit_label(mt, l_success);
            return;
        }
    }

    // Simple pattern test
    emit_single_pattern_test(mt, pattern, boxed_scrut, l_fail);
}

static MIR_reg_t transpile_match(MirTranspiler* mt, AstMatchNode* match_node) {
    MIR_reg_t scrutinee = transpile_expr(mt, match_node->scrutinee);
    TypeId scrut_tid = match_node->scrutinee->type ? match_node->scrutinee->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_scrut = emit_box(mt, scrutinee, scrut_tid);

    // Set ~ (current item) to the scrutinee value for match bodies
    bool old_in_pipe = mt->in_pipe;
    MIR_reg_t old_pipe_item = mt->pipe_item_reg;
    MIR_reg_t old_pipe_index = mt->pipe_index_reg;
    mt->in_pipe = true;
    mt->pipe_item_reg = boxed_scrut;

    MIR_reg_t result = new_reg(mt, "match", MIR_T_I64);
    MIR_label_t l_end = new_label(mt);

    AstNode* arm = (AstNode*)match_node->first_arm;
    while (arm) {
        AstMatchArm* match_arm = (AstMatchArm*)arm;
        if (match_arm->pattern) {
            MIR_label_t l_next = new_label(mt);

            // Emit pattern test (handles union/or-patterns recursively)
            emit_match_pattern_test(mt, match_arm->pattern, boxed_scrut, l_next);

            // Body
            MIR_reg_t body = transpile_box_item(mt, match_arm->body);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            emit_label(mt, l_next);
        } else {
            // Default arm
            MIR_reg_t body = transpile_box_item(mt, match_arm->body);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        }
        arm = arm->next;
    }

    // No match - return ITEM_NULL
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));

    emit_label(mt, l_end);

    // Restore old pipe state
    mt->in_pipe = old_in_pipe;
    mt->pipe_item_reg = old_pipe_item;
    mt->pipe_index_reg = old_pipe_index;

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

    // Check for map type at runtime (maps and vmaps need special iteration via item_keys)
    MIR_reg_t coll_type_id = emit_call_1(mt, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));
    MIR_reg_t is_map_coll = new_reg(mt, "is_map_coll", MIR_T_I64);
    MIR_reg_t is_map_t = new_reg(mt, "is_map_t", MIR_T_I64);
    MIR_reg_t is_vmap_t = new_reg(mt, "is_vmap_t", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map_t),
        MIR_new_reg_op(mt->ctx, coll_type_id), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_vmap_t),
        MIR_new_reg_op(mt->ctx, coll_type_id), MIR_new_int_op(mt->ctx, LMD_TYPE_VMAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map_coll),
        MIR_new_reg_op(mt->ctx, is_map_t), MIR_new_reg_op(mt->ctx, is_vmap_t)));

    // Get length (branching for map vs non-map)
    MIR_reg_t len = new_reg(mt, "for_len", MIR_T_I64);
    MIR_reg_t keys_al = new_reg(mt, "for_keys", MIR_T_P);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_non_map_for = new_label(mt);
    MIR_label_t l_start_for = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_non_map_for),
        MIR_new_reg_op(mt->ctx, is_map_coll)));

    // MAP path
    MIR_reg_t keys_res = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_reg_op(mt->ctx, keys_res)));
    MIR_reg_t map_len_v = emit_call_1(mt, "pipe_map_len", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, map_len_v)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start_for)));

    // NON-MAP path
    emit_label(mt, l_non_map_for);
    MIR_reg_t arr_len_v = emit_call_1(mt, "fn_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, arr_len_v)));

    emit_label(mt, l_start_for);

    // Create spreadable output array (for-expressions produce spreadable arrays)
    MIR_reg_t output = emit_call_0(mt, "array_spreadable", MIR_T_P);

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

    // Get current item (branching for map vs non-map)
    MIR_reg_t current_item = new_reg(mt, "cur_item", MIR_T_I64);
    MIR_label_t l_arr_get = new_label(mt);
    MIR_label_t l_got_cur = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_arr_get),
        MIR_new_reg_op(mt->ctx, is_map_coll)));

    // MAP path: get value via pipe_map_val
    MIR_reg_t map_v = emit_call_3(mt, "pipe_map_val", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, current_item),
        MIR_new_reg_op(mt->ctx, map_v)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_got_cur)));

    // NON-MAP path: use item_at
    emit_label(mt, l_arr_get);
    MIR_reg_t arr_v = emit_call_2(mt, "item_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, current_item),
        MIR_new_reg_op(mt->ctx, arr_v)));
    emit_label(mt, l_got_cur);

    // Bind loop variable
    char var_name[128];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)loop->name->len, loop->name->chars);
    TypeId var_tid = loop->type ? loop->type->type_id : LMD_TYPE_ANY;

    if (loop->is_named && !loop->index_name) {
        // Single-variable 'at' form: for k at map → k is the key name
        MIR_reg_t key_item = emit_call_2(mt, "pipe_map_key", MIR_T_I64,
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
        set_var(mt, var_name, key_item, MIR_T_I64, LMD_TYPE_ANY);
    } else {
        // Normal case: name = current value
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
            if (coll_tid == LMD_TYPE_RANGE) {
                set_var(mt, idx_name, var_reg, var_mir, var_tid);
            } else if (loop->is_named) {
                // Two-variable 'at' form: for k, v at map → index_name = key
                MIR_reg_t key_item = emit_call_2(mt, "pipe_map_key", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
                set_var(mt, idx_name, key_item, MIR_T_I64, LMD_TYPE_ANY);
            } else {
                set_var(mt, idx_name, idx, MIR_T_I64, LMD_TYPE_INT);
            }
        }
    }

    // Process let clauses (additional variable bindings)
    if (for_node->let_clause) {
        AstNode* lc = for_node->let_clause;
        while (lc) {
            AstNamedNode* let_node = (AstNamedNode*)lc;
            if (let_node->as) {
                MIR_reg_t val = transpile_expr(mt, let_node->as);
                char lc_name[128];
                snprintf(lc_name, sizeof(lc_name), "%.*s", (int)let_node->name->len, let_node->name->chars);
                TypeId lc_tid = let_node->as->type ? let_node->as->type->type_id : LMD_TYPE_ANY;
                MIR_type_t lc_mtype = type_to_mir(lc_tid);
                set_var(mt, lc_name, val, lc_mtype, lc_tid);
            }
            lc = lc->next;
        }
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

    // Push to output (use spread to flatten nested spreadable arrays)
    emit_call_void_2(mt, "array_push_spread", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

    // Continue: increment index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);

    // Finalize array — array_end returns Item (int64_t)
    MIR_reg_t final = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));

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
                TypeId expr_tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
                // Use the variable's declared type if available, otherwise the expression type
                TypeId var_tid = declare->type ? declare->type->type_id : expr_tid;

                // Convert value if expression type differs from variable type
                if (var_tid == LMD_TYPE_FLOAT && expr_tid == LMD_TYPE_INT) {
                    // int -> float conversion
                    MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                        MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
                    val = fval;
                } else if (var_tid == LMD_TYPE_INT && expr_tid == LMD_TYPE_FLOAT) {
                    // float -> int conversion
                    MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                        MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                    val = ival;
                }

                MIR_type_t mtype = type_to_mir(var_tid);
                // Store in current scope
                set_var(mt, name_buf, val, mtype, var_tid);

                // Handle error destructuring: let a^err = expr
                if (asn->error_name) {
                    // Box the value for error checking
                    MIR_reg_t boxed = emit_box(mt, val, var_tid);
                    // Check if result is an error: item_type_id(boxed) == LMD_TYPE_ERROR
                    MIR_reg_t type_id = emit_call_1(mt, "item_type_id", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
                    MIR_reg_t is_error = new_reg(mt, "iserr", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_error),
                        MIR_new_reg_op(mt->ctx, type_id),
                        MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));

                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    MIR_label_t l_is_err = new_label(mt);
                    MIR_label_t l_end = new_label(mt);

                    // error_var = is_error ? boxed : ITEM_NULL
                    MIR_reg_t err_result = new_reg(mt, "errv", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_is_err),
                        MIR_new_reg_op(mt->ctx, is_error)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
                    emit_label(mt, l_is_err);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_reg_op(mt->ctx, boxed)));
                    // If error: value_var = ITEM_NULL
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, val),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    set_var(mt, name_buf, val, MIR_T_I64, LMD_TYPE_ANY);
                    emit_label(mt, l_end);

                    char err_name_buf[128];
                    snprintf(err_name_buf, sizeof(err_name_buf), "%.*s",
                        (int)asn->error_name->len, asn->error_name->chars);
                    set_var(mt, err_name_buf, err_result, MIR_T_I64, LMD_TYPE_ANY);
                }
            }
        } else if (declare->node_type == AST_NODE_DECOMPOSE) {
            AstDecomposeNode* dec = (AstDecomposeNode*)declare;
            if (dec->as && dec->name_count > 0) {
                // Evaluate source expression and box it
                MIR_reg_t src_val = transpile_expr(mt, dec->as);
                TypeId src_tid = dec->as->type ? dec->as->type->type_id : LMD_TYPE_ANY;
                MIR_reg_t boxed_src = emit_box(mt, src_val, src_tid);

                for (int i = 0; i < dec->name_count; i++) {
                    String* name = dec->names[i];
                    char name_buf[128];
                    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)name->len, name->chars);

                    MIR_reg_t extracted;
                    if (dec->is_named) {
                        // Named decomposition: item_attr(src, "field_name")
                        // item_attr takes (Item, const char*) → Item
                        // Use name->chars directly (persistent name pool pointer)
                        MIR_reg_t name_str = emit_load_string_literal(mt, name->chars);
                        extracted = emit_call_2(mt, "item_attr", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_src),
                            MIR_T_P, MIR_new_reg_op(mt->ctx, name_str));
                    } else {
                        // Positional decomposition: item_at(src, index)
                        extracted = emit_call_2(mt, "item_at", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_src),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, i));
                    }
                    set_var(mt, name_buf, extracted, MIR_T_I64, LMD_TYPE_ANY);
                }
            }
        } else if (declare->node_type == AST_NODE_FUNC || declare->node_type == AST_NODE_FUNC_EXPR ||
                   declare->node_type == AST_NODE_PROC) {
            // Function definitions are handled in the module-level pre-pass
            // (MIR requires all functions to be defined before generating code)
        }
        declare = declare->next;
    }
}

// ============================================================================
// Array expressions
// ============================================================================

static MIR_reg_t transpile_array(MirTranspiler* mt, AstArrayNode* arr_node) {
    // Check if any child is a for-expression or spread (needs spread path)
    bool has_spreadable = false;
    AstNode* scan = arr_node->item;
    while (scan) {
        if (scan->node_type == AST_NODE_FOR_EXPR || scan->node_type == AST_NODE_SPREAD) {
            has_spreadable = true;
        }
        scan = scan->next;
    }

    // Generic array path
    MIR_reg_t arr = emit_call_0(mt, "array", MIR_T_P);

    // Handle empty arrays without array_end() which returns ITEM_NULL_SPREADABLE
    if (!arr_node->item) {
        emit_call_void_0(mt, "frame_end");
        // Array* pointer is already a valid Item (container pointer)
        return arr;
    }

    AstNode* item = arr_node->item;
    while (item) {
        MIR_reg_t val = transpile_expr(mt, item);
        TypeId val_tid = item->type ? item->type->type_id : LMD_TYPE_ANY;

        if (has_spreadable) {
            // When any child is spreadable, use array_push_spread for ALL children
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_call_void_2(mt, "array_push_spread",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        } else if (item->node_type == AST_NODE_SPREAD) {
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

    return emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, arr));
}

// ============================================================================
// List/content expressions
// ============================================================================

static MIR_reg_t transpile_list(MirTranspiler* mt, AstListNode* list_node) {
    // Check for block expression optimization: 1 value item + declarations
    // In this case, emit declarations then return the single value directly
    AstNode* declare = list_node->declare;
    if (declare) {
        // Count value items
        AstNode* scan = list_node->item;
        int val_count = 0;
        AstNode* last_value = nullptr;
        while (scan) {
            val_count++;
            last_value = scan;
            scan = scan->next;
        }

        if (val_count == 1 && last_value) {
            // Block expression: (let x = 10, x) → process declares, return value
            push_scope(mt);
            while (declare) {
                if (declare->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)declare;
                    if (asn->as) {
                        MIR_reg_t val = transpile_expr(mt, asn->as);
                        char name_buf[128];
                        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                        TypeId tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
                        set_var(mt, name_buf, val, type_to_mir(tid), tid);
                    }
                }
                declare = declare->next;
            }
            MIR_reg_t result = transpile_box_item(mt, last_value);
            pop_scope(mt);
            return result;
        }

        // Multiple values with declarations: process declares then build list
        push_scope(mt);
        while (declare) {
            if (declare->node_type == AST_NODE_ASSIGN) {
                AstNamedNode* asn = (AstNamedNode*)declare;
                if (asn->as) {
                    MIR_reg_t val = transpile_expr(mt, asn->as);
                    char name_buf[128];
                    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                    TypeId tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
                    set_var(mt, name_buf, val, type_to_mir(tid), tid);
                }
            }
            declare = declare->next;
        }

        MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
        AstNode* item = list_node->item;
        while (item) {
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

    // No declarations - simple list
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
            scan->node_type == AST_NODE_TYPE_STAM || scan->node_type == AST_NODE_VAR_STAM ||
            scan->node_type == AST_NODE_FUNC ||
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
            if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                transpile_let_stam(mt, (AstLetNode*)item);
            }
            // Func defs are handled in module-level pre-pass
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
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
            transpile_let_stam(mt, (AstLetNode*)item);
        }
        // Func defs handled in module-level pre-pass
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
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM ||
            item->node_type == AST_NODE_FUNC ||
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

    // Create map: Map* m = map(type_index)  — this calls frame_start()
    MIR_reg_t m = emit_call_1(mt, "map", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, type_index));

    // Count value items
    AstNode* item = map_node->item;
    int val_count = 0;
    while (item) { val_count++; item = item->next; }

    if (val_count == 0) {
        // No fields - call frame_end() and return map
        MirImportEntry* fe = ensure_import(mt, "frame_end", MIR_T_I64, 0, NULL, 0);
        emit_insn(mt, MIR_new_call_insn(mt->ctx, 2,
            MIR_new_ref_op(mt->ctx, fe->proto),
            MIR_new_ref_op(mt->ctx, fe->import)));
        return m;
    }

    // Evaluate all values into boxed Item ops
    MIR_op_t* val_ops = (MIR_op_t*)alloca(val_count * sizeof(MIR_op_t));
    item = map_node->item;
    int vi = 0;
    while (item) {
        if (item->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* key_expr = (AstNamedNode*)item;
            if (key_expr->as) {
                MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
            } else {
                MIR_reg_t nul = new_reg(mt, "mapnull", MIR_T_I64);
                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, nul);
            }
        } else {
            MIR_reg_t val = transpile_box_item(mt, item);
            val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
        }
        item = item->next;
    }

    // Call map_fill(m, val1, val2, ...) — variadic, calls frame_end() internally
    MIR_reg_t filled = emit_vararg_call(mt, "map_fill", MIR_T_P, 1,
        MIR_T_P, MIR_new_reg_op(mt->ctx, m), vi, val_ops);

    return filled;
}

// ============================================================================
// Element expressions
// ============================================================================

static MIR_reg_t transpile_element(MirTranspiler* mt, AstElementNode* elmt_node) {
    if (!elmt_node || !elmt_node->type) {
        log_error("mir: transpile_element with null node or type");
        MIR_reg_t r = new_reg(mt, "elmt_err", MIR_T_I64);
        uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
        return r;
    }

    TypeElmt* type = (TypeElmt*)elmt_node->type;

    // Create element: Element* el = elmt(type_index) — calls frame_start() if has attrs/content
    MIR_reg_t el = emit_call_1(mt, "elmt", MIR_T_P, MIR_T_I64,
        MIR_new_int_op(mt->ctx, type->type_index));

    // Fill attributes if present
    AstNode* item = elmt_node->item;
    if (item) {
        // Count attribute values
        int attr_count = 0;
        AstNode* scan = item;
        while (scan) { attr_count++; scan = scan->next; }

        // Evaluate attribute values
        MIR_op_t* attr_ops = (MIR_op_t*)alloca(attr_count * sizeof(MIR_op_t));
        int ai = 0;
        scan = item;
        while (scan) {
            if (scan->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)scan;
                if (key_expr->as) {
                    MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                    attr_ops[ai++] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    MIR_reg_t nul = new_reg(mt, "atnull", MIR_T_I64);
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    attr_ops[ai++] = MIR_new_reg_op(mt->ctx, nul);
                }
            } else {
                MIR_reg_t val = transpile_box_item(mt, scan);
                attr_ops[ai++] = MIR_new_reg_op(mt->ctx, val);
            }
            scan = scan->next;
        }

        // Call elmt_fill(el, val1, val2, ...) — variadic, does NOT call frame_end
        emit_vararg_call(mt, "elmt_fill", MIR_T_P, 1,
            MIR_T_P, MIR_new_reg_op(mt->ctx, el), ai, attr_ops);
    }

    // Fill content if present
    if (type->content_length) {
        if (elmt_node->content) {
            AstListNode* content_list = (AstListNode*)elmt_node->content;
            AstNode* content_item = content_list->item;

            if (type->content_length < 10) {
                // Use list_fill(el, count, val1, val2, ...) for small content
                // Count content items
                int content_count = 0;
                AstNode* cscan = content_item;
                while (cscan) { content_count++; cscan = cscan->next; }

                MIR_op_t* content_ops = (MIR_op_t*)alloca(content_count * sizeof(MIR_op_t));
                int ci = 0;
                cscan = content_item;
                while (cscan) {
                    MIR_reg_t val = transpile_box_item(mt, cscan);
                    content_ops[ci++] = MIR_new_reg_op(mt->ctx, val);
                    cscan = cscan->next;
                }

                // list_fill(el, count, items...) — returns Item
                emit_vararg_call_2(mt, "list_fill", MIR_T_I64, 1,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, el),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, content_count),
                    ci, content_ops);
            } else {
                // Use list_push_spread for each content item, then list_end
                while (content_item) {
                    MIR_reg_t val = transpile_box_item(mt, content_item);
                    emit_call_void_2(mt, "list_push_spread",
                        MIR_T_P, MIR_new_reg_op(mt->ctx, el),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    content_item = content_item->next;
                }
                emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
            }
        } else {
            // content_length but no content node — just list_end
            emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
        }
    } else {
        // No content
        if (elmt_node->item) {
            // Has attributes but no content — call list_end to finalize frame
            emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
        }
        // else: no attrs and no content — element is just a bare pointer, no frame_end needed
    }

    return el;
}

// ============================================================================
// Member/Index access
// ============================================================================

static MIR_reg_t transpile_member(MirTranspiler* mt, AstFieldNode* field_node) {
    MIR_reg_t obj = transpile_expr(mt, field_node->object);
    TypeId obj_tid = field_node->object->type ? field_node->object->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_obj = emit_box(mt, obj, obj_tid);

    // Field name for member access: extract name from ident and create boxed string Item
    AstNode* field = field_node->field;
    MIR_reg_t boxed_field;
    if (field->node_type == AST_NODE_IDENT) {
        // IDENT node has no type info for member fields — use name pool String* directly
        AstIdentNode* ident = (AstIdentNode*)field;
        String* str = ident->name; // persistent name pool pointer
        uint64_t str_item = ((uint64_t)LMD_TYPE_STRING << 56) | (uint64_t)(uintptr_t)str;
        boxed_field = new_reg(mt, "fld", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, boxed_field),
            MIR_new_int_op(mt->ctx, (int64_t)str_item)));
    } else {
        // Non-ident field (computed member): transpile as expression
        boxed_field = transpile_box_item(mt, field);
    }

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

        // ==== VMap: map() and map(arr) ====
        if (info->fn == SYSFUNC_VMAP_NEW) {
            if (arg_count == 0) {
                // map() -> vmap_new()
                return emit_call_0(mt, "vmap_new", MIR_T_I64);
            } else {
                // map([k1, v1, ...]) -> vmap_from_array(arr)
                arg = call_node->argument;
                MIR_reg_t a1 = transpile_expr(mt, arg);
                TypeId a1_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
                MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);
                return emit_call_1(mt, "vmap_from_array", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            }
        }
        // ==== VMap: m.set(k, v) -> vmap_set(m, k, v) ====
        if (info->fn == SYSPROC_VMAP_SET) {
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

            return emit_call_3(mt, "vmap_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
        }

        // Build runtime function name: "fn_" or "pn_" + name + optional arg count for overloaded
        char sys_fn_name[128];
        if (info->is_overloaded) {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s%d",
                info->is_proc ? "pn_" : "fn_", info->name, arg_count);
        } else {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s",
                info->is_proc ? "pn_" : "fn_", info->name);
        }

        // Determine the actual C return type based on the SysFunc enum value.
        // The return_type in SysFuncInfo is the Lambda-level semantic type, NOT the C type.
        // Some funcs with same Lambda return type (e.g., TYPE_STRING) return String* in C
        // while others return Item. We must use the enum to distinguish.
        TypeId c_ret_tid = LMD_TYPE_ANY;  // default: C function returns Item
        switch (info->fn) {
        // C functions returning int64_t
        case SYSFUNC_LEN: case SYSFUNC_INT64:
        case SYSFUNC_INDEX_OF: case SYSFUNC_LAST_INDEX_OF:
        case SYSFUNC_BAND: case SYSFUNC_BOR: case SYSFUNC_BXOR:
        case SYSFUNC_BNOT: case SYSFUNC_SHL: case SYSFUNC_SHR:
            c_ret_tid = LMD_TYPE_INT; break;
        // C functions returning Bool (uint8_t)
        case SYSFUNC_CONTAINS: case SYSFUNC_STARTS_WITH: case SYSFUNC_ENDS_WITH:
        case SYSFUNC_EXISTS:
            c_ret_tid = LMD_TYPE_BOOL; break;
        // C functions returning String*
        case SYSFUNC_STRING: case SYSFUNC_FORMAT1: case SYSFUNC_FORMAT2:
            c_ret_tid = LMD_TYPE_STRING; break;
        // C functions returning Symbol*
        case SYSFUNC_NAME: case SYSFUNC_SYMBOL:
            c_ret_tid = LMD_TYPE_SYMBOL; break;
        // C functions returning Type*
        case SYSFUNC_TYPE:
            c_ret_tid = LMD_TYPE_TYPE; break;
        // C functions returning DateTime (uint64_t)
        case SYSFUNC_DATETIME: case SYSFUNC_DATETIME0:
        case SYSFUNC_DATE: case SYSFUNC_DATE0: case SYSFUNC_DATE3:
        case SYSFUNC_TIME: case SYSFUNC_TIME0: case SYSFUNC_TIME3:
        case SYSFUNC_JUSTNOW:
            c_ret_tid = LMD_TYPE_DTIME; break;
        // C functions returning double
        case SYSPROC_CLOCK:
            c_ret_tid = LMD_TYPE_FLOAT; break;
        default: break;  // returns Item, no boxing needed
        }
        MIR_type_t mir_ret_type = (c_ret_tid == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;

        // For 0-arg functions like datetime(), date() etc
        if (arg_count == 0) {
            return emit_call_0(mt, sys_fn_name, mir_ret_type);
        }

        // For 1-arg system functions
        if (arg_count == 1) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);
            return emit_call_1(mt, sys_fn_name, mir_ret_type, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
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

            return emit_call_2(mt, sys_fn_name, mir_ret_type,
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

            return emit_call_3(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
        }

        // Fallback for more args: use vararg call
        {
            arg = call_node->argument;
            MIR_op_t arg_ops[16];
            int ai = 0;
            while (arg && ai < 16) {
                MIR_reg_t v = transpile_expr(mt, arg);
                TypeId tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
                MIR_reg_t boxed = emit_box(mt, v, tid);
                arg_ops[ai++] = MIR_new_reg_op(mt->ctx, boxed);
                arg = arg->next;
            }

            // Create unique vararg proto name per call site
            char proto_name[160];
            snprintf(proto_name, sizeof(proto_name), "%s_sp%d", sys_fn_name, mt->label_counter++);

            // First arg is mandatory, rest are varargs
            MIR_var_t first_arg = {MIR_T_I64, "a", 0};
            MIR_type_t res_types[1] = { mir_ret_type };
            MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, 1, res_types, 1, &first_arg);
            MIR_item_t imp = MIR_new_import(mt->ctx, sys_fn_name);

            int nops = 3 + ai;
            MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
            ops[0] = MIR_new_ref_op(mt->ctx, proto);
            ops[1] = MIR_new_ref_op(mt->ctx, imp);
            MIR_reg_t result = new_reg(mt, "sys", mir_ret_type);
            ops[2] = MIR_new_reg_op(mt->ctx, result);
            for (int i = 0; i < ai; i++) ops[3 + i] = arg_ops[i];

            emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            return result;
        }
    }

    // User-defined function call
    if (fn_expr->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)fn_expr;
        if (pri->expr) fn_expr = pri->expr;
    }

    if (fn_expr->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)fn_expr;
        AstNode* entry_node = ident->entry ? ident->entry->node : nullptr;

        // Try entry-based lookup first, then raw name fallback
        const char* fn_mangled = nullptr;
        StrBuf* name_buf = nullptr;
        MIR_item_t local_func = nullptr;

        if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR ||
            entry_node->node_type == AST_NODE_PROC)) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;
            name_buf = strbuf_new_cap(64);
            write_fn_name(name_buf, fn_node, ident->entry->import);
            fn_mangled = name_buf->str;
            local_func = find_local_func(mt, fn_mangled);
        }

        // Fallback: try raw name lookup when entry is NULL or mangled name not found
        char raw_name[128];
        if (!local_func) {
            snprintf(raw_name, sizeof(raw_name), "%.*s", (int)ident->name->len, ident->name->chars);
            local_func = find_local_func(mt, raw_name);
            if (local_func) fn_mangled = raw_name;
        }

        if (fn_mangled && (local_func || entry_node)) {

            // Get function definition for param info (defaults, named args)
            AstFuncNode* fn_def = NULL;
            if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
                entry_node->node_type == AST_NODE_FUNC_EXPR ||
                entry_node->node_type == AST_NODE_PROC)) {
                fn_def = (AstFuncNode*)entry_node;
            }

            // Get expected param count
            int expected_params = 0;
            if (fn_def && fn_def->type && fn_def->type->type_id == LMD_TYPE_FUNC) {
                TypeFunc* ft = (TypeFunc*)fn_def->type;
                expected_params = ft->param_count;
            }

            // Check if any arguments are named
            bool has_named_args = false;
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) {
                if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
                arg_count++;
                arg = arg->next;
            }
            if (expected_params == 0) expected_params = arg_count;

            // Build resolved_args array: maps each parameter position to its argument expression
            AstNode* resolved_args[16] = {0};

            if (has_named_args && fn_def) {
                // Reorder: named args go to their param positions, positional fill sequentially
                int positional_idx = 0;
                arg = call_node->argument;
                while (arg) {
                    if (arg->node_type == AST_NODE_NAMED_ARG) {
                        AstNamedNode* named_arg = (AstNamedNode*)arg;
                        // Find param index by name
                        int param_idx = 0;
                        AstNamedNode* param = fn_def->param;
                        while (param) {
                            if (param->name && named_arg->name &&
                                param->name->len == named_arg->name->len &&
                                memcmp(param->name->chars, named_arg->name->chars, param->name->len) == 0) {
                                if (param_idx < 16) resolved_args[param_idx] = named_arg->as;
                                break;
                            }
                            param_idx++;
                            param = (AstNamedNode*)((AstNode*)param)->next;
                        }
                    } else {
                        // Skip positional slots already filled by named args
                        while (positional_idx < 16 && resolved_args[positional_idx]) positional_idx++;
                        if (positional_idx < 16) resolved_args[positional_idx++] = arg;
                    }
                    arg = arg->next;
                }
            } else {
                // Simple positional: fill in order
                arg = call_node->argument;
                for (int i = 0; i < arg_count && i < 16; i++) {
                    resolved_args[i] = arg;
                    arg = arg->next;
                }
            }

            // Emit args in parameter order, filling defaults for missing slots
            MIR_op_t arg_ops[16];
            MIR_var_t arg_vars[16];
            int ai = 0;
            AstNamedNode* param_iter = fn_def ? fn_def->param : NULL;
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            for (int i = 0; i < expected_params && i < 16; i++) {
                if (resolved_args[i]) {
                    MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    // Check for default value in TypeParam
                    TypeParam* tp = param_iter ? (TypeParam*)((AstNode*)param_iter)->type : NULL;
                    if (tp && tp->default_value) {
                        MIR_reg_t val = transpile_box_item(mt, tp->default_value);
                        arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                    } else {
                        MIR_reg_t null_reg = new_reg(mt, "pad", MIR_T_I64);
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, null_reg),
                            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                        arg_ops[i] = MIR_new_reg_op(mt->ctx, null_reg);
                    }
                }
                arg_vars[i] = {MIR_T_I64, "p", 0};
                if (param_iter) param_iter = (AstNamedNode*)((AstNode*)param_iter)->next;
            }
            ai = expected_params;

            // Return type is always Item (MIR_T_I64) for user functions
            MIR_type_t ret_type = MIR_T_I64;

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

            // Unbox return value if the call expression has a native type
            TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
            if (call_tid == LMD_TYPE_FLOAT || call_tid == LMD_TYPE_INT || call_tid == LMD_TYPE_BOOL) {
                result = emit_unbox(mt, result, call_tid);
            }

            if (name_buf) strbuf_free(name_buf);
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

    MIR_reg_t dyn_result;

    if (arg_count == 0) {
        dyn_result = emit_call_1(mt, call_fn, MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn));
    } else if (arg_count <= 3) {
        MIR_reg_t args[3];
        arg = call_node->argument;
        for (int i = 0; i < arg_count; i++) {
            // Use transpile_box_item to correctly handle BINARY/UNARY with native returns
            args[i] = transpile_box_item(mt, arg);
            arg = arg->next;
        }
        if (arg_count == 1) {
            dyn_result = emit_call_2(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]));
        } else if (arg_count == 2) {
            dyn_result = emit_call_3(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[1]));
        } else {
            // 3 args
            MIR_var_t avars[4] = {{MIR_T_I64,"f",0},{MIR_T_I64,"a",0},{MIR_T_I64,"b",0},{MIR_T_I64,"c",0}};
            MirImportEntry* ie = ensure_import(mt, call_fn, MIR_T_I64, 4, avars, 1);
            dyn_result = new_reg(mt, "call3", MIR_T_I64);
            emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                MIR_new_ref_op(mt->ctx, ie->proto),
                MIR_new_ref_op(mt->ctx, ie->import),
                MIR_new_reg_op(mt->ctx, dyn_result),
                MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_new_reg_op(mt->ctx, args[0]),
                MIR_new_reg_op(mt->ctx, args[1]),
                MIR_new_reg_op(mt->ctx, args[2])));
        }
    } else {
        // More than 3 args: use fn_call with list
        log_error("mir: calls with >3 args not yet fully supported");
        dyn_result = boxed_fn;
    }

    // Dynamic calls (fn_call0/1/2/3) return Item (already boxed).
    // Unbox to native type to match direct call behavior, so callers
    // can re-box consistently based on AST type.
    TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
    if (call_tid == LMD_TYPE_FLOAT || call_tid == LMD_TYPE_INT || call_tid == LMD_TYPE_BOOL) {
        dyn_result = emit_unbox(mt, dyn_result, call_tid);
    }
    return dyn_result;
}

// ============================================================================
// Pipe expressions
// ============================================================================

static MIR_reg_t transpile_pipe(MirTranspiler* mt, AstPipeNode* pipe_node) {
    MIR_reg_t left = transpile_expr(mt, pipe_node->left);
    TypeId left_tid = pipe_node->left->type ? pipe_node->left->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_left = emit_box(mt, left, left_tid);

    bool uses_current_item = has_current_item_ref(pipe_node->right);

    // Simple aggregate pipe without ~ : data | func -> func(data)
    if (!uses_current_item && pipe_node->op == OPERATOR_PIPE) {
        MIR_reg_t right = transpile_expr(mt, pipe_node->right);
        TypeId right_tid = pipe_node->right->type ? pipe_node->right->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed_right = emit_box(mt, right, right_tid);
        return emit_call_2(mt, "fn_pipe_call", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Iterating pipe (uses ~ or WHERE/THAT) - generate inline loop
    // Create result array
    MIR_reg_t result_arr = emit_call_0(mt, "array", MIR_T_P);

    // Check if collection is a MAP or VMAP (maps need key-based iteration)
    MIR_reg_t type_id_reg = emit_call_1(mt, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));

    MIR_reg_t is_map = new_reg(mt, "is_map", MIR_T_I64);
    MIR_reg_t is_map_t2 = new_reg(mt, "is_map_t2", MIR_T_I64);
    MIR_reg_t is_vmap_t2 = new_reg(mt, "is_vmap_t2", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_vmap_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_VMAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map),
        MIR_new_reg_op(mt->ctx, is_map_t2), MIR_new_reg_op(mt->ctx, is_vmap_t2)));

    // Pre-declare len, keys_al registers
    MIR_reg_t len = new_reg(mt, "pipe_len", MIR_T_I64);
    MIR_reg_t keys_al = new_reg(mt, "pipe_keys", MIR_T_P);

    // Initialize keys_al to NULL
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_non_map = new_label(mt);
    MIR_label_t l_start_loop = new_label(mt);

    // Branch: if not map, use normal fn_len path
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_non_map),
        MIR_new_reg_op(mt->ctx, is_map)));

    // MAP path: get keys and length from keys
    MIR_reg_t keys_result = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_reg_op(mt->ctx, keys_result)));
    MIR_reg_t map_len = emit_call_1(mt, "pipe_map_len", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, map_len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start_loop)));

    // NON-MAP path: use fn_len
    emit_label(mt, l_non_map);
    MIR_reg_t arr_len = emit_call_1(mt, "fn_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, arr_len)));

    emit_label(mt, l_start_loop);

    // Index counter
    MIR_reg_t idx = new_reg(mt, "pipe_i", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_continue = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_label(mt, l_loop);
    // Exit when idx >= len
    MIR_reg_t cmp = new_reg(mt, "pipe_cmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get pipe_item and pipe_index based on collection type
    MIR_reg_t pipe_item = new_reg(mt, "pipe_item", MIR_T_I64);
    MIR_reg_t pipe_index = new_reg(mt, "pipe_idx_item", MIR_T_I64);

    MIR_label_t l_arr_item = new_label(mt);
    MIR_label_t l_got_item = new_label(mt);

    // Branch: if not map, use item_at
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_arr_item),
        MIR_new_reg_op(mt->ctx, is_map)));

    // MAP path: get value and key from map using keys ArrayList
    MIR_reg_t map_val = emit_call_3(mt, "pipe_map_val", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
        MIR_new_reg_op(mt->ctx, map_val)));
    MIR_reg_t map_key = emit_call_2(mt, "pipe_map_key", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_index),
        MIR_new_reg_op(mt->ctx, map_key)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_got_item)));

    // ARRAY/LIST/RANGE path: use item_at
    emit_label(mt, l_arr_item);
    MIR_reg_t arr_item = emit_call_2(mt, "item_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
        MIR_new_reg_op(mt->ctx, arr_item)));
    MIR_reg_t arr_idx = emit_box_int(mt, idx);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_index),
        MIR_new_reg_op(mt->ctx, arr_idx)));

    emit_label(mt, l_got_item);

    // Save old pipe state and set new
    bool old_in_pipe = mt->in_pipe;
    MIR_reg_t old_pipe_item = mt->pipe_item_reg;
    MIR_reg_t old_pipe_index = mt->pipe_index_reg;
    mt->in_pipe = true;
    mt->pipe_item_reg = pipe_item;
    mt->pipe_index_reg = pipe_index;

    // Evaluate right-side expression
    MIR_reg_t right_val = transpile_expr(mt, pipe_node->right);
    TypeId right_tid = pipe_node->right->type ? pipe_node->right->type->type_id : LMD_TYPE_ANY;
    MIR_reg_t boxed_right = emit_box(mt, right_val, right_tid);

    // Restore old pipe state
    mt->in_pipe = old_in_pipe;
    mt->pipe_item_reg = old_pipe_item;
    mt->pipe_index_reg = old_pipe_index;

    if (pipe_node->op == OPERATOR_WHERE) {
        // Filter: only push original item if predicate is truthy
        MIR_reg_t truthy;
        if (right_tid == LMD_TYPE_BOOL) {
            // right_val is already native Bool (0/1), use directly
            truthy = right_val;
        } else {
            // Need to box and check truthiness
            truthy = emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
        }
        MIR_label_t l_skip = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_skip),
            MIR_new_reg_op(mt->ctx, truthy)));
        // Push original item
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pipe_item));
        emit_label(mt, l_skip);
    } else {
        // Map: push transformed result
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Continue: increment index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);

    // Finalize array — array_end returns Item
    MIR_reg_t final = emit_call_1(mt, "array_end", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr));
    return final;
}

// ============================================================================
// Raise expressions
// ============================================================================

static MIR_reg_t transpile_raise(MirTranspiler* mt, AstRaiseNode* raise_node) {
    if (raise_node->value) {
        // Evaluate the raise value and return it directly (like C transpiler)
        // The value is typically already an error from error() call
        MIR_reg_t val = transpile_expr(mt, raise_node->value);
        TypeId val_tid = raise_node->value->type ? raise_node->value->type->type_id : LMD_TYPE_ANY;
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        // Return the error value from the function
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed)));
        // Dummy register for unreachable code after return
        MIR_reg_t r = new_reg(mt, "raise_dummy", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    MIR_reg_t r = new_reg(mt, "err", MIR_T_I64);
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, r)));
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
        log_debug("mir: box_item: node_type=%d has NULL type, skipping boxing",
            node ? node->node_type : -1);
        MIR_reg_t val = transpile_expr(mt, node);
        return val;
    }

    TypeId tid = node->type->type_id;
    log_debug("mir: box_item: node_type=%d, type_id=%d, is_literal=%d",
        node->node_type, tid, node->type->is_literal);

    // PRIMARY nodes are transparent wrappers (parenthesized expressions).
    // Delegate to the inner expression so BINARY/UNARY-specific boxing applies.
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr && (tid == LMD_TYPE_ANY || tid == LMD_TYPE_NUMBER)) {
            return transpile_box_item(mt, pri->expr);
        }
    }

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

    // For BINARY/UNARY nodes: check if the result is already a boxed Item
    // from the runtime fallback path (not native handling).
    // transpile_binary returns NATIVE values only for specific type+op combos;
    // everything else goes through boxed runtime and returns an already-boxed Item.
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)node;
        TypeId lt = bi->left && bi->left->type ? bi->left->type->type_id : LMD_TYPE_ANY;
        TypeId rt = bi->right && bi->right->type ? bi->right->type->type_id : LMD_TYPE_ANY;
        bool both_int = (lt == LMD_TYPE_INT) && (rt == LMD_TYPE_INT);
        bool both_float = (lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_FLOAT);
        bool int_float = (lt == LMD_TYPE_INT && rt == LMD_TYPE_FLOAT) ||
                         (lt == LMD_TYPE_FLOAT && rt == LMD_TYPE_INT);
        bool both_bool = (lt == LMD_TYPE_BOOL) && (rt == LMD_TYPE_BOOL);
        bool both_string = (lt == LMD_TYPE_STRING) && (rt == LMD_TYPE_STRING);
        int op = bi->op;

        // Enumerate ALL cases where transpile_binary returns native values:

        // 1. Comparisons (EQ-GE) always return native bool (both native MIR and fn_eq/fn_lt/etc.)
        bool is_cmp = (op >= OPERATOR_EQ && op <= OPERATOR_GE);
        if (is_cmp) {
            return emit_box_bool(mt, val);
        }

        // 1b. IS and IN also return native Bool from fn_is/fn_in
        if (op == OPERATOR_IS || op == OPERATOR_IN) {
            return emit_box_bool(mt, val);
        }

        // 2. AND/OR with both_bool → native bool
        if ((op == OPERATOR_AND || op == OPERATOR_OR) && both_bool) {
            return emit_box_bool(mt, val);
        }

        // 3. both_int arithmetic: ADD,SUB,MUL → native int; DIV → native float
        //    IDIV,MOD always go through boxed runtime (handled before native block)
        //    POW falls through to boxed runtime (AST type is ANY)
        if (both_int) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
                return emit_box_int(mt, val);
            case OPERATOR_DIV:
                return emit_box_float(mt, val);
            default:
                return val; // POW, IDIV, MOD, JOIN, etc. → boxed fallback
            }
        }

        // 4. both_float: ADD,SUB,MUL,DIV → native float
        //    IDIV,MOD,POW fall through to boxed runtime
        if (both_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,MOD,POW with float → boxed fallback
            }
        }

        // 5. int_float: ADD,SUB,MUL,DIV → native float
        //    IDIV,MOD,POW fall through to boxed runtime
        if (int_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,MOD,POW with int_float → boxed fallback
            }
        }

        // 6. JOIN with both_string → native String*
        if (op == OPERATOR_JOIN && both_string) {
            return emit_box_string(mt, val);
        }

        // Everything else: transpile_binary used boxed runtime → result is already boxed Item
        return val;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* un = (AstUnaryNode*)node;
        TypeId ct = un->operand && un->operand->type ? un->operand->type->type_id : LMD_TYPE_ANY;
        int uop = un->op;

        // NEG: native for INT (→int), FLOAT (→float); otherwise boxed
        if (uop == OPERATOR_NEG) {
            if (ct == LMD_TYPE_INT) return emit_box_int(mt, val);
            if (ct == LMD_TYPE_FLOAT) return emit_box_float(mt, val);
            return val; // fn_neg → boxed
        }
        // NOT: always returns native bool (MIR_EQ for BOOL operand, fn_not returns Bool/uint8_t)
        if (uop == OPERATOR_NOT) {
            return emit_box_bool(mt, val);
        }
        // POS: for native numeric types, box as that type; otherwise fn_pos already returns boxed Item
        if (uop == OPERATOR_POS) {
            if (ct == LMD_TYPE_INT) return emit_box_int(mt, val);
            if (ct == LMD_TYPE_FLOAT) return emit_box_float(mt, val);
            // INT64, DTIME, DECIMAL: POS returns the operand directly from
            // transpile_expr, which is already a boxed Item. No re-boxing needed.
            return val;
        }
        // IS_ERROR: always returns native bool regardless of operand type
        if (uop == OPERATOR_IS_ERROR) {
            return emit_box_bool(mt, val);
        }
        // Default: assume boxed
        return val;
    }

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

    // If this is a TypeType, get the actual type and check for special cases
    if (type_node->type && type_node->type->type_id == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)type_node->type;
        if (tt->type) {
            // For datetime sub-types (date, time), use const_type to preserve the specific Type pointer
            extern Type TYPE_DATE, TYPE_TIME;
            if (tt->type == &TYPE_DATE || tt->type == &TYPE_TIME) {
                // Cannot use base_type because date/time/dtime share the same type_id
                // Must use the original Type* pointer via type_list
                // Fall through to base_type for now - this will be improved when type_list is accessible
            }
            tid = tt->type->type_id;
        }
    }

    return emit_call_1(mt, "base_type", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, tid));
}

// Emit const_type(type_index) for complex types that need runtime type_index lookup
static MIR_reg_t transpile_const_type(MirTranspiler* mt, int type_index) {
    return emit_call_1(mt, "const_type", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, type_index));
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
    case AST_NODE_ELEMENT:
        return transpile_element(mt, (AstElementNode*)node);
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
    case AST_NODE_KEY_EXPR: {
        // Key expression - transpile the value part
        AstNamedNode* key = (AstNamedNode*)node;
        if (key->as) return transpile_expr(mt, key->as);
        MIR_reg_t r = new_reg(mt, "key", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_BINARY_TYPE: {
        // Union type in match pattern: const_type(type_index)
        AstBinaryNode* bin = (AstBinaryNode*)node;
        if (bin->type && bin->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)bin->type;
            if (tt->type) {
                TypeBinary* bt = (TypeBinary*)tt->type;
                return emit_call_1(mt, "const_type", MIR_T_P, MIR_T_I64,
                    MIR_new_int_op(mt->ctx, bt->type_index));
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_UNARY_TYPE: {
        // Unary type (e.g. int?, int+, int[2]): use const_type(type_index)
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeUnary* ut = (TypeUnary*)tt->type;
                return transpile_const_type(mt, ut->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE: {
        // List type: use const_type(type_index)
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeList* lt = (TypeList*)tt->type;
                return transpile_const_type(mt, lt->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_ARRAY_TYPE: {
        // Array type (e.g. [int], int[2]): use const_type(type_index)
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeArray* at = (TypeArray*)tt->type;
                return transpile_const_type(mt, at->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_MAP_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeMap* mp = (TypeMap*)tt->type;
                return transpile_const_type(mt, mp->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_ELMT_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeElmt* et = (TypeElmt*)tt->type;
                return transpile_const_type(mt, et->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_FUNC_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeFunc* ft = (TypeFunc*)tt->type;
                return transpile_const_type(mt, ft->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_CONSTRAINED_TYPE: {
        // Constrained type (e.g. int where (~ > 0)): use const_type(type_index)
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)node;
        TypeConstrained* constrained = (TypeConstrained*)ct->type;
        if (constrained) {
            return transpile_const_type(mt, constrained->type_index);
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_NAMED_ARG: {
        // Named argument: transpile the value expression
        AstNamedNode* named = (AstNamedNode*)node;
        if (named->as) return transpile_expr(mt, named->as);
        MIR_reg_t r = new_reg(mt, "narg", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_PATH_EXPR: {
        // Path expression: build path object using path_new and path_extend
        // For now, fall through to boxed fallback which handles it via runtime
        log_debug("mir: path_expr - using boxed fallback");
        MIR_reg_t r = new_reg(mt, "path", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_PARENT_EXPR: {
        // Parent access: expr.. → fn_member(expr, "parent") repeated for depth levels
        AstParentNode* parent = (AstParentNode*)node;
        if (parent->object) {
            MIR_reg_t obj = transpile_expr(mt, parent->object);
            TypeId obj_tid = parent->object->type ? parent->object->type->type_id : LMD_TYPE_ANY;
            MIR_reg_t current = emit_box(mt, obj, obj_tid);

            // Create a boxed string Item for "parent" at compile time
            String* parent_str = heap_create_name("parent");
            uint64_t parent_item = ((uint64_t)LMD_TYPE_STRING << 56) | (uint64_t)(uintptr_t)parent_str;

            for (int i = 0; i < parent->depth; i++) {
                MIR_reg_t parent_key = new_reg(mt, "pkey", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, parent_key),
                    MIR_new_int_op(mt->ctx, (int64_t)parent_item)));
                current = emit_call_2(mt, "fn_member", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, current),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, parent_key));
            }
            return current;
        }
        MIR_reg_t r = new_reg(mt, "parent", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        // Look up the MIR function created in pre-pass and create a runtime Function object
        AstFuncNode* fn_node = (AstFuncNode*)node;
        StrBuf* name_buf = strbuf_new_cap(64);
        write_fn_name(name_buf, fn_node, NULL);

        MIR_item_t func_item = find_local_func(mt, name_buf->str);
        if (func_item) {
            // Count arity from AST param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Get function's native code address via MIR ref
            MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                MIR_new_ref_op(mt->ctx, func_item)));

            // Call to_fn_n(fn_ptr, arity) -> Function*
            MIR_reg_t fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arity));

            strbuf_free(name_buf);
            return fn_obj;  // Function* is a container - type_id in first byte
        }

        strbuf_free(name_buf);
        // Function not found in pre-pass - return null
        log_error("mir: function not found in pre-pass: %s", name_buf->str);
        MIR_reg_t r = new_reg(mt, "def", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_IMPORT: {
        // Definitions handled in root pass - return null as placeholder
        MIR_reg_t r = new_reg(mt, "defp", MIR_T_I64);
        uint64_t NULL_VAL2 = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL2)));
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
    MIR_reg_t saved_consts_reg = mt->consts_reg;

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

    // Set up consts_reg for this function by loading from global _lambda_rt
    // _lambda_rt is a Context* stored at a global address resolved by import
    MIR_item_t rt_import_fn = MIR_new_import(mt->ctx, "_lambda_rt");
    MIR_reg_t rt_addr_fn = new_reg(mt, "rt_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_addr_fn),
        MIR_new_ref_op(mt->ctx, rt_import_fn)));
    // Dereference: runtime = *(&_lambda_rt)
    MIR_reg_t runtime_fn = new_reg(mt, "runtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, runtime_fn),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr_fn, 0, 1)));
    // Load consts: runtime->consts
    mt->consts_reg = new_reg(mt, "consts", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->consts_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, consts), runtime_fn, 0, 1)));

    // Register as local function early (before body transpilation for recursion)
    register_local_func(mt, name_buf->str, func_item);

    // Also register by raw name for fallback lookup when ident->entry is NULL
    if (fn_node->name && fn_node->name->chars) {
        char raw_name[128];
        snprintf(raw_name, sizeof(raw_name), "%.*s", (int)fn_node->name->len, fn_node->name->chars);
        if (!find_local_func(mt, raw_name)) {
            register_local_func(mt, raw_name, func_item);
        }
    }

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
    mt->consts_reg = saved_consts_reg;

    strbuf_free(name_buf);
}

// ============================================================================
// Forward-declaration pass: create MIR forward declarations for ALL functions
// BEFORE any function bodies are transpiled. This allows forward references
// between functions (e.g., first() calling second() defined later).
// ============================================================================

static void prepass_forward_declare(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            StrBuf* name_buf = strbuf_new_cap(64);
            write_fn_name(name_buf, fn_node, NULL);

            // Create a forward declaration at module level
            MIR_item_t fwd = MIR_new_forward(mt->ctx, name_buf->str);
            register_local_func(mt, name_buf->str, fwd);

            // Also register by raw name for fallback lookup
            if (fn_node->name && fn_node->name->chars) {
                char raw_name[128];
                snprintf(raw_name, sizeof(raw_name), "%.*s",
                    (int)fn_node->name->len, fn_node->name->chars);
                if (!find_local_func(mt, raw_name)) {
                    register_local_func(mt, raw_name, fwd);
                }
            }

            strbuf_free(name_buf);

            // Recurse into function body to find nested function definitions
            if (fn_node->body) prepass_forward_declare(mt, fn_node->body);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_forward_declare(mt, list->declare);
            prepass_forward_declare(mt, list->item);
            break;
        }
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_forward_declare(mt, list->declare);
            prepass_forward_declare(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_forward_declare(mt, let_node->declare);
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_PARAM:
        case AST_NODE_NAMED_ARG: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as) prepass_forward_declare(mt, named->as);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            if (pri->expr) prepass_forward_declare(mt, pri->expr);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (call->function) prepass_forward_declare(mt, call->function);
            if (call->argument) prepass_forward_declare(mt, call->argument);
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            if (bi->left) prepass_forward_declare(mt, bi->left);
            if (bi->right) prepass_forward_declare(mt, bi->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->operand) prepass_forward_declare(mt, un->operand);
            break;
        }
        case AST_NODE_IF_EXPR:
        case AST_NODE_IF_STAM: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->cond) prepass_forward_declare(mt, if_node->cond);
            if (if_node->then) prepass_forward_declare(mt, if_node->then);
            if (if_node->otherwise) prepass_forward_declare(mt, if_node->otherwise);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->loop) prepass_forward_declare(mt, for_node->loop);
            if (for_node->let_clause) prepass_forward_declare(mt, for_node->let_clause);
            if (for_node->where) prepass_forward_declare(mt, for_node->where);
            if (for_node->then) prepass_forward_declare(mt, for_node->then);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            if (match->scrutinee) prepass_forward_declare(mt, match->scrutinee);
            if (match->first_arm) prepass_forward_declare(mt, (AstNode*)match->first_arm);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* arm = (AstMatchArm*)node;
            if (arm->body) prepass_forward_declare(mt, arm->body);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopNode* loop = (AstLoopNode*)node;
            if (loop->as) prepass_forward_declare(mt, loop->as);
            break;
        }
        case AST_NODE_DECOMPOSE: {
            AstDecomposeNode* dec = (AstDecomposeNode*)node;
            if (dec->as) prepass_forward_declare(mt, dec->as);
            break;
        }
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR: {
            AstRaiseNode* raise = (AstRaiseNode*)node;
            if (raise->value) prepass_forward_declare(mt, raise->value);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            if (ret->value) prepass_forward_declare(mt, ret->value);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->cond) prepass_forward_declare(mt, wh->cond);
            if (wh->body) prepass_forward_declare(mt, wh->body);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            if (assign->value) prepass_forward_declare(mt, assign->value);
            break;
        }
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            if (ca->object) prepass_forward_declare(mt, ca->object);
            if (ca->key) prepass_forward_declare(mt, ca->key);
            if (ca->value) prepass_forward_declare(mt, ca->value);
            break;
        }
        case AST_NODE_INDEX_EXPR:
        case AST_NODE_MEMBER_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            if (field->object) prepass_forward_declare(mt, field->object);
            if (field->field) prepass_forward_declare(mt, field->field);
            break;
        }
        case AST_NODE_ARRAY: {
            AstArrayNode* arr = (AstArrayNode*)node;
            if (arr->item) prepass_forward_declare(mt, arr->item);
            break;
        }
        case AST_NODE_MAP: {
            AstMapNode* map = (AstMapNode*)node;
            if (map->item) prepass_forward_declare(mt, map->item);
            break;
        }
        case AST_NODE_ELEMENT: {
            AstElementNode* elmt = (AstElementNode*)node;
            if (elmt->item) prepass_forward_declare(mt, elmt->item);
            if (elmt->content) prepass_forward_declare(mt, elmt->content);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// ============================================================================
// Pre-pass: recursively find and define all function definitions
// MIR requires all functions to be created at module level before we start
// generating code for main. This scanner descends into content blocks,
// let statements, and other constructs to find nested function definitions.
// ============================================================================

static void prepass_define_functions(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            // First define any nested functions inside this function's body
            // so they exist before the outer function body references them
            if (fn_node->body) prepass_define_functions(mt, fn_node->body);
            transpile_func_def(mt, fn_node);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_define_functions(mt, list->declare);
            prepass_define_functions(mt, list->item);
            break;
        }
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            // Check declare chain
            if (list->declare) prepass_define_functions(mt, list->declare);
            prepass_define_functions(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_define_functions(mt, let_node->declare);
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_PARAM:
        case AST_NODE_NAMED_ARG: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as) prepass_define_functions(mt, named->as);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            if (pri->expr) prepass_define_functions(mt, pri->expr);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (call->function) prepass_define_functions(mt, call->function);
            if (call->argument) prepass_define_functions(mt, call->argument);
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            if (bi->left) prepass_define_functions(mt, bi->left);
            if (bi->right) prepass_define_functions(mt, bi->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->operand) prepass_define_functions(mt, un->operand);
            break;
        }
        case AST_NODE_IF_EXPR:
        case AST_NODE_IF_STAM: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->cond) prepass_define_functions(mt, if_node->cond);
            if (if_node->then) prepass_define_functions(mt, if_node->then);
            if (if_node->otherwise) prepass_define_functions(mt, if_node->otherwise);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->loop) prepass_define_functions(mt, for_node->loop);
            if (for_node->let_clause) prepass_define_functions(mt, for_node->let_clause);
            if (for_node->where) prepass_define_functions(mt, for_node->where);
            if (for_node->then) prepass_define_functions(mt, for_node->then);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            if (match->scrutinee) prepass_define_functions(mt, match->scrutinee);
            if (match->first_arm) prepass_define_functions(mt, (AstNode*)match->first_arm);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* arm = (AstMatchArm*)node;
            if (arm->body) prepass_define_functions(mt, arm->body);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopNode* loop = (AstLoopNode*)node;
            if (loop->as) prepass_define_functions(mt, loop->as);
            break;
        }
        case AST_NODE_DECOMPOSE: {
            AstDecomposeNode* dec = (AstDecomposeNode*)node;
            if (dec->as) prepass_define_functions(mt, dec->as);
            break;
        }
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR: {
            AstRaiseNode* raise = (AstRaiseNode*)node;
            if (raise->value) prepass_define_functions(mt, raise->value);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            if (ret->value) prepass_define_functions(mt, ret->value);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->cond) prepass_define_functions(mt, wh->cond);
            if (wh->body) prepass_define_functions(mt, wh->body);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            if (assign->value) prepass_define_functions(mt, assign->value);
            break;
        }
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            if (ca->object) prepass_define_functions(mt, ca->object);
            if (ca->key) prepass_define_functions(mt, ca->key);
            if (ca->value) prepass_define_functions(mt, ca->value);
            break;
        }
        case AST_NODE_INDEX_EXPR:
        case AST_NODE_MEMBER_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            if (field->object) prepass_define_functions(mt, field->object);
            if (field->field) prepass_define_functions(mt, field->field);
            break;
        }
        case AST_NODE_ARRAY: {
            AstArrayNode* arr = (AstArrayNode*)node;
            if (arr->item) prepass_define_functions(mt, arr->item);
            break;
        }
        case AST_NODE_MAP: {
            AstMapNode* map = (AstMapNode*)node;
            if (map->item) prepass_define_functions(mt, map->item);
            break;
        }
        case AST_NODE_ELEMENT: {
            AstElementNode* elmt = (AstElementNode*)node;
            if (elmt->item) prepass_define_functions(mt, elmt->item);
            if (elmt->content) prepass_define_functions(mt, elmt->content);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
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

    // Forward-declare ALL functions first (handles forward references between functions)
    prepass_forward_declare(&mt, script->child);

    // Define ALL functions at module level (transpiles bodies)
    prepass_define_functions(&mt, script->child);

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

    AstNode* child = script->child;
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
        case AST_NODE_VAR_STAM:
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

    // Dump MIR text for debugging
    FILE* mir_dump = fopen("temp/mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }

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
