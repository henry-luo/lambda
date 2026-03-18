#include "transpiler.hpp"
#include "re2_wrapper.hpp"
#include "mark_builder.hpp"
#include "safety_analyzer.hpp"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/hashmap.h"
#include "../lib/gc_heap.h"
#include "validator/validator.hpp"
#include "lambda-stack.h"
#include <mir.h>
#include <mir-gen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <malloc.h>  // alloca on Windows
#else
#include <alloca.h>
#endif
#include <time.h>

extern Type TYPE_ANY, TYPE_INT;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern void heap_destroy();

extern "C" {
    int gc_object_zone_class_index(size_t size);
}

// Profiling infrastructure (defined in runner.cpp)
#define PROFILE_MAX_SCRIPTS 64
typedef struct PhaseProfile {
    const char* script_path;
    double parse_ms;
    double ast_ms;
    double transpile_ms;
    double jit_init_ms;
    double file_write_ms;
    double c2mir_ms;
    double mir_gen_ms;
    int code_len;
} PhaseProfile;
extern bool is_profile_enabled();
extern PhaseProfile profile_data[];
extern int profile_count;
#ifdef _WIN32
#include <windows.h>
typedef LARGE_INTEGER profile_time_t;
#else
typedef struct timespec profile_time_t;
#endif
extern void profile_get_time(profile_time_t* t);
extern double elapsed_ms_val(profile_time_t t0, profile_time_t t1);
extern void profile_dump_to_file();
extern Url* get_current_dir();

// Forward declare Runner helper functions from runner.cpp
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);

// Forward declare has_current_item_ref from build_ast.cpp
bool has_current_item_ref(AstNode* node);

// Forward declare resolve_sys_paths_recursive from runner.cpp
void resolve_sys_paths_recursive(Item item);

// Forward declare import resolver from mir.c
extern "C" {
    void *import_resolver(const char *name);
    void register_bss_gc_roots(void* mir_ctx);
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
    TypeId elem_type;  // P4-3.1: element type for container vars (from fill() or annotation)
    int env_offset;  // >= 0 if captured variable (byte offset in env struct), -1 otherwise
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

    // Pattern type list (shared with Script's type_list for const_pattern access)
    ArrayList* type_list;
    Pool* script_pool;  // pool for pattern compilation

    // MIR context
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache: name -> MirImportEntry
    struct hashmap* import_cache;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Global variables: name -> GlobalVarEntry (BSS-backed module-level let bindings)
    struct hashmap* global_vars;

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

    // GC heap pointer register (loaded at function entry for inline alloc)
    MIR_reg_t gc_reg;

    // Consts pointer register
    MIR_reg_t consts_reg;

    // Current pipe context
    MIR_reg_t pipe_item_reg;
    MIR_reg_t pipe_index_reg;
    bool in_pipe;

    // TCO
    AstFuncNode* tco_func;
    MIR_label_t tco_label;
    MIR_reg_t tco_count_reg;   // iteration counter for TCO guard
    bool in_tail_position;     // current expression is in tail position

    // Closure
    AstFuncNode* current_closure;
    MIR_reg_t env_reg;

    // Object method context: set when transpiling a method body inside an object type
    TypeObject* method_owner;
    MIR_reg_t self_reg;  // register holding the boxed self Item (_self parameter)

    // Whether we're inside a user-defined function (not the main function)
    bool in_user_func;

    // Whether we're inside a proc function body (pn)
    bool in_proc;

    // P4-3.2: Current function/script body for mutation analysis
    // Set to script->child at module level, fn_node->body inside functions
    AstNode* func_body;

    // Set when a return/break/continue emits a terminal instruction.
    // Used to skip dead-code boxing that causes MIR type mismatches.
    bool block_returned;

    // Phase 4: Native function info map — tracks which functions have native
    // (unboxed) versions and their parameter/return types.
    // name -> NativeFuncInfo
    struct hashmap* native_func_info;

    // P4-3.4: Native return type for the current function being compiled.
    // Set when a function (fn or pn) has a native return type (INT, FLOAT, BOOL).
    // Used by transpile_return() to emit unboxed return values in procs.
    TypeId native_return_tid;

    // Variadic function body context: when true, return/raise must emit restore_vargs
    bool in_variadic_body;
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

// Native function info: tracks parameter types and return type for functions
// that have a dual native+boxed version (Phase 4 optimization).
struct NativeFuncInfo {
    char name[128];           // mangled function name (native version)
    TypeId param_types[16];   // resolved (declared or inferred) TypeId per param
    MIR_type_t param_mir[16]; // MIR type per param
    int param_count;          // number of user params (excluding _env_ptr, _self, _vargs)
    TypeId return_type;       // resolved return TypeId (LMD_TYPE_ANY if unknown)
    MIR_type_t return_mir;    // MIR return type
    bool has_native;          // true if a native version was generated
};

static int native_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((NativeFuncInfo*)a)->name, ((NativeFuncInfo*)b)->name);
}
static uint64_t native_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const NativeFuncInfo* e = (const NativeFuncInfo*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

// Global variable entry (module-level let bindings stored in BSS)
struct GlobalVarEntry {
    char name[128];
    MIR_item_t bss_item;
    TypeId type_id;
    MIR_type_t mir_type;
};

static int global_var_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((GlobalVarEntry*)a)->name, ((GlobalVarEntry*)b)->name);
}
static uint64_t global_var_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const GlobalVarEntry* e = (const GlobalVarEntry*)item;
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
    entry.var.elem_type = LMD_TYPE_ANY;
    entry.var.env_offset = -1;  // not a captured variable by default
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static MirVarEntry* find_var(MirTranspiler* mt, const char* name) {
    VarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        VarScopeEntry* found = (VarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) {
            return &found->var;
        }
    }
    return NULL;
}

// Look up a global (module-level) variable
static GlobalVarEntry* find_global_var(MirTranspiler* mt, const char* name) {
    if (!mt->global_vars) return NULL;
    GlobalVarEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return (GlobalVarEntry*)hashmap_get(mt->global_vars, &key);
}

// Forward declarations (defined after emit_box/emit_unbox)
static MIR_reg_t load_global_var(MirTranspiler* mt, GlobalVarEntry* gvar);
static void store_global_var(MirTranspiler* mt, GlobalVarEntry* gvar, MIR_reg_t val, TypeId val_tid);

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

// Look up native function info for dual-version functions
static NativeFuncInfo* find_native_func_info(MirTranspiler* mt, const char* name) {
    if (!mt->native_func_info) return NULL;
    NativeFuncInfo key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return (NativeFuncInfo*)hashmap_get(mt->native_func_info, &key);
}

static void register_native_func_info(MirTranspiler* mt, const char* name, NativeFuncInfo* info) {
    if (!mt->native_func_info) {
        mt->native_func_info = hashmap_new(sizeof(NativeFuncInfo), 16, 0, 0,
            native_func_hash, native_func_cmp, NULL, NULL);
    }
    snprintf(info->name, sizeof(info->name), "%s", name);
    hashmap_set(mt->native_func_info, info);
}

// Check if a parameter type qualifies for native (unboxed) representation.
// Only types that map to a different native type than boxed Item are worth unboxing.
static bool mir_is_native_param_type(TypeId tid) {
    switch (tid) {
    case LMD_TYPE_INT:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_BOOL:
    case LMD_TYPE_STRING:
    case LMD_TYPE_INT64:
        return true;
    default:
        return false;
    }
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

static MIR_reg_t emit_call_4(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = new_reg(mt, fn_name, ret_type);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res),
        a1, a2, a3, a4));
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

static void emit_call_void_3(MirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    MirImportEntry* ie = ensure_import(mt, fn_name, MIR_T_I64, 3, args, 0);
    emit_insn(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        a1, a2, a3));
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

// Zero-extend uint8_t Bool return from a runtime function call.
// MIR reads full i64 from the return register but C functions returning uint8_t
// may leave garbage in the upper 56 bits. This masks to the low 8 bits.
static MIR_reg_t emit_uext8(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t ext = new_reg(mt, "bext", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UEXT8, MIR_new_reg_op(mt->ctx, ext),
        MIR_new_reg_op(mt->ctx, val_reg)));
    return ext;
}

// Box bool -> Item (inline b2it)
static MIR_reg_t emit_box_bool(MirTranspiler* mt, MIR_reg_t val_reg) {
    // Bool can be BOOL_FALSE(0), BOOL_TRUE(1), or BOOL_ERROR(2)
    // Zero-extend from 8 bits: runtime functions return uint8_t Bool, but MIR
    // reads full i64 from the return register — upper bits may contain garbage.
    MIR_reg_t masked = new_reg(mt, "bmask", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UEXT8, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val_reg)));
    // If val >= BOOL_ERROR (2), return ITEM_ERROR instead of boxing as bool
    MIR_reg_t is_error = new_reg(mt, "berr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, is_error),
        MIR_new_reg_op(mt->ctx, masked), MIR_new_int_op(mt->ctx, 2)));
    MIR_label_t l_error = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    MIR_reg_t result = new_reg(mt, "boxb", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_error),
        MIR_new_reg_op(mt->ctx, is_error)));
    // Normal bool: box as (BOOL_TAG | val)
    uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, masked)));
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
// Note: push_l takes int64_t by value, which maps correctly to MIR_T_I64
static MIR_reg_t emit_box_int64(MirTranspiler* mt, MIR_reg_t val_reg) {
    // Use push_l_safe to handle both raw int64 and already-boxed INT64 Items.
    // This prevents double-boxing when the value comes from a runtime function
    // return (boxed Item) vs a native computation (raw int64).
    return emit_call_1(mt, "push_l_safe", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box DateTime* pointer -> Item (inline k2it)
// val_reg is a POINTER to DateTime (on num_stack or consts), not the DateTime value itself.
// Use inline OR tagging: result = DTIME_TAG | ptr
static MIR_reg_t emit_box_dtime(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxk", MIR_T_I64);
    uint64_t DTIME_TAG = (uint64_t)LMD_TYPE_DTIME << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)DTIME_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box raw DateTime VALUE -> Item via push_k
// val_reg is the raw DateTime uint64_t value (NOT a pointer).
// push_k allocates on num_stack and returns a properly tagged Item.
static MIR_reg_t emit_box_dtime_value(MirTranspiler* mt, MIR_reg_t val_reg) {
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

// Box Decimal* pointer -> Item (inline dc2it)
// Cannot use push_dc because it takes Decimal struct by value.
static MIR_reg_t emit_box_decimal(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxdc", MIR_T_I64);
    uint64_t DEC_TAG = (uint64_t)LMD_TYPE_DECIMAL << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)DEC_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box Binary* pointer -> Item (inline)
static MIR_reg_t emit_box_binary(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxx", MIR_T_I64);
    uint64_t BIN_TAG = (uint64_t)LMD_TYPE_BINARY << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BIN_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_label(mt, l_end);
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
    case LMD_TYPE_DTIME:
        return emit_box_dtime(mt, val_reg);
    case LMD_TYPE_STRING:
        return emit_box_string(mt, val_reg);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, val_reg);
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: case LMD_TYPE_RANGE: case LMD_TYPE_FUNC:
    case LMD_TYPE_TYPE: case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        return emit_box_container(mt, val_reg);
    case LMD_TYPE_DECIMAL:
        return emit_box_decimal(mt, val_reg);
    case LMD_TYPE_BINARY:
        return emit_box_binary(mt, val_reg);
    case LMD_TYPE_NULL:
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
    default:
        // Already boxed Item or NULL
        return val_reg;
    }
}

// Unbox boxed Item -> container pointer by stripping the type tag (upper 8 bits)
// Container types embed their TypeId in the struct's first field, so the tag bits
// from boxing must be cleared to recover the raw pointer.
static MIR_reg_t emit_unbox_container(MirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t ptr = new_reg(mt, "unboxc", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_reg_op(mt->ctx, item_reg),
        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
    return ptr;
}

// Unbox boxed Item -> raw int64_t by stripping the type tag (upper 8 bits)
static MIR_reg_t emit_unbox_int_mask(MirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t val = new_reg(mt, "unboxi", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, val),
        MIR_new_reg_op(mt->ctx, item_reg),
        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
    return val;
}

// Unbox Item -> native type
static MIR_reg_t emit_unbox(MirTranspiler* mt, MIR_reg_t item_reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:
        return emit_call_1(mt, "it2i", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_FLOAT:
        return emit_call_1(mt, "it2d", MIR_T_D, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_BOOL:
        return emit_uext8(mt, emit_call_1(mt, "it2b", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg)));
    case LMD_TYPE_STRING:
        return emit_call_1(mt, "it2s", MIR_T_P, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_INT64:
        return emit_call_1(mt, "it2l", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    // Container types: strip upper 8 tag bits to recover raw pointer
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: case LMD_TYPE_RANGE:
    case LMD_TYPE_FUNC: case LMD_TYPE_TYPE: case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        return emit_unbox_container(mt, item_reg);
    default:
        return item_reg;
    }
}

// ============================================================================
// Phase 3: Direct Map/Struct Field Access helpers
// ============================================================================

// Check if a map type has a fixed shape suitable for direct field access:
// - must be a named type (from `type Name = { ... }` declaration)
// - all fields are named (no spread entries)
// - all byte offsets are 8-byte aligned
static bool mir_has_fixed_shape(TypeMap* map_type) {
    if (!map_type->struct_name) return false;
    if (!map_type->shape || map_type->length == 0) return false;
    ShapeEntry* field = map_type->shape;
    while (field) {
        if (!field->name) return false;
        if (field->byte_offset % sizeof(void*) != 0) return false;
        field = field->next;
    }
    return true;
}

// Find a named field in a map shape at compile time
static ShapeEntry* mir_find_shape_field(TypeMap* map_type, const char* name, int name_len) {
    ShapeEntry* field = map_type->shape;
    while (field) {
        if (field->name && (int)field->name->length == name_len &&
            strncmp(field->name->str, name, name_len) == 0) {
            return field;
        }
        field = field->next;
    }
    return NULL;
}

// Resolve the stored-data type for a shape field.
// Type-defined maps have LMD_TYPE_TYPE wrapper on shape entries; unwrap to
// get the actual data type (e.g., LMD_TYPE_INT).
static TypeId mir_resolve_field_type(ShapeEntry* field) {
    Type* t = field->type;
    if (t && t->type_id == LMD_TYPE_TYPE) {
        Type* inner = ((TypeType*)t)->type;
        if (inner) return inner->type_id;
    }
    return t ? t->type_id : LMD_TYPE_ANY;
}

// Check if a field type is eligible for direct access optimization
static bool mir_is_direct_access_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_BOOL: case LMD_TYPE_INT: case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT: case LMD_TYPE_DTIME: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY:
    case LMD_TYPE_RANGE: case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64: case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT: case LMD_TYPE_TYPE: case LMD_TYPE_FUNC:
    case LMD_TYPE_PATH:
        return true;
    default:
        return false;
    }
}

// Check if a type is a container type whose runtime storage format is a raw pointer
// (not a tagged Item). The runtime's map_field_store stores raw Container* for these
// types, so MIR direct read must re-tag and direct write must strip the tag.
static bool mir_is_container_field_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64: case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE: case LMD_TYPE_TYPE: case LMD_TYPE_FUNC:
    case LMD_TYPE_PATH:
        return true;
    default:
        return false;
    }
}

// ============================================================================
// Global variable BSS access helpers
// ============================================================================

// Load a global variable from BSS storage into a register
static MIR_reg_t load_global_var(MirTranspiler* mt, GlobalVarEntry* gvar) {
    // Get BSS address
    MIR_reg_t addr = new_reg(mt, "gv_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_ref_op(mt->ctx, gvar->bss_item)));
    // Load boxed Item from BSS
    MIR_reg_t boxed = new_reg(mt, "gv_val", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, boxed),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1)));
    // Unbox to native type if needed
    TypeId tid = gvar->type_id;
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
        tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT64) {
        return emit_unbox(mt, boxed, tid);
    }
    return boxed;
}

// Store a value to a global variable's BSS storage
static void store_global_var(MirTranspiler* mt, GlobalVarEntry* gvar, MIR_reg_t val, TypeId val_tid) {
    // Box the value first
    MIR_reg_t boxed = emit_box(mt, val, val_tid);
    // Get BSS address
    MIR_reg_t addr = new_reg(mt, "gv_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_ref_op(mt->ctx, gvar->bss_item)));
    // Store boxed Item to BSS
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
        MIR_new_reg_op(mt->ctx, boxed)));
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
static MIR_reg_t transpile_const_type(MirTranspiler* mt, int type_index);
static void transpile_let_stam(MirTranspiler* mt, AstLetNode* let_node);
static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node);
static bool has_index_mutation(const char* var_name, AstNode* node);  // P4-3.2

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
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
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

    // Check if the AST name resolution says this is a function reference FIRST.
    // This must come before variable/global lookup because a local fn declaration
    // should shadow a global variable with the same name (but fn declarations
    // don't create set_var entries — they're compiled as MIR functions).
    if (ident->entry && ident->entry->node) {
        AstNode* entry_node = ident->entry->node;
        if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR) {
            // Only use the function reference path if the function is NOT stored
            // as a captured variable (closures store captured functions as vars)
            MirVarEntry* cap_var = find_var(mt, name_buf);
            if (!cap_var) {
                goto function_reference;
            }
        }
    }

    {
    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        return var->reg;
    }
    }

    // Check global (module-level) variables stored in BSS
    {
    GlobalVarEntry* gvar = find_global_var(mt, name_buf);
    if (gvar) {
        return load_global_var(mt, gvar);
    }
    }

    // Check if this is an imported variable (pub var from another module)
    if (ident->entry && ident->entry->import) {
        AstNode* entry_node = ident->entry->node;
        // Imported pub variables
        if (entry_node && (entry_node->node_type == AST_NODE_ASSIGN ||
            entry_node->node_type == AST_NODE_PARAM)) {
            AstNamedNode* named = (AstNamedNode*)entry_node;
            StrBuf* var_name = strbuf_new_cap(64);
            write_var_name(var_name, named, NULL);

            // Determine native MIR type from the variable's declared type
            // BSS stores native values (not boxed Items): doubles for float, i64 for int, etc.
            TypeId var_tid = entry_node->type ? entry_node->type->type_id : LMD_TYPE_ANY;
            MIR_type_t load_type = type_to_mir(var_tid);
            log_debug("mir: loading imported variable '%s' as type %d (native %d)", var_name->str, var_tid, load_type);

            // Create MIR import for the BSS variable address
            MIR_item_t imp = MIR_new_import(mt->ctx, var_name->str);
            MIR_reg_t addr_reg = new_reg(mt, "impvar_addr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, addr_reg),
                MIR_new_ref_op(mt->ctx, imp)));
            // Load the native value from the BSS address with correct type
            MIR_reg_t val_reg = new_reg(mt, "impvar", load_type);
            MIR_insn_code_t load_insn = (load_type == MIR_T_D) ? MIR_DMOV : MIR_MOV;
            emit_insn(mt, MIR_new_insn(mt->ctx, load_insn,
                MIR_new_reg_op(mt->ctx, val_reg),
                MIR_new_mem_op(mt->ctx, load_type, 0, addr_reg, 0, 1)));

            strbuf_free(var_name);
            return val_reg;
        }
        // Imported pub object types — resolve via const_type(type_index)
        if (entry_node && entry_node->node_type == AST_NODE_OBJECT_TYPE) {
            AstObjectTypeNode* obj_node = (AstObjectTypeNode*)entry_node;
            if (obj_node->type && obj_node->type->type_id == LMD_TYPE_TYPE) {
                TypeObject* ot = (TypeObject*)((TypeType*)obj_node->type)->type;
                log_debug("mir: resolving imported object type '%.*s' via const_type(%d)",
                    (int)obj_node->name->len, obj_node->name->chars, ot->type_index);
                return transpile_const_type(mt, ot->type_index);
            }
        }
    }

    // Check if this is a reference to a function (for first-class function usage)
    function_reference:
    if (ident->entry && ident->entry->node) {
        AstNode* entry_node = ident->entry->node;
        if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Count arity from param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Handle imported function references
            if (ident->entry->import) {
                // Get the function name (use boxed wrapper for typed params)
                StrBuf* fn_import_name = strbuf_new_cap(64);
                bool use_wrapper = (entry_node->node_type != AST_NODE_PROC && needs_fn_call_wrapper(fn_node));
                if (use_wrapper) {
                    write_fn_name_ex(fn_import_name, fn_node, NULL, "_b");
                } else {
                    write_fn_name(fn_import_name, fn_node, NULL);
                }
                log_debug("mir: imported function reference '%s'", fn_import_name->str);

                // Get the imported function address via MIR import
                MIR_item_t imp_item = MIR_new_import(mt->ctx, fn_import_name->str);
                MIR_reg_t fn_addr = new_reg(mt, "imp_fnaddr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_new_ref_op(mt->ctx, imp_item)));

                // Create Function* via to_fn_named
                const char* fn_display_name = fn_node->name ? fn_node->name->chars : nullptr;
                MIR_reg_t fn_obj;
                if (fn_display_name) {
                    MIR_reg_t name_reg = emit_load_string_literal(mt, fn_display_name);
                    MIR_var_t cv[3] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"n",0}};
                    MirImportEntry* ie = ensure_import(mt, "to_fn_named", MIR_T_P, 3, cv, 1);
                    fn_obj = new_reg(mt, "imp_fn", MIR_T_I64);
                    emit_insn(mt, MIR_new_call_insn(mt->ctx, 6,
                        MIR_new_ref_op(mt->ctx, ie->proto),
                        MIR_new_ref_op(mt->ctx, ie->import),
                        MIR_new_reg_op(mt->ctx, fn_obj),
                        MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_new_int_op(mt->ctx, arity),
                        MIR_new_reg_op(mt->ctx, name_reg)));
                } else {
                    fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity));
                }
                strbuf_free(fn_import_name);
                return fn_obj;
            }

            // Look up the MIR function item
            StrBuf* nm_buf = strbuf_new_cap(64);
            write_fn_name(nm_buf, fn_node, ident->entry->import);
            MIR_item_t func_item = find_local_func(mt, nm_buf->str);
            if (!func_item) func_item = find_local_func(mt, name_buf);

            // Phase 4: For native functions used as first-class values, use the
            // boxed wrapper (_b) since dynamic dispatch uses the Item ABI.
            NativeFuncInfo* nfi_ref = find_native_func_info(mt, nm_buf->str);
            if (nfi_ref && nfi_ref->has_native) {
                StrBuf* wrapper_buf = strbuf_new_cap(64);
                write_fn_name_ex(wrapper_buf, fn_node, ident->entry->import, "_b");
                MIR_item_t wrapper_item = find_local_func(mt, wrapper_buf->str);
                if (wrapper_item) {
                    log_debug("mir: fn ref '%s' → using boxed wrapper '%s'",
                        nm_buf->str, wrapper_buf->str);
                    func_item = wrapper_item;
                }
                strbuf_free(wrapper_buf);
            }

            if (func_item) {
                // Get function address via MIR ref
                MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_new_ref_op(mt->ctx, func_item)));

                if (fn_node->captures) {
                    // Closure: allocate and populate env
                    int cap_count = 0;
                    CaptureInfo* cap = fn_node->captures;
                    while (cap) { cap_count++; cap = cap->next; }

                    MIR_reg_t env_reg = emit_call_2(mt, "heap_calloc", MIR_T_P,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(cap_count * 8)),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

                    int cap_idx = 0;
                    cap = fn_node->captures;
                    while (cap) {
                        char cap_name[128];
                        snprintf(cap_name, sizeof(cap_name), "%.*s", (int)cap->name->len, cap->name->chars);

                        MIR_reg_t cap_val = 0;
                        MirVarEntry* cvar = find_var(mt, cap_name);
                        if (cvar) {
                            cap_val = emit_box(mt, cvar->reg, cvar->type_id);
                        } else {
                            GlobalVarEntry* gvar = find_global_var(mt, cap_name);
                            if (gvar) {
                                cap_val = load_global_var(mt, gvar);
                            } else {
                                log_error("mir: closure capture '%s' not found in scope", cap_name);
                                cap_val = new_reg(mt, "capnull", MIR_T_I64);
                                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, cap_val),
                                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                            }
                        }

                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, cap_idx * 8, env_reg, 0, 1),
                            MIR_new_reg_op(mt->ctx, cap_val)));

                        cap_idx++;
                        cap = cap->next;
                    }

                    // Create closure: to_closure_named(fn_ptr, arity, env, name)
                    const char* closure_name = fn_node->name ? fn_node->name->chars : nullptr;
                    MIR_reg_t fn_obj;
                    if (closure_name) {
                        MIR_reg_t cn_reg = emit_load_string_literal(mt, closure_name);
                        MIR_var_t cv[4] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"e",0},{MIR_T_P,"n",0}};
                        MirImportEntry* ie = ensure_import(mt, "to_closure_named", MIR_T_P, 4, cv, 1);
                        fn_obj = new_reg(mt, "closure", MIR_T_I64);
                        emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                            MIR_new_ref_op(mt->ctx, ie->proto),
                            MIR_new_ref_op(mt->ctx, ie->import),
                            MIR_new_reg_op(mt->ctx, fn_obj),
                            MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_new_int_op(mt->ctx, arity),
                            MIR_new_reg_op(mt->ctx, env_reg),
                            MIR_new_reg_op(mt->ctx, cn_reg)));
                    } else {
                        fn_obj = emit_call_3(mt, "to_closure", MIR_T_P,
                            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                            MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg));
                    }

                    strbuf_free(nm_buf);

                    // set closure_field_count (offset 2 in Function struct)
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, fn_obj, 0, 1),
                        MIR_new_int_op(mt->ctx, cap_count)));

                    return fn_obj;
                } else {
                    // Plain function: Create Function* via to_fn_n(fn_ptr, arity)
                    MIR_reg_t fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity));

                    strbuf_free(nm_buf);
                    return fn_obj;  // Function* is a container
                }
            }
            strbuf_free(nm_buf);
        }
        // Check if this is a reference to a compiled pattern
        else if (entry_node->node_type == AST_NODE_STRING_PATTERN ||
                 entry_node->node_type == AST_NODE_SYMBOL_PATTERN) {
            AstPatternDefNode* pattern_def = (AstPatternDefNode*)entry_node;
            TypePattern* pattern_type = (TypePattern*)pattern_def->type;
            log_debug("mir: pattern reference '%s', index=%d", name_buf, pattern_type->pattern_index);

            // emit const_pattern(pattern_index) call
            MIR_reg_t pat_reg = emit_call_1(mt, "const_pattern", MIR_T_P,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)pattern_type->pattern_index));
            return pat_reg;  // TypePattern* used as Item by fn_is etc.
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

