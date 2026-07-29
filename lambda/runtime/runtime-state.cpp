#include "../lambda-data.hpp"
#include "runtime-state.h"
#include "transpiler.hpp"
#include "heap_api.h"
#include "../../lib/memtrack.h"
#include "../../lib/log.h"

// Keep the active evaluator in the runtime layer so runner orchestration and
// focused runtime fixtures share one provider without linking each other.
__thread EvalContext* context = nullptr;

bool eval_context_thread_initialize(EvalContext* owner) {
    if (!owner) {
        log_error("eval-thread-init: missing EvalContext owner");
        return false;
    }
    if (context && context != owner) {
        // A live evaluator thread cannot borrow another isolate at a nested
        // boundary; that work must be routed to the other owner thread.
        log_error("eval-thread-init: refusing context switch current=%p owner=%p",
                  (void*)context, (void*)owner);
        return false;
    }
    context = owner;
    return true;
}

bool eval_context_thread_matches(const EvalContext* owner) {
    return owner && context == owner;
}

bool eval_context_thread_shutdown(EvalContext* owner) {
    if (!owner || context != owner) {
        log_error("eval-thread-shutdown: owner mismatch current=%p owner=%p",
                  (void*)context, (void*)owner);
        return false;
    }
    context = NULL;
    return true;
}

extern "C" Context* eval_context_tls_runtime(void) {
    return (Context*)context;
}

static uint32_t module_state_capacity_for(uint32_t module_id) {
    uint32_t capacity = 8;
    while (capacity <= module_id) capacity *= 2;
    return capacity;
}

extern "C" bool lambda_module_state_prepare(uint32_t module_id,
        uint32_t var_count, uint32_t member_ic_count) {
    EvalContext* owner = context;
    if (!owner) return false;

    if (module_id >= owner->module_state_capacity) {
        uint32_t capacity = module_state_capacity_for(module_id);
        LambdaModuleState** states = (LambdaModuleState**)mem_calloc(capacity,
            sizeof(LambdaModuleState*), MEM_CAT_EVAL);
        if (!states) return false;
        if (owner->module_states && owner->module_state_capacity) {
            memcpy(states, owner->module_states,
                owner->module_state_capacity * sizeof(LambdaModuleState*));
            mem_free(owner->module_states);
        }
        owner->module_states = states;
        owner->module_state_capacity = capacity;
    }

    LambdaModuleState* state = owner->module_states[module_id];
    if (state) {
        if (state->var_count != var_count ||
                state->member_ic_count != member_ic_count) {
            log_error("module-state: sealed layout changed for module %u", module_id);
            return false;
        }
        if (state->vars && !state->vars_registered) {
            if (!heap_try_register_gc_root_range((uint64_t*)state->vars,
                    (int)state->var_count)) return false;
            state->vars_registered = true;
        }
        return true;
    }

    state = (LambdaModuleState*)mem_calloc(1, sizeof(LambdaModuleState), MEM_CAT_EVAL);
    if (!state) return false;
    state->module_id = module_id;
    state->var_count = var_count;
    state->member_ic_count = member_ic_count;
    if (var_count) {
        state->vars = (Item*)mem_calloc(var_count, sizeof(Item), MEM_CAT_EVAL);
        state->var_payloads = (uint64_t*)mem_calloc(var_count, sizeof(uint64_t), MEM_CAT_EVAL);
        if (!state->vars || !state->var_payloads || !heap_try_register_gc_root_range(
                (uint64_t*)state->vars, (int)var_count)) {
            mem_free(state->vars);
            mem_free(state->var_payloads);
            mem_free(state);
            return false;
        }
        state->vars_registered = true;
    }
    if (member_ic_count) {
        // LambdaMemberIC is two pointer-sized words. Keep this storage
        // context-owned and fixed; generated code casts the slab to its ABI
        // type and performs no cache publication or synchronization.
        state->member_ics = mem_calloc(member_ic_count, 2 * sizeof(uint64_t), MEM_CAT_EVAL);
        if (!state->member_ics) {
            if (state->vars) heap_unregister_gc_root_range((uint64_t*)state->vars);
            mem_free(state->vars);
            mem_free(state);
            return false;
        }
    }
    owner->module_states[module_id] = state;
    return true;
}

extern "C" bool lambda_module_state_reserve(uint32_t var_count,
        uint32_t member_ic_count, uint32_t* out_module_id) {
    EvalContext* owner = context;
    if (!owner || !out_module_id) return false;
    Runtime* runtime_owner = owner->runtime;
    if (!runtime_owner) {
        log_error("module-state: cannot reserve module id without owning runtime");
        return false;
    }
    uint32_t module_id = runtime_owner->next_module_state_id++;
    if (!lambda_module_state_prepare(module_id, var_count, member_ic_count)) return false;
    *out_module_id = module_id;
    return true;
}

