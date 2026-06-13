#include "editing_dispatch.hpp"

#include "editing_geometry.hpp"
#include "editing_host.hpp"
#include "editing_target_range.hpp"
#include "event_state_log.hpp"
#include "handler.hpp"
#include "state_machine.hpp"
#include "state_schema.hpp"
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

static uint64_t g_editing_transaction_next_id = 1;

static uint64_t editing_dispatch_next_transaction_id() {
    uint64_t id = g_editing_transaction_next_id++;
    if (g_editing_transaction_next_id == 0) {
        g_editing_transaction_next_id = 1;
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

static bool editing_history_intent_can_coalesce(const EditingIntent* intent) {
    return intent && intent->type == INPUT_INTENT_INSERT_TEXT;
}

static bool editing_history_intent_is_recordable(const EditingIntent* intent) {
    if (!intent) return false;
    switch (intent->type) {
        case INPUT_INTENT_NONE:
        case INPUT_INTENT_COMPOSITION_START:
        case INPUT_INTENT_INSERT_COMPOSITION_TEXT:
        case INPUT_INTENT_FORMAT_BOLD:
        case INPUT_INTENT_FORMAT_ITALIC:
        case INPUT_INTENT_FORMAT_UNDERLINE:
        case INPUT_INTENT_FORMAT_INDENT:
        case INPUT_INTENT_FORMAT_OUTDENT:
        case INPUT_INTENT_SELECT_ALL:
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
            return false;
        default:
            return true;
    }
}

static bool editing_history_intent_is_boundary(const EditingIntent* intent) {
    if (!intent) return false;
    switch (intent->type) {
        case INPUT_INTENT_INSERT_PARAGRAPH:
        case INPUT_INTENT_INSERT_LINE_BREAK:
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:
        case INPUT_INTENT_INSERT_HORIZONTAL_RULE:
        case INPUT_INTENT_INSERT_LINK:
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_YANK:
        case INPUT_INTENT_INSERT_FROM_DROP:
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
        case INPUT_INTENT_DELETE_CONTENT_FORWARD:
        case INPUT_INTENT_DELETE_WORD_BACKWARD:
        case INPUT_INTENT_DELETE_WORD_FORWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:
        case INPUT_INTENT_DELETE_BY_CUT:
        case INPUT_INTENT_DELETE_BY_DRAG:
        case INPUT_INTENT_INSERT_FROM_COMPOSITION:
        case INPUT_INTENT_DELETE_COMPOSITION_TEXT:
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
            return true;
        default:
            return false;
    }
}

static const char* editing_history_boundary_reason(const EditingIntent* intent) {
    if (!intent) return "none";
    switch (intent->type) {
        case INPUT_INTENT_INSERT_PARAGRAPH:
        case INPUT_INTENT_INSERT_LINE_BREAK:
            return "enter";
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_YANK:
            return "paste";
        case INPUT_INTENT_INSERT_FROM_DROP:
            return "drop";
        case INPUT_INTENT_DELETE_BY_CUT:
            return "cut";
        case INPUT_INTENT_DELETE_BY_DRAG:
            return "drag";
        case INPUT_INTENT_INSERT_FROM_COMPOSITION:
        case INPUT_INTENT_DELETE_COMPOSITION_TEXT:
            return "ime_commit";
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
            return "history";
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:
        case INPUT_INTENT_INSERT_HORIZONTAL_RULE:
        case INPUT_INTENT_INSERT_LINK:
            return "structural";
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
        case INPUT_INTENT_DELETE_CONTENT_FORWARD:
        case INPUT_INTENT_DELETE_WORD_BACKWARD:
        case INPUT_INTENT_DELETE_WORD_FORWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:
            return "delete";
        default:
            return "none";
    }
}

static const char* editing_transaction_transfer_kind(
        const EditingIntent* intent) {
    if (!intent) return "none";
    switch (intent->type) {
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_YANK:
            return "paste";
        case INPUT_INTENT_INSERT_FROM_DROP:
            return "drop";
        case INPUT_INTENT_DELETE_BY_CUT:
            return "cut";
        case INPUT_INTENT_DELETE_BY_DRAG:
            return "drag";
        default:
            return "none";
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

static bool editing_dispatch_normalize_transaction(
        const EditingTransaction* tx,
        EditingTransaction* normalized_tx,
        EditingIntent* normalized_intent) {
    if (!tx || !normalized_tx || !normalized_intent) return false;
    if (!editing_dispatch_plaintext_normalize_intent(tx->surface, tx->intent,
                                                     normalized_intent)) {
        return false;
    }
    *normalized_tx = *tx;
    normalized_tx->intent = normalized_intent;
    return true;
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

struct EditingSelectionSnapshot {
    EditingSelectionKind kind;
    DomSelectionDirection direction;
    uint32_t mutation_seq;
    uint32_t projection_seq;
    bool collapsed;
    uint32_t range_count;
    DomBoundary anchor;
    DomBoundary focus;
    DomElement* control;
    uint32_t start_u16;
    uint32_t end_u16;
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

static EditingSelectionSnapshot editing_dispatch_selection_snapshot(DocState* state) {
    EditingSelectionSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.kind = EDIT_SEL_NONE;
    snapshot.direction = DOM_SEL_DIR_NONE;
    snapshot.mutation_seq = state ? state->selection_mutation_seq : 0;
    snapshot.projection_seq = state ? state->selection_projection_seq : 0;

    if (!state) return snapshot;
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
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

static void editing_dispatch_clear_rich_target_ranges(DocState* state) {
    if (!state) return;
    state->editing.rich_transaction_target_ranges_active = false;
    state->editing.rich_transaction_target_ranges_required = false;
    state->editing.rich_transaction_target_ranges_valid = true;
    state->editing.rich_transaction_input_type = (uint32_t)INPUT_INTENT_NONE;
    state->editing.rich_transaction_selection_seq = 0;
    state->editing.rich_transaction_target_range_count = 0;
    memset(state->editing.rich_transaction_target_ranges, 0,
           sizeof(state->editing.rich_transaction_target_ranges));
}

static void editing_dispatch_set_rich_target_ranges(
        DocState* state,
        const EditingTargetRangeStatus* status,
        const EditingIntent* intent) {
    if (!state) return;
    if (!status) {
        editing_dispatch_clear_rich_target_ranges(state);
        return;
    }

    state->editing.rich_transaction_target_ranges_active = true;
    state->editing.rich_transaction_target_ranges_required = status->required;
    state->editing.rich_transaction_target_ranges_valid = status->valid;
    state->editing.rich_transaction_input_type =
        intent ? (uint32_t)intent->type : (uint32_t)INPUT_INTENT_NONE;
    state->editing.rich_transaction_selection_seq =
        state->selection_mutation_seq;
    uint32_t count = status->count;
    if (count > 4) count = 4;
    state->editing.rich_transaction_target_range_count = count;
    memset(state->editing.rich_transaction_target_ranges, 0,
           sizeof(state->editing.rich_transaction_target_ranges));
    for (uint32_t i = 0; i < count; i++) {
        state->editing.rich_transaction_target_ranges[i].start =
            status->ranges[i].start;
        state->editing.rich_transaction_target_ranges[i].end =
            status->ranges[i].end;
    }
}

static void editing_dispatch_set_rich_phase(DocState* state,
                                            EditingRichTransactionPhase phase,
                                            View* target) {
    if (!state) return;
    state->editing.rich_transaction_phase = phase;
    state->editing.rich_transaction_target =
        phase == EDITING_RICH_TX_IDLE ? nullptr : target;
    if (phase == EDITING_RICH_TX_IDLE) {
        editing_dispatch_clear_rich_target_ranges(state);
    }
}

static void editing_dispatch_commit_phase(DocState* state,
                                          SmEvent event,
                                          View* target,
                                          EditingRichTransactionPhase phase,
                                          uint32_t action,
                                          const char* context) {
    if (!state) return;
    SmTransitionGuard phase_guard(state, SM_FAMILY_RICH_EDIT, event, target);
    if (action != SM_ACT_NONE) {
        sm_observe_action(state, action);
    }
    editing_dispatch_set_rich_phase(state, phase, target);
    phase_guard.commit();
    radiant_state_assert_valid(state, context);
}

static bool editing_dispatch_surface_same(const EditingSurface* a,
                                          const EditingSurface* b) {
    return a && b &&
        a->kind == b->kind &&
        a->mode == b->mode &&
        a->owner == b->owner &&
        a->view == b->view;
}

static bool editing_dispatch_surface_contains_dom_boundary(
        const EditingSurface* surface,
        const DomBoundary* boundary) {
    if (!surface || !boundary || !boundary->node) return false;

    EditingBoundary edit_boundary;
    editing_boundary_clear(&edit_boundary);
    edit_boundary.kind = EDITING_BOUNDARY_DOM;
    edit_boundary.surface = *surface;
    edit_boundary.dom = *boundary;
    edit_boundary.view = static_cast<View*>(boundary->node);
    return editing_geometry_surface_contains_boundary(surface, &edit_boundary);
}

static bool editing_dispatch_live_surface_from_selection(DocState* state,
                                                         EditingSurface* out) {
    if (out) editing_surface_clear(out);
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0) {
        return false;
    }

    DomSelection* selection = state->dom_selection;
    DomBoundary anchor = dom_selection_anchor_boundary(selection);
    DomBoundary focus = dom_selection_focus_boundary(selection);
    DomBoundary target = focus.node ? focus : anchor;
    if (!target.node) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(static_cast<View*>(target.node), &surface) ||
        !editing_surface_is_rich(&surface) ||
        surface.target_in_false_island) {
        return false;
    }
    if (!editing_dispatch_surface_contains_dom_boundary(&surface, &anchor) ||
        !editing_dispatch_surface_contains_dom_boundary(&surface, &focus)) {
        return false;
    }

    if (out) *out = surface;
    return true;
}

static bool editing_dispatch_sync_rich_transaction_to_selection(
        DocState* state,
        EditingSurface* surface,
        View** tx_target) {
    if (!state || !surface || !tx_target ||
        !editing_surface_is_rich(surface)) {
        return false;
    }

    EditingSurface live_surface;
    if (!editing_dispatch_live_surface_from_selection(state, &live_surface)) {
        return false;
    }
    if (editing_dispatch_surface_same(surface, &live_surface) &&
        *tx_target == live_surface.view) {
        return false;
    }

    *surface = live_surface;
    *tx_target = live_surface.view;
    editing_interaction_set_active_surface(state, &live_surface);
    if (state->editing.rich_transaction_phase != EDITING_RICH_TX_IDLE) {
        state->editing.rich_transaction_target = live_surface.view;
    }
    log_debug("editing_dispatch: synced rich transaction target after selection handoff");
    return true;
}

static bool editing_dispatch_target_ranges_are_valid(DocState* state,
                                                     const EditingSurface* surface,
                                                     const EditingIntent* intent) {
    EditingTargetRangeStatus status =
        editing_dispatch_target_range_status(state, surface, intent);
    return status.valid;
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
        jw_kv_uint(w, "projection_seq", snapshot->projection_seq);
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

static void editing_log_write_history_metadata(JsonWriter* w,
                                               const EditingSurface* surface,
                                               const EditingIntent* intent) {
    bool can_coalesce = editing_history_intent_can_coalesce(intent);
    bool is_boundary = editing_history_intent_is_boundary(intent);
    jw_key(w, "history");
    jw_obj_begin(w);
        jw_kv_str(w, "owned_by",
                  editing_surface_is_text_control(surface) ? "radiant" : "consumer");
        jw_kv_bool(w, "owned_by_form",
                   editing_surface_is_text_control(surface));
        jw_kv_bool(w, "recordable",
                   editing_history_intent_is_recordable(intent));
        jw_kv_bool(w, "history_boundary", is_boundary);
        jw_kv_str(w, "boundary_reason",
                  editing_history_boundary_reason(intent));
        jw_kv_bool(w, "coalescable", can_coalesce);
        jw_kv_str(w, "coalesce_key", can_coalesce ? "insertText" : "");
    jw_obj_end(w);
}

static bool editing_selection_snapshot_changed(
        const EditingSelectionSnapshot* before,
        const EditingSelectionSnapshot* after) {
    if (!before || !after) return false;
    return before->kind != after->kind ||
        before->direction != after->direction ||
        before->mutation_seq != after->mutation_seq ||
        before->projection_seq != after->projection_seq ||
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

static void editing_log_write_transaction_summary(
        JsonWriter* w,
        const EditingSurface* surface,
        const EditingIntent* intent,
        const EditingTargetRangeStatus* target_ranges,
        const EditingSelectionSnapshot* selection_before,
        const EditingSelectionSnapshot* selection_after,
        bool prevented,
        bool lambda_handled,
        bool mutated,
        bool committed) {
    bool redacted = editing_log_redact(surface);
    jw_key(w, "summary");
    jw_obj_begin(w);
        jw_kv_bool(w, "beforeinput_prevented", prevented);
        jw_kv_bool(w, "lambda_handled", lambda_handled);
        jw_kv_bool(w, "dom_mutated", mutated);
        jw_kv_bool(w, "input_event_dispatched", committed);
        jw_kv_bool(w, "selection_changed",
                   editing_selection_snapshot_changed(selection_before,
                                                      selection_after));
        jw_kv_bool(w, "target_ranges_valid",
                   target_ranges ? target_ranges->valid : true);
        jw_kv_uint(w, "target_range_count",
                   target_ranges ? target_ranges->count : 0);
        jw_kv_bool(w, "is_composing",
                   intent ? intent->is_composing : false);
        jw_kv_str(w, "transfer_kind",
                  editing_transaction_transfer_kind(intent));
        jw_kv_str(w, "transfer_mime",
                  redacted || !intent || !intent->data_mime
                      ? ""
                      : intent->data_mime);
        jw_kv_uint(w, "transfer_text_len",
                   redacted || !intent ? 0 : editing_log_cstr_len(intent->data));
        jw_kv_uint(w, "transfer_html_len",
                   redacted || !intent ? 0 : editing_log_cstr_len(intent->html_data));
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

static void editing_log_transaction(EventContext* evcon,
                                    const EditingTransaction* tx,
                                    uint64_t transaction_id,
                                    const EditingTargetRangeStatus* target_ranges,
                                    const EditingSelectionSnapshot* selection_before,
                                    const EditingSelectionSnapshot* selection_after,
                                    const char* phase,
                                    bool prevented,
                                    bool lambda_handled,
                                    bool mutated,
                                    bool committed) {
    DocState* state = editing_dispatch_doc_state(evcon);
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    const EditingSurface* surface = tx ? tx->surface : nullptr;
    const EditingIntent* intent = tx ? tx->intent : nullptr;
    char buf[8192];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "editing.transaction", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_uint(&w, "transaction_id", transaction_id);
        jw_kv_str(&w, "phase", phase ? phase : "");
        jw_kv_str(&w, "operation",
                  tx && tx->operation ? tx->operation : "default");
        editing_log_write_surface(&w, surface);
        editing_log_write_intent(&w, surface, intent);
        editing_log_write_target_ranges(&w, target_ranges);
        editing_log_write_selection(&w, "selection_before", selection_before);
        editing_log_write_selection(&w, "selection_after", selection_after);
        editing_log_write_history_metadata(&w, surface, intent);
        editing_log_write_transaction_summary(&w, surface, intent,
            target_ranges, selection_before, selection_after,
            prevented, lambda_handled, mutated, committed);
        jw_kv_bool(&w, "prevented", prevented);
        jw_kv_bool(&w, "lambda_handled", lambda_handled);
        jw_kv_bool(&w, "mutated", mutated);
        jw_kv_bool(&w, "committed", committed);
        jw_kv_bool(&w, "dispatch_input_without_mutation",
                   tx ? tx->dispatch_input_without_mutation : false);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

void editing_dispatch_log_intent(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent) {
    editing_log_record(evcon, surface, intent, "editing.intent", false, false);
}

bool editing_run_transaction(EventContext* evcon,
                             const EditingTransaction* tx,
                             bool* out_prevented,
                             bool* out_mutated,
                             bool* out_lambda_handled) {
    if (out_prevented) *out_prevented = false;
    if (out_mutated) *out_mutated = false;
    if (out_lambda_handled) *out_lambda_handled = false;
    if (!evcon || !tx || !tx->surface || !tx->intent || !tx->hooks ||
        tx->intent->type == INPUT_INTENT_NONE) {
        return false;
    }

    EditingIntent normalized_intent;
    EditingTransaction normalized_tx;
    const EditingTransaction* run_tx = tx;
    if (editing_dispatch_normalize_transaction(tx, &normalized_tx,
                                               &normalized_intent)) {
        run_tx = &normalized_tx;
    }

    EditingSurface current_surface = *run_tx->surface;
    EditingTransaction current_tx = *run_tx;
    current_tx.surface = &current_surface;

    DocState* state = editing_dispatch_doc_state(evcon);
    EditingTargetRangeStatus target_ranges =
        editing_dispatch_target_range_status(state, current_tx.surface,
                                             current_tx.intent);
    EditingSelectionSnapshot selection_before =
        editing_dispatch_selection_snapshot(state);
    uint64_t transaction_id = editing_dispatch_next_transaction_id();
    editing_log_transaction(evcon, &current_tx, transaction_id, &target_ranges,
                            &selection_before, nullptr, "begin",
                            false, false, false, false);
    EditingTargetRangeScope target_range_scope(evcon, &target_ranges);
    View* tx_target = current_surface.view;
    if (state) {
        editing_interaction_set_active_surface(state, current_tx.surface);
        editing_dispatch_set_rich_target_ranges(state, &target_ranges,
                                                current_tx.intent);
    }

    editing_dispatch_commit_phase(state, SM_EV_EDIT_TX_BEGIN, tx_target,
                                  EDITING_RICH_TX_OPEN, SM_ACT_NONE,
                                  "editing_transaction_begin");

    bool prevented = false;
    bool lambda_handled = false;
    bool dispatched = false;
    if (state) {
        SmTransitionGuard before_guard(state, SM_FAMILY_RICH_EDIT,
                                       SM_EV_EDIT_BEFOREINPUT, tx_target);
        uint32_t beforeinput_selection_seq = state->selection_mutation_seq;
        dispatched = editing_dispatch_beforeinput_ex(evcon, current_tx.surface,
            current_tx.intent, current_tx.hooks, false, &prevented, &lambda_handled);
        if (dispatched) {
            sm_observe_action(state, SM_ACT_DISPATCH_BEFOREINPUT);
            if (state->selection_mutation_seq != beforeinput_selection_seq) {
                editing_dispatch_sync_rich_transaction_to_selection(
                    state, &current_surface, &tx_target);
            }
            editing_dispatch_set_rich_phase(state, EDITING_RICH_TX_BEFOREINPUT,
                                            tx_target);
            before_guard.commit();
            radiant_state_assert_valid(state, "editing_transaction_beforeinput");
        }
    } else {
        dispatched = editing_dispatch_beforeinput_ex(evcon, current_tx.surface,
            current_tx.intent, current_tx.hooks, false, &prevented, &lambda_handled);
    }
    if (!dispatched) {
        editing_dispatch_commit_phase(state, SM_EV_EDIT_TX_ABORT, tx_target,
                                      EDITING_RICH_TX_IDLE, SM_ACT_NONE,
                                      "editing_transaction_abort");
        EditingSelectionSnapshot selection_after =
            editing_dispatch_selection_snapshot(state);
        editing_log_transaction(evcon, &current_tx, transaction_id,
                                &target_ranges, &selection_before,
                                &selection_after, "abort", prevented,
                                lambda_handled, false, false);
        if (out_prevented) *out_prevented = prevented;
        if (out_lambda_handled) *out_lambda_handled = lambda_handled;
        return false;
    }

    bool mutated = false;
    uint32_t selection_seq_before = state ? state->selection_mutation_seq : 0;
    if (!prevented && !lambda_handled && current_tx.mutate) {
        if (state) {
            SmTransitionGuard mutate_guard(state, SM_FAMILY_RICH_EDIT,
                                           SM_EV_EDIT_MUTATE_DOM, tx_target);
            mutated = current_tx.mutate(evcon, state, current_tx.surface,
                                        current_tx.intent, current_tx.mutate_user);
            if (mutated) {
                sm_observe_action(state, SM_ACT_MUTATE_DOM);
                editing_dispatch_set_rich_phase(state, EDITING_RICH_TX_MUTATED,
                                                tx_target);
                mutate_guard.commit();
                radiant_state_assert_valid(state, "editing_transaction_mutate");
            }
        } else {
            mutated = current_tx.mutate(evcon, state, current_tx.surface,
                                        current_tx.intent, current_tx.mutate_user);
        }
    }

    if (mutated && state &&
        state->selection_mutation_seq != selection_seq_before) {
        editing_dispatch_commit_phase(state, SM_EV_EDIT_SET_SELECTION,
                                      tx_target,
                                      EDITING_RICH_TX_SELECTION_SET,
                                      SM_ACT_SET_SELECTION,
                                      "editing_transaction_set_selection");
    }

    bool should_input = input_intent_is_dispatchable(current_tx.intent->type) &&
        !prevented &&
        (lambda_handled || mutated || current_tx.dispatch_input_without_mutation);
    if (should_input) {
        if (state) {
            SmTransitionGuard input_guard(state, SM_FAMILY_RICH_EDIT,
                                          SM_EV_EDIT_INPUT, tx_target);
            editing_dispatch_input(evcon, current_tx.surface, current_tx.intent,
                                   current_tx.hooks);
            sm_observe_action(state, SM_ACT_DISPATCH_INPUT);
            editing_dispatch_set_rich_phase(state, EDITING_RICH_TX_INPUT,
                                            tx_target);
            input_guard.commit();
            radiant_state_assert_valid(state, "editing_transaction_input");
        } else {
            editing_dispatch_input(evcon, current_tx.surface, current_tx.intent,
                                   current_tx.hooks);
        }
    }

    editing_dispatch_commit_phase(state, SM_EV_EDIT_TX_COMMIT, tx_target,
                                  EDITING_RICH_TX_IDLE, SM_ACT_NONE,
                                  "editing_transaction_commit");
    EditingSelectionSnapshot selection_after =
        editing_dispatch_selection_snapshot(state);
    editing_log_transaction(evcon, &current_tx, transaction_id, &target_ranges,
                            &selection_before, &selection_after,
                            prevented ? "cancel" : "commit",
                            prevented, lambda_handled, mutated, should_input);
    if (out_prevented) *out_prevented = prevented;
    if (out_mutated) *out_mutated = mutated;
    if (out_lambda_handled) *out_lambda_handled = lambda_handled;
    return true;
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
