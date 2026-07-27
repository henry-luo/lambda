#include "js_host_hooks.h"
#include "js_runtime_state.hpp"

static JsHostHooksState* js_host_hooks_current(void) {
    return js_active_runtime_state ? &js_runtime_state.host_hooks : NULL;
}

void js_host_hooks_set_shutdown_participant(JsHostShutdownParticipant participant) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->shutdown_participant = participant;
}

void js_host_hooks_run_shutdown_participants(void) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks && hooks->shutdown_participant) hooks->shutdown_participant();
}

void js_host_hooks_set_ipc_accept_hook(JsHostIpcAcceptHook hook) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->ipc_accept_hook = hook;
}

Item js_host_hooks_accept_ipc_handle(void* pipe) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (!hooks || !hooks->ipc_accept_hook) return ItemNull;
    return hooks->ipc_accept_hook(pipe);
}

void js_host_hooks_set_cluster_online_hook(JsHostClusterOnlineHook hook) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->cluster_online_hook = hook;
}

void js_host_hooks_emit_cluster_online(Item child) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks && hooks->cluster_online_hook) hooks->cluster_online_hook(child);
}

void js_host_hooks_set_console_format_hook(JsHostConsoleFormatHook hook) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->console_format_hook = hook;
}

Item js_host_hooks_format_console(Item args) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (!hooks || !hooks->console_format_hook) return ItemNull;
    return hooks->console_format_hook(args);
}
