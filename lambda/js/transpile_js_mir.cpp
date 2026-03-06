// transpile_js_mir.cpp - Direct MIR generation for JavaScript
//
// Replaces the C codegen path (transpile_js.cpp -> C2MIR) with direct MIR IR
// emission. Mirrors the Lambda MIR transpiler architecture (transpile-mir.cpp).
//
// Design: All JS values are boxed Items (MIR_T_I64). JS runtime functions
// (js_add, js_subtract, etc.) take and return Items. Boxing is only needed
// for literals; expression results are already boxed.

#include "js_transpiler.hpp"
#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../../lib/file_utils.h"
#include "../transpiler.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <alloca.h>

// External reference to Lambda runtime context pointer (defined in mir.c)
extern "C" Context* _lambda_rt;
extern "C" {
    void *import_resolver(const char *name);
}

// External from runner.cpp
extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();

// ============================================================================
// JsMirTranspiler Context
// ============================================================================

struct JsMirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
};

struct JsMirVarEntry {
    MIR_reg_t reg;
};

// Loop label pair for break/continue
struct JsLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
};

// Function entry for pre-pass collection
struct JsFuncCollected {
    JsFunctionNode* node;
    char name[128];
    MIR_item_t func_item;   // set after creation
};

struct JsMirTranspiler {
    JsTranspiler* tp;        // access to AST, name_pool, scopes

    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache: name -> JsMirImportEntry
    struct hashmap* import_cache;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Variable scopes: array of hashmaps, name -> JsMirVarEntry
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop label stack
    JsLoopLabels loop_stack[32];
    int loop_depth;

    int reg_counter;
    int label_counter;

    // Collected functions (pre-pass)
    JsFuncCollected func_entries[256];
    int func_count;
};

// ============================================================================
// Hashmap helpers
// ============================================================================

struct JsImportCacheEntry {
    char name[128];
    JsMirImportEntry entry;
};

static int js_import_cache_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsImportCacheEntry*)a)->name, ((JsImportCacheEntry*)b)->name);
}
static uint64_t js_import_cache_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsImportCacheEntry*)item)->name,
        strlen(((JsImportCacheEntry*)item)->name), seed0, seed1);
}

struct JsVarScopeEntry {
    char name[128];
    JsMirVarEntry var;
};

static int js_var_scope_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsVarScopeEntry*)a)->name, ((JsVarScopeEntry*)b)->name);
}
static uint64_t js_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsVarScopeEntry*)item)->name,
        strlen(((JsVarScopeEntry*)item)->name), seed0, seed1);
}

struct JsLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

static int js_local_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsLocalFuncEntry*)a)->name, ((JsLocalFuncEntry*)b)->name);
}
static uint64_t js_local_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsLocalFuncEntry*)item)->name,
        strlen(((JsLocalFuncEntry*)item)->name), seed0, seed1);
}

// ============================================================================
// Basic MIR helpers
// ============================================================================

static MIR_reg_t jm_new_reg(JsMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, mt->reg_counter++);
    MIR_type_t rtype = (type == MIR_T_P || type == MIR_T_F) ? MIR_T_I64 : type;
    return MIR_new_func_reg(mt->ctx, mt->current_func, rtype, name);
}

static MIR_label_t jm_new_label(JsMirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

static void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn) {
    MIR_append_insn(mt->ctx, mt->current_func_item, insn);
}

static void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label) {
    MIR_append_insn(mt->ctx, mt->current_func_item, label);
}

// ============================================================================
// Scope management
// ============================================================================

static void jm_push_scope(JsMirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("js-mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(JsVarScopeEntry), 16, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
}

static void jm_pop_scope(JsMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("js-mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg) {
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name) {
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Import management
// ============================================================================

static JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    JsImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    JsImportCacheEntry* found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return &found->entry;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    JsImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return &found->entry;
}

// Item(Item, Item)
static JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// Item(Item)
static JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(void)
static JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name) {
    return jm_ensure_import(mt, name, MIR_T_I64, 0, NULL, 1);
}

// ============================================================================
// Emit call helpers
// ============================================================================

static MIR_reg_t jm_call_0(JsMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

static MIR_reg_t jm_call_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1));
    return res;
}

static MIR_reg_t jm_call_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2));
    return res;
}

static MIR_reg_t jm_call_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3));
    return res;
}

static MIR_reg_t jm_call_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4));
    return res;
}

static void jm_call_void_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1));
}

static void jm_call_void_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2));
}

static void jm_call_void_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 3, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3));
}

// ============================================================================
// Constants
// ============================================================================

static const uint64_t ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;
static const uint64_t MASK56         = 0x00FFFFFFFFFFFFFFULL;

static MIR_reg_t jm_emit_null(JsMirTranspiler* mt) {
    MIR_reg_t r = jm_new_reg(mt, "null", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    return r;
}

// ============================================================================
// Boxing helpers
// ============================================================================

// Box int64 constant -> Item
static MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value) {
    // Inline i2it: result = ITEM_INT_TAG | (value & MASK56)
    MIR_reg_t r = jm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = ITEM_INT_TAG | ((uint64_t)value & MASK56);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)tagged)));
    return r;
}

// Box int64 register -> Item (runtime range check)
static MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val) {
    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;

    MIR_reg_t result = jm_new_reg(mt, "boxi", MIR_T_I64);
    MIR_reg_t masked = jm_new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = jm_new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t le_max = jm_new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = jm_new_reg(mt, "ge", MIR_T_I64);
    MIR_reg_t in_range = jm_new_reg(mt, "rng", MIR_T_I64);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LE, MIR_new_reg_op(mt->ctx, le_max),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MAX_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, ge_min),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MIN_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range),
        MIR_new_reg_op(mt->ctx, le_max), MIR_new_reg_op(mt->ctx, ge_min)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, (int64_t)MASK56)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, tagged),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_INT_TAG), MIR_new_reg_op(mt->ctx, masked)));

    MIR_label_t l_ok = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, in_range)));
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_ok);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tagged)));
    jm_emit_label(mt, l_end);
    return result;
}

// Box double -> Item via push_d
static MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d_reg));
}

// Box string via s2it tagging: result = ptr ? (STR_TAG | ptr) : ITEM_NULL
static MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = jm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_nn);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit_label(mt, l_end);
    return result;
}

// Create a boxed string Item from a C string literal
// Calls heap_create_name(chars) -> String*, then boxes with s2it
static MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len) {
    // Intern so pointer is valid at JIT runtime
    String* interned = name_pool_create_len(mt->tp->name_pool, str, len);
    MIR_reg_t ptr = jm_new_reg(mt, "cs", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
    MIR_reg_t name_reg = jm_call_1(mt, "heap_create_name", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, ptr));
    return jm_box_string(mt, name_reg);
}

// ============================================================================
// Local function management
// ============================================================================

static MIR_item_t jm_find_local_func(JsMirTranspiler* mt, const char* name) {
    JsLocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsLocalFuncEntry* found = (JsLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
    return found ? found->func_item : NULL;
}

static void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item) {
    JsLocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.func_item = func_item;
    hashmap_set(mt->local_funcs, &entry);
}

// ============================================================================
// Function name generation
// ============================================================================

static void jm_make_fn_name(char* buf, int bufsize, JsFunctionNode* fn, JsMirTranspiler* mt) {
    StrBuf* sb = strbuf_new_cap(64);
    strbuf_append_str(sb, "_js_");
    if (fn->name && fn->name->chars) {
        strbuf_append_str_n(sb, fn->name->chars, fn->name->len);
    } else {
        strbuf_append_str(sb, "anon");
        strbuf_append_int(sb, mt->label_counter++);
    }
    strbuf_append_char(sb, '_');
    strbuf_append_int(sb, ts_node_start_byte(fn->base.node));
    snprintf(buf, bufsize, "%s", sb->str);
    strbuf_free(sb);
}

static int jm_count_params(JsFunctionNode* fn) {
    int count = 0;
    JsAstNode* p = fn->params;
    while (p) { count++; p = p->next; }
    return count;
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr);
static MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);
static void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt);