extern "C" bool lambda_active_js_module_state_ensure_vars(
        uint32_t required_var_count) {
    EvalContext* owner = context;
    LambdaModuleState* state = owner ? owner->active_js_module_state : NULL;
    if (!state || required_var_count <= state->var_count) return state != NULL;

    Item* vars = (Item*)mem_calloc(required_var_count, sizeof(Item), MEM_CAT_EVAL);
    uint64_t* payloads = (uint64_t*)mem_calloc(required_var_count,
        sizeof(uint64_t), MEM_CAT_EVAL);
    if (!vars || !payloads) {
        mem_free(vars);
        mem_free(payloads);
        return false;
    }
    memcpy(vars, state->vars, state->var_count * sizeof(Item));
    memcpy(payloads, state->var_payloads, state->var_count * sizeof(uint64_t));
    if (!heap_try_register_gc_root_range((uint64_t*)vars,
            (int)required_var_count)) {
        mem_free(vars);
        mem_free(payloads);
        return false;
    }

    // Direct eval shares its caller's bindings but can introduce new slots.
    // Retire the old exact root only after the replacement root is published.
    if (state->vars_registered) {
        heap_unregister_gc_root_range((uint64_t*)state->vars);
    }
    mem_free(state->vars);
    mem_free(state->var_payloads);
    state->vars = vars;
    state->var_payloads = payloads;
    state->var_count = required_var_count;
    state->vars_registered = true;
    return true;
}

extern "C" bool lambda_module_state_bind_static(uint32_t module_id,
        void* consts, void* type_list) {
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    if (!state) return false;
    state->consts = consts;
    state->type_list = type_list;
    return true;
}

extern "C" void* lambda_module_const_at(const LambdaModuleLayout* layout,
        uint32_t index) {
    EvalContext* owner = context;
    if (!owner || !layout || layout->module_id >= owner->module_state_capacity) return NULL;
    LambdaModuleState* state = owner->module_states[layout->module_id];
    if (!state || !state->consts) return NULL;
    // A generated literal may follow an arbitrary call; resolve through the
    // owning context here so MIR cannot retain a call-clobbered pool register.
    return ((void**)state->consts)[index];
}

extern "C" void lambda_module_var_store(void* module_state, uint32_t slot,
        Item item) {
    LambdaModuleState* state = (LambdaModuleState*)module_state;
    if (!state || !state->vars || !state->var_payloads || slot >= state->var_count) return;
    state->vars[slot] = item;
    uint64_t* payload = &state->var_payloads[slot];
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64:
        *(int64_t*)payload = item.get_int64();
        state->vars[slot] = {.item = l2it(payload)};
        break;
    case LMD_TYPE_UINT64:
        *payload = item.get_uint64();
        state->vars[slot] = {.item = u2it(payload)};
        break;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        if (!(item.item & ITEM_DBL_MASK) && item.item != ITEM_FLOAT_P0 &&
                item.item != ITEM_FLOAT_N0) {
            *(double*)payload = item.get_double();
            state->vars[slot] = lambda_float_ptr_to_item((double*)payload);
        }
        break;
    default:
        break;
    }
}

extern "C" void lambda_module_state_reset(void) {
    EvalContext* owner = context;
    if (!owner || !owner->module_states) return;
    for (uint32_t i = 0; i < owner->module_state_capacity; i++) {
        LambdaModuleState* state = owner->module_states[i];
        if (!state) continue;
        if (state->vars && state->vars_registered) {
            heap_unregister_gc_root_range((uint64_t*)state->vars);
            state->vars_registered = false;
        }
        if (state->vars) {
            memset(state->vars, 0, state->var_count * sizeof(Item));
            memset(state->var_payloads, 0, state->var_count * sizeof(uint64_t));
        }
        if (state->member_ics) {
            memset(state->member_ics, 0,
                state->member_ic_count * 2 * sizeof(uint64_t));
        }
    }
}

extern "C" void lambda_module_state_destroy(void) {
    EvalContext* owner = context;
    if (!owner || !owner->module_states) return;
    owner->active_js_module_state = NULL;
    for (uint32_t i = 0; i < owner->module_state_capacity; i++) {
        LambdaModuleState* state = owner->module_states[i];
        if (!state) continue;
        if (state->vars && state->vars_registered) {
            heap_unregister_gc_root_range((uint64_t*)state->vars);
        }
        mem_free(state->vars);
        mem_free(state->var_payloads);
        mem_free(state->member_ics);
        mem_free(state);
    }
    mem_free(owner->module_states);
    owner->module_states = NULL;
    owner->module_state_capacity = 0;
}
