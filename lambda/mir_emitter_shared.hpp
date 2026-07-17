#pragma once

#include <mir.h>
#include <stdio.h>
#include <string.h>
#include "lambda-data.hpp"
#include "sys_func_registry.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"

struct JsClassEntry;

// Max positional args for an emitted MIR call. 8 accommodates fn_hash_join_tuples
// (join tuple stream + prior keys + rows + row keys + name + optional + index name/values).
#define MIR_SHARED_MAX_CALL_ARGS 8

struct MirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
    JitImportMetadata metadata;
};

struct MirImportCacheEntry {
    char name[128];
    MirImportEntry entry;
};

struct VarEntry {
    MIR_reg_t reg;
    int root_slot;
    int async_slot;
    MIR_type_t mir_type;
    TypeId type_id;
    TypeId elem_type;
    NumSizedType num_type;
    int env_offset;
    bool is_state_var;
    const char* state_name_ptr;
    bool from_env;
    int env_slot;
    MIR_reg_t env_reg;
    bool from_shared_env;
    bool in_scope_env;
    int scope_env_slot;
    MIR_reg_t scope_env_reg;
    int typed_array_type;
    bool is_js_array;
    JsClassEntry* class_entry;
    Type* full_type;
    bool is_let_const;
    bool is_const;
    bool is_nfe_binding;
    bool from_block_func_decl;
    bool from_catch_param;
    bool tdz_active;
    uint32_t binding_start;
    uint32_t binding_end;
    MIR_reg_t hoisted_data_reg;
    MIR_reg_t hoisted_len_reg;
    bool from_hoist;
    bool is_live_default_binding;
    const char* live_binding_specifier;
};

typedef VarEntry MirVarEntry;
typedef VarEntry JsMirVarEntry;

struct VarScopeEntry {
    char name[128];
    VarEntry var;
};

typedef VarScopeEntry JsVarScopeEntry;

static inline int em_var_scope_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((VarScopeEntry*)a)->name, ((VarScopeEntry*)b)->name);
}

static inline uint64_t em_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((VarScopeEntry*)item)->name,
        strlen(((VarScopeEntry*)item)->name), seed0, seed1);
}

static inline struct hashmap* em_var_scope_new(int capacity) {
    return hashmap_new(sizeof(VarScopeEntry), capacity, 0, 0,
        em_var_scope_hash, em_var_scope_cmp, NULL, NULL);
}

static inline int em_import_cache_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((MirImportCacheEntry*)a)->name, ((MirImportCacheEntry*)b)->name);
}

static inline uint64_t em_import_cache_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((MirImportCacheEntry*)item)->name,
        strlen(((MirImportCacheEntry*)item)->name), seed0, seed1);
}

static inline struct hashmap* em_import_cache_new(int capacity) {
    return hashmap_new(sizeof(MirImportCacheEntry), capacity, 0, 0,
        em_import_cache_hash, em_import_cache_cmp, NULL, NULL);
}

static inline MIR_type_t mir_reg_type_for_alloc(MIR_type_t type,
                                                bool coerce_float32) {
    return (type == MIR_T_P || (coerce_float32 && type == MIR_T_F)) ? MIR_T_I64 : type;
}

static inline MIR_reg_t mir_new_numbered_reg(MIR_context_t ctx, MIR_func_t func,
                                             int* reg_counter,
                                             const char* prefix,
                                             MIR_type_t type,
                                             bool coerce_float32 = false) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, (*reg_counter)++);
    return MIR_new_func_reg(ctx, func, mir_reg_type_for_alloc(type, coerce_float32), name);
}

static inline MIR_label_t mir_new_emit_label(MIR_context_t ctx) {
    return MIR_new_label(ctx);
}

static inline void mir_append_emit_insn(MIR_context_t ctx,
                                        MIR_item_t func_item,
                                        MIR_insn_t insn) {
    MIR_append_insn(ctx, func_item, insn);
}

static inline void mir_append_emit_label(MIR_context_t ctx,
                                         MIR_item_t func_item,
                                         MIR_label_t label) {
    MIR_append_insn(ctx, func_item, label);
}