// Get effective runtime type for a node. If the node is an identifier whose
// variable is stored as boxed (ANY) — e.g. optional params — return ANY instead
// of the AST-declared type. This prevents native op dispatch on boxed values.
static TypeId get_effective_type(MirTranspiler* mt, AstNode* node) {
    if (!node) return LMD_TYPE_ANY;
    // Unwrap PRIMARY nodes (parenthesized expressions)
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return get_effective_type(mt, pri->expr);
    }
    TypeId tid = node->type ? node->type->type_id : LMD_TYPE_ANY;
    // P4-3.1: For index expressions (subscripts), check if the object variable
    // has a known element type from fill() narrowing. This enables native bool
    // paths in AND/OR/NOT for bool array elements.
    // P4-3.2: Check MirVarEntry elem_type and ARRAY_INT var type for INDEX_EXPR.
    // elem_type is set by fill() narrowing (P4-3.1) or pre-pass mutation analysis (P4-3.2).
    // Only returns native types when provably safe (explicitly set elem_type).
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* fn = (AstFieldNode*)node;
        // Guard: if the index is a TYPE expression (child query like arr[int]),
        // the result is a spreadable array, NOT a single element. Skip elem_type opt.
        AstNode* idx_unwrapped = fn->field;
        while (idx_unwrapped && idx_unwrapped->node_type == AST_NODE_PRIMARY)
            idx_unwrapped = ((AstPrimaryNode*)idx_unwrapped)->expr;
        bool idx_is_type = false;
        if (idx_unwrapped) {
            if (idx_unwrapped->node_type == AST_NODE_TYPE ||
                idx_unwrapped->node_type == AST_NODE_ARRAY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_LIST_TYPE ||
                idx_unwrapped->node_type == AST_NODE_MAP_TYPE ||
                idx_unwrapped->node_type == AST_NODE_ELMT_TYPE ||
                idx_unwrapped->node_type == AST_NODE_FUNC_TYPE ||
                idx_unwrapped->node_type == AST_NODE_BINARY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_UNARY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_CONTENT_TYPE)
                idx_is_type = true;
            if (!idx_is_type && idx_unwrapped->type && idx_unwrapped->type->type_id == LMD_TYPE_TYPE)
                idx_is_type = true;
        }
        if (!idx_is_type) {
            // Unwrap PRIMARY around the object to find the IDENT
            AstNode* obj_unwrapped = fn->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type != LMD_TYPE_ANY) return ov->elem_type;
                // P4-3.2: ARRAY_INT variable → element is INT
                if (ov && ov->type_id == LMD_TYPE_ARRAY_INT) return LMD_TYPE_INT;
            }
        }
    }
    // For identifiers: reconcile AST type with actual variable storage.
    // Two cases: (1) AST says typed but var stored as ANY → downgrade to ANY
    //            (2) AST says ANY but var narrowed by inference → upgrade to narrowed type
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node;
        char name[128];
        snprintf(name, sizeof(name), "%.*s", (int)ident->name->len, ident->name->chars);
        MirVarEntry* v = find_var(mt, name);
        if (v) {
            if (v->type_id == LMD_TYPE_ANY) return LMD_TYPE_ANY;
            // Narrowed: var has concrete type (from param inference or typed init)
            if (tid == LMD_TYPE_ANY && v->type_id != LMD_TYPE_ANY) return v->type_id;
            // Type annotation meta-type (e.g. `arr: int[]` produces LMD_TYPE_TYPE
            // on the AST node).  The variable's tracked type_id is the actual
            // runtime representation type, so prefer it.
            if (tid == LMD_TYPE_TYPE && v->type_id != LMD_TYPE_ANY) return v->type_id;
        }
    }
    // For binary nodes: if either operand has effective type ANY, the runtime
    // will use the boxed fallback path → result is a boxed Item (ANY)
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)node;

        // IDIV/MOD ALWAYS return boxed Items from runtime (fn_idiv/fn_mod)
        // UNLESS both operands are INT — then we use native fn_idiv_i/fn_mod_i
        // which return int64_t (with INT64_ERROR for div-by-zero).
        // Note: INT64 excluded — transpile_expr returns inconsistent values for INT64
        // (raw int64 from literals but boxed Items from generic binary fallback).
        if (bi->op == OPERATOR_IDIV || bi->op == OPERATOR_MOD) {
            TypeId idiv_lt = get_effective_type(mt, bi->left);
            TypeId idiv_rt = get_effective_type(mt, bi->right);
            if (idiv_lt == LMD_TYPE_ANY || idiv_rt == LMD_TYPE_ANY)
                return LMD_TYPE_ANY;
            bool both_int_idiv = (idiv_lt == LMD_TYPE_INT) && (idiv_rt == LMD_TYPE_INT);
            if (both_int_idiv)
                return LMD_TYPE_INT;
            // Float MOD uses fmod() which returns double
            if (bi->op == OPERATOR_MOD) {
                bool has_float = (idiv_lt == LMD_TYPE_FLOAT || idiv_rt == LMD_TYPE_FLOAT);
                bool other_numeric = (idiv_lt == LMD_TYPE_INT || idiv_lt == LMD_TYPE_FLOAT) &&
                                     (idiv_rt == LMD_TYPE_INT || idiv_rt == LMD_TYPE_FLOAT);
                if (has_float && other_numeric)
                    return LMD_TYPE_FLOAT;
            }
            return LMD_TYPE_ANY;
        }

        TypeId lt = get_effective_type(mt, bi->left);
        TypeId rt = get_effective_type(mt, bi->right);

        // Comparisons ALWAYS return bool regardless of operand types.
        // Must check BEFORE the ANY short-circuit below, because comparison
        // operators (fn_eq/fn_gt/etc.) return Bool (uint8_t 0/1), not Item.
        // If we returned ANY, the raw 0/1 would be stored as a boxed Item
        // and later dereferenced as a pointer → SIGSEGV.
        {
            int op_ck = bi->op;
            if (op_ck >= OPERATOR_EQ && op_ck <= OPERATOR_GE) return LMD_TYPE_BOOL;
            if (op_ck == OPERATOR_IS || op_ck == OPERATOR_IS_NAN || op_ck == OPERATOR_IN)
                return LMD_TYPE_BOOL;
        }

        if (lt == LMD_TYPE_ANY || rt == LMD_TYPE_ANY) return LMD_TYPE_ANY;

        // INT64 arithmetic (with INT or INT64) goes through boxed fn_add/fn_sub/fn_mul
        // runtime path (transpile_binary skips native arithmetic for INT64 due to
        // inconsistent representation — raw int64 from some paths, tagged Items from
        // others). The boxed runtime returns tagged Items, so effective type is ANY.
        // Without this, the AST type (often INT) would be returned, causing
        // transpile_assign_stam to treat the tagged Item as a raw int → infinite loops.
        {
            bool has_int64 = (lt == LMD_TYPE_INT64 || rt == LMD_TYPE_INT64);
            bool other_int = (lt == LMD_TYPE_INT || lt == LMD_TYPE_INT64) &&
                             (rt == LMD_TYPE_INT || rt == LMD_TYPE_INT64);
            if (has_int64 && other_int) {
                int op64 = bi->op;
                if (op64 == OPERATOR_ADD || op64 == OPERATOR_SUB || op64 == OPERATOR_MUL ||
                    op64 == OPERATOR_DIV || op64 == OPERATOR_POW)
                    return LMD_TYPE_ANY;
            }
        }

        // Infer result type from operand types when AST type is not set.
        // This ensures transpile_assign_stam correctly identifies native values
        // returned by transpile_binary (e.g. unboxed INT from fn_mod with int ops).
        if (tid == LMD_TYPE_ANY || tid == LMD_TYPE_NULL) {
            int op = bi->op;
            bool both_int = (lt == LMD_TYPE_INT) && (rt == LMD_TYPE_INT);
            bool both_float = (lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_FLOAT);
            bool int_float = (lt == LMD_TYPE_INT && rt == LMD_TYPE_FLOAT) ||
                             (lt == LMD_TYPE_FLOAT && rt == LMD_TYPE_INT);
            bool both_bool = (lt == LMD_TYPE_BOOL) && (rt == LMD_TYPE_BOOL);

            if (both_int) {
                if (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL)
                    return LMD_TYPE_INT;
                if (op == OPERATOR_DIV)
                    return LMD_TYPE_FLOAT;
                // IDIV/MOD with both_int handled above via native fn_idiv_i/fn_mod_i
            }
            if (both_float || int_float) {
                if (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
                    op == OPERATOR_DIV)
                    return LMD_TYPE_FLOAT;
            }
            // Comparisons always return bool
            if (op >= OPERATOR_EQ && op <= OPERATOR_GE)
                return LMD_TYPE_BOOL;
            if (op == OPERATOR_IS || op == OPERATOR_IS_NAN || op == OPERATOR_IN)
                return LMD_TYPE_BOOL;
            // AND/OR with both_bool return bool
            if ((op == OPERATOR_AND || op == OPERATOR_OR) && both_bool)
                return LMD_TYPE_BOOL;
        }
    }
    // For ASSIGN_STAM: transpile_assign_stam returns the RHS value, so
    // effective type is the RHS type. This ensures callers (e.g. transpile_if)
    // know the actual MIR register type when an assignment appears as a
    // branch result (e.g. if (cond) { x = x / 2 } → RHS is FLOAT).
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        if (assign->value) return get_effective_type(mt, assign->value);
    }
    // For INDEX nodes: if object is known ArrayInt and index is INT, element type is INT
    //                   if object is known ArrayFloat and index is INT, element type is FLOAT
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* fn = (AstFieldNode*)node;
        TypeId obj_eff = get_effective_type(mt, fn->object);
        TypeId idx_eff = get_effective_type(mt, fn->field);
        if (obj_eff == LMD_TYPE_ARRAY_INT &&
            (idx_eff == LMD_TYPE_INT || idx_eff == LMD_TYPE_INT64)) {
            return LMD_TYPE_INT;
        }
        if (obj_eff == LMD_TYPE_ARRAY_FLOAT &&
            (idx_eff == LMD_TYPE_INT || idx_eff == LMD_TYPE_INT64)) {
            return LMD_TYPE_FLOAT;
        }
        // ARRAY with nested=INT + INT index: return type is ANY (not INT)
        // because the array may have been converted from ArrayInt to generic
        // Array by fn_array_set (e.g., arr[1] = 3.14 on an int array).
        // The transpile_index runtime check handles this correctly.
    }
    // For if nodes: use effective type of branches
    if (node->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* if_node = (AstIfNode*)node;
        TypeId then_eff = if_node->then ? get_effective_type(mt, if_node->then) : LMD_TYPE_ANY;
        TypeId else_eff = if_node->otherwise ? get_effective_type(mt, if_node->otherwise) : LMD_TYPE_ANY;
        if (then_eff == LMD_TYPE_ANY || else_eff == LMD_TYPE_ANY) return LMD_TYPE_ANY;
    }
    // For UNARY nodes: infer result type from operand's effective type.
    // transpile_unary returns native values for NEG(INT) → INT, NEG(FLOAT) → FLOAT,
    // NOT(BOOL) → BOOL, POS(numeric) → same type. Without this, Phase 4 native
    // functions whose parameter types are inferred (not in AST) would have wrong
    // effective types for unary expressions, causing type mismatches in assignments.
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* un = (AstUnaryNode*)node;
        TypeId operand_eff = get_effective_type(mt, un->operand);
        if (un->op == OPERATOR_NEG) {
            if (operand_eff == LMD_TYPE_INT) return LMD_TYPE_INT;
            if (operand_eff == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
        } else if (un->op == OPERATOR_NOT) {
            return LMD_TYPE_BOOL;
        } else if (un->op == OPERATOR_POS) {
            if (operand_eff == LMD_TYPE_INT || operand_eff == LMD_TYPE_INT64 ||
                operand_eff == LMD_TYPE_FLOAT)
                return operand_eff;
        } else if (un->op == OPERATOR_IS_ERROR) {
            return LMD_TYPE_BOOL;
        }
    }
    return tid;
}

static MIR_reg_t transpile_binary(MirTranspiler* mt, AstBinaryNode* bi) {
    // TCO: binary expression operands are NEVER in tail position.
    // e.g., `1 + f(n-1)` — the call is NOT tail because addition follows.
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    TypeId left_tid = get_effective_type(mt, bi->left);
    TypeId right_tid = get_effective_type(mt, bi->right);
    TypeId result_tid = ((AstNode*)bi)->type ? ((AstNode*)bi)->type->type_id : LMD_TYPE_ANY;

    // IDIV and MOD: native fast paths when operand types are known
    // Note: INT64 excluded from native path — transpile_expr returns inconsistent
    // values for INT64 (raw vs boxed). All INT64 ops use generic boxed fallback.
    if (bi->op == OPERATOR_IDIV || bi->op == OPERATOR_MOD) {
        bool both_int = (left_tid == LMD_TYPE_INT) && (right_tid == LMD_TYPE_INT);
        if (both_int) {
            // Native integer path: fn_idiv_i / fn_mod_i (returns int64_t, INT64_ERROR on div-by-zero)
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            const char* fn_name = (bi->op == OPERATOR_IDIV) ? "fn_idiv_i" : "fn_mod_i";
            return emit_call_2(mt, fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
        }
        // Float MOD: use fmod(double, double) -> double
        // Requires at least one FLOAT operand and the other being INT or FLOAT
        if (bi->op == OPERATOR_MOD) {
            bool has_float = (left_tid == LMD_TYPE_FLOAT || right_tid == LMD_TYPE_FLOAT);
            bool both_numeric = has_float &&
                                (left_tid == LMD_TYPE_INT || left_tid == LMD_TYPE_FLOAT) &&
                                (right_tid == LMD_TYPE_INT || right_tid == LMD_TYPE_FLOAT);
            if (both_numeric) {
                MIR_reg_t left = transpile_expr(mt, bi->left);
                MIR_reg_t right = transpile_expr(mt, bi->right);
                // Convert to double if needed
                MIR_reg_t dl = left, dr = right;
                if (left_tid == LMD_TYPE_INT) {
                    dl = new_reg(mt, "i2d_ml", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dl),
                        MIR_new_reg_op(mt->ctx, left)));
                }
                if (right_tid == LMD_TYPE_INT) {
                    dr = new_reg(mt, "i2d_mr", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dr),
                        MIR_new_reg_op(mt->ctx, right)));
                }
                return emit_call_2(mt, "fmod", MIR_T_D,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, dl),
                    MIR_T_D, MIR_new_reg_op(mt->ctx, dr));
            }
        }
        // Fallback: boxed runtime for unknown/mixed types
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        const char* fn_name = (bi->op == OPERATOR_IDIV) ? "fn_idiv" : "fn_mod";
        MIR_reg_t result = emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
        return result;
    }

    // Type-dispatch: if both sides are native types, use native MIR ops
    bool left_int = (left_tid == LMD_TYPE_INT);
    bool right_int = (right_tid == LMD_TYPE_INT);
    bool left_float = (left_tid == LMD_TYPE_FLOAT);
    bool right_float = (right_tid == LMD_TYPE_FLOAT);
    bool left_int64 = (left_tid == LMD_TYPE_INT64);
    bool right_int64 = (right_tid == LMD_TYPE_INT64);
    bool both_int = left_int && right_int;
    bool both_float = left_float && right_float;
    bool int_float = (left_int && right_float) || (left_float && right_int);
    // INT64 + INT or INT64 + INT64: both stored as native int64 in MIR registers.
    // Safe for COMPARISONS only (EQ/NE/LT/LE/GT/GE) — no risk of overflow or
    // representation mismatch. Arithmetic still goes through boxed path since
    // transpile_expr may return inconsistent values for INT64.
    bool int_or_int64 = (left_int || left_int64) && (right_int || right_int64);

    // Note: INT64 arithmetic is NOT handled natively because transpile_expr
    // returns inconsistent values for INT64 (raw int64 from literals/fn_int64,
    // but boxed Items from generic binary fallback). All INT64 ops go through
    // the generic boxed path, and emit_box_int64 uses push_l_safe to handle
    // both raw and already-boxed values safely.

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

    // Native INT64 vs INT comparisons: both are int64 in MIR registers.
    // Only for comparison operators (safe, no overflow risk).
    // NOTE: Arithmetic (ADD/SUB/MUL) intentionally NOT handled natively for INT64
    // because transpile_expr returns inconsistent values for INT64 (raw int64 from
    // some paths, tagged Items from others). All INT64 arithmetic goes through the
    // boxed runtime path. The assignment unbox path (INT64 in transpile_assign_stam)
    // ensures the boxed result is correctly converted back to raw int64.
    if (int_or_int64 && !both_int) {
        int op = bi->op;
        if (op >= OPERATOR_EQ && op <= OPERATOR_GE) {
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            MIR_insn_code_t mir_op;
            const char* name;
            switch (op) {
            case OPERATOR_EQ: mir_op = MIR_EQ; name = "eq"; break;
            case OPERATOR_NE: mir_op = MIR_NE; name = "ne"; break;
            case OPERATOR_LT: mir_op = MIR_LT; name = "lt"; break;
            case OPERATOR_LE: mir_op = MIR_LE; name = "le"; break;
            case OPERATOR_GT: mir_op = MIR_GT; name = "gt"; break;
            case OPERATOR_GE: mir_op = MIR_GE; name = "ge"; break;
            default: mir_op = MIR_EQ; name = "eq"; break;
            }
            MIR_reg_t r = new_reg(mt, name, MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, mir_op,
                MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, left),
                MIR_new_reg_op(mt->ctx, right)));
            return r;
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
        MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));
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
        MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));
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

    // IEEE NaN check: expr is nan
    if (bi->op == OPERATOR_IS_NAN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        return emit_call_1(mt, "fn_is_nan", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl));
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
            MIR_reg_t tid_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ct_value)));
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
            MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val)));
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

    // ======================================================================
    // Inline string/symbol comparison: avoid boxing + type dispatch
    // Fast path: pointer identity (single MIR_EQ, no function call)
    // Slow path: lightweight helper (no boxing, no type dispatch)
    // ======================================================================
    {
        bool both_string = (left_tid == LMD_TYPE_STRING && right_tid == LMD_TYPE_STRING);
        bool both_symbol = (left_tid == LMD_TYPE_SYMBOL && right_tid == LMD_TYPE_SYMBOL);
        if ((both_string || both_symbol) &&
            (bi->op == OPERATOR_EQ || bi->op == OPERATOR_NE)) {
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            MIR_reg_t result = new_reg(mt, "seq", MIR_T_I64);
            MIR_label_t l_end = new_label(mt);

            // Fast path: pointer identity
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, left),
                MIR_new_reg_op(mt->ctx, right)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, result)));

            // Slow path: content comparison via lightweight helper
            const char* helper = both_string ? "fn_str_eq_ptr" : "fn_sym_eq_ptr";
            MIR_reg_t cmp = emit_call_2(mt, helper, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, cmp),
                MIR_new_int_op(mt->ctx, 0xFF)));

            emit_label(mt, l_end);

            // For NE: invert the result
            if (bi->op == OPERATOR_NE) {
                MIR_reg_t inv = new_reg(mt, "sne", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                    MIR_new_reg_op(mt->ctx, inv),
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
                return inv;
            }
            return result;
        }
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

    MIR_reg_t result = emit_call_2(mt, fn_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));

    // Comparison functions (fn_eq/fn_ne/fn_lt/fn_gt/fn_le/fn_ge) return Bool
    // (uint8_t) but we declare MIR_T_I64 as return type. On some ABIs the
    // upper bytes of the return register may contain garbage. Mask to 0xFF
    // to ensure a clean 0/1 value that can be safely used as a native bool.
    if (bi->op >= OPERATOR_EQ && bi->op <= OPERATOR_GE) {
        MIR_reg_t clean = new_reg(mt, "cmask", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, clean),
            MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0xFF)));
        return clean;
    }

    // NOTE: The general fallback is only reached for non-native type combinations
    // or POW (which breaks out of the native block). ADD/SUB/MUL/DIV with native
    // types are handled above, IDIV/MOD are intercepted at the top of this function.
    // POW with int operands can return float (negative exponent), so we do NOT
    // unbox POW results — they remain boxed Items from the runtime.
    return result;
}

// ============================================================================
// Unary expressions
// ============================================================================

static MIR_reg_t transpile_unary(MirTranspiler* mt, AstUnaryNode* un) {
    TypeId operand_tid = get_effective_type(mt, un->operand);

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
    TypeId cond_tid = get_effective_type(mt, if_node->cond);

    // TCO: condition is NOT in tail position
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    MIR_reg_t cond = transpile_expr(mt, if_node->cond);

    // Restore tail position for branches
    mt->in_tail_position = saved_tail;

    // For non-bool condition, check truthiness via runtime
    // Lambda semantics: only null, error, and false are falsy.
    // Note: even when type says INT/FLOAT/STRING, runtime value could be null
    // (e.g. optional parameters), so always use is_truthy for safety.
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
    }

    // Determine result type - check if branches could produce different MIR types
    // Use effective types to account for optional params and mixed runtime paths
    TypeId if_tid = get_effective_type(mt, (AstNode*)if_node);
    TypeId then_tid = if_node->then ? get_effective_type(mt, if_node->then) : LMD_TYPE_ANY;
    TypeId else_tid = if_node->otherwise ? get_effective_type(mt, if_node->otherwise) : LMD_TYPE_ANY;
    MIR_type_t then_mir = type_to_mir(then_tid);
    MIR_type_t else_mir = type_to_mir(else_tid);

    log_debug("mir: transpile_if if_tid=%d then_tid=%d else_tid=%d then_mir=%d else_mir=%d",
             if_tid, then_tid, else_tid, then_mir, else_mir);

    // If branches have different MIR types or the node type is ANY, always box to Item (I64)
    bool need_boxing = (if_tid == LMD_TYPE_ANY || if_tid == LMD_TYPE_NUMBER ||
                        then_mir != else_mir ||
                        then_mir != type_to_mir(if_tid));

    // In proc context, if/else is a statement — the result is typically unused.
    // If boxing is needed (expensive for FLOAT→Item in tight loops), skip it
    // and just assign a dummy null. Saves push_d allocations per iteration.
    bool proc_discard = mt->in_proc && need_boxing;

    MIR_type_t result_type = need_boxing ? MIR_T_I64 : type_to_mir(if_tid);
    MIR_reg_t result = new_reg(mt, "if_res", result_type);
    MIR_label_t l_else = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Then branch
    if (if_node->then) {
        mt->in_tail_position = saved_tail;  // ensure correct before then branch
        push_scope(mt);  // isolate branch variables
        MIR_reg_t then_val = transpile_expr(mt, if_node->then);
        if (mt->block_returned) {
            // Branch contains a terminal statement (return/break/continue).
            // Code after MIR_RET is unreachable, but MIR still validates types.
            // Assign a dummy I64 value to the result register instead of boxing
            // (which could cause type mismatch: e.g. emit_box_float on an I64).
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            mt->block_returned = false;
        } else if (proc_discard) {
            // Proc context: result unused, skip expensive boxing.
            // Just assign a dummy null Item to satisfy MIR type validation.
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        } else if (need_boxing) {
            // transpile_content always returns I64 (boxed Item from list_end
            // or transpile_box_item). The AST-inferred then_tid may be stale
            // (e.g. FLOAT for a side-effect-only block). Use ANY for CONTENT
            // branches to avoid type mismatches like emit_box_float(I64_reg).
            TypeId box_tid = then_tid;
            if (if_node->then->node_type == AST_NODE_CONTENT ||
                if_node->then->node_type == AST_NODE_LIST) {
                box_tid = LMD_TYPE_ANY;
            }
            MIR_reg_t boxed = emit_box(mt, then_val, box_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        }
        pop_scope(mt);
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Else branch
    emit_label(mt, l_else);
    mt->in_tail_position = saved_tail;  // restore for else branch
    if (if_node->otherwise) {
        push_scope(mt);  // isolate branch variables
        MIR_reg_t else_val = transpile_expr(mt, if_node->otherwise);
        if (mt->block_returned) {
            // Same as above: terminal in else branch
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            mt->block_returned = false;
        } else if (proc_discard) {
            // Proc context: result unused, skip expensive boxing.
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        } else if (need_boxing) {
            TypeId box_tid = else_tid;
            if (if_node->otherwise->node_type == AST_NODE_CONTENT ||
                if_node->otherwise->node_type == AST_NODE_LIST) {
                box_tid = LMD_TYPE_ANY;
            }
            MIR_reg_t boxed = emit_box(mt, else_val, box_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        }
        pop_scope(mt);
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
            MIR_reg_t tid_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut)));
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
            MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val)));

            mt->pipe_item_reg = old_pipe_item;
            mt->in_pipe = old_in_pipe;

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
                MIR_new_reg_op(mt->ctx, truthy)));
            return;
        }
    }

    MIR_reg_t pat = transpile_expr(mt, pattern);
    TypeId pat_tid = get_effective_type(mt, pattern);
    MIR_reg_t boxed_pat = emit_box(mt, pat, pat_tid);

    MIR_reg_t match_test;
    if (pat_tid == LMD_TYPE_TYPE) {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
    } else if (pat_tid == LMD_TYPE_RANGE) {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
    } else {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
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
    // TCO: scrutinee is NOT in tail position
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    MIR_reg_t scrutinee = transpile_expr(mt, match_node->scrutinee);

    // Restore tail position for arm bodies
    mt->in_tail_position = saved_tail;
    TypeId scrut_tid = get_effective_type(mt, match_node->scrutinee);
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

            // Body (restore tail position before each arm body)
            mt->in_tail_position = saved_tail;
            MIR_reg_t body = transpile_box_item(mt, match_arm->body);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            emit_label(mt, l_next);
        } else {
            // Default arm (restore tail position)
            mt->in_tail_position = saved_tail;
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

    int key_filter = (int)loop->key_filter;  // 0=ALL, 1=INT, 2=SYMBOL

    // Evaluate collection
    MIR_reg_t collection = transpile_expr(mt, loop->as);
    TypeId coll_tid = get_effective_type(mt, loop->as);

    // Box the collection
    MIR_reg_t boxed_coll = emit_box(mt, collection, coll_tid);

    // Get keys (for maps/elements/objects - returns ArrayList* or NULL for arrays)
    MIR_reg_t keys_al = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));

    // Get unified iteration length via iter_len(data, keys, key_filter)
    MIR_reg_t len = emit_call_3(mt, "iter_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

    // Create spreadable output array
    MIR_reg_t output = emit_call_0(mt, "array_spreadable", MIR_T_P);

    // If order by is present, allocate a parallel keys array
    bool has_order = for_node->order != NULL;
    bool has_limit = for_node->limit != NULL;
    bool has_offset = for_node->offset != NULL;
    MIR_reg_t keys_arr = 0;
    if (has_order) {
        keys_arr = emit_call_0(mt, "array_plain", MIR_T_P);
    }

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

    // Get current value via iter_val_at(data, keys, idx, key_filter)
    MIR_reg_t current_item = emit_call_4(mt, "iter_val_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

    // Determine the proper type for the loop variable from AST
    TypeId val_tid = loop->type ? loop->type->type_id : LMD_TYPE_ANY;
    MIR_type_t val_mtype = MIR_T_I64;
    MIR_reg_t val_reg = current_item;

    // For known element types, unbox to the proper MIR type
    if (val_tid == LMD_TYPE_FLOAT) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_FLOAT);
        val_mtype = MIR_T_D;
    } else if (val_tid == LMD_TYPE_INT) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT);
        val_mtype = MIR_T_I64;
    } else if (val_tid == LMD_TYPE_INT64) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT64);
        val_mtype = MIR_T_I64;
    } else if (val_tid == LMD_TYPE_STRING) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_STRING);
        val_mtype = MIR_T_P;
    }

    // Bind loop value variable
    char var_name[128];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)loop->name->len, loop->name->chars);
    set_var(mt, var_name, val_reg, val_mtype, val_tid);

    // Bind index/key variable if present
    if (loop->index_name) {
        char idx_name[128];
        snprintf(idx_name, sizeof(idx_name), "%.*s", (int)loop->index_name->len, loop->index_name->chars);

        // Get key via iter_key_at(data, keys, idx, key_filter)
        MIR_reg_t key_item = emit_call_4(mt, "iter_key_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));
        set_var(mt, idx_name, key_item, MIR_T_I64, LMD_TYPE_ANY);
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
        TypeId where_tid = get_effective_type(mt, for_node->where);
        MIR_reg_t where_test = where_val;
        if (where_tid != LMD_TYPE_BOOL) {
            MIR_reg_t boxw = emit_box(mt, where_val, where_tid);
            where_test = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxw)));
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_continue),
            MIR_new_reg_op(mt->ctx, where_test)));
    }

    // Body expression
    MIR_reg_t body_result = transpile_expr(mt, for_node->then);
    TypeId body_tid = get_effective_type(mt, for_node->then);
    MIR_reg_t boxed_result = emit_box(mt, body_result, body_tid);

    // Push to output (use spread to flatten nested spreadable arrays)
    emit_call_void_2(mt, "array_push_spread", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

    // If order by is present, also push the sort key value
    if (has_order) {
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        MIR_reg_t key_val = transpile_expr(mt, first_spec->expr);
        TypeId key_tid = get_effective_type(mt, first_spec->expr);
        MIR_reg_t boxed_key = emit_box(mt, key_val, key_tid);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_key));
    }

    // Continue: increment index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);

    // Post-processing: ORDER BY, then OFFSET, then LIMIT
    MIR_reg_t final_reg;

    if (has_order) {
        // Sort arr_out in-place by the collected keys, with ascending/descending flag
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        int64_t desc_flag = first_spec->descending ? 1 : 0;
        emit_call_void_3(mt, "fn_sort_by_keys",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, output),  // cast Array* to Item
            MIR_T_I64, MIR_new_reg_op(mt->ctx, keys_arr), // cast Array* to Item
            MIR_T_I64, MIR_new_int_op(mt->ctx, desc_flag));
    }

    if (has_order && (has_offset || has_limit)) {
        // When order by + offset/limit, apply them in-place on arr_out
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            MIR_reg_t off_raw = off_val;
            if (off_tid != LMD_TYPE_INT && off_tid != LMD_TYPE_INT64) {
                off_raw = emit_unbox(mt, emit_box(mt, off_val, off_tid), LMD_TYPE_INT);
            }
            // Strip tag bits from int value
            MIR_reg_t off_masked = emit_unbox_int_mask(mt, off_raw);
            emit_call_void_2(mt, "array_drop_inplace",
                MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, off_masked));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            MIR_reg_t lim_raw = lim_val;
            if (lim_tid != LMD_TYPE_INT && lim_tid != LMD_TYPE_INT64) {
                lim_raw = emit_unbox(mt, emit_box(mt, lim_val, lim_tid), LMD_TYPE_INT);
            }
            MIR_reg_t lim_masked = emit_unbox_int_mask(mt, lim_raw);
            emit_call_void_2(mt, "array_limit_inplace",
                MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_masked));
        }
        // Finalize via array_end
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    } else if (has_offset || has_limit) {
        // Without order by, use fn_drop/fn_take which return spreadable results

        MIR_reg_t cur_result = emit_box_container(mt, output); // cast Array* → Item
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            MIR_reg_t off_boxed = emit_box(mt, off_val, off_tid);
            cur_result = emit_call_2(mt, "fn_drop", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, off_boxed));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            MIR_reg_t lim_boxed = emit_box(mt, lim_val, lim_tid);
            cur_result = emit_call_2(mt, "fn_take", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_boxed));
        }
        final_reg = cur_result;
    } else {
        // No post-processing — just finalize via array_end
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    }

    if (mt->loop_depth > 0) mt->loop_depth--;
    pop_scope(mt);

    // If array_end returned ITEM_NULL_SPREADABLE (empty for-expr),
    // convert to a proper empty Array* so [for (x in []) x] returns []
    uint64_t NULL_SPREAD = (uint64_t)LMD_TYPE_NULL << 56 | 1;
    MIR_reg_t is_spread = new_reg(mt, "sns", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_spread),
        MIR_new_reg_op(mt->ctx, final_reg), MIR_new_int_op(mt->ctx, (int64_t)NULL_SPREAD)));
    MIR_label_t l_not_spread = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_not_spread),
        MIR_new_reg_op(mt->ctx, is_spread)));
    // Use the Array* pointer directly — it's a valid empty container
    MIR_reg_t empty_arr = emit_box_container(mt, output);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, final_reg),
        MIR_new_reg_op(mt->ctx, empty_arr)));
    emit_label(mt, l_not_spread);

    return final_reg;
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

    // Condition — use get_effective_type to detect boxed values from runtime
    // calls (e.g., fn_gt returns boxed bool when operands are ANY)
    MIR_reg_t cond = transpile_expr(mt, while_node->cond);
    TypeId cond_tid = get_effective_type(mt, while_node->cond);
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
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
                TypeId expr_tid = get_effective_type(mt, asn->as);
                // Use the variable's declared type if available, otherwise the expression type.
                // But if the expression is boxed ANY (e.g. captured variable), the declared
                // type from AST is stale — use ANY to match the actual runtime value.
                // Also: when declared type is ANY (untyped var) but expression type is concrete,
                // prefer the expression type so type narrowing propagates through assignments.
                TypeId var_tid = declare->type ? declare->type->type_id : expr_tid;
                if (expr_tid == LMD_TYPE_ANY) var_tid = LMD_TYPE_ANY;
                if (var_tid == LMD_TYPE_ANY && expr_tid != LMD_TYPE_ANY) var_tid = expr_tid;

                // Detect fill(n, int_val) → narrows variable to ARRAY_INT for inline access.
                // fill() with an int fill value always creates ArrayInt at runtime.
                // The variable still stores a tagged Item (fill returns boxed container);
                // ARRAY_INT type_id tells downstream code what kind of container it is.
                TypeId fill_elem_type = LMD_TYPE_ANY;  // P4-3.1: track fill element type
                {
                    AstNode* rhs = asn->as;
                    // Unwrap PRIMARY wrapper nodes (parenthesized expressions)
                    while (rhs && rhs->node_type == AST_NODE_PRIMARY) {
                        AstPrimaryNode* pri = (AstPrimaryNode*)rhs;
                        if (pri->expr) rhs = pri->expr; else break;
                    }
                    if (var_tid == LMD_TYPE_ANY && rhs && rhs->node_type == AST_NODE_CALL_EXPR) {
                        AstCallNode* call = (AstCallNode*)rhs;
                        if (call->function && call->function->node_type == AST_NODE_SYS_FUNC) {
                            AstSysFuncNode* sys = (AstSysFuncNode*)call->function;
                            if (sys->fn_info && sys->fn_info->fn == SYSFUNC_FILL) {
                                AstNode* arg2 = call->argument ? call->argument->next : nullptr;
                                if (arg2) {
                                    TypeId fill_val_tid = get_effective_type(mt, arg2);
                                    if (fill_val_tid == LMD_TYPE_INT || fill_val_tid == LMD_TYPE_INT64) {
                                        var_tid = LMD_TYPE_ARRAY_INT;
                                    } else if (fill_val_tid == LMD_TYPE_FLOAT) {
                                        var_tid = LMD_TYPE_ARRAY_FLOAT;
                                    } else if (fill_val_tid == LMD_TYPE_BOOL) {
                                        // P4-3.1: fill(n, bool) → generic Array of bools
                                        var_tid = LMD_TYPE_ARRAY;
                                        fill_elem_type = LMD_TYPE_BOOL;
                                    }
                                }
                            }
                        }
                    }
                }

                log_debug("mir: let/var '%s' declare_type=%d expr_tid=%d var_tid=%d",
                    name_buf, declare->type ? (int)declare->type->type_id : -1, expr_tid, var_tid);

                // Runtime typed array coercion for occurrence annotations (int[], float[], etc.)
                // When declared type is TypeUnary and RHS is dynamic or a mismatched typed array,
                // call ensure_typed_array to convert at runtime.
                if (declare->type && declare->type->kind == TYPE_KIND_UNARY) {
                    bool needs_coerce = (expr_tid == LMD_TYPE_ANY || expr_tid == LMD_TYPE_NULL ||
                                         expr_tid == LMD_TYPE_ARRAY || expr_tid == LMD_TYPE_ARRAY ||
                                         expr_tid == LMD_TYPE_ARRAY_INT || expr_tid == LMD_TYPE_ARRAY_INT64 ||
                                         expr_tid == LMD_TYPE_ARRAY_FLOAT);
                    if (needs_coerce) {
                        // extract element type from TypeUnary operand
                        TypeUnary* unary = (TypeUnary*)declare->type;
                        Type* operand = unary->operand;
                        if (operand && operand->type_id == LMD_TYPE_TYPE && operand->kind == TYPE_KIND_SIMPLE) {
                            operand = ((TypeType*)operand)->type;
                        }
                        TypeId elem_tid = operand ? operand->type_id : LMD_TYPE_ANY;
                        // box the expression value to Item if not already boxed
                        MIR_reg_t boxed = emit_box(mt, val, expr_tid);
                        // call ensure_typed_array(item, element_type_id) → void* (pointer)
                        val = emit_call_2(mt, "ensure_typed_array", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, elem_tid));
                        // result is a pointer (stored as I64), treat as ANY
                        var_tid = LMD_TYPE_ANY;
                    }
                }

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

                // Copy value to a new register so the let binding has its own
                // independent copy. Without this, `let tmp = a` shares a's
                // register, so subsequent mutations to a also affect tmp.
                MIR_reg_t copy = new_reg(mt, "letv", mtype);
                if (mtype == MIR_T_D) {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                        MIR_new_reg_op(mt->ctx, copy), MIR_new_reg_op(mt->ctx, val)));
                } else {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, copy), MIR_new_reg_op(mt->ctx, val)));
                }

                // Store in current scope
                set_var(mt, name_buf, copy, mtype, var_tid);

                // P4-3.1: Set element type for container variables (from fill() narrowing)
                // Only set when var_tid is still ARRAY (not overridden by typed array coercion)
                if (fill_elem_type != LMD_TYPE_ANY && var_tid == LMD_TYPE_ARRAY) {
                    MirVarEntry* v = find_var(mt, name_buf);
                    if (v) v->elem_type = fill_elem_type;
                }

                // P4-3.2: Set elem_type for array variables with known nested type,
                // ONLY when the variable is never index-mutated in the current scope.
                // This enables NATIVE return from FAST PATH 1b/1d array indexing.
                // Must check after fill narrowing (which handles fill(n, bool) → BOOL).
                if (var_tid == LMD_TYPE_ARRAY && fill_elem_type == LMD_TYPE_ANY) {
                    // Check AST nested type from array literal or typed array assignment
                    AstNode* rhs = asn->as;
                    while (rhs && rhs->node_type == AST_NODE_PRIMARY)
                        rhs = ((AstPrimaryNode*)rhs)->expr;
                    Type* rhs_type = rhs ? rhs->type : nullptr;
                    if (rhs_type && rhs_type->type_id == LMD_TYPE_ARRAY) {
                        TypeArray* arr_type = (TypeArray*)rhs_type;
                        if (arr_type->nested &&
                            (arr_type->nested->type_id == LMD_TYPE_INT ||
                             arr_type->nested->type_id == LMD_TYPE_BOOL)) {
                            // Check mutation: scan current scope for arr[i] = val
                            if (mt->func_body && !has_index_mutation(name_buf, mt->func_body)) {
                                MirVarEntry* v = find_var(mt, name_buf);
                                if (v && v->elem_type == LMD_TYPE_ANY) {
                                    v->elem_type = arr_type->nested->type_id;
                                }
                            }
                        }
                    }
                }

                // If this is a module-level variable (not inside a user function),
                // store to BSS so other functions can access it
                if (!mt->in_user_func) {
                    GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                    if (gvar) {
                        store_global_var(mt, gvar, val, var_tid);
                    }
                }

                // Handle error destructuring: let a^err = expr
                if (asn->error_name) {
                    // Box the value for error checking
                    MIR_reg_t boxed = emit_box(mt, val, var_tid);
                    // Check if result is an error: item_type_id(boxed) == LMD_TYPE_ERROR
                    // item_type_id returns uint8_t TypeId — zero-extend for clean comparison
                    MIR_reg_t type_id = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
                    MIR_reg_t is_error = new_reg(mt, "iserr", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_error),
                        MIR_new_reg_op(mt->ctx, type_id),
                        MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));

                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    MIR_label_t l_is_err = new_label(mt);
                    MIR_label_t l_end = new_label(mt);

                    // error_var = is_error ? boxed : ITEM_NULL
                    // value_var = is_error ? ITEM_NULL : boxed
                    MIR_reg_t err_result = new_reg(mt, "errv", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_is_err),
                        MIR_new_reg_op(mt->ctx, is_error)));
                    // Non-error path: val = boxed (convert native to boxed Item)
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, val),
                        MIR_new_reg_op(mt->ctx, boxed)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
                    emit_label(mt, l_is_err);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_reg_op(mt->ctx, boxed)));
                    // If error: value_var = ITEM_NULL
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, val),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    emit_label(mt, l_end);
                    // After both paths, val is a boxed Item (either boxed value or NULL)
                    set_var(mt, name_buf, val, MIR_T_I64, LMD_TYPE_ANY);

                    // Update BSS global if module-level (val may have changed to NULL on error)
                    if (!mt->in_user_func) {
                        GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                        if (gvar) {
                            store_global_var(mt, gvar, val, LMD_TYPE_ANY);
                        }
                    }

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
                TypeId src_tid = get_effective_type(mt, dec->as);
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

                    // Store to BSS if this is a module-level decomposed variable
                    if (!mt->in_user_func) {
                        GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                        if (gvar) {
                            store_global_var(mt, gvar, extracted, LMD_TYPE_ANY);
                        }
                    }
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
    // Check if any child is a for-expression, spread, or let binding
    bool has_spreadable = false;
    bool has_let = false;
    AstNode* scan = arr_node->item;
    while (scan) {
        if (scan->node_type == AST_NODE_FOR_EXPR || scan->node_type == AST_NODE_SPREAD) {
            has_spreadable = true;
        }
        if (scan->node_type == AST_NODE_ASSIGN) {
            has_let = true;
        }
        scan = scan->next;
    }

    // push scope to contain let bindings within the array
    if (has_let) push_scope(mt);

    // Detect homogeneous typed arrays (ArrayInt, ArrayFloat) to match C transpiler behavior.
    // Vector functions (concat, take, drop, reverse, etc.) dispatch based on runtime type_id,
    // so creating the correct specialized array type is essential.
    TypeArray* arr_type = (TypeArray*)arr_node->type;
    bool is_int_array = arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
    bool is_float_array = arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_FLOAT;

    // Specialized ArrayFloat path: array_float_new(count) + array_float_set(arr, i, val)
    // Skip specialized paths when array has let bindings (let nodes are transparent)
    if (is_float_array && !has_spreadable && !has_let && arr_node->item) {
        int count = (int)arr_type->length;
        MIR_reg_t arr = emit_call_1(mt, "array_float_new", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, count));
        int idx = 0;
        AstNode* item = arr_node->item;
        while (item) {
            MIR_reg_t val = transpile_expr(mt, item);
            // val is native double (MIR_T_D) for float literals/expressions
            TypeId val_tid = item->type ? item->type->type_id : LMD_TYPE_ANY;
            if (val_tid != LMD_TYPE_FLOAT) {
                // coerce non-float to double via i2d or unbox
                if (val_tid == LMD_TYPE_INT) {
                    MIR_reg_t dval = new_reg(mt, "i2d", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dval),
                        MIR_new_reg_op(mt->ctx, val)));
                    val = dval;
                }
            }
            emit_call_void_3(mt, "array_float_set",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_D, MIR_new_reg_op(mt->ctx, val));
            idx++;
            item = item->next;
        }
        if (has_let) pop_scope(mt);
        return arr;  // ArrayFloat* is a container pointer
    }

    // Specialized ArrayInt path: array_int_new(count) + array_int_set(arr, i, val)
    // Skip specialized paths when array has let bindings (let nodes are transparent)
    if (is_int_array && !has_spreadable && !has_let && arr_node->item) {
        int count = (int)arr_type->length;
        MIR_reg_t arr = emit_call_1(mt, "array_int_new", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, count));
        int idx = 0;
        AstNode* item = arr_node->item;
        while (item) {
            MIR_reg_t val = transpile_expr(mt, item);
            // val is native int64 for int literals/expressions
            emit_call_void_3(mt, "array_int_set",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            idx++;
            item = item->next;
        }
        if (has_let) pop_scope(mt);
        return arr;  // ArrayInt* is a container pointer
    }

    // Generic array path
    MIR_reg_t arr = emit_call_0(mt, "array", MIR_T_P);

    // Handle empty arrays without array_end() which returns ITEM_NULL_SPREADABLE
    if (!arr_node->item) {
        // Array* pointer is already a valid Item (container pointer)
        if (has_let) pop_scope(mt);
        return arr;
    }

    AstNode* item = arr_node->item;
    while (item) {
        MIR_reg_t val = transpile_expr(mt, item);

        // let bindings are transparent - evaluate for side effect but don't push to array
        if (item->node_type == AST_NODE_ASSIGN) {
            item = item->next;
            continue;
        }

        TypeId val_tid = get_effective_type(mt, item);

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

    MIR_reg_t arr_result = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, arr));

    // If array contains spreadable children (for-expressions) and all produce
    // empty results, array_end returns ITEM_NULL_SPREADABLE. For top-level
    // array literals [for ...], convert to proper empty array.
    if (has_spreadable) {
        uint64_t NULL_SPREAD = (uint64_t)LMD_TYPE_NULL << 56 | 1;
        MIR_reg_t is_sn = new_reg(mt, "sn2", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_sn),
            MIR_new_reg_op(mt->ctx, arr_result), MIR_new_int_op(mt->ctx, (int64_t)NULL_SPREAD)));
        MIR_label_t l_not_sn = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_not_sn),
            MIR_new_reg_op(mt->ctx, is_sn)));
        MIR_reg_t empty_a = emit_box_container(mt, arr);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_result),
            MIR_new_reg_op(mt->ctx, empty_a)));
        emit_label(mt, l_not_sn);
    }

    if (has_let) pop_scope(mt);
    return arr_result;
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
            MIR_reg_t result = transpile_expr(mt, last_value);
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
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_OBJECT_TYPE ||
            item->node_type == AST_NODE_FUNC ||
            item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN) {
            item = item->next;
            continue;
        }
        // Use transpile_box_item for proper type-aware boxing
        MIR_reg_t boxed = transpile_box_item(mt, item);
        emit_call_void_2(mt, "list_push_spread",
            MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        item = item->next;
    }

    return emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
}

