#include "../lambda-data.hpp"
#include "runtime-state.h"
#include "transpiler.hpp"
#include "heap_api.h"
#include "side_stack.h"
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
    // The side-stack regions are thread-local while EvalContext is reusable;
    // rebinding here prevents a context handoff from pairing stale pointers
    // with the new thread's uninitialized native root region.
    if (!lambda_side_stack_bind_for((Context*)owner)) {
        context = nullptr;
        log_error("eval-thread-init: failed to bind side stack for owner=%p",
                  (void*)owner);
        return false;
    }
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

static bool lambda_property_key_spec_decode(const PropertyKeySpec* specs,
        uint32_t count, uint32_t bytes_size, uint32_t index,
        NameId* out_predefined_id, StrView* out_name) {
    if (!specs || index >= count ||
            bytes_size < count * sizeof(PropertyKeySpec)) return false;
    const PropertyKeySpec* spec = &specs[index];
    if (spec->reserved != 0) return false;
    if (out_predefined_id) *out_predefined_id = NAME_ID_NONE;
    if (out_name) *out_name = {NULL, 0};
    if (spec->predefined_id != NAME_ID_NONE) {
        // Sealed images may transport only generated IDs. Context-local static
        // and dynamic IDs are linked from spelling bytes in this context.
        NameRef predefined = well_known_name_ref(spec->predefined_id);
        if (!predefined || property_key_kind(predefined) == NAME_KEY_PRIVATE ||
                spec->name_offset != 0 || spec->name_length != 0) return false;
        if (out_predefined_id) *out_predefined_id = spec->predefined_id;
        return true;
    }
    uint32_t header_size = count * (uint32_t)sizeof(PropertyKeySpec);
    if (spec->name_offset < header_size || spec->name_offset > bytes_size ||
            spec->name_length >= bytes_size - spec->name_offset) return false;
    const uint8_t* bytes = (const uint8_t*)specs;
    if (bytes[spec->name_offset + spec->name_length] != '\0') return false;
    if (out_name) {
        *out_name = {(const char*)bytes + spec->name_offset, spec->name_length};
    }
    return true;
}

extern "C" bool lambda_property_key_specs_prelink(const PropertyKeySpec* specs,
        uint32_t count, uint32_t bytes_size) {
    if (count == 0) return true;
    if (!context || !context->name_pool) {
        log_error("module-key-prelink: missing runtime name pool");
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        NameId predefined_id = NAME_ID_NONE;
        StrView name = {NULL, 0};
        if (!lambda_property_key_spec_decode(specs, count, bytes_size, index,
                &predefined_id, &name)) {
            log_error("module-key-prelink: invalid property key spec %u", index);
            return false;
        }
        if (predefined_id == NAME_ID_NONE &&
                !name_pool_create_strview(context->name_pool, name)) {
            log_error("module-key-prelink: name allocation failed for spec %u", index);
            return false;
        }
    }
    return true;
}

static bool lambda_module_state_resolve_property_keys(const PropertyKeySpec* specs,
        uint32_t count, uint32_t bytes_size, NameId* out_ids) {
    if (count == 0) return true;
    if (!specs || bytes_size < count * sizeof(PropertyKeySpec) ||
            !context || !context->name_pool || !out_ids) {
        log_error("module-key-link: missing key specs or runtime name pool");
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        NameId key_id = NAME_ID_NONE;
        StrView name = {NULL, 0};
        if (!lambda_property_key_spec_decode(specs, count, bytes_size, index,
                &key_id, &name)) {
            log_error("module-key-link: invalid property key spec %u", index);
            return false;
        }
        if (key_id == NAME_ID_NONE) {
            String* key = name_pool_create_strview(context->name_pool, name);
            key_id = name_ref_id(key);
        }
        if (key_id == NAME_ID_NONE) {
            log_error("module-key-link: name allocation failed for property key %u", index);
            return false;
        }
        out_ids[index] = key_id;
    }
    return true;
}

