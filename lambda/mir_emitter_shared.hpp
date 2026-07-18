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
    JitImportMetadata metadata;
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
enum MirGcEventKind {
    MIR_GC_EVENT_DEFINITION,
    MIR_GC_EVENT_USE,
    MIR_GC_EVENT_TEMP_DEFINITION,
    MIR_GC_EVENT_TEMP_USE,
    MIR_GC_EVENT_SAFEPOINT,
};

struct MirGcEvent {
    MirGcEventKind kind;
    int sequence;
    int home_id;
    MIR_reg_t reg;
    JitValueClass value_class;
    JitGcEffect gc_effect;
    char label[64];
};

struct MirRootCandidate {
    MIR_reg_t reg;
    JitValueClass value_class;
    int home_id;
};

struct MirGcCallSite {
    MIR_insn_t insn;
    JitGcEffect effect;
};

struct MirRootWriteBackResult {
    int stable_slots;
    int scratch_slots;
    int inserted_stores;
    int instruction_count;
    int block_count;
};

struct MirGcAnalysis {
    MirGcEvent* events;
    int event_count;
    int event_capacity;
    int next_home_id;
    int detail;
};

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
    MirGcAnalysis gc_analysis;    // opt-in semantic GC event stream for S0/S1
};

static inline bool em_root_write_back_enabled(void) {
    const char* mode = getenv("LAMBDA_MIR_ROOT_MODE");
    if (!mode || !mode[0] || strcmp(mode, "write-back") == 0) return true;
    // Write-through is the differential oracle, not a production fallback:
    // publishing after every instruction causes pathological MIR growth and
    // can exceed the backend's practical register/branch limits.
    return strcmp(mode, "write-through") != 0 && strcmp(mode, "eager") != 0;
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
        JitGcEffect effect) {
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

static inline void em_gc_analysis_begin(MirEmitter* em) {
    if (!em) return;
    em->gc_analysis.event_count = 0;
    em->gc_analysis.next_home_id = 0;
    const char* enabled = getenv("LAMBDA_MIR_LOG_GC_EVENTS");
    em->gc_analysis.detail = enabled && enabled[0] && enabled[0] != '0'
        ? (enabled[0] == '2' ? 2 : 1) : 0;
}

static inline int em_gc_new_home(MirEmitter* em) {
    if (!em) return -1;
    // Home zero is reserved as the memset-compatible "unassigned" value.
    return ++em->gc_analysis.next_home_id;
}

static inline void em_gc_note_event(MirEmitter* em, MirGcEventKind kind,
        int home_id, MIR_reg_t reg, JitValueClass value_class,
        JitGcEffect gc_effect, const char* label) {
    if (!em || em->gc_analysis.detail == 0) return;
    if (em->gc_analysis.event_count >= em->gc_analysis.event_capacity) {
        int next_capacity = em->gc_analysis.event_capacity
            ? em->gc_analysis.event_capacity * 2 : 128;
        MirGcEvent* events = (MirGcEvent*)mem_realloc(
            em->gc_analysis.events,
            (size_t)next_capacity * sizeof(MirGcEvent), MEM_CAT_TEMP);
        if (!events) {
            log_error("mir-gc-events: disabling diagnostics after allocation failure");
            em->gc_analysis.detail = 0;
            return;
        }
        em->gc_analysis.events = events;
        em->gc_analysis.event_capacity = next_capacity;
    }
    MirGcEvent* event = &em->gc_analysis.events[em->gc_analysis.event_count];
    memset(event, 0, sizeof(*event));
    event->kind = kind;
    event->sequence = em->gc_analysis.event_count;
    event->home_id = home_id;
    event->reg = reg;
    event->value_class = value_class;
    event->gc_effect = gc_effect;
    snprintf(event->label, sizeof(event->label), "%s", label ? label : "");
    em->gc_analysis.event_count++;
}

static inline void em_gc_note_definition(MirEmitter* em, int home_id,
        MIR_reg_t reg, JitValueClass value_class, const char* label) {
    em_gc_note_event(em, MIR_GC_EVENT_DEFINITION, home_id, reg, value_class,
        JIT_EFFECT_MAY_GC, label);
}

static inline void em_gc_note_use(MirEmitter* em, int home_id,
        MIR_reg_t reg, JitValueClass value_class, const char* label) {
    em_gc_note_event(em, MIR_GC_EVENT_USE, home_id, reg, value_class,
        JIT_EFFECT_MAY_GC, label);
}

static inline void em_gc_note_temp_definition(MirEmitter* em,
        MIR_reg_t reg, JitValueClass value_class, const char* label) {
    em_gc_note_event(em, MIR_GC_EVENT_TEMP_DEFINITION, -1, reg, value_class,
        JIT_EFFECT_MAY_GC, label);
}

static inline void em_gc_note_temp_use(MirEmitter* em,
        MIR_reg_t reg, JitValueClass value_class, const char* label) {
    em_gc_note_event(em, MIR_GC_EVENT_TEMP_USE, -1, reg, value_class,
        JIT_EFFECT_MAY_GC, label);
}

static inline void em_gc_note_safepoint(MirEmitter* em,
        JitGcEffect gc_effect, const char* label) {
    em_gc_note_event(em, MIR_GC_EVENT_SAFEPOINT, -1, 0,
        JIT_VALUE_UNKNOWN, gc_effect, label);
}

static inline void em_gc_analysis_dump(MirEmitter* em,
        const char* function_name) {
    if (!em || em->gc_analysis.detail == 0) return;
    int definitions = 0;
    int uses = 0;
    int temp_definitions = 0;
    int temp_uses = 0;
    int may_gc = 0;
    int no_gc = 0;
    for (int i = 0; i < em->gc_analysis.event_count; i++) {
        MirGcEvent* event = &em->gc_analysis.events[i];
        if (event->kind == MIR_GC_EVENT_DEFINITION) definitions++;
        else if (event->kind == MIR_GC_EVENT_USE) uses++;
        else if (event->kind == MIR_GC_EVENT_TEMP_DEFINITION) temp_definitions++;
        else if (event->kind == MIR_GC_EVENT_TEMP_USE) temp_uses++;
        else if (event->gc_effect == JIT_EFFECT_NO_GC) no_gc++;
        else may_gc++;
    }
    log_info("mir-gc-events: function=%s homes=%d events=%d definitions=%d uses=%d temp_definitions=%d temp_uses=%d may_gc=%d no_gc=%d",
        function_name ? function_name : "<anonymous>",
        em->gc_analysis.next_home_id, em->gc_analysis.event_count,
        definitions, uses, temp_definitions, temp_uses, may_gc, no_gc);
    // Release logging is compiled out; these counters still drive the debug
    // audit without making optimized builds fail their unused-value policy.
    (void)definitions;
    (void)uses;
    (void)temp_definitions;
    (void)temp_uses;
    (void)may_gc;
    (void)no_gc;
    if (em->gc_analysis.detail < 2) return;
    for (int i = 0; i < em->gc_analysis.event_count; i++) {
        MirGcEvent* event = &em->gc_analysis.events[i];
        const char* kind = event->kind == MIR_GC_EVENT_DEFINITION ? "definition" :
            (event->kind == MIR_GC_EVENT_USE ? "use" :
            (event->kind == MIR_GC_EVENT_TEMP_DEFINITION ? "temp-definition" :
            (event->kind == MIR_GC_EVENT_TEMP_USE ? "temp-use" : "safepoint")));
        log_info("mir-gc-event: function=%s sequence=%d kind=%s home=%d reg=%u class=%d effect=%d label=%s",
            function_name ? function_name : "<anonymous>", event->sequence,
            kind, event->home_id, (unsigned)event->reg,
            (int)event->value_class, (int)event->gc_effect, event->label);
        (void)kind;
    }
}

static inline void em_gc_analysis_dispose(MirEmitter* em) {
    if (!em) return;
    if (em->gc_analysis.events) {
        mem_free(em->gc_analysis.events);
        em->gc_analysis.events = NULL;
    }
    em->gc_analysis.event_count = 0;
    em->gc_analysis.event_capacity = 0;
    em->gc_analysis.next_home_id = 0;
    em->gc_analysis.detail = 0;
}

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

static inline void em_store_frame_slot_typed(MirEmitter* em,
        MIR_reg_t frame_base, int slot, MIR_reg_t value, MIR_type_t type) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, type,
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
    if (!em || !em->func || !em->func->vars || !reg) return false;
    MIR_var_t* vars = VARR_ADDR(MIR_var_t, em->func->vars);
    for (uint32_t i = 0; i < em->func->nargs; i++) {
        if (vars[i].name && MIR_reg(em->ctx, vars[i].name, em->func) == reg) {
            return true;
        }
    }
    return false;
}

