#ifndef LAMBDA_JS_HOST_HOOKS_H
#define LAMBDA_JS_HOST_HOOKS_H

#include "../lambda-data.hpp"

// Host hooks invert optional Node-leaf back-calls. An absent hook is a
// deliberate no-op so minimal profiles remain independent of Node modules.
typedef void (*JsHostShutdownParticipant)(void);
typedef Item (*JsHostIpcAcceptHook)(void* pipe);
typedef void (*JsHostClusterOnlineHook)(Item child);
typedef Item (*JsHostConsoleFormatHook)(Item args);

void js_host_hooks_set_shutdown_participant(JsHostShutdownParticipant participant);
void js_host_hooks_run_shutdown_participants(void);
void js_host_hooks_set_ipc_accept_hook(JsHostIpcAcceptHook hook);
Item js_host_hooks_accept_ipc_handle(void* pipe);
void js_host_hooks_set_cluster_online_hook(JsHostClusterOnlineHook hook);
void js_host_hooks_emit_cluster_online(Item child);
void js_host_hooks_set_console_format_hook(JsHostConsoleFormatHook hook);
Item js_host_hooks_format_console(Item args);

#endif
