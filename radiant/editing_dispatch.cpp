#include "editing_dispatch.hpp"

#include "editing_geometry.hpp"
#include "editing_target_range.hpp"
#include "event_state_log.hpp"
#include "handler.hpp"
#include "state_store.hpp"
#include "view.hpp"
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

static bool editing_intent_needs_target_range_validation(
        const EditingIntent* intent) {
    if (!intent || !input_intent_is_dispatchable(intent->type)) return false;
    switch (intent->type) {
        case INPUT_INTENT_COMPOSITION_START:
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
            return false;
        default:
            return true;
    }
}

static bool editing_dispatch_target_ranges_are_valid(DocState* state,
                                                     const EditingSurface* surface,
                                                     const EditingIntent* intent) {
    if (!editing_intent_needs_target_range_validation(intent)) return true;
    if (!state || !surface) return true;

    EditingTargetRange ranges[1];
    uint32_t range_count = editing_compute_target_ranges(state, surface, intent,
                                                         ranges, 1);
    for (uint32_t i = 0; i < range_count; i++) {
        if (!editing_geometry_surface_contains_target_range(surface, &ranges[i])) {
            return false;
        }
    }
    return true;
}

static void editing_log_write_surface(JsonWriter* w,
                                      const EditingSurface* surface) {
    jw_key(w, "surface");
    jw_obj_begin(w);
        jw_kv_str(w, "kind", editing_surface_kind_name(
            surface ? surface->kind : EDIT_SURFACE_NONE));
        jw_kv_str(w, "mode", editing_mode_name(
            surface ? surface->mode : EDIT_MODE_RICH));
        jw_kv_bool(w, "readonly", surface ? surface->readonly : false);
        jw_kv_bool(w, "disabled", surface ? surface->disabled : false);
        jw_kv_bool(w, "target_in_false_island",
                   surface ? surface->target_in_false_island : false);
        event_state_log_write_node_ref(w, "owner",
            surface ? (const DomNode*)surface->owner : nullptr);
        event_state_log_write_node_ref(w, "target",
            surface ? (const DomNode*)surface->view : nullptr);
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
                               bool prevented,
                               bool lambda_handled) {
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
        jw_kv_bool(&w, "lambda_handled", lambda_handled);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

void editing_dispatch_log_intent(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent) {
    editing_log_record(evcon, surface, intent, "editing.intent", false, false);
}

bool editing_dispatch_beforeinput(EventContext* evcon,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  const EditingDispatchHooks* hooks) {
    return editing_dispatch_beforeinput_ex(evcon, surface, intent, hooks,
                                           true, nullptr, nullptr);
}

bool editing_dispatch_beforeinput_ex(EventContext* evcon,
                                     const EditingSurface* surface,
                                     const EditingIntent* intent,
                                     const EditingDispatchHooks* hooks,
                                     bool dispatch_input_after,
                                     bool* out_prevented,
                                     bool* out_lambda_handled) {
    if (out_prevented) *out_prevented = false;
    if (out_lambda_handled) *out_lambda_handled = false;
    if (!evcon || !surface || !surface->view || !intent ||
        intent->type == INPUT_INTENT_NONE || !hooks) {
        return false;
    }

    // rich hosts use this full dispatcher because their model mutation is
    // consumer-owned. Form text controls use the sibling form dispatcher below,
    // which shares intent logging and beforeinput/input ordering while keeping
    // value-store mutation in text_edit.cpp.
    if (!editing_surface_is_rich(surface)) {
        return false;
    }

    // §9: format* and selectAll are never fired as `beforeinput
    // { inputType: "formatBold" }` etc. Keep the intent flowing to Lambda
    // template handlers, but do not dispatch a JS InputEvent and do not
    // signal handled to the lower input pipeline.
    bool dispatchable = input_intent_is_dispatchable(intent->type);
    editing_dispatch_log_intent(evcon, surface, intent);
    DocState* state = editing_dispatch_doc_state(evcon);

    if (!editing_dispatch_target_ranges_are_valid(state, surface, intent)) {
        editing_log_record(evcon, surface, intent, "editing.beforeinput",
                           true, false);
        if (out_prevented) *out_prevented = true;
        log_debug("editing_dispatch_beforeinput: rejected mixed-surface target range for %s",
                  input_intent_type_name(intent->type));
        return true;
    }

    if (intent->type == INPUT_INTENT_DELETE_BY_CUT) {
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
    editing_log_record(evcon, surface, intent, "editing.beforeinput",
                       js_prevented, lambda_handled);
    if (out_prevented) *out_prevented = js_prevented;
    if (out_lambda_handled) *out_lambda_handled = lambda_handled;
    if (!lambda_handled && !js_prevented) {
        log_debug("editing_dispatch_beforeinput: no beforeinput handler on %s surface",
                  editing_surface_kind_name(surface->kind));
    }

    // `input` is the post-mutation notification and is not cancelable.
    if (dispatch_input_after && !js_prevented) {
        editing_dispatch_input(evcon, surface, intent, hooks);
    }

    // Rich editing hosts own the default action. Callers that pass
    // dispatch_input_after=false perform the default mutation themselves and
    // then emit `input`; older rich paths keep the legacy event-only behavior.
    return true;
}

void editing_dispatch_input(EventContext* evcon,
                            const EditingSurface* surface,
                            const EditingIntent* intent,
                            const EditingDispatchHooks* hooks) {
    if (!evcon || !surface || !surface->view || !intent ||
        intent->type == INPUT_INTENT_NONE || !hooks) {
        return;
    }
    if (!input_intent_is_dispatchable(intent->type)) {
        return;
    }
    if (hooks->dispatch_input_event) {
        hooks->dispatch_input_event(evcon, surface->view, "input", intent,
                                    hooks->user);
        editing_log_record(evcon, surface, intent, "editing.input",
                           false, false);
    }
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
    editing_dispatch_log_intent(evcon, surface, intent);
    bool js_prevented = false;
    if (dispatchable && hooks->dispatch_input_event) {
        js_prevented = hooks->dispatch_input_event(evcon, surface->view,
                                                   "beforeinput", intent,
                                                   hooks->user);
    }
    if (out_prevented) *out_prevented = js_prevented;
    editing_log_record(evcon, surface, intent, "editing.beforeinput",
                       js_prevented, false);
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
        editing_log_record(evcon, surface, intent, "editing.input",
                           false, false);
    }
}