// ============================================================================
// Function collection (pre-pass) - post-order to get innermost first
// ============================================================================

static void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* prog = (JsProgramNode*)node;
        JsAstNode* s = prog->body;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        // recurse into body first (post-order)
        if (fn->body) jm_collect_functions(mt, fn->body);
        // add this function
        if (mt->func_count < 256) {
            JsFuncCollected* e = &mt->func_entries[mt->func_count];
            e->node = fn;
            jm_make_fn_name(e->name, sizeof(e->name), fn, mt);
            e->func_item = NULL; // set during creation
            mt->func_count++;
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->consequent);
        jm_collect_functions(mt, n->alternate);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_collect_functions(mt, n->init);
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->update);
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* n = (JsExpressionStatementNode*)node;
        jm_collect_functions(mt, n->expression);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* n = (JsVariableDeclarationNode*)node;
        JsAstNode* d = n->declarations;
        while (d) { jm_collect_functions(mt, d); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* n = (JsVariableDeclaratorNode*)node;
        jm_collect_functions(mt, n->init);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* n = (JsReturnNode*)node;
        jm_collect_functions(mt, n->argument);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        jm_collect_functions(mt, n->callee);
        JsAstNode* a = n->arguments;
        while (a) { jm_collect_functions(mt, a); a = a->next; }
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* n = (JsBinaryNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* n = (JsUnaryNode*)node;
        jm_collect_functions(mt, n->operand);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* n = (JsMemberNode*)node;
        jm_collect_functions(mt, n->object);
        jm_collect_functions(mt, n->property);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* n = (JsArrayNode*)node;
        JsAstNode* e = n->elements;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* n = (JsObjectNode*)node;
        JsAstNode* p = n->properties;
        while (p) { jm_collect_functions(mt, p); p = p->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* n = (JsPropertyNode*)node;
        jm_collect_functions(mt, n->value);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* n = (JsConditionalNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->consequent);
        jm_collect_functions(mt, n->alternate);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* n = (JsTemplateLiteralNode*)node;
        JsAstNode* e = n->expressions;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_collect_functions(mt, n->block);
        jm_collect_functions(mt, n->handler);
        jm_collect_functions(mt, n->finalizer);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* n = (JsThrowNode*)node;
        jm_collect_functions(mt, n->argument);
        break;
    }
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        jm_collect_functions(mt, n->callee);
        JsAstNode* a = n->arguments;
        while (a) { jm_collect_functions(mt, a); a = a->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        jm_collect_functions(mt, n->discriminant);
        JsAstNode* c = n->cases;
        while (c) { jm_collect_functions(mt, c); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        jm_collect_functions(mt, n->test);
        JsAstNode* s = n->consequent;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_collect_functions(mt, n->body);
        jm_collect_functions(mt, n->test);
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* n = (JsForOfNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        jm_collect_functions(mt, n->body);
        break;
    }
    default:
        break; // leaf nodes, identifiers, literals
    }
}

// ============================================================================
// Find collected function entry by node pointer
// ============================================================================

static JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn) {
    for (int i = 0; i < mt->func_count; i++) {
        if (mt->func_entries[i].node == fn) return &mt->func_entries[i];
    }
    return NULL;
}

// ============================================================================
// Argument array allocation helper
// ============================================================================

// Allocates stack space for an Item[] args array, stores evaluated args,
// returns register pointing to the array. If arg_count == 0, returns 0.
static MIR_reg_t jm_build_args_array(JsMirTranspiler* mt, JsAstNode* first_arg, int arg_count) {
    if (arg_count == 0) return 0;

    // Allocate stack space: ALLOCA(arg_count * 8)
    MIR_reg_t args_ptr = jm_new_reg(mt, "args", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
        MIR_new_reg_op(mt->ctx, args_ptr),
        MIR_new_int_op(mt->ctx, arg_count * 8)));

    // Evaluate and store each argument
    JsAstNode* arg = first_arg;
    for (int i = 0; i < arg_count && arg; i++) {
        MIR_reg_t val = jm_transpile_box_item(mt, arg);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, i * 8, args_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        arg = arg->next;
    }

    return args_ptr;
}

static int jm_count_args(JsAstNode* arg) {
    int count = 0;
    while (arg) { count++; arg = arg->next; }
    return count;
}

// ============================================================================
// Expression transpilers - each returns MIR_reg_t holding boxed Item result
// ============================================================================

// Forward declarations for transpiler functions defined later
static MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call);
static void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw);
static void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw);
static void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo);

static MIR_reg_t jm_transpile_literal(JsMirTranspiler* mt, JsLiteralNode* lit) {
    switch (lit->literal_type) {
    case JS_LITERAL_NUMBER: {
        double val = lit->value.number_value;
        // check if value is an integer
        if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0) {
            return jm_box_int_const(mt, (int64_t)val);
        } else {
            MIR_reg_t d = jm_new_reg(mt, "dbl", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, d),
                MIR_new_double_op(mt->ctx, val)));
            return jm_box_float(mt, d);
        }
    }
    case JS_LITERAL_STRING: {
        String* sv = lit->value.string_value;
        return jm_box_string_literal(mt, sv->chars, sv->len);
    }
    case JS_LITERAL_BOOLEAN: {
        MIR_reg_t r = jm_new_reg(mt, "bool", MIR_T_I64);
        uint64_t bval = lit->value.boolean_value ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(mt->ctx, (int64_t)bval)));
        return r;
    }
    case JS_LITERAL_NULL:
    case JS_LITERAL_UNDEFINED:
        return jm_emit_null(mt);
    }
    return jm_emit_null(mt);
}

static MIR_reg_t jm_transpile_identifier(JsMirTranspiler* mt, JsIdentifierNode* id) {
    // Build variable name: _js_<name>
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
    JsMirVarEntry* var = jm_find_var(mt, vname);
    if (var) return var->reg;

    log_error("js-mir: undefined variable '%s'", vname);
    return jm_emit_null(mt);
}