static bool lambda_module_state_link_property_keys_for_state(LambdaModuleState* state,
        const PropertyKeySpec* specs, uint32_t count, uint32_t bytes_size) {
    if (!state || state->property_key_count != count) return false;
    if (count == 0) return true;
    if (!state->property_keys) {
        state->property_keys = (NameId*)mem_calloc(count,
            sizeof(NameId), MEM_CAT_EVAL);
        if (!state->property_keys) return false;
    }
    return lambda_module_state_resolve_property_keys(specs, count, bytes_size,
        state->property_keys);
}

extern "C" bool lambda_module_state_prepare(uint32_t module_id,
        uint32_t var_count) {
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
        if (state->var_count != var_count) {
            log_error("module-state: sealed layout changed for module %u", module_id);
            return false;
        }
        if (state->vars && !state->vars_registered) {
            if (!heap_try_register_gc_root_range((uint64_t*)state->vars,
                    (int)state->var_capacity)) return false;
            state->vars_registered = true;
        }
        return true;
    }

    state = (LambdaModuleState*)mem_calloc(1, sizeof(LambdaModuleState), MEM_CAT_EVAL);
    if (!state) return false;
    state->module_id = module_id;
    state->var_count = var_count;
    state->var_capacity = var_count;
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
    owner->module_states[module_id] = state;
    return true;
}

extern "C" bool lambda_module_state_grow_vars(uint32_t module_id,
        uint32_t var_count) {
    EvalContext* owner = context;
    if (!owner) return false;
    if (module_id >= owner->module_state_capacity || !owner->module_states ||
            !owner->module_states[module_id]) {
        return lambda_module_state_prepare(module_id, var_count);
    }
    LambdaModuleState* state = owner->module_states[module_id];
    if (var_count <= state->var_count) return true;

    if (var_count <= state->var_capacity) {
        // Reusing the already registered tail keeps ordinary REPL binding
        // growth O(fragment); newly exposed slots must start unrooted/null.
        memset(state->vars + state->var_count, 0,
            (var_count - state->var_count) * sizeof(Item));
        memset(state->var_payloads + state->var_count, 0,
            (var_count - state->var_count) * sizeof(uint64_t));
        state->var_count = var_count;
        return true;
    }

    uint32_t capacity = state->var_capacity ? state->var_capacity : 8;
    while (capacity < var_count && capacity <= UINT32_MAX / 2) capacity *= 2;
    if (capacity < var_count) capacity = var_count;

    Item* vars = (Item*)mem_calloc(capacity, sizeof(Item), MEM_CAT_EVAL);
    uint64_t* payloads = (uint64_t*)mem_calloc(capacity, sizeof(uint64_t),
        MEM_CAT_EVAL);
    if (!vars || !payloads) {
        mem_free(vars);
        mem_free(payloads);
        return false;
    }
    if (state->var_count) {
        memcpy(vars, state->vars, state->var_count * sizeof(Item));
        memcpy(payloads, state->var_payloads, state->var_count * sizeof(uint64_t));
    }
    if (!heap_try_register_gc_root_range((uint64_t*)vars, (int)capacity)) {
        mem_free(vars);
        mem_free(payloads);
        return false;
    }

    // Publish the replacement root before retiring the old range: a REPL
    // append can allocate while extending its binding slab, so no existing
    // closure or module value may become temporarily unrooted (D5.3.3).
    if (state->vars_registered) heap_unregister_gc_root_range((uint64_t*)state->vars);
    mem_free(state->vars);
    mem_free(state->var_payloads);
    state->vars = vars;
    state->var_payloads = payloads;
    state->var_count = var_count;
    state->var_capacity = capacity;
    state->vars_registered = true;
    return true;
}

