#pragma once

#include <mir.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lambda-data.hpp"
#include "sys_func_registry.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

struct JsClassEntry;

// Max positional args for an emitted MIR call. 8 accommodates fn_hash_join_tuples
// (join tuple stream + prior keys + rows + row keys + name + optional + index name/values).
#define MIR_SHARED_MAX_CALL_ARGS 8

struct MirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
    JitImportMetadata audit;
    JitCallMetadata call;
    JitAbiArg abi_args[MIR_SHARED_MAX_CALL_ARGS];
};

struct MirImportCacheEntry {
    char name[128];
    MirImportEntry entry;
};

struct VarEntry {
    MIR_reg_t reg;
    int root_slot;
    int gc_home_id;
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

static inline JitAbiRep em_abi_rep(MIR_type_t type,
        JitValueClass value_class, bool has_result) {
    if (!has_result) return JIT_ABI_VOID;
    if (type == MIR_T_D || type == MIR_T_F) return JIT_ABI_F64;
    if (type == MIR_T_P) return JIT_ABI_POINTER;
    if (value_class == JIT_VALUE_BOXED_ITEM ||
            value_class == JIT_VALUE_UNKNOWN) return JIT_ABI_ITEM;
    // MIR carriers define ABI representation; value class separately controls
    // rooting because pointer bits may intentionally travel through I64.
    return JIT_ABI_I64;
}

static inline void em_normalize_import_call(MirImportEntry* entry,
        MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    if (!entry) return;
    memset(&entry->call, 0, sizeof(entry->call));
    memset(entry->abi_args, 0, sizeof(entry->abi_args));
    entry->call.abi_args = entry->abi_args;
    entry->call.effects.gc = entry->audit.gc_effect;
    entry->call.effects.reentry = entry->audit.reentry_effect;
    entry->call.effects.exception = entry->audit.exception_effect;
    entry->call.effects.number_stack =
        entry->audit.flags & JIT_IMPORT_NUMBER_STACK_PRESERVES
        ? JIT_NUMBER_STACK_PRESERVES : JIT_NUMBER_STACK_MAY_ALLOCATE;
    JitValueClass ret_class = entry->audit.ret_class;
    entry->call.normal_result.value.abi_rep = em_abi_rep(
        ret_type, ret_class, nres > 0);
    entry->call.normal_result.value.value_class = ret_class;
    entry->call.normal_result.transport = nres > 0
        ? JIT_RETURN_MIR_RESULT : JIT_RETURN_NONE;
    entry->call.normal_result.scalar_class =
        ret_class == JIT_VALUE_BOXED_ITEM &&
        !(entry->audit.flags & JIT_IMPORT_RESULT_SCALAR_STABLE)
        ? SCALAR_RETURN_DYNAMIC : SCALAR_RETURN_NONE;
    entry->call.normal_result.may_use_scalar_return_home =
        entry->call.normal_result.scalar_class != SCALAR_RETURN_NONE;
    entry->call.abi_arg_count = (uint16_t)nargs;
    entry->call.source_arg_count = (uint16_t)nargs;
    entry->call.scalar_return_home_arg_index = -1;
    entry->call.scalar_home_lane_mask =
        entry->call.normal_result.may_use_scalar_return_home ? 1u : 0u;
    for (int i = 0; i < nargs && i < MIR_SHARED_MAX_CALL_ARGS; i++) {
        JitValueClass arg_class = jit_import_arg_class(&entry->audit, i);
        entry->abi_args[i].value.abi_rep = em_abi_rep(
            args[i].type, arg_class, true);
        entry->abi_args[i].value.value_class = arg_class;
        JitArgEffect arg_effect = jit_import_arg_effect(&entry->audit, i);
        entry->abi_args[i].effects = arg_effect != JIT_ARG_EFFECT_UNKNOWN
            ? arg_effect
            : entry->audit.flags & JIT_IMPORT_ARGS_BORROWED_AUDITED
                ? JIT_ARG_BORROWED : JIT_ARG_EFFECT_UNKNOWN;
    }
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
struct MirRootCandidate {
    MIR_reg_t reg;
    JitValueClass value_class;
    int home_id;
};

struct MirGcCallSite {
    MIR_insn_t insn;
    JitGcEffect effect;
    JitExceptionEffect exception_effect;
    bool is_exception_poll;
};

struct MirRootWriteBackResult {
    int stable_slots;
    int scratch_slots;
    int inserted_stores;
    int instruction_count;
    int block_count;
};

enum MirScalarReturnMode {
    MIR_SCALAR_RETURN_NONE,
    MIR_SCALAR_RETURN_FLOAT,
    MIR_SCALAR_RETURN_INT64,
    MIR_SCALAR_RETURN_UINT64,
    MIR_SCALAR_RETURN_DYNAMIC,
};
struct MirRootBinding {
    MIR_reg_t reg;
    int slot;
    int home_id;
};
struct MirPendingRootStore {
    int slot;
    MIR_reg_t value;
    MIR_insn_t definition;
};
struct MirEnvBinding {
    MIR_reg_t source_reg;
    MIR_reg_t reg;
};
enum MirEntryMode {
    MIR_ENTRY_CHECKED,
    MIR_ENTRY_BOUND_INTERNAL,
};
enum MirFrameRefKind {
    MIR_FRAME_REF_NONE,
    MIR_FRAME_REF_LOCAL_SCALAR_HOME,
    MIR_FRAME_REF_DISCARD_SCRATCH,
    MIR_FRAME_REF_FIXED_SCRATCH,
    MIR_FRAME_REF_INCOMING_CALLER_HOME,
};
struct MirFrameRef {
    MirFrameRefKind kind;
    int logical_home_id;
};
enum ScalarPayloadProvenance {
    SCALAR_PROVENANCE_NONE,
    SCALAR_PROVENANCE_INLINE,
    SCALAR_PROVENANCE_HEAP,
    SCALAR_PROVENANCE_ACTIVATION_HOME,
    SCALAR_PROVENANCE_UNKNOWN,
};
struct MirValue {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId semantic_type;
    ValueRep rep;
    JitValueClass value_class;
    int gc_home_id;
    int scalar_home_id;
    ScalarPayloadProvenance scalar_provenance;
};
struct MirCallOptions {
    MirFrameRef scalar_return_home;
    uint8_t observed_return_lane_mask;
    bool is_tail_call;
};
struct MirCallResult {
    MirValue normal;
    MirValue error;
    JitCallEffects effects;
};
struct MirFunctionMetadata {
    const char* debug_name;
    FnEntryKind entry_kind;
    bool bound_context_entry;
    uint16_t root_slot_count;
    uint16_t scalar_home_count;
    uint16_t temporary_number_slot_count;
    uint32_t mir_instruction_count;
    uint32_t safepoint_count;
    uint32_t root_store_count;
};
struct MirScalarHomeBinding {
    MIR_reg_t reg;
    int logical_home_id;
};
struct MirScalarHomeFixup {
    MIR_insn_t address_insn;
    MirFrameRefKind kind;
    int logical_home_id;
};
struct MirFunctionPlan {
    FnEntryKind entry_kind;
    MirEntryMode entry_mode;
    MirScalarReturnMode scalar_return_mode;
    uint8_t scalar_home_lane_mask;
    int fixed_number_scratch_slots;
    bool accepts_caller_scalar_home;
    const char* debug_name;
};

// Parameter registers arrive initialized at function entry, even though no
// MIR instruction defines them.  Hosted lowering records the opaque register
// identities as it binds parameters so its module never needs MIR's private
// name-to-register lookup during root-liveness finalization.
static const uint32_t MIR_SHARED_MAX_FUNCTION_ARGUMENT_REGS = 32;
struct MirFunctionArgumentState {
    uint32_t count;
    MIR_reg_t regs[MIR_SHARED_MAX_FUNCTION_ARGUMENT_REGS];
};

// One shared owner prevents physical-frame facts from being mirrored in
// language-specific contexts while those contexts retain semantic exits.
struct MirFrameState {
    bool active;
    bool item_return;
    bool number_active;
    MirScalarReturnMode scalar_return_mode;
    MIR_type_t return_type;
    MIR_reg_t runtime;
    MIR_reg_t root_base;
    MIR_reg_t number_base;
    MIR_label_t anchor;
    MIR_label_t return_label;
    MIR_reg_t return_reg;
    MIR_reg_t error_return_reg;
    MIR_reg_t incoming_scalar_home;
    int return_lane_kind;
    int root_slot_count;
    int gc_home_count;
    int fixed_number_slots;
    MIR_reg_t* root_latest;
    int root_latest_capacity;
    MirRootBinding* root_bindings;
    int root_binding_count;
    int root_binding_capacity;
    int* root_binding_by_reg;
    int root_binding_by_reg_capacity;
    int* root_binding_by_home;
    int root_binding_by_home_capacity;
    int root_store_count;
    int may_gc_call_count;
    int no_gc_call_count;
    MirRootCandidate* gc_candidates;
    int gc_candidate_count;
    int gc_candidate_capacity;
    int* gc_candidate_by_reg;
    int gc_candidate_by_reg_capacity;
    MirPendingRootStore* pending_root_stores;
    int pending_root_store_count;
    int pending_root_store_capacity;
    MirGcCallSite* gc_call_sites;
    int gc_call_site_count;
    int gc_call_site_capacity;
    MIR_insn_t* root_backedge_reloads;
    int root_backedge_reload_count;
    int root_backedge_reload_capacity;
    MirEnvBinding* env_bindings;
    int env_binding_count;
    int env_binding_capacity;
    MirFunctionPlan plan;
    MirScalarHomeBinding* scalar_home_bindings;
    int scalar_home_binding_count;
    int scalar_home_binding_capacity;
    MirScalarHomeFixup* scalar_home_fixups;
    int scalar_home_fixup_count;
    int scalar_home_fixup_capacity;
    int scalar_home_count;
    int colored_scalar_home_count;
    int discard_scalar_home_slot;
};

struct MirEmitter {
    MIR_context_t ctx;            // MIR context — immutable after init (cached from owner)
    MIR_item_t func_item;         // current function item — emit target cursor
    MIR_func_t func;              // current function — register-allocation target
    int reg_counter;              // monotonic register-id source
    int label_counter;            // monotonic label/proto-id source
    struct hashmap* import_cache; // name -> import (proto+import) memo
    void (*note_mir_call)(const char* name); // optional per-language call telemetry hook
    void* call_owner;
    void (*before_may_gc_call)(void* owner);
    void (*root_call_value)(void* owner, MIR_reg_t reg);
    void (*after_call_result)(void* owner, MIR_reg_t reg, MIR_type_t type);
    MirValue (*convert_rep)(void* owner, MirValue value, ValueRep required);
    // Hosted compilers provide their build-coupled catalog lookup. Core
    // transpilers leave this NULL and retain the existing registry path.
    bool (*lookup_import_metadata)(const char* name, JitImportMetadata* metadata);
    ArrayList* const_list;        // per-module constant pool, owned by Script/Transpiler
    MIR_reg_t consts_reg;         // register holding const_list->data for current function
    MIR_item_t consts_bss;        // per-module BSS slot holding const_list->data
    MirFrameState frame;           // canonical physical activation state
    MirFunctionMetadata last_function;
    MirFunctionArgumentState argument_registers;
};

static inline void em_function_arguments_clear(MirEmitter* em) {
    if (!em) return;
    memset(&em->argument_registers, 0, sizeof(em->argument_registers));
}

static inline void em_function_argument_register(MirEmitter* em, MIR_reg_t reg) {
    if (!em || !reg) return;
    MirFunctionArgumentState* arguments = &em->argument_registers;
    for (uint32_t i = 0; i < arguments->count; i++) {
        if (arguments->regs[i] == reg) return;
    }
    if (arguments->count < MIR_SHARED_MAX_FUNCTION_ARGUMENT_REGS) {
        arguments->regs[arguments->count++] = reg;
    }
}

static inline MirFunctionArgumentState em_function_arguments_suspend(
        MirEmitter* em) {
    MirFunctionArgumentState saved = {};
    if (!em) return saved;
    saved = em->argument_registers;
    em_function_arguments_clear(em);
    return saved;
}

static inline void em_function_arguments_restore(MirEmitter* em,
        MirFunctionArgumentState saved) {
    if (!em) return;
    em->argument_registers = saved;
}

static inline void em_frame_dispose(MirEmitter* em) {
    if (!em) return;
    MirFrameState* frame = &em->frame;
    if (frame->root_latest) mem_free(frame->root_latest);
    if (frame->root_bindings) mem_free(frame->root_bindings);
    if (frame->root_binding_by_reg) mem_free(frame->root_binding_by_reg);
    if (frame->root_binding_by_home) mem_free(frame->root_binding_by_home);
    if (frame->gc_candidates) mem_free(frame->gc_candidates);
    if (frame->gc_candidate_by_reg) mem_free(frame->gc_candidate_by_reg);
    if (frame->pending_root_stores) mem_free(frame->pending_root_stores);
    if (frame->gc_call_sites) mem_free(frame->gc_call_sites);
    if (frame->root_backedge_reloads) mem_free(frame->root_backedge_reloads);
    if (frame->env_bindings) mem_free(frame->env_bindings);
    if (frame->scalar_home_bindings) mem_free(frame->scalar_home_bindings);
    if (frame->scalar_home_fixups) mem_free(frame->scalar_home_fixups);
    memset(frame, 0, sizeof(*frame));
}

// Detach the complete frame: partial saves leaked new facts across nested
// function finalization.
static inline MirFrameState em_frame_suspend(MirEmitter* em) {
    MirFrameState saved = {};
    if (!em) return saved;
    saved = em->frame;
    memset(&em->frame, 0, sizeof(em->frame));
    return saved;
}

static inline void em_frame_restore(MirEmitter* em, MirFrameState saved) {
    if (!em) return;
    em_frame_dispose(em);
    em->frame = saved;
}

static inline bool em_root_ensure_index_map(int** map, int* capacity,
        int key) {
    if (!map || !capacity || key < 0) return false;
    if (key < *capacity) return true;
    int next_capacity = *capacity ? *capacity : 64;
    while (next_capacity <= key) next_capacity *= 2;
    int old_capacity = *capacity;
    int* resized = (int*)mem_realloc(*map,
        (size_t)next_capacity * sizeof(int), MEM_CAT_TEMP);
    if (!resized) return false;
    for (int i = old_capacity; i < next_capacity; i++) resized[i] = -1;
    *map = resized;
    *capacity = next_capacity;
    return true;
}

static inline bool em_root_note_candidate(MirRootCandidate** candidates,
        int* candidate_count, int* candidate_capacity,
        int** candidate_by_reg, int* candidate_by_reg_capacity,
        MIR_reg_t reg, JitValueClass value_class, int home_id) {
    if (!candidates || !candidate_count || !candidate_capacity ||
            !candidate_by_reg || !candidate_by_reg_capacity || !reg) {
        return false;
    }
    int reg_key = (int)reg;
    if (!em_root_ensure_index_map(candidate_by_reg,
            candidate_by_reg_capacity, reg_key)) return false;
    int candidate_index = (*candidate_by_reg)[reg_key];
    if (candidate_index >= 0 && candidate_index < *candidate_count) {
        MirRootCandidate* candidate = &(*candidates)[candidate_index];
        if (candidate->value_class == JIT_VALUE_UNKNOWN) {
            candidate->value_class = value_class;
        }
        if (candidate->home_id == 0 && home_id > 0) {
            candidate->home_id = home_id;
        }
        return true;
    }
    if (*candidate_count >= *candidate_capacity) {
        int next_capacity = *candidate_capacity
            ? *candidate_capacity * 2 : 64;
        MirRootCandidate* resized = (MirRootCandidate*)mem_realloc(
            *candidates, (size_t)next_capacity * sizeof(MirRootCandidate),
            MEM_CAT_TEMP);
        if (!resized) return false;
        *candidates = resized;
        *candidate_capacity = next_capacity;
    }
    candidate_index = (*candidate_count)++;
    MirRootCandidate* candidate = &(*candidates)[candidate_index];
    candidate->reg = reg;
    candidate->value_class = value_class;
    candidate->home_id = home_id;
    (*candidate_by_reg)[reg_key] = candidate_index;
    return true;
}

static inline bool em_root_candidate_info(const MirRootCandidate* candidates,
        int candidate_count, const int* candidate_by_reg,
        int candidate_by_reg_capacity, MIR_reg_t reg,
        JitValueClass* value_class) {
    if (!reg || !candidate_by_reg ||
            reg >= (MIR_reg_t)candidate_by_reg_capacity) return false;
    int candidate_index = candidate_by_reg[reg];
    if (candidate_index < 0 || candidate_index >= candidate_count) return false;
    if (value_class) *value_class = candidates[candidate_index].value_class;
    return true;
}

static inline bool em_root_note_call_site(MirGcCallSite** call_sites,
        int* call_site_count, int* call_site_capacity, MIR_insn_t insn,
        JitGcEffect effect, JitExceptionEffect exception_effect,
        bool is_exception_poll) {
    if (!call_sites || !call_site_count || !call_site_capacity || !insn) {
        return false;
    }
    if (*call_site_count >= *call_site_capacity) {
        int next_capacity = *call_site_capacity
            ? *call_site_capacity * 2 : 64;
        MirGcCallSite* resized = (MirGcCallSite*)mem_realloc(*call_sites,
            (size_t)next_capacity * sizeof(MirGcCallSite), MEM_CAT_TEMP);
        if (!resized) return false;
        *call_sites = resized;
        *call_site_capacity = next_capacity;
    }
    MirGcCallSite* site = &(*call_sites)[(*call_site_count)++];
    site->insn = insn;
    site->effect = effect;
    site->exception_effect = exception_effect;
    site->is_exception_poll = is_exception_poll;
    return true;
}

static inline void em_root_propagate_mov_candidates(MirEmitter* em,
        MirRootCandidate** candidates, int* candidate_count,
        int* candidate_capacity, int** candidate_by_reg,
        int* candidate_by_reg_capacity) {
    if (!em || !em->func || !candidates || !candidate_count ||
            !candidate_by_reg) return;
    bool changed = true;
    while (changed) {
        changed = false;
        for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
                insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
            if (insn->code != MIR_MOV || insn->nops != 2 ||
                    insn->ops[0].mode != MIR_OP_REG ||
                    insn->ops[1].mode != MIR_OP_REG) continue;
            JitValueClass value_class = JIT_VALUE_UNKNOWN;
            if (!em_root_candidate_info(*candidates, *candidate_count,
                    *candidate_by_reg, *candidate_by_reg_capacity,
                    insn->ops[1].u.reg, &value_class) ||
                    em_root_candidate_info(*candidates, *candidate_count,
                        *candidate_by_reg, *candidate_by_reg_capacity,
                        insn->ops[0].u.reg, NULL)) continue;
            int before = *candidate_count;
            // A copied value has an independent lifetime and therefore uses a
            // scratch home even when its source is a canonical binding.
            if (!em_root_note_candidate(candidates, candidate_count,
                    candidate_capacity, candidate_by_reg,
                    candidate_by_reg_capacity, insn->ops[0].u.reg,
                    value_class, 0)) {
                log_error("mir-root-candidates: MOV propagation allocation failed");
                // Dropping a candidate would make precise-only generated code
                // unsound. Compilation metadata OOM is therefore fail-stop.
                abort();
            }
            if (*candidate_count != before) changed = true;
        }
    }
}

static inline bool mir_gc_value_needs_root(JitValueClass value_class,
        MIR_type_t physical_type) {
    switch (value_class) {
    case JIT_VALUE_BOXED_ITEM:
    case JIT_VALUE_RAW_GC_POINTER:
        return true;
    case JIT_VALUE_NON_GC_SCALAR:
    case JIT_VALUE_RAW_NON_GC_POINTER:
        return false;
    case JIT_VALUE_UNKNOWN:
    default:
        // Until an import is semantically audited, preserve the compatibility
        // fallback for word-sized carriers rather than silently losing roots.
        return physical_type == MIR_T_I64 || physical_type == MIR_T_P;
    }
}

static inline int em_gc_new_home(MirEmitter* em) {
    if (!em) return -1;
    return ++em->frame.gc_home_count;
}

// Boxed scalar returns need a lifetime policy that is independent of their
// machine return type. Both Lambda and LambdaJS use this enum so Item encoding
// changes cannot make their return epilogues drift apart.
static inline MirScalarReturnMode em_scalar_return_mode_for_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        return MIR_SCALAR_RETURN_FLOAT;
    case LMD_TYPE_INT64:
        return MIR_SCALAR_RETURN_INT64;
    case LMD_TYPE_UINT64:
        return MIR_SCALAR_RETURN_UINT64;
    case LMD_TYPE_ANY:
        return MIR_SCALAR_RETURN_DYNAMIC;
    default:
        return MIR_SCALAR_RETURN_NONE;
    }
}

