#include "js_host_hooks.h"

static JsHostShutdownParticipant js_host_shutdown_participant = 0;
static JsHostIpcAcceptHook js_host_ipc_accept_hook = 0;
static JsHostClusterOnlineHook js_host_cluster_online_hook = 0;
static JsHostConsoleFormatHook js_host_console_format_hook = 0;

void js_host_hooks_set_shutdown_participant(JsHostShutdownParticipant participant) {
    js_host_shutdown_participant = participant;
}

void js_host_hooks_run_shutdown_participants(void) {
    if (js_host_shutdown_participant) js_host_shutdown_participant();
}

void js_host_hooks_set_ipc_accept_hook(JsHostIpcAcceptHook hook) {
    js_host_ipc_accept_hook = hook;
}

Item js_host_hooks_accept_ipc_handle(void* pipe) {
    if (!js_host_ipc_accept_hook) return ItemNull;
    return js_host_ipc_accept_hook(pipe);
}

void js_host_hooks_set_cluster_online_hook(JsHostClusterOnlineHook hook) {
    js_host_cluster_online_hook = hook;
}

void js_host_hooks_emit_cluster_online(Item child) {
    if (js_host_cluster_online_hook) js_host_cluster_online_hook(child);
}

void js_host_hooks_set_console_format_hook(JsHostConsoleFormatHook hook) {
    js_host_console_format_hook = hook;
}

Item js_host_hooks_format_console(Item args) {
    if (!js_host_console_format_hook) return ItemNull;
    return js_host_console_format_hook(args);
}