// Binary expression: emit call to js_<op>(left, right)
static MIR_reg_t jm_transpile_binary(JsMirTranspiler* mt, JsBinaryNode* bin) {
    const char* fn_name = NULL;
    switch (bin->op) {
    case JS_OP_ADD:        fn_name = "js_add"; break;
    case JS_OP_SUB:        fn_name = "js_subtract"; break;
    case JS_OP_MUL:        fn_name = "js_multiply"; break;
    case JS_OP_DIV:        fn_name = "js_divide"; break;
    case JS_OP_MOD:        fn_name = "js_modulo"; break;
    case JS_OP_EXP:        fn_name = "js_power"; break;
    case JS_OP_EQ:         fn_name = "js_equal"; break;
    case JS_OP_NE:         fn_name = "js_not_equal"; break;
    case JS_OP_STRICT_EQ:  fn_name = "js_strict_equal"; break;
    case JS_OP_STRICT_NE:  fn_name = "js_strict_not_equal"; break;
    case JS_OP_LT:         fn_name = "js_less_than"; break;
    case JS_OP_LE:         fn_name = "js_less_equal"; break;
    case JS_OP_GT:         fn_name = "js_greater_than"; break;
    case JS_OP_GE:         fn_name = "js_greater_equal"; break;
    case JS_OP_AND:        fn_name = "js_logical_and"; break;
    case JS_OP_OR:         fn_name = "js_logical_or"; break;
    case JS_OP_BIT_AND:    fn_name = "js_bitwise_and"; break;
    case JS_OP_BIT_OR:     fn_name = "js_bitwise_or"; break;
    case JS_OP_BIT_XOR:    fn_name = "js_bitwise_xor"; break;
    case JS_OP_BIT_LSHIFT: fn_name = "js_left_shift"; break;
    case JS_OP_BIT_RSHIFT: fn_name = "js_right_shift"; break;
    case JS_OP_BIT_URSHIFT: fn_name = "js_unsigned_right_shift"; break;
    case JS_OP_INSTANCEOF: fn_name = "js_instanceof"; break;
    case JS_OP_IN:         fn_name = "js_in"; break;
    case JS_OP_NULLISH_COALESCE: fn_name = "js_nullish_coalesce"; break;
    default:
        log_error("js-mir: unknown binary op %d", bin->op);
        return jm_emit_null(mt);
    }
    MIR_reg_t left = jm_transpile_box_item(mt, bin->left);
    MIR_reg_t right = jm_transpile_box_item(mt, bin->right);
    return jm_call_2(mt, fn_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
}

// Unary expression
static MIR_reg_t jm_transpile_unary(JsMirTranspiler* mt, JsUnaryNode* un) {
    switch (un->op) {
    case JS_OP_NOT:
        return jm_call_1(mt, "js_logical_not", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_BIT_NOT:
        return jm_call_1(mt, "js_bitwise_not", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_TYPEOF:
        return jm_call_1(mt, "js_typeof", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_PLUS:
    case JS_OP_ADD:
        return jm_call_1(mt, "js_unary_plus", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_MINUS:
    case JS_OP_SUB:
        return jm_call_1(mt, "js_unary_minus", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_INCREMENT: {
        // ++var or var++ -> var = js_add(var, i2it(1))
        MIR_reg_t operand = jm_transpile_box_item(mt, un->operand);
        MIR_reg_t one = jm_box_int_const(mt, 1);
        MIR_reg_t result = jm_call_2(mt, "js_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, operand),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
        // Update variable if operand is identifier
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, result)));
            }
        }
        return result;
    }
    case JS_OP_DECREMENT: {
        MIR_reg_t operand = jm_transpile_box_item(mt, un->operand);
        MIR_reg_t one = jm_box_int_const(mt, 1);
        MIR_reg_t result = jm_call_2(mt, "js_subtract", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, operand),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, result)));
            }
        }
        return result;
    }
    case JS_OP_VOID: {
        // Evaluate for side effects, return null
        jm_transpile_box_item(mt, un->operand);
        return jm_emit_null(mt);
    }
    case JS_OP_DELETE:
        return jm_box_int_const(mt, 1); // simplified: always true
    default:
        log_error("js-mir: unknown unary op %d", un->op);
        return jm_emit_null(mt);
    }
}

// Assignment expression
static MIR_reg_t jm_transpile_assignment(JsMirTranspiler* mt, JsAssignmentNode* asgn) {
    if (!asgn->left || !asgn->right) return jm_emit_null(mt);

    // Simple variable assignment: x = expr
    if (asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)asgn->left;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (!var) {
            log_error("js-mir: assignment to undefined var '%s'", vname);
            return jm_emit_null(mt);
        }

        MIR_reg_t rhs;
        if (asgn->op == JS_OP_ASSIGN) {
            rhs = jm_transpile_box_item(mt, asgn->right);
        } else {
            // Compound assignment: var op= expr -> var = js_op(var, expr)
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            const char* fn = NULL;
            switch (asgn->op) {
            case JS_OP_ADD_ASSIGN: fn = "js_add"; break;
            case JS_OP_SUB_ASSIGN: fn = "js_subtract"; break;
            case JS_OP_MUL_ASSIGN: fn = "js_multiply"; break;
            case JS_OP_DIV_ASSIGN: fn = "js_divide"; break;
            case JS_OP_MOD_ASSIGN: fn = "js_modulo"; break;
            case JS_OP_EXP_ASSIGN: fn = "js_power"; break;
            case JS_OP_BIT_AND_ASSIGN: fn = "js_bitwise_and"; break;
            case JS_OP_BIT_OR_ASSIGN: fn = "js_bitwise_or"; break;
            case JS_OP_BIT_XOR_ASSIGN: fn = "js_bitwise_xor"; break;
            case JS_OP_LSHIFT_ASSIGN: fn = "js_left_shift"; break;
            case JS_OP_RSHIFT_ASSIGN: fn = "js_right_shift"; break;
            case JS_OP_URSHIFT_ASSIGN: fn = "js_unsigned_right_shift"; break;
            default: fn = "js_add"; break;
            }
            rhs = jm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, rval));
        }

        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, var->reg),
            MIR_new_reg_op(mt->ctx, rhs)));
        return var->reg;
    }

    // Member assignment: obj.prop = expr, obj[key] = expr
    if (asgn->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)asgn->left;

        // Detect chained member: obj.style.prop = val -> js_dom_set_style_property
        if (!member->computed && member->object &&
            member->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* outer = (JsMemberNode*)member->object;
            if (!outer->computed && outer->property &&
                outer->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* mid_prop = (JsIdentifierNode*)outer->property;
                if (mid_prop->name && mid_prop->name->len == 5 &&
                    strncmp(mid_prop->name->chars, "style", 5) == 0 &&
                    member->property &&
                    member->property->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* style_prop = (JsIdentifierNode*)member->property;
                    MIR_reg_t obj = jm_transpile_box_item(mt, outer->object);
                    MIR_reg_t key = jm_box_string_literal(mt, style_prop->name->chars, style_prop->name->len);
                    MIR_reg_t val = jm_transpile_box_item(mt, asgn->right);
                    return jm_call_3(mt, "js_dom_set_style_property", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
            }
        }

        // General member assignment
        MIR_reg_t obj = jm_transpile_box_item(mt, member->object);
        MIR_reg_t key;
        if (member->computed) {
            key = jm_transpile_box_item(mt, member->property);
        } else if (member->property && member->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)member->property;
            key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
        } else {
            key = jm_transpile_box_item(mt, member->property);
        }

        MIR_reg_t new_val;
        if (asgn->op == JS_OP_ASSIGN) {
            new_val = jm_transpile_box_item(mt, asgn->right);
        } else {
            // Compound: get current value, apply operation, set result
            MIR_reg_t cur_val = jm_call_2(mt, "js_property_access", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            const char* fn = NULL;
            switch (asgn->op) {
            case JS_OP_ADD_ASSIGN: fn = "js_add"; break;
            case JS_OP_SUB_ASSIGN: fn = "js_subtract"; break;
            case JS_OP_MUL_ASSIGN: fn = "js_multiply"; break;
            case JS_OP_DIV_ASSIGN: fn = "js_divide"; break;
            case JS_OP_MOD_ASSIGN: fn = "js_modulo"; break;
            case JS_OP_EXP_ASSIGN: fn = "js_power"; break;
            case JS_OP_BIT_AND_ASSIGN: fn = "js_bitwise_and"; break;
            case JS_OP_BIT_OR_ASSIGN: fn = "js_bitwise_or"; break;
            case JS_OP_BIT_XOR_ASSIGN: fn = "js_bitwise_xor"; break;
            case JS_OP_LSHIFT_ASSIGN: fn = "js_left_shift"; break;
            case JS_OP_RSHIFT_ASSIGN: fn = "js_right_shift"; break;
            case JS_OP_URSHIFT_ASSIGN: fn = "js_unsigned_right_shift"; break;
            default: fn = "js_add"; break;
            }
            new_val = jm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, rval));
        }

        return jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, new_val));
    }

    log_error("js-mir: unsupported assignment target %d", asgn->left->node_type);
    return jm_emit_null(mt);
}

// ============================================================================
// Call expression helpers
// ============================================================================

static bool jm_is_console_log(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    if (!m->property || m->property->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
    return obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "console", 7) == 0 &&
           prop->name && prop->name->len == 3 && strncmp(prop->name->chars, "log", 3) == 0;
}

static bool jm_is_math_call(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    return obj->name && obj->name->len == 4 && strncmp(obj->name->chars, "Math", 4) == 0;
}

static bool jm_is_document_call(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    return obj->name && obj->name->len == 8 && strncmp(obj->name->chars, "document", 8) == 0;
}

static bool jm_is_window_getComputedStyle(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    if (!m->property || m->property->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
    return obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "window", 6) == 0 &&
           prop->name && prop->name->len == 16 && strncmp(prop->name->chars, "getComputedStyle", 16) == 0;
}

