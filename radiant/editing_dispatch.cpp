#include "event.hpp"

#include "../lib/log.h"

extern "C" bool radiant_dispatch_behavior_beforeinput(EventContext* evcon,
                                                      View* target,
                                                      const InputIntent* intent);

bool editing_dispatch_form_beforeinput(EventContext* evcon,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       const EditingFormNotificationHooks* hooks,
                                       bool* out_prevented,
                                       bool* out_applied) {
    if (out_prevented) *out_prevented = false;
    if (out_applied) *out_applied = false;
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
    // F5: a UA behavior template is the applier. It runs after the JS listener,
    // as ES5 requires, and only when JS has not already cancelled — a cancelled
    // beforeinput means the edit does not happen at all, not that someone else
    // should apply it.
    bool lambda_prevented = false;
    if (dispatchable && !js_prevented) {
        lambda_prevented = radiant_dispatch_behavior_beforeinput(
            evcon, surface->view, intent);
    }
    if (out_prevented) *out_prevented = js_prevented || lambda_prevented;
    if (out_applied) *out_applied = lambda_prevented;
    log_debug("editing_dispatch_form_beforeinput: surface=%s inputType=%s "
              "js_prevented=%d applied_by_template=%d",
              editing_mode_name(surface->mode),
              input_intent_type_name(intent->type),
              js_prevented ? 1 : 0, lambda_prevented ? 1 : 0);
    return dispatchable;
}

extern "C" bool radiant_dispatch_behavior_input(EventContext* evcon, View* target);

void editing_dispatch_form_input(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 const EditingFormNotificationHooks* hooks) {
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
    // UA behavior templates observe the *committed* value here, after the
    // buffer mutation — unlike the pre-mutation `input` app templates receive.
    radiant_dispatch_behavior_input(evcon, surface->view);
}