// ============================================================================
// MirEmitter — bundled emit state shared by both transpilers (Lambda + JS).
// Holds the immutable MIR context handle plus the per-function emit cursor
// (current function item/reg-alloc target, register + label counters) and the
// import cache. Both MirTranspiler and JsMirTranspiler embed one and route
// their primitive emit helpers (new_reg / emit_insn / emit_label / calls)
// through the em_* functions below, so the primitives exist once instead of
// once per transpiler (Unified-AST plan Phase 0, P0.1).
// ============================================================================
struct MirEmitter {
    MIR_context_t ctx;            // MIR context — immutable after init (cached from owner)
    MIR_item_t func_item;         // current function item — emit target cursor
    MIR_func_t func;              // current function — register-allocation target
    int reg_counter;              // monotonic register-id source
    int label_counter;            // monotonic label/proto-id source
    struct hashmap* import_cache; // name -> import (proto+import) memo
    void (*note_mir_call)(const char* name); // optional per-language call telemetry hook
    ArrayList* const_list;        // per-module constant pool, owned by Script/Transpiler
    MIR_reg_t consts_reg;         // register holding const_list->data for current function
    MIR_item_t consts_bss;        // per-module BSS slot holding const_list->data
};

// Boxed scalar returns need a lifetime policy that is independent of their
// machine return type. Both Lambda and LambdaJS use this enum so Item encoding
// changes cannot make their return epilogues drift apart.
enum MirScalarReturnMode {
    MIR_SCALAR_RETURN_NONE,
    MIR_SCALAR_RETURN_FLOAT,
    MIR_SCALAR_RETURN_INT64,
    MIR_SCALAR_RETURN_DTIME,
    MIR_SCALAR_RETURN_DYNAMIC,
};

static inline MirScalarReturnMode em_scalar_return_mode_for_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        return MIR_SCALAR_RETURN_FLOAT;
    case LMD_TYPE_INT64:
        return MIR_SCALAR_RETURN_INT64;
    case LMD_TYPE_DTIME:
        return MIR_SCALAR_RETURN_DTIME;
    case LMD_TYPE_ANY:
        return MIR_SCALAR_RETURN_DYNAMIC;
    default:
        return MIR_SCALAR_RETURN_NONE;
    }
}

static inline MIR_reg_t em_new_reg(MirEmitter* em, const char* prefix,
                                   MIR_type_t type) {
    return mir_new_numbered_reg(em->ctx, em->func, &em->reg_counter, prefix,
                                type, true);
}

static inline MIR_label_t em_new_label(MirEmitter* em) {
    return mir_new_emit_label(em->ctx);
}

static inline void em_emit_insn(MirEmitter* em, MIR_insn_t insn) {
    mir_append_emit_insn(em->ctx, em->func_item, insn);
}

static inline void em_emit_label(MirEmitter* em, MIR_label_t label) {
    mir_append_emit_label(em->ctx, em->func_item, label);
}

static inline MIR_reg_t em_load_frame_top(MirEmitter* em, MIR_reg_t runtime,
                                          size_t context_offset,
                                          const char* prefix) {
    MIR_reg_t result = em_new_reg(em, prefix, MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, result),
        MIR_new_mem_op(em->ctx, MIR_T_I64, (MIR_disp_t)context_offset,
            runtime, 0, 1)));
    return result;
}

static inline void em_store_frame_top(MirEmitter* em, MIR_reg_t runtime,
                                      size_t context_offset, MIR_reg_t value) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, MIR_T_I64, (MIR_disp_t)context_offset,
            runtime, 0, 1),
        MIR_new_reg_op(em->ctx, value)));
}

static inline void em_store_frame_slot(MirEmitter* em, MIR_reg_t frame_base,
                                       int slot, MIR_reg_t value) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, MIR_T_I64,
            (MIR_disp_t)slot * (MIR_disp_t)sizeof(uint64_t),
            frame_base, 0, 1),
        MIR_new_reg_op(em->ctx, value)));
}

static inline MIR_reg_t em_load_frame_slot(MirEmitter* em, MIR_reg_t frame_base,
                                           int slot, const char* prefix) {
    MIR_reg_t result = em_new_reg(em, prefix, MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, result),
        MIR_new_mem_op(em->ctx, MIR_T_I64,
            (MIR_disp_t)slot * (MIR_disp_t)sizeof(uint64_t),
            frame_base, 0, 1)));
    return result;
}