// Call expression
static MIR_reg_t jm_transpile_call(JsMirTranspiler* mt, JsCallNode* call) {
    int arg_count = jm_count_args(call->arguments);

    // console.log(args...)
    if (jm_is_console_log(call)) {
        JsAstNode* arg = call->arguments;
        if (arg_count > 1) {
            // Multi-arg: space-separated output
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            jm_call_void_2(mt, "js_console_log_multi",
                MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
        } else if (arg) {
            MIR_reg_t val = jm_transpile_box_item(mt, arg);
            jm_call_void_1(mt, "js_console_log",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else {
            MIR_reg_t null_val = jm_emit_null(mt);
            jm_call_void_1(mt, "js_console_log",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, null_val));
        }
        return jm_emit_null(mt);
    }

    // process.stdout.write(str)
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* outer = (JsMemberNode*)call->callee;
        if (!outer->computed && outer->property &&
            outer->property->node_type == JS_AST_NODE_IDENTIFIER &&
            outer->object && outer->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* inner = (JsMemberNode*)outer->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)outer->property;
            if (!inner->computed && inner->object &&
                inner->object->node_type == JS_AST_NODE_IDENTIFIER &&
                inner->property && inner->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj = (JsIdentifierNode*)inner->object;
                JsIdentifierNode* mid = (JsIdentifierNode*)inner->property;
                // process.stdout.write
                if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
                    mid->name && mid->name->len == 6 && strncmp(mid->name->chars, "stdout", 6) == 0 &&
                    prop->name && prop->name->len == 5 && strncmp(prop->name->chars, "write", 5) == 0) {
                    MIR_reg_t arg_val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                    jm_call_void_1(mt, "js_process_stdout_write",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
                    return jm_emit_null(mt);
                }
                // process.hrtime.bigint()
                if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
                    mid->name && mid->name->len == 6 && strncmp(mid->name->chars, "hrtime", 6) == 0 &&
                    prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "bigint", 6) == 0) {
                    return jm_call_0(mt, "js_process_hrtime_bigint", MIR_T_I64);
                }
            }
        }
    }

    // document.<method>(args...)
    if (jm_is_document_call(call)) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
        MIR_reg_t method_str = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_3(mt, "js_document_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_str),
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // Math.<method>(args...)
    if (jm_is_math_call(call)) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
        MIR_reg_t method_str = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_3(mt, "js_math_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_str),
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // window.getComputedStyle(elem, pseudo)
    if (jm_is_window_getComputedStyle(call)) {
        JsAstNode* arg = call->arguments;
        MIR_reg_t elem = arg ? jm_transpile_box_item(mt, arg) : jm_emit_null(mt);
        MIR_reg_t pseudo = (arg && arg->next) ? jm_transpile_box_item(mt, arg->next) : jm_emit_null(mt);
        return jm_call_2(mt, "js_get_computed_style", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, elem),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pseudo));
    }

    // String.fromCharCode(code)
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (!m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER &&
            m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
            if (obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "String", 6) == 0 &&
                prop->name && prop->name->len == 12 && strncmp(prop->name->chars, "fromCharCode", 12) == 0) {
                MIR_reg_t code = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_box_int_const(mt, 0);
                return jm_call_1(mt, "js_string_fromCharCode", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, code));
            }
        }
    }

    // Generic method call: obj.method(args) -> dispatch by receiver type
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;

            MIR_reg_t recv = jm_transpile_box_item(mt, m->object);
            MIR_reg_t method_name = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            MIR_op_t args_op = args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0);

            // Get receiver type
            MIR_reg_t rtype = jm_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));

            MIR_reg_t result = jm_new_reg(mt, "mcall", MIR_T_I64);
            MIR_label_t l_string = jm_new_label(mt);
            MIR_label_t l_array = jm_new_label(mt);
            MIR_label_t l_dom = jm_new_label(mt);
            MIR_label_t l_fallback = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);

            // if type == STRING
            MIR_reg_t is_str = jm_new_reg(mt, "isstr", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_str),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_STRING)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_string),
                MIR_new_reg_op(mt->ctx, is_str)));

            // if type == ARRAY
            MIR_reg_t is_arr = jm_new_reg(mt, "isarr", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_arr),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_array),
                MIR_new_reg_op(mt->ctx, is_arr)));

            // if type == MAP && is_dom_node
            MIR_reg_t is_map = jm_new_reg(mt, "ismap", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fallback),
                MIR_new_reg_op(mt->ctx, is_map)));
            MIR_reg_t is_dom = jm_call_1(mt, "js_is_dom_node", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_dom),
                MIR_new_reg_op(mt->ctx, is_dom)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_fallback)));

            // STRING path
            jm_emit_label(mt, l_string);
            {
                MIR_reg_t r = jm_call_4(mt, "js_string_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // ARRAY path
            jm_emit_label(mt, l_array);
            {
                MIR_reg_t r = jm_call_4(mt, "js_array_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // DOM path
            jm_emit_label(mt, l_dom);
            {
                MIR_reg_t r = jm_call_4(mt, "js_dom_element_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Fallback: property access + js_call_function
            jm_emit_label(mt, l_fallback);
            {
                MIR_reg_t fn = jm_call_2(mt, "js_property_access", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name));
                MIR_reg_t r = jm_call_4(mt, "js_call_function", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit_label(mt, l_end);
            return result;
        }
    }

    // Direct function call: identifier(args)
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;

        // Global builtin functions
        if (id->name) {
            const char* n = id->name->chars;
            int nl = (int)id->name->len;

            // parseInt(str, radix?)
            if (nl == 8 && strncmp(n, "parseInt", 8) == 0) {
                MIR_reg_t str = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                MIR_reg_t radix = (call->arguments && call->arguments->next)
                    ? jm_transpile_box_item(mt, call->arguments->next)
                    : jm_box_int_const(mt, 10);
                return jm_call_2(mt, "js_parseInt", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, str),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, radix));
            }
            // parseFloat(str)
            if (nl == 10 && strncmp(n, "parseFloat", 10) == 0) {
                MIR_reg_t str = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_parseFloat", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, str));
            }
            // isNaN(val)
            if (nl == 5 && strncmp(n, "isNaN", 5) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_isNaN", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // isFinite(val)
            if (nl == 8 && strncmp(n, "isFinite", 8) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_isFinite", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // Number(val)
            if (nl == 6 && strncmp(n, "Number", 6) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_unary_plus", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // String(val) — toString
            if (nl == 6 && strncmp(n, "String", 6) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_to_string_val", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // String.fromCharCode(code)
            if (nl == 6 && strncmp(n, "String", 6) == 0) {
                // Already handled above; this is a member expression path
            }
        }

        NameEntry* entry = js_scope_lookup(mt->tp, id->name);

        if (entry && entry->node &&
            ((JsAstNode*)entry->node)->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)entry->node;
            JsFuncCollected* fc = jm_find_collected_func(mt, fn);
            if (fc && fc->func_item) {
                // Direct call to local function
                int param_count = jm_count_params(fn);

                // Build proto for this call site
                char p_name[160];
                snprintf(p_name, sizeof(p_name), "%s_cp%d", fc->name, mt->label_counter++);
                MIR_var_t* p_args = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
                for (int i = 0; i < param_count; i++) {
                    p_args[i] = {MIR_T_I64, "a", 0};
                }
                MIR_type_t res_types[1] = {MIR_T_I64};
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, p_name, 1, res_types, param_count, p_args);

                // Build call operands: proto, func_ref, result, args...
                int nops = 3 + param_count;
                MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
                int oi = 0;
                ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
                ops[oi++] = MIR_new_ref_op(mt->ctx, fc->func_item);
                MIR_reg_t result = jm_new_reg(mt, "dcall", MIR_T_I64);
                ops[oi++] = MIR_new_reg_op(mt->ctx, result);

                JsAstNode* arg = call->arguments;
                for (int i = 0; i < param_count; i++) {
                    if (arg) {
                        MIR_reg_t val = jm_transpile_box_item(mt, arg);
                        ops[oi++] = MIR_new_reg_op(mt->ctx, val);
                        arg = arg->next;
                    } else {
                        MIR_reg_t null_val = jm_emit_null(mt);
                        ops[oi++] = MIR_new_reg_op(mt->ctx, null_val);
                    }
                }

                jm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
                return result;
            }
        }
    }

    // Fallback: evaluate callee, build args array, call js_call_function
    MIR_reg_t callee = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    MIR_reg_t null_this = jm_emit_null(mt);
    return jm_call_4(mt, "js_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, null_this),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
}

// Member expression
static MIR_reg_t jm_transpile_member(JsMirTranspiler* mt, JsMemberNode* mem) {
    // document.property
    if (!mem->computed && mem->object &&
        mem->object->node_type == JS_AST_NODE_IDENTIFIER &&
        mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* obj = (JsIdentifierNode*)mem->object;
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;

        // document.<prop>
        if (obj->name && obj->name->len == 8 && strncmp(obj->name->chars, "document", 8) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_document_get_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }

        // Math.<prop>
        if (obj->name && obj->name->len == 4 && strncmp(obj->name->chars, "Math", 4) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_math_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }

        // process.argv
        if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
            prop->name && prop->name->len == 4 && strncmp(prop->name->chars, "argv", 4) == 0) {
            return jm_call_0(mt, "js_get_process_argv", MIR_T_I64);
        }

        // Number.MAX_SAFE_INTEGER, Number.MIN_SAFE_INTEGER, etc. → js_number_property
        if (obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "Number", 6) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_number_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }
    }

    // obj.style.X -> js_dom_get_style_property(obj, "X")
    if (!mem->computed && mem->object &&
        mem->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION &&
        mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsMemberNode* outer = (JsMemberNode*)mem->object;
        if (!outer->computed && outer->property &&
            outer->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* mid = (JsIdentifierNode*)outer->property;
            if (mid->name && mid->name->len == 5 && strncmp(mid->name->chars, "style", 5) == 0) {
                JsIdentifierNode* sp = (JsIdentifierNode*)mem->property;
                MIR_reg_t obj = jm_transpile_box_item(mt, outer->object);
                MIR_reg_t key = jm_box_string_literal(mt, sp->name->chars, sp->name->len);
                return jm_call_2(mt, "js_dom_get_style_property", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            }
        }
    }

    // .length -> i2it(fn_len(obj))
    if (!mem->computed && mem->property &&
        mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        if (prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "length", 6) == 0) {
            MIR_reg_t obj = jm_transpile_box_item(mt, mem->object);
            MIR_reg_t len = jm_call_1(mt, "fn_len", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj));
            return jm_box_int_reg(mt, len);
        }
    }

    // General property access: js_property_access(obj, key)
    MIR_reg_t obj = jm_transpile_box_item(mt, mem->object);
    MIR_reg_t key;
    if (mem->computed) {
        key = jm_transpile_box_item(mt, mem->property);
    } else if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
    } else {
        key = jm_transpile_box_item(mt, mem->property);
    }

    return jm_call_2(mt, "js_property_access", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
}

// Array expression
static MIR_reg_t jm_transpile_array(JsMirTranspiler* mt, JsArrayNode* arr) {
    MIR_reg_t array = jm_call_1(mt, "js_array_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, arr->length));

    JsAstNode* elem = arr->elements;
    int index = 0;
    while (elem) {
        MIR_reg_t idx = jm_box_int_const(mt, index);
        MIR_reg_t val = jm_transpile_box_item(mt, elem);
        jm_call_3(mt, "js_array_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        elem = elem->next;
        index++;
    }

    return array;
}

// Object expression
static MIR_reg_t jm_transpile_object(JsMirTranspiler* mt, JsObjectNode* obj) {
    MIR_reg_t object = jm_call_0(mt, "js_new_object", MIR_T_I64);

    JsAstNode* prop = obj->properties;
    while (prop) {
        if (prop->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* p = (JsPropertyNode*)prop;
            MIR_reg_t key;
            if (p->computed) {
                key = jm_transpile_box_item(mt, p->key);
            } else if (p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)p->key;
                key = jm_box_string_literal(mt, id->name->chars, id->name->len);
            } else {
                key = jm_transpile_box_item(mt, p->key);
            }
            MIR_reg_t val = jm_transpile_box_item(mt, p->value);
            jm_call_3(mt, "js_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, object),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        prop = prop->next;
    }

    return object;
}

// Conditional expression (ternary)
static MIR_reg_t jm_transpile_conditional(JsMirTranspiler* mt, JsConditionalNode* cond) {
    MIR_reg_t test = jm_transpile_box_item(mt, cond->test);
    MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));

    MIR_reg_t result = jm_new_reg(mt, "tern", MIR_T_I64);
    MIR_label_t l_false = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, truthy)));

    MIR_reg_t cons = jm_transpile_box_item(mt, cond->consequent);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, cons)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    jm_emit_label(mt, l_false);
    MIR_reg_t alt = jm_transpile_box_item(mt, cond->alternate);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, alt)));

    jm_emit_label(mt, l_end);
    return result;
}

