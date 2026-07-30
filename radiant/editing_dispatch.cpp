#include "event.hpp"

#include "view.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
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

static uint64_t editing_dispatch_next_transaction_id(DocState* state) {
    if (!state) return 0;
    if (state->editing_transaction_next_id == 0) {
        state->editing_transaction_next_id = 1;
    }
    uint64_t id = state->editing_transaction_next_id++;
    if (state->editing_transaction_next_id == 0) {
        state->editing_transaction_next_id = 1;
    }
    return id;
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

static bool editing_dispatch_plaintext_filters_payload(
        const EditingIntent* intent) {
    if (!intent) return false;
    switch (intent->type) {
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_DROP:
            return true;
        default:
            return false;
    }
}

static bool editing_dispatch_plaintext_normalize_intent(
        const EditingSurface* surface,
        const EditingIntent* intent,
        EditingIntent* normalized) {
    if (!surface || !intent || !normalized ||
        surface->mode != EDIT_MODE_PLAINTEXT_ONLY) {
        return false;
    }

    bool changed = false;
    *normalized = *intent;
    normalized->owned_data = nullptr;
    normalized->owned_html_data = nullptr;

    if (editing_dispatch_plaintext_filters_payload(intent)) {
        normalized->html_data = nullptr;
        normalized->data_mime = "text/plain";
        changed = true;
    }
    if (intent->type == INPUT_INTENT_INSERT_PARAGRAPH) {
        normalized->type = INPUT_INTENT_INSERT_LINE_BREAK;
        changed = true;
    }
    return changed;
}

static bool editing_dispatch_boundary_in_false_island(
        const EditingSurface* surface,
        const DomBoundary* boundary) {
    if (!surface || !editing_surface_is_rich(surface) ||
        !surface->owner || !boundary || !boundary->node) {
        return false;
    }
    EditingHost host;
    if (!editing_host_lookup(boundary->node, &host)) return false;
    return host.host == surface->owner && host.target_in_false_island;
}

struct EditingTargetRangeStatus {
    bool required;
    bool valid;
    uint32_t count;
    EditingTargetRange ranges[4];
};

static EditingTargetRangeStatus editing_dispatch_target_range_status(
        DocState* state,
        const EditingSurface* surface,
        const EditingIntent* intent) {
    EditingTargetRangeStatus status;
    status.required = editing_intent_needs_target_range_validation(intent);
    status.valid = true;
    status.count = 0;
    if (!status.required || !state || !surface) return status;
    if (surface->target_in_false_island) {
        status.valid = false;
        return status;
    }

    status.count = editing_compute_target_ranges(state, surface, intent,
                                                 status.ranges, 4);
    for (uint32_t i = 0; i < status.count; i++) {
        if (!editing_geometry_surface_contains_target_range(surface, &status.ranges[i])) {
            status.valid = false;
            break;
        }
        if (editing_dispatch_boundary_in_false_island(surface, &status.ranges[i].start) ||
            editing_dispatch_boundary_in_false_island(surface, &status.ranges[i].end)) {
            status.valid = false;
            break;
        }
    }
    return status;
}

static EditingSelectionSnapshot editing_dispatch_selection_snapshot(
        DocState* state, const EditingSurface* surface) {
    EditingSelectionSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.kind = EDIT_SEL_NONE;
    snapshot.direction = DOM_SEL_DIR_NONE;
    snapshot.mutation_seq = state ? state->selection_mutation_seq : 0;

    if (!state) return snapshot;
    bool rich_surface = editing_surface_is_rich(surface);
    if (!rich_surface && state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        snapshot.kind = EDIT_SEL_TEXT_CONTROL;
        snapshot.direction = state->sel.direction;
        snapshot.mutation_seq = state->sel.mutation_seq;
        snapshot.control = state->sel.control;
        snapshot.start_u16 = state->sel.start_u16;
        snapshot.end_u16 = state->sel.end_u16;
        snapshot.collapsed = state->sel.start_u16 == state->sel.end_u16;
        snapshot.range_count = snapshot.control ? 1 : 0;
        return snapshot;
    }

    // A script-owned editor can keep auxiliary form controls (such as a
    // toolbar search field) alive while focus remains in its contenteditable
    // host. Rich transactions must snapshot their DOM range, never that
    // unrelated text-control projection.
    DomSelection* selection = state->dom_selection;
    if (!selection || selection->range_count == 0) {
        return snapshot;
    }
    snapshot.kind = EDIT_SEL_DOM_RANGE;
    snapshot.direction = selection->direction;
    snapshot.range_count = selection->range_count;
    snapshot.collapsed = dom_selection_is_collapsed(selection);
    snapshot.anchor = dom_selection_anchor_boundary(selection);
    snapshot.focus = dom_selection_focus_boundary(selection);
    return snapshot;
}

struct EditingTargetRangeScope {
    EventContext* evcon;
    bool previous_active;
    const EditingTargetRange* previous_ranges;
    uint32_t previous_count;

    EditingTargetRangeScope(EventContext* ctx,
                            const EditingTargetRangeStatus* status) {
        evcon = ctx;
        previous_active = ctx ? ctx->editing_target_ranges_active : false;
        previous_ranges = ctx ? ctx->editing_target_ranges : nullptr;
        previous_count = ctx ? ctx->editing_target_range_count : 0;
        if (!ctx) return;
        ctx->editing_target_ranges_active = true;
        ctx->editing_target_ranges = status ? status->ranges : nullptr;
        ctx->editing_target_range_count = status ? status->count : 0;
    }

    ~EditingTargetRangeScope() {
        if (!evcon) return;
        evcon->editing_target_ranges_active = previous_active;
        evcon->editing_target_ranges = previous_ranges;
        evcon->editing_target_range_count = previous_count;
    }
};

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

static void editing_log_write_target_ranges(JsonWriter* w,
                                            const EditingTargetRangeStatus* status) {
    jw_key(w, "target_ranges");
    jw_obj_begin(w);
        jw_kv_bool(w, "required", status ? status->required : false);
        jw_kv_uint(w, "count", status ? status->count : 0);
        jw_kv_bool(w, "valid", status ? status->valid : true);
    jw_obj_end(w);
}

static const char* editing_log_selection_kind_name(EditingSelectionKind kind) {
    switch (kind) {
        case EDIT_SEL_DOM_RANGE: return "dom_range";
        case EDIT_SEL_TEXT_CONTROL: return "text_control";
        case EDIT_SEL_NONE:
        default:
            return "none";
    }
}

static const char* editing_log_selection_direction_name(
        DomSelectionDirection direction) {
    switch (direction) {
        case DOM_SEL_DIR_FORWARD: return "forward";
        case DOM_SEL_DIR_BACKWARD: return "backward";
        case DOM_SEL_DIR_NONE:
        default:
            return "none";
    }
}

static void editing_log_write_boundary(JsonWriter* w,
                                       const char* key,
                                       const DomBoundary* boundary) {
    jw_key(w, key);
    if (!boundary || !boundary->node) {
        jw_null(w);
        return;
    }
    jw_obj_begin(w);
        event_state_log_write_node_ref(w, "node", boundary->node);
        jw_kv_uint(w, "offset", boundary->offset);
    jw_obj_end(w);
}

static void editing_log_write_selection(JsonWriter* w,
                                        const char* key,
                                        const EditingSelectionSnapshot* snapshot) {
    jw_key(w, key);
    if (!snapshot) {
        jw_null(w);
        return;
    }
    jw_obj_begin(w);
        jw_kv_str(w, "kind", editing_log_selection_kind_name(snapshot->kind));
        jw_kv_str(w, "direction",
                  editing_log_selection_direction_name(snapshot->direction));
        jw_kv_uint(w, "mutation_seq", snapshot->mutation_seq);
        jw_kv_bool(w, "collapsed", snapshot->collapsed);
        jw_kv_uint(w, "range_count", snapshot->range_count);
        if (snapshot->kind == EDIT_SEL_TEXT_CONTROL) {
            event_state_log_write_node_ref(w, "control",
                (const DomNode*)snapshot->control);
            jw_kv_uint(w, "start_u16", snapshot->start_u16);
            jw_kv_uint(w, "end_u16", snapshot->end_u16);
        } else if (snapshot->kind == EDIT_SEL_DOM_RANGE) {
            editing_log_write_boundary(w, "anchor", &snapshot->anchor);
            editing_log_write_boundary(w, "focus", &snapshot->focus);
        }
    jw_obj_end(w);
}

static bool editing_selection_snapshot_changed(
        const EditingSelectionSnapshot* before,
        const EditingSelectionSnapshot* after) {
    if (!before || !after) return false;
    return before->kind != after->kind ||
        before->direction != after->direction ||
        before->mutation_seq != after->mutation_seq ||
        before->collapsed != after->collapsed ||
        before->range_count != after->range_count ||
        before->start_u16 != after->start_u16 ||
        before->end_u16 != after->end_u16 ||
        before->anchor.node != after->anchor.node ||
        before->anchor.offset != after->anchor.offset ||
        before->focus.node != after->focus.node ||
        before->focus.offset != after->focus.offset ||
        before->control != after->control;
}

static const char* editing_log_route_name(EditingRouteKind route) {
    switch (route) {
        case EDITING_ROUTE_DOM_SCRIPT: return "dom_script";
        case EDITING_ROUTE_RADIANT_TEMPLATE: return "radiant_template";
        case EDITING_ROUTE_NONE:
        default: return "none";
    }
}

static void editing_log_contenteditable_result(
        EventContext* evcon, const EditingPreparedTransaction* prepared,
        const EditingTargetRangeStatus* target_ranges,
        const EditingTransactionResult* result) {
    DocState* state = editing_dispatch_doc_state(evcon);
    if (!state || !prepared || !result ||
        !event_state_log_enabled(state->active_event_log)) {
        return;
    }
    EditingSelectionSnapshot selection_after =
        editing_dispatch_selection_snapshot(state, &prepared->surface);
    char buf[8192];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "editing.transaction", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_uint(&w, "transaction_id", prepared->transaction_id);
        jw_kv_str(&w, "route", editing_log_route_name(prepared->route.kind));
        jw_kv_uint(&w, "route_owner_generation",
                   prepared->route.owner_generation);
        jw_kv_uint(&w, "registry_generation", prepared->registry_generation);
        jw_kv_uint(&w, "registration_id",
                   prepared->selected_registration_id);
        jw_kv_str(&w, "action_handler",
                  result->action_handler_id ? result->action_handler_id : "");
        event_state_log_write_node_ref(&w, "host", prepared->host_ref.address);
        editing_log_write_surface(&w, &prepared->surface);
        editing_log_write_intent(&w, &prepared->surface, &prepared->intent);
        editing_log_write_target_ranges(&w, target_ranges);
        editing_log_write_selection(&w, "selection_before",
                                    &prepared->selection_before);
        editing_log_write_selection(&w, "selection_after", &selection_after);
        jw_key(&w, "result");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "prepared", result->prepared);
            jw_kv_bool(&w, "beforeinput_dispatched",
                       result->beforeinput_dispatched);
            jw_kv_bool(&w, "beforeinput_prevented",
                       result->beforeinput_prevented);
            jw_kv_bool(&w, "beforeinput_mutated_dom",
                       result->beforeinput_mutated_dom);
            jw_kv_bool(&w, "contract_violation",
                       result->contract_violation);
            jw_kv_bool(&w, "action_selected", result->action_selected);
            jw_kv_bool(&w, "action_invoked", result->action_invoked);
            jw_kv_bool(&w, "action_claimed", result->action_claimed);
            jw_kv_bool(&w, "dom_mutated", result->dom_mutated);
            jw_kv_bool(&w, "model_reconciled", result->model_reconciled);
            jw_kv_bool(&w, "selection_changed", result->selection_changed);
            jw_kv_bool(&w, "input_dispatched", result->input_dispatched);
            jw_kv_bool(&w, "unsupported_fallthrough",
                       result->unsupported_fallthrough);
            jw_kv_bool(&w, "failed", result->failed);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

void editing_prepared_transaction_dispose(EditingPreparedTransaction* prepared) {
    if (!prepared) return;
    input_intent_dispose(&prepared->intent);
    prepared->selected_handler_id = nullptr;
    prepared->target_range_count = 0;
}

bool editing_notify_beforeinput(EventContext* evcon,
                                const EditingPreparedTransaction* prepared,
                                const EditingNotificationHooks* hooks,
                                bool* out_prevented) {
    if (out_prevented) *out_prevented = false;
    if (!evcon || !prepared || !hooks || !hooks->dispatch_beforeinput ||
        !prepared->surface.view || !input_intent_is_dispatchable(prepared->intent.type)) {
        return false;
    }
    // This boundary constructs and dispatches the notification only. Action
    // selection, Lambda execution, and DOM mutation stay in the gate stages.
    bool prevented = hooks->dispatch_beforeinput(evcon, prepared, hooks->user);
    if (out_prevented) *out_prevented = prevented;
    return true;
}

void editing_notify_input(EventContext* evcon,
                          const EditingPreparedTransaction* prepared,
                          const EditingNotificationHooks* hooks) {
    if (!evcon || !prepared || !hooks || !hooks->dispatch_input ||
        !prepared->surface.view || !input_intent_is_dispatchable(prepared->intent.type)) {
        return;
    }
    // input is a post-action notification and its cancellation state is never
    // read back into default processing.
    hooks->dispatch_input(evcon, prepared, hooks->user);
}

static bool editing_node_is_connected(DomDocument* document, DomNode* node) {
    if (!document || !node) return false;
    if (document->root && view_tree_contains_view(
            static_cast<DomNode*>(document->root), static_cast<View*>(node))) {
        return true;
    }
    return document->view_tree && document->view_tree->root &&
        view_tree_contains_view(static_cast<DomNode*>(document->view_tree->root),
                                static_cast<View*>(node));
}

DomElement* editing_prepared_live_host(
        DomDocument* document, const EditingPreparedTransaction* prepared) {
    if (!document || !prepared) return nullptr;
    DomNode* live_host = nullptr;
    if (prepared->host_view_id && document->view_tree &&
            document->view_tree->root) {
        // Event selection and target ranges belong to the current view tree.
        // A retained source-DOM record can be valid yet detached from that
        // tree after relayout, so prefer the logical view identity first.
        live_host = static_cast<DomNode*>(view_tree_find_live_id(
            static_cast<DomNode*>(document->view_tree->root),
            prepared->host_view_id));
    }
    if (!live_host && prepared->host_ref.address) {
        live_host = dom_node_ref_validate(document, prepared->host_ref);
    }
    if (!live_host) {
        return nullptr;
    }
    if (!live_host->is_element()) {
        return nullptr;
    }
    if (!editing_node_is_connected(document, live_host)) {
        return nullptr;
    }
    EditingSurface current_surface;
    if (!editing_surface_from_target(static_cast<View*>(live_host),
                                     &current_surface)) {
        return nullptr;
    }
    // A listener may detach the host, but it may not cause this transaction to
    // reroute to another editable owner after its action snapshot was chosen.
    if (!editing_surface_is_rich(&current_surface) ||
            current_surface.owner != live_host->as_element()) {
        return nullptr;
    }
    return current_surface.owner;
}

bool editing_run_contenteditable_transaction(
        EventContext* evcon, const EditingSurface* surface,
        const EditingIntent* intent, const EditingRouteSnapshot* route,
        const EditingNotificationHooks* notifications,
        EditingTransactionResult* out_result) {
    EditingTransactionResult result = {};
    if (out_result) *out_result = result;
    if (!evcon || !surface || !intent || !notifications ||
        !editing_surface_is_rich(surface) || !surface->owner ||
        intent->type == INPUT_INTENT_NONE) {
        return false;
    }

    DomDocument* document = evcon->target_document
        ? evcon->target_document : surface->owner->doc;
    DocState* state = document ? (DocState*)document->state : nullptr;
    if (!document || !state) return false;

    EditingPreparedTransaction prepared = {};
    prepared.transaction_id = editing_dispatch_next_transaction_id(state);
    prepared.surface = *surface;
    prepared.route = route ? *route : EditingRouteSnapshot{EDITING_ROUTE_NONE, nullptr, 0};
    prepared.host_ref = dom_node_ref(static_cast<DomNode*>(surface->owner));
    prepared.host_view_id = static_cast<DomNode*>(surface->owner)->id;
    result.transaction_id = prepared.transaction_id;
    result.route = prepared.route.kind;

    EditingIntent normalized_intent = {};
    const EditingIntent* prepared_intent = intent;
    if (editing_dispatch_plaintext_normalize_intent(surface, intent,
                                                    &normalized_intent)) {
        prepared_intent = &normalized_intent;
    }
    if (!input_intent_clone(prepared_intent, &prepared.intent)) {
        result.failed = true;
        editing_log_contenteditable_result(evcon, &prepared, nullptr, &result);
        if (out_result) *out_result = result;
        return false;
    }
    EditingTargetRangeStatus target_status = editing_dispatch_target_range_status(
        state, surface, &prepared.intent);
    if (!target_status.valid || target_status.count > 4) {
        // A false-island or structurally stale target has no native fallback.
        result.failed = true;
        editing_log_contenteditable_result(evcon, &prepared, &target_status, &result);
        editing_prepared_transaction_dispose(&prepared);
        if (out_result) *out_result = result;
        return true;
    }
    prepared.target_range_count = target_status.count;
    memcpy(prepared.target_ranges, target_status.ranges,
           sizeof(EditingTargetRange) * target_status.count);
    prepared.selection_before = editing_dispatch_selection_snapshot(state, surface);
    editing_interaction_set_active_surface(state, surface);
    result.prepared = true;

    if (prepared.route.kind == EDITING_ROUTE_DOM_SCRIPT) {
        editing_dom_action_register(document);
    }
    prepared.registry_generation = editing_action_registry_generation(document);
    EditingActionSnapshot action_snapshot = {};
    bool configuration_error = false;
    if (editing_action_registry_select(document, &prepared, &action_snapshot,
                                       &configuration_error)) {
        prepared.selected_registration_id = action_snapshot.registration_id;
        prepared.selected_handler_id = action_snapshot.handler_id;
        result.action_selected = true;
        result.action_handler_id = action_snapshot.handler_id;
    } else if (configuration_error) {
        result.failed = true;
        editing_log_contenteditable_result(evcon, &prepared, &target_status, &result);
        editing_prepared_transaction_dispose(&prepared);
        if (out_result) *out_result = result;
        return true;
    }

    EditingTargetRangeScope target_range_scope(evcon, &target_status);
    prepared.mutation_epoch_before_notification = js_dom_mutation_epoch(document);
    uint32_t mutation_sequence_before_notification = document->js.mutation_sequence;
    DomElement* notification_host = editing_prepared_live_host(document, &prepared);
    bool beforeinput_prevented = false;
    bool notification_required = input_intent_is_dispatchable(prepared.intent.type);
    // Script-owned editors can replace the selected subtree while handling
    // beforeinput; selection/focus observers must defer rich invariants until
    // the notification returns and the host can be re-anchored below.
    bool previous_in_script_dispatch = state->editing.rich_transaction_in_script_dispatch;
    state->editing.rich_transaction_in_script_dispatch = true;
    bool beforeinput_dispatched = notification_required &&
        editing_notify_beforeinput(evcon, &prepared, notifications,
                                   &beforeinput_prevented);
    state->editing.rich_transaction_in_script_dispatch = previous_in_script_dispatch;
    result.beforeinput_dispatched = beforeinput_dispatched;
    result.beforeinput_prevented = beforeinput_prevented;
    uint64_t mutation_epoch_after_notification = js_dom_mutation_epoch(document);
    // Status observers may write outside the editing host during beforeinput;
    // only host-affecting mutations invalidate its prepared target ranges.
    result.beforeinput_mutated_dom = mutation_epoch_after_notification !=
        prepared.mutation_epoch_before_notification &&
        js_dom_mutation_since_affects_subtree(document,
            mutation_sequence_before_notification, notification_host);

    if (beforeinput_prevented) {
        EditingSurface host_surface;
        DomElement* live_host = editing_prepared_live_host(document, &prepared);
        if (live_host && editing_surface_from_target(static_cast<View*>(live_host),
                                        &host_surface) &&
            editing_surface_is_rich(&host_surface)) {
            // A canceled listener may synchronously rebuild its host. Keep the
            // interaction projection anchored to the surviving host instead of
            // retaining the detached leaf until queued selectionchange runs.
            editing_interaction_set_active_surface(state, &host_surface);
        }
    }

    if (notification_required && !beforeinput_dispatched) {
        result.failed = true;
    } else if (result.beforeinput_mutated_dom) {
        // Prepared target ranges cannot be safely reinterpreted after an
        // uncancelled listener mutates the DOM in the same transaction.
        result.contract_violation = !beforeinput_prevented;
    } else if (beforeinput_prevented) {
        // Cancellation is a complete action result; do not synthesize input.
    } else if (!result.action_selected) {
        result.unsupported_fallthrough = true;
    } else if (prepared.route.kind == EDITING_ROUTE_DOM_SCRIPT &&
            !editing_prepared_live_host(document, &prepared)) {
        result.failed = true;
    } else {
        // A template click can retransform its source DOM before the next
        // layout pass replaces the old view tree. Its frozen model route is
        // still valid here; DOM actions instead require a live native host.
        result.action_invoked = true;
        EditingActionOutcome outcome = editing_action_snapshot_invoke(
            &action_snapshot, evcon, &prepared);
        uint64_t mutation_epoch_after_action = js_dom_mutation_epoch(document);
        result.dom_mutated = mutation_epoch_after_action !=
            mutation_epoch_after_notification;
        result.action_claimed = outcome.status == EDITING_ACTION_CLAIMED;
        result.model_reconciled = outcome.model_reconciled;
        EditingSelectionSnapshot selection_after =
            editing_dispatch_selection_snapshot(state, surface);
        result.selection_changed = outcome.selection_changed ||
            editing_selection_snapshot_changed(&prepared.selection_before,
                                               &selection_after);
        result.failed = outcome.status == EDITING_ACTION_ERROR;
        result.unsupported_fallthrough = outcome.status == EDITING_ACTION_PASS;
        bool composition_cancel =
            prepared.intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT;
        if (!result.failed && notification_required && !composition_cancel &&
            (result.action_claimed || result.dom_mutated ||
             result.model_reconciled)) {
            editing_notify_input(evcon, &prepared, notifications);
            result.input_dispatched = true;
        }
    }

    editing_log_contenteditable_result(evcon, &prepared, &target_status, &result);
    editing_action_snapshot_release(&action_snapshot);
    editing_prepared_transaction_dispose(&prepared);
    if (out_result) *out_result = result;
    return true;
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

bool editing_dispatch_form_beforeinput(EventContext* evcon,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       const EditingFormNotificationHooks* hooks,
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
                       js_prevented);
    log_debug("editing_dispatch_form_beforeinput: surface=%s inputType=%s prevented=%d",
              editing_mode_name(surface->mode),
              input_intent_type_name(intent->type),
              js_prevented ? 1 : 0);
    return dispatchable;
}

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
}