// Check if a node type is a declaration (processed separately, not a value)
static bool is_declaration_node(int node_type) {
    switch (node_type) {
    case AST_NODE_LET_STAM: case AST_NODE_PUB_STAM:
    case AST_NODE_TYPE_STAM: case AST_NODE_VAR_STAM:
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN:
        return true;
    default:
        return false;
    }
}

// Check if a node type is a procedural side-effect statement.
// These execute for side effects but do NOT produce output values.
// NOTE: IF_EXPR (with block form), WHILE_STAM, FOR_STAM can appear in functional code
// and produce values, so they are NOT included here.
static bool is_side_effect_stam(int node_type) {
    switch (node_type) {
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
        return true;
    default:
        return false;
    }
}

static MIR_reg_t transpile_content(MirTranspiler* mt, AstListNode* list_node) {
    // TCO: content block items are NOT in tail position. Only return statements
    // (which set in_tail_position=true for their value) should be considered tail.
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    // Detect proc context: either we're inside a pn function (mt->in_proc),
    // or the content block contains proc-only nodes like VAR_STAM.
    // In proc context, IF_EXPR/WHILE_STAM/FOR_STAM are side-effect statements,
    // and only the LAST value expression contributes to the result.
    bool is_proc = mt->in_proc;
    if (!is_proc) {
        AstNode* scan = list_node->item;
        while (scan) {
            if (scan->node_type == AST_NODE_VAR_STAM) { is_proc = true; break; }
            scan = scan->next;
        }
    }

    // Count: declarations, side-effect statements, and value expressions
    AstNode* scan = list_node->item;
    int decl_count = 0, stam_count = 0, value_count = 0;
    AstNode* last_value = nullptr;
    while (scan) {
        if (is_declaration_node(scan->node_type)) {
            decl_count++;
        } else if (is_side_effect_stam(scan->node_type)) {
            stam_count++;
        } else if (is_proc &&
                   (scan->node_type == AST_NODE_IF_EXPR ||
                    scan->node_type == AST_NODE_WHILE_STAM ||
                    scan->node_type == AST_NODE_FOR_STAM)) {
            // In proc context, these are side-effect statements
            stam_count++;
        } else {
            value_count++;
            last_value = scan;
        }
        scan = scan->next;
    }

    // In proc context with multiple values, only the LAST value expression
    // contributes to the result. Preceding value expressions are side effects.
    // Treat as single-value block expression.
    if (is_proc && value_count > 1) {
        push_scope(mt);
        MIR_reg_t result = 0;
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                }
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_expr(mt, item);
            } else if (item->node_type == AST_NODE_IF_EXPR ||
                       item->node_type == AST_NODE_WHILE_STAM ||
                       item->node_type == AST_NODE_FOR_STAM) {
                transpile_expr(mt, item);
            } else if (item == last_value) {
                // Last value expression: this is the return value
                result = transpile_box_item(mt, item);
            } else {
                // Non-last value expression in proc: side effect only
                transpile_expr(mt, item);
            }
            item = item->next;
        }
        pop_scope(mt);
        return result;
    }

    // Single value with declarations/statements: block expression
    // Process all items in order, return the single value expression result.
    // Exclude FOR_EXPR/FOR_STAM — spreadable results need list_push_spread
    if (value_count == 1 && last_value && (decl_count > 0 || stam_count > 0)
        && last_value->node_type != AST_NODE_FOR_EXPR
        && last_value->node_type != AST_NODE_FOR_STAM) {
        push_scope(mt);
        MIR_reg_t result = 0;
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                }
                // Func defs handled in module-level pre-pass
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_expr(mt, item); // execute for side effects
            } else if (is_proc &&
                       (item->node_type == AST_NODE_IF_EXPR ||
                        item->node_type == AST_NODE_WHILE_STAM ||
                        item->node_type == AST_NODE_FOR_STAM)) {
                transpile_expr(mt, item); // proc context side effect
            } else {
                // This is the single value expression
                result = transpile_box_item(mt, item);
            }
            item = item->next;
        }
        pop_scope(mt);
        return result;
    }

    // Single value without declarations: just return it boxed
    if (value_count == 1 && last_value && decl_count == 0 && stam_count == 0) {
        return transpile_box_item(mt, last_value);
    }

    // No value items: execute side-effect statements and return null
    if (value_count == 0) {
        push_scope(mt);
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                }
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_expr(mt, item);
            } else if (is_proc &&
                       (item->node_type == AST_NODE_IF_EXPR ||
                        item->node_type == AST_NODE_WHILE_STAM ||
                        item->node_type == AST_NODE_FOR_STAM)) {
                transpile_expr(mt, item); // proc context side effect
            }
            item = item->next;
        }
        // In proc context, the result of a side-effect-only block is never used.
        // Skip the expensive list()/list_end() allocation and return a null Item.
        if (is_proc) {
            MIR_reg_t result = new_reg(mt, "null_block", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            pop_scope(mt);
            return result;
        }
        MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
        MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
        pop_scope(mt);
        return result;
    }

    // Multiple values: build a list (functional context)
    push_scope(mt);

    // Process all items in order
    MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
    AstNode* item = list_node->item;
    while (item) {
        if (is_declaration_node(item->node_type)) {
            if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                transpile_let_stam(mt, (AstLetNode*)item);
            }
            item = item->next;
            continue;
        }
        if (is_side_effect_stam(item->node_type)) {
            transpile_expr(mt, item); // execute for side effects, don't push to list
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
    TypeMap* map_type = (TypeMap*)map_node->type;
    int type_index = map_type->type_index;

    // Create map with inline data: Map* m = map_with_data(type_index)
    MIR_reg_t m = 0; // allocated below based on path

    // Count value items
    AstNode* item = map_node->item;
    int val_count = 0;
    while (item) { val_count++; item = item->next; }

    // =========================================================================
    // Optimization: Direct field stores for typed maps with known shapes.
    // Additionally, map_with_data() is inlined: we call heap_calloc directly
    // and set Map header fields with immediate constants, eliminating:
    //   - map_with_data() function call overhead
    //   - type_list[type_index] runtime lookup (2 pointer chases)
    //   - C varargs marshaling/unmarshaling overhead
    //   - set_fields() per-field type switch dispatch
    //   - runtime type checking for each field
    // =========================================================================
    if ((mir_has_fixed_shape(map_type) || map_type->has_named_shape) &&
        map_type->byte_size > 0 && val_count > 0 && val_count == (int)map_type->length) {
        // Check all fields: named, typed, 8-byte aligned offsets, supported types
        bool all_direct = true;
        ShapeEntry* check = map_type->shape;
        while (check) {
            if (!check->name || !check->type ||
                check->byte_offset % sizeof(void*) != 0) {
                all_direct = false;
                break;
            }
            TypeId ft = mir_resolve_field_type(check);
            if (ft == LMD_TYPE_ANY) {
                all_direct = false;
                break;
            }
            if (ft != LMD_TYPE_FLOAT && ft != LMD_TYPE_INT && ft != LMD_TYPE_INT64 &&
                ft != LMD_TYPE_BOOL && ft != LMD_TYPE_STRING && ft != LMD_TYPE_NULL &&
                !mir_is_container_field_type(ft)) {
                all_direct = false;
                break;
            }
            check = check->next;
        }

        if (all_direct) {
            log_debug("mir: inline alloc + direct field store for typed map '%s' (%d fields)",
                map_type->struct_name, val_count);

            // Inline map_with_data: call heap_calloc directly with compile-time constants
            int64_t byte_size = map_type->byte_size;
            int64_t total_size = (int64_t)sizeof(Map) + byte_size;

            // Compute size class at JIT compile time to skip runtime class_index lookup.
            // Size classes: {16, 32, 48, 64, 96, 128, 256} — find smallest that fits.
            int alloc_class = gc_object_zone_class_index((size_t)total_size);
            int64_t class_size = (alloc_class >= 0) ? (int64_t)gc_object_zone_class_size(alloc_class) : total_size;
            int64_t slot_size = (int64_t)sizeof(gc_header_t) + class_size;

            // ================================================================
            // INLINE BUMP-POINTER ALLOCATION (#10 + #11)
            // Fast path: bump gc->bump_cursor by slot_size.
            // Slow path: call heap_calloc_class (handles free list + new block).
            // gc_reg is loaded at function entry: context->heap->gc
            // ================================================================
            if (alloc_class >= 0 && mt->gc_reg != 0) {
                // gc_heap_t offsets: bump_cursor=16, bump_end=24, all_objects=8
                MIR_reg_t cursor = new_reg(mt, "bcur", MIR_T_I64);
                MIR_reg_t new_cursor = new_reg(mt, "bnew", MIR_T_I64);
                MIR_reg_t bend = new_reg(mt, "bend", MIR_T_I64);

                // Load bump_cursor and bump_end from gc_heap_t
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, mt->gc_reg, 0, 1)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, new_cursor),
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_int_op(mt->ctx, slot_size)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, bend),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 24, mt->gc_reg, 0, 1)));

                // Branch: if new_cursor > bump_end → slow path
                MIR_label_t slow_label = MIR_new_label(mt->ctx);
                MIR_label_t done_label = MIR_new_label(mt->ctx);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UBGT,
                    MIR_new_label_op(mt->ctx, slow_label),
                    MIR_new_reg_op(mt->ctx, new_cursor),
                    MIR_new_reg_op(mt->ctx, bend)));

                // ---- Fast path: bump succeeded ----
                // Store updated cursor back to gc->bump_cursor
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, mt->gc_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, new_cursor)));

                // m = cursor + sizeof(gc_header_t) — user pointer past header
                m = new_reg(mt, "mptr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, m),
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_int_op(mt->ctx, (int64_t)sizeof(gc_header_t))));

                // Initialize gc_header_t at cursor (memory is pre-zeroed):
                //   header->next = gc->all_objects  (offset 0 from cursor)
                //   header->type_tag = LMD_TYPE_MAP (offset 8, uint16)
                //   header->alloc_size = total_size (offset 12, uint32)
                //   gc_flags=0 and marked=0 are already zero from block memset
                MIR_reg_t old_head = new_reg(mt, "oldh", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, old_head),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, mt->gc_reg, 0, 1))); // gc->all_objects
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, cursor, 0, 1),          // header->next
                    MIR_new_reg_op(mt->ctx, old_head)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, mt->gc_reg, 0, 1),    // gc->all_objects = cursor
                    MIR_new_reg_op(mt->ctx, cursor)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U16, 8, cursor, 0, 1),          // header->type_tag
                    MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U32, 12, cursor, 0, 1),         // header->alloc_size
                    MIR_new_int_op(mt->ctx, total_size)));

                // Set Container.is_heap = 1 (flags byte at user+1)
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 1, m, 0, 1),
                    MIR_new_int_op(mt->ctx, 1)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, done_label)));

                // ---- Slow path: call heap_calloc_class ----
                emit_insn(mt, slow_label);
                MIR_reg_t slow_m = emit_call_3(mt, "heap_calloc_class", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)alloc_class));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, m),
                    MIR_new_reg_op(mt->ctx, slow_m)));

                // Reload gc->bump_cursor after slow path (may have changed block)
                // Not needed for m, but needed for future allocations in this function.
                // gc_reg is stable (gc_heap_t* doesn't change).

                emit_insn(mt, done_label);
            } else if (alloc_class >= 0) {
                m = emit_call_3(mt, "heap_calloc_class", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)alloc_class));
            } else {
                // Fallback for very large maps (unlikely)
                m = emit_call_2(mt, "heap_calloc", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP));
            }

            // Set Map header fields
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, m, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, m, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)map_type)));

            // Compute and store data pointer = m + sizeof(Map)
            MIR_reg_t data_ptr = new_reg(mt, "mdata", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, data_ptr),
                MIR_new_reg_op(mt->ctx, m),
                MIR_new_int_op(mt->ctx, (int64_t)sizeof(Map))));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, m, 0, 1),
                MIR_new_reg_op(mt->ctx, data_ptr)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I32, 24, m, 0, 1),
                MIR_new_int_op(mt->ctx, byte_size)));

            // Store each field directly at known byte offset
            item = map_node->item;
            ShapeEntry* field = map_type->shape;
            while (item && field) {
                TypeId field_type = mir_resolve_field_type(field);
                int64_t offset = field->byte_offset;

                // Extract value AST node from key-value pair
                AstNode* value_node = nullptr;
                bool is_null_literal = false;
                if (item->node_type == AST_NODE_KEY_EXPR) {
                    AstNamedNode* key_expr = (AstNamedNode*)item;
                    value_node = key_expr->as;
                    is_null_literal = (value_node == nullptr);
                } else {
                    value_node = item;
                }

                if (is_null_literal || field_type == LMD_TYPE_NULL) {
                    // Data buffer is zero-initialized by heap_calloc — skip store
                } else if (field_type == LMD_TYPE_FLOAT) {
                    // Store native double at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (val_tid == LMD_TYPE_FLOAT) {
                        val = transpile_expr(mt, value_node);
                    } else if (val_tid == LMD_TYPE_INT || val_tid == LMD_TYPE_INT64) {
                        MIR_reg_t int_val = transpile_expr(mt, value_node);
                        val = new_reg(mt, "i2d", MIR_T_D);
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                            MIR_new_reg_op(mt->ctx, val),
                            MIR_new_reg_op(mt->ctx, int_val)));
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, LMD_TYPE_FLOAT);
                    }
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64 ||
                           field_type == LMD_TYPE_BOOL) {
                    // Store native int/bool at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (val_tid == LMD_TYPE_INT || val_tid == LMD_TYPE_INT64 ||
                        val_tid == LMD_TYPE_BOOL) {
                        val = transpile_expr(mt, value_node);
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, field_type);
                    }
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (field_type == LMD_TYPE_STRING) {
                    // Store raw String* pointer at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (val_tid == LMD_TYPE_STRING) {
                        val = transpile_expr(mt, value_node);
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, LMD_TYPE_STRING);
                    }
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (mir_is_container_field_type(field_type)) {
                    // Container types: get boxed Item, strip tag → raw Container*
                    // For null Items: AND mask strips to 0 (matching nullptr)
                    MIR_reg_t boxed = transpile_box_item(mt, value_node);
                    MIR_reg_t raw = emit_unbox_container(mt, boxed);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
                        MIR_new_reg_op(mt->ctx, raw)));
                }

                item = item->next;
                field = field->next;
            }

            return m;
        }
    }

    // Non-direct path: allocate map via map_with_data()
    m = emit_call_1(mt, "map_with_data", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(mt->ctx, type_index));

    if (val_count == 0) {
        return m;
    }

    // Fallback: evaluate all values as boxed Items and call map_fill() via varargs
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

    // Call map_fill(m, val1, val2, ...) — variadic
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

    // Create element: Element* el = elmt(type_index)
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

        // Call elmt_fill(el, val1, val2, ...) — variadic
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
        // else: no attrs and no content — element is just a bare pointer,
    }

    return el;
}

// ============================================================================
// Member/Index access
// ============================================================================

// Emit direct field READ from a packed map/object data struct.
// Map/Object layout: [TypeId(1) flags(1) pad(6)] [void* type @8] [void* data @16] [int data_cap @24]
// data points to packed struct where each field is 8-byte aligned.
// Returns native value for scalars (INT/FLOAT/BOOL/STRING) or tagged Item for containers.
// obj_boxed: pre-computed tagged Item for the object (avoids double-evaluation).
// skip_null_guard: when true, omit the null check branch (caller guarantees non-null).
static MIR_reg_t emit_mir_direct_field_read(MirTranspiler* mt, MIR_reg_t obj_boxed,
    ShapeEntry* field, bool skip_null_guard) {
    TypeId type_id = mir_resolve_field_type(field);
    int64_t offset = field->byte_offset;

    // strip tag from boxed Item → raw Map*/Object*
    MIR_reg_t map_ptr = emit_unbox_container(mt, obj_boxed);

    // Null guard: if map_ptr == 0 (object is null), return default value.
    // Skipped when the caller can prove the object is non-null (e.g. typed
    // local variable or parameter — the type annotation is a non-null contract).
    MIR_label_t l_not_null = 0;
    MIR_label_t l_done = 0;
    if (!skip_null_guard) {
        l_not_null = new_label(mt);
        l_done = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, l_not_null),
            MIR_new_reg_op(mt->ctx, map_ptr), MIR_new_int_op(mt->ctx, 0)));
    }

    // null path: produce default value
    if (type_id == LMD_TYPE_FLOAT) {
        MIR_reg_t result = new_reg(mt, "dfld", MIR_T_D);
        if (!skip_null_guard) {
            // null → 0.0
            MIR_reg_t zero_i = new_reg(mt, "zi", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, zero_i),
                MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, zero_i)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
            // non-null path
            emit_label(mt, l_not_null);
        }

        MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));
        if (skip_null_guard) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1)));
        } else {
            MIR_reg_t nn_result = new_reg(mt, "dfld", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, nn_result),
                MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, nn_result)));
            emit_label(mt, l_done);
        }
        return result;
    }

    if (mir_is_container_field_type(type_id)) {
        // Container fields store raw Container* pointers (no type tag in high bits).
        // The Lambda runtime identifies container types by reading the first byte of
        // the struct (Container.type_id), NOT from Item tag bits. So we must return
        // the raw pointer as-is. For null fields (ptr==0), return ItemNull.
        MIR_reg_t result = new_reg(mt, "cfraw", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        if (!skip_null_guard) {
            // null object → ItemNull
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
            // non-null path
            emit_label(mt, l_not_null);
        }

        // load raw Container* from data buffer
        MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));

        MIR_reg_t raw = new_reg(mt, "cfptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));

        // if raw == 0, field is null → produce ItemNull; else return raw pointer
        MIR_label_t null_field_lbl = new_label(mt);
        MIR_label_t done_field_lbl = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, null_field_lbl),
            MIR_new_reg_op(mt->ctx, raw), MIR_new_int_op(mt->ctx, 0)));
        // non-null: return raw pointer as-is (Container* is a valid Item with _type_id=0;
        // get_type_id() reads Container.type_id from the struct's first byte)
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, raw)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, done_field_lbl)));
        // null field: produce ItemNull
        emit_label(mt, null_field_lbl);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        emit_label(mt, done_field_lbl);
        if (!skip_null_guard) {
            emit_label(mt, l_done);
        }
        return result;
    }

    // Scalar types: load 8 bytes as int64
    // INT/INT64: native int64 value
    // BOOL: bool (1 byte) in 8-byte zero-padded slot → safe to read 8 bytes
    // STRING/SYMBOL/BINARY: raw pointer (String*/Symbol*)
    MIR_reg_t result = new_reg(mt, "mfld", MIR_T_I64);
    if (!skip_null_guard) {
        // null → 0 (default for scalars)
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
        // non-null path
        emit_label(mt, l_not_null);
    }

    MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));
    if (skip_null_guard) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));
    } else {
        MIR_reg_t nn_val = new_reg(mt, "mfld", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nn_val),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, nn_val)));
        emit_label(mt, l_done);
    }
    return result;
}

