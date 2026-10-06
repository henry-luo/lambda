#include "radiant_event_hook.h"
#include "../../lib/log.h"

static LambdaRadiantEmitFn g_emit_fn = nullptr;
static __thread LambdaEmitScope* g_scoped_emit = nullptr;

void lambda_radiant_event_register(LambdaRadiantEmitFn emit_fn) {
    g_emit_fn = emit_fn;
}

void lambda_emit_scope_enter(LambdaEmitScope* scope, LambdaScopedEmitFn receiver_fn,
                             void* receiver) {
    if (!scope) return;
    scope->receiver_fn = receiver_fn;
    scope->receiver = receiver;
    scope->previous = g_scoped_emit;
    g_scoped_emit = scope;
}

void lambda_emit_scope_leave(LambdaEmitScope* scope) {
    if (!scope || g_scoped_emit != scope) {
        log_error("template-emit: scope exit order mismatch");
        return;
    }
    g_scoped_emit = scope->previous;
    scope->previous = nullptr;
}

Item lambda_radiant_emit(Item event_name, Item event_data) {
    if (g_scoped_emit && g_scoped_emit->receiver_fn) {
        return g_scoped_emit->receiver_fn(g_scoped_emit->receiver,
            event_name, event_data);
    }
    return g_emit_fn ? g_emit_fn(event_name, event_data) : ItemNull;
}