static inline MirScalarReturnMode em_scalar_return_mode_for_class(
        ScalarReturnClass scalar_class) {
    switch (scalar_class) {
    case SCALAR_RETURN_I64: return MIR_SCALAR_RETURN_INT64;
    case SCALAR_RETURN_U64: return MIR_SCALAR_RETURN_UINT64;
    case SCALAR_RETURN_F64: return MIR_SCALAR_RETURN_FLOAT;
    case SCALAR_RETURN_DYNAMIC: return MIR_SCALAR_RETURN_DYNAMIC;
    default: return MIR_SCALAR_RETURN_NONE;
    }
}

static inline ScalarReturnClass em_scalar_return_class_for_type(
        TypeId type_id) {
    switch (em_scalar_return_mode_for_type(type_id)) {
    case MIR_SCALAR_RETURN_INT64: return SCALAR_RETURN_I64;
    case MIR_SCALAR_RETURN_UINT64: return SCALAR_RETURN_U64;
    case MIR_SCALAR_RETURN_FLOAT: return SCALAR_RETURN_F64;
    case MIR_SCALAR_RETURN_DYNAMIC: return SCALAR_RETURN_DYNAMIC;
    default: return SCALAR_RETURN_NONE;
    }
}

static inline MirValue em_value(MIR_reg_t reg, MIR_type_t mir_type,
        TypeId semantic_type, ValueRep rep, JitValueClass value_class) {
    MirValue value = {};
    value.reg = reg;
    value.mir_type = mir_type;
    value.semantic_type = semantic_type;
    value.rep = rep;
    value.value_class = value_class;
    value.scalar_provenance = SCALAR_PROVENANCE_UNKNOWN;
    return value;
}
static inline MirValue em_require_rep(MirEmitter* em, MirValue value,
        ValueRep required) {
    if (value.rep == required) return value;
    if (em && em->convert_rep) {
        MirValue converted = em->convert_rep(em->call_owner, value, required);
        if (converted.rep == required) return converted;
    }
    // Fail closed so an Item/pointer ABI mismatch cannot escape verification.
    log_error("mir-value: unavailable representation transition %d -> %d in %s",
        (int)value.rep, (int)required,
        em && em->frame.plan.debug_name ? em->frame.plan.debug_name :
        "<unnamed>");
    abort();
}
static inline void em_validate_call_abi(const char* call_name,
        const JitCallMetadata* metadata, int nargs, MIR_type_t* arg_types) {
    if (!metadata || metadata->abi_arg_count != (uint16_t)nargs) {
        log_error("mir-call: metadata arity mismatch for %s", call_name);
        abort();
    }
    for (int i = 0; i < nargs; i++) {
        JitAbiRep rep = metadata->abi_args[i].value.abi_rep;
        MIR_type_t type = arg_types[i];
        bool valid = rep == JIT_ABI_ITEM || rep == JIT_ABI_I64
            ? type == MIR_T_I64
            : rep == JIT_ABI_F64 ? type == MIR_T_D || type == MIR_T_F
            : rep == JIT_ABI_POINTER ? type == MIR_T_P
            : false;
        if (!valid) {
            log_error("mir-call: ABI mismatch for %s argument=%d rep=%d type=%d",
                call_name, i, (int)rep, (int)type);
            abort();
        }
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
static inline void em_emit_unknown_call(MirEmitter* em, MIR_insn_t insn);

static inline void em_emit_insn(MirEmitter* em, MIR_insn_t insn) {
    if (em && em->frame.active && insn &&
            (insn->code == MIR_CALL || insn->code == MIR_JCALL)) {
        em_emit_unknown_call(em, insn);
        return;
    }
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

static inline void em_store_frame_slot_typed(MirEmitter* em,
        MIR_reg_t frame_base, int slot, MIR_reg_t value, MIR_type_t type) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, type,
            (MIR_disp_t)slot * (MIR_disp_t)sizeof(uint64_t),
            frame_base, 0, 1),
        MIR_new_reg_op(em->ctx, value)));
}
static inline MIR_reg_t em_call_with_args(MirEmitter* em,
    const char* fn_name, MIR_type_t ret_type, int nargs,
    MIR_type_t* arg_types, MIR_op_t* arg_ops, bool include_signature);

// Adopt a temporary boxed scalar before restoring its source number extent.
static inline MIR_reg_t em_adopt_scalar_item(MirEmitter* em,
                                             MirScalarReturnMode mode,
                                             MIR_reg_t item,
                                             MIR_reg_t runtime,
                                             size_t number_top_offset,
                                             MIR_reg_t source_base,
                                             MIR_reg_t target_home) {
    if (mode == MIR_SCALAR_RETURN_NONE) {
        em_store_frame_top(em, runtime, number_top_offset, source_base);
        return item;
    }
    MIR_type_t types[2] = {MIR_T_I64, MIR_T_P};
    MIR_op_t args[2] = {MIR_new_reg_op(em->ctx, item),
        MIR_new_reg_op(em->ctx, target_home)};
    MIR_reg_t result = em_call_with_args(em,
        "lambda_item_adopt_scalar_home", MIR_T_I64, 2, types, args, true);
    em_store_frame_top(em, runtime, number_top_offset, source_base);
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
    int op_count = (result ? 3 : 2) + nargs;
    // Dynamic sizing prevents many-parameter generated wrappers from overflow.
    MIR_op_t* ops = (MIR_op_t*)mem_alloc(
        (size_t)op_count * sizeof(MIR_op_t), MEM_CAT_TEMP);
    if (!ops) {
        log_error("mir-call: operand allocation failed for %d operands",
            op_count);
        abort();
    }
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(ctx, proto);
    ops[oi++] = MIR_new_ref_op(ctx, import);
    if (result) {
        ops[oi++] = MIR_new_reg_op(ctx, result);
    }
    for (int i = 0; i < nargs; i++) {
        ops[oi++] = arg_ops[i];
    }
    MIR_insn_t call = MIR_new_insn_arr(ctx, MIR_CALL, oi, ops);
    mem_free(ops);
    return call;
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
    if (found) {
        found->entry.call.abi_args = found->entry.abi_args;
        return &found->entry;
    }

    MIR_item_t proto = NULL;
    MIR_item_t imp = NULL;
    mir_create_import_proto_pair(em->ctx, name, ret_type, nargs, args, nres,
        include_signature, &proto, &imp);

    MirImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", key.name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    // Unknown imports retain conservative MAY_GC/unknown-value defaults. Each
    // compiler supplies its own catalog adapter; this shared header must not
    // bind the private core resolver into a hosted module binary.
    if (em && em->lookup_import_metadata) {
        em->lookup_import_metadata(name, &new_entry.entry.audit);
    }
    em_normalize_import_call(&new_entry.entry, ret_type, nargs, args, nres);
    hashmap_set(em->import_cache, &new_entry);

    found = (MirImportCacheEntry*)hashmap_get(em->import_cache, &key);
    // Repair the copied descriptor's self-pointer after hashmap insertion.
    if (found) found->entry.call.abi_args = found->entry.abi_args;
    return found ? &found->entry : NULL;
}

// A newly published root-frame range must never expose stale side-stack words
// to the collector. Small frames are cheaper to clear inline; larger frames
// use the audited NO_GC memset leaf so zeroing does not create a safepoint.
static inline void em_insert_zero_frame_slots(MirEmitter* em,
        MIR_insn_t before, MIR_reg_t frame_base, int slot_count) {
    if (!em || !before || !frame_base || slot_count <= 0) return;
    const int inline_slot_limit = 8;
    if (slot_count <= inline_slot_limit) {
        for (int slot = 0; slot < slot_count; slot++) {
            MIR_insn_t clear = MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)slot * (MIR_disp_t)sizeof(uint64_t),
                    frame_base, 0, 1),
                MIR_new_int_op(em->ctx, 0));
            MIR_insert_insn_before(em->ctx, em->func_item, before, clear);
        }
        return;
    }

    MIR_var_t args[3] = {
        {MIR_T_P, "dest", 0},
        {MIR_T_I64, "value", 0},
        {MIR_T_I64, "size", 0},
    };
    MirImportEntry* memset_import = em_ensure_import(em, "memset", MIR_T_P,
        3, args, 1, true);
    if (!memset_import) {
        log_error("mir-root-frame-zero: unable to import memset");
        return;
    }
    MIR_reg_t result = em_new_reg(em, "root_frame_zeroed", MIR_T_P);
    MIR_op_t call_args[3] = {
        MIR_new_reg_op(em->ctx, frame_base),
        MIR_new_int_op(em->ctx, 0),
        MIR_new_int_op(em->ctx,
            (int64_t)slot_count * (int64_t)sizeof(uint64_t)),
    };
    MIR_insn_t clear = mir_new_call_with_args(em->ctx,
        memset_import->proto, memset_import->import, result, 3, call_args);
    MIR_insert_insn_before(em->ctx, em->func_item, before, clear);
}