// Emit direct field WRITE to a packed map/object data struct.
// Stores value at data + byte_offset. Value must be native type for scalars,
// or tagged Item for containers.
static void emit_mir_direct_field_write(MirTranspiler* mt, AstNode* object,
    ShapeEntry* field, AstNode* value, bool skip_null_guard) {
    TypeId type_id = mir_resolve_field_type(field);
    int64_t offset = field->byte_offset;

    // get tagged Item for the object, strip tag → raw Map*/Object*
    MIR_reg_t obj_item = transpile_expr(mt, object);
    MIR_reg_t map_ptr = emit_unbox_container(mt, obj_item);

    // Null guard: if map_ptr == 0 (object is null), skip the write entirely.
    // Skipped when caller proves object is non-null (typed IDENT variable/param).
    MIR_label_t l_skip_write = 0;
    if (!skip_null_guard) {
        l_skip_write = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_skip_write),
            MIR_new_reg_op(mt->ctx, map_ptr), MIR_new_int_op(mt->ctx, 0)));
    }

    // load data pointer from Map* + 16
    MIR_reg_t data_ptr = new_reg(mt, "dwptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));

    if (type_id == LMD_TYPE_FLOAT) {
        // value should be native double from transpile_expr for FLOAT-typed expressions
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (val_tid == LMD_TYPE_FLOAT) {
            val = transpile_expr(mt, value);
        } else if (val_tid == LMD_TYPE_INT || val_tid == LMD_TYPE_INT64) {
            // int → float promotion
            MIR_reg_t int_val = transpile_expr(mt, value);
            val = new_reg(mt, "i2d", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, val),
                MIR_new_reg_op(mt->ctx, int_val)));
        } else {
            // fallback: box then unbox to double
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, LMD_TYPE_FLOAT);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    if (type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_BOOL) {
        // store native int64 value
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (val_tid == LMD_TYPE_INT || val_tid == LMD_TYPE_INT64 || val_tid == LMD_TYPE_BOOL) {
            val = transpile_expr(mt, value);
        } else {
            // fallback: box then unbox
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, type_id);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    if (type_id == LMD_TYPE_STRING) {
        // store native String* pointer
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (val_tid == LMD_TYPE_STRING) {
            val = transpile_expr(mt, value);  // native String*
        } else {
            // fallback: box then unbox to String*
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, LMD_TYPE_STRING);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    // container and other pointer types: strip tag, store raw pointer
    // (runtime's map_field_store stores raw Container*/pointer, not tagged Items)
    MIR_reg_t val = transpile_box_item(mt, value);
    MIR_reg_t raw = emit_unbox_container(mt, val);  // strip tag → raw ptr (0 for null)
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
        MIR_new_reg_op(mt->ctx, raw)));
    if (!skip_null_guard) emit_label(mt, l_skip_write);
}

static MIR_reg_t transpile_member(MirTranspiler* mt, AstFieldNode* field_node) {
    // Use transpile_box_item for the object to ensure proper boxing.
    // This is critical for types like DTIME/INT64/DECIMAL where transpile_expr
    // returns raw unboxed values from transpile_primary, but variables may hold
    // values that were previously boxed via transpile_box_item. Using
    // transpile_box_item here ensures consistent boxing at the call site.
    MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);

    // For Path objects with known property fields, use item_attr(item, "key")
    // instead of fn_member(item, key_item). fn_member doesn't auto-load metadata,
    // but item_attr (used by C transpiler) does. This matches C transpiler behavior.
    TypeId obj_tid = get_effective_type(mt, field_node->object);
    if (obj_tid == LMD_TYPE_PATH && field_node->field->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)field_node->field;
        const char* k = ident->name->chars;
        bool is_property = (strcmp(k, "name") == 0 || strcmp(k, "is_dir") == 0 ||
                           strcmp(k, "is_file") == 0 || strcmp(k, "is_link") == 0 ||
                           strcmp(k, "size") == 0 || strcmp(k, "modified") == 0 ||
                           strcmp(k, "path") == 0 || strcmp(k, "extension") == 0 ||
                           strcmp(k, "scheme") == 0 || strcmp(k, "depth") == 0 ||
                           strcmp(k, "parent") == 0 || strcmp(k, "mode") == 0);
        if (is_property) {
            MIR_reg_t key_ptr = emit_load_string_literal(mt, k);
            return emit_call_2(mt, "item_attr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_P, MIR_new_reg_op(mt->ctx, key_ptr));
        }
        // Non-property field on path: use fn_member which extends the path
    }

    // ==================================================================
    // Phase 3: Direct map/object field access optimization
    // For typed maps/objects with fixed shape, bypass fn_member runtime call
    // and load field directly from packed data struct.
    // Use AST type (not get_effective_type) because parameters stored as
    // boxed ANY still have correct AST type from type annotations.
    // ==================================================================
    TypeId ast_obj_tid = field_node->object->type ? field_node->object->type->type_id : LMD_TYPE_ANY;
    if ((ast_obj_tid == LMD_TYPE_MAP || ast_obj_tid == LMD_TYPE_OBJECT) &&
        field_node->field->node_type == AST_NODE_IDENT &&
        field_node->object->type) {
        TypeMap* map_type = (TypeMap*)field_node->object->type;
        if (mir_has_fixed_shape(map_type)) {
            AstIdentNode* ident = (AstIdentNode*)field_node->field;
            ShapeEntry* se = mir_find_shape_field(map_type,
                ident->name->chars, ident->name->len);
            if (se && se->type && mir_is_direct_access_type(mir_resolve_field_type(se))) {
                log_debug("mir: direct field read: %.*s (type=%d offset=%lld)",
                    (int)ident->name->len, ident->name->chars,
                    mir_resolve_field_type(se), (long long)se->byte_offset);
                bool skip_null_guard = false; // typed variables can still hold null
                return emit_mir_direct_field_read(mt, boxed_obj, se, skip_null_guard);
            }
        }
    }

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

    MIR_reg_t result = emit_call_2(mt, "fn_member", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_field));

    // when the member expression has a resolved field type, unbox the fn_member
    // result so that transpile_expr returns a native value matching the type
    TypeId mem_tid = ((AstNode*)field_node)->type ? ((AstNode*)field_node)->type->type_id : LMD_TYPE_ANY;
    if (mem_tid == LMD_TYPE_INT || mem_tid == LMD_TYPE_FLOAT || mem_tid == LMD_TYPE_BOOL
        || mem_tid == LMD_TYPE_STRING || mem_tid == LMD_TYPE_INT64) {
        result = emit_unbox(mt, result, mem_tid);
    }
    return result;
}