// Template literal
static MIR_reg_t jm_transpile_template_literal(JsMirTranspiler* mt, JsTemplateLiteralNode* tmpl) {
    // Get pool pointer from _lambda_rt for StringBuf allocation
    // Load _lambda_rt import
    JsMirImportEntry* rt_ie = jm_ensure_import(mt, "_lambda_rt", MIR_T_I64, 0, NULL, 0);
    MIR_reg_t rt_addr = jm_new_reg(mt, "rt_addr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, rt_addr),
        MIR_new_ref_op(mt->ctx, rt_ie->import)));
    // Load _lambda_rt pointer: Context* rt = *(Context**)rt_addr
    MIR_reg_t rt_ptr = jm_new_reg(mt, "rt_ptr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, rt_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr, 0, 1)));
    // Load rt->pool (offset = offsetof(Context, pool))
    MIR_reg_t pool_reg = jm_new_reg(mt, "pool", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, pool_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, pool), rt_ptr, 0, 1)));

    // Create StringBuf: stringbuf_new(pool)
    MIR_reg_t sb = jm_call_1(mt, "stringbuf_new", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, pool_reg));

    JsAstNode* quasi = tmpl->quasis;
    JsAstNode* expr = tmpl->expressions;

    while (quasi) {
        if (quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            JsTemplateElementNode* elem = (JsTemplateElementNode*)quasi;
            if (elem->cooked && elem->cooked->len > 0) {
                // Intern the template text
                String* interned = name_pool_create_len(mt->tp->name_pool,
                    elem->cooked->chars, elem->cooked->len);
                MIR_reg_t str_ptr = jm_new_reg(mt, "tstr", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, str_ptr),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
                // stringbuf_append_str(sb, str)
                MIR_var_t app_args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
                JsMirImportEntry* app_ie = jm_ensure_import(mt, "stringbuf_append_str", MIR_T_I64, 2, app_args, 0);
                jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
                    MIR_new_ref_op(mt->ctx, app_ie->proto),
                    MIR_new_ref_op(mt->ctx, app_ie->import),
                    MIR_new_reg_op(mt->ctx, sb),
                    MIR_new_reg_op(mt->ctx, str_ptr)));
            }
        }

        // Interpolated expression
        if (expr && quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT &&
            !((JsTemplateElementNode*)quasi)->tail) {
            MIR_reg_t eval = jm_transpile_box_item(mt, expr);
            // Convert to string: js_to_string(value)
            MIR_reg_t str_item = jm_call_1(mt, "js_to_string", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, eval));
            // Unbox string: it2s(str_item) -> String*
            MIR_reg_t str_ptr = jm_call_1(mt, "it2s", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, str_item));
            // Load String.chars and String.len
            MIR_reg_t chars = jm_new_reg(mt, "chars", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, chars),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(String, chars), str_ptr, 0, 1)));
            MIR_reg_t len = jm_new_reg(mt, "slen", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, len),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(String, len), str_ptr, 0, 1)));
            // stringbuf_append_str_n(sb, chars, len)
            jm_call_void_3(mt, "stringbuf_append_str_n",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, sb),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, chars),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, len));
            expr = expr->next;
        }

        quasi = quasi->next;
    }

    // stringbuf_to_string(sb) -> String*
    MIR_reg_t result_str = jm_call_1(mt, "stringbuf_to_string", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, sb));
    // Box as string
    return jm_box_string(mt, result_str);
}