extern "C" bool lambda_module_state_snapshot(uint32_t module_id,
        LambdaModuleStateSnapshot* snapshot) {
    if (!snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity ||
            !owner->module_states || !owner->module_states[module_id]) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    snapshot->count = state->var_count;
    if (snapshot->count == 0) return true;
    snapshot->vars = (Item*)mem_calloc(snapshot->count, sizeof(Item), MEM_CAT_EVAL);
    snapshot->payloads = (uint64_t*)mem_calloc(snapshot->count, sizeof(uint64_t),
        MEM_CAT_EVAL);
    if (!snapshot->vars || !snapshot->payloads) {
        lambda_module_state_snapshot_dispose(snapshot);
        return false;
    }
    memcpy(snapshot->vars, state->vars, snapshot->count * sizeof(Item));
    memcpy(snapshot->payloads, state->var_payloads,
        snapshot->count * sizeof(uint64_t));
    if (!heap_try_register_gc_root_range((uint64_t*)snapshot->vars,
            (int)snapshot->count)) {
        lambda_module_state_snapshot_dispose(snapshot);
        return false;
    }
    snapshot->roots_registered = true;
    return true;
}

extern "C" bool lambda_module_state_restore(uint32_t module_id,
        const LambdaModuleStateSnapshot* snapshot) {
    EvalContext* owner = context;
    if (!snapshot || !owner || module_id >= owner->module_state_capacity ||
            !owner->module_states || !owner->module_states[module_id]) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    if (snapshot->count > state->var_count) return false;
    if (snapshot->count) {
        memcpy(state->vars, snapshot->vars, snapshot->count * sizeof(Item));
        memcpy(state->var_payloads, snapshot->payloads,
            snapshot->count * sizeof(uint64_t));
    }
    if (snapshot->count < state->var_count) {
        memset(state->vars + snapshot->count, 0,
            (state->var_count - snapshot->count) * sizeof(Item));
        memset(state->var_payloads + snapshot->count, 0,
            (state->var_count - snapshot->count) * sizeof(uint64_t));
    }
    return true;
}

extern "C" void lambda_module_state_snapshot_dispose(
        LambdaModuleStateSnapshot* snapshot) {
    if (!snapshot) return;
    if (snapshot->roots_registered && snapshot->vars) {
        heap_unregister_gc_root_range((uint64_t*)snapshot->vars);
    }
    mem_free(snapshot->vars);
    mem_free(snapshot->payloads);
    memset(snapshot, 0, sizeof(*snapshot));
}

extern "C" void lambda_module_state_release(uint32_t module_id) {
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity ||
            !owner->module_states) return;
    LambdaModuleState* state = owner->module_states[module_id];
    if (!state) return;
    // A cleared REPL module must drop its exact root before its Script/AST is
    // destroyed; otherwise stale bindings remain live for the next session.
    if (state->vars && state->vars_registered) {
        heap_unregister_gc_root_range((uint64_t*)state->vars);
    }
    if (owner->active_js_module_state == state) {
        owner->active_js_module_state = NULL;
    }
    mem_free(state->vars);
    mem_free(state->var_payloads);
    mem_free(state->property_keys);
    mem_free(state);
    owner->module_states[module_id] = NULL;
}

extern "C" bool lambda_module_state_prepare_layout(const LambdaModuleLayout* layout) {
    if (!layout || !lambda_module_state_prepare(layout->module_id,
            layout->var_count)) return false;
    EvalContext* owner = context;
    LambdaModuleState* state = owner->module_states[layout->module_id];
    if (!state || state->property_key_count == 0) {
        if (state) state->property_key_count = layout->property_key_count;
    }
    if (!state || state->property_key_count != layout->property_key_count) {
        log_error("module-key-link: sealed layout changed for module %u", layout->module_id);
        return false;
    }
    return lambda_module_state_link_property_keys_for_state(state, layout->property_key_specs,
        layout->property_key_count, layout->property_key_bytes_size);
}

extern "C" bool lambda_module_state_link_property_keys(uint32_t module_id,
        const PropertyKeySpec* specs, uint32_t count, uint32_t bytes_size) {
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity ||
            !owner->module_states || !owner->module_states[module_id]) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    if (state->property_key_count != 0 && state->property_key_count != count) {
        log_error("module-key-link: sealed count changed for module %u", module_id);
        return false;
    }
    state->property_key_count = count;
    return lambda_module_state_link_property_keys_for_state(state, specs,
        count, bytes_size);
}