static MIR_reg_t transpile_index(MirTranspiler* mt, AstFieldNode* field_node) {
    TypeId idx_tid = get_effective_type(mt, field_node->field);
    TypeId obj_tid = get_effective_type(mt, field_node->object);

    // P4-3.2: Guard — if the index is a TYPE expression (child query like arr[int]),
    // skip all fast paths and fall through to fn_index which handles type-based dispatch.
    AstNode* idx_node = field_node->field;
    while (idx_node && idx_node->node_type == AST_NODE_PRIMARY)
        idx_node = ((AstPrimaryNode*)idx_node)->expr;
    bool is_type_index = false;
    if (idx_node) {
        // Direct type node (int, string, float, etc.)
        if (idx_node->node_type == AST_NODE_TYPE ||
            idx_node->node_type == AST_NODE_ARRAY_TYPE ||
            idx_node->node_type == AST_NODE_LIST_TYPE ||
            idx_node->node_type == AST_NODE_MAP_TYPE ||
            idx_node->node_type == AST_NODE_ELMT_TYPE ||
            idx_node->node_type == AST_NODE_FUNC_TYPE ||
            idx_node->node_type == AST_NODE_BINARY_TYPE ||
            idx_node->node_type == AST_NODE_UNARY_TYPE ||
            idx_node->node_type == AST_NODE_CONTENT_TYPE) {
            is_type_index = true;
        }
        // Node whose expression type is LMD_TYPE_TYPE (type-valued expression)
        if (!is_type_index && idx_node->type && idx_node->type->type_id == LMD_TYPE_TYPE) {
            is_type_index = true;
        }
    }
    if (is_type_index) {
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
        MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
        return emit_call_2(mt, "fn_index", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
    }

    // ======================================================================
    // FAST PATH 1: Compile-time known ArrayInt (from fill() narrowing)
    // No runtime type check needed — we KNOW it's ArrayInt.
    // Returns NATIVE INT (not boxed), enabling native arithmetic downstream.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY_INT &&
        (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64)) {
        MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);  // tagged container Item
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);       // strip tag → raw ArrayInt*

        // Bounds check
        MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

        MIR_label_t l_ok = new_label(mt);
        MIR_label_t l_oob = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        MIR_reg_t result = new_reg(mt, "aidx", MIR_T_I64);

        MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, neg_check)));

        MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, ge_check)));

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

        // OOB: return 0 (safe default for int arrays)
        emit_label(mt, l_oob);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        // In bounds: load items[idx]
        emit_label(mt, l_ok);
        MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

        MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

        MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
            MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

        emit_label(mt, l_end);
        return result;  // NATIVE INT (not boxed)
    }

    // ======================================================================
    // FAST PATH 1a: Compile-time known ArrayFloat (from fill() narrowing)
    // No runtime type check needed — we KNOW it's ArrayFloat.
    // Returns NATIVE FLOAT (double), enabling native FP arithmetic downstream.
    // ArrayFloat stores raw doubles in items[], same struct layout as ArrayInt:
    //   offset 0: type_id (uint8_t), offset 8: double* items, offset 16: int64_t length
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY_FLOAT &&
        (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64)) {
        MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);  // tagged container Item
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);       // strip tag → raw ArrayFloat*

        // Bounds check
        MIR_reg_t arr_len = new_reg(mt, "aflen", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

        MIR_label_t l_ok = new_label(mt);
        MIR_label_t l_oob = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        MIR_reg_t result = new_reg(mt, "afidx", MIR_T_D);

        MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, neg_check)));

        MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, ge_check)));

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

        // OOB: return 0.0 (safe default for float arrays)
        emit_label(mt, l_oob);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_double_op(mt->ctx, 0.0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        // In bounds: load items[idx] as double
        emit_label(mt, l_ok);
        MIR_reg_t items_ptr = new_reg(mt, "fitms", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

        MIR_reg_t byte_off = new_reg(mt, "fboff", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

        MIR_reg_t elem_addr = new_reg(mt, "feadr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
            MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

        // Load as double (MIR_T_D) — ArrayFloat stores raw doubles
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_mem_op(mt->ctx, MIR_T_D, 0, elem_addr, 0, 1)));

        emit_label(mt, l_end);
        return result;  // NATIVE FLOAT (double, not boxed)
    }

    // ======================================================================
    // FAST PATH 1b: ARRAY type + INT index — compile-time typed inline read
    // The compile-time type is generic ARRAY, but when the AST records a
    // nested element type (e.g., nested=INT for [0,4,2,...]), we know the
    // runtime object is a typed array created by transpile_array.
    // Runtime type check needed because fn_array_set may convert ArrayInt
    // to generic Array in-place (e.g., arr[1] = 3.14 on an int array).
    // P4-3.2: nested_int now returns NATIVE INT (matching get_effective_type).
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY &&
        (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64)) {
        // Check if AST nested type suggests this was originally an int array
        Type* obj_type = field_node->object ? field_node->object->type : nullptr;
        bool nested_int = false;
        bool nested_bool = false;
        if (obj_type && obj_type->type_id == LMD_TYPE_ARRAY) {
            TypeArray* arr_type = (TypeArray*)obj_type;
            nested_int = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
            nested_bool = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL;
        }

        // P4-3.1: Check MirVarEntry elem_type (from fill() narrowing), unwrap PRIMARY
        if (!nested_bool) {
            AstNode* obj_unwrapped = field_node->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type == LMD_TYPE_BOOL) nested_bool = true;
            }
        }

        if (nested_int) {
            // P4-3.2: Likely ArrayInt — inline read with runtime type check.
            // Return type depends on whether elem_type is provably INT:
            //   safe_native_int=true  → NATIVE INT (matching get_effective_type=INT)
            //   safe_native_int=false → BOXED Item (AST nested can be wrong after mutation)
            bool safe_native_int = false;
            {
                AstNode* obj_unwrapped = field_node->object;
                while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                    obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
                if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                    AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                    char oname[128];
                    snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                    MirVarEntry* ov = find_var(mt, oname);
                    if (ov && ov->elem_type == LMD_TYPE_INT) safe_native_int = true;
                }
            }
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            // Read Container.type_id (uint8_t at offset 0) for runtime check
            MIR_reg_t rt_tid = new_reg(mt, "rttid", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_tid),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_aint = new_reg(mt, "isai", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
                MIR_new_reg_op(mt->ctx, rt_tid), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_INT)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_reg_t result = new_reg(mt, "aidx", MIR_T_I64);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_aint)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // == ArrayInt at runtime: inline items[idx] ==
            emit_label(mt, l_fast);
            {
                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                MIR_reg_t raw_val = new_reg(mt, "rval", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

                if (safe_native_int) {
                    // NATIVE return: raw int64 directly
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, raw_val)));
                } else {
                    // BOXED return: mask lower 56 bits + tag as INT Item
                    MIR_reg_t masked = new_reg(mt, "mskv", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
                        MIR_new_reg_op(mt->ctx, raw_val),
                        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
                    uint64_t INT_TAG = (uint64_t)LMD_TYPE_INT << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_int_op(mt->ctx, (int64_t)INT_TAG), MIR_new_reg_op(mt->ctx, masked)));
                }
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB
            emit_label(mt, l_oob);
            if (safe_native_int) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
            } else {
                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Not ArrayInt at runtime → item_at fallback
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
                MIR_reg_t slow_item = emit_call_2(mt, "item_at", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
                if (safe_native_int) {
                    MIR_reg_t slow_native = emit_unbox(mt, slow_item, LMD_TYPE_INT);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, slow_native)));
                } else {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, slow_item)));
                }
            }

            emit_label(mt, l_end);
            return result;  // NATIVE INT if safe_native_int, else BOXED Item
        } else if (nested_bool) {
            // ==============================================================
            // P4-3.1: Bool array — inline read with native bool result
            // Generic Array stores boxed Item elements (Item*). For bool arrays
            // from fill(n, true/false), each element is a tagged bool Item.
            // Runtime type check: Container.type_id must be LMD_TYPE_ARRAY
            // (fn_array_set keeps it as generic Array for bool values).
            // Returns NATIVE BOOL (0/1), enabling native AND/OR/NOT paths.
            // ==============================================================
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            // Runtime type check: Container.type_id == LMD_TYPE_ARRAY
            MIR_reg_t rt_tid = new_reg(mt, "rttid", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_tid),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_arr = new_reg(mt, "isarr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_arr),
                MIR_new_reg_op(mt->ctx, rt_tid), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_reg_t result = new_reg(mt, "bidx", MIR_T_I64);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_arr)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // == Generic Array inline read ==
            emit_label(mt, l_fast);
            {
                // Load length at offset 16
                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                // Bounds check
                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                // Load items pointer at offset 8
                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                // items[idx] — each Item is 8 bytes
                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                MIR_reg_t raw_val = new_reg(mt, "rval", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

                // Extract native bool: low bit of boxed bool Item
                // Bool Item: (LMD_TYPE_BOOL << 56) | value, value is 0 or 1
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, raw_val), MIR_new_int_op(mt->ctx, 1)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB → false
            emit_label(mt, l_oob);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Slow path: item_at → unbox bool
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
                MIR_reg_t slow_item = emit_call_2(mt, "item_at", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
                // Unbox: extract low bit from boxed bool Item
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, slow_item), MIR_new_int_op(mt->ctx, 1)));
            }

            emit_label(mt, l_end);
            return result;  // NATIVE BOOL
        } else {
            // Generic ARRAY — use item_at(boxed_obj, native_idx) to skip index dispatch
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
        }
    }

    // ======================================================================
    // FAST PATH 1c: ARRAY_INT + ANY index — unbox index then inline read
    // Object is known ArrayInt (from fill() narrowing), index type unknown.
    // Unbox the field with it2i() to get native int, then inline items[idx].
    // Returns NATIVE INT (AST type for ARRAY_INT[*] is always INT).
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY_INT && idx_tid == LMD_TYPE_ANY) {
        MIR_reg_t boxed_field = transpile_box_item(mt, field_node->field);
        MIR_reg_t idx_native = emit_unbox(mt, boxed_field, LMD_TYPE_INT);  // it2i
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

        MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

        MIR_label_t l_ok = new_label(mt);
        MIR_label_t l_oob = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        MIR_reg_t result = new_reg(mt, "aidx", MIR_T_I64);

        MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, neg_check)));

        MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, ge_check)));

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

        emit_label(mt, l_oob);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        emit_label(mt, l_ok);
        MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

        MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

        MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
            MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

        emit_label(mt, l_end);
        return result;  // NATIVE INT
    }

    // ======================================================================
    // FAST PATH 1d: ARRAY + ANY index — unbox index, typed dispatch
    // Object is generic ARRAY, index type unknown. Unbox field with it2i().
    // P4-3.2: nested=INT returns NATIVE INT only when elem_type proven safe;
    // otherwise returns BOXED Item (backward-compatible with pre-P4-3.2).
    // For nested=BOOL (P4-3.1): inline items[idx], extract native bool.
    // Otherwise: item_at(boxed_obj, idx). Returns BOXED Item.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY && idx_tid == LMD_TYPE_ANY) {
        Type* obj_type = field_node->object ? field_node->object->type : nullptr;
        bool nested_int = false;
        bool nested_bool = false;
        if (obj_type && obj_type->type_id == LMD_TYPE_ARRAY) {
            TypeArray* arr_type = (TypeArray*)obj_type;
            nested_int = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
            nested_bool = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL;
        }

        // P4-3.1: Check MirVarEntry elem_type (from fill() narrowing), unwrap PRIMARY
        if (!nested_bool) {
            AstNode* obj_unwrapped = field_node->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type == LMD_TYPE_BOOL) nested_bool = true;
            }
        }

        MIR_reg_t boxed_field = transpile_box_item(mt, field_node->field);
        MIR_reg_t idx_native = emit_unbox(mt, boxed_field, LMD_TYPE_INT);  // it2i

        if (nested_int) {
            // P4-3.2: Likely ArrayInt — inline read with runtime type check.
            // Return NATIVE INT only when elem_type proven (safe_native_int);
            // otherwise return BOXED Item (AST nested may be wrong after mutation).
            bool safe_native_int = false;
            {
                AstNode* obj_unwrapped = field_node->object;
                while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                    obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
                if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                    AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                    char oname[128];
                    snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                    MirVarEntry* ov = find_var(mt, oname);
                    if (ov && ov->elem_type == LMD_TYPE_INT) safe_native_int = true;
                }
            }
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            // Read Container.type_id for runtime check
            MIR_reg_t rt_tid = new_reg(mt, "rttid", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_tid),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_aint = new_reg(mt, "isai", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
                MIR_new_reg_op(mt->ctx, rt_tid), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_INT)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_reg_t result = new_reg(mt, "aidx", MIR_T_I64);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_aint)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // ArrayInt at runtime: inline items[idx]
            emit_label(mt, l_fast);
            {
                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                MIR_reg_t raw_val = new_reg(mt, "rval", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

                if (safe_native_int) {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, raw_val)));
                } else {
                    MIR_reg_t masked = new_reg(mt, "mskv", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
                        MIR_new_reg_op(mt->ctx, raw_val),
                        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
                    uint64_t INT_TAG = (uint64_t)LMD_TYPE_INT << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_int_op(mt->ctx, (int64_t)INT_TAG), MIR_new_reg_op(mt->ctx, masked)));
                }
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB
            emit_label(mt, l_oob);
            if (safe_native_int) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
            } else {
                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Not ArrayInt → item_at fallback
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
                MIR_reg_t slow_item = emit_call_2(mt, "item_at", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
                if (safe_native_int) {
                    MIR_reg_t slow_native = emit_unbox(mt, slow_item, LMD_TYPE_INT);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, slow_native)));
                } else {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                        MIR_new_reg_op(mt->ctx, slow_item)));
                }
            }

            emit_label(mt, l_end);
            return result;  // NATIVE INT if safe_native_int, else BOXED Item
        } else if (nested_bool) {
            // ==============================================================
            // P4-3.1: Bool array + ANY index — inline read, native bool
            // Unbox index (already done above), inline Array items[idx],
            // extract native bool. Runtime type check for safety.
            // ==============================================================
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            // Runtime type check: Container.type_id == LMD_TYPE_ARRAY
            MIR_reg_t rt_tid = new_reg(mt, "rttid", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_tid),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_arr = new_reg(mt, "isarr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_arr),
                MIR_new_reg_op(mt->ctx, rt_tid), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_reg_t result = new_reg(mt, "bidx", MIR_T_I64);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_arr)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // == Generic Array inline read ==
            emit_label(mt, l_fast);
            {
                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                MIR_reg_t raw_val = new_reg(mt, "rval", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

                // Extract native bool: low bit of boxed bool Item
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, raw_val), MIR_new_int_op(mt->ctx, 1)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB → false
            emit_label(mt, l_oob);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Slow path: item_at → unbox bool
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
                MIR_reg_t slow_item = emit_call_2(mt, "item_at", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, slow_item), MIR_new_int_op(mt->ctx, 1)));
            }

            emit_label(mt, l_end);
            return result;  // NATIVE BOOL
        } else {
            // Generic ARRAY, ANY index — item_at(boxed_obj, it2i(field))
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
        }
    }

    // ======================================================================
    // FAST PATH 2: Runtime type check for unknown containers with INT index
    // Object might be ArrayInt at runtime — check type tag.
    // Returns BOXED Item (type is unknown at compile time).
    // ======================================================================
    if (idx_tid == LMD_TYPE_INT && obj_tid == LMD_TYPE_ANY) {
        MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);

        MIR_reg_t obj_tag = new_reg(mt, "otag", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, obj_tag),
            MIR_new_reg_op(mt->ctx, boxed_obj), MIR_new_int_op(mt->ctx, 56)));

        MIR_reg_t is_aint = new_reg(mt, "isai", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
            MIR_new_reg_op(mt->ctx, obj_tag), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_INT)));

        MIR_label_t l_fast = new_label(mt);
        MIR_label_t l_slow = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        MIR_label_t l_oob = new_label(mt);

        MIR_reg_t result = new_reg(mt, "idx_r", MIR_T_I64);

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
            MIR_new_reg_op(mt->ctx, is_aint)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

        // == ArrayInt inline ==
        emit_label(mt, l_fast);
        MIR_reg_t arr_ptr = emit_unbox_container(mt, boxed_obj);

        MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

        MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, neg_check)));

        MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
            MIR_new_reg_op(mt->ctx, ge_check)));

        MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

        MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
            MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 3)));

        MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
            MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

        MIR_reg_t raw_val = new_reg(mt, "rval", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw_val),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1)));

        // Tag as INT Item for boxed result
        MIR_reg_t masked = new_reg(mt, "mskv", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
            MIR_new_reg_op(mt->ctx, raw_val),
            MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
        uint64_t INT_TAG = (uint64_t)LMD_TYPE_INT << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)INT_TAG), MIR_new_reg_op(mt->ctx, masked)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        // OOB → return null Item
        emit_label(mt, l_oob);
        {
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

        // Slow path: item_at (saves index-type dispatch vs fn_index)
        emit_label(mt, l_slow);
        {
            MIR_reg_t slow_result = emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, slow_result)));
        }

        emit_label(mt, l_end);
        return result;
    }

    // ======================================================================
    // FAST PATH 3: ANY + ANY with inner INDEX_EXPR detection (Level 3)
    // When object is ANY and field is also ANY, check if the field expression
    // is an INDEX_EXPR into a typed int array. If so, the field produces an
    // integer Item and we can use item_at(obj, it2i(field)) instead of the
    // expensive fn_index double dispatch. Mirrors C2MIR D1 Level 3.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ANY && idx_tid == LMD_TYPE_ANY) {
        bool field_known_int = false;
        AstNode* field_expr = field_node->field;
        // Unwrap PRIMARY wrapper
        if (field_expr && field_expr->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)field_expr;
            if (pri->expr) field_expr = pri->expr;
        }
        if (field_expr && field_expr->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* inner_idx = (AstFieldNode*)field_expr;
            // Check inner object's effective type (handles fill() narrowing)
            TypeId inner_obj_eff = get_effective_type(mt, inner_idx->object);
            if (inner_obj_eff == LMD_TYPE_ARRAY_INT || inner_obj_eff == LMD_TYPE_ARRAY_INT64) {
                field_known_int = true;
            } else if (inner_obj_eff == LMD_TYPE_ARRAY) {
                // Check AST nested type
                Type* inner_type = inner_idx->object ? inner_idx->object->type : nullptr;
                if (inner_type && inner_type->type_id == LMD_TYPE_ARRAY) {
                    TypeArray* arr_type = (TypeArray*)inner_type;
                    if (arr_type->nested && (arr_type->nested->type_id == LMD_TYPE_INT
                        || arr_type->nested->type_id == LMD_TYPE_INT64)) {
                        field_known_int = true;
                    }
                }
            }
        }
        if (field_known_int) {
            // Field produces an integer Item — use item_at(obj, it2i(field))
            MIR_reg_t boxed_field = transpile_box_item(mt, field_node->field);
            MIR_reg_t field_native = emit_unbox(mt, boxed_field, LMD_TYPE_INT);
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, field_native));
        }
    }

    // ======================================================================
    // DEFAULT: box both operands and call fn_index
    // ======================================================================
    MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
    MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);

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
                TypeId a1_tid = get_effective_type(mt, arg);
                MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);
                return emit_call_1(mt, "vmap_from_array", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            }
        }
        // ==== VMap: m.set(k, v) -> vmap_set(m, k, v) ====
        if (info->fn == SYSPROC_VMAP_SET) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);

            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a2 = emit_box(mt, a2, a2_tid);

            arg = arg->next;
            MIR_reg_t a3 = transpile_expr(mt, arg);
            TypeId a3_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a3 = emit_box(mt, a3, a3_tid);

            return emit_call_3(mt, "vmap_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
        }

        // ==== Native len() for typed collections/strings ====
        // When the argument type is known, dispatch to type-specific fn_len_* variants
        // that take native pointers and return int64_t directly (no boxing overhead).
        if (info->fn == SYSFUNC_LEN && arg_count == 1) {
            arg = call_node->argument;
            TypeId arg_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            if (arg_tid == LMD_TYPE_ARRAY) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_len_l", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (arg_tid == LMD_TYPE_ARRAY || arg_tid == LMD_TYPE_ARRAY_INT ||
                arg_tid == LMD_TYPE_ARRAY_INT64 || arg_tid == LMD_TYPE_ARRAY_FLOAT) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_len_a", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (arg_tid == LMD_TYPE_STRING || arg_tid == LMD_TYPE_SYMBOL) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_len_s", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (arg_tid == LMD_TYPE_ELEMENT) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_len_e", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            // Fallback: use generic fn_len(Item) for unknown types (handled below)
        }

        // ==== Bitwise functions: inline as native MIR instructions ====
        // band/bor/bxor → MIR_AND/MIR_OR/MIR_XOR (single instruction, no function call)
        // shl/shr → guarded: if (b >= 0 && b < 64) then MIR_LSH/MIR_RSH else 0
        // bnot → MIR_XOR with -1 (equivalent to ~a)
        if (info->fn == SYSFUNC_BAND || info->fn == SYSFUNC_BOR ||
            info->fn == SYSFUNC_BXOR || info->fn == SYSFUNC_SHL ||
            info->fn == SYSFUNC_SHR) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            if (a1_tid != LMD_TYPE_INT && a1_tid != LMD_TYPE_INT64) {
                a1 = emit_unbox(mt, a1, LMD_TYPE_INT);
            }
            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = get_effective_type(mt, arg);
            if (a2_tid != LMD_TYPE_INT && a2_tid != LMD_TYPE_INT64) {
                a2 = emit_unbox(mt, a2, LMD_TYPE_INT);
            }

            // band/bor/bxor: single MIR instruction, always safe
            MIR_insn_code_t mir_op = (MIR_insn_code_t)0;
            switch (info->fn) {
                case SYSFUNC_BAND: mir_op = MIR_AND; break;
                case SYSFUNC_BOR:  mir_op = MIR_OR;  break;
                case SYSFUNC_BXOR: mir_op = MIR_XOR; break;
                default: break;
            }
            if (mir_op) {
                MIR_reg_t result = new_reg(mt, "bw", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, mir_op,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, a1),
                    MIR_new_reg_op(mt->ctx, a2)));
                return result;
            }

            // shl/shr: guarded shift — if (b >= 0 && b < 64) then (a op b) else 0
            {
                MIR_insn_code_t shift_op = (info->fn == SYSFUNC_SHL) ? MIR_LSH : MIR_RSH;
                MIR_reg_t result = new_reg(mt, "shft", MIR_T_I64);
                MIR_label_t l_ok = new_label(mt);
                MIR_label_t l_zero = new_label(mt);
                MIR_label_t l_end = new_label(mt);

                // check b >= 0
                MIR_reg_t neg_chk = new_reg(mt, "sneg", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_chk),
                    MIR_new_reg_op(mt->ctx, a2), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_zero),
                    MIR_new_reg_op(mt->ctx, neg_chk)));

                // check b < 64
                MIR_reg_t ge_chk = new_reg(mt, "sge", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_chk),
                    MIR_new_reg_op(mt->ctx, a2), MIR_new_int_op(mt->ctx, 64)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_zero),
                    MIR_new_reg_op(mt->ctx, ge_chk)));

                // in range: do the shift
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

                emit_label(mt, l_zero);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

                emit_label(mt, l_ok);
                emit_insn(mt, MIR_new_insn(mt->ctx, shift_op,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, a1),
                    MIR_new_reg_op(mt->ctx, a2)));

                emit_label(mt, l_end);
                return result;
            }
        }
        if (info->fn == SYSFUNC_BNOT) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            if (a1_tid != LMD_TYPE_INT && a1_tid != LMD_TYPE_INT64) {
                a1 = emit_unbox(mt, a1, LMD_TYPE_INT);
            }
            // ~a == a XOR -1
            MIR_reg_t result = new_reg(mt, "bnot", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_XOR,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, a1),
                MIR_new_int_op(mt->ctx, (int64_t)-1)));
            return result;
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

        // For C_RET_RETITEM functions (returning 16-byte struct), use MIR wrapper
        // that returns Item. RetItem has ABI issues with MIR's i64 return type.
        if (info->c_ret_type == C_RET_RETITEM) {
            char mir_name[140];
            snprintf(mir_name, sizeof(mir_name), "%s_mir", sys_fn_name);
            memcpy(sys_fn_name, mir_name, sizeof(sys_fn_name));
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
        case SYSFUNC_ORD:
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

        // Helper lambda: post-process DateTime returns via push_k so variables store
        // DateTime* pointers (consistent with literals from emit_load_const).
        // DateTime C functions return raw uint64_t values, not pointers.
        #define POST_PROCESS_DTIME(result) \
            if (c_ret_tid == LMD_TYPE_DTIME) { result = emit_box_dtime_value(mt, result); }

        // Helper: when a sys func returns a boxed Item (c_ret_tid=ANY) but the
        // call expression has a specific native type, unbox to native format.
        // This ensures consistency with local/dynamic calls which also unbox
        // Items to native types (FLOAT→double, INT→int64, BOOL→int64, INT64→int64).
        TypeId call_expr_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
        #define POST_PROCESS_UNBOX(result) \
            if (c_ret_tid == LMD_TYPE_ANY && \
                (call_expr_tid == LMD_TYPE_FLOAT || call_expr_tid == LMD_TYPE_INT || \
                 call_expr_tid == LMD_TYPE_BOOL || call_expr_tid == LMD_TYPE_INT64)) { \
                result = emit_unbox(mt, result, call_expr_tid); \
            }

        // For 0-arg functions like datetime(), date() etc
        if (arg_count == 0) {
            MIR_reg_t result = emit_call_0(mt, sys_fn_name, mir_ret_type);
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // For 1-arg system functions
        if (arg_count == 1) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
            MIR_reg_t result = emit_call_1(mt, sys_fn_name, mir_ret_type, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // For 2-arg system functions
        if (arg_count == 2) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);

            MIR_reg_t result = emit_call_2(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // 3-arg system functions
        if (arg_count == 3) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a3 = transpile_box_item(mt, arg);

            MIR_reg_t result = emit_call_3(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // Fallback for more args: use vararg call
        {
            arg = call_node->argument;
            MIR_op_t arg_ops[16];
            int ai = 0;
            while (arg && ai < 16) {
                MIR_reg_t boxed = transpile_box_item(mt, arg);
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
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
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

        // Handle imported function calls (from another module compiled via C transpiler)
        if (ident->entry && ident->entry->import && entry_node &&
            (entry_node->node_type == AST_NODE_FUNC ||
             entry_node->node_type == AST_NODE_FUNC_EXPR ||
             entry_node->node_type == AST_NODE_PROC)) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Determine the import function name:
            // Use boxed wrapper (_b) for typed-param functions, direct for untyped
            StrBuf* fn_import_name = strbuf_new_cap(64);
            bool use_wrapper = (entry_node->node_type != AST_NODE_PROC && needs_fn_call_wrapper(fn_node));
            if (use_wrapper) {
                write_fn_name_ex(fn_import_name, fn_node, NULL, "_b");
            } else {
                write_fn_name(fn_import_name, fn_node, NULL);
            }
            log_debug("mir: imported function call '%s' (wrapper=%d)", fn_import_name->str, use_wrapper);

            // Count and box arguments
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) { arg_count++; arg = arg->next; }

            // Build resolved_args with named arg reordering if needed
            AstNode* resolved_args[16] = {0};
            bool has_named_args = false;
            arg = call_node->argument;
            while (arg) {
                if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
                arg = arg->next;
            }

            if (has_named_args) {
                int positional_idx = 0;
                arg = call_node->argument;
                while (arg) {
                    if (arg->node_type == AST_NODE_NAMED_ARG) {
                        AstNamedNode* named_arg = (AstNamedNode*)arg;
                        int param_idx = 0;
                        AstNamedNode* param = fn_node->param;
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
                        while (positional_idx < 16 && resolved_args[positional_idx]) positional_idx++;
                        if (positional_idx < 16) resolved_args[positional_idx++] = arg;
                    }
                    arg = arg->next;
                }
            } else {
                arg = call_node->argument;
                for (int i = 0; i < arg_count && i < 16; i++) {
                    resolved_args[i] = arg;
                    arg = arg->next;
                }
            }

            // Get expected param count
            int expected_params = arg_count;
            TypeFunc* call_fn_type = fn_node->type ? (TypeFunc*)fn_node->type : nullptr;
            if (call_fn_type && call_fn_type->type_id == LMD_TYPE_FUNC) {
                expected_params = call_fn_type->param_count;
            }

            // Box all args to Items
            MIR_op_t arg_ops[16];
            MIR_var_t arg_vars[16];
            int ai = 0;
            for (int i = 0; i < expected_params && i < 16; i++) {
                if (resolved_args[i]) {
                    MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    MIR_reg_t null_reg = new_reg(mt, "pad", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, null_reg),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, null_reg);
                }
                arg_vars[i] = {MIR_T_I64, "p", 0};
            }
            ai = expected_params;

            // Handle variadic args
            bool call_is_variadic = call_fn_type && call_fn_type->is_variadic;
            if (call_is_variadic && ai < 16) {
                int extra_count = arg_count - expected_params;
                if (extra_count < 0) extra_count = 0;
                MIR_reg_t vargs_reg;
                if (extra_count == 0) {
                    vargs_reg = new_reg(mt, "vargs", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vargs_reg),
                        MIR_new_int_op(mt->ctx, 0)));
                } else {
                    vargs_reg = emit_call_0(mt, "list", MIR_T_I64);
                    for (int i = expected_params; i < arg_count && i < 16; i++) {
                        if (resolved_args[i]) {
                            MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                            emit_call_void_2(mt, "list_push",
                                MIR_T_P, MIR_new_reg_op(mt->ctx, vargs_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                        }
                    }
                }
                arg_ops[ai] = MIR_new_reg_op(mt->ctx, vargs_reg);
                arg_vars[ai] = {MIR_T_P, "va", 0};
                ai++;
            }

            // Create proto and import for the cross-module call
            char proto_name[160];
            snprintf(proto_name, sizeof(proto_name), "%s_ip%d", fn_import_name->str, mt->label_counter++);
            MIR_type_t res_types[1] = { MIR_T_I64 };
            MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, res_types, ai, arg_vars);
            MIR_item_t imp_item = MIR_new_import(mt->ctx, fn_import_name->str);

            int nops = 3 + ai;
            MIR_op_t ops[19];
            ops[0] = MIR_new_ref_op(mt->ctx, proto);
            ops[1] = MIR_new_ref_op(mt->ctx, imp_item);
            MIR_reg_t result = new_reg(mt, "impcall", MIR_T_I64);
            ops[2] = MIR_new_reg_op(mt->ctx, result);
            for (int i = 0; i < ai; i++) ops[3 + i] = arg_ops[i];

            emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));

            // Import calls return boxed Items. Unbox to native type to match
            // local/system call behavior, so callers can re-box consistently.
            TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
            if (call_tid == LMD_TYPE_FLOAT || call_tid == LMD_TYPE_INT || call_tid == LMD_TYPE_BOOL) {
                result = emit_unbox(mt, result, call_tid);
            }

            strbuf_free(fn_import_name);
            return result;
        }

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

        // Fallback: try raw name lookup ONLY when entry is NULL (unresolved) or mangled name not found
        // Do NOT use raw name fallback when entry_node exists but is not a function —
        // that means it's a variable (e.g. "let adder = ...") which should go through dynamic call
        char raw_name[128];
        if (!local_func && !entry_node) {
            snprintf(raw_name, sizeof(raw_name), "%.*s", (int)ident->name->len, ident->name->chars);
            local_func = find_local_func(mt, raw_name);
            if (local_func) fn_mangled = raw_name;
        }

        if (fn_mangled && (local_func || entry_node)) {

            // ======== TCO interception ========
            // If this is a tail-recursive call to the current TCO function,
            // transform into: evaluate args → assign to params → goto tco_label
            if (mt->tco_func && mt->in_tail_position && is_recursive_call(call_node, mt->tco_func)) {
                log_debug("mir: TCO tail call to '%.*s' — converting to goto",
                    (int)mt->tco_func->name->len, mt->tco_func->name->chars);

                // Arguments are NOT in tail position
                bool saved_tail = mt->in_tail_position;
                mt->in_tail_position = false;

                // Phase 1: Evaluate all arguments into temporaries
                // This prevents aliasing when args reference parameters being overwritten (e.g., f(b, a))
                AstNode* arg = call_node->argument;
                AstNamedNode* param = mt->tco_func->param;
                MIR_reg_t temps[16];
                int arg_idx = 0;
                while (arg && param && arg_idx < 16) {
                    MIR_reg_t val = transpile_expr(mt, arg);
                    TypeId val_tid = get_effective_type(mt, arg);

                    // Determine the parameter's expected type (matching transpile_func_def logic)
                    TypeId param_tid = param->type ? param->type->type_id : LMD_TYPE_ANY;
                    if (param_tid == LMD_TYPE_ANY) {
                        char pname[64];
                        snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);
                        MirVarEntry* pvar = find_var(mt, pname);
                        if (pvar) param_tid = pvar->type_id;
                    }

                    // Convert to match parameter's representation
                    if (param_tid == LMD_TYPE_INT || param_tid == LMD_TYPE_FLOAT || param_tid == LMD_TYPE_BOOL) {
                        if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                            val = emit_unbox(mt, val, param_tid);
                        } else if (val_tid != param_tid) {
                            MIR_reg_t boxed = emit_box(mt, val, val_tid);
                            val = emit_unbox(mt, boxed, param_tid);
                        }
                    } else {
                        val = emit_box(mt, val, val_tid);
                    }

                    temps[arg_idx] = val;
                    arg_idx++;
                    arg = arg->next;
                    param = (AstNamedNode*)((AstNode*)param)->next;
                }

                // Phase 2: Assign temporaries to parameter registers
                param = mt->tco_func->param;
                for (int i = 0; i < arg_idx; i++) {
                    if (!param) break;
                    char pname[64];
                    snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);
                    MirVarEntry* pvar = find_var(mt, pname);
                    if (pvar) {
                        if (pvar->mir_type == MIR_T_D) {
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                MIR_new_reg_op(mt->ctx, pvar->reg),
                                MIR_new_reg_op(mt->ctx, temps[i])));
                        } else {
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, pvar->reg),
                                MIR_new_reg_op(mt->ctx, temps[i])));
                        }
                    }
                    param = (AstNamedNode*)((AstNode*)param)->next;
                }

                mt->in_tail_position = saved_tail;

                // Jump back to function start
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->tco_label)));
                mt->block_returned = true;

                // Return a dummy register (unreachable code, but MIR needs a value)
                MIR_reg_t dummy = new_reg(mt, "tco_dummy", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, dummy),
                    MIR_new_int_op(mt->ctx, 0)));
                if (name_buf) strbuf_free(name_buf);
                return dummy;
            }

            // Get function definition for param info (defaults, named args)
            AstFuncNode* fn_def = NULL;
            if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
                entry_node->node_type == AST_NODE_FUNC_EXPR ||
                entry_node->node_type == AST_NODE_PROC)) {
                fn_def = (AstFuncNode*)entry_node;
            }

            // Get expected param count and variadic info
            int expected_params = 0;
            TypeFunc* call_fn_type = NULL;
            if (fn_def && fn_def->type && fn_def->type->type_id == LMD_TYPE_FUNC) {
                call_fn_type = (TypeFunc*)fn_def->type;
                expected_params = call_fn_type->param_count;
            }
            bool call_is_variadic = call_fn_type && call_fn_type->is_variadic;

            // Check if any arguments are named
            bool has_named_args = false;
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) {
                if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
                arg_count++;
                arg = arg->next;
            }
            // Only bump expected_params to arg_count when we don't know the function signature
            // and the function is NOT variadic (variadic funcs collect extras into vargs)
            if (expected_params == 0 && !call_is_variadic) expected_params = arg_count;

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

            // Phase 4: Check if target function has a native version for optimized calling
            NativeFuncInfo* call_nfi = fn_mangled ? find_native_func_info(mt, fn_mangled) : nullptr;
            bool native_call = (call_nfi && call_nfi->has_native && local_func);

            // Emit args in parameter order, filling defaults for missing slots
            MIR_op_t arg_ops[16];
            MIR_var_t arg_vars[16];
            int ai = 0;
            AstNamedNode* param_iter = fn_def ? fn_def->param : NULL;
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            for (int i = 0; i < expected_params && i < 16; i++) {
                if (native_call && i < call_nfi->param_count &&
                    mir_is_native_param_type(call_nfi->param_types[i])) {
                    // Phase 4: Native param — pass native value directly (skip boxing)
                    TypeId param_tid = call_nfi->param_types[i];
                    if (resolved_args[i]) {
                        MIR_reg_t val = transpile_expr(mt, resolved_args[i]);
                        TypeId val_tid = get_effective_type(mt, resolved_args[i]);
                        if (val_tid == param_tid) {
                            // Direct native match — pass through
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                        } else if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                            // Boxed Item → unbox to expected native type
                            MIR_reg_t unboxed = emit_unbox(mt, val, param_tid);
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                        } else {
                            // Type mismatch: box → unbox (handles int→float etc.)
                            MIR_reg_t boxed = emit_box(mt, val, val_tid);
                            MIR_reg_t unboxed = emit_unbox(mt, boxed, param_tid);
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                        }
                    } else {
                        // Default value for native param
                        TypeParam* tp = param_iter ? (TypeParam*)((AstNode*)param_iter)->type : NULL;
                        if (tp && tp->default_value) {
                            MIR_reg_t val = transpile_expr(mt, tp->default_value);
                            TypeId val_tid = get_effective_type(mt, tp->default_value);
                            if (val_tid == param_tid) {
                                arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                            } else {
                                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                                MIR_reg_t unboxed = emit_unbox(mt, boxed, param_tid);
                                arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                            }
                        } else {
                            // No value: pass 0 as native default
                            MIR_reg_t zero = new_reg(mt, "npad", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, zero),
                                MIR_new_int_op(mt->ctx, 0)));
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, zero);
                        }
                    }
                    arg_vars[i] = {call_nfi->param_mir[i], "p", 0};
                } else {
                    // Non-native param: standard boxed Item ABI
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
                }
                if (param_iter) param_iter = (AstNamedNode*)((AstNode*)param_iter)->next;
            }
            ai = expected_params;

            // Handle variadic arguments: collect extra args into a List*
            if (call_is_variadic && ai < 16) {
                int extra_count = arg_count - expected_params;
                if (extra_count < 0) extra_count = 0;

                MIR_reg_t vargs_reg;
                if (extra_count == 0) {
                    // No variadic args: pass null pointer
                    vargs_reg = new_reg(mt, "vargs", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vargs_reg),
                        MIR_new_int_op(mt->ctx, 0)));
                } else {
                    // Create list and push extra args
                    vargs_reg = emit_call_0(mt, "list", MIR_T_I64);
                    for (int i = expected_params; i < arg_count && i < 16; i++) {
                        if (resolved_args[i]) {
                            MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                            emit_call_void_2(mt, "list_push",
                                MIR_T_P, MIR_new_reg_op(mt->ctx, vargs_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                        }
                    }
                }
                arg_ops[ai] = MIR_new_reg_op(mt->ctx, vargs_reg);
                arg_vars[ai] = {MIR_T_P, "va", 0};
                ai++;
            }

            // P4-3.3: Return type — use native type when calling native version
            bool call_native_return = (native_call && call_nfi->return_type != LMD_TYPE_ANY);
            MIR_type_t ret_type = call_native_return ? call_nfi->return_mir : MIR_T_I64;

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

            // P4-3.3: Post-call type handling for return values
            TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
            if (call_native_return) {
                // Native function returns native value directly
                if (call_tid == call_nfi->return_type) {
                    // Perfect match — no conversion needed
                } else if (call_tid == LMD_TYPE_FLOAT || call_tid == LMD_TYPE_INT || call_tid == LMD_TYPE_BOOL) {
                    // Caller expects different native type — box then unbox
                    MIR_reg_t boxed = emit_box(mt, result, call_nfi->return_type);
                    result = emit_unbox(mt, boxed, call_tid);
                } else {
                    // Caller expects boxed Item — box the native return
                    result = emit_box(mt, result, call_nfi->return_type);
                }
            } else {
                // Standard boxed return — unbox if caller expects native type
                if (call_tid == LMD_TYPE_FLOAT || call_tid == LMD_TYPE_INT || call_tid == LMD_TYPE_BOOL) {
                    result = emit_unbox(mt, result, call_tid);
                }
            }

            if (name_buf) strbuf_free(name_buf);
            return result;
        }
    }

    // Dynamic call via fn_call
    log_debug("mir: dynamic call - using fn_call");
    MIR_reg_t fn_val = transpile_expr(mt, call_node->function);
    TypeId fn_tid = get_effective_type(mt, call_node->function);
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
    TypeId left_tid = get_effective_type(mt, pipe_node->left);
    MIR_reg_t boxed_left = emit_box(mt, left, left_tid);

    bool uses_current_item = has_current_item_ref(pipe_node->right);

    // Simple aggregate pipe without ~ : data | func -> func(data)
    if (!uses_current_item && pipe_node->op == OPERATOR_PIPE) {
        // Check for pipe call injection: data | func(args) -> func(data, args)
        // This avoids generating fn_name1 for overloaded sys functions that only have fn_name2
        AstNode* right_node = pipe_node->right;
        if (right_node && right_node->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)right_node;
            if (pri->expr) right_node = pri->expr;
        }
        if (right_node && right_node->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call_node = (AstCallNode*)right_node;
            AstNode* fn_expr = call_node->function;
            if (fn_expr && fn_expr->node_type == AST_NODE_SYS_FUNC) {
                AstSysFuncNode* sys = (AstSysFuncNode*)fn_expr;
                SysFuncInfo* info = sys->fn_info;
                if (info) {
                    // build the correct function name using the fn_info (already resolved to N+1 version)
                    char sys_fn_name[128];
                    if (info->is_overloaded) {
                        snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s%d",
                            info->is_proc ? "pn_" : "fn_", info->name, info->arg_count);
                    } else {
                        snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s",
                            info->is_proc ? "pn_" : "fn_", info->name);
                    }

                    // count call arguments
                    AstNode* arg = call_node->argument;
                    int arg_count = 0;
                    while (arg) { arg_count++; arg = arg->next; }

                    // emit call with left injected as first argument
                    if (arg_count == 0) {
                        return emit_call_1(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
                    } else if (arg_count == 1) {
                        arg = call_node->argument;
                        MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                        return emit_call_2(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
                    } else if (arg_count == 2) {
                        arg = call_node->argument;
                        MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                        arg = arg->next;
                        MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);
                        return emit_call_3(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
                    }
                    // fall through for more args — use fn_pipe_call
                }
            }
        }

        MIR_reg_t right = transpile_expr(mt, pipe_node->right);
        TypeId right_tid = get_effective_type(mt, pipe_node->right);
        MIR_reg_t boxed_right = emit_box(mt, right, right_tid);
        return emit_call_2(mt, "fn_pipe_call", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Iterating pipe (uses ~ or WHERE/THAT) - generate inline loop
    // Create result array
    MIR_reg_t result_arr = emit_call_0(mt, "array", MIR_T_P);

    // Check if collection is a MAP or VMAP (maps need key-based iteration)
    MIR_reg_t type_id_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));

    MIR_reg_t is_map = new_reg(mt, "is_map", MIR_T_I64);
    MIR_reg_t is_map_t2 = new_reg(mt, "is_map_t2", MIR_T_I64);
    MIR_reg_t is_vmap_t2 = new_reg(mt, "is_vmap_t2", MIR_T_I64);
    MIR_reg_t is_obj_t2 = new_reg(mt, "is_obj_t2", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_vmap_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_VMAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_obj_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_OBJECT)));
    MIR_reg_t is_map_or_vmap2 = new_reg(mt, "is_map_or_vmap2", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map_or_vmap2),
        MIR_new_reg_op(mt->ctx, is_map_t2), MIR_new_reg_op(mt->ctx, is_vmap_t2)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map),
        MIR_new_reg_op(mt->ctx, is_map_or_vmap2), MIR_new_reg_op(mt->ctx, is_obj_t2)));

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
    // Use get_effective_type to account for expressions where AST type (e.g. BOOL
    // from chained comparison transformation) doesn't match runtime behavior
    // (e.g. boxed AND returning Item when ~ has type ANY)
    TypeId right_tid = get_effective_type(mt, (AstNode*)pipe_node->right);
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
            truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right)));
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
        TypeId val_tid = get_effective_type(mt, raise_node->value);
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
    // TCO: return value is in tail position — if this is a recursive call,
    // it can be converted to a goto jump instead of a function call.
    bool saved_tail = mt->in_tail_position;
    if (mt->tco_func) {
        mt->in_tail_position = true;
    }

    // P4-3.4: Check if current function has native return type
    TypeId native_ret = mt->native_return_tid;

    // Helper: emit restore_vargs before returning from variadic function
    auto emit_vargs_restore = [&]() {
        if (mt->in_variadic_body) {
            MIR_reg_t saved_vargs = MIR_reg(mt->ctx, "_saved_vargs", mt->current_func);
            emit_call_void_1(mt, "restore_vargs", MIR_T_P, MIR_new_reg_op(mt->ctx, saved_vargs));
        }
    };

    if (ret_node->value) {
        MIR_reg_t val = transpile_expr(mt, ret_node->value);

        // If TCO converted the inner call to a goto, block_returned is already set.
        // Skip the boxing/ret — it's dead code and would emit after a JMP.
        if (mt->block_returned) {
            mt->in_tail_position = saved_tail;
            return val;
        }

        // Use get_effective_type instead of AST type to match the actual
        // register type returned by transpile_expr. AST may say FLOAT but
        // the variable is stored as ANY (boxed Item / I64) when params are
        // untyped, causing emit_box_float to receive an I64 register → MIR
        // type validation error "Got 'int', expected 'double'".
        TypeId val_tid = get_effective_type(mt, ret_node->value);

        if (native_ret != LMD_TYPE_ANY) {
            // P4-3.4: Native return — emit unboxed value matching function's return type
            MIR_reg_t native_val;
            if (val_tid == native_ret) {
                // Perfect match — use value directly
                native_val = val;
            } else if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                // Boxed Item → unbox to native type
                native_val = emit_unbox(mt, val, native_ret);
            } else {
                // Different native type — box then unbox
                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                native_val = emit_unbox(mt, boxed, native_ret);
            }
            emit_vargs_restore();
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_val)));
        } else {
            // Standard: box and return
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_vargs_restore();
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed)));
        }
    } else {
        // Return with no value
        emit_vargs_restore();
        if (native_ret == LMD_TYPE_FLOAT) {
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_double_op(mt->ctx, 0.0)));
        } else if (native_ret != LMD_TYPE_ANY) {
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, 0)));
        } else {
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        }
    }

    mt->in_tail_position = saved_tail;

    MIR_type_t dummy_type = (native_ret == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
    MIR_reg_t r = new_reg(mt, "ret_dummy", dummy_type);
    if (dummy_type == MIR_T_D) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_double_op(mt->ctx, 0.0)));
    } else {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
    }
    mt->block_returned = true;
    return r;
}

// ============================================================================
// Assignment statement (procedural)
// ============================================================================

