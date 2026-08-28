#include "event.hpp"

#include "../lib/log.h"

#include <string.h>

static DocState* editing_dispatch_doc_state(EventContext* evcon) {
    if (!evcon) {
        return nullptr;
    }
    DomDocument* doc = evcon->target_document
        ? evcon->target_document
        : (evcon->ui_context ? evcon->ui_context->document : nullptr);
    return doc ? (DocState*)doc->state : nullptr;
}

static uint32_t editing_log_cstr_len(const char* text) {
    return text ? (uint32_t)strlen(text) : 0;
}

static bool editing_log_redact(const EditingSurface* surface) {
    return surface && surface->mode == EDIT_MODE_PASSWORD_TEXT;
}

static void editing_log_write_surface(JsonWriter* w,
                                      const EditingSurface* surface) {
    jw_key(w, "surface");
    jw_obj_begin(w);
        editing_log_write_surface_core_fields(w, surface, true);
    jw_obj_end(w);
}

static void editing_log_write_intent(JsonWriter* w,
                                     const EditingSurface* surface,
                                     const EditingIntent* intent) {
    bool redacted = editing_log_redact(surface);
    jw_key(w, "intent");
    jw_obj_begin(w);
        jw_kv_str(w, "input_type",
                  intent ? input_intent_type_name(intent->type) : "");
        jw_kv_bool(w, "dispatchable",
                   intent ? input_intent_is_dispatchable(intent->type) : false);
        jw_kv_bool(w, "is_composing", intent ? intent->is_composing : false);
        jw_kv_uint(w, "composition_caret",
                   redacted || !intent ? 0 : intent->composition_caret);
        jw_kv_uint(w, "data_len",
                   redacted || !intent ? 0 : editing_log_cstr_len(intent->data));
        jw_kv_uint(w, "html_data_len",
                   redacted || !intent ? 0 : editing_log_cstr_len(intent->html_data));
        jw_kv_bool(w, "redacted", redacted);
    jw_obj_end(w);
}

static void editing_log_record(EventContext* evcon,
                               const EditingSurface* surface,
                               const EditingIntent* intent,
                               const char* record_type,
                               bool prevented) {
    DocState* state = editing_dispatch_doc_state(evcon);
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    char buf[4096];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        record_type ? record_type : "editing.event", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        editing_log_write_surface(&w, surface);
        editing_log_write_intent(&w, surface, intent);
        jw_kv_bool(&w, "prevented", prevented);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

void editing_dispatch_log_intent(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent) {
    editing_log_record(evcon, surface, intent, "editing.intent", false);
}

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
    editing_dispatch_log_intent(evcon, surface, intent);
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
    editing_log_record(evcon, surface, intent, "editing.beforeinput",
                       js_prevented);
    // Report both halves: logging only the JS flag hid a template that had
    // claimed the intent, which reads as "nobody handled it".
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
        editing_log_record(evcon, surface, intent, "editing.input",
                           false);
    }
    // UA behavior templates observe the *committed* value here, after the
    // buffer mutation — unlike the pre-mutation `input` app templates receive.
    radiant_dispatch_behavior_input(evcon, surface->view);
}
