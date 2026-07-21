#include "radiant_event_hook.h"

static LambdaRadiantEmitFn g_emit_fn = nullptr;
static LambdaRadiantSelectionFn g_selection_fn = nullptr;

void lambda_radiant_event_register(LambdaRadiantEmitFn emit_fn,
                                   LambdaRadiantSelectionFn selection_fn) {
    g_emit_fn = emit_fn;
    g_selection_fn = selection_fn;
}

Item lambda_radiant_emit(Item event_name, Item event_data) {
    return g_emit_fn ? g_emit_fn(event_name, event_data) : ItemNull;
}

Item lambda_radiant_set_selection(Item selection) {
    return g_selection_fn ? g_selection_fn(selection) : ItemNull;
}