static inline void em_root_insert_store_after(MirEmitter* em,
        MIR_insn_t position, MIR_reg_t frame_base, MIR_reg_t reg, int slot) {
    if (!em || !em->func || !em->func_item || !position || !frame_base ||
            !reg || slot < 0) return;
    MIR_type_t type = MIR_reg_type(em->ctx, reg, em->func);
    if (type != MIR_T_P) type = MIR_T_I64;
    MIR_insn_t store = MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, type,
            (MIR_disp_t)slot * (MIR_disp_t)sizeof(uint64_t),
            frame_base, 0, 1),
        MIR_new_reg_op(em->ctx, reg));
    MIR_insn_t next = DLIST_NEXT(MIR_insn_t, position);
    if (next) {
        MIR_insert_insn_before(em->ctx, em->func_item, next, store);
    } else {
        MIR_append_insn(em->ctx, em->func_item, store);
    }
}

static inline void em_root_restore_write_through_after_analysis_failure(
        MirEmitter* em, MIR_insn_t anchor, MIR_reg_t frame_base,
        const MirRootCandidate* candidates, int candidate_count,
        const int* candidate_by_reg, int candidate_by_reg_capacity) {
    if (!em || !em->func || !anchor || !frame_base || !candidates ||
            candidate_count <= 0) return;
    // An analysis OOM must degrade to the correct oracle, never to unrooted
    // generated code. One physical slot per candidate avoids more allocation.
    for (int ci = 0; ci < candidate_count; ci++) {
        MIR_reg_t reg = candidates[ci].reg;
        if (em_root_is_function_argument_reg(em, reg)) {
            em_root_insert_store_after(em, anchor, frame_base, reg, ci);
        }
    }
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns); insn;) {
        MIR_insn_t next = DLIST_NEXT(MIR_insn_t, insn);
        size_t operand_count = MIR_insn_nops(em->ctx, insn);
        for (size_t oi = 0; oi < operand_count; oi++) {
            int output = 0;
            MIR_insn_op_mode(em->ctx, insn, oi, &output);
            if (!output || insn->ops[oi].mode != MIR_OP_REG) continue;
            MIR_reg_t reg = insn->ops[oi].u.reg;
            if (!candidate_by_reg ||
                    reg >= (MIR_reg_t)candidate_by_reg_capacity) continue;
            int ci = candidate_by_reg[reg];
            if (ci >= 0 && ci < candidate_count) {
                em_root_insert_store_after(em, insn, frame_base, reg, ci);
            }
        }
        insn = next;
    }
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