static MIR_reg_t transpile_assign_stam(MirTranspiler* mt, AstAssignStamNode* assign) {
    MIR_reg_t val = transpile_expr(mt, assign->value);
    TypeId val_tid = get_effective_type(mt, assign->value);

    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)assign->target->len, assign->target->chars);

    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        TypeId var_tid = var->type_id;

        // Check if the new value type matches the variable's tracked type.
        // INT and INT64 are both raw int64 in MIR registers (MIR_T_I64) —
        // treat them as compatible for direct move. This prevents re-boxing
        // when e.g. len() (INT64) result participates in INT arithmetic.
        bool same_int_family = (var_tid == LMD_TYPE_INT || var_tid == LMD_TYPE_INT64) &&
                               (val_tid == LMD_TYPE_INT || val_tid == LMD_TYPE_INT64);
        if (var_tid == val_tid || same_int_family ||
            (var_tid == LMD_TYPE_ANY && val_tid == LMD_TYPE_ANY)) {
            // Same type (or INT/INT64 compatible): direct move
            if (var->mir_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            }
        } else if (var_tid == LMD_TYPE_ANY && val_tid != LMD_TYPE_ANY) {
            // Variable is boxed (ANY), value is typed: box the value and store
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (var_tid == LMD_TYPE_FLOAT && val_tid == LMD_TYPE_INT) {
            // var is float, assigning int: convert int -> float
            MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, fval)));
        } else if (var_tid == LMD_TYPE_INT && val_tid == LMD_TYPE_FLOAT) {
            // var is int, assigning float value.
            // Inside loops: MIR registers are fixed-type; the while condition
            // code was already emitted for INT, so we must truncate to preserve
            // type consistency. Outside loops: safe to widen by boxing to ANY.
            if (mt->loop_depth > 0) {
                MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                    MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, ival)));
            } else {
                // Outside loops: box and widen to ANY to preserve float value
                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->type_id = LMD_TYPE_ANY;
                var->mir_type = MIR_T_I64;
            }
        } else if (val_tid == LMD_TYPE_ANY &&
                   (var_tid == LMD_TYPE_INT || var_tid == LMD_TYPE_INT64 ||
                    var_tid == LMD_TYPE_FLOAT || var_tid == LMD_TYPE_BOOL)) {
            // Value is boxed (e.g., from IDIV/MOD runtime call) but variable
            // is native. Unbox to maintain type consistency — critical for loops
            // where the condition code is emitted once with native types.
            // INT64 included: len() and similar functions return raw int64 values
            // stored in INT64 variables; boxed fallback results must be unboxed.
            // Error items (div-by-zero) get silently converted to 0/0.0/false.
            MIR_reg_t unboxed = emit_unbox(mt, val, var_tid);
            if (var->mir_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, unboxed)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, unboxed)));
            }
            // Variable stays at its native type
        } else {
            // Type mismatch (e.g. null->int, int->string): box the value and
            // switch to ANY type so the variable always holds a valid boxed Item
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            if (var->mir_type == MIR_T_D) {
                // Variable was float, needs to change to i64 for boxed Item
                // Modify in-place to persist through scope pop
                var->reg = boxed;
                var->mir_type = MIR_T_I64;
                var->type_id = LMD_TYPE_ANY;
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->type_id = LMD_TYPE_ANY;
                var->mir_type = MIR_T_I64;
            }
        }

        // Write-back for captured variables: if this variable came from a
        // closure env, store the updated value back so subsequent calls
        // to the closure see the mutations (persistent mutable state).
        if (var->env_offset >= 0 && mt->env_reg) {
            // Box the current value to store back as Item
            MIR_reg_t boxed_wb = emit_box(mt, var->reg, var->type_id);
            // Store: *(env_ptr + env_offset) = boxed_wb
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, var->env_offset, mt->env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, boxed_wb)));
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
    if (!node) {
        MIR_reg_t null_r = new_reg(mt, "null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, null_r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return null_r;
    }

    // ASSIGN_STAM nodes have NULL type but return the expression value.
    // After assignment, look up the variable's tracked type to box correctly.
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        transpile_expr(mt, node); // execute the assignment (side effect)
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)assign->target->len, assign->target->chars);
        MirVarEntry* var = find_var(mt, name_buf);
        if (var) {
            return emit_box(mt, var->reg, var->type_id);
        }
        // Fallback: return null Item
        MIR_reg_t null_r = new_reg(mt, "null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, null_r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return null_r;
    }

    if (!node->type) {
        log_debug("mir: box_item: node_type=%d has NULL type, skipping boxing",
            node->node_type);
        MIR_reg_t val = transpile_expr(mt, node);
        return val;
    }

    // Use effective type, which accounts for variables stored differently
    // than their AST type (e.g. optional params stored as boxed ANY)
    TypeId tid = get_effective_type(mt, node);
    log_debug("mir: box_item: node_type=%d, type_id=%d, is_literal=%d",
        node->node_type, tid, node->type->is_literal);

    // PRIMARY nodes are transparent wrappers (parenthesized expressions).
    // Always delegate to the inner expression for correct type-aware boxing.
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) {
            return transpile_box_item(mt, pri->expr);
        }
    }

    // For literals of constant types (string, symbol, etc), use const boxing.
    // Only apply to PRIMARY nodes which are actual literals - variables (IDENT)
    // may have is_literal=true on their type from type inference propagation
    // but should NOT load from the const pool.
    if (node->type->is_literal && node->node_type == AST_NODE_PRIMARY) {
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

    // If the expression already emitted a return (e.g. RETURN_STAM in a proc),
    // the val is a dummy register and any further boxing would be dead code.
    // Skip boxing to avoid type mismatches (e.g. trying to box an I64 dummy as FLOAT).
    if (mt->block_returned) return val;

    // For BINARY/UNARY nodes: check if the result is already a boxed Item
    // from the runtime fallback path (not native handling).
    // transpile_binary returns NATIVE values only for specific type+op combos;
    // everything else goes through boxed runtime and returns an already-boxed Item.
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)node;
        TypeId lt = get_effective_type(mt, bi->left);
        TypeId rt = get_effective_type(mt, bi->right);
        bool both_int = (lt == LMD_TYPE_INT) && (rt == LMD_TYPE_INT);
        bool both_float = (lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_FLOAT);
        bool int_float = (lt == LMD_TYPE_INT && rt == LMD_TYPE_FLOAT) ||
                         (lt == LMD_TYPE_FLOAT && rt == LMD_TYPE_INT);
        bool both_bool = (lt == LMD_TYPE_BOOL) && (rt == LMD_TYPE_BOOL);
        bool both_string = (lt == LMD_TYPE_STRING) && (rt == LMD_TYPE_STRING);
        bool left_int64 = (lt == LMD_TYPE_INT64);
        bool right_int64 = (rt == LMD_TYPE_INT64);
        bool both_int64 = left_int64 && right_int64;
        bool int_int64 = ((lt == LMD_TYPE_INT) && right_int64) || (left_int64 && (rt == LMD_TYPE_INT));
        int op = bi->op;

        // Enumerate ALL cases where transpile_binary returns native values:

        // 1. Comparisons (EQ-GE) always return native bool (both native MIR and fn_eq/fn_lt/etc.)
        bool is_cmp = (op >= OPERATOR_EQ && op <= OPERATOR_GE);
        if (is_cmp) {
            return emit_box_bool(mt, val);
        }

        // 1b. IS, IS_NAN, and IN also return native Bool from fn_is/fn_in/fn_is_nan
        if (op == OPERATOR_IS || op == OPERATOR_IS_NAN || op == OPERATOR_IN) {
            return emit_box_bool(mt, val);
        }

        // 2. AND/OR with both_bool → native bool
        if ((op == OPERATOR_AND || op == OPERATOR_OR) && both_bool) {
            return emit_box_bool(mt, val);
        }

        // 3. both_int arithmetic: ADD,SUB,MUL,IDIV,MOD → native int; DIV → native float
        //    POW falls through to boxed runtime (AST type is ANY)
        if (both_int) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_IDIV: case OPERATOR_MOD:
                return emit_box_int(mt, val);
            case OPERATOR_DIV:
                return emit_box_float(mt, val);
            default:
                return val; // POW, JOIN, etc. → boxed fallback
            }
        }

        // 3b. INT64 binary results: all INT64 ops go through the boxed fallback
        //     (no native INT64 handling), so results are already boxed Items.
        //     Comparisons already handled by is_cmp above.
        bool left_int64_b = (lt == LMD_TYPE_INT64);
        bool right_int64_b = (rt == LMD_TYPE_INT64);
        bool has_int64_b = left_int64_b || right_int64_b;
        if (has_int64_b) {
            return val; // already a boxed Item from the generic fallback
        }

        // 4. both_float: ADD,SUB,MUL,DIV,MOD → native float
        //    IDIV,POW fall through to boxed runtime
        if (both_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV: case OPERATOR_MOD:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,POW with float → boxed fallback
            }
        }

        // 5. int_float: ADD,SUB,MUL,DIV,MOD → native float
        //    IDIV,POW fall through to boxed runtime
        if (int_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV: case OPERATOR_MOD:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,POW with int_float → boxed fallback
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
        TypeId ct = get_effective_type(mt, un->operand);
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
            if (ct == LMD_TYPE_INT64) return emit_box_int64(mt, val);
            if (ct == LMD_TYPE_FLOAT) return emit_box_float(mt, val);
            return val; // fn_pos returns boxed Item for non-numeric operands
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
    if (tid == LMD_TYPE_ARRAY && node->node_type == AST_NODE_CONTENT) {
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
            // For datetime sub-types (date, time), load the specific LIT_TYPE_DATE/TIME pointer
            // because date/time/dtime share the same type_id (LMD_TYPE_DTIME)
            extern Type TYPE_DATE, TYPE_TIME;
            extern TypeType LIT_TYPE_DATE, LIT_TYPE_TIME;
            if (tt->type == &TYPE_DATE) {
                MIR_reg_t r = new_reg(mt, "tdate", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_DATE)));
                return r;
            }
            if (tt->type == &TYPE_TIME) {
                MIR_reg_t r = new_reg(mt, "ttime", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_TIME)));
                return r;
            }
            // For 'list' bare keyword: emit &LIT_TYPE_LIST directly so fn_is returns BOOL_FALSE
            // (LMD_TYPE_LIST no longer exists; 'list' type never matches at runtime)
            extern Type TYPE_LIST;
            extern TypeType LIT_TYPE_LIST;
            if (tt->type == &TYPE_LIST) {
                MIR_reg_t r = new_reg(mt, "tlist", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_LIST)));
                return r;
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
    case AST_NODE_INDEX_ASSIGN_STAM: {
        // arr[i] = val → inline store for ArrayInt, or fn_array_set fallback
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        MIR_reg_t obj = transpile_expr(mt, ca->object);
        TypeId obj_tid = get_effective_type(mt, ca->object);
        // Object must be a pointer (Array*), unbox if boxed
        MIR_reg_t arr_ptr;
        if (obj_tid == LMD_TYPE_ANY || obj_tid == LMD_TYPE_NULL) {
            arr_ptr = emit_unbox_container(mt, obj);
        } else if (obj_tid == LMD_TYPE_ARRAY_INT || obj_tid == LMD_TYPE_ARRAY_FLOAT ||
                   obj_tid == LMD_TYPE_ARRAY || obj_tid == LMD_TYPE_ARRAY) {
            // Container variable: stored as tagged Item, strip tag to get pointer
            arr_ptr = emit_unbox_container(mt, obj);
        } else {
            arr_ptr = obj;
        }
        MIR_reg_t idx = transpile_expr(mt, ca->key);
        TypeId idx_tid = get_effective_type(mt, ca->key);
        MIR_reg_t idx_int;
        if (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64) {
            idx_int = idx;
        } else {
            idx_int = emit_unbox(mt, idx, LMD_TYPE_INT);
        }

        TypeId val_tid = get_effective_type(mt, ca->value);

        // ==================================================================
        // FAST PATH: Compile-time known ArrayInt + native INT index + native INT value
        // No runtime type check — direct inline store to items[idx].
        // ==================================================================
        if (obj_tid == LMD_TYPE_ARRAY_INT &&
            (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64) &&
            val_tid == LMD_TYPE_INT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_label_t l_ok = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_label_t l_end = new_label(mt);

            // Bounds check
            MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

            MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, neg_check)));

            MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, ge_check)));

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

            // OOB: fallback to fn_array_set
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                emit_call_void_3(mt, "fn_array_set",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // In bounds: direct store items[idx] = val
            emit_label(mt, l_ok);
            {
                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // Runtime type-check path: INT index + INT value, unknown container type
        // ==================================================================
        else if ((idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64) &&
                 val_tid == LMD_TYPE_INT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_reg_t tid_byte = new_reg(mt, "tidb", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, tid_byte),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_aint = new_reg(mt, "isai", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
                MIR_new_reg_op(mt->ctx, tid_byte),
                MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_INT)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_aint)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // Inline ArrayInt store
            emit_label(mt, l_fast);
            {
                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                emit_call_void_3(mt, "fn_array_set",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Non-ArrayInt fallback
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                emit_call_void_3(mt, "fn_array_set",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // FAST PATH: Compile-time known ArrayFloat + INT index + FLOAT value
        // Direct inline store of native double to items[idx].
        // ==================================================================
        else if (obj_tid == LMD_TYPE_ARRAY_FLOAT &&
                 (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64) &&
                 val_tid == LMD_TYPE_FLOAT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_label_t l_ok = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_label_t l_end = new_label(mt);

            // Bounds check
            MIR_reg_t arr_len = new_reg(mt, "aflen", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

            MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, neg_check)));

            MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, ge_check)));

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

            // OOB: fallback to fn_array_set with boxed float
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_float(mt, val_native);
                emit_call_void_3(mt, "fn_array_set",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // In bounds: direct store items[idx] = double_val
            emit_label(mt, l_ok);
            {
                MIR_reg_t items_ptr = new_reg(mt, "fitms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "fboff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "feadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                // Store native double directly
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_D, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // DEFAULT: box value, call fn_array_set
        // ==================================================================
        else {
            MIR_reg_t val = transpile_box_item(mt, ca->value);
            emit_call_void_3(mt, "fn_array_set",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }

        // Return null (void statement)
        MIR_reg_t r = new_reg(mt, "void", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        // obj.field = val → fn_map_set(boxed_obj, boxed_key, boxed_val)
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;

        // ==================================================================
        // Phase 3: Direct field write optimization for typed maps/objects
        // ==================================================================
        if (ca->object->type && ca->key->node_type == AST_NODE_IDENT) {
            TypeId obj_type_id = ca->object->type->type_id;
            if (obj_type_id == LMD_TYPE_MAP || obj_type_id == LMD_TYPE_OBJECT) {
                TypeMap* map_type = (TypeMap*)ca->object->type;
                if (mir_has_fixed_shape(map_type)) {
                    AstIdentNode* ident = (AstIdentNode*)ca->key;
                    ShapeEntry* se = mir_find_shape_field(map_type,
                        ident->name->chars, ident->name->len);
                    if (se && se->type && mir_is_direct_access_type(mir_resolve_field_type(se))) {
                        log_debug("mir: direct field write: %.*s (type=%d offset=%lld)",
                            (int)ident->name->len, ident->name->chars,
                            mir_resolve_field_type(se), (long long)se->byte_offset);
                        bool skip_null_guard = false; // typed variables can still hold null
                        emit_mir_direct_field_write(mt, ca->object, se, ca->value, skip_null_guard);
                        // Return null (void statement)
                        MIR_reg_t r = new_reg(mt, "void", MIR_T_I64);
                        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                        return r;
                    }
                }
            }
        }

        // Fallback: runtime fn_map_set
        MIR_reg_t obj = transpile_box_item(mt, ca->object);
        // Key: if it's an ident, emit as string constant; otherwise box it
        MIR_reg_t key;
        if (ca->key->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident = (AstIdentNode*)ca->key;
            MIR_reg_t name_ptr = emit_load_string_literal(mt, ident->name->chars);
            MIR_reg_t str_obj = emit_call_1(mt, "heap_create_name", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            key = emit_box_string(mt, str_obj);
        } else {
            key = transpile_box_item(mt, ca->key);
        }
        MIR_reg_t val = transpile_box_item(mt, ca->value);
        emit_call_void_3(mt, "fn_map_set",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        // Return null (void statement)
        MIR_reg_t r = new_reg(mt, "void", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_PIPE_FILE_STAM: {
        // data |> file → pn_output2(boxed_data, boxed_file)
        // data |>> file → pn_output_append(boxed_data, boxed_file)
        AstBinaryNode* pipe = (AstBinaryNode*)node;
        MIR_reg_t data = transpile_box_item(mt, pipe->left);
        MIR_reg_t target = transpile_box_item(mt, pipe->right);
        const char* fn_name = (pipe->op == OPERATOR_PIPE_APPEND) ? "pn_output_append_mir" : "pn_output2_mir";
        return emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, data),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, target));
    }
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
    case AST_NODE_OBJECT_LITERAL: {
        // object literal: {TypeName key: value, ...}
        AstObjectLiteralNode* obj_lit = (AstObjectLiteralNode*)node;
        TypeObject* obj_type = (TypeObject*)obj_lit->type;
        int type_index = obj_type->type_index;

        // create object with inline data: Object* o = object_with_data(type_index)
        MIR_reg_t o = emit_call_1(mt, "object_with_data", MIR_T_P, MIR_T_I64,
            MIR_new_int_op(mt->ctx, type_index));

        // count and evaluate field values
        AstNode* item = obj_lit->item;
        int val_count = 0;
        while (item) { val_count++; item = item->next; }

        if (val_count == 0) {
            // no fields - return empty object
            return o;
        }

        MIR_op_t* val_ops = (MIR_op_t*)alloca(val_count * sizeof(MIR_op_t));
        item = obj_lit->item;
        int vi = 0;
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                    val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    MIR_reg_t nul = new_reg(mt, "objnull", MIR_T_I64);
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    val_ops[vi++] = MIR_new_reg_op(mt->ctx, nul);
                }
            } else {
                // bare expression (wrapping source object) — not yet handled
                MIR_reg_t val = transpile_box_item(mt, item);
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
            }
            item = item->next;
        }

        // call object_fill(o, val1, val2, ...) — variadic
        MIR_reg_t filled = emit_vararg_call(mt, "object_fill", MIR_T_P, 1,
            MIR_T_P, MIR_new_reg_op(mt->ctx, o), vi, val_ops);
        return filled;
    }
    case AST_NODE_OBJECT_TYPE: {
        // object type definition — type is already registered, emit const_type for type value
        AstObjectTypeNode* obj_type_node = (AstObjectTypeNode*)node;
        if (obj_type_node->type && obj_type_node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)obj_type_node->type;
            if (tt->type) {
                TypeObject* ot = (TypeObject*)tt->type;
                MIR_reg_t type_val = transpile_const_type(mt, ot->type_index);

                // Register method function pointers on the TypeObject
                AstNode* method_node = obj_type_node->methods;
                while (method_node) {
                    if (method_node->node_type == AST_NODE_FUNC ||
                        method_node->node_type == AST_NODE_PROC ||
                        method_node->node_type == AST_NODE_FUNC_EXPR) {
                        AstFuncNode* fn_method = (AstFuncNode*)method_node;
                        // Get the mangled function name
                        StrBuf* fn_name_buf = strbuf_new_cap(64);
                        write_fn_name(fn_name_buf, fn_method, NULL);
                        // Find the compiled MIR function
                        MIR_item_t method_func = find_local_func(mt, fn_name_buf->str);
                        if (method_func) {
                            // Get function address
                            MIR_reg_t fn_addr = new_reg(mt, "mthaddr", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, fn_addr),
                                MIR_new_ref_op(mt->ctx, method_func)));
                            // Get method name as string
                            MIR_reg_t mth_name = emit_load_string_literal(mt,
                                fn_method->name->chars);
                            // Count user-visible arity (without hidden _self)
                            int arity = 0;
                            AstNamedNode* p = fn_method->param;
                            while (p) { arity++; p = (AstNamedNode*)p->next; }
                            // Emit: object_type_set_method(type_index, name, fn_ptr, arity, is_proc)
                            MIR_var_t args[5] = {
                                {MIR_T_I64, "ti", 0}, {MIR_T_P, "n", 0},
                                {MIR_T_P, "f", 0}, {MIR_T_I64, "a", 0},
                                {MIR_T_I64, "p", 0}
                            };
                            MirImportEntry* ie = ensure_import(mt, "object_type_set_method",
                                MIR_T_I64, 5, args, 0);
                            emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                                MIR_new_ref_op(mt->ctx, ie->proto),
                                MIR_new_ref_op(mt->ctx, ie->import),
                                MIR_new_int_op(mt->ctx, (int64_t)ot->type_index),
                                MIR_new_reg_op(mt->ctx, mth_name),
                                MIR_new_reg_op(mt->ctx, fn_addr),
                                MIR_new_int_op(mt->ctx, (int64_t)arity),
                                MIR_new_int_op(mt->ctx, method_node->node_type == AST_NODE_PROC ? 1 : 0)));
                            log_debug("mir: registered method '%s' on type '%.*s'",
                                fn_name_buf->str, (int)ot->type_name.length, ot->type_name.str);
                        }
                        strbuf_free(fn_name_buf);
                    }
                    method_node = method_node->next;
                }

                return type_val;
            }
        }
        // fallback: return null
        MIR_reg_t r = new_reg(mt, "objtype_null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_ELEMENT:
        return transpile_element(mt, (AstElementNode*)node);
    case AST_NODE_MEMBER_EXPR:
        return transpile_member(mt, (AstFieldNode*)node);
    case AST_NODE_INDEX_EXPR:
        return transpile_index(mt, (AstFieldNode*)node);
    case AST_NODE_CALL_EXPR:
        return transpile_call(mt, (AstCallNode*)node);
    case AST_NODE_QUERY_EXPR: {
        // query expression: expr?T or expr.?T → fn_query(data, type_val, direct)
        AstQueryNode* query = (AstQueryNode*)node;
        MIR_reg_t obj = transpile_expr(mt, query->object);
        TypeId obj_tid = get_effective_type(mt, query->object);
        MIR_reg_t boxed_obj = emit_box(mt, obj, obj_tid);
        MIR_reg_t type_reg = transpile_expr(mt, query->query);
        TypeId type_tid = get_effective_type(mt, query->query);
        MIR_reg_t boxed_type = emit_box(mt, type_reg, type_tid);
        return emit_call_3(mt, "fn_query", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_type),
            MIR_T_I64, MIR_new_int_op(mt->ctx, query->direct ? 1 : 0));
    }
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
        // List type: use const_type(type_index) for structured types; fall back to base_type otherwise
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                extern Type TYPE_LIST;
                // bare 'list' keyword: TYPE_LIST is a plain Type (no type_index), use base_type
                if (tt->type == &TYPE_LIST) {
                    return transpile_base_type(mt, (AstTypeNode*)node);
                }
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
        // Path expression: build path using path_new + chained path_extend calls
        AstPathNode* path_node = (AstPathNode*)node;

        // Get runtime pool pointer
        MIR_reg_t pool = emit_call_0(mt, "get_runtime_pool", MIR_T_P);

        // Create base path: path_new(pool, scheme)
        MIR_reg_t path = emit_call_2(mt, "path_new", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int)path_node->scheme));

        // Extend with each segment
        for (int i = 0; i < path_node->segment_count; i++) {
            AstPathSegment* seg = &path_node->segments[i];
            if (seg->type == LPATH_SEG_WILDCARD) {
                path = emit_call_2(mt, "path_wildcard", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path));
            } else if (seg->type == LPATH_SEG_WILDCARD_REC) {
                path = emit_call_2(mt, "path_wildcard_recursive", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path));
            } else {
                // Normal segment: pass C string name
                const char* seg_name = seg->name ? seg->name->chars : "";
                MIR_reg_t name_ptr = emit_load_string_literal(mt, seg_name);
                path = emit_call_3(mt, "path_extend", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            }
        }
        // Path* is a container pointer, return as-is
        return path;
    }
    case AST_NODE_PATH_INDEX_EXPR: {
        // Path index: path[expr] → path_extend(pool, base, fn_to_cstr(expr))
        AstPathIndexNode* pix = (AstPathIndexNode*)node;
        MIR_reg_t pool = emit_call_0(mt, "get_runtime_pool", MIR_T_P);
        MIR_reg_t base = transpile_expr(mt, pix->base_path);
        MIR_reg_t seg_val = transpile_expr(mt, pix->segment_expr);
        TypeId seg_tid = get_effective_type(mt, pix->segment_expr);
        MIR_reg_t boxed_seg = emit_box(mt, seg_val, seg_tid);
        MIR_reg_t seg_cstr = emit_call_1(mt, "fn_to_cstr", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_seg));
        MIR_reg_t result = emit_call_3(mt, "path_extend", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
            MIR_T_P, MIR_new_reg_op(mt->ctx, base),
            MIR_T_P, MIR_new_reg_op(mt->ctx, seg_cstr));
        return result;
    }
    case AST_NODE_PARENT_EXPR: {
        // Parent access: expr.. → fn_member(expr, "parent") repeated for depth levels
        AstParentNode* parent = (AstParentNode*)node;
        if (parent->object) {
            MIR_reg_t obj = transpile_expr(mt, parent->object);
            TypeId obj_tid = get_effective_type(mt, parent->object);
            MIR_reg_t current = emit_box(mt, obj, obj_tid);

            // Create a "parent" string at runtime via heap_create_name
            MIR_reg_t name_ptr = emit_load_string_literal(mt, "parent");
            MIR_reg_t parent_str = emit_call_1(mt, "heap_create_name", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            MIR_reg_t parent_key = emit_box_string(mt, parent_str);

            for (int i = 0; i < parent->depth; i++) {
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

        // Phase 4: For native dual-version functions used as first-class values,
        // use the boxed wrapper (_b) since dynamic dispatch uses the Item ABI.
        NativeFuncInfo* nfi_expr = find_native_func_info(mt, name_buf->str);
        if (nfi_expr && nfi_expr->has_native) {
            StrBuf* wrapper_buf = strbuf_new_cap(64);
            write_fn_name_ex(wrapper_buf, fn_node, NULL, "_b");
            MIR_item_t wrapper_item = find_local_func(mt, wrapper_buf->str);
            if (wrapper_item) {
                log_debug("mir: fn expr '%s' → using boxed wrapper '%s'",
                    name_buf->str, wrapper_buf->str);
                func_item = wrapper_item;
            }
            strbuf_free(wrapper_buf);
        }

        if (func_item) {
            // Count arity from AST param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Get function's native code address via MIR ref
            MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                MIR_new_ref_op(mt->ctx, func_item)));

            if (fn_node->captures) {
                // Closure: allocate and populate env, then create closure object
                int cap_count = 0;
                CaptureInfo* cap = fn_node->captures;
                while (cap) { cap_count++; cap = cap->next; }

                // Allocate env: heap_calloc(cap_count * 8, 0)
                MIR_reg_t env_reg = emit_call_2(mt, "heap_calloc", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(cap_count * 8)),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

                // Populate env with captured variable values
                int cap_idx = 0;
                cap = fn_node->captures;
                while (cap) {
                    char cap_name[128];
                    snprintf(cap_name, sizeof(cap_name), "%.*s", (int)cap->name->len, cap->name->chars);

                    // Look up the captured variable value
                    MIR_reg_t cap_val = 0;
                    MirVarEntry* var = find_var(mt, cap_name);
                    if (var) {
                        // Box the variable value if needed
                        cap_val = emit_box(mt, var->reg, var->type_id);
                    } else {
                        GlobalVarEntry* gvar = find_global_var(mt, cap_name);
                        if (gvar) {
                            cap_val = load_global_var(mt, gvar);
                        } else {
                            // Variable not found - use null
                            log_error("mir: closure capture '%s' not found in scope", cap_name);
                            cap_val = new_reg(mt, "capnull", MIR_T_I64);
                            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, cap_val),
                                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                        }
                    }

                    // Store: *(env + cap_idx * 8) = cap_val
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, cap_idx * 8, env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, cap_val)));

                    log_debug("mir: closure '%s' - captured '%s' at env offset %d",
                        name_buf->str, cap_name, cap_idx * 8);

                    cap_idx++;
                    cap = cap->next;
                }

                // Get closure name for stack traces
                const char* closure_name = fn_node->name ? fn_node->name->chars : nullptr;

                // Create closure: to_closure_named(fn_ptr, arity, env, name)
                MIR_reg_t fn_obj;
                if (closure_name) {
                    MIR_reg_t name_reg = emit_load_string_literal(mt, closure_name);
                    MIR_var_t cv[4] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"e",0},{MIR_T_P,"n",0}};
                    MirImportEntry* ie = ensure_import(mt, "to_closure_named", MIR_T_P, 4, cv, 1);
                    fn_obj = new_reg(mt, "closure", MIR_T_I64);
                    emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                        MIR_new_ref_op(mt->ctx, ie->proto),
                        MIR_new_ref_op(mt->ctx, ie->import),
                        MIR_new_reg_op(mt->ctx, fn_obj),
                        MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_new_int_op(mt->ctx, arity),
                        MIR_new_reg_op(mt->ctx, env_reg),
                        MIR_new_reg_op(mt->ctx, name_reg)));
                } else {
                    fn_obj = emit_call_3(mt, "to_closure", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                        MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg));
                }

                strbuf_free(name_buf);

                // set closure_field_count (offset 2 in Function struct)
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, fn_obj, 0, 1),
                    MIR_new_int_op(mt->ctx, cap_count)));

                return fn_obj;
            } else {
                // Plain function: call to_fn_n(fn_ptr, arity) -> Function*
                MIR_reg_t fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arity));

                strbuf_free(name_buf);
                return fn_obj;  // Function* is a container - type_id in first byte
            }
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

// ============================================================================
// Phase 2: Parameter Type Inference from Usage Context
// ============================================================================
// For untyped function parameters (type_id == ANY), scan the function body to
// infer the most likely type from how the parameter is used. This allows
// speculative unboxing at function entry, enabling native arithmetic ops
// instead of slow boxed runtime calls (e.g., fn_add, fn_sub).
//
// Evidence rules:
//   +INT: param used in binary op with int literal or int-typed expr
//   +FLOAT: param used in binary op with float literal or float-typed expr
//   +NUMERIC_USE: param used in arithmetic/comparison (no literal type info on other side)
//   +STOP: param used in function call, member access, index, or non-numeric context
// If all evidence is INT → infer INT.  FLOAT present (no STOP) → infer FLOAT.
// NUMERIC_USE only (no INT/FLOAT/STOP) → infer INT (default numeric type).
// Any STOP evidence → keep as ANY.

#define INFER_INT         1
#define INFER_FLOAT       2
#define INFER_STOP        4
#define INFER_NUMERIC_USE 8
#define INFER_FLOAT_CONTEXT 16  // function body contains float literals (guards NUMERIC_USE→INT)
#define INFER_ARITH_USE   32  // param used in arithmetic (+,-,*,/,%,**), not just comparisons

#define MAX_ALIASES 8

struct InferCtx {
    int evidence;
    int name_count;
    char names[MAX_ALIASES][64];   // param name + alias names
    int  name_lens[MAX_ALIASES];
};

// Get the AST-declared type_id for a node, handling common cases
static TypeId node_type_id(AstNode* node) {
    if (!node || !node->type) return LMD_TYPE_ANY;
    return node->type->type_id;
}

// Check if a node is a reference to ANY tracked name (param or alias)
static bool is_tracked_ref(AstNode* node, InferCtx* ctx) {
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (!ident->name) return false;
        for (int i = 0; i < ctx->name_count; i++) {
            if ((int)ident->name->len == ctx->name_lens[i] &&
                memcmp(ident->name->chars, ctx->names[i], ctx->name_lens[i]) == 0)
                return true;
        }
    }
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        return is_tracked_ref(pri->expr, ctx);
    }
    return false;
}

// Add alias name to tracking context
static void add_alias(InferCtx* ctx, const char* name, int name_len) {
    if (ctx->name_count >= MAX_ALIASES || name_len >= 64) return;
    // Check if already tracked
    for (int i = 0; i < ctx->name_count; i++) {
        if (ctx->name_lens[i] == name_len &&
            memcmp(ctx->names[i], name, name_len) == 0) return;
    }
    memcpy(ctx->names[ctx->name_count], name, name_len);
    ctx->names[ctx->name_count][name_len] = '\0';
    ctx->name_lens[ctx->name_count] = name_len;
    ctx->name_count++;
}

// Classify the "other side" type for a binary op involving our param
static int classify_other_type(TypeId tid) {
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_BOOL) return INFER_INT;
    if (tid == LMD_TYPE_FLOAT) return INFER_FLOAT;
    if (tid == LMD_TYPE_ANY) return 0;  // unknown other — no evidence
    return INFER_STOP;  // string, map, list, etc.
}

// First pass: find aliases (var x = <tracked>) and register them
static void find_aliases(AstNode* node, InferCtx* ctx) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            find_aliases(list->declare, ctx);
            find_aliases(list->item, ctx);
            break;
        }
        case AST_NODE_VAR_STAM:
        case AST_NODE_LET_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            find_aliases(let_node->declare, ctx);
            break;
        }
        case AST_NODE_ASSIGN: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as && is_tracked_ref(named->as, ctx) && named->name) {
                add_alias(ctx, named->name->chars, (int)named->name->len);
            }
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* ifn = (AstIfNode*)node;
            find_aliases(ifn->then, ctx);
            find_aliases(ifn->otherwise, ctx);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            find_aliases(wh->body, ctx);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// Recursively walk AST to gather type evidence for tracked names