// Function expression / arrow function
static MIR_reg_t jm_transpile_func_expr(JsMirTranspiler* mt, JsFunctionNode* fn) {
    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->func_item) {
        log_error("js-mir: function expression not found in collected functions");
        return jm_emit_null(mt);
    }

    int param_count = jm_count_params(fn);

    // js_new_function((void*)func_item, param_count)
    return jm_call_2(mt, "js_new_function", MIR_T_I64,
        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
        MIR_T_I64, MIR_new_int_op(mt->ctx, param_count));
}

// ============================================================================
// Box item dispatcher: returns register containing boxed Item
// ============================================================================

static MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item) {
    if (!item) return jm_emit_null(mt);

    // Identifiers are already boxed Items
    if (item->node_type == JS_AST_NODE_IDENTIFIER) {
        return jm_transpile_identifier(mt, (JsIdentifierNode*)item);
    }

    // Expressions that return Item through runtime functions - transpile directly
    switch (item->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION:
    case JS_AST_NODE_UNARY_EXPRESSION:
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_MEMBER_EXPRESSION:
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_OBJECT_EXPRESSION:
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return jm_transpile_expression(mt, item);
    default:
        break;
    }

    // Type-based boxing for literals
    if (item->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)item;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER: {
            double val = lit->value.number_value;
            if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0) {
                return jm_box_int_const(mt, (int64_t)val);
            }
            MIR_reg_t d = jm_new_reg(mt, "dbl", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, d),
                MIR_new_double_op(mt->ctx, val)));
            return jm_box_float(mt, d);
        }
        case JS_LITERAL_STRING: {
            return jm_box_string_literal(mt, lit->value.string_value->chars,
                lit->value.string_value->len);
        }
        case JS_LITERAL_BOOLEAN: {
            MIR_reg_t r = jm_new_reg(mt, "bool", MIR_T_I64);
            uint64_t bval = lit->value.boolean_value ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(mt->ctx, (int64_t)bval)));
            return r;
        }
        case JS_LITERAL_NULL:
        case JS_LITERAL_UNDEFINED:
            return jm_emit_null(mt);
        }
    }

    // If type info available, box based on type
    if (item->type) {
        switch (item->type->type_id) {
        case LMD_TYPE_NULL:
            return jm_emit_null(mt);
        case LMD_TYPE_INT: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_int_reg(mt, raw);
        }
        case LMD_TYPE_FLOAT: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_float(mt, raw);
        }
        case LMD_TYPE_BOOL: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            MIR_reg_t result = jm_new_reg(mt, "boxb", MIR_T_I64);
            uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG),
                MIR_new_reg_op(mt->ctx, raw)));
            return result;
        }
        case LMD_TYPE_STRING: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_string(mt, raw);
        }
        default:
            // Already boxed or unknown - just transpile as-is
            return jm_transpile_expression(mt, item);
        }
    }

    // Fallback
    return jm_transpile_expression(mt, item);
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr) {
    if (!expr) return jm_emit_null(mt);

    switch (expr->node_type) {
    case JS_AST_NODE_LITERAL:
        return jm_transpile_literal(mt, (JsLiteralNode*)expr);
    case JS_AST_NODE_IDENTIFIER:
        return jm_transpile_identifier(mt, (JsIdentifierNode*)expr);
    case JS_AST_NODE_BINARY_EXPRESSION:
        return jm_transpile_binary(mt, (JsBinaryNode*)expr);
    case JS_AST_NODE_UNARY_EXPRESSION:
        return jm_transpile_unary(mt, (JsUnaryNode*)expr);
    case JS_AST_NODE_CALL_EXPRESSION:
        return jm_transpile_call(mt, (JsCallNode*)expr);
    case JS_AST_NODE_MEMBER_EXPRESSION:
        return jm_transpile_member(mt, (JsMemberNode*)expr);
    case JS_AST_NODE_ARRAY_EXPRESSION:
        return jm_transpile_array(mt, (JsArrayNode*)expr);
    case JS_AST_NODE_OBJECT_EXPRESSION:
        return jm_transpile_object(mt, (JsObjectNode*)expr);
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        return jm_transpile_func_expr(mt, (JsFunctionNode*)expr);
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
        return jm_transpile_conditional(mt, (JsConditionalNode*)expr);
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return jm_transpile_template_literal(mt, (JsTemplateLiteralNode*)expr);
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
        return jm_transpile_assignment(mt, (JsAssignmentNode*)expr);
    case JS_AST_NODE_NEW_EXPRESSION:
        return jm_transpile_new_expr(mt, (JsCallNode*)expr);
    default:
        log_error("js-mir: unsupported expression type %d", expr->node_type);
        return jm_emit_null(mt);
    }
}

// ============================================================================
// Statement transpilers
// ============================================================================

static void jm_transpile_var_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* var) {
    JsAstNode* decl = var->declarations;
    while (decl) {
        if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);

                MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                if (d->init) {
                    log_debug("var-decl: '%s' init node_type=%d", vname, d->init->node_type);
                    MIR_reg_t val = jm_transpile_box_item(mt, d->init);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, reg),
                        MIR_new_reg_op(mt->ctx, val)));
                } else {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, reg),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
                }
                jm_set_var(mt, vname, reg);
            }
        }
        decl = decl->next;
    }
}

static void jm_transpile_if(JsMirTranspiler* mt, JsIfNode* if_node) {
    MIR_reg_t test = jm_transpile_box_item(mt, if_node->test);
    MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));

    MIR_label_t l_else = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, truthy)));

    // Consequent
    if (if_node->consequent) {
        if (if_node->consequent->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)if_node->consequent;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, if_node->consequent);
        }
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Alternate
    jm_emit_label(mt, l_else);
    if (if_node->alternate) {
        if (if_node->alternate->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)if_node->alternate;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, if_node->alternate);
        }
    }
    jm_emit_label(mt, l_end);
}

static void jm_transpile_while(JsMirTranspiler* mt, JsWhileNode* wh) {
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_test;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    jm_emit_label(mt, l_test);
    MIR_reg_t test = jm_transpile_box_item(mt, wh->test);
    MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, truthy)));

    // Body
    if (wh->body) {
        if (wh->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)wh->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, wh->body);
        }
    }

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

static void jm_transpile_for(JsMirTranspiler* mt, JsForNode* for_node) {
    jm_push_scope(mt);

    // Init
    if (for_node->init) {
        jm_transpile_statement(mt, for_node->init);
    }

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_update;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    jm_emit_label(mt, l_test);

    // Test
    if (for_node->test) {
        MIR_reg_t test = jm_transpile_box_item(mt, for_node->test);
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // Body
    if (for_node->body) {
        if (for_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)for_node->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, for_node->body);
        }
    }

    // Update
    jm_emit_label(mt, l_update);
    if (for_node->update) {
        jm_transpile_box_item(mt, for_node->update);
    }

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

