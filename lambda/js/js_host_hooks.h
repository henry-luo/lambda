#ifndef LAMBDA_JS_HOST_HOOKS_H
#define LAMBDA_JS_HOST_HOOKS_H

#include "../lambda-data.hpp"
#include "../jube/jube.h"

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
void js_host_hooks_set_redirect_stdout_to_stderr(bool enabled);
bool js_host_hooks_redirect_stdout_to_stderr(void);

// Shared JS runtime predicates and Unicode helpers live in js_runtime.cpp so
// global builtins and runtime dispatch use one implementation of each rule.
const JubeTypeDef* js_host_object_type(Item object);
bool js_host_object_get_property(Item object, Item key, Item* out);
bool js_host_object_set_property(Item object, Item key, Item value, Item* out);
bool js_host_object_has_property(Item object, Item key, Item* out);
bool js_host_object_delete_property(Item object, Item key, Item* out);
bool js_host_object_own_property_names(Item object, Item* out);
bool js_host_object_own_property_descriptor(Item object, Item key, Item* out);
bool js_host_object_prototype(Item object, Item* out);
bool js_is_arguments_exotic_array(Item value);
int64_t js_utf16_len(const char* chars, int str_len, bool is_ascii);

#endif