// Finalize reservation sizes; bound bodies skip binding, not frame reservation.
static inline MIR_label_t em_finalize_frame_prologue(MirEmitter* em,
        MirEntryMode entry_mode, size_t root_top_offset,
        size_t root_limit_offset, size_t number_top_offset,
        size_t number_limit_offset, size_t root_commit_limit_offset) {
    MirFrameState* frame = &em->frame;
    MIR_label_t overflow_label = em_new_label(em);
    MirImportEntry* ensure_import = NULL;
    if (frame->root_slot_count == 0) {
        MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
        while (insn) {
            MIR_insn_t next = DLIST_NEXT(MIR_insn_t, insn);
            if (insn->code == MIR_MOV && insn->nops == 2 &&
                    insn->ops[0].mode == MIR_OP_MEM &&
                    insn->ops[0].u.mem.base == frame->runtime &&
                    insn->ops[0].u.mem.disp == (MIR_disp_t)root_top_offset &&
                    insn->ops[1].mode == MIR_OP_REG &&
                    insn->ops[1].u.reg == frame->root_base) {
                // Compaction may erase the last slot after epilogue emission.
                MIR_remove_insn(em->ctx, em->func_item, insn);
            }
            insn = next;
        }
    }
    if (entry_mode == MIR_ENTRY_CHECKED) {
        MIR_var_t ensure_args[3] = {
            {MIR_T_P, "context", 0}, {MIR_T_I64, "root_slots", 0},
            {MIR_T_I64, "number_slots", 0},
        };
        ensure_import = em_ensure_import(em, "lambda_side_stack_ensure",
            MIR_T_I64, 3, ensure_args, 1, false);
        MIR_reg_t bound = em_new_reg(em, "frame_bound", MIR_T_I64);
        MIR_reg_t ensured = em_new_reg(em, "frame_ensured", MIR_T_I64);
        MIR_label_t bound_label = em_new_label(em);
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_reg_op(em->ctx, frame->root_base),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_top_offset, frame->runtime, 0, 1)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_NE, MIR_new_reg_op(em->ctx, bound),
                MIR_new_reg_op(em->ctx, frame->root_base),
                MIR_new_int_op(em->ctx, 0)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BT,
                MIR_new_label_op(em->ctx, bound_label),
                MIR_new_reg_op(em->ctx, bound)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_call_insn(em->ctx, 6,
                MIR_new_ref_op(em->ctx, ensure_import->proto),
                MIR_new_ref_op(em->ctx, ensure_import->import),
                MIR_new_reg_op(em->ctx, ensured),
                MIR_new_reg_op(em->ctx, frame->runtime),
                MIR_new_int_op(em->ctx, frame->root_slot_count),
                MIR_new_int_op(em->ctx, frame->fixed_number_slots)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BF,
                MIR_new_label_op(em->ctx, overflow_label),
                MIR_new_reg_op(em->ctx, ensured)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_reg_op(em->ctx, frame->root_base),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_top_offset, frame->runtime, 0, 1)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            bound_label);
    } else if (frame->root_slot_count > 0) {
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_reg_op(em->ctx, frame->root_base),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_top_offset, frame->runtime, 0, 1)));
    }

    if (frame->number_base) {
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_reg_op(em->ctx, frame->number_base),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)number_top_offset, frame->runtime, 0, 1)));
    } else if (frame->fixed_number_slots > 0) {
        log_error("mir-frame-prologue: number slots=%d lane=%d root=%u require a frame base for %s",
            frame->fixed_number_slots,
            frame->return_lane_kind, (unsigned)frame->root_base,
            frame->plan.debug_name ? frame->plan.debug_name : "<unnamed>");
        abort();
    }
    if (frame->fixed_number_slots > 0) {
        MIR_reg_t top = em_new_reg(em, "number_top", MIR_T_I64);
        MIR_reg_t limit = em_new_reg(em, "number_limit", MIR_T_I64);
        MIR_reg_t overflow = em_new_reg(em, "number_overflow", MIR_T_I64);
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, top),
                MIR_new_reg_op(em->ctx, frame->number_base),
                MIR_new_int_op(em->ctx, (int64_t)frame->fixed_number_slots *
                    (int64_t)sizeof(uint64_t))));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, limit),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)number_limit_offset, frame->runtime, 0, 1)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_UGT, MIR_new_reg_op(em->ctx, overflow),
                MIR_new_reg_op(em->ctx, top), MIR_new_reg_op(em->ctx, limit)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BT,
                MIR_new_label_op(em->ctx, overflow_label),
                MIR_new_reg_op(em->ctx, overflow)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)number_top_offset, frame->runtime, 0, 1),
                MIR_new_reg_op(em->ctx, top)));
    }

    if (frame->root_slot_count > 0) {
        MIR_reg_t top = em_new_reg(em, "root_top", MIR_T_I64);
        MIR_reg_t limit = em_new_reg(em, "root_limit", MIR_T_I64);
        MIR_reg_t overflow = em_new_reg(em, "root_overflow", MIR_T_I64);
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, top),
                MIR_new_reg_op(em->ctx, frame->root_base),
                MIR_new_int_op(em->ctx, (int64_t)frame->root_slot_count *
                    (int64_t)sizeof(uint64_t))));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, limit),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_limit_offset, frame->runtime, 0, 1)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_UGT, MIR_new_reg_op(em->ctx, overflow),
                MIR_new_reg_op(em->ctx, top), MIR_new_reg_op(em->ctx, limit)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BT,
                MIR_new_label_op(em->ctx, overflow_label),
                MIR_new_reg_op(em->ctx, overflow)));
#if defined(_WIN32)
        if (!ensure_import) {
            MIR_var_t ensure_args[3] = {
                {MIR_T_P, "context", 0}, {MIR_T_I64, "root_slots", 0},
                {MIR_T_I64, "number_slots", 0},
            };
            ensure_import = em_ensure_import(em, "lambda_side_stack_ensure",
                MIR_T_I64, 3, ensure_args, 1, false);
        }
        MIR_reg_t commit_limit = em_new_reg(em, "root_commit_limit", MIR_T_I64);
        MIR_reg_t needs_commit = em_new_reg(em, "root_needs_commit", MIR_T_I64);
        MIR_reg_t committed = em_new_reg(em, "root_committed", MIR_T_I64);
        MIR_label_t ready = em_new_label(em);
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_reg_op(em->ctx, commit_limit),
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_commit_limit_offset,
                    frame->runtime, 0, 1)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_UGT,
                MIR_new_reg_op(em->ctx, needs_commit),
                MIR_new_reg_op(em->ctx, top),
                MIR_new_reg_op(em->ctx, commit_limit)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, ready),
                MIR_new_reg_op(em->ctx, needs_commit)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_call_insn(em->ctx, 6,
                MIR_new_ref_op(em->ctx, ensure_import->proto),
                MIR_new_ref_op(em->ctx, ensure_import->import),
                MIR_new_reg_op(em->ctx, committed),
                MIR_new_reg_op(em->ctx, frame->runtime),
                MIR_new_int_op(em->ctx, frame->root_slot_count),
                MIR_new_int_op(em->ctx, frame->fixed_number_slots)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_BF,
                MIR_new_label_op(em->ctx, overflow_label),
                MIR_new_reg_op(em->ctx, committed)));
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor, ready);
#else
        (void)root_commit_limit_offset;
#endif
        em_insert_zero_frame_slots(em, frame->anchor, frame->root_base,
            frame->root_slot_count);
        MIR_insert_insn_before(em->ctx, em->func_item, frame->anchor,
            MIR_new_insn(em->ctx, MIR_MOV,
                MIR_new_mem_op(em->ctx, MIR_T_I64,
                    (MIR_disp_t)root_top_offset, frame->runtime, 0, 1),
                MIR_new_reg_op(em->ctx, top)));
    }
    em_emit_label(em, overflow_label);
    return overflow_label;
}

// Shared CFG liveness substrate for converting an eager exact-root oracle into
// safepoint-current publication. Language lowerers decide which values are GC
// values; this pass owns control flow, live sets, slot compaction, and stores.
struct MirRootLivenessBlock {
    int start;
    int end;
};

static inline int em_root_candidate_index(const int* reg_to_candidate,
        int reg_map_count, MIR_reg_t reg) {
    if (!reg_to_candidate || reg == 0 ||
            reg >= (MIR_reg_t)reg_map_count) return -1;
    return reg_to_candidate[reg];
}

static inline void em_root_set_bit(uint64_t* bits, int candidate) {
    if (!bits || candidate < 0) return;
    bits[candidate >> 6] |= UINT64_C(1) << (candidate & 63);
}

static inline bool em_is_eager_root_store(MIR_insn_t insn,
        MIR_reg_t frame_base) {
    return insn && insn->code == MIR_MOV && insn->nops == 2 &&
        insn->ops[0].mode == MIR_OP_MEM &&
        insn->ops[0].u.mem.base == frame_base &&
        insn->ops[1].mode == MIR_OP_REG;
}

static inline bool em_root_is_function_argument_reg(const MirEmitter* em,
        MIR_reg_t reg) {
    if (!em || !reg) return false;
#if defined(MIR_EMITTER_NO_DIRECT_REGISTER_LOOKUP)
    for (uint32_t i = 0; i < em->argument_registers.count; i++) {
        if (em->argument_registers.regs[i] == reg) return true;
    }
    return false;
#else
    if (!em->func || !em->func->vars) return false;
    MIR_var_t* vars = VARR_ADDR(MIR_var_t, em->func->vars);
    for (uint32_t i = 0; i < em->func->nargs; i++) {
        if (vars[i].name && MIR_reg(em->ctx, vars[i].name, em->func) == reg) {
            return true;
        }
    }
    return false;
#endif
}

static inline void em_collect_root_candidate_bits(MIR_context_t ctx,
        MIR_insn_t insn, const int* reg_to_candidate, int reg_map_count,
        uint64_t* uses, uint64_t* definitions) {
    if (!insn) return;
    size_t operand_count = MIR_insn_nops(ctx, insn);
    for (size_t oi = 0; oi < operand_count; oi++) {
        int output = 0;
        MIR_insn_op_mode(ctx, insn, oi, &output);
        MIR_op_t* op = &insn->ops[oi];
        if (op->mode == MIR_OP_REG) {
            int candidate = em_root_candidate_index(reg_to_candidate,
                reg_map_count, op->u.reg);
            em_root_set_bit(output ? definitions : uses, candidate);
        } else if (op->mode == MIR_OP_MEM) {
            // Address registers are inputs even when the memory word is an
            // output; raw managed pointers can otherwise vanish from liveness.
            em_root_set_bit(uses, em_root_candidate_index(reg_to_candidate,
                reg_map_count, op->u.mem.base));
            em_root_set_bit(uses, em_root_candidate_index(reg_to_candidate,
                reg_map_count, op->u.mem.index));
        }
    }
}

static inline bool em_root_block_terminates(MIR_insn_t insn) {
    return insn && (MIR_any_branch_code_p(insn->code) ||
        insn->code == MIR_RET || insn->code == MIR_JRET);
}

static inline int em_root_find_label_block(MIR_label_t target,
        MIR_label_t* labels, const int* label_blocks, int label_count) {
    if (!target) return -1;
    for (int i = 0; i < label_count; i++) {
        if (labels[i] == target) return label_blocks[i];
    }
    return -1;
}

static inline void em_root_union_bits(uint64_t* destination,
        const uint64_t* source, int word_count) {
    if (!destination || !source) return;
    for (int word = 0; word < word_count; word++) {
        destination[word] |= source[word];
    }
}

static inline void em_root_union_successor_live(uint64_t* destination,
        int successor, const uint64_t* live_in, int block_count,
        int word_count) {
    if (successor < 0 || successor >= block_count) return;
    em_root_union_bits(destination,
        live_in + (size_t)successor * (size_t)word_count, word_count);
}

static inline void em_root_compute_block_live_out(int block_index,
        const MirRootLivenessBlock* blocks, int block_count,
        MIR_insn_t* instructions, MIR_label_t* labels,
        const int* label_blocks, int label_count,
        const uint64_t* live_in, uint64_t* out, int word_count) {
    memset(out, 0, (size_t)word_count * sizeof(uint64_t));
    const MirRootLivenessBlock* block = &blocks[block_index];
    if (block->end <= block->start) {
        em_root_union_successor_live(out, block_index + 1, live_in,
            block_count, word_count);
        return;
    }
    MIR_insn_t last = instructions[block->end - 1];
    if (last->code == MIR_RET || last->code == MIR_JRET) return;
    if (last->code == MIR_JMPI) {
        // Indirect control flow has no enumerable target. Conservatively make
        // every block a successor rather than omit a possible live root.
        for (int successor = 0; successor < block_count; successor++) {
            em_root_union_successor_live(out, successor, live_in,
                block_count, word_count);
        }
        return;
    }
    if (last->code == MIR_SWITCH) {
        for (unsigned oi = 1; oi < last->nops; oi++) {
            if (last->ops[oi].mode != MIR_OP_LABEL) continue;
            int successor = em_root_find_label_block(last->ops[oi].u.label,
                labels, label_blocks, label_count);
            em_root_union_successor_live(out, successor, live_in,
                block_count, word_count);
        }
        return;
    }
    if (MIR_branch_code_p(last->code)) {
        if (last->nops > 0 && last->ops[0].mode == MIR_OP_LABEL) {
            int successor = em_root_find_label_block(last->ops[0].u.label,
                labels, label_blocks, label_count);
            em_root_union_successor_live(out, successor, live_in,
                block_count, word_count);
        }
        if (last->code != MIR_JMP) {
            em_root_union_successor_live(out, block_index + 1, live_in,
                block_count, word_count);
        }
        return;
    }
    em_root_union_successor_live(out, block_index + 1, live_in,
        block_count, word_count);
}