static void gather_evidence(AstNode* node, InferCtx* ctx) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_BINARY: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            int op = bi->op;
            bool is_arith = (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
                             op == OPERATOR_DIV || op == OPERATOR_IDIV || op == OPERATOR_MOD ||
                             op == OPERATOR_POW);
            bool is_cmp = (op >= OPERATOR_EQ && op <= OPERATOR_GE);

            if (is_arith || is_cmp) {
                bool left_is_tracked = is_tracked_ref(bi->left, ctx);
                bool right_is_tracked = is_tracked_ref(bi->right, ctx);
                // Only gather strong type evidence from LITERAL values (e.g. n+1, n%2).
                // Typed variables (e.g. a:int in a+b) are NOT strong evidence — the
                // untyped param may intentionally accept multiple types.
                if (left_is_tracked && bi->right && bi->right->type && bi->right->type->is_literal) {
                    TypeId rtid = node_type_id(bi->right);
                    ctx->evidence |= classify_other_type(rtid);
                }
                if (right_is_tracked && bi->left && bi->left->type && bi->left->type->is_literal) {
                    TypeId ltid = node_type_id(bi->left);
                    ctx->evidence |= classify_other_type(ltid);
                }
                // Track that param is used in arithmetic/comparison context.
                // Even without literal evidence, this means the param is numeric.
                if (left_is_tracked || right_is_tracked) {
                    ctx->evidence |= INFER_NUMERIC_USE;
                    if (is_arith) ctx->evidence |= INFER_ARITH_USE;
                }
                // If both sides are untyped but param is involved, check binary node's type
                if ((left_is_tracked || right_is_tracked) &&
                    !(ctx->evidence & (INFER_INT | INFER_FLOAT))) {
                    TypeId bi_tid = node_type_id((AstNode*)bi);
                    if (bi_tid == LMD_TYPE_INT) ctx->evidence |= INFER_INT;
                    else if (bi_tid == LMD_TYPE_FLOAT) ctx->evidence |= INFER_FLOAT;
                }
            }

            gather_evidence(bi->left, ctx);
            gather_evidence(bi->right, ctx);
            break;
        }
        case AST_NODE_UNARY: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->op == OPERATOR_NEG && is_tracked_ref(un->operand, ctx)) {
                // negation applies to both int and float — weak evidence only
                ctx->evidence |= INFER_NUMERIC_USE;
            }
            gather_evidence(un->operand, ctx);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* ifn = (AstIfNode*)node;
            gather_evidence(ifn->cond, ctx);
            gather_evidence(ifn->then, ctx);
            gather_evidence(ifn->otherwise, ctx);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            gather_evidence(wh->cond, ctx);
            gather_evidence(wh->body, ctx);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            gather_evidence(ret->value, ctx);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* asn = (AstAssignStamNode*)node;
            gather_evidence(asn->value, ctx);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            gather_evidence(list->declare, ctx);
            gather_evidence(list->item, ctx);
            break;
        }
        case AST_NODE_VAR_STAM:
        case AST_NODE_LET_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            gather_evidence(let_node->declare, ctx);
            break;
        }
        case AST_NODE_ASSIGN: {
            AstNamedNode* named = (AstNamedNode*)node;
            gather_evidence(named->as, ctx);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            gather_evidence(call->function, ctx);
            gather_evidence(call->argument, ctx);
            break;
        }
        case AST_NODE_INDEX_EXPR: {
            AstBinaryNode* idx = (AstBinaryNode*)node;
            if (is_tracked_ref(idx->right, ctx)) {
                ctx->evidence |= INFER_INT;
            }
            gather_evidence(idx->left, ctx);
            gather_evidence(idx->right, ctx);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            // Detect float literals anywhere in the function body.
            // This sets INFER_FLOAT_CONTEXT which guards NUMERIC_USE→INT inference.
            if (node->type && node->type->is_literal && node->type->type_id == LMD_TYPE_FLOAT) {
                ctx->evidence |= INFER_FLOAT_CONTEXT;
            }
            gather_evidence(pri->expr, ctx);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            gather_evidence(match->scrutinee, ctx);
            AstMatchArm* arm = match->first_arm;
            while (arm) {
                gather_evidence(arm->body, ctx);
                arm = (AstMatchArm*)arm->next;
            }
            break;
        }
        default:
            break;
        }

        // Advance to next sibling in linked list (iterative, not recursive)
        node = node->next;
    }
}

// Infer a parameter's type from usage context in the function body.
// First finds aliases (var x = param), then gathers evidence on param + aliases.
// Returns LMD_TYPE_INT, LMD_TYPE_FLOAT, or LMD_TYPE_ANY if ambiguous.
// is_proc: true for pn (procedural) functions — enables weaker numeric inference.
static TypeId infer_param_type(AstNode* body, const char* pname, int pname_len, bool is_proc) {
    InferCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    // Seed with the parameter name
    memcpy(ctx.names[0], pname, pname_len);
    ctx.names[0][pname_len] = '\0';
    ctx.name_lens[0] = pname_len;
    ctx.name_count = 1;

    // Pass 1: find aliases (iterate to find transitive aliases)
    int prev_count = 0;
    while (prev_count != ctx.name_count) {
        prev_count = ctx.name_count;
        find_aliases(body, &ctx);
    }

    // Pass 2: gather evidence for all tracked names
    gather_evidence(body, &ctx);

    log_debug("mir: infer_param_type('%s') aliases=%d evidence=%d", pname, ctx.name_count - 1, ctx.evidence);
    for (int i = 1; i < ctx.name_count; i++) {
        log_debug("mir:   alias[%d]: '%s'", i, ctx.names[i]);
    }

    // If we have STOP evidence, the param may be non-numeric — keep ANY
    if (ctx.evidence & INFER_STOP) return LMD_TYPE_ANY;
    // Pure INT evidence (from literals)
    if ((ctx.evidence & INFER_INT) && !(ctx.evidence & INFER_FLOAT)) return LMD_TYPE_INT;
    // Any FLOAT evidence (even mixed with INT) → FLOAT (int promotes to float)
    if (ctx.evidence & INFER_FLOAT) return LMD_TYPE_FLOAT;
    // Weak evidence: used only in numeric contexts (arithmetic/comparison) but no
    // literal type info. Only apply for procedural (pn) functions where untyped
    // params in compute-heavy loops are almost always int. Functional (fn) params
    // are often intentionally polymorphic.
    // Guard: if the function body contains float literals, the untyped param may
    // receive float values — don't force INT. (e.g. mbrot's count() uses 16.0, 2.0)
    // Only infer INT when param is used in actual arithmetic (not just comparisons).
    // Comparisons (==, <, >) are polymorphic and don't prove the param is int.
    // e.g. `(tree.root).key == key` should NOT cause key to be inferred as INT.
    if (is_proc && (ctx.evidence & INFER_ARITH_USE)) {
        if (!(ctx.evidence & INFER_FLOAT_CONTEXT)) return LMD_TYPE_INT;
    }
    // No evidence at all — keep ANY
    return LMD_TYPE_ANY;
}

// ============================================================================
// Phase 4 (P4-3.3): Infer function return type from declaration and body.
// Returns a native TypeId (INT, FLOAT, BOOL) or LMD_TYPE_ANY if unknown.
// Only returns native types for simple cases where the return path is singular.
// ============================================================================
static TypeId infer_return_type(AstFuncNode* fn_node) {
    AstNode* fn_as_node = (AstNode*)fn_node;
    bool is_proc = (fn_as_node->node_type == AST_NODE_PROC);

    // 1. Check declared return type from TypeFunc::returned
    if (fn_as_node->type && fn_as_node->type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* ft = (TypeFunc*)fn_as_node->type;
        // Skip functions that can raise errors (T^ or T^E) — error branch
        // produces a non-native value, so we can't use native return.
        if (ft->can_raise) return LMD_TYPE_ANY;
        if (ft->returned) {
            TypeId ret_tid = ft->returned->type_id;
            // Only accept simple native types for now
            if (ret_tid == LMD_TYPE_INT || ret_tid == LMD_TYPE_FLOAT ||
                ret_tid == LMD_TYPE_BOOL) {
                log_debug("mir: infer_return_type - declared type_id=%d (proc=%d)", ret_tid, is_proc);
                return ret_tid;
            }
        }
    }

    // 2. For fn (not pn): Check the body expression's type (fn body IS the return value)
    // Procs have multiple return paths via explicit `return` statements,
    // so body type inference is unreliable — only declared types are used.
    if (!is_proc && fn_node->body && fn_node->body->type) {
        TypeId body_tid = fn_node->body->type->type_id;
        // Only accept simple native scalar types
        if (body_tid == LMD_TYPE_INT || body_tid == LMD_TYPE_FLOAT ||
            body_tid == LMD_TYPE_BOOL) {
            log_debug("mir: infer_return_type - body type_id=%d", body_tid);
            return body_tid;
        }
    }

    return LMD_TYPE_ANY;
}

// ============================================================================
// Phase 4: Boxed wrapper for dual-version functions
// Generates a thin "_b" suffixed function with all-Item ABI that unboxes params,
// calls the native version, and returns the (already boxed) result.
// Used for dynamic dispatch (Function*) and cross-module calls.
// ============================================================================
static void emit_native_boxed_wrapper(MirTranspiler* mt, const char* native_name,
    AstFuncNode* fn_node, NativeFuncInfo* nfi)
{
    // Generate _b wrapper name
    StrBuf* wrapper_name = strbuf_new_cap(64);
    write_fn_name_ex(wrapper_name, fn_node, NULL, "_b");
    log_debug("mir: generating boxed wrapper '%s' for native '%s'", wrapper_name->str, native_name);

    // All wrapper params are MIR_T_I64 (boxed Item ABI)
    MIR_var_t params[16];
    int param_count = 0;
    AstNamedNode* param = fn_node->param;
    char* param_name_copies[16];
    while (param && param_count < 16) {
        char pname[64];
        snprintf(pname, sizeof(pname), "_%.*s", (int)param->name->len, param->name->chars);
        params[param_count] = {MIR_T_I64, strdup(pname), 0};
        param_name_copies[param_count] = (char*)params[param_count].name;
        param_count++;
        param = (AstNamedNode*)param->next;
    }

    // Save outer function context
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    MIR_reg_t saved_consts = mt->consts_reg;

    // Create wrapper function
    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t wrapper_item = MIR_new_func_arr(mt->ctx, wrapper_name->str, 1, &ret_type, param_count, params);
    MIR_func_t wrapper_func = MIR_get_item_func(mt->ctx, wrapper_item);
    mt->current_func_item = wrapper_item;
    mt->current_func = wrapper_func;

    // Free strdup copies
    for (int i = 0; i < param_count; i++) free(param_name_copies[i]);

    // Unbox each param to match native function's expected types
    MIR_op_t call_args[16];
    MIR_var_t call_vars[16];
    param = fn_node->param;
    for (int i = 0; i < nfi->param_count && i < 16; i++) {
        char prefixed[68];
        snprintf(prefixed, sizeof(prefixed), "_%.*s", (int)param->name->len, param->name->chars);
        MIR_reg_t preg = MIR_reg(mt->ctx, prefixed, wrapper_func);

        if (mir_is_native_param_type(nfi->param_types[i])) {
            // Unbox: Item → native type
            MIR_reg_t unboxed = emit_unbox(mt, preg, nfi->param_types[i]);
            call_args[i] = MIR_new_reg_op(mt->ctx, unboxed);
        } else {
            // Already boxed Item, pass through
            call_args[i] = MIR_new_reg_op(mt->ctx, preg);
        }
        call_vars[i] = {nfi->param_mir[i], "p", 0};
        param = (AstNamedNode*)param->next;
    }

    // Call the native version
    MIR_item_t native_func = find_local_func(mt, native_name);
    if (!native_func) {
        log_error("mir: boxed wrapper - native func '%s' not found", native_name);
    }

    // P4-3.3: Proto return type matches native function's return type
    char proto_name[160];
    snprintf(proto_name, sizeof(proto_name), "%s_wp%d", native_name, mt->label_counter++);
    MIR_type_t native_ret = nfi->return_mir;
    MIR_type_t res_types[1] = {native_ret};
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, res_types,
        nfi->param_count, call_vars);

    int nops = 3 + nfi->param_count;
    MIR_op_t ops[19];
    ops[0] = MIR_new_ref_op(mt->ctx, proto);
    ops[1] = MIR_new_ref_op(mt->ctx, native_func);
    MIR_reg_t result = new_reg(mt, "wres", native_ret);
    ops[2] = MIR_new_reg_op(mt->ctx, result);
    for (int i = 0; i < nfi->param_count; i++) ops[3 + i] = call_args[i];

    emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));

    // P4-3.3: Box native return to Item for the boxed wrapper ABI
    if (nfi->return_type != LMD_TYPE_ANY) {
        MIR_reg_t boxed = emit_box(mt, result, nfi->return_type);
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed)));
    } else {
        // Return is already boxed Item
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, result)));
    }

    MIR_finish_func(mt->ctx);

    // Register wrapper as local function with _b name
    register_local_func(mt, wrapper_name->str, wrapper_item);

    log_debug("mir: boxed wrapper '%s' generated successfully", wrapper_name->str);

    // Restore outer function context
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;
    mt->consts_reg = saved_consts;

    strbuf_free(wrapper_name);
}

static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node) {
    // Build function name
    StrBuf* name_buf = strbuf_new_cap(64);
    write_fn_name(name_buf, fn_node, NULL);

    log_debug("mir: transpile_func_def '%s'", name_buf->str);

    // Check if this function is a closure (has captured variables)
    bool is_closure = (fn_node->captures != nullptr);

    // Check if this is a method inside an object type definition
    bool is_method = (mt->method_owner != nullptr);

    AstNode* fn_as_node = (AstNode*)fn_node;
    TypeFunc* fn_type = (fn_as_node->type && fn_as_node->type->type_id == LMD_TYPE_FUNC)
        ? (TypeFunc*)fn_as_node->type : NULL;
    bool is_variadic = fn_type && fn_type->is_variadic;
    bool is_proc_fn = (fn_as_node->node_type == AST_NODE_PROC);

    // ===== Phase 4: Pre-resolve all parameter types (declared + inferred) =====
    // We resolve types BEFORE creating the MIR function so we can decide whether
    // to generate a native-typed version or the traditional all-Item version.
    TypeId resolved_param_types[16];
    int user_param_count = 0;
    {
        TypeParam* tp_iter = fn_type ? fn_type->param : NULL;
        AstNamedNode* p = fn_node->param;
        while (p && user_param_count < 16) {
            TypeId tid = p->type ? p->type->type_id : LMD_TYPE_ANY;
            bool may_be_null = tp_iter && tp_iter->is_optional && !tp_iter->default_value;

            // Resolve typed array annotations: float[] → ARRAY_FLOAT, int[] → ARRAY_INT, etc.
            // Note: p->type is a TypeParam (not TypeUnary), so we must use full_type
            // to access the actual TypeUnary structure with its operand pointer.
            if (p->type && p->type->kind == TYPE_KIND_UNARY) {
                TypeParam* tp_cast = (TypeParam*)p->type;
                Type* full = tp_cast->full_type;
                if (full && full->kind == TYPE_KIND_UNARY) {
                    TypeUnary* unary = (TypeUnary*)full;
                    Type* operand = unary->operand;
                    if (operand && operand->type_id == LMD_TYPE_TYPE && operand->kind == TYPE_KIND_SIMPLE) {
                        operand = ((TypeType*)operand)->type;
                    }
                    if (operand) {
                        switch (operand->type_id) {
                        case LMD_TYPE_FLOAT: tid = LMD_TYPE_ARRAY_FLOAT; break;
                        case LMD_TYPE_INT: tid = LMD_TYPE_ARRAY_INT; break;
                        case LMD_TYPE_INT64: tid = LMD_TYPE_ARRAY_INT64; break;
                        default: break;
                        }
                    }
                }
                if (tid != LMD_TYPE_ANY) {
                    log_debug("mir: param '%.*s' resolved typed array annotation → type_id=%d",
                        (int)p->name->len, p->name->chars, tid);
                }
            }

            // Infer type from body usage for untyped params
            if (!may_be_null && tid == LMD_TYPE_ANY && fn_node->body) {
                TypeId inferred = infer_param_type(fn_node->body,
                    p->name->chars, (int)p->name->len, is_proc_fn);
                if (inferred != LMD_TYPE_ANY) {
                    log_debug("mir: param '%.*s' pre-resolved inferred type_id=%d",
                        (int)p->name->len, p->name->chars, inferred);
                    tid = inferred;
                }
            }

            // Optional params without defaults must remain boxed (could be null)
            if (may_be_null) tid = LMD_TYPE_ANY;

            resolved_param_types[user_param_count] = tid;
            user_param_count++;
            tp_iter = tp_iter ? tp_iter->next : NULL;
            p = (AstNamedNode*)p->next;
        }
    }

    // Determine if this function qualifies for native (unboxed) dual version.
    // Eligible: not a closure, not a method, not variadic, has at least one
    // param with a native type (INT, FLOAT, BOOL, STRING, INT64), OR has a
    // declared native return type (P4-3.4: return-type-only optimization).
    bool generate_native = false;
    if (!is_closure && !is_method && !is_variadic) {
        for (int i = 0; i < user_param_count; i++) {
            if (mir_is_native_param_type(resolved_param_types[i])) {
                generate_native = true;
                break;
            }
        }
        // P4-3.4: Also enable native version when return type alone is native.
        // This allows procs with untyped params but declared return type to
        // avoid boxing/unboxing return values at call sites.
        if (!generate_native) {
            TypeId ret_tid = infer_return_type(fn_node);
            if (ret_tid == LMD_TYPE_INT || ret_tid == LMD_TYPE_FLOAT ||
                ret_tid == LMD_TYPE_BOOL) {
                generate_native = true;
            }
        }
    }

    // Register NativeFuncInfo so call sites know to use the native version
    if (generate_native) {
        NativeFuncInfo nfi;
        memset(&nfi, 0, sizeof(nfi));
        nfi.param_count = user_param_count;
        nfi.has_native = true;
        for (int i = 0; i < user_param_count; i++) {
            nfi.param_types[i] = resolved_param_types[i];
            nfi.param_mir[i] = type_to_mir(resolved_param_types[i]);
        }
        // P4-3.3: Infer return type for native version
        TypeId ret_tid = infer_return_type(fn_node);
        nfi.return_type = ret_tid;
        nfi.return_mir = (ret_tid != LMD_TYPE_ANY) ? type_to_mir(ret_tid) : MIR_T_I64;
        register_native_func_info(mt, name_buf->str, &nfi);
        log_debug("mir: dual version - native '%s' with %d params, return=%d",
            name_buf->str, user_param_count, ret_tid);
    }

    // ===== Build MIR parameter list =====
    // For native version: use resolved native MIR types for params
    // For boxed version (non-native): all params are MIR_T_I64 (Item)
    MIR_var_t params[32];
    int param_count = 0;

    // Hidden leading params (_env_ptr for closures, _self for methods)
    if (is_closure) {
        params[param_count] = {MIR_T_P, strdup("_env_ptr"), 0};
        param_count++;
    }
    if (is_method && !is_closure) {
        params[param_count] = {MIR_T_P, strdup("_self"), 0};
        param_count++;
    }

    // User params: use native types for native version, MIR_T_I64 for boxed
    AstNamedNode* param = fn_node->param;
    int pi_build = 0;
    while (param && param_count < 31) {
        char pname[64];
        snprintf(pname, sizeof(pname), "_%.*s", (int)param->name->len, param->name->chars);

        MIR_type_t mir_ptype = MIR_T_I64;  // default: boxed Item
        if (generate_native && pi_build < user_param_count) {
            mir_ptype = type_to_mir(resolved_param_types[pi_build]);
        }
        params[param_count] = {mir_ptype, strdup(pname), 0};
        param_count++;
        pi_build++;
        param = (AstNamedNode*)param->next;
    }

    // Hidden trailing params (_vargs)
    if (is_variadic && param_count < 32) {
        params[param_count] = {MIR_T_P, strdup("_vargs"), 0};
        param_count++;
    }

    // Determine return type
    // P4-3.3: Use native return type when inferred (INT/BOOL → I64, FLOAT → D)
    MIR_type_t ret_type = MIR_T_I64;
    bool native_return = false;
    if (generate_native) {
        NativeFuncInfo* nfi_ret = find_native_func_info(mt, name_buf->str);
        if (nfi_ret && nfi_ret->return_type != LMD_TYPE_ANY) {
            ret_type = nfi_ret->return_mir;
            native_return = true;
        }
    }

    // Save current function context
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    MIR_reg_t saved_consts_reg = mt->consts_reg;
    MIR_reg_t saved_gc_reg = mt->gc_reg;
    bool saved_in_user_func = mt->in_user_func;
    AstNode* saved_func_body = mt->func_body;
    mt->in_user_func = true;
    mt->func_body = fn_node->body;  // P4-3.2: for mutation analysis

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

    // Load gc_heap_t* for inline bump allocation: context->heap->gc
    MIR_reg_t heap_reg_fn = new_reg(mt, "heap_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, heap_reg_fn),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 64, runtime_fn, 0, 1)));  // context->heap
    mt->gc_reg = new_reg(mt, "gc_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->gc_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, heap_reg_fn, 0, 1)));  // heap->gc

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

    // Save and set closure context
    AstFuncNode* saved_closure = mt->current_closure;
    MIR_reg_t saved_env_reg = mt->env_reg;
    if (is_closure) {
        mt->current_closure = fn_node;
        // Get the _env_ptr parameter register
        MIR_reg_t env_ptr_reg = MIR_reg(mt->ctx, "_env_ptr", func);
        mt->env_reg = env_ptr_reg;

        // Load captured variables from env into local scope
        // env is a flat array of Items (8 bytes each)
        int cap_index = 0;
        CaptureInfo* cap = fn_node->captures;
        while (cap) {
            char cap_name[128];
            snprintf(cap_name, sizeof(cap_name), "%.*s", (int)cap->name->len, cap->name->chars);

            // Load: captured_val = *(env_ptr + cap_index * 8)
            MIR_reg_t cap_val = new_reg(mt, "cap", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, cap_val),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, cap_index * 8, env_ptr_reg, 0, 1)));

            // Store as local variable (already boxed as Item)
            set_var(mt, cap_name, cap_val, MIR_T_I64, LMD_TYPE_ANY);

            // Mark as captured variable with env offset for write-back on mutation
            MirVarEntry* cap_var = find_var(mt, cap_name);
            if (cap_var) {
                cap_var->env_offset = cap_index * 8;
            }

            log_debug("mir: closure '%s' - loaded capture '%s' at env offset %d",
                name_buf->str, cap_name, cap_index * 8);

            cap_index++;
            cap = cap->next;
        }
    }

    // Set vargs for variadic functions (save previous for nesting)
    if (is_variadic) {
        MIR_reg_t vargs_reg = MIR_reg(mt->ctx, "_vargs", func);
        MIR_reg_t saved_reg = emit_call_1(mt, "set_vargs", MIR_T_P, MIR_T_P,
            MIR_new_reg_op(mt->ctx, vargs_reg));
        // Store in a named register so we can reference it at function exit
        MIR_reg_t named_saved = MIR_new_func_reg(mt->ctx, mt->current_func, MIR_T_I64, "_saved_vargs");
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, named_saved),
            MIR_new_reg_op(mt->ctx, saved_reg)));
    }

    // Bind parameters as local variables
    // Uses pre-resolved types from the Phase 4 pre-resolve step above.
    log_debug("mir: binding params for '%s', fn_type=%p, native=%d",
        name_buf->str, (void*)fn_type, generate_native);
    param = fn_node->param;
    int pi = 0;
    while (param && pi < user_param_count) {
        char pname[64];
        snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);

        // Function parameter register is already created by MIR with the prefixed name
        char prefixed[68];
        snprintf(prefixed, sizeof(prefixed), "_%s", pname);
        MIR_reg_t preg = MIR_reg(mt->ctx, prefixed, func);

        TypeId tid = resolved_param_types[pi];

        log_debug("mir: param '%s' type_id=%d has_annotation=%d native=%d", pname, tid,
            param->type ? param->type->type_id : -1, generate_native);

        if (generate_native && mir_is_native_param_type(tid)) {
            // Phase 4 native version: param arrives as native type, no unboxing needed.
            // Register directly with the native MIR type.
            MIR_type_t mtype = type_to_mir(tid);
            set_var(mt, pname, preg, mtype, tid);
            log_debug("mir: native param '%s' registered directly, mir_type=%d", pname, mtype);
        } else if (!generate_native && mir_is_native_param_type(tid)) {
            // Boxed version: params arrive as boxed Items, unbox to native type
            MIR_reg_t unboxed = emit_unbox(mt, preg, tid);
            MIR_type_t mtype = type_to_mir(tid);
            set_var(mt, pname, unboxed, mtype, tid);
        } else {
            // Untyped, nullable optional, or complex type: keep as boxed Item.
            // Preserve known container type (e.g. ARRAY_FLOAT from annotation) so
            // the transpiler can generate fast paths for array access.
            TypeId var_type = (tid == LMD_TYPE_ARRAY_FLOAT || tid == LMD_TYPE_ARRAY_INT ||
                               tid == LMD_TYPE_ARRAY_INT64) ? tid : LMD_TYPE_ANY;

            MIR_reg_t final_reg = preg;
            // For typed array params: ensure the incoming array is the correct
            // typed representation.  The caller may pass a generic Array that
            // needs to be converted to ArrayInt/ArrayFloat/ArrayInt64 so that
            // the FAST PATH inline reads operate on the correct memory layout.
            if (var_type == LMD_TYPE_ARRAY_INT || var_type == LMD_TYPE_ARRAY_FLOAT ||
                var_type == LMD_TYPE_ARRAY_INT64) {
                TypeId elem_tid = (var_type == LMD_TYPE_ARRAY_INT)   ? LMD_TYPE_INT   :
                                  (var_type == LMD_TYPE_ARRAY_FLOAT) ? LMD_TYPE_FLOAT  :
                                                                       LMD_TYPE_INT64;
                final_reg = emit_call_2(mt, "ensure_typed_array", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, preg),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, elem_tid));
                log_debug("mir: param '%s' — inserted ensure_typed_array (elem=%d)", pname, elem_tid);
            }

            set_var(mt, pname, final_reg, MIR_T_I64, var_type);
        }

        pi++;
        param = (AstNamedNode*)param->next;
    }

    // Set proc flag based on function type
    bool saved_in_proc = mt->in_proc;

    // Set variadic body flag for restore_vargs in return/raise paths
    bool saved_in_variadic_body = mt->in_variadic_body;
    mt->in_variadic_body = is_variadic;
    mt->in_proc = (fn_as_node->node_type == AST_NODE_PROC);

    // P4-3.4: Set native return type for transpile_return() to use
    TypeId saved_native_return_tid = mt->native_return_tid;
    mt->native_return_tid = LMD_TYPE_ANY;
    if (native_return) {
        NativeFuncInfo* nfi_ctx = find_native_func_info(mt, name_buf->str);
        if (nfi_ctx && nfi_ctx->return_type != LMD_TYPE_ANY) {
            mt->native_return_tid = nfi_ctx->return_type;
        }
    }

    log_debug("mir: params bound, transpiling body of '%s'", name_buf->str);

    // Reset block_returned for this function's compilation scope.
    // Prevents the flag from leaking from one function's body into the next.
    bool saved_block_returned = mt->block_returned;
    mt->block_returned = false;

    // For methods: load object fields from _self into local scope
    TypeObject* saved_method_owner = mt->method_owner;
    MIR_reg_t saved_self_reg = mt->self_reg;
    if (is_method && !is_closure) {
        // _self is passed as void* (actually a boxed Item: uint64_t reinterpreted as pointer)
        MIR_reg_t self_ptr_reg = MIR_reg(mt->ctx, "_self", func);
        // Cast void* to i64 to use as boxed Item
        MIR_reg_t self_item_reg = new_reg(mt, "self_item", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, self_item_reg),
            MIR_new_reg_op(mt->ctx, self_ptr_reg)));
        mt->self_reg = self_item_reg;

        // Load each field from the object into local scope
        TypeObject* owner = mt->method_owner;
        ShapeEntry* field = owner->shape;
        while (field) {
            const char* field_name = field->name->str;
            int field_name_len = (int)field->name->length;
            // Create a Symbol* key for fn_member lookup
            MIR_reg_t name_ptr = emit_load_string_literal(mt, field_name);
            MIR_reg_t sym_ptr = emit_call_2(mt, "heap_create_symbol", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)field_name_len));
            MIR_reg_t key_boxed = emit_box_symbol(mt, sym_ptr);
            // Call fn_member(self_item, key) to get field value
            MIR_reg_t field_val = emit_call_2(mt, "fn_member", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, self_item_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_boxed));

            // Determine the field's type for optimal native representation
            TypeId field_tid = field->type ? field->type->type_id : LMD_TYPE_ANY;
            // Unwrap TypeType wrapper if present
            if (field_tid == LMD_TYPE_TYPE && field->type) {
                TypeType* ft = (TypeType*)field->type;
                if (ft->type) field_tid = ft->type->type_id;
            }
            if (field_tid == LMD_TYPE_INT || field_tid == LMD_TYPE_FLOAT || field_tid == LMD_TYPE_BOOL) {
                MIR_reg_t unboxed = emit_unbox(mt, field_val, field_tid);
                MIR_type_t mtype = type_to_mir(field_tid);
                set_var(mt, field_name, unboxed, mtype, field_tid);
            } else {
                set_var(mt, field_name, field_val, MIR_T_I64, LMD_TYPE_ANY);
            }

            log_debug("mir: method '%s' - loaded field '%s' from self", name_buf->str, field_name);
            field = field->next;
        }
    }

    // ===== TCO Setup =====
    // Check if this function is eligible for tail call optimization.
    // If so, wrap the body in a loop: tco_label -> body -> (tail calls jump back)
    // with an iteration counter to prevent infinite loops.
    bool use_tco = should_use_tco(fn_node);
    AstFuncNode* saved_tco_func = mt->tco_func;
    MIR_label_t saved_tco_label = mt->tco_label;
    MIR_reg_t saved_tco_count_reg = mt->tco_count_reg;
    bool saved_tail_position = mt->in_tail_position;

    if (use_tco) {
        log_debug("mir: TCO enabled for '%s'", name_buf->str);
        mt->tco_func = fn_node;

        // Create iteration counter register, init to 0
        mt->tco_count_reg = new_reg(mt, "tco_count", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, 0)));

        // Create the TCO loop label
        mt->tco_label = MIR_new_label(mt->ctx);
        emit_insn(mt, mt->tco_label);

        // Increment counter: tco_count = tco_count + 1
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, 1)));

        // Guard: if tco_count > LAMBDA_TCO_MAX_ITERATIONS, call overflow error
        MIR_label_t ok_label = MIR_new_label(mt->ctx);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BLE, MIR_new_label_op(mt->ctx, ok_label),
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, LAMBDA_TCO_MAX_ITERATIONS)));

        // Overflow path: call lambda_stack_overflow_error(func_name)
        MIR_reg_t fn_name_ptr = emit_load_string_literal(mt, name_buf->str);
        emit_call_void_1(mt, "lambda_stack_overflow_error",
            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_name_ptr));

        // Emit a ret after overflow call (unreachable but needed for MIR validation)
        if (ret_type == MIR_T_D) {
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_double_op(mt->ctx, 0.0)));
        } else {
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        }

        // Continue label for normal execution
        emit_insn(mt, ok_label);

        // Body is in tail position for TCO
        mt->in_tail_position = true;
    }

    // Transpile body
    MIR_reg_t body_result;
    if (native_return) {
        // P4-3.3: Native return — produce unboxed native value
        NativeFuncInfo* nfi_body = find_native_func_info(mt, name_buf->str);
        body_result = transpile_expr(mt, fn_node->body);
        // P4-3.4: For procs, explicit `return` statements already emit native
        // MIR_new_ret_insn via transpile_return(). The body_result here is just
        // the ret_dummy value and may have an incompatible MIR type. Skip the
        // post-body unbox/box when block_returned is set.
        if (!mt->block_returned) {
            TypeId body_tid = get_effective_type(mt, fn_node->body);
            if (body_tid != nfi_body->return_type) {
                if (body_tid == LMD_TYPE_ANY || body_tid == LMD_TYPE_NULL) {
                    // Body returned boxed Item, unbox to native
                    body_result = emit_unbox(mt, body_result, nfi_body->return_type);
                } else {
                    // Different native type — box then unbox
                    MIR_reg_t boxed = emit_box(mt, body_result, body_tid);
                    body_result = emit_unbox(mt, boxed, nfi_body->return_type);
                }
            }
        } else {
            // Proc already returned — emit type-correct dummy for trailing ret
            if (nfi_body->return_mir == MIR_T_D) {
                body_result = new_reg(mt, "body_fallthrough", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, body_result),
                    MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                body_result = new_reg(mt, "body_fallthrough", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, body_result),
                    MIR_new_int_op(mt->ctx, 0)));
            }
        }
    } else {
        // Standard: box result to Item
        body_result = transpile_box_item(mt, fn_node->body);
    }

    // Return result
    // Always emit the trailing ret — MIR needs a ret on every code path.
    // For TCO functions, this serves as the non-tail-call exit path.
    // For functions where body already returned, it's dead code (MIR removes it).
    // Restore vargs before returning from variadic function
    if (is_variadic) {
        MIR_reg_t saved_vargs = MIR_reg(mt->ctx, "_saved_vargs", func);
        emit_call_void_1(mt, "restore_vargs", MIR_T_P, MIR_new_reg_op(mt->ctx, saved_vargs));
    }
    emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, body_result)));

    pop_scope(mt);

    MIR_finish_func(mt->ctx);

    // Phase 4: Generate boxed wrapper (_b) for native functions
    // This must happen after MIR_finish_func but before restoring outer context,
    // because emit_native_boxed_wrapper creates a new MIR function.
    if (generate_native) {
        NativeFuncInfo* nfi = find_native_func_info(mt, name_buf->str);
        if (nfi) {
            emit_native_boxed_wrapper(mt, name_buf->str, fn_node, nfi);
        }
    }

    // Restore function context
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;
    mt->consts_reg = saved_consts_reg;
    mt->gc_reg = saved_gc_reg;
    mt->in_user_func = saved_in_user_func;
    mt->func_body = saved_func_body;
    mt->current_closure = saved_closure;
    mt->env_reg = saved_env_reg;
    mt->in_proc = saved_in_proc;
    mt->in_variadic_body = saved_in_variadic_body;
    mt->native_return_tid = saved_native_return_tid;
    mt->block_returned = saved_block_returned;
    mt->method_owner = saved_method_owner;
    mt->self_reg = saved_self_reg;
    mt->tco_func = saved_tco_func;
    mt->tco_label = saved_tco_label;
    mt->tco_count_reg = saved_tco_count_reg;
    mt->in_tail_position = saved_tail_position;

    strbuf_free(name_buf);
}