// Re-home a boxed scalar returned from the current number-stack extent. The
// rare arm donates frame_base[0] to the caller, bounding retained storage to
// one slot without an imported capture/rebuild call.
static inline MIR_reg_t em_rehome_scalar_return(MirEmitter* em,
                                                MirScalarReturnMode mode,
                                                MIR_reg_t item,
                                                MIR_reg_t runtime,
                                                size_t number_top_offset,
                                                MIR_reg_t frame_base) {
    if (mode == MIR_SCALAR_RETURN_NONE) {
        em_store_frame_top(em, runtime, number_top_offset, frame_base);
        return item;
    }

    MIR_reg_t current_top = em_load_frame_top(em, runtime, number_top_offset,
        "scalar_top");
    MIR_reg_t payload = em_new_reg(em, "scalar_payload", MIR_T_I64);
    MIR_reg_t result = em_new_reg(em, "scalar_result", MIR_T_I64);
    MIR_label_t classify_done = em_new_label(em);
    MIR_label_t float_case = em_new_label(em);
    MIR_label_t float_tag_case = em_new_label(em);
    MIR_label_t int64_case = em_new_label(em);
    MIR_label_t payload_case = em_new_label(em);
    MIR_label_t donate = em_new_label(em);
    MIR_label_t restore = em_new_label(em);
    MIR_label_t done = em_new_label(em);

    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, payload), MIR_new_int_op(em->ctx, 0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, item)));

    if (mode == MIR_SCALAR_RETURN_DYNAMIC) {
        MIR_reg_t double_bits = em_new_reg(em, "scalar_double", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
            MIR_new_reg_op(em->ctx, double_bits), MIR_new_reg_op(em->ctx, item),
            MIR_new_int_op(em->ctx, (int64_t)ITEM_DBL_MASK)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, classify_done),
            MIR_new_reg_op(em->ctx, double_bits)));

        MIR_reg_t tag = em_new_reg(em, "scalar_tag", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_URSH,
            MIR_new_reg_op(em->ctx, tag), MIR_new_reg_op(em->ctx, item),
            MIR_new_int_op(em->ctx, 56)));
        MIR_reg_t is_type = em_new_reg(em, "scalar_is_type", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
            MIR_new_reg_op(em->ctx, is_type), MIR_new_reg_op(em->ctx, tag),
            MIR_new_int_op(em->ctx, LMD_TYPE_INT64)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, int64_case),
            MIR_new_reg_op(em->ctx, is_type)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
            MIR_new_reg_op(em->ctx, is_type), MIR_new_reg_op(em->ctx, tag),
            MIR_new_int_op(em->ctx, LMD_TYPE_FLOAT)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, float_tag_case),
            MIR_new_reg_op(em->ctx, is_type)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
            MIR_new_reg_op(em->ctx, is_type), MIR_new_reg_op(em->ctx, tag),
            MIR_new_int_op(em->ctx, LMD_TYPE_FLOAT64)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, float_tag_case),
            MIR_new_reg_op(em->ctx, is_type)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
            MIR_new_reg_op(em->ctx, is_type), MIR_new_reg_op(em->ctx, tag),
            MIR_new_int_op(em->ctx, LMD_TYPE_DTIME)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, payload_case),
            MIR_new_reg_op(em->ctx, is_type)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, classify_done)));
    } else if (mode == MIR_SCALAR_RETURN_FLOAT) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, float_case)));
    } else if (mode == MIR_SCALAR_RETURN_INT64) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, int64_case)));
    } else {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, payload_case)));
    }

    em_emit_label(em, float_case);
    MIR_reg_t inline_double = em_new_reg(em, "scalar_inline_double", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, inline_double), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)ITEM_DBL_MASK)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, classify_done),
        MIR_new_reg_op(em->ctx, inline_double)));
    em_emit_label(em, float_tag_case);
    MIR_reg_t is_packed_zero = em_new_reg(em, "scalar_zero", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
        MIR_new_reg_op(em->ctx, is_packed_zero), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)ITEM_FLOAT_P0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, classify_done),
        MIR_new_reg_op(em->ctx, is_packed_zero)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
        MIR_new_reg_op(em->ctx, is_packed_zero), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)ITEM_FLOAT_N0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, classify_done),
        MIR_new_reg_op(em->ctx, is_packed_zero)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, payload_case)));

    em_emit_label(em, int64_case);
    MIR_reg_t inline_int64 = em_new_reg(em, "scalar_inline_int64", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, inline_int64), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)ITEM_INT64_INLINE_MARK)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, classify_done),
        MIR_new_reg_op(em->ctx, inline_int64)));

    em_emit_label(em, payload_case);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, payload), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)UINT64_C(0x00FFFFFFFFFFFFFF))));

    em_emit_label(em, classify_done);
    MIR_reg_t at_or_above_base = em_new_reg(em, "scalar_ge_base", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UGE,
        MIR_new_reg_op(em->ctx, at_or_above_base),
        MIR_new_reg_op(em->ctx, payload), MIR_new_reg_op(em->ctx, frame_base)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF,
        MIR_new_label_op(em->ctx, restore),
        MIR_new_reg_op(em->ctx, at_or_above_base)));
    MIR_reg_t below_top = em_new_reg(em, "scalar_lt_top", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ULT,
        MIR_new_reg_op(em->ctx, below_top), MIR_new_reg_op(em->ctx, payload),
        MIR_new_reg_op(em->ctx, current_top)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, donate), MIR_new_reg_op(em->ctx, below_top)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, restore)));

    em_emit_label(em, donate);
    MIR_reg_t raw = em_new_reg(em, "scalar_raw", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, raw),
        MIR_new_mem_op(em->ctx, MIR_T_I64, 0, payload, 0, 1)));
    em_store_frame_slot(em, frame_base, 0, raw);
    MIR_reg_t tag_bits = em_new_reg(em, "scalar_tag_bits", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, tag_bits), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)ITEM_HIGH_BYTE_MASK)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, tag_bits),
        MIR_new_reg_op(em->ctx, frame_base)));
    MIR_reg_t donated_top = em_new_reg(em, "scalar_donated_top", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD,
        MIR_new_reg_op(em->ctx, donated_top), MIR_new_reg_op(em->ctx, frame_base),
        MIR_new_int_op(em->ctx, (int64_t)sizeof(uint64_t))));
    em_store_frame_top(em, runtime, number_top_offset, donated_top);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, done)));

    em_emit_label(em, restore);
    // Only callee-owned payloads may survive the restore; heap and ancestor
    // pointers deliberately fall through with their original Item unchanged.
    em_store_frame_top(em, runtime, number_top_offset, frame_base);
    em_emit_label(em, done);
    return result;
}