// new expression: new TypedArray(len), new Array(len), new Object()
static MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee) return jm_emit_null(mt);

    const char* ctor_name = NULL;
    int ctor_len = 0;
    if (call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
        ctor_name = id->name->chars;
        ctor_len = (int)id->name->len;
    }

    if (!ctor_name) {
        log_error("js-mir: new expression with non-identifier constructor");
        return jm_emit_null(mt);
    }

    // Evaluate first argument (length for typed arrays / Array)
    MIR_reg_t first_arg = 0;
    int arg_count = jm_count_args(call->arguments);
    if (call->arguments) {
        first_arg = jm_transpile_box_item(mt, call->arguments);
    }

    // Typed arrays: Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array
    int typed_array_type = -1;
    if (ctor_len == 10 && strncmp(ctor_name, "Int32Array", 10) == 0) typed_array_type = 4; // JS_TYPED_INT32
    else if (ctor_len == 10 && strncmp(ctor_name, "Int16Array", 10) == 0) typed_array_type = 2; // JS_TYPED_INT16
    else if (ctor_len == 9 && strncmp(ctor_name, "Int8Array", 9) == 0) typed_array_type = 0; // JS_TYPED_INT8
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint32Array", 11) == 0) typed_array_type = 5; // JS_TYPED_UINT32
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint16Array", 11) == 0) typed_array_type = 3; // JS_TYPED_UINT16
    else if (ctor_len == 10 && strncmp(ctor_name, "Uint8Array", 10) == 0) typed_array_type = 1; // JS_TYPED_UINT8
    else if (ctor_len == 12 && strncmp(ctor_name, "Float64Array", 12) == 0) typed_array_type = 7; // JS_TYPED_FLOAT64
    else if (ctor_len == 12 && strncmp(ctor_name, "Float32Array", 12) == 0) typed_array_type = 6; // JS_TYPED_FLOAT32

    if (typed_array_type >= 0) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_box_int_const(mt, 0);
        return jm_call_2(mt, "js_typed_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, typed_array_type),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg));
    }

    // new Array(len)
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_box_int_const(mt, 0);
        return jm_call_1(mt, "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg));
    }

    // new Object()
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) {
        return jm_call_0(mt, "js_new_object", MIR_T_I64);
    }

    // Fallback: user-defined constructor — look up and call like a regular function
    log_info("js-mir: new %.*s — treating as function call", ctor_len, ctor_name);
    MIR_reg_t callee = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    MIR_reg_t null_this = jm_emit_null(mt);
    return jm_call_4(mt, "js_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, null_this),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
}

// switch statement
static void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw) {
    jm_push_scope(mt);

    MIR_reg_t discriminant = jm_transpile_box_item(mt, sw->discriminant);
    MIR_label_t l_end = jm_new_label(mt);

    // Push break label for the switch (break exits the switch)
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = 0; // no continue in switch
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Collect case labels and default
    int case_count = 0;
    JsSwitchCaseNode* cases[128];
    JsAstNode* c = sw->cases;
    while (c && case_count < 128) {
        cases[case_count++] = (JsSwitchCaseNode*)c;
        c = c->next;
    }

    // Generate labels for each case body
    MIR_label_t case_labels[128];
    for (int i = 0; i < case_count; i++) {
        case_labels[i] = jm_new_label(mt);
    }

    // Test phase: for each non-default case, compare discriminant with test value
    // and branch to the corresponding case body label
    int default_idx = -1;
    for (int i = 0; i < case_count; i++) {
        if (!cases[i]->test) {
            default_idx = i;
            continue;
        }
        MIR_reg_t test_val = jm_transpile_box_item(mt, cases[i]->test);
        MIR_reg_t eq = jm_call_2(mt, "js_strict_equal", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, discriminant),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test_val));
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, eq));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, case_labels[i]),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // If no case matched, jump to default or end
    if (default_idx >= 0) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, case_labels[default_idx])));
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    }

    // Body phase: emit each case body with fall-through semantics
    for (int i = 0; i < case_count; i++) {
        jm_emit_label(mt, case_labels[i]);
        JsAstNode* s = cases[i]->consequent;
        while (s) {
            jm_transpile_statement(mt, s);
            s = s->next;
        }
        // Fall through to next case (break will jump to l_end)
    }

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

// do-while statement
static void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw) {
    MIR_label_t l_body = jm_new_label(mt);
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_test;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Body first
    jm_emit_label(mt, l_body);
    if (dw->body) {
        if (dw->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)dw->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, dw->body);
        }
    }

    // Test
    jm_emit_label(mt, l_test);
    if (dw->test) {
        MIR_reg_t test = jm_transpile_box_item(mt, dw->test);
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_body),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
}

// for-of / for-in statement
// Uses fn_len + js_property_access for arrays, or js_object_keys for objects
static void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo) {
    jm_push_scope(mt);

    // Get loop variable name
    const char* var_name = NULL;
    int var_len = 0;
    if (fo->left && fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)fo->left;
        if (decl->declarations && decl->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl->declarations;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                var_name = id->name->chars;
                var_len = (int)id->name->len;
            }
        }
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)fo->left;
        var_name = id->name->chars;
        var_len = (int)id->name->len;
    }

    if (!var_name) {
        log_error("js-mir: for-of/for-in missing loop variable");
        jm_pop_scope(mt);
        return;
    }

    // Create loop variable
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", var_len, var_name);
    MIR_reg_t loop_var = jm_new_reg(mt, vname, MIR_T_I64);
    jm_set_var(mt, vname, loop_var);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, loop_var),
        MIR_new_int_op(mt->ctx, ITEM_NULL_VAL)));

    // Evaluate right-hand side (the iterable)
    MIR_reg_t iterable = jm_transpile_box_item(mt, fo->right);

    // For for-in: get keys array first
    bool is_for_in = (fo->base.node_type == JS_AST_NODE_FOR_IN_STATEMENT);
    MIR_reg_t collection = iterable;
    if (is_for_in) {
        collection = jm_call_1(mt, "js_object_keys", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));
    }

    // Get length
    MIR_reg_t len = jm_call_1(mt, "fn_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, collection));

    // Index counter
    MIR_reg_t idx = jm_new_reg(mt, "foridx", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_update;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Test: idx < len
    jm_emit_label(mt, l_test);
    MIR_reg_t cmp = jm_new_reg(mt, "foricmp", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get current element: collection[idx]
    MIR_reg_t idx_item = jm_box_int_reg(mt, idx);
    MIR_reg_t elem = jm_call_2(mt, "js_property_access", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, collection),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_item));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, elem)));

    // Body
    if (fo->body) {
        if (fo->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)fo->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, fo->body);
        }
    }

    // Update: idx++
    jm_emit_label(mt, l_update);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

static void jm_transpile_return(JsMirTranspiler* mt, JsReturnNode* ret) {
    MIR_reg_t val;
    if (ret->argument) {
        val = jm_transpile_box_item(mt, ret->argument);
    } else {
        val = jm_emit_null(mt);
    }
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
}

// Statement dispatcher
static void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION:
        jm_transpile_var_decl(mt, (JsVariableDeclarationNode*)stmt);
        break;
    case JS_AST_NODE_FUNCTION_DECLARATION:
        // Already handled in pre-pass; skip
        break;
    case JS_AST_NODE_IF_STATEMENT:
        jm_transpile_if(mt, (JsIfNode*)stmt);
        break;
    case JS_AST_NODE_WHILE_STATEMENT:
        jm_transpile_while(mt, (JsWhileNode*)stmt);
        break;
    case JS_AST_NODE_FOR_STATEMENT:
        jm_transpile_for(mt, (JsForNode*)stmt);
        break;
    case JS_AST_NODE_DO_WHILE_STATEMENT:
        jm_transpile_do_while(mt, (JsDoWhileNode*)stmt);
        break;
    case JS_AST_NODE_SWITCH_STATEMENT:
        jm_transpile_switch(mt, (JsSwitchNode*)stmt);
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT:
        jm_transpile_for_of(mt, (JsForOfNode*)stmt);
        break;
    case JS_AST_NODE_RETURN_STATEMENT:
        jm_transpile_return(mt, (JsReturnNode*)stmt);
        break;
    case JS_AST_NODE_BREAK_STATEMENT:
        if (mt->loop_depth > 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        break;
    case JS_AST_NODE_CONTINUE_STATEMENT:
        if (mt->loop_depth > 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        break;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        jm_push_scope(mt);
        JsBlockNode* blk = (JsBlockNode*)stmt;
        JsAstNode* s = blk->statements;
        while (s) { jm_transpile_statement(mt, s); s = s->next; }
        jm_pop_scope(mt);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
        if (es->expression) {
            jm_transpile_box_item(mt, es->expression);
        }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        // Simplified: just execute the try block, skip catch/finally for now
        // Full try/catch would require MIR exception handling or setjmp/longjmp
        JsTryNode* try_node = (JsTryNode*)stmt;
        if (try_node->block && try_node->block->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)try_node->block;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        }
        // Execute finally block if present
        if (try_node->finalizer && try_node->finalizer->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* fin = (JsBlockNode*)try_node->finalizer;
            JsAstNode* s = fin->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        // Log and return error for now
        log_debug("js-mir: throw statement encountered (simplified)");
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        // Simplified: create constructor that returns empty object
        log_debug("js-mir: class declaration (simplified)");
        break;
    }
    default:
        log_error("js-mir: unsupported statement type %d", stmt->node_type);
        break;
    }
}