static inline bool em_root_semantic_analysis_fallback(MirEmitter* em,
        MIR_insn_t anchor, MIR_reg_t frame_base,
        const MirRootCandidate* candidates, int candidate_count,
        const int* candidate_by_reg, int candidate_by_reg_capacity,
        bool oracle_stores_present, int oracle_slot_count,
        MirRootWriteBackResult* result) {
    if (!oracle_stores_present) {
        em_root_restore_write_through_after_analysis_failure(em, anchor,
            frame_base, candidates, candidate_count, candidate_by_reg,
            candidate_by_reg_capacity);
    }
    if (result) {
        result->stable_slots = oracle_stores_present
            ? oracle_slot_count : candidate_count;
        result->scratch_slots = 0;
        result->inserted_stores = oracle_stores_present
            ? oracle_slot_count : candidate_count;
    }
    return false;
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
        log_error("mir-semantic-root-write-back: liveness allocation failed; restoring write-through roots");
        em_root_semantic_analysis_fallback(em, anchor, frame_base,
            root_candidates, candidate_count, *candidate_by_reg,
            *candidate_by_reg_capacity, oracle_stores_present,
            oracle_slot_count, result);
        if (instructions) mem_free(instructions);
        if (blocks) mem_free(blocks);
        if (instruction_blocks) mem_free(instruction_blocks);
        if (labels) mem_free(labels);
        if (label_blocks) mem_free(label_blocks);
        if (reg_to_candidate) mem_free(reg_to_candidate);
        if (insn_uses) mem_free(insn_uses);
        if (insn_definitions) mem_free(insn_definitions);
        return false;
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
        log_error("mir-semantic-root-write-back: CFG allocation failed; restoring write-through roots");
        em_root_semantic_analysis_fallback(em, anchor, frame_base,
            root_candidates, candidate_count, *candidate_by_reg,
            *candidate_by_reg_capacity, oracle_stores_present,
            oracle_slot_count, result);
        mem_free(instructions); mem_free(blocks); mem_free(instruction_blocks);
        mem_free(labels); mem_free(label_blocks); mem_free(reg_to_candidate);
        mem_free(insn_uses); mem_free(insn_definitions);
        if (block_uses) mem_free(block_uses);
        if (block_definitions) mem_free(block_definitions);
        if (live_in) mem_free(live_in);
        if (live_out) mem_free(live_out);
        if (scratch_out) mem_free(scratch_out);
        if (scratch_in) mem_free(scratch_in);
        return false;
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
        log_error("mir-semantic-root-write-back: publication allocation failed; restoring write-through roots");
        em_root_semantic_analysis_fallback(em, anchor, frame_base,
            root_candidates, candidate_count, *candidate_by_reg,
            *candidate_by_reg_capacity, oracle_stores_present,
            oracle_slot_count, result);
        mem_free(instructions); mem_free(blocks); mem_free(instruction_blocks);
        mem_free(labels); mem_free(label_blocks); mem_free(reg_to_candidate);
        mem_free(insn_uses); mem_free(insn_definitions);
        mem_free(block_uses); mem_free(block_definitions);
        mem_free(live_in); mem_free(live_out);
        mem_free(scratch_out); mem_free(scratch_in);
        if (instruction_live_in) mem_free(instruction_live_in);
        if (candidate_defined) mem_free(candidate_defined);
        if (home_to_slot) mem_free(home_to_slot);
        if (candidate_slots) mem_free(candidate_slots);
        if (set_candidates) mem_free(set_candidates);
        return false;
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
        log_error("mir-semantic-root-write-back: scratch-color allocation failed; restoring write-through roots");
        em_root_semantic_analysis_fallback(em, anchor, frame_base,
            root_candidates, candidate_count, *candidate_by_reg,
            *candidate_by_reg_capacity, oracle_stores_present,
            oracle_slot_count, result);
        mem_free(instructions); mem_free(blocks); mem_free(instruction_blocks);
        mem_free(labels); mem_free(label_blocks); mem_free(reg_to_candidate);
        mem_free(insn_uses); mem_free(insn_definitions);
        mem_free(block_uses); mem_free(block_definitions);
        mem_free(live_in); mem_free(live_out);
        mem_free(scratch_out); mem_free(scratch_in);
        mem_free(instruction_live_in); mem_free(candidate_defined);
        mem_free(home_to_slot); mem_free(candidate_slots); mem_free(set_candidates);
        if (interference) mem_free(interference);
        if (used_colors) mem_free(used_colors);
        if (candidate_obligated) mem_free(candidate_obligated);
        return false;
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
        log_error("mir-semantic-root-write-back: dirty-state allocation failed; restoring write-through roots");
        em_root_semantic_analysis_fallback(em, anchor, frame_base,
            root_candidates, candidate_count, *candidate_by_reg,
            *candidate_by_reg_capacity, oracle_stores_present,
            oracle_slot_count, result);
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
        if (dirty_in) mem_free(dirty_in);
        if (dirty_out) mem_free(dirty_out);
        if (dirty_state) mem_free(dirty_state);
        if (dirty_preserve) mem_free(dirty_preserve);
        if (dirty_generate) mem_free(dirty_generate);
        if (successors) mem_free(successors);
        return false;
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
                    if (em->gc_analysis.detail >= 2) {
                        log_info("mir-semantic-root-write-back-root: instruction=%d slot=%d reg=%u home=%d",
                            i, slot, (unsigned)reg,
                            root_candidates[ci].home_id);
                    }
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

static inline bool em_finalize_eager_root_write_back(MirEmitter* em,
        MIR_reg_t frame_base, MIR_insn_t anchor,
        const MirGcCallSite* call_sites, int call_site_count,
        MirRootWriteBackResult* result, const char* log_label,
        const MirRootCandidate* semantic_candidates = NULL,
        int semantic_candidate_count = 0) {
    if (!em || !em->func || !em->func_item || !frame_base || !anchor) {
        return false;
    }
    if (result) memset(result, 0, sizeof(*result));

    int eager_store_count = 0;
    int max_home = 0;
    MIR_reg_t max_reg = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) continue;
        eager_store_count++;
        MIR_reg_t reg = insn->ops[1].u.reg;
        int home = (int)(insn->ops[0].u.mem.disp /
            (MIR_disp_t)sizeof(uint64_t)) + 1;
        if (reg > max_reg) max_reg = reg;
        if (home > max_home) max_home = home;
    }
    for (int ci = 0; ci < semantic_candidate_count; ci++) {
        if (semantic_candidates[ci].reg > max_reg) {
            max_reg = semantic_candidates[ci].reg;
        }
        if (semantic_candidates[ci].home_id > max_home) {
            max_home = semantic_candidates[ci].home_id;
        }
    }
    if (eager_store_count == 0 && semantic_candidate_count == 0) return true;

    MIR_reg_t* home_first_reg = (MIR_reg_t*)mem_calloc(
        (size_t)max_home + 1, sizeof(MIR_reg_t), MEM_CAT_TEMP);
    uint8_t* preserved_homes = (uint8_t*)mem_calloc(
        (size_t)max_home + 1, sizeof(uint8_t), MEM_CAT_TEMP);
    int candidate_by_reg_capacity = (int)max_reg + 1;
    int* candidate_by_reg = (int*)mem_alloc(
        (size_t)candidate_by_reg_capacity * sizeof(int), MEM_CAT_TEMP);
    if (!home_first_reg || !preserved_homes || !candidate_by_reg) {
        log_error("mir-root-write-back: candidate allocation failed; preserving eager roots");
        if (home_first_reg) mem_free(home_first_reg);
        if (preserved_homes) mem_free(preserved_homes);
        if (candidate_by_reg) mem_free(candidate_by_reg);
        if (result) {
            result->stable_slots = max_home;
            result->inserted_stores = eager_store_count;
        }
        return false;
    }
    for (int reg = 0; reg < candidate_by_reg_capacity; reg++) {
        candidate_by_reg[reg] = -1;
    }

    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) continue;
        MIR_reg_t reg = insn->ops[1].u.reg;
        int home = (int)(insn->ops[0].u.mem.disp /
            (MIR_disp_t)sizeof(uint64_t)) + 1;
        if (home < 1 || home > max_home) {
            log_error("mir-root-write-back: invalid eager home %d; preserving eager roots",
                home);
            mem_free(home_first_reg);
            mem_free(preserved_homes);
            mem_free(candidate_by_reg);
            if (result) {
                result->stable_slots = max_home;
                result->inserted_stores = eager_store_count;
            }
            return false;
        }
        if (home_first_reg[home] && home_first_reg[home] != reg) {
            preserved_homes[home] = 1;
        }
        home_first_reg[home] = reg;
    }

    MirRootCandidate* candidates = NULL;
    int candidate_count = 0;
    int candidate_capacity = 0;
    int retained_store_count = 0;
    for (int ci = 0; ci < semantic_candidate_count; ci++) {
        const MirRootCandidate* candidate = &semantic_candidates[ci];
        if (!em_root_note_candidate(&candidates, &candidate_count,
                &candidate_capacity, &candidate_by_reg,
                &candidate_by_reg_capacity, candidate->reg,
                candidate->value_class, candidate->home_id)) {
            log_error("mir-root-write-back: semantic candidate merge failed");
            mem_free(home_first_reg);
            mem_free(preserved_homes);
            mem_free(candidate_by_reg);
            if (candidates) mem_free(candidates);
            abort();
        }
    }
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, em->func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (!em_is_eager_root_store(insn, frame_base)) continue;
        MIR_reg_t reg = insn->ops[1].u.reg;
        int home = (int)(insn->ops[0].u.mem.disp /
            (MIR_disp_t)sizeof(uint64_t)) + 1;
        if (preserved_homes[home]) {
            retained_store_count++;
            continue;
        }
        if (!em_root_note_candidate(&candidates, &candidate_count,
                &candidate_capacity, &candidate_by_reg,
                &candidate_by_reg_capacity, reg, JIT_VALUE_UNKNOWN, home)) {
            log_error("mir-root-write-back: candidate registration failed; preserving eager roots");
            mem_free(home_first_reg);
            mem_free(preserved_homes);
            mem_free(candidate_by_reg);
            if (candidates) mem_free(candidates);
            if (result) {
                result->stable_slots = max_home;
                result->inserted_stores = eager_store_count;
            }
            return false;
        }
    }
    mem_free(home_first_reg);
    if (candidate_count == 0) {
        mem_free(preserved_homes);
        mem_free(candidate_by_reg);
        if (result) {
            result->stable_slots = max_home;
            result->inserted_stores = eager_store_count;
        }
        return false;
    }

    const char* log_slots = getenv("LAMBDA_MIR_LOG_FRAME_SLOTS");
    if (retained_store_count > 0 && log_slots && log_slots[0] &&
            strcmp(log_slots, "0") != 0) {
        log_info("mir-root-write-back: function=%s retained_stores=%d for merged semantic homes",
            log_label ? log_label : "<anonymous>", retained_store_count);
    }

    bool optimized = em_finalize_semantic_root_write_back(em, frame_base,
        anchor, eager_store_count > 0, max_home, &candidates, &candidate_count,
        &candidate_capacity, &candidate_by_reg, &candidate_by_reg_capacity,
        call_sites, call_site_count, result, log_label, preserved_homes,
        max_home + 1, retained_store_count > 0, retained_store_count);
    if (candidates) mem_free(candidates);
    mem_free(preserved_homes);
    if (candidate_by_reg) mem_free(candidate_by_reg);
    return optimized;
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