extern "C" bool lambda_module_state_append_property_keys(uint32_t module_id,
        const PropertyKeySpec* specs, uint32_t count, uint32_t bytes_size) {
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity ||
            !owner->module_states || !owner->module_states[module_id]) return false;
    LambdaModuleState* state = owner->module_states[module_id];
    if (count == 0) return true;
    if (state->property_key_count > UINT32_MAX - count) return false;

    NameId* appended = (NameId*)mem_calloc(count, sizeof(NameId), MEM_CAT_EVAL);
    if (!appended || !lambda_module_state_resolve_property_keys(specs, count,
            bytes_size, appended)) {
        mem_free(appended);
        return false;
    }
    uint32_t old_count = state->property_key_count;
    uint32_t total = old_count + count;
    NameId* keys = (NameId*)mem_realloc(state->property_keys,
        (size_t)total * sizeof(NameId), MEM_CAT_EVAL);
    if (!keys) {
        mem_free(appended);
        return false;
    }
    memcpy(keys + old_count, appended, (size_t)count * sizeof(NameId));
    state->property_keys = keys;
    state->property_key_count = total;
    mem_free(appended);
    return true;
}

extern "C" bool lambda_module_state_reserve(uint32_t var_count,
        uint32_t* out_module_id) {
    EvalContext* owner = context;
    if (!owner || !out_module_id) return false;
    Runtime* runtime_owner = owner->runtime;
    if (!runtime_owner) {
        log_error("module-state: cannot reserve module id without owning runtime");
        return false;
    }
    uint32_t module_id = runtime_owner->next_module_state_id++;
    if (!lambda_module_state_prepare(module_id, var_count)) return false;
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
    state->var_capacity = required_var_count;
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

extern "C" uint32_t lambda_module_state_property_key_count(uint32_t module_id) {
    EvalContext* owner = context;
    if (!owner || module_id >= owner->module_state_capacity ||
            !owner->module_states || !owner->module_states[module_id]) return 0;
    return owner->module_states[module_id]->property_key_count;
}

extern "C" Item lambda_name_id_to_item(NameId name_id) {
    if (name_id == NAME_ID_NONE) return ItemNull;
    NameRef name = name_pool_resolve_id(context ? context->name_pool : NULL,
        name_id);
    return name ? (Item){.item = s2it(name)} : ItemNull;
}

extern "C" uint64_t lambda_module_name_id_at(void* module_state,
        uint32_t index) {
    LambdaModuleState* state = (LambdaModuleState*)module_state;
    if (!state || !state->property_keys || index >= state->property_key_count) {
        return (uint64_t)NAME_ID_NONE;
    }
    return (uint64_t)state->property_keys[index];
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

extern "C" void* lambda_module_const_at_state(void* module_state,
        uint32_t index) {
    LambdaModuleState* state = (LambdaModuleState*)module_state;
    if (!state || !state->consts) return NULL;
    // Satellites carry the owner's slab state directly; resolving constants
    // through it avoids stale per-image layout metadata during batch linking
    // (D5.2, D8.2).
    return ((void**)state->consts)[index];
}

extern "C" Item lambda_module_var_at(void* module_state, uint32_t slot) {
    LambdaModuleState* state = (LambdaModuleState*)module_state;
    if (!state || !state->vars || slot >= state->var_count) return ItemNull;
    // Keep satellite reads on the same checked slab accessor as T0. A direct
    // MIR load can be overwritten by root write-back when its value is already
    // owned by the module root range (D5.2).
    return state->vars[slot];
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
            memset(state->vars, 0, state->var_capacity * sizeof(Item));
            memset(state->var_payloads, 0, state->var_capacity * sizeof(uint64_t));
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
        mem_free(state->property_keys);
        mem_free(state);
    }
    mem_free(owner->module_states);
    owner->module_states = NULL;
    owner->module_state_capacity = 0;
}