static inline int em_add_const(MirEmitter* em, void* ptr) {
    if (!em || !em->const_list || !ptr) return -1;
    arraylist_append(em->const_list, ptr);
    return em->const_list->length - 1;
}

static inline MIR_reg_t em_load_const(MirEmitter* em, int const_index,
                                      MIR_type_t as_type) {
    MIR_reg_t ptr = em_new_reg(em, "cptr", MIR_T_P);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, ptr),
        MIR_new_mem_op(em->ctx, as_type, const_index * 8, em->consts_reg, 0, 1)));
    return ptr;
}

static inline MIR_reg_t em_load_consts_from_bss(MirEmitter* em) {
    MIR_reg_t bss_addr = em_new_reg(em, "mod_consts_bss", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, bss_addr), MIR_new_ref_op(em->ctx, em->consts_bss)));
    em->consts_reg = em_new_reg(em, "consts", MIR_T_I64);
    // Per-function consts_reg must be loaded from module BSS so cross-module
    // function calls read the callee module's constant pool, not the caller's.
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, em->consts_reg),
        MIR_new_mem_op(em->ctx, MIR_T_I64, 0, bss_addr, 0, 1)));
    return em->consts_reg;
}

static inline void mir_emit_i64_const_to_reg(MIR_context_t ctx,
                                             MIR_item_t func_item,
                                             MIR_reg_t reg,
                                             int64_t value) {
    mir_append_emit_insn(ctx, func_item,
        MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, reg),
            MIR_new_int_op(ctx, value)));
}

static inline void mir_prepare_call_args(MIR_var_t* args,
                                         MIR_type_t* arg_types,
                                         int nargs) {
    static const char* ARG_NAMES[MIR_SHARED_MAX_CALL_ARGS] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    for (int i = 0; i < nargs; i++) {
        args[i].type = arg_types[i];
        args[i].name = ARG_NAMES[i];
        args[i].size = 0;
    }
}

static inline MIR_insn_t mir_new_call_with_args(MIR_context_t ctx,
                                                MIR_item_t proto,
                                                MIR_item_t import,
                                                MIR_reg_t result,
                                                int nargs,
                                                MIR_op_t* arg_ops) {
    MIR_op_t ops[3 + MIR_SHARED_MAX_CALL_ARGS];
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(ctx, proto);
    ops[oi++] = MIR_new_ref_op(ctx, import);
    if (result) {
        ops[oi++] = MIR_new_reg_op(ctx, result);
    }
    for (int i = 0; i < nargs; i++) {
        ops[oi++] = arg_ops[i];
    }
    return MIR_new_insn_arr(ctx, MIR_CALL, oi, ops);
}

