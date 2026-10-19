#include "js_execution_internal.hpp"
#include "../../lib/mem.h"

JsCompileRecoveryState* js_compile_recovery_state_ensure(void) {
    if (!js_active_runtime_state) return NULL;
    if (!js_runtime_state.compile_recovery_state) {
        js_runtime_state.compile_recovery_state = (JsCompileRecoveryState*)mem_calloc(1,
            sizeof(JsCompileRecoveryState), MEM_CAT_JS_RUNTIME);
    }
    return js_runtime_state.compile_recovery_state;
}

JsCompileRecoveryState* js_compile_recovery_state_current(void) {
    return js_active_runtime_state ? js_runtime_state.compile_recovery_state : NULL;
}

static JsCompileRecoveryState* js_compile_recovery_state_required(void) {
    JsCompileRecoveryState* state = js_compile_recovery_state_ensure();
    if (!state) {
        log_error("js-mir-recovery: compilation without a bound JS realm");
    }
    return state;
}

static int js_active_compile_owner_count(const JsCompileRecoveryState* state) {
    return state && state->stack ? state->stack->length : 0;
}

static ActiveJsTranspileOwner* js_active_compile_owner_at(
        const JsCompileRecoveryState* state, int index) {
    return state && state->stack && index >= 0 && index < state->stack->length
        ? (ActiveJsTranspileOwner*)arraylist_get(state->stack, index) : NULL;
}

static ActiveJsTranspileOwner* js_active_compile_owner_push(
        JsCompileRecoveryState* state) {
    if (!state) return NULL;
    if (!state->stack) state->stack = arraylist_new(4);
    ActiveJsTranspileOwner* owner = (ActiveJsTranspileOwner*)mem_calloc(1,
        sizeof(ActiveJsTranspileOwner), MEM_CAT_JS_RUNTIME);
    if (!owner || !state->stack || !arraylist_append(state->stack, owner)) {
        mem_free(owner);
        return NULL;
    }
    return owner;
}

static void js_active_compile_owner_pop(JsCompileRecoveryState* state) {
    int count = js_active_compile_owner_count(state);
    if (count <= 0) return;
    ActiveJsTranspileOwner* owner = js_active_compile_owner_at(state, count - 1);
    arraylist_remove(state->stack, count - 1);
    mem_free(owner);
}

static void js_sync_active_compile_top(JsCompileRecoveryState* state) {
    if (!state) return;
    int count = js_active_compile_owner_count(state);
    if (count <= 0) {
        g_active_js_transpiler = NULL;
        g_active_js_owned_source = NULL;
        return;
    }
    ActiveJsTranspileOwner* top = js_active_compile_owner_at(state, count - 1);
    if (!top) return;
    g_active_js_transpiler = top->tp;
    g_active_js_owned_source = top->owned_source;
}

static void js_pop_empty_active_compile_owners(JsCompileRecoveryState* state) {
    if (!state) return;
    while (js_active_compile_owner_count(state) > 0) {
        int count = js_active_compile_owner_count(state);
        ActiveJsTranspileOwner* top = js_active_compile_owner_at(state, count - 1);
        if (!top) break;
        if (top->tp || top->owned_source) break;
        js_active_compile_owner_pop(state);
    }
    js_sync_active_compile_top(state);
}

void js_track_active_compile(JsTranspiler* tp, char* owned_source) {
    if (!tp && !owned_source) return;
    JsCompileRecoveryState* state = js_compile_recovery_state_required();
    if (!state) return;
    ActiveJsTranspileOwner* top = NULL;
    if (js_active_compile_owner_count(state) <= 0) {
        top = js_active_compile_owner_push(state);
    } else {
        top = js_active_compile_owner_at(state,
            js_active_compile_owner_count(state) - 1);
    }
    if (!top) {
        log_error("js-mir-recovery: could not grow active transpile owner stack");
        return;
    }
    bool starts_nested_owner =
        (tp && top->tp && top->tp != tp) ||
        (owned_source && top->tp && top->owned_source != owned_source);
    if (starts_nested_owner) {
        top = js_active_compile_owner_push(state);
        if (!top) {
            log_error("js-mir-recovery: could not grow active transpile owner stack");
            return;
        }
    }
    if (tp) top->tp = tp;
    if (owned_source) top->owned_source = owned_source;
    js_sync_active_compile_top(state);
}

void js_clear_active_compile(JsTranspiler* tp, char* owned_source) {
    JsCompileRecoveryState* state = js_compile_recovery_state_required();
    if (!state) return;
    for (int i = js_active_compile_owner_count(state) - 1; i >= 0; i--) {
        ActiveJsTranspileOwner* owner = js_active_compile_owner_at(state, i);
        if (!owner) continue;
        bool matched = false;
        if (tp && owner->tp == tp) {
            owner->tp = NULL;
            matched = true;
        }
        if (owned_source && owner->owned_source == owned_source) {
            owner->owned_source = NULL;
            matched = true;
        }
        if (matched) break;
    }
    js_pop_empty_active_compile_owners(state);
}

static void js_cleanup_active_compile_state(JsCompileRecoveryState* state) {
    if (!state) return;
    while (js_active_compile_owner_count(state) > 0) {
        int count = js_active_compile_owner_count(state);
        ActiveJsTranspileOwner* owner = js_active_compile_owner_at(state, count - 1);
        if (!owner) break;
        JsTranspiler* tp = owner->tp;
        char* owned_source = owner->owned_source;
        owner->tp = NULL;
        owner->owned_source = NULL;
        js_active_compile_owner_pop(state);
        if (tp) js_transpiler_destroy(tp);
        if (owned_source) mem_free(owned_source);
    }
}

void js_cleanup_active_compile(void) {
    js_cleanup_active_compile_state(js_compile_recovery_state_current());
}

void js_abandon_active_compile_after_signal(void) {
    // parser/source owners are independent of the formal compiler's retained units.
    js_cleanup_active_compile_state(js_compile_recovery_state_current());
}

void js_compile_recovery_state_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->compile_recovery_state) return;
    JsCompileRecoveryState* state = runtime_state->compile_recovery_state;
    // release interrupted parser owners before their realm's allocation pools.
    js_cleanup_active_compile_state(state);
    if (state->stack) {
        arraylist_free(state->stack);
        state->stack = NULL;
    }
    mem_free(state);
    runtime_state->compile_recovery_state = NULL;
}