// ============================================================================
// P4-3.2: Mutation analysis — check if a variable is ever index-assigned
// in a given AST subtree. Used to determine if elem_type can be safely set.
// Returns true if arr[i] = val is found where arr matches var_name.
// Does NOT recurse into nested function/procedure definitions (separate scope).
// ============================================================================
static bool has_index_mutation(const char* var_name, AstNode* node) {
    while (node) {
        // Check if this node is an index-assignment targeting our variable
        if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            AstNode* obj = ca->object;
            while (obj && obj->node_type == AST_NODE_PRIMARY)
                obj = ((AstPrimaryNode*)obj)->expr;
            if (obj && obj->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident = (AstIdentNode*)obj;
                if (ident->name && ident->name->len > 0 &&
                    strncmp(var_name, ident->name->chars, ident->name->len) == 0 &&
                    var_name[ident->name->len] == '\0') {
                    return true;
                }
            }
        }

        // Recurse into child nodes that can contain statements
        switch (node->node_type) {
        case AST_NODE_IF_EXPR: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->then && has_index_mutation(var_name, if_node->then)) return true;
            if (if_node->otherwise && has_index_mutation(var_name, if_node->otherwise)) return true;
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->body && has_index_mutation(var_name, wh->body)) return true;
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->then && has_index_mutation(var_name, for_node->then)) return true;
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            AstMatchArm* arm = match->first_arm;
            while (arm) {
                if (arm->body && has_index_mutation(var_name, arm->body)) return true;
                arm = (AstMatchArm*)arm->next;
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare && has_index_mutation(var_name, list->declare)) return true;
            if (list->item && has_index_mutation(var_name, list->item)) return true;
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            if (let_node->declare && has_index_mutation(var_name, let_node->declare)) return true;
            break;
        }
        // Recurse into function bodies (module-level vars can be mutated from within)
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn = (AstFuncNode*)node;
            if (fn->body && has_index_mutation(var_name, fn->body)) return true;
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
    return false;
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

            // Phase 4: Forward-declare the boxed wrapper (_b) for functions that
            // will generate dual versions. Also pre-register NativeFuncInfo so that
            // call sites in other functions can use the native calling convention
            // even when the target function hasn't been transpiled yet.
            {
                bool is_closure = (fn_node->captures != nullptr);
                bool is_method = false; // in prepass, method_owner is not set
                AstNode* fn_as = (AstNode*)fn_node;
                TypeFunc* ft = (fn_as->type && fn_as->type->type_id == LMD_TYPE_FUNC)
                    ? (TypeFunc*)fn_as->type : NULL;
                bool is_variadic = ft && ft->is_variadic;
                if (!is_closure && !is_variadic && !is_method) {
                    // Resolve all param types (matching transpile_func_def logic)
                    TypeId fwd_param_types[16];
                    int fwd_param_count = 0;
                    bool has_native = false;
                    bool is_proc = (fn_as->node_type == AST_NODE_PROC);
                    {
                        TypeParam* tp = ft ? ft->param : NULL;
                        AstNamedNode* p = fn_node->param;
                        while (p && fwd_param_count < 16) {
                            TypeId tid = p->type ? p->type->type_id : LMD_TYPE_ANY;
                            bool may_be_null = tp && tp->is_optional && !tp->default_value;
                            if (!may_be_null && tid == LMD_TYPE_ANY && fn_node->body) {
                                TypeId inferred = infer_param_type(fn_node->body,
                                    p->name->chars, (int)p->name->len, is_proc);
                                if (inferred != LMD_TYPE_ANY) tid = inferred;
                            }
                            if (may_be_null) tid = LMD_TYPE_ANY;
                            fwd_param_types[fwd_param_count] = tid;
                            if (mir_is_native_param_type(tid)) has_native = true;
                            fwd_param_count++;
                            tp = tp ? tp->next : NULL;
                            p = (AstNamedNode*)p->next;
                        }
                    }
                    if (has_native) {
                        // Forward-declare the _b wrapper
                        StrBuf* wrapper_buf = strbuf_new_cap(64);
                        write_fn_name_ex(wrapper_buf, fn_node, NULL, "_b");
                        MIR_item_t fwd_b = MIR_new_forward(mt->ctx, wrapper_buf->str);
                        register_local_func(mt, wrapper_buf->str, fwd_b);
                        log_debug("mir: forward-declared boxed wrapper '%s'", wrapper_buf->str);
                        strbuf_free(wrapper_buf);

                        // Pre-register NativeFuncInfo so call sites can use native ABI
                        NativeFuncInfo nfi;
                        memset(&nfi, 0, sizeof(nfi));
                        nfi.param_count = fwd_param_count;
                        nfi.has_native = true;
                        for (int i = 0; i < fwd_param_count; i++) {
                            nfi.param_types[i] = fwd_param_types[i];
                            nfi.param_mir[i] = type_to_mir(fwd_param_types[i]);
                        }
                        // P4-3.3: Infer return type
                        TypeId fwd_ret_tid = infer_return_type(fn_node);
                        nfi.return_type = fwd_ret_tid;
                        nfi.return_mir = (fwd_ret_tid != LMD_TYPE_ANY) ? type_to_mir(fwd_ret_tid) : MIR_T_I64;
                        register_native_func_info(mt, name_buf->str, &nfi);
                        log_debug("mir: pre-registered NativeFuncInfo for '%s', return=%d",
                            name_buf->str, fwd_ret_tid);
                    }
                    // P4-3.4: Also enable native version when return type alone is native
                    if (!has_native) {
                        TypeId fwd_ret_tid = infer_return_type(fn_node);
                        if (fwd_ret_tid == LMD_TYPE_INT || fwd_ret_tid == LMD_TYPE_FLOAT ||
                            fwd_ret_tid == LMD_TYPE_BOOL) {
                            // Forward-declare the _b wrapper
                            StrBuf* wrapper_buf = strbuf_new_cap(64);
                            write_fn_name_ex(wrapper_buf, fn_node, NULL, "_b");
                            MIR_item_t fwd_b = MIR_new_forward(mt->ctx, wrapper_buf->str);
                            register_local_func(mt, wrapper_buf->str, fwd_b);
                            log_debug("mir: forward-declared boxed wrapper '%s' (return-type-only)", wrapper_buf->str);
                            strbuf_free(wrapper_buf);

                            NativeFuncInfo nfi;
                            memset(&nfi, 0, sizeof(nfi));
                            nfi.param_count = fwd_param_count;
                            nfi.has_native = true;
                            for (int i = 0; i < fwd_param_count; i++) {
                                nfi.param_types[i] = fwd_param_types[i];
                                nfi.param_mir[i] = type_to_mir(fwd_param_types[i]);
                            }
                            nfi.return_type = fwd_ret_tid;
                            nfi.return_mir = type_to_mir(fwd_ret_tid);
                            register_native_func_info(mt, name_buf->str, &nfi);
                            log_debug("mir: pre-registered NativeFuncInfo for '%s' (return-only), return=%d",
                                name_buf->str, fwd_ret_tid);
                        }
                    }
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
        case AST_NODE_OBJECT_TYPE: {
            // forward-declare methods inside the object type
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) prepass_forward_declare(mt, obj->methods);
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
        case AST_NODE_IF_EXPR: {
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
// Pre-pass: create BSS items for module-level let bindings
// This allows user-defined functions to access module-level variables
// through BSS memory instead of MIR registers (which are function-local).
// Only scans TOP-LEVEL declarations (not nested in functions).
// ============================================================================

static void prepass_create_global_vars(MirTranspiler* mt, AstNode* node) {
    while (node) {
        if (node->node_type == AST_NODE_LET_STAM || node->node_type == AST_NODE_PUB_STAM ||
            node->node_type == AST_NODE_VAR_STAM) {
            AstLetNode* let_node = (AstLetNode*)node;
            AstNode* decl = let_node->declare;
            while (decl) {
                if (decl->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)decl;
                    char name[128];
                    snprintf(name, sizeof(name), "%.*s", (int)asn->name->len, asn->name->chars);

                    // Create BSS for this variable (8 bytes for boxed Item)
                    char bss_name[140];
                    snprintf(bss_name, sizeof(bss_name), "_gvar_%s", name);
                    MIR_item_t bss = MIR_new_bss(mt->ctx, bss_name, 8);

                    TypeId tid = decl->type ? decl->type->type_id : LMD_TYPE_ANY;
                    if (tid == LMD_TYPE_ANY && asn->as && asn->as->type) {
                        tid = asn->as->type->type_id;
                    }

                    GlobalVarEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    snprintf(entry.name, sizeof(entry.name), "%s", name);
                    entry.bss_item = bss;
                    entry.type_id = tid;
                    entry.mir_type = type_to_mir(tid);
                    hashmap_set(mt->global_vars, &entry);
                } else if (decl->node_type == AST_NODE_DECOMPOSE) {
                    AstDecomposeNode* dec = (AstDecomposeNode*)decl;
                    for (int i = 0; i < dec->name_count; i++) {
                        String* name_str = dec->names[i];
                        char name[128];
                        snprintf(name, sizeof(name), "%.*s", (int)name_str->len, name_str->chars);
                        char bss_name[140];
                        snprintf(bss_name, sizeof(bss_name), "_gvar_%s", name);
                        MIR_item_t bss = MIR_new_bss(mt->ctx, bss_name, 8);

                        GlobalVarEntry entry;
                        memset(&entry, 0, sizeof(entry));
                        snprintf(entry.name, sizeof(entry.name), "%s", name);
                        entry.bss_item = bss;
                        entry.type_id = LMD_TYPE_ANY;
                        entry.mir_type = MIR_T_I64;
                        hashmap_set(mt->global_vars, &entry);
                    }
                }
                decl = decl->next;
            }
        } else if (node->node_type == AST_NODE_CONTENT) {
            // Recurse into content blocks to find nested let statements
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_create_global_vars(mt, list->declare);
            prepass_create_global_vars(mt, list->item);
        }
        node = node->next;
    }
}

// ============================================================================
// Pre-pass: compile string/symbol pattern definitions
// Walks AST to find pattern defs and compiles them to RE2 regex,
// storing results in the script's type_list for runtime const_pattern() access.
// ============================================================================

static void prepass_compile_patterns(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN: {
            AstPatternDefNode* pattern_def = (AstPatternDefNode*)node;
            TypePattern* pattern_type = (TypePattern*)pattern_def->type;

            // compile pattern to regex if not already compiled
            if (pattern_type->re2 == nullptr && pattern_def->as != nullptr) {
                const char* error_msg = nullptr;
                TypePattern* compiled = compile_pattern_ast(mt->script_pool, pattern_def->as,
                    pattern_def->is_symbol, &error_msg);
                if (compiled) {
                    // copy compiled info to existing type
                    pattern_type->re2 = compiled->re2;
                    pattern_type->source = compiled->source;
                    // add to type_list for runtime access via const_pattern()
                    arraylist_append(mt->type_list, pattern_type);
                    pattern_type->pattern_index = mt->type_list->length - 1;
                    log_debug("mir: compiled pattern '%.*s' to regex, index=%d",
                        (int)pattern_def->name->len, pattern_def->name->chars, pattern_type->pattern_index);
                } else {
                    log_error("mir: failed to compile pattern '%.*s': %s",
                        (int)pattern_def->name->len, pattern_def->name->chars,
                        error_msg ? error_msg : "unknown error");
                }
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_compile_patterns(mt, list->declare);
            prepass_compile_patterns(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_compile_patterns(mt, let_node->declare);
            break;
        }
        case AST_NODE_OBJECT_TYPE: {
            // recurse into methods for pattern compilation
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) prepass_compile_patterns(mt, obj->methods);
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            if (fn_node->body) prepass_compile_patterns(mt, fn_node->body);
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
        case AST_NODE_OBJECT_TYPE: {
            // recurse into methods for function definition, with method_owner context
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) {
                TypeObject* saved_owner = mt->method_owner;
                TypeType* tt = (TypeType*)obj->type;
                mt->method_owner = (TypeObject*)tt->type;
                prepass_define_functions(mt, obj->methods);
                mt->method_owner = saved_owner;
            }
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
        case AST_NODE_IF_EXPR: {
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

void transpile_mir_ast(MIR_context_t ctx, AstScript *script, const char* source,
                       ArrayList* type_list, Pool* script_pool) {
    log_notice("transpile AST to MIR (direct)");

    MirTranspiler mt;
    memset(&mt, 0, sizeof(mt));
    mt.ctx = ctx;
    mt.script = script;
    mt.source = source;
    mt.is_main = true;
    mt.type_list = type_list;
    mt.script_pool = script_pool;
    mt.native_return_tid = LMD_TYPE_ANY;  // P4-3.4: no native return at module level

    // Init import cache
    mt.import_cache = hashmap_new(sizeof(ImportCacheEntry), 128, 0, 0,
        import_cache_hash, import_cache_cmp, NULL, NULL);
    mt.local_funcs = hashmap_new(sizeof(LocalFuncEntry), 32, 0, 0,
        local_func_hash, local_func_cmp, NULL, NULL);
    mt.global_vars = hashmap_new(sizeof(GlobalVarEntry), 32, 0, 0,
        global_var_hash, global_var_cmp, NULL, NULL);

    // Create module
    mt.module = MIR_new_module(ctx, "lambda_script");

    // Import _lambda_rt (shared context pointer)
    MIR_item_t rt_import = MIR_new_import(ctx, "_lambda_rt");

    // Pre-pass: compile string/symbol patterns
    prepass_compile_patterns(&mt, script->child);

    // Pre-pass: create BSS items for module-level variables
    prepass_create_global_vars(&mt, script->child);

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

    // Load gc_heap_t* for inline bump allocation: context->heap->gc
    // EvalContext.heap is at offset 64 (sizeof(Context)), Heap.gc is at offset 8
    MIR_reg_t heap_reg = new_reg(&mt, "heap_ptr", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, heap_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, 64, runtime_reg, 0, 1)));  // context->heap
    mt.gc_reg = new_reg(&mt, "gc_ptr", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, mt.gc_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, 8, heap_reg, 0, 1)));  // heap->gc

    // Set up variable scope for main body
    push_scope(&mt);

    // P4-3.2: Set func_body for mutation analysis at module level
    mt.func_body = script->child;

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
        case AST_NODE_OBJECT_TYPE: {
            // Object type definition — methods compiled in pre-pass
            // Emit method registration code (transpile_expr handles this)
            transpile_expr(&mt, child);
            break;
        }
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

    // Emit invocation of user-defined main() procedure if present
    // Equivalent to C transpiler's: if (rt->run_main) result = _main0();
    child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            child = ((AstListNode*)child)->item;
            continue;
        }
        if (child->node_type == AST_NODE_PROC) {
            AstFuncNode* proc_node = (AstFuncNode*)child;
            if (proc_node->name && strcmp(proc_node->name->chars, "main") == 0) {
                // Load rt->run_main flag
                MIR_reg_t run_main_val = new_reg(&mt, "run_main", MIR_T_I64);
                emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, run_main_val),
                    MIR_new_mem_op(ctx, MIR_T_U8, offsetof(Context, run_main), runtime_reg, 0, 1)));

                MIR_label_t skip_main = MIR_new_label(ctx);
                emit_insn(&mt, MIR_new_insn(ctx, MIR_BF, MIR_new_label_op(ctx, skip_main),
                    MIR_new_reg_op(ctx, run_main_val)));

                // Call the user-defined main procedure: _main0()
                StrBuf* main_name = strbuf_new_cap(64);
                write_fn_name(main_name, proc_node, NULL);

                MIR_item_t main_func = find_local_func(&mt, main_name->str);
                if (main_func) {
                    // Local function: call directly with MIR_new_ref_op
                    char proto_name[160];
                    snprintf(proto_name, sizeof(proto_name), "%s_mp%d", main_name->str, mt.label_counter++);
                    MIR_type_t res_types[1] = { MIR_T_I64 };
                    MIR_item_t proto = MIR_new_proto_arr(ctx, proto_name, 1, res_types, 0, NULL);
                    MIR_reg_t main_result = new_reg(&mt, "main_call", MIR_T_I64);
                    emit_insn(&mt, MIR_new_call_insn(ctx, 3,
                        MIR_new_ref_op(ctx, proto),
                        MIR_new_ref_op(ctx, main_func),
                        MIR_new_reg_op(ctx, main_result)));
                    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                        MIR_new_reg_op(ctx, main_result)));
                } else {
                    log_error("mir: could not find local function '%s' for run_main", main_name->str);
                }
                strbuf_free(main_name);

                emit_insn(&mt, skip_main);
                break;  // only one main proc
            }
        }
        child = child->next;
    }

    pop_scope(&mt);

    // Return result
    emit_insn(&mt, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result)));

#ifndef NDEBUG
    // Dump MIR text for debugging (before finish_func to capture state on error)
    FILE* mir_dump = fopen("temp/mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }
#endif

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    // Load module for linking (required before MIR_link)
    MIR_load_module(ctx, mt.module);

    // Cleanup
    hashmap_free(mt.import_cache);
    hashmap_free(mt.local_funcs);
    hashmap_free(mt.global_vars);
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
    // Note: load_script recursively compiles imported modules via C transpiler path
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

    // Register imported module functions and variables for cross-module resolution
    clear_dynamic_imports();
    AstScript* ast_script = (AstScript*)runner.script->ast_root;
    AstNode* import_child = ast_script->child;
    while (import_child) {
        if (import_child->node_type == AST_NODE_IMPORT) {
            AstImportNode* imp = (AstImportNode*)import_child;
            if (imp->script && imp->script->jit_context) {
                log_info("mir: registering imports from module '%.*s' (index=%d)",
                    (int)imp->module.length, imp->module.str, imp->script->index);

                // Register pub functions from the imported module
                AstNode* mod_node = imp->script->ast_root;
                if (mod_node && mod_node->node_type == AST_SCRIPT) {
                    AstNode* mod_child = ((AstScript*)mod_node)->child;
                    while (mod_child) {
                        if (mod_child->node_type == AST_NODE_CONTENT) {
                            mod_child = ((AstListNode*)mod_child)->item;
                            continue;
                        }
                        if (mod_child->node_type == AST_NODE_FUNC ||
                            mod_child->node_type == AST_NODE_FUNC_EXPR ||
                            mod_child->node_type == AST_NODE_PROC) {
                            AstFuncNode* fn_node = (AstFuncNode*)mod_child;
                            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
                            if (fn_type && fn_type->is_public) {
                                // Register the boxed wrapper version if it exists, otherwise direct
                                StrBuf* fn_name = strbuf_new_cap(64);
                                if (mod_child->node_type != AST_NODE_PROC && needs_fn_call_wrapper(fn_node)) {
                                    write_fn_name_ex(fn_name, fn_node, NULL, "_b");
                                    void* fn_ptr = find_func(imp->script->jit_context, fn_name->str);
                                    if (fn_ptr) {
                                        register_dynamic_import(strdup(fn_name->str), fn_ptr);
                                        log_debug("mir: registered import wrapper fn: %s -> %p", fn_name->str, fn_ptr);
                                    }
                                }
                                // Also register the direct version
                                strbuf_free(fn_name);
                                fn_name = strbuf_new_cap(64);
                                write_fn_name(fn_name, fn_node, NULL);
                                void* fn_ptr = find_func(imp->script->jit_context, fn_name->str);
                                if (fn_ptr) {
                                    register_dynamic_import(strdup(fn_name->str), fn_ptr);
                                    log_debug("mir: registered import fn: %s -> %p", fn_name->str, fn_ptr);
                                }
                                strbuf_free(fn_name);
                            }
                        }
                        else if (mod_child->node_type == AST_NODE_PUB_STAM) {
                            // Register pub variable BSS addresses
                            AstNode* declare = ((AstLetNode*)mod_child)->declare;
                            while (declare) {
                                AstNamedNode* named = (AstNamedNode*)declare;
                                if (named->name) {
                                    StrBuf* var_name = strbuf_new_cap(64);
                                    write_var_name(var_name, named, NULL);
                                    MIR_item_t bss_item = find_import(imp->script->jit_context, var_name->str);
                                    if (bss_item && bss_item->addr) {
                                        // Register BSS address — the main script will load the value at runtime
                                        register_dynamic_import(strdup(var_name->str), bss_item->addr);
                                        log_debug("mir: registered import var BSS: %s -> %p", var_name->str, bss_item->addr);
                                    }
                                    strbuf_free(var_name);
                                }
                                // also register error variable BSS for ^err destructuring
                                if (named->error_name) {
                                    StrBuf* err_name = strbuf_new_cap(64);
                                    strbuf_append_char(err_name, '_');
                                    strbuf_append_str_n(err_name, named->error_name->chars, named->error_name->len);
                                    MIR_item_t err_bss = find_import(imp->script->jit_context, err_name->str);
                                    if (err_bss && err_bss->addr) {
                                        register_dynamic_import(strdup(err_name->str), err_bss->addr);
                                        log_debug("mir: registered import error var BSS: %s -> %p", err_name->str, err_bss->addr);
                                    }
                                    strbuf_free(err_name);
                                }
                                declare = declare->next;
                            }
                        }
                        mod_child = mod_child->next;
                    }
                }
            }
        }
        import_child = import_child->next;
    }

    // Initialize MIR context
    unsigned int opt_level = runtime ? runtime->optimize_level : 2;

    // MIR path profiling probes
    bool profiling = is_profile_enabled() && profile_count < PROFILE_MAX_SCRIPTS;
    profile_time_t m0, m1, m2, m3;

    if (profiling) profile_get_time(&m0);

    MIR_context_t ctx = jit_init(opt_level);

    if (profiling) profile_get_time(&m1);

    // Transpile AST to MIR directly
    transpile_mir_ast(ctx, (AstScript*)runner.script->ast_root, runner.script->source,
                      runner.script->type_list, runner.script->pool);

    if (profiling) profile_get_time(&m2);

    // Link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    if (profiling) profile_get_time(&m3);

    // Record MIR path profiling data
    // Fields reused: jit_init_ms = MIR ctx init, transpile_ms = AST->MIR transpile,
    //                c2mir_ms = MIR_link (link+gen), script_path prefixed with "[MIR]"
    if (profiling) {
        PhaseProfile* prof = &profile_data[profile_count++];
        char* mir_path_buf = (char*)malloc(512);
        snprintf(mir_path_buf, 512, "[MIR] %s",
                 runner.script->reference ? runner.script->reference : "unknown");
        prof->script_path = mir_path_buf;
        prof->parse_ms = 0;    // already captured in C2MIR entry
        prof->ast_ms = 0;      // already captured in C2MIR entry
        prof->transpile_ms = elapsed_ms_val(m1, m2);  // AST->MIR direct transpile
        prof->jit_init_ms = elapsed_ms_val(m0, m1);   // MIR context init
        prof->file_write_ms = 0;
        prof->c2mir_ms = elapsed_ms_val(m2, m3);      // MIR_link (link + codegen)
        prof->mir_gen_ms = 0;
        prof->code_len = 0;
        // Dump immediately — --mir path may crash during execution/cleanup
        profile_dump_to_file();
    }

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

    // Initialize imported modules before executing main script
    // This runs each module's _init_consts + _mod_main to compute pub variable values
    import_child = ast_script->child;
    bool has_imports = false;
    while (import_child) {
        if (import_child->node_type == AST_NODE_IMPORT) has_imports = true;
        import_child = import_child->next;
    }
    if (has_imports) {
        // Set up context for module initialization
        runner_setup_context(&runner);
        runner.context.run_main = run_main;

        // Register BSS global variables as GC roots (after context/heap is created)
        register_bss_gc_roots((void*)ctx);

        import_child = ast_script->child;
        while (import_child) {
            if (import_child->node_type == AST_NODE_IMPORT) {
                AstImportNode* imp = (AstImportNode*)import_child;
                if (imp->script && imp->script->jit_context) {
                    // Initialize module constants
                    typedef void (*init_consts_fn)(void**);
                    init_consts_fn init_fn = (init_consts_fn)find_func(imp->script->jit_context, "_init_mod_consts");
                    if (init_fn && imp->script->const_list) {
                        init_fn(imp->script->const_list->data);
                    }
                    // Initialize module types
                    typedef void (*init_types_fn)(void*);
                    init_types_fn init_types = (init_types_fn)find_func(imp->script->jit_context, "_init_mod_types");
                    if (init_types && imp->script->type_list) {
                        init_types(imp->script->type_list);
                    }
                    // Run module main to compute pub variables
                    if (imp->script->main_func) {
                        log_info("mir: running imported module main for '%.*s'",
                            (int)imp->module.length, imp->module.str);
                        // Set up context for the imported module
                        runner.context.consts = imp->script->const_list ? imp->script->const_list->data : nullptr;
                        runner.context.type_list = imp->script->type_list;
                        imp->script->main_func(&runner.context);
                        // Restore main script context
                        runner.context.consts = runner.script->const_list ? runner.script->const_list->data : nullptr;
                        runner.context.type_list = runner.script->type_list;
                    }
                }
            }
            import_child = import_child->next;
        }

        // Execute main script using already-initialized context
        log_notice("Executing JIT compiled code...");
        runner.context.run_main = run_main;
        Item result;
        #if defined(__APPLE__) || defined(__linux__)
        if (sigsetjmp(_lambda_recovery_point, 1)) {
        #elif defined(_WIN32)
        if (setjmp(_lambda_recovery_point)) {
        #else
        if (0) {
        #endif
            log_error("exec: recovered from stack overflow via signal handler");
            _lambda_stack_overflow_flag = false;
            lambda_stack_overflow_error("<signal>");
            result = runner.context.result = ItemError;
        } else {
            result = runner.context.result = runner.script->main_func(&runner.context);
        }

        // Create output
        Pool* output_pool = pool_create();
        Input* output = Input::create(output_pool, nullptr);
        if (!output) {
            log_error("Failed to create output Input");
            if (output_pool) pool_destroy(output_pool);
            jit_cleanup(ctx);
            return nullptr;
        }
        resolve_sys_paths_recursive(result);
        MarkBuilder builder(output);
        output->root = builder.deep_copy(result);

        jit_cleanup(ctx);
        return output;
    }

    // Execute (no imports — use standard path)
    // Save original C2MIR context (created by load_script) before overwriting
    MIR_context_t c2mir_ctx = runner.script->jit_context;
    runner.script->jit_context = ctx;  // store for BSS root registration
    Input* output = execute_script_and_create_output(&runner, run_main);

    // Cleanup MIR direct context
    jit_cleanup(ctx);

    // Cleanup original C2MIR context (leaked when we overwrote jit_context)
    if (c2mir_ctx && c2mir_ctx != ctx) {
        jit_cleanup(c2mir_ctx);
    }

    // Null out to prevent double-cleanup in runtime_cleanup
    runner.script->jit_context = NULL;

    return output;
}
