#include "editing_dispatch.hpp"

#include "handler.hpp"
#include "state_store.hpp"
#include "view.hpp"
#include "../lib/log.h"

static DocState* editing_dispatch_doc_state(EventContext* evcon) {
    if (!evcon || !evcon->ui_context || !evcon->ui_context->document) {
        return nullptr;
    }
    return (DocState*)evcon->ui_context->document->state;
}

bool editing_dispatch_beforeinput(EventContext* evcon,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  const EditingDispatchHooks* hooks) {
    if (!evcon || !surface || !surface->view || !intent ||
        intent->type == INPUT_INTENT_NONE || !hooks) {
        return false;
    }

    // the shared dispatcher is currently enabled for rich hosts. Form text
    // controls still use their existing value-mutation path until the form
    // value boundary store can produce stable InputEvent target ranges.
    if (!editing_surface_is_rich(surface)) {
        return false;
    }

    // §9: format* and selectAll are never fired as `beforeinput
    // { inputType: "formatBold" }` etc. Keep the intent flowing to Lambda
    // template handlers, but do not dispatch a JS InputEvent and do not
    // signal handled to the lower input pipeline.
    bool dispatchable = input_intent_is_dispatchable(intent->type);

    if (intent->type == INPUT_INTENT_DELETE_BY_CUT) {
        DocState* state = editing_dispatch_doc_state(evcon);
        if (!state || !selection_has(state)) return false;
        if (hooks->copy_selection) {
            hooks->copy_selection(state, "rich cut", hooks->user);
        }
    }

    // JS `beforeinput` runs before Lambda template handlers so JS
    // preventDefault() remains the cancellation surface for Input Events.
    bool js_prevented = false;
    if (dispatchable && hooks->dispatch_input_event) {
        js_prevented = hooks->dispatch_input_event(evcon, surface->view,
                                                   "beforeinput", intent,
                                                   hooks->user);
    }

    bool lambda_handled = false;
    if (hooks->dispatch_lambda_event) {
        lambda_handled = hooks->dispatch_lambda_event(evcon, surface->view,
                                                      "beforeinput", intent,
                                                      hooks->user);
    }
    if (!lambda_handled && !js_prevented) {
        log_debug("editing_dispatch_beforeinput: no beforeinput handler on %s surface",
                  editing_surface_kind_name(surface->kind));
    }

    // `input` is the post-mutation notification and is not cancelable.
    if (dispatchable && !js_prevented && hooks->dispatch_input_event) {
        hooks->dispatch_input_event(evcon, surface->view, "input", intent,
                                    hooks->user);
    }

    // Rich editing hosts own the default action. For now the actual mutation
    // remains consumer-driven, so handled means the keystroke belonged to the
    // editing host and should not fall through to unrelated platform behavior.
    return true;
}

bool editing_dispatch_form_beforeinput(EventContext* evcon,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       const EditingDispatchHooks* hooks,
                                       bool* out_prevented) {
    if (out_prevented) *out_prevented = false;
    if (!evcon || !surface || !surface->view || !intent ||
        intent->type == INPUT_INTENT_NONE || !hooks) {
        return false;
    }
    if (!editing_surface_is_text_control(surface)) {
        return false;
    }

    bool dispatchable = input_intent_is_dispatchable(intent->type);
    bool js_prevented = false;
    if (dispatchable && hooks->dispatch_input_event) {
        js_prevented = hooks->dispatch_input_event(evcon, surface->view,
                                                   "beforeinput", intent,
                                                   hooks->user);
    }
    if (out_prevented) *out_prevented = js_prevented;
    log_debug("editing_dispatch_form_beforeinput: surface=%s inputType=%s prevented=%d",
              editing_mode_name(surface->mode),
              input_intent_type_name(intent->type),
              js_prevented ? 1 : 0);
    return dispatchable;
}

void editing_dispatch_form_input(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 const EditingDispatchHooks* hooks) {
    if (!evcon || !surface || !surface->view || !intent ||
        intent->type == INPUT_INTENT_NONE || !hooks) {
        return;
    }
    if (!editing_surface_is_text_control(surface)) {
        return;
    }
    if (!input_intent_is_dispatchable(intent->type)) {
        return;
    }
    if (hooks->dispatch_input_event) {
        hooks->dispatch_input_event(evcon, surface->view, "input", intent,
                                    hooks->user);
    }
}
