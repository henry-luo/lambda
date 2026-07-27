#include "../lambda-data.hpp"
#include "runtime-state.h"
#include "heap_api.h"
#include "../../lib/memtrack.h"
#include "../../lib/log.h"

// Keep the active evaluator in the runtime layer so runner orchestration and
// focused runtime fixtures share one provider without linking each other.
__thread EvalContext* context = nullptr;

EvalContext* eval_context_bind(EvalContext* next) {
    EvalContext* previous = context;
    context = next;
    return previous;
}

void eval_context_restore(EvalContext* previous) {
    context = previous;
}

static uint32_t module_state_capacity_for(uint32_t module_id) {
    uint32_t capacity = 8;
    while (capacity <= module_id) capacity *= 2;
    return capacity;
}

extern "C" bool lambda_module_state_prepare(Context* runtime, uint32_t module_id,
        uint32_t var_count, uint32_t member_ic_count) {
    EvalContext* owner = (EvalContext*)runtime;
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
            if (!heap_register_gc_root_range_for(runtime, (uint64_t*)state->vars,
                    (int)state->var_count)) return false;
            state->vars_registered = true;
        }
        return true;
    }

    state = (LambdaModuleState*)mem_calloc(1, sizeof(LambdaModuleState), MEM_CAT_EVAL);
    if (!state) return false;
    state->var_count = var_count;
    state->member_ic_count = member_ic_count;
    if (var_count) {
        state->vars = (Item*)mem_calloc(var_count, sizeof(Item), MEM_CAT_EVAL);
        state->var_payloads = (uint64_t*)mem_calloc(var_count, sizeof(uint64_t), MEM_CAT_EVAL);
        if (!state->vars || !state->var_payloads || !heap_register_gc_root_range_for(runtime,
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
            if (state->vars) heap_unregister_gc_root_range_for(runtime,
                (uint64_t*)state->vars);
            mem_free(state->vars);
            mem_free(state);
            return false;
        }
    }
    owner->module_states[module_id] = state;
    return true;
}

extern "C" bool lambda_module_state_bind_static(Context* runtime,
        uint32_t module_id, void* consts, void* type_list) {
    EvalContext* owner = (EvalContext*)runtime;
    if (!owner || module_id >= owner->module_state_capacity) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    if (!state) return false;
    state->consts = consts;
    state->type_list = type_list;
    return true;
}

extern "C" void* lambda_module_const_at(Context* runtime,
        const LambdaModuleLayout* layout, uint32_t index) {
    EvalContext* owner = (EvalContext*)runtime;
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

extern "C" void lambda_module_state_reset(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (!owner || !owner->module_states) return;
    for (uint32_t i = 0; i < owner->module_state_capacity; i++) {
        LambdaModuleState* state = owner->module_states[i];
        if (!state) continue;
        if (state->vars && state->vars_registered) {
            heap_unregister_gc_root_range_for(runtime, (uint64_t*)state->vars);
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

extern "C" void lambda_module_state_destroy(Context* runtime) {
    EvalContext* owner = (EvalContext*)runtime;
    if (!owner || !owner->module_states) return;
    for (uint32_t i = 0; i < owner->module_state_capacity; i++) {
        LambdaModuleState* state = owner->module_states[i];
        if (!state) continue;
        if (state->vars && state->vars_registered) {
            heap_unregister_gc_root_range_for(runtime, (uint64_t*)state->vars);
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