static inline void em_root_set_block_successor(uint64_t* successors,
        int successor_word_count, int block_count, int block, int successor) {
    if (!successors || block < 0 || block >= block_count || successor < 0 ||
            successor >= block_count) return;
    uint64_t* row = successors +
        (size_t)block * (size_t)successor_word_count;
    row[successor >> 6] |= UINT64_C(1) << (successor & 63);
}

static inline int em_root_take_lowest_set_bit(uint64_t* bits) {
    if (!bits || !*bits) return -1;
    uint64_t value = *bits;
    int index = 0;
    while ((value & UINT64_C(1)) == 0) {
        value >>= 1;
        index++;
    }
    *bits &= *bits - UINT64_C(1);
    return index;
}

static inline int em_root_collect_set_candidates(const uint64_t* bits,
        int word_count, int candidate_count, int* candidates) {
    if (!bits || !candidates) return 0;
    int count = 0;
    for (int word = 0; word < word_count; word++) {
        uint64_t pending = bits[word];
        while (pending) {
            int bit = em_root_take_lowest_set_bit(&pending);
            int candidate = word * 64 + bit;
            if (candidate < candidate_count) candidates[count++] = candidate;
        }
    }
    return count;
}

static inline void em_root_collect_block_successors(int block_index,
        const MirRootLivenessBlock* blocks, int block_count,
        MIR_insn_t* instructions, MIR_label_t* labels,
        const int* label_blocks, int label_count, uint64_t* successors,
        int successor_word_count) {
    const MirRootLivenessBlock* block = &blocks[block_index];
    if (block->end <= block->start) {
        em_root_set_block_successor(successors, successor_word_count,
            block_count, block_index, block_index + 1);
        return;
    }
    MIR_insn_t last = instructions[block->end - 1];
    if (last->code == MIR_RET || last->code == MIR_JRET) return;
    if (last->code == MIR_JMPI) {
        // Indirect control flow has no enumerable target, so every block is a
        // possible successor for exact-root dirty-state propagation.
        for (int successor = 0; successor < block_count; successor++) {
            em_root_set_block_successor(successors, successor_word_count,
                block_count, block_index, successor);
        }
        return;
    }
    if (last->code == MIR_SWITCH) {
        for (unsigned oi = 1; oi < last->nops; oi++) {
            if (last->ops[oi].mode != MIR_OP_LABEL) continue;
            int successor = em_root_find_label_block(last->ops[oi].u.label,
                labels, label_blocks, label_count);
            em_root_set_block_successor(successors, successor_word_count,
                block_count, block_index, successor);
        }
        return;
    }
    if (MIR_branch_code_p(last->code)) {
        if (last->nops > 0 && last->ops[0].mode == MIR_OP_LABEL) {
            int successor = em_root_find_label_block(last->ops[0].u.label,
                labels, label_blocks, label_count);
            em_root_set_block_successor(successors, successor_word_count,
                block_count, block_index, successor);
        }
        if (last->code != MIR_JMP) {
            em_root_set_block_successor(successors, successor_word_count,
                block_count, block_index, block_index + 1);
        }
        return;
    }
    em_root_set_block_successor(successors, successor_word_count, block_count,
        block_index, block_index + 1);
}

static inline bool em_root_call_may_collect(MIR_insn_t insn,
        const MirGcCallSite* call_sites, int call_site_count) {
    for (int i = 0; i < call_site_count; i++) {
        if (call_sites[i].insn == insn) {
            return call_sites[i].effect != JIT_EFFECT_NO_GC;
        }
    }
    // A bypass that did not report metadata is conservatively MAY_GC.
    return true;
}