static inline void mir_format_import_key(char* out,
                                         size_t out_size,
                                         const char* name,
                                         MIR_type_t ret_type,
                                         int nargs,
                                         MIR_var_t* args,
                                         int nres,
                                         bool include_signature) {
    if (!out || out_size == 0) return;
    if (!include_signature) {
        snprintf(out, out_size, "%s", name);
        return;
    }

    int key_len = snprintf(out, out_size, "%s#r%d#n%d#a%d",
        name, (int)ret_type, nres, nargs);
    for (int i = 0; i < nargs && key_len > 0 && key_len < (int)out_size; i++) {
        key_len += snprintf(out + key_len, out_size - (size_t)key_len,
            "#%d", (int)args[i].type);
    }
}

static inline void mir_format_import_proto_name(char* out,
                                                size_t out_size,
                                                const char* name,
                                                MIR_type_t ret_type,
                                                int nargs,
                                                int nres,
                                                bool include_signature) {
    if (!out || out_size == 0) return;
    if (include_signature) {
        snprintf(out, out_size, "%s_p_r%d_n%d_a%d",
            name, (int)ret_type, nres, nargs);
    } else {
        snprintf(out, out_size, "%s_p", name);
    }
}

static inline void mir_create_import_proto_pair(MIR_context_t ctx,
                                                const char* name,
                                                MIR_type_t ret_type,
                                                int nargs,
                                                MIR_var_t* args,
                                                int nres,
                                                bool include_signature,
                                                MIR_item_t* out_proto,
                                                MIR_item_t* out_import) {
    char proto_name[140];
    mir_format_import_proto_name(proto_name, sizeof(proto_name), name,
        ret_type, nargs, nres, include_signature);

    MIR_type_t res_types[1] = { ret_type };
    if (out_proto) {
        *out_proto = MIR_new_proto_arr(ctx, proto_name, nres, res_types, nargs, args);
    }
    if (out_import) {
        *out_import = MIR_new_import(ctx, name);
    }
}

static inline MirImportEntry* em_ensure_import(MirEmitter* em,
                                               const char* name,
                                               MIR_type_t ret_type,
                                               int nargs,
                                               MIR_var_t* args,
                                               int nres,
                                               bool include_signature) {
    MirImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    mir_format_import_key(key.name, sizeof(key.name), name,
        ret_type, nargs, args, nres, include_signature);

    MirImportCacheEntry* found = (MirImportCacheEntry*)hashmap_get(em->import_cache, &key);
    if (found) return &found->entry;

    MIR_item_t proto = NULL;
    MIR_item_t imp = NULL;
    mir_create_import_proto_pair(em->ctx, name, ret_type, nargs, args, nres,
        include_signature, &proto, &imp);

    MirImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", key.name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    // Unknown imports retain conservative MAY_GC/unknown-value defaults.
    jit_import_get_metadata(name, &new_entry.entry.metadata);
    hashmap_set(em->import_cache, &new_entry);

    found = (MirImportCacheEntry*)hashmap_get(em->import_cache, &key);
    return found ? &found->entry : NULL;
}

static inline MIR_reg_t em_call_with_args(MirEmitter* em,
                                          const char* fn_name,
                                          MIR_type_t ret_type,
                                          int nargs,
                                          MIR_type_t* arg_types,
                                          MIR_op_t* arg_ops,
                                          bool include_signature) {
    if (nargs < 0 || nargs > MIR_SHARED_MAX_CALL_ARGS) return 0;
    if (em->note_mir_call) em->note_mir_call(fn_name);

    MIR_var_t args[MIR_SHARED_MAX_CALL_ARGS];
    if (nargs > 0) {
        mir_prepare_call_args(args, arg_types, nargs);
    }
    MirImportEntry* ie = em_ensure_import(em, fn_name, ret_type, nargs,
        nargs ? args : NULL, 1, include_signature);
    if (!ie) return 0;

    MIR_reg_t res = em_new_reg(em, fn_name, ret_type);
    em_emit_insn(em, mir_new_call_with_args(em->ctx, ie->proto, ie->import,
        res, nargs, arg_ops));
    return res;
}