// ============================================================================
// Function definition transpiler
// ============================================================================

static void jm_define_function(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int param_count = jm_count_params(fn);

    // Create MIR function: Item fn_name(Item p1, Item p2, ...)
    MIR_var_t* params = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
    // Create unique parameter names
    char** param_names = (char**)alloca(param_count * sizeof(char*));
    JsAstNode* param_node = fn->params;
    for (int i = 0; i < param_count; i++) {
        param_names[i] = (char*)alloca(128);
        if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
            snprintf(param_names[i], 128, "_js_%.*s", (int)pid->name->len, pid->name->chars);
        } else {
            snprintf(param_names[i], 128, "_js_p%d", i);
        }
        params[i] = {MIR_T_I64, param_names[i], 0};
        param_node = param_node ? param_node->next : NULL;
    }

    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, fc->name, 1, &ret_type, param_count, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);

    fc->func_item = func_item;
    jm_register_local_func(mt, fc->name, func_item);

    // Save transpiler state
    MIR_item_t saved_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    int saved_scope_depth = mt->scope_depth;
    int saved_loop_depth = mt->loop_depth;

    mt->current_func_item = func_item;
    mt->current_func = func;
    mt->loop_depth = 0;

    // Set up scope with parameters
    jm_push_scope(mt);
    param_node = fn->params;
    for (int i = 0; i < param_count; i++) {
        if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
            MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);
            jm_set_var(mt, vname, preg);
        }
        param_node = param_node ? param_node->next : NULL;
    }

    // Transpile body
    if (fn->body) {
        if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)fn->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            // Expression body (arrow function)
            MIR_reg_t val = jm_transpile_box_item(mt, fn->body);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
            goto finish_func;
        }
    }

    // Implicit return ITEM_NULL
    {
        MIR_reg_t null_val = jm_emit_null(mt);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, null_val)));
    }

finish_func:
    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);

    // Restore state
    mt->current_func_item = saved_item;
    mt->current_func = saved_func;
    mt->scope_depth = saved_scope_depth;
    mt->loop_depth = saved_loop_depth;
}

// ============================================================================
// AST root transpilation
// ============================================================================

void transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root) {
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("js-mir: expected program node");
        return;
    }

    JsProgramNode* program = (JsProgramNode*)root;

    // Phase 1: Collect all functions (post-order: innermost first)
    jm_collect_functions(mt, root);
    log_debug("js-mir: collected %d functions", mt->func_count);

    // Phase 2: Define all collected functions (innermost first)
    for (int i = 0; i < mt->func_count; i++) {
        jm_define_function(mt, &mt->func_entries[i]);
    }

    // Phase 3: Create js_main(Context* ctx) -> Item
    MIR_var_t main_vars[] = {{MIR_T_P, "ctx", 0}};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(mt->ctx, "js_main", 1, &main_ret, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(mt->ctx, main_item);
    mt->current_func_item = main_item;
    mt->current_func = main_func;

    jm_push_scope(mt);

    // Initialize result register
    MIR_reg_t result = jm_new_reg(mt, "result", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));

    // Emit variable bindings for named function declarations (so they can be
    // used as first-class values, e.g., passed as callbacks)
    JsAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name && fn->name->chars) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item) {
                    int pc = jm_count_params(fn);
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t fn_item = jm_call_2(mt, "js_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    jm_set_var(mt, vname, var_reg);
                }
            }
        }
        stmt = stmt->next;
    }

    // Transpile top-level statements
    stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            // Already handled above (variable binding)
            stmt = stmt->next;
            continue;
        }

        if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
            JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
            if (es->expression) {
                MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, val)));
            }
        } else {
            jm_transpile_statement(mt, stmt);
        }

        stmt = stmt->next;
    }

    // Return result
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, result)));

    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);
    MIR_finish_module(mt->ctx);

    // Load module for linking
    MIR_load_module(mt->ctx, mt->module);
}

// ============================================================================
// Public entry point for JS transpilation via direct MIR generation
// ============================================================================

Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename) {
    log_debug("js-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");

    // Create JS transpiler (for parsing and AST building)
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // Parse JavaScript source
    if (!js_transpiler_parse(tp, js_source, strlen(js_source))) {
        log_error("js-mir: parse failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // Build JavaScript AST
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Initialize MIR context
    MIR_context_t ctx = jit_init(2);
    if (!ctx) {
        log_error("js-mir: MIR context init failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Set up MIR transpiler
    JsMirTranspiler mt;
    memset(&mt, 0, sizeof(mt));
    mt.tp = tp;
    mt.ctx = ctx;
    mt.import_cache = hashmap_new(sizeof(JsImportCacheEntry), 64, 0, 0,
        js_import_cache_hash, js_import_cache_cmp, NULL, NULL);
    mt.local_funcs = hashmap_new(sizeof(JsLocalFuncEntry), 32, 0, 0,
        js_local_func_hash, js_local_func_cmp, NULL, NULL);
    mt.var_scopes[0] = hashmap_new(sizeof(JsVarScopeEntry), 16, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
    mt.scope_depth = 0;

    // Create module
    mt.module = MIR_new_module(ctx, "js_script");

    // Transpile AST to MIR
    transpile_js_mir_ast(&mt, js_ast);

    // Dump MIR for debugging
    create_dir_recursive("temp");
    FILE* mir_dump = fopen("temp/js_mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }

    // Link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // Find js_main
    typedef Item (*js_main_func_t)(Context*);
    // Use find_func which is declared in transpiler.hpp
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir: failed to find js_main");
        hashmap_free(mt.import_cache);
        hashmap_free(mt.local_funcs);
        for (int i = 0; i <= mt.scope_depth; i++) {
            if (mt.var_scopes[i]) hashmap_free(mt.var_scopes[i]);
        }
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Set up evaluation context (same logic as js_transpiler_compile in js_scope.cpp)
    EvalContext js_context;
    memset(&js_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = gc_nursery_create(0);
        }
    } else {
        js_context.nursery = gc_nursery_create(0);
        context = &js_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
    }

    _lambda_rt = (Context*)context;

    // Create Input context for JS runtime map_put operations
    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    // Set up DOM document context if available
    if (runtime->dom_doc) {
        js_dom_set_document(runtime->dom_doc);
    }

    // Execute
    log_notice("js-mir: executing JIT compiled code");
    Item result = js_main((Context*)context);

    // Handle result (same logic as js_transpiler_compile)
    Item final_result;
    TypeId type_id = get_type_id(result);

    if (reusing_context) {
        final_result = result;
    } else {
        if (type_id == LMD_TYPE_FLOAT) {
            double value = it2d(result);
            if (value == (double)(int64_t)value && value >= INT32_MIN && value <= INT32_MAX) {
                final_result = (Item){.item = i2it((int64_t)value)};
            } else {
                double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *ptr = value;
                final_result = (Item){.item = d2it(ptr)};
            }
        } else {
            final_result = result;
        }
    }

    // Convert JS HashMap objects to VMap for proper printing (before context restore)
    // (no longer needed — JS objects are now Lambda Maps)

    context = old_context;

    // Cleanup
    hashmap_free(mt.import_cache);
    hashmap_free(mt.local_funcs);
    for (int i = 0; i <= mt.scope_depth; i++) {
        if (mt.var_scopes[i]) hashmap_free(mt.var_scopes[i]);
    }
    MIR_finish(ctx);
    js_transpiler_destroy(tp);

    log_debug("js-mir: transpilation completed");
    return final_result;
}