static inline bool em_finalize_semantic_root_write_back(MirEmitter* em,
        MIR_reg_t frame_base, MIR_insn_t anchor,
        bool oracle_stores_present, int oracle_slot_count,
        MirRootCandidate** candidates, int* candidate_count_ptr,
        int* candidate_capacity, int** candidate_by_reg,
        int* candidate_by_reg_capacity,
        const MirGcCallSite* call_sites, int call_site_count,
        MirRootWriteBackResult* result, const char* log_label,
        const uint8_t* preserved_oracle_homes = NULL,
        int preserved_oracle_home_count = 0,
        bool pin_semantic_home_slots = false,
        int retained_oracle_store_count = 0) {
    if (!em || !em->func || !em->func_item || !frame_base || !anchor ||
            !candidates || !candidate_count_ptr || !candidate_capacity ||
            !candidate_by_reg || !candidate_by_reg_capacity) return false;
    if (result) memset(result, 0, sizeof(*result));

    // Root identity follows MIR copies into narrowing, return, and join
    // registers even when the destination has no source-language binding.
    em_root_propagate_mov_candidates(em, candidates, candidate_count_ptr,
        candidate_capacity, candidate_by_reg, candidate_by_reg_capacity);
    MirRootCandidate* root_candidates = *candidates;
    int candidate_count = *candidate_count_ptr;
    if (candidate_count == 0) return true;

    int instruction_count = 0;
    MIR_reg_t max_candidate_reg = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) {
            instruction_count++;
        }
    }
    for (int ci = 0; ci < candidate_count; ci++) {
        if (root_candidates[ci].reg > max_candidate_reg) {
            max_candidate_reg = root_candidates[ci].reg;
        }
    }
    int reg_map_count = (int)max_candidate_reg + 1;
    int word_count = (candidate_count + 63) / 64;
    MIR_insn_t* instructions = (MIR_insn_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_insn_t), MEM_CAT_TEMP);
    MirRootLivenessBlock* blocks = (MirRootLivenessBlock*)mem_alloc(
        (size_t)instruction_count * sizeof(MirRootLivenessBlock), MEM_CAT_TEMP);
    int* instruction_blocks = (int*)mem_alloc(
        (size_t)instruction_count * sizeof(int), MEM_CAT_TEMP);
    MIR_label_t* labels = (MIR_label_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_label_t), MEM_CAT_TEMP);
    int* label_blocks = (int*)mem_alloc(
        (size_t)instruction_count * sizeof(int), MEM_CAT_TEMP);
    int* reg_to_candidate = (int*)mem_alloc(
        (size_t)reg_map_count * sizeof(int), MEM_CAT_TEMP);
    uint64_t* insn_uses = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* insn_definitions = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    if (!instructions || !blocks || !instruction_blocks || !labels ||
            !label_blocks || !reg_to_candidate || !insn_uses ||
            !insn_definitions) {
        log_error("mir-semantic-root-write-back: liveness allocation failed");
        abort();
    }
    for (int reg = 0; reg < reg_map_count; reg++) reg_to_candidate[reg] = -1;
    for (int ci = 0; ci < candidate_count; ci++) {
        reg_to_candidate[root_candidates[ci].reg] = ci;
    }

    int instruction_index = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) {
            instructions[instruction_index++] = insn;
        }
    }
    int block_count = 0;
    int block_start = 0;
    for (int i = 0; i < instruction_count; i++) {
        bool starts_block = i == 0 || instructions[i]->code == MIR_LABEL ||
            em_root_block_terminates(instructions[i - 1]);
        if (starts_block && i > block_start) {
            blocks[block_count++] = {block_start, i};
            block_start = i;
        }
    }
    if (block_start < instruction_count) {
        blocks[block_count++] = {block_start, instruction_count};
    }
    for (int bi = 0; bi < block_count; bi++) {
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            instruction_blocks[i] = bi;
        }
    }
    int label_count = 0;
    for (int i = 0; i < instruction_count; i++) {
        if (instructions[i]->code == MIR_LABEL) {
            labels[label_count] = instructions[i];
            label_blocks[label_count++] = instruction_blocks[i];
        }
    }

    size_t block_word_count = (size_t)block_count * (size_t)word_count;
    uint64_t* block_uses = (uint64_t*)mem_alloc(
        block_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* block_definitions = (uint64_t*)mem_alloc(
        block_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* live_in = (uint64_t*)mem_alloc(
        block_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* live_out = (uint64_t*)mem_alloc(
        block_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* scratch_out = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* scratch_in = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    if (!block_uses || !block_definitions || !live_in || !live_out ||
            !scratch_out || !scratch_in) {
        log_error("mir-semantic-root-write-back: CFG allocation failed");
        abort();
    }
    memset(block_uses, 0, block_word_count * sizeof(uint64_t));
    memset(block_definitions, 0, block_word_count * sizeof(uint64_t));
    memset(live_in, 0, block_word_count * sizeof(uint64_t));
    memset(live_out, 0, block_word_count * sizeof(uint64_t));
    for (int bi = 0; bi < block_count; bi++) {
        uint64_t* uses = block_uses + (size_t)bi * (size_t)word_count;
        uint64_t* definitions = block_definitions +
            (size_t)bi * (size_t)word_count;
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            memset(insn_uses, 0, (size_t)word_count * sizeof(uint64_t));
            memset(insn_definitions, 0,
                (size_t)word_count * sizeof(uint64_t));
            em_collect_root_candidate_bits(em->ctx, instructions[i],
                reg_to_candidate, reg_map_count, insn_uses, insn_definitions);
            for (int word = 0; word < word_count; word++) {
                uses[word] |= insn_uses[word] & ~definitions[word];
                definitions[word] |= insn_definitions[word];
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = block_count - 1; bi >= 0; bi--) {
            em_root_compute_block_live_out(bi, blocks, block_count, instructions,
                labels, label_blocks, label_count, live_in, scratch_out,
                word_count);
            uint64_t* old_out = live_out +
                (size_t)bi * (size_t)word_count;
            uint64_t* old_in = live_in +
                (size_t)bi * (size_t)word_count;
            uint64_t* uses = block_uses +
                (size_t)bi * (size_t)word_count;
            uint64_t* definitions = block_definitions +
                (size_t)bi * (size_t)word_count;
            for (int word = 0; word < word_count; word++) {
                scratch_in[word] = uses[word] |
                    (scratch_out[word] & ~definitions[word]);
                if (old_out[word] != scratch_out[word] ||
                        old_in[word] != scratch_in[word]) {
                    changed = true;
                }
                old_out[word] = scratch_out[word];
                old_in[word] = scratch_in[word];
            }
        }
    }

    size_t instruction_word_count =
        (size_t)instruction_count * (size_t)word_count;
    uint64_t* instruction_live_in = (uint64_t*)mem_alloc(
        instruction_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint8_t* candidate_defined = (uint8_t*)mem_alloc(
        (size_t)candidate_count * sizeof(uint8_t), MEM_CAT_TEMP);
    int max_home_id = 0;
    for (int ci = 0; ci < candidate_count; ci++) {
        if (root_candidates[ci].home_id > max_home_id) {
            max_home_id = root_candidates[ci].home_id;
        }
    }
    if (pin_semantic_home_slots && oracle_slot_count > max_home_id) {
        max_home_id = oracle_slot_count;
    }
    int home_map_count = max_home_id + 1;
    if (home_map_count < 1) home_map_count = 1;
    int* home_to_slot = (int*)mem_alloc(
        (size_t)home_map_count * sizeof(int), MEM_CAT_TEMP);
    int* candidate_slots = (int*)mem_alloc(
        (size_t)candidate_count * sizeof(int), MEM_CAT_TEMP);
    int* set_candidates = (int*)mem_alloc(
        (size_t)candidate_count * sizeof(int), MEM_CAT_TEMP);
    if (!instruction_live_in || !candidate_defined || !home_to_slot ||
            !candidate_slots || !set_candidates) {
        log_error("mir-semantic-root-write-back: publication allocation failed");
        abort();
    }
    memset(instruction_live_in, 0,
        instruction_word_count * sizeof(uint64_t));
    memset(candidate_defined, 0,
        (size_t)candidate_count * sizeof(uint8_t));
    for (int home = 0; home < home_map_count; home++) home_to_slot[home] = -1;
    for (int ci = 0; ci < candidate_count; ci++) candidate_slots[ci] = -1;

    // Materialize exact per-instruction live-in sets once. Safepoint
    // publication and dirty-state transfer consume the same solved CFG facts.
    for (int bi = block_count - 1; bi >= 0; bi--) {
        memcpy(scratch_in,
            live_out + (size_t)bi * (size_t)word_count,
            (size_t)word_count * sizeof(uint64_t));
        for (int i = blocks[bi].end - 1; i >= blocks[bi].start; i--) {
            memset(insn_uses, 0, (size_t)word_count * sizeof(uint64_t));
            memset(insn_definitions, 0,
                (size_t)word_count * sizeof(uint64_t));
            em_collect_root_candidate_bits(em->ctx, instructions[i],
                reg_to_candidate, reg_map_count, insn_uses, insn_definitions);
            for (int word = 0; word < word_count; word++) {
                scratch_in[word] = (scratch_in[word] &
                    ~insn_definitions[word]) | insn_uses[word];
            }
            memcpy(instruction_live_in +
                    (size_t)i * (size_t)word_count,
                scratch_in, (size_t)word_count * sizeof(uint64_t));
            int definition_count = em_root_collect_set_candidates(insn_definitions,
                word_count, candidate_count, set_candidates);
            for (int di = 0; di < definition_count; di++) {
                candidate_defined[set_candidates[di]] = 1;
            }
        }
    }

    int stable_slot_count = 0;
    if (pin_semantic_home_slots) {
        // Lambda MIR-Direct may still read selected root homes as value-merge
        // memory. Reserve only those physical offsets, then compact optimized
        // homes around them instead of preserving the old oversized frame.
        int next_slot = 0;
        for (int i = 0; i < instruction_count; i++) {
            MIR_insn_t current = instructions[i];
            if (!MIR_call_code_p(current->code) ||
                    !em_root_call_may_collect(current, call_sites,
                        call_site_count)) continue;
            const uint64_t* live = instruction_live_in +
                (size_t)i * (size_t)word_count;
            int live_count = em_root_collect_set_candidates(live, word_count,
                candidate_count, set_candidates);
            for (int li = 0; li < live_count; li++) {
                int ci = set_candidates[li];
                int home_id = root_candidates[ci].home_id;
                if (home_id <= 0 || home_id >= home_map_count ||
                        home_to_slot[home_id] >= 0) continue;
                while (preserved_oracle_homes &&
                        next_slot + 1 < preserved_oracle_home_count &&
                        preserved_oracle_homes[next_slot + 1]) {
                    next_slot++;
                }
                home_to_slot[home_id] = next_slot++;
            }
        }
        stable_slot_count = next_slot;
        if (preserved_oracle_homes) {
            for (int home = 1; home < preserved_oracle_home_count; home++) {
                if (preserved_oracle_homes[home] &&
                        home > stable_slot_count) {
                    stable_slot_count = home;
                }
            }
        }
    } else {
        for (int i = 0; i < instruction_count; i++) {
            MIR_insn_t current = instructions[i];
            if (!MIR_call_code_p(current->code) ||
                    !em_root_call_may_collect(current, call_sites,
                        call_site_count)) continue;
            const uint64_t* live = instruction_live_in +
                (size_t)i * (size_t)word_count;
            int live_count = em_root_collect_set_candidates(live, word_count,
                candidate_count, set_candidates);
            for (int li = 0; li < live_count; li++) {
                int ci = set_candidates[li];
                int home_id = root_candidates[ci].home_id;
                if (home_id <= 0 || home_id >= home_map_count) continue;
                if (home_to_slot[home_id] < 0) {
                    home_to_slot[home_id] = stable_slot_count++;
                }
            }
        }
    }
    for (int ci = 0; ci < candidate_count; ci++) {
        int home_id = root_candidates[ci].home_id;
        if (home_id > 0 && home_id < home_map_count) {
            candidate_slots[ci] = home_to_slot[home_id];
        }
    }

    size_t interference_word_count =
        (size_t)candidate_count * (size_t)word_count;
    uint64_t* interference = (uint64_t*)mem_alloc(
        interference_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint8_t* used_colors = (uint8_t*)mem_alloc(
        (size_t)candidate_count * sizeof(uint8_t), MEM_CAT_TEMP);
    uint8_t* candidate_obligated = (uint8_t*)mem_alloc(
        (size_t)candidate_count * sizeof(uint8_t), MEM_CAT_TEMP);
    if (!interference || !used_colors || !candidate_obligated) {
        log_error("mir-semantic-root-write-back: scratch-color allocation failed");
        abort();
    }
    memset(interference, 0,
        interference_word_count * sizeof(uint64_t));
    memset(candidate_obligated, 0,
        (size_t)candidate_count * sizeof(uint8_t));
    // Scratch candidates interfere only when they are simultaneously
    // root-obligated at a safepoint. Coloring this graph gives each live range
    // a stable reusable slot, so an unchanged temporary remains current across
    // repeated calls instead of being republished at each one.
    for (int i = 0; i < instruction_count; i++) {
        MIR_insn_t current = instructions[i];
        if (!MIR_call_code_p(current->code) ||
                !em_root_call_may_collect(current, call_sites,
                    call_site_count)) continue;
        const uint64_t* live = instruction_live_in +
            (size_t)i * (size_t)word_count;
        int live_count = em_root_collect_set_candidates(live, word_count,
            candidate_count, set_candidates);
        int scratch_live_count = 0;
        for (int li = 0; li < live_count; li++) {
            int ci = set_candidates[li];
            candidate_obligated[ci] = 1;
            if (root_candidates[ci].home_id <= 0) {
                set_candidates[scratch_live_count++] = ci;
            }
        }
        for (int left = 0; left < scratch_live_count; left++) {
            int ci = set_candidates[left];
            uint64_t* row = interference +
                (size_t)ci * (size_t)word_count;
            for (int right = left + 1; right < scratch_live_count; right++) {
                int cj = set_candidates[right];
                row[cj >> 6] |= UINT64_C(1) << (cj & 63);
                interference[(size_t)cj * (size_t)word_count +
                    (size_t)(ci >> 6)] |= UINT64_C(1) << (ci & 63);
            }
        }
    }
    int scratch_slot_count = 0;
    for (int ci = 0; ci < candidate_count; ci++) {
        if (root_candidates[ci].home_id > 0 ||
                !candidate_obligated[ci]) continue;
        memset(used_colors, 0, (size_t)candidate_count * sizeof(uint8_t));
        const uint64_t* row = interference +
            (size_t)ci * (size_t)word_count;
        int neighbor_count = em_root_collect_set_candidates(row, word_count,
            candidate_count, set_candidates);
        for (int ni = 0; ni < neighbor_count; ni++) {
            int cj = set_candidates[ni];
            if (cj >= ci) continue;
            int slot = candidate_slots[cj];
            if (slot >= stable_slot_count) {
                used_colors[slot - stable_slot_count] = 1;
            }
        }
        int color = 0;
        while (color < scratch_slot_count && used_colors[color]) color++;
        if (color == scratch_slot_count) scratch_slot_count++;
        candidate_slots[ci] = stable_slot_count + color;
    }

    int total_slot_count = stable_slot_count + scratch_slot_count;
    int dirty_word_count = (total_slot_count + 63) / 64;
    if (dirty_word_count < 1) dirty_word_count = 1;
    int successor_word_count = (block_count + 63) / 64;
    size_t dirty_block_words =
        (size_t)block_count * (size_t)dirty_word_count;
    size_t successor_block_words =
        (size_t)block_count * (size_t)successor_word_count;
    uint64_t* dirty_in = (uint64_t*)mem_alloc(
        dirty_block_words * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* dirty_out = (uint64_t*)mem_alloc(
        dirty_block_words * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* dirty_state = (uint64_t*)mem_alloc(
        (size_t)dirty_word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* dirty_preserve = (uint64_t*)mem_alloc(
        dirty_block_words * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* dirty_generate = (uint64_t*)mem_alloc(
        dirty_block_words * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* successors = (uint64_t*)mem_alloc(
        successor_block_words * sizeof(uint64_t), MEM_CAT_TEMP);
    if (!dirty_in || !dirty_out || !dirty_state || !dirty_preserve ||
            !dirty_generate || !successors) {
        log_error("mir-semantic-root-write-back: dirty-state allocation failed");
        abort();
    }
    memset(dirty_in, 0, dirty_block_words * sizeof(uint64_t));
    memset(dirty_out, 0, dirty_block_words * sizeof(uint64_t));
    memset(dirty_preserve, 0xff, dirty_block_words * sizeof(uint64_t));
    memset(dirty_generate, 0, dirty_block_words * sizeof(uint64_t));
    memset(successors, 0, successor_block_words * sizeof(uint64_t));
    for (int bi = 0; bi < block_count; bi++) {
        em_root_collect_block_successors(bi, blocks, block_count, instructions,
            labels, label_blocks, label_count, successors,
            successor_word_count);
    }
    // Dirty-state flow is a per-block transfer function:
    //     out = (in & preserve) | generate
    // Precomputing it avoids rescanning every instruction and every candidate
    // on each CFG iteration, which made large JS bundles spend minutes here.
    for (int bi = 0; bi < block_count; bi++) {
        uint64_t* preserve = dirty_preserve +
            (size_t)bi * (size_t)dirty_word_count;
        uint64_t* generate = dirty_generate +
            (size_t)bi * (size_t)dirty_word_count;
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            MIR_insn_t current = instructions[i];
            if (MIR_call_code_p(current->code) &&
                    em_root_call_may_collect(current, call_sites,
                        call_site_count)) {
                const uint64_t* live = instruction_live_in +
                    (size_t)i * (size_t)word_count;
                int live_count = em_root_collect_set_candidates(live, word_count,
                    candidate_count, set_candidates);
                for (int li = 0; li < live_count; li++) {
                    int ci = set_candidates[li];
                    int slot = candidate_slots[ci];
                    if (slot < 0) continue;
                    uint64_t bit = UINT64_C(1) << (slot & 63);
                    preserve[slot >> 6] &= ~bit;
                    generate[slot >> 6] &= ~bit;
                }
            }
            memset(insn_uses, 0, (size_t)word_count * sizeof(uint64_t));
            memset(insn_definitions, 0,
                (size_t)word_count * sizeof(uint64_t));
            em_collect_root_candidate_bits(em->ctx, current,
                reg_to_candidate, reg_map_count, insn_uses,
                insn_definitions);
            int definition_count = em_root_collect_set_candidates(insn_definitions,
                word_count, candidate_count, set_candidates);
            for (int di = 0; di < definition_count; di++) {
                int ci = set_candidates[di];
                int slot = candidate_slots[ci];
                if (slot >= 0) {
                    generate[slot >> 6] |=
                        UINT64_C(1) << (slot & 63);
                }
            }
        }
    }
    // Parameters are defined on entry even when the same MIR register is
    // reassigned later. Looking only for any defining instruction loses that
    // initial value and can leave a captured parameter unrooted at the first
    // allocation in the function.
    for (int ci = 0; ci < candidate_count; ci++) {
        int slot = candidate_slots[ci];
        if (slot >= 0 && (!candidate_defined[ci] ||
                em_root_is_function_argument_reg(em,
                    root_candidates[ci].reg))) {
            dirty_in[slot >> 6] |= UINT64_C(1) << (slot & 63);
        }
    }

    changed = true;
    while (changed) {
        changed = false;
        for (int bi = 0; bi < block_count; bi++) {
            uint64_t* block_in = dirty_in +
                (size_t)bi * (size_t)dirty_word_count;
            uint64_t* block_out = dirty_out +
                (size_t)bi * (size_t)dirty_word_count;
            const uint64_t* preserve = dirty_preserve +
                (size_t)bi * (size_t)dirty_word_count;
            const uint64_t* generate = dirty_generate +
                (size_t)bi * (size_t)dirty_word_count;
            for (int word = 0; word < dirty_word_count; word++) {
                uint64_t transferred =
                    (block_in[word] & preserve[word]) | generate[word];
                if (block_out[word] != transferred) {
                    block_out[word] = transferred;
                    changed = true;
                }
            }
            const uint64_t* successor_row = successors +
                (size_t)bi * (size_t)successor_word_count;
            for (int successor_word = 0;
                    successor_word < successor_word_count; successor_word++) {
                uint64_t successor_bits = successor_row[successor_word];
                while (successor_bits) {
                    int bit = em_root_take_lowest_set_bit(&successor_bits);
                    int successor = successor_word * 64 + bit;
                    if (successor >= block_count) continue;
                    uint64_t* successor_in = dirty_in +
                        (size_t)successor * (size_t)dirty_word_count;
                    for (int word = 0; word < dirty_word_count; word++) {
                        uint64_t merged = successor_in[word] | block_out[word];
                        if (merged != successor_in[word]) {
                            successor_in[word] = merged;
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    // All analysis allocations succeeded and the complete CFG solution is
    // available. Only now remove the write-through oracle stores.
    MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
    while (insn) {
        MIR_insn_t next = DLIST_NEXT(MIR_insn_t, insn);
        if (em_is_eager_root_store(insn, frame_base)) {
            int home = (int)(insn->ops[0].u.mem.disp /
                (MIR_disp_t)sizeof(uint64_t)) + 1;
            if (preserved_oracle_homes && home > 0 &&
                    home < preserved_oracle_home_count &&
                    preserved_oracle_homes[home]) {
                insn = next;
                continue;
            }
            MIR_remove_insn(em->ctx, em->func_item, insn);
        }
        insn = next;
    }

    int inserted_stores = retained_oracle_store_count;
    for (int bi = 0; bi < block_count; bi++) {
        memcpy(dirty_state,
            dirty_in + (size_t)bi * (size_t)dirty_word_count,
            (size_t)dirty_word_count * sizeof(uint64_t));
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            MIR_insn_t current = instructions[i];
            if (MIR_call_code_p(current->code) &&
                    em_root_call_may_collect(current, call_sites,
                        call_site_count)) {
                const uint64_t* live = instruction_live_in +
                    (size_t)i * (size_t)word_count;
                int live_count = em_root_collect_set_candidates(live, word_count,
                    candidate_count, set_candidates);
                for (int li = 0; li < live_count; li++) {
                    int ci = set_candidates[li];
                    int slot = candidate_slots[ci];
                    if (slot < 0 || (dirty_state[slot >> 6] &
                            (UINT64_C(1) << (slot & 63))) == 0) {
                        continue;
                    }
                    MIR_reg_t reg = root_candidates[ci].reg;
                    MIR_type_t type = MIR_reg_type(em->ctx, reg,
                        em->func);
                    if (type != MIR_T_P) type = MIR_T_I64;
                    MIR_insn_t store = MIR_new_insn(em->ctx, MIR_MOV,
                        MIR_new_mem_op(em->ctx, type,
                            (MIR_disp_t)slot *
                                (MIR_disp_t)sizeof(uint64_t),
                            frame_base, 0, 1),
                        MIR_new_reg_op(em->ctx, reg));
                    MIR_insert_insn_before(em->ctx, em->func_item,
                        current, store);
                    dirty_state[slot >> 6] &=
                        ~(UINT64_C(1) << (slot & 63));
                    inserted_stores++;
                }
            }
            memset(insn_uses, 0, (size_t)word_count * sizeof(uint64_t));
            memset(insn_definitions, 0,
                (size_t)word_count * sizeof(uint64_t));
            em_collect_root_candidate_bits(em->ctx, current,
                reg_to_candidate, reg_map_count, insn_uses, insn_definitions);
            int definition_count = em_root_collect_set_candidates(insn_definitions,
                word_count, candidate_count, set_candidates);
            for (int di = 0; di < definition_count; di++) {
                int ci = set_candidates[di];
                int slot = candidate_slots[ci];
                if (slot >= 0) {
                    dirty_state[slot >> 6] |=
                        UINT64_C(1) << (slot & 63);
                }
            }
        }
    }
    if (result) {
        result->stable_slots = stable_slot_count;
        result->scratch_slots = scratch_slot_count;
        result->inserted_stores = inserted_stores;
        result->instruction_count = instruction_count;
        result->block_count = block_count;
    }
    const char* log_slots = getenv("LAMBDA_MIR_LOG_FRAME_SLOTS");
    if (log_slots && log_slots[0] && strcmp(log_slots, "0") != 0) {
        log_info("mir-semantic-root-write-back: function=%s candidates=%d instructions=%d blocks=%d call_sites=%d stable=%d scratch=%d root_stores=%d",
            log_label ? log_label : "<anonymous>", candidate_count,
            instruction_count, block_count, call_site_count,
            stable_slot_count, scratch_slot_count, inserted_stores);
    }
    mem_free(instructions); mem_free(blocks); mem_free(instruction_blocks);
    mem_free(labels); mem_free(label_blocks); mem_free(reg_to_candidate);
    mem_free(insn_uses); mem_free(insn_definitions);
    mem_free(block_uses); mem_free(block_definitions);
    mem_free(live_in); mem_free(live_out);
    mem_free(scratch_out); mem_free(scratch_in);
    mem_free(instruction_live_in); mem_free(candidate_defined);
    mem_free(home_to_slot); mem_free(candidate_slots); mem_free(set_candidates);
    mem_free(interference); mem_free(used_colors);
    mem_free(candidate_obligated);
    mem_free(dirty_in); mem_free(dirty_out); mem_free(dirty_state);
    mem_free(dirty_preserve); mem_free(dirty_generate);
    mem_free(successors);
    return true;
}

static inline bool em_finalize_recorded_roots(MirEmitter* em,
        MIR_reg_t frame_base, MIR_insn_t anchor,
        MirRootWriteBackResult* result, const char* log_label) {
    MirFrameState* frame = em ? &em->frame : NULL;
    if (!frame || frame->gc_candidate_count == 0) return true;
    int home_count = frame->root_slot_count + 1;
    uint8_t* preserved = (uint8_t*)mem_calloc(
        (size_t)home_count, sizeof(uint8_t), MEM_CAT_TEMP);
    if (!preserved) {
        log_error("mir-root-write-back: preserved-home allocation failed");
        abort();
    }
    int retained = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) continue;
        int home = (int)(insn->ops[0].u.mem.disp /
            (MIR_disp_t)sizeof(uint64_t)) + 1;
        if (home <= 0 || home >= home_count) {
            log_error("mir-root-write-back: invalid retained home %d", home);
            abort();
        }
        preserved[home] = 1;
        retained++;
    }
    bool optimized = em_finalize_semantic_root_write_back(em, frame_base,
        anchor, retained > 0, frame->root_slot_count,
        &frame->gc_candidates, &frame->gc_candidate_count,
        &frame->gc_candidate_capacity, &frame->gc_candidate_by_reg,
        &frame->gc_candidate_by_reg_capacity, frame->gc_call_sites,
        frame->gc_call_site_count, result, log_label, preserved, home_count,
        retained > 0, retained);
    mem_free(preserved);
    return optimized;
}

static inline int em_scalar_home_new(MirEmitter* em) {
    if (!em || !em->frame.active) return 0;
    return ++em->frame.scalar_home_count;
}

static inline void em_scalar_home_bind(MirEmitter* em, int logical_home_id,
        MIR_reg_t reg) {
    if (!em || logical_home_id <= 0 || !reg) return;
    MirFrameState* frame = &em->frame;
    if (frame->scalar_home_binding_count >=
            frame->scalar_home_binding_capacity) {
        int capacity = frame->scalar_home_binding_capacity
            ? frame->scalar_home_binding_capacity * 2 : 16;
        MirScalarHomeBinding* bindings = (MirScalarHomeBinding*)mem_realloc(
            frame->scalar_home_bindings,
            (size_t)capacity * sizeof(MirScalarHomeBinding), MEM_CAT_TEMP);
        if (!bindings) {
            log_error("mir-scalar-homes: unable to grow binding table");
            abort();
        }
        frame->scalar_home_bindings = bindings;
        frame->scalar_home_binding_capacity = capacity;
    }
    frame->scalar_home_bindings[frame->scalar_home_binding_count++] = {
        reg, logical_home_id
    };
}

static inline int em_scalar_home_for_reg(MirEmitter* em, MIR_reg_t reg) {
    if (!em || !reg) return 0;
    for (int i = em->frame.scalar_home_binding_count - 1; i >= 0; i--) {
        if (em->frame.scalar_home_bindings[i].reg == reg) {
            return em->frame.scalar_home_bindings[i].logical_home_id;
        }
    }
    return 0;
}

static inline MirFrameRef em_scalar_home_ref(MirEmitter* em,
        int logical_home_id) {
    if (!em || logical_home_id <= 0) return {MIR_FRAME_REF_NONE, 0};
    return {MIR_FRAME_REF_LOCAL_SCALAR_HOME, logical_home_id};
}

static inline MirFrameRef em_discard_scalar_home_ref(void) {
    return {MIR_FRAME_REF_DISCARD_SCRATCH, 0};
}

static inline MirFrameRef em_fixed_number_scratch_ref(int slot) {
    return slot >= 0 ? MirFrameRef{MIR_FRAME_REF_FIXED_SCRATCH, slot}
        : MirFrameRef{MIR_FRAME_REF_NONE, 0};
}

static inline MIR_reg_t em_materialize_frame_ref(MirEmitter* em,
        MirFrameRef ref) {
    if (!em) return 0;
    if (ref.kind == MIR_FRAME_REF_INCOMING_CALLER_HOME) {
        return em->frame.incoming_scalar_home;
    }
    if ((ref.kind != MIR_FRAME_REF_LOCAL_SCALAR_HOME ||
            ref.logical_home_id <= 0) &&
            ref.kind != MIR_FRAME_REF_DISCARD_SCRATCH &&
            (ref.kind != MIR_FRAME_REF_FIXED_SCRATCH ||
             ref.logical_home_id < 0)) return 0;
    MirFrameState* frame = &em->frame;
    if (ref.kind == MIR_FRAME_REF_DISCARD_SCRATCH) {
        frame->discard_scalar_home_slot = -1;
    }
    MIR_reg_t address = em_new_reg(em, "scalar_home", MIR_T_I64);
    MIR_insn_t add = MIR_new_insn(em->ctx, MIR_ADD,
        MIR_new_reg_op(em->ctx, address),
        MIR_new_reg_op(em->ctx, frame->number_base),
        MIR_new_int_op(em->ctx, 0));
    em_emit_insn(em, add);
    if (frame->scalar_home_fixup_count >= frame->scalar_home_fixup_capacity) {
        int capacity = frame->scalar_home_fixup_capacity
            ? frame->scalar_home_fixup_capacity * 2 : 16;
        MirScalarHomeFixup* fixups = (MirScalarHomeFixup*)mem_realloc(
            frame->scalar_home_fixups,
            (size_t)capacity * sizeof(MirScalarHomeFixup), MEM_CAT_TEMP);
        if (!fixups) {
            log_error("mir-scalar-homes: unable to grow fixup table");
            abort();
        }
        frame->scalar_home_fixups = fixups;
        frame->scalar_home_fixup_capacity = capacity;
    }
    frame->scalar_home_fixups[frame->scalar_home_fixup_count++] = {
        add, ref.kind, ref.logical_home_id
    };
    return address;
}

static inline void em_scalar_set_bit(uint64_t* bits, int home_id) {
    if (bits && home_id > 0) {
        int index = home_id - 1;
        bits[index >> 6] |= UINT64_C(1) << (index & 63);
    }
}

static inline void em_scalar_union_reg_bits(uint64_t* bits,
        const uint64_t* reg_homes, int reg, int reg_count, int word_count) {
    if (!bits || !reg_homes || reg <= 0 || reg >= reg_count) return;
    em_root_union_bits(bits,
        reg_homes + (size_t)reg * (size_t)word_count, word_count);
}

static inline void em_scalar_collect_insn_bits(MIR_context_t ctx,
        MIR_insn_t insn, const uint64_t* reg_homes,
        const uint64_t* reg_def_homes, int reg_count, int word_count,
        uint64_t* uses, uint64_t* definitions) {
    memset(uses, 0, (size_t)word_count * sizeof(uint64_t));
    memset(definitions, 0, (size_t)word_count * sizeof(uint64_t));
    size_t operand_count = MIR_insn_nops(ctx, insn);
    for (size_t oi = 0; oi < operand_count; oi++) {
        int output = 0;
        MIR_insn_op_mode(ctx, insn, oi, &output);
        MIR_op_t* op = &insn->ops[oi];
        if (op->mode == MIR_OP_REG) {
            const uint64_t* map = output ? reg_def_homes : reg_homes;
            em_scalar_union_reg_bits(output ? definitions : uses, map,
                (int)op->u.reg, reg_count, word_count);
        } else if (op->mode == MIR_OP_MEM) {
            em_scalar_union_reg_bits(uses, reg_homes, (int)op->u.mem.base,
                reg_count, word_count);
            em_scalar_union_reg_bits(uses, reg_homes, (int)op->u.mem.index,
                reg_count, word_count);
        }
    }
}

static inline void em_scalar_add_interference(uint64_t* interference,
        int word_count, int left, int right) {
    if (!interference || left < 0 || right < 0 || left == right) return;
    interference[(size_t)left * (size_t)word_count + (right >> 6)] |=
        UINT64_C(1) << (right & 63);
    interference[(size_t)right * (size_t)word_count + (left >> 6)] |=
        UINT64_C(1) << (left & 63);
}

static inline int em_finalize_scalar_homes(MirEmitter* em) {
    if (!em || !em->func || !em->func_item) return 0;
    MirFrameState* frame = &em->frame;
    int home_count = frame->scalar_home_count;
    if (home_count == 0) {
        frame->colored_scalar_home_count = 0;
        int discard_count = frame->discard_scalar_home_slot < 0 ? 1 : 0;
        frame->discard_scalar_home_slot = discard_count ? 0 : -1;
        for (int i = 0; i < frame->scalar_home_fixup_count; i++) {
            MirScalarHomeFixup* fixup = &frame->scalar_home_fixups[i];
            if (!fixup->address_insn ||
                    (fixup->kind != MIR_FRAME_REF_DISCARD_SCRATCH &&
                     fixup->kind != MIR_FRAME_REF_FIXED_SCRATCH)) {
                log_error("mir-scalar-homes: unresolved zero-home fixup");
                abort();
            }
            int slot = fixup->kind == MIR_FRAME_REF_DISCARD_SCRATCH
                ? frame->discard_scalar_home_slot
                : discard_count + fixup->logical_home_id;
            fixup->address_insn->ops[2].u.i = (int64_t)slot *
                (int64_t)sizeof(uint64_t);
        }
        frame->fixed_number_slots = frame->plan.fixed_number_scratch_slots +
            discard_count;
        return 0;
    }
    int word_count = (home_count + 63) / 64;
    int instruction_count = 0;
    int max_reg = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        instruction_count++;
        size_t operand_count = MIR_insn_nops(em->ctx, insn);
        for (size_t oi = 0; oi < operand_count; oi++) {
            MIR_op_t* op = &insn->ops[oi];
            if (op->mode == MIR_OP_REG && (int)op->u.reg > max_reg) {
                max_reg = (int)op->u.reg;
            } else if (op->mode == MIR_OP_MEM) {
                if ((int)op->u.mem.base > max_reg) max_reg = (int)op->u.mem.base;
                if ((int)op->u.mem.index > max_reg) max_reg = (int)op->u.mem.index;
            }
        }
    }
    int reg_count = max_reg + 1;
    MIR_insn_t* instructions = (MIR_insn_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_insn_t), MEM_CAT_TEMP);
    MirRootLivenessBlock* blocks = (MirRootLivenessBlock*)mem_alloc(
        (size_t)instruction_count * sizeof(MirRootLivenessBlock), MEM_CAT_TEMP);
    MIR_label_t* labels = (MIR_label_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_label_t), MEM_CAT_TEMP);
    int* label_blocks = (int*)mem_alloc(
        (size_t)instruction_count * sizeof(int), MEM_CAT_TEMP);
    uint64_t* reg_homes = (uint64_t*)mem_calloc(
        (size_t)reg_count * (size_t)word_count, sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* reg_def_homes = (uint64_t*)mem_calloc(
        (size_t)reg_count * (size_t)word_count, sizeof(uint64_t), MEM_CAT_TEMP);
    if (!instructions || !blocks || !labels || !label_blocks || !reg_homes ||
            !reg_def_homes) {
        log_error("mir-scalar-homes: CFG allocation failed");
        abort();
    }
    for (int i = 0; i < frame->scalar_home_binding_count; i++) {
        MirScalarHomeBinding* binding = &frame->scalar_home_bindings[i];
        if ((int)binding->reg >= reg_count || binding->logical_home_id <= 0 ||
                binding->logical_home_id > home_count) continue;
        uint64_t* homes = reg_homes +
            (size_t)binding->reg * (size_t)word_count;
        uint64_t* definitions = reg_def_homes +
            (size_t)binding->reg * (size_t)word_count;
        em_scalar_set_bit(homes, binding->logical_home_id);
        em_scalar_set_bit(definitions, binding->logical_home_id);
    }
    bool propagated = true;
    while (propagated) {
        propagated = false;
        for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
                insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
            if (insn->code != MIR_MOV || insn->nops < 2 ||
                    insn->ops[0].mode != MIR_OP_REG ||
                    insn->ops[1].mode != MIR_OP_REG) continue;
            int destination = (int)insn->ops[0].u.reg;
            int source = (int)insn->ops[1].u.reg;
            uint64_t* dst = reg_homes +
                (size_t)destination * (size_t)word_count;
            const uint64_t* src = reg_homes +
                (size_t)source * (size_t)word_count;
            for (int word = 0; word < word_count; word++) {
                uint64_t merged = dst[word] | src[word];
                if (merged != dst[word]) {
                    dst[word] = merged;
                    propagated = true;
                }
            }
        }
    }
    int ii = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) instructions[ii++] = insn;
    int block_count = 0;
    int block_start = 0;
    for (int i = 0; i < instruction_count; i++) {
        bool starts = i == 0 || instructions[i]->code == MIR_LABEL ||
            em_root_block_terminates(instructions[i - 1]);
        if (starts && i > block_start) {
            blocks[block_count++] = {block_start, i};
            block_start = i;
        }
    }
    if (block_start < instruction_count) {
        blocks[block_count++] = {block_start, instruction_count};
    }
    int label_count = 0;
    for (int bi = 0; bi < block_count; bi++) {
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            if (instructions[i]->code == MIR_LABEL) {
                labels[label_count] = instructions[i];
                label_blocks[label_count++] = bi;
            }
        }
    }
    size_t block_words = (size_t)block_count * (size_t)word_count;
    uint64_t* block_uses = (uint64_t*)mem_calloc(
        block_words, sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* block_defs = (uint64_t*)mem_calloc(
        block_words, sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* live_in = (uint64_t*)mem_calloc(
        block_words, sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* live_out = (uint64_t*)mem_calloc(
        block_words, sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* uses = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* definitions = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    uint64_t* scratch = (uint64_t*)mem_alloc(
        (size_t)word_count * sizeof(uint64_t), MEM_CAT_TEMP);
    if (!block_uses || !block_defs || !live_in || !live_out || !uses ||
            !definitions || !scratch) {
        log_error("mir-scalar-homes: liveness allocation failed");
        abort();
    }
    for (int bi = 0; bi < block_count; bi++) {
        uint64_t* bu = block_uses + (size_t)bi * (size_t)word_count;
        uint64_t* bd = block_defs + (size_t)bi * (size_t)word_count;
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            em_scalar_collect_insn_bits(em->ctx, instructions[i], reg_homes,
                reg_def_homes, reg_count, word_count, uses, definitions);
            for (int word = 0; word < word_count; word++) {
                bu[word] |= uses[word] & ~bd[word];
                bd[word] |= definitions[word];
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int bi = block_count - 1; bi >= 0; bi--) {
            em_root_compute_block_live_out(bi, blocks, block_count,
                instructions, labels, label_blocks, label_count, live_in,
                scratch, word_count);
            uint64_t* out = live_out + (size_t)bi * (size_t)word_count;
            uint64_t* in = live_in + (size_t)bi * (size_t)word_count;
            uint64_t* bu = block_uses + (size_t)bi * (size_t)word_count;
            uint64_t* bd = block_defs + (size_t)bi * (size_t)word_count;
            for (int word = 0; word < word_count; word++) {
                uint64_t next_in = bu[word] | (scratch[word] & ~bd[word]);
                if (out[word] != scratch[word] || in[word] != next_in) {
                    changed = true;
                }
                out[word] = scratch[word];
                in[word] = next_in;
            }
        }
    }
    uint64_t* interference = (uint64_t*)mem_calloc(
        (size_t)home_count * (size_t)word_count,
        sizeof(uint64_t), MEM_CAT_TEMP);
    int* set_homes = (int*)mem_alloc(
        (size_t)home_count * sizeof(int), MEM_CAT_TEMP);
    int* colors = (int*)mem_alloc(
        (size_t)home_count * sizeof(int), MEM_CAT_TEMP);
    uint8_t* used = (uint8_t*)mem_alloc(
        (size_t)home_count * sizeof(uint8_t), MEM_CAT_TEMP);
    if (!interference || !set_homes || !colors || !used) {
        log_error("mir-scalar-homes: coloring allocation failed");
        abort();
    }
    for (int bi = block_count - 1; bi >= 0; bi--) {
        memcpy(scratch, live_out + (size_t)bi * (size_t)word_count,
            (size_t)word_count * sizeof(uint64_t));
        for (int i = blocks[bi].end - 1; i >= blocks[bi].start; i--) {
            em_scalar_collect_insn_bits(em->ctx, instructions[i], reg_homes,
                reg_def_homes, reg_count, word_count, uses, definitions);
            int def_count = em_root_collect_set_candidates(definitions,
                word_count, home_count, set_homes);
            for (int di = 0; di < def_count; di++) {
                int defined = set_homes[di];
                for (int word = 0; word < word_count; word++) {
                    uint64_t pending = scratch[word];
                    while (pending) {
                        int bit = em_root_take_lowest_set_bit(&pending);
                        int live = word * 64 + bit;
                        if (live < home_count) {
                            em_scalar_add_interference(interference,
                                word_count, defined, live);
                        }
                    }
                }
            }
            for (int word = 0; word < word_count; word++) {
                scratch[word] = (scratch[word] & ~definitions[word]) |
                    uses[word];
            }
        }
    }
    int color_count = 0;
    for (int home = 0; home < home_count; home++) {
        memset(used, 0, (size_t)home_count * sizeof(uint8_t));
        const uint64_t* row = interference +
            (size_t)home * (size_t)word_count;
        for (int prior = 0; prior < home; prior++) {
            if ((row[prior >> 6] & (UINT64_C(1) << (prior & 63))) != 0 &&
                    colors[prior] >= 0) used[colors[prior]] = 1;
        }
        int color = 0;
        while (color < color_count && used[color]) color++;
        if (color == color_count) color_count++;
        colors[home] = color;
    }
    int discard_count = frame->discard_scalar_home_slot < 0 ? 1 : 0;
    frame->discard_scalar_home_slot = discard_count
        ? color_count : -1;
    for (int i = 0; i < frame->scalar_home_fixup_count; i++) {
        MirScalarHomeFixup* fixup = &frame->scalar_home_fixups[i];
        int home = fixup->logical_home_id - 1;
        if (!fixup->address_insn || fixup->address_insn->nops < 3 ||
                fixup->address_insn->ops[2].mode != MIR_OP_INT ||
                (fixup->kind == MIR_FRAME_REF_LOCAL_SCALAR_HOME &&
                 (home < 0 || home >= home_count)) ||
                (fixup->kind != MIR_FRAME_REF_LOCAL_SCALAR_HOME &&
                 fixup->kind != MIR_FRAME_REF_DISCARD_SCRATCH &&
                 (fixup->kind != MIR_FRAME_REF_FIXED_SCRATCH ||
                  fixup->logical_home_id < 0 ||
                  fixup->logical_home_id >=
                      frame->plan.fixed_number_scratch_slots))) {
            log_error("mir-scalar-homes: unresolved address fixup");
            abort();
        }
        int slot = fixup->kind == MIR_FRAME_REF_DISCARD_SCRATCH
            ? frame->discard_scalar_home_slot
            : fixup->kind == MIR_FRAME_REF_FIXED_SCRATCH
                ? color_count + discard_count + fixup->logical_home_id
                : colors[home];
        fixup->address_insn->ops[2].u.i = (int64_t)slot *
            (int64_t)sizeof(uint64_t);
    }
    frame->colored_scalar_home_count = color_count;
    frame->fixed_number_slots = color_count + discard_count +
        frame->plan.fixed_number_scratch_slots;
    mem_free(instructions); mem_free(blocks); mem_free(labels);
    mem_free(label_blocks); mem_free(reg_homes); mem_free(reg_def_homes);
    mem_free(block_uses); mem_free(block_defs); mem_free(live_in);
    mem_free(live_out); mem_free(uses); mem_free(definitions);
    mem_free(scratch); mem_free(interference); mem_free(set_homes);
    mem_free(colors); mem_free(used);
    return color_count;
}

static inline void em_finalize_function_metadata(MirEmitter* em) {
    if (!em || !em->func) return;
    MirFrameState* frame = &em->frame;
    MirFunctionPlan* plan = &frame->plan;
    if (plan->entry_kind == FN_ENTRY_PUBLIC_WRAPPER &&
            plan->entry_mode != MIR_ENTRY_CHECKED) {
        log_error("mir-verify: public entry is not checked: %s",
            plan->debug_name ? plan->debug_name : "<unnamed>");
        abort();
    }
    if (plan->accepts_caller_scalar_home !=
            (frame->incoming_scalar_home != 0) ||
            (plan->scalar_home_lane_mask != 0) !=
            plan->accepts_caller_scalar_home) {
        log_error("mir-verify: inconsistent scalar-home ABI: %s",
            plan->debug_name ? plan->debug_name : "<unnamed>");
        abort();
    }
    if (plan->accepts_caller_scalar_home) {
        MIR_var_t* vars = VARR_ADDR(MIR_var_t, em->func->vars);
        if (em->func->nargs == 0 ||
                vars[em->func->nargs - 1].type != MIR_T_P) {
            log_error("mir-verify: scalar home is not the final pointer operand: %s",
                plan->debug_name ? plan->debug_name : "<unnamed>");
            abort();
        }
    }
    for (int i = 0; i < frame->scalar_home_fixup_count; i++) {
        MIR_insn_t add = frame->scalar_home_fixups[i].address_insn;
        int64_t displacement = add && add->nops >= 3 &&
            add->ops[2].mode == MIR_OP_INT ? add->ops[2].u.i : -1;
        if (displacement < 0 || displacement % (int64_t)sizeof(uint64_t) ||
                displacement >= (int64_t)frame->fixed_number_slots *
                    (int64_t)sizeof(uint64_t)) {
            log_error("mir-verify: scalar-home fixup escaped fixed prefix: %s",
                plan->debug_name ? plan->debug_name : "<unnamed>");
            abort();
        }
    }
    uint32_t instruction_count = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (insn->code != MIR_LABEL) instruction_count++;
    }
    em->last_function = {plan->debug_name, plan->entry_kind,
        plan->entry_mode == MIR_ENTRY_BOUND_INTERNAL,
        (uint16_t)frame->root_slot_count,
        (uint16_t)frame->colored_scalar_home_count,
        (uint16_t)frame->plan.fixed_number_scratch_slots,
        instruction_count, (uint32_t)frame->may_gc_call_count,
        (uint32_t)frame->root_store_count};
    const char* enabled = getenv("LAMBDA_MIR_LOG_FRAME_SLOTS");
    if (enabled && enabled[0] && strcmp(enabled, "0") != 0) {
        log_info("mir-function: function=%s entry=%d bound=%d instructions=%u roots=%u root_stores=%u scalar_homes=%u number_scratch=%u safepoints=%u",
            plan->debug_name ? plan->debug_name : "<unnamed>",
            (int)plan->entry_kind,
            plan->entry_mode == MIR_ENTRY_BOUND_INTERNAL,
            instruction_count, (unsigned)frame->root_slot_count,
            (unsigned)frame->root_store_count,
            (unsigned)frame->colored_scalar_home_count,
            (unsigned)frame->plan.fixed_number_scratch_slots,
            (unsigned)frame->may_gc_call_count);
    }
}

static inline void em_before_resolved_call(MirEmitter* em,
        const char* call_name, const JitCallMetadata* metadata, int nargs,
        MIR_type_t* arg_types, MIR_op_t* arg_ops) {
    if (!em || !metadata) return;
    em_validate_call_abi(call_name, metadata, nargs, arg_types);
    if (em->note_mir_call) em->note_mir_call(call_name);
    bool may_gc = metadata->effects.gc != JIT_EFFECT_NO_GC;
    if (may_gc) {
        em->frame.may_gc_call_count++;
        if (em->before_may_gc_call) em->before_may_gc_call(em->call_owner);
        if (em->root_call_value) {
            for (int i = 0; i < nargs; i++) {
                if (arg_ops[i].mode == MIR_OP_REG && mir_gc_value_needs_root(
                        metadata->abi_args[i].value.value_class, arg_types[i])) {
                    em->root_call_value(em->call_owner, arg_ops[i].u.reg);
                }
            }
        }
    } else {
        em->frame.no_gc_call_count++;
    }
}

static inline void em_after_resolved_call(MirEmitter* em,
        const char* call_name, const JitCallMetadata* metadata,
        MIR_insn_t call, MIR_reg_t result, MIR_type_t ret_type) {
    if (!em || !metadata || !call) return;
    if (!em_root_note_call_site(
            &em->frame.gc_call_sites, &em->frame.gc_call_site_count,
            &em->frame.gc_call_site_capacity, call, metadata->effects.gc,
            metadata->effects.exception,
            call_name && strcmp(call_name, "js_check_exception") == 0)) {
        log_error("mir-call: unable to record call site for %s", call_name);
        abort();
    }
    if (!result) return;
    if (em->after_call_result) {
        em->after_call_result(em->call_owner, result, ret_type);
    }
    if (em->root_call_value && mir_gc_value_needs_root(
            metadata->normal_result.value.value_class, ret_type)) {
        em->root_call_value(em->call_owner, result);
    }
}

static inline MIR_reg_t em_heap_rehome_item_arg(MirEmitter* em,
        MIR_reg_t item) {
    MIR_var_t arg = {MIR_T_I64, "item", 0};
    MirImportEntry* import = em_ensure_import(em, "lambda_item_heap_rehome",
        MIR_T_I64, 1, &arg, 1, false);
    if (!import) {
        log_error("mir-call: missing lambda_item_heap_rehome import");
        abort();
    }
    MIR_type_t type = MIR_T_I64;
    MIR_op_t op = MIR_new_reg_op(em->ctx, item);
    em_before_resolved_call(em, "lambda_item_heap_rehome",
        &import->call, 1, &type, &op);
    MIR_reg_t result = em_new_reg(em, "persistent_arg", MIR_T_I64);
    MIR_insn_t call = mir_new_call_with_args(em->ctx, import->proto,
        import->import, result, 1, &op);
    mir_append_emit_insn(em->ctx, em->func_item, call);
    em_after_resolved_call(em, "lambda_item_heap_rehome", &import->call,
        call, result, MIR_T_I64);
    return result;
}

static inline void em_enforce_argument_ownership(MirEmitter* em,
        const JitCallMetadata* metadata, int nargs, MIR_op_t* arg_ops) {
    if (!em || !metadata || !arg_ops) return;
    for (int i = 0; i < nargs; i++) {
        uint16_t effects = metadata->abi_args[i].effects;
        if (!(effects & (JIT_ARG_MAY_CAPTURE | JIT_ARG_PERSISTENT_STORE |
                JIT_ARG_EFFECT_UNKNOWN)) ||
                metadata->abi_args[i].value.value_class !=
                    JIT_VALUE_BOXED_ITEM ||
                arg_ops[i].mode != MIR_OP_REG ||
                !em_scalar_home_for_reg(em, arg_ops[i].u.reg)) {
            continue;
        }
        arg_ops[i] = MIR_new_reg_op(em->ctx,
            em_heap_rehome_item_arg(em, arg_ops[i].u.reg));
    }
}

// Conservatively make metadata-free calls and values participate in safepoints.
static inline void em_emit_unknown_call(MirEmitter* em, MIR_insn_t insn) {
    if (!em || !insn) return;
    const char* name = "<unresolved-call>";
    if (em->note_mir_call) em->note_mir_call(name);
    size_t count = MIR_insn_nops(em->ctx, insn);
    for (size_t i = 0; i < count; i++) {
        int output = 0;
        MIR_insn_op_mode(em->ctx, insn, i, &output);
        if (!output && insn->ops[i].mode == MIR_OP_REG) {
            MIR_reg_t reg = insn->ops[i].u.reg;
            // Retaining callees require arguments outside activation homes.
            if (em_scalar_home_for_reg(em, reg)) {
                reg = em_heap_rehome_item_arg(em, reg);
                insn->ops[i] = MIR_new_reg_op(em->ctx, reg);
            }
            if (em->root_call_value) em->root_call_value(em->call_owner, reg);
        }
    }
    em->frame.may_gc_call_count++;
    if (em->before_may_gc_call) em->before_may_gc_call(em->call_owner);
    mir_append_emit_insn(em->ctx, em->func_item, insn);
    if (!em_root_note_call_site(
            &em->frame.gc_call_sites, &em->frame.gc_call_site_count,
            &em->frame.gc_call_site_capacity, insn, JIT_EFFECT_MAY_GC,
            JIT_EXCEPTION_MAY_SET, false)) {
        log_error("mir-call: unable to record unresolved call site");
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        int output = 0;
        MIR_insn_op_mode(em->ctx, insn, i, &output);
        if (output && insn->ops[i].mode == MIR_OP_REG) {
            MIR_reg_t reg = insn->ops[i].u.reg;
            MIR_type_t type = MIR_reg_type(em->ctx, reg, em->func);
            if (em->after_call_result) {
                em->after_call_result(em->call_owner, reg, type);
            }
            if (em->root_call_value) em->root_call_value(em->call_owner, reg);
        }
    }
}

static inline MIR_reg_t em_call_with_args(MirEmitter* em,
                                          const char* fn_name,
                                          MIR_type_t ret_type,
                                          int nargs,
                                          MIR_type_t* arg_types,
                                          MIR_op_t* arg_ops,
                                          bool include_signature) {
    if (nargs < 0 || nargs > MIR_SHARED_MAX_CALL_ARGS) return 0;
    MIR_var_t args[MIR_SHARED_MAX_CALL_ARGS];
    if (nargs > 0) mir_prepare_call_args(args, arg_types, nargs);
    MirImportEntry* ie = em_ensure_import(em, fn_name, ret_type, nargs,
        nargs ? args : NULL, 1, include_signature);
    if (!ie) return 0;
    // Snapshot before ownership enforcement can resize the import hashmap.
    MirImportEntry resolved = *ie;
    resolved.call.abi_args = resolved.abi_args;
    if (resolved.call.abi_arg_count != (uint16_t)nargs) {
        log_error("mir-call: ABI arity mismatch for %s: expected=%u actual=%d",
            fn_name, (unsigned)resolved.call.abi_arg_count, nargs);
        abort();
    }
    em_enforce_argument_ownership(em, &resolved.call, nargs, arg_ops);
    MirScalarReturnMode scalar_mode = em_scalar_return_mode_for_class(
        resolved.call.normal_result.scalar_class);
    MIR_reg_t source_base = 0;
    MIR_reg_t scalar_home = 0;
    int scalar_home_id = 0;
    if (scalar_mode != MIR_SCALAR_RETURN_NONE && em->frame.active &&
            em->frame.number_base) {
        scalar_home_id = em_scalar_home_new(em);
        scalar_home = em_materialize_frame_ref(em,
            em_scalar_home_ref(em, scalar_home_id));
        source_base = em_load_frame_top(em, em->frame.runtime,
            offsetof(Context, side_number_top), "call_number_base");
    }
    em_before_resolved_call(em, fn_name, &resolved.call, nargs,
        arg_types, arg_ops);

    MIR_reg_t res = em_new_reg(em, fn_name, ret_type);
    MIR_insn_t call = mir_new_call_with_args(em->ctx, resolved.proto,
        resolved.import, res, nargs, arg_ops);
    mir_append_emit_insn(em->ctx, em->func_item, call);
    em_after_resolved_call(em, fn_name, &resolved.call, call, res, ret_type);
    if (scalar_home_id) {
        res = em_adopt_scalar_item(em, scalar_mode, res,
            em->frame.runtime, offsetof(Context, side_number_top),
            source_base, scalar_home);
        em_scalar_home_bind(em, scalar_home_id, res);
    }
    return res;
}

static inline void em_call_void_with_args(MirEmitter* em,
                                          const char* fn_name,
                                          int nargs,
                                          MIR_type_t* arg_types,
                                          MIR_op_t* arg_ops,
                                          bool include_signature) {
    if (nargs < 0 || nargs > MIR_SHARED_MAX_CALL_ARGS) return;
    MIR_var_t args[MIR_SHARED_MAX_CALL_ARGS];
    if (nargs > 0) {
        mir_prepare_call_args(args, arg_types, nargs);
    }
    MirImportEntry* ie = em_ensure_import(em, fn_name, MIR_T_I64, nargs,
        nargs ? args : NULL, 0, include_signature);
    if (!ie) return;

    em_before_resolved_call(em, fn_name, &ie->call, nargs,
        arg_types, arg_ops);

    MIR_insn_t call = mir_new_call_with_args(em->ctx, ie->proto, ie->import,
        0, nargs, arg_ops);
    mir_append_emit_insn(em->ctx, em->func_item, call);
    em_after_resolved_call(em, fn_name, &ie->call, call, 0, MIR_T_I64);
}

static inline MIR_type_t em_mir_type_for_rep(ValueRep rep) {
    // MIR's virtual register file has one canonical 64-bit integer carrier.
    // VALUE_REP_U64 preserves signedness for opcode/conversion selection.
    return rep == VALUE_REP_F64 ? MIR_T_D : rep == VALUE_REP_RAW_GC_POINTER ||
        rep == VALUE_REP_RAW_NON_GC_POINTER ? MIR_T_P : MIR_T_I64;
}

static inline JitValueClass em_value_class_for_rep(ValueRep rep) {
    return rep == VALUE_REP_ITEM ? JIT_VALUE_BOXED_ITEM
        : rep == VALUE_REP_RAW_GC_POINTER ? JIT_VALUE_RAW_GC_POINTER
        : rep == VALUE_REP_RAW_NON_GC_POINTER ? JIT_VALUE_RAW_NON_GC_POINTER
        : JIT_VALUE_NON_GC_SCALAR;
}

static inline MirCallResult em_call_direct(MirEmitter* em,
        const char* call_name, MIR_item_t target,
        const FnVariantAnalysis* variant, int source_nargs,
        MIR_type_t* arg_types, MIR_op_t* arg_ops,
        const MirCallOptions* options = NULL) {
    MirCallResult call_result = {};
    if (!em || !target || source_nargs < 0) return call_result;
    bool accepts_scalar_home = variant &&
        variant->result.scalar_home_lane_mask != 0;
    int nargs = source_nargs + (accepts_scalar_home ? 1 : 0);
    MIR_var_t* args = nargs > 0 ? (MIR_var_t*)mem_alloc(
        (size_t)nargs * sizeof(MIR_var_t), MEM_CAT_TEMP) : NULL;
    JitAbiArg* abi_args = nargs > 0 ? (JitAbiArg*)mem_calloc(
        (size_t)nargs, sizeof(JitAbiArg), MEM_CAT_TEMP) : NULL;
    if (nargs > 0 && (!args || !abi_args)) {
        log_error("mir-direct-call: metadata allocation failed for %s", call_name);
        abort();
    }
    MIR_type_t* physical_types = nargs > 0 ? (MIR_type_t*)mem_alloc(
        (size_t)nargs * sizeof(MIR_type_t), MEM_CAT_TEMP) : NULL;
    MIR_op_t* physical_ops = nargs > 0 ? (MIR_op_t*)mem_alloc(
        (size_t)nargs * sizeof(MIR_op_t), MEM_CAT_TEMP) : NULL;
    if (nargs > 0 && (!physical_types || !physical_ops)) {
        log_error("mir-direct-call: operand allocation failed for %s", call_name);
        abort();
    }
    for (int i = 0; i < source_nargs; i++) {
        physical_types[i] = arg_types[i];
        physical_ops[i] = arg_ops[i];
    }
    int scalar_home_arg_index = accepts_scalar_home ? source_nargs : -1;
    int scalar_home_id = 0;
    if (accepts_scalar_home) {
        uint8_t observed = options ? options->observed_return_lane_mask :
            (FN_RETURN_HOME_NORMAL | FN_RETURN_HOME_ERROR);
        MirFrameRef ref = {MIR_FRAME_REF_NONE, 0};
        if (options && options->is_tail_call) {
            if (!em->frame.incoming_scalar_home ||
                    (options->scalar_return_home.kind != MIR_FRAME_REF_NONE) ||
                    (observed & variant->result.scalar_home_lane_mask) !=
                        variant->result.scalar_home_lane_mask) {
                log_error("mir-direct-call: invalid tail-home contract for %s",
                    call_name);
                abort();
            }
            ref = {MIR_FRAME_REF_INCOMING_CALLER_HOME, 0};
        } else if (!(observed & variant->result.scalar_home_lane_mask)) {
            ref = em_discard_scalar_home_ref();
        } else if (options && options->scalar_return_home.kind !=
                MIR_FRAME_REF_NONE) {
            ref = options->scalar_return_home;
        } else {
            scalar_home_id = em_scalar_home_new(em);
            ref = em_scalar_home_ref(em, scalar_home_id);
        }
        MIR_reg_t home = em_materialize_frame_ref(em, ref);
        if (!home) {
            log_error("mir-direct-call: unresolved scalar home for %s", call_name);
            abort();
        }
        physical_types[source_nargs] = MIR_T_P;
        physical_ops[source_nargs] = MIR_new_reg_op(em->ctx, home);
    }
    for (int i = 0; i < nargs; i++) {
        args[i] = {physical_types[i], "a", 0};
        bool home = i == scalar_home_arg_index;
        ValueRep param_rep = variant && variant->params &&
            i < variant->param_count
            ? variant->params[i].canonical_rep : VALUE_REP_NONE;
        JitValueClass value_class = param_rep != VALUE_REP_NONE
            ? em_value_class_for_rep(param_rep)
            : physical_types[i] == MIR_T_D || physical_types[i] == MIR_T_F
                ? JIT_VALUE_NON_GC_SCALAR
                : physical_types[i] == MIR_T_P
                    ? JIT_VALUE_RAW_GC_POINTER
                    : JIT_VALUE_BOXED_ITEM;
        abi_args[i].value.abi_rep = home ? JIT_ABI_POINTER :
            em_abi_rep(physical_types[i], value_class, true);
        abi_args[i].value.value_class = home ? JIT_VALUE_RAW_NON_GC_POINTER :
            value_class;
        abi_args[i].effects = home ? JIT_ARG_BORROWED
            : JIT_ARG_EFFECT_UNKNOWN;
    }
    JitCallMetadata metadata = {};
    FnEffectSummary effects = variant ? variant->effects
        : FnEffectSummary{true, true, true, false, false, true};
    metadata.effects = {
        effects.may_gc || effects.has_unknown_call
            ? JIT_EFFECT_MAY_GC : JIT_EFFECT_NO_GC,
        effects.may_reenter || effects.has_unknown_call
            ? JIT_REENTRY_YES : JIT_REENTRY_NO,
        effects.may_set_exception || effects.has_unknown_call
            ? JIT_EXCEPTION_MAY_SET : JIT_EXCEPTION_PRESERVES,
        JIT_NUMBER_STACK_MAY_ALLOCATE};
    FnReturnLaneAnalysis normal = variant ? variant->result.normal
        : FnReturnLaneAnalysis{LMD_TYPE_ANY, VALUE_REP_ITEM,
            SCALAR_RETURN_DYNAMIC, true};
    MIR_type_t result_type = em_mir_type_for_rep(normal.abi_rep);
    metadata.normal_result.value = {
        em_abi_rep(result_type, em_value_class_for_rep(normal.abi_rep), true),
        em_value_class_for_rep(normal.abi_rep)};
    metadata.normal_result.transport = JIT_RETURN_MIR_RESULT;
    metadata.normal_result.scalar_class = normal.scalar_class;
    metadata.normal_result.may_use_scalar_return_home =
        normal.may_need_caller_scalar_home;
    if (variant && variant->result.error_lane == FN_ERROR_LANE_CONTEXT_ITEM) {
        metadata.error_result.value = {JIT_ABI_ITEM, JIT_VALUE_BOXED_ITEM};
        metadata.error_result.transport = JIT_RETURN_CONTEXT_ERROR;
        metadata.error_result.scalar_class =
            variant->result.error.scalar_class;
        metadata.error_result.may_use_scalar_return_home =
            variant->result.error.may_need_caller_scalar_home;
    }
    metadata.abi_args = abi_args;
    metadata.abi_arg_count = (uint16_t)nargs;
    metadata.source_arg_count = (uint16_t)source_nargs;
    metadata.scalar_return_home_arg_index = (int16_t)scalar_home_arg_index;
    metadata.scalar_home_lane_mask = variant
        ? variant->result.scalar_home_lane_mask : 0;
    em_enforce_argument_ownership(em, &metadata, nargs, physical_ops);
    char proto_name[192];
    snprintf(proto_name, sizeof(proto_name), "%s_dp%d", call_name,
        em->label_counter++);
    MIR_item_t proto = MIR_new_proto_arr(em->ctx, proto_name, 1,
        &result_type, nargs, args);
    em_before_resolved_call(em, call_name, &metadata, nargs,
        physical_types, physical_ops);
    MIR_reg_t result = em_new_reg(em, "direct_result", result_type);
    MIR_insn_t call = mir_new_call_with_args(em->ctx, proto, target,
        result, nargs, physical_ops);
    mir_append_emit_insn(em->ctx, em->func_item, call);
    em_after_resolved_call(em, call_name, &metadata, call, result, result_type);
    call_result.normal = em_value(result, result_type, normal.semantic_type,
        normal.abi_rep, metadata.normal_result.value.value_class);
    call_result.normal.scalar_home_id = scalar_home_id;
    call_result.normal.scalar_provenance = scalar_home_id
        ? SCALAR_PROVENANCE_ACTIVATION_HOME : SCALAR_PROVENANCE_NONE;
    call_result.error.scalar_home_id = scalar_home_id;
    call_result.effects = metadata.effects;
    if (scalar_home_id &&
            (metadata.scalar_home_lane_mask & FN_RETURN_HOME_NORMAL)) {
        em_scalar_home_bind(em, scalar_home_id, result);
    }
    if (args) mem_free(args);
    if (abi_args) mem_free(abi_args);
    if (physical_types) mem_free(physical_types);
    if (physical_ops) mem_free(physical_ops);
    return call_result;
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

static inline MIR_reg_t em_call_6(MirEmitter* em, const char* fn_name,
        MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
        MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
        MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5,
        MIR_type_t a6t, MIR_op_t a6, bool include_signature) {
    MIR_type_t types[6] = {a1t, a2t, a3t, a4t, a5t, a6t};
    MIR_op_t ops[6] = {a1, a2, a3, a4, a5, a6};
    return em_call_with_args(em, fn_name, ret_type, 6, types, ops,
        include_signature);
}

static inline MIR_reg_t em_call_8(MirEmitter* em, const char* fn_name,
        MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
        MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
        MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5,
        MIR_type_t a6t, MIR_op_t a6, MIR_type_t a7t, MIR_op_t a7,
        MIR_type_t a8t, MIR_op_t a8, bool include_signature) {
    MIR_type_t types[8] = {a1t, a2t, a3t, a4t, a5t, a6t, a7t, a8t};
    MIR_op_t ops[8] = {a1, a2, a3, a4, a5, a6, a7, a8};
    return em_call_with_args(em, fn_name, ret_type, 8, types, ops,
        include_signature);
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

static inline void em_call_void_5(MirEmitter* em, const char* fn_name,
        MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
        MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4,
        MIR_type_t a5t, MIR_op_t a5, bool include_signature) {
    MIR_type_t types[5] = {a1t, a2t, a3t, a4t, a5t};
    MIR_op_t ops[5] = {a1, a2, a3, a4, a5};
    em_call_void_with_args(em, fn_name, 5, types, ops, include_signature);
}