static inline void em_call_void_with_args(MirEmitter* em,
                                          const char* fn_name,
                                          int nargs,
                                          MIR_type_t* arg_types,
                                          MIR_op_t* arg_ops,
                                          bool include_signature) {
    if (nargs < 0 || nargs > MIR_SHARED_MAX_CALL_ARGS) return;
    if (em->note_mir_call) em->note_mir_call(fn_name);

    MIR_var_t args[MIR_SHARED_MAX_CALL_ARGS];
    if (nargs > 0) {
        mir_prepare_call_args(args, arg_types, nargs);
    }
    MirImportEntry* ie = em_ensure_import(em, fn_name, MIR_T_I64, nargs,
        nargs ? args : NULL, 0, include_signature);
    if (!ie) return;

    em_emit_insn(em, mir_new_call_with_args(em->ctx, ie->proto, ie->import,
        0, nargs, arg_ops));
}

static inline MIR_reg_t em_call_0(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, bool include_signature) {
    return em_call_with_args(em, fn_name, ret_type, 0, NULL, NULL, include_signature);
}

static inline MIR_reg_t em_call_1(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
                                  bool include_signature) {
    MIR_type_t types[1] = {a1t};
    MIR_op_t ops[1] = {a1};
    return em_call_with_args(em, fn_name, ret_type, 1, types, ops, include_signature);
}

static inline MIR_reg_t em_call_2(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
                                  MIR_type_t a2t, MIR_op_t a2, bool include_signature) {
    MIR_type_t types[2] = {a1t, a2t};
    MIR_op_t ops[2] = {a1, a2};
    return em_call_with_args(em, fn_name, ret_type, 2, types, ops, include_signature);
}

static inline MIR_reg_t em_call_3(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
                                  MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
                                  bool include_signature) {
    MIR_type_t types[3] = {a1t, a2t, a3t};
    MIR_op_t ops[3] = {a1, a2, a3};
    return em_call_with_args(em, fn_name, ret_type, 3, types, ops, include_signature);
}

static inline MIR_reg_t em_call_4(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
                                  MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
                                  MIR_type_t a4t, MIR_op_t a4, bool include_signature) {
    MIR_type_t types[4] = {a1t, a2t, a3t, a4t};
    MIR_op_t ops[4] = {a1, a2, a3, a4};
    return em_call_with_args(em, fn_name, ret_type, 4, types, ops, include_signature);
}

static inline MIR_reg_t em_call_5(MirEmitter* em, const char* fn_name,
                                  MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
                                  MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
                                  MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5,
                                  bool include_signature) {
    MIR_type_t types[5] = {a1t, a2t, a3t, a4t, a5t};
    MIR_op_t ops[5] = {a1, a2, a3, a4, a5};
    return em_call_with_args(em, fn_name, ret_type, 5, types, ops, include_signature);
}

static inline void em_call_void_0(MirEmitter* em, const char* fn_name,
                                  bool include_signature) {
    em_call_void_with_args(em, fn_name, 0, NULL, NULL, include_signature);
}

static inline void em_call_void_1(MirEmitter* em, const char* fn_name,
                                  MIR_type_t a1t, MIR_op_t a1, bool include_signature) {
    MIR_type_t types[1] = {a1t};
    MIR_op_t ops[1] = {a1};
    em_call_void_with_args(em, fn_name, 1, types, ops, include_signature);
}

static inline void em_call_void_2(MirEmitter* em, const char* fn_name,
                                  MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
                                  bool include_signature) {
    MIR_type_t types[2] = {a1t, a2t};
    MIR_op_t ops[2] = {a1, a2};
    em_call_void_with_args(em, fn_name, 2, types, ops, include_signature);
}

static inline void em_call_void_3(MirEmitter* em, const char* fn_name,
                                  MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
                                  MIR_type_t a3t, MIR_op_t a3, bool include_signature) {
    MIR_type_t types[3] = {a1t, a2t, a3t};
    MIR_op_t ops[3] = {a1, a2, a3};
    em_call_void_with_args(em, fn_name, 3, types, ops, include_signature);
}

static inline void em_call_void_4(MirEmitter* em, const char* fn_name,
                                  MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
                                  MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4,
                                  bool include_signature) {
    MIR_type_t types[4] = {a1t, a2t, a3t, a4t};
    MIR_op_t ops[4] = {a1, a2, a3, a4};
    em_call_void_with_args(em, fn_name, 4, types, ops, include_signature);
}
