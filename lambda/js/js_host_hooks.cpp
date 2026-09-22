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

void js_host_hooks_set_console_format_hook(JsHostConsoleFormatHook hook) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->console_format_hook = hook;
}

Item js_host_hooks_format_console(Item args) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (!hooks || !hooks->console_format_hook) return ItemNull;
    return hooks->console_format_hook(args);
}

void js_host_hooks_set_redirect_stdout_to_stderr(bool enabled) {
    JsHostHooksState* hooks = js_host_hooks_current();
    if (hooks) hooks->redirect_stdout_to_stderr = enabled;
}

bool js_host_hooks_redirect_stdout_to_stderr(void) {
    JsHostHooksState* hooks = js_host_hooks_current();
    return hooks && hooks->redirect_stdout_to_stderr;
}
