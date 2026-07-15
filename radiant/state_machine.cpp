/* Radiant interaction state-machine boundary — Phase 3 implementation. */

#include "event.hpp"
#include "state_store_internal.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"

#ifndef NDEBUG
#include <assert.h>
#endif
#include <stdio.h>
#include <string.h>

extern bool is_view_focusable(View* view);
extern bool is_view_programmatically_focusable(View* view);

#define STATE_MACHINE_RECORD_BUFSZ 4096

static void transition_enter(DocState* state) {
    if (state) state->transition_depth++;
}

static void transition_leave(DocState* state) {
    if (state && state->transition_depth > 0) state->transition_depth--;
}

static void state_machine_sync_selection_projection(DocState* state);

struct TransitionDepthScope {
    DocState* state;

    TransitionDepthScope(DocState* state) : state(state) {
        transition_enter(state);
    }

    ~TransitionDepthScope() {
        transition_leave(state);
    }
};

static bool finish_transition(DocState* state,
                              SmTransitionGuard* sm_guard,
                              const char* context,
                              bool sync_selection,
                              bool sync_editing) {
    if (sync_selection) state_machine_sync_selection_projection(state);
    if (sync_editing) editing_interaction_sync_projection(state);
    sm_guard->commit();
    radiant_state_assert_valid(state, context);
    return radiant_state_validate_interaction(state, NULL);
}

typedef void (*SingleTargetSetter)(DocState* state, View* target);

static bool single_target_transition(DocState* state,
                                     SmFamily family,
                                     SmEvent event,
                                     View* target,
                                     SingleTargetSetter setter,
                                     const char* context) {
    if (!state || !setter) return false;
    SmTransitionGuard sm_guard(state, family, event, target);
    {
        TransitionDepthScope transition_scope(state);
        setter(state, target);
    }
    return finish_transition(state, &sm_guard, context, false, false);
}

static bool focus_kind_to_sm_event(FocusTransitionKind kind,
                                   FocusTransitionArgs* args,
                                   SmEvent* out_event) {
    if (!out_event) return false;
    switch (kind) {
        case FOCUS_TRANSITION_FOCUS_ELEMENT:
            *out_event = args && args->target ? SM_EV_FOCUS_ELEMENT : SM_EV_BLUR_CURRENT;
            return true;
        case FOCUS_TRANSITION_BLUR_CURRENT:
            *out_event = SM_EV_BLUR_CURRENT;
            return true;
        case FOCUS_TRANSITION_MOVE:
            if (!args) return false;
            *out_event = args->forward ? SM_EV_FOCUS_MOVE_FWD : SM_EV_FOCUS_MOVE_BACK;
            return true;
        default:
            return false;
    }
}

bool focus_transition(DocState* state,
                      FocusTransitionKind kind,
                      FocusTransitionArgs* args) {
    if (!state) return false;
    SmEvent event;
    if (!focus_kind_to_sm_event(kind, args, &event)) return false;

    SmTransitionGuard sm_guard(state, SM_FAMILY_FOCUS, event,
                               args ? args->target : NULL);
    {
        TransitionDepthScope transition_scope(state);
        switch (kind) {
            case FOCUS_TRANSITION_FOCUS_ELEMENT:
                if (args && args->programmatic) {
                    focus_set_programmatic(state, args->target);
                } else {
                    focus_set(state, args ? args->target : NULL,
                              args ? args->from_keyboard : false);
                }
                break;
            case FOCUS_TRANSITION_BLUR_CURRENT:
                focus_clear(state);
                break;
            case FOCUS_TRANSITION_MOVE:
                if (!args) return false;
                if (!focus_move(state, args->root, args->forward)) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }
    return finish_transition(state, &sm_guard, "focus_transition", false, true);
}

static void state_machine_sync_selection_projection(DocState* state) {
    if (!state) return;
    selection_refresh_presentation(state);
}

static bool caret_kind_to_sm_event(CaretTransitionKind kind, SmEvent* out_event) {
    if (!out_event) return false;
    switch (kind) {
        case CARET_TRANSITION_COLLAPSE_TO_BOUNDARY:
            *out_event = SM_EV_COLLAPSE_TO_BOUNDARY;
            return true;
        default:
            return false;
    }
}

static bool selection_kind_to_sm_event(SelectionTransitionKind kind, SmEvent* out_event) {
    if (!out_event) return false;
    static const SmEvent events[] = {
        SM_EV_START_POINTER_SELECTION, SM_EV_END_POINTER_SELECTION,
        SM_EV_EXTEND_TO_BOUNDARY, SM_EV_EXTEND_TO_VIEW,
        SM_EV_SET_BASE_AND_EXTENT, SM_EV_SELECT_ALL,
        SM_EV_COLLAPSE_TO_START, SM_EV_COLLAPSE_TO_END, SM_EV_CLEAR_SELECTION,
    };
    if (kind < SELECTION_TRANSITION_START_POINTER_SELECTION ||
        kind > SELECTION_TRANSITION_CLEAR_SELECTION) return false;
    *out_event = events[kind];
    return true;
}

bool caret_transition(DocState* state,
                      CaretTransitionKind kind,
                      CaretTransitionArgs* args) {
    if (!state || !args) return false;
    SmEvent event;
    if (!caret_kind_to_sm_event(kind, &event)) return false;

    SmTransitionGuard sm_guard(state, SM_FAMILY_SELECTION, event,
                               args ? args->target : NULL);
    {
        TransitionDepthScope transition_scope(state);
        switch (kind) {
            case CARET_TRANSITION_COLLAPSE_TO_BOUNDARY:
                state_store_caret_collapse_to_view_offset(state, args->target, args->offset);
                break;
            default:
                return false;
        }
    }
    return finish_transition(state, &sm_guard, "caret_transition", true, true);
}

bool selection_transition(DocState* state,
                          SelectionTransitionKind kind,
                          SelectionTransitionArgs* args) {
    if (!state) return false;
    SmEvent event;
    if (!selection_kind_to_sm_event(kind, &event)) return false;

    SmTransitionGuard sm_guard(state, SM_FAMILY_SELECTION, event,
                               args ? args->target : NULL);
    {
        TransitionDepthScope transition_scope(state);
        switch (kind) {
            case SELECTION_TRANSITION_START_POINTER_SELECTION:
                if (!args) return false;
                state_store_selection_start_pointer(state, args->target, args->focus_offset);
                break;
            case SELECTION_TRANSITION_END_POINTER_SELECTION:
                state->editing.pointer_selecting = false;
                break;
            case SELECTION_TRANSITION_EXTEND_TO_BOUNDARY:
                if (!args) return false;
                state_store_selection_extend_to_offset(state, args->focus_offset);
                break;
            case SELECTION_TRANSITION_EXTEND_TO_VIEW:
                if (!args) return false;
                state_store_selection_extend_to_view(state, args->target, args->focus_offset);
                break;
            case SELECTION_TRANSITION_SET_BASE_AND_EXTENT:
                if (!args) return false;
                state_store_selection_set_view_offsets(state, args->target, args->anchor_offset, args->focus_offset);
                break;
            case SELECTION_TRANSITION_SELECT_ALL:
                state_store_selection_select_all(state);
                break;
            case SELECTION_TRANSITION_COLLAPSE_TO_START:
                state_store_selection_collapse_to_edge(state, true);
                break;
            case SELECTION_TRANSITION_COLLAPSE_TO_END:
                state_store_selection_collapse_to_edge(state, false);
                break;
            case SELECTION_TRANSITION_CLEAR_SELECTION:
                state_store_selection_clear(state);
                break;
            default:
                return false;
        }
    }
    return finish_transition(state, &sm_guard, "selection_transition", true, true);
}

static bool hover_kind_to_sm_event(HoverTransitionKind kind,
                                   HoverTransitionArgs* args,
                                   SmEvent* out_event) {
    if (!out_event) return false;
    switch (kind) {
        case HOVER_TRANSITION_SET_TARGET:
            *out_event = args && args->target ? SM_EV_HOVER_SET : SM_EV_HOVER_CLEAR;
            return true;
        default:
            return false;
    }
}

bool hover_transition(DocState* state,
                      HoverTransitionKind kind,
                      HoverTransitionArgs* args) {
    if (!state) return false;
    SmEvent event;
    if (!hover_kind_to_sm_event(kind, args, &event)) return false;

    return single_target_transition(state, SM_FAMILY_HOVER, event,
        args ? args->target : NULL, doc_state_set_hover_target,
        "hover_transition");
}

static bool active_kind_to_sm_event(ActiveTransitionKind kind,
                                    ActiveTransitionArgs* args,
                                    SmEvent* out_event) {
    if (!out_event) return false;
    switch (kind) {
        case ACTIVE_TRANSITION_SET_TARGET:
            *out_event = args && args->target ? SM_EV_ACTIVE_SET : SM_EV_ACTIVE_CLEAR;
            return true;
        default:
            return false;
    }
}

bool active_transition(DocState* state,
                       ActiveTransitionKind kind,
                       ActiveTransitionArgs* args) {
    if (!state) return false;
    SmEvent event;
    if (!active_kind_to_sm_event(kind, args, &event)) return false;

    return single_target_transition(state, SM_FAMILY_ACTIVE, event,
        args ? args->target : NULL, doc_state_set_active_target,
        "active_transition");
}

static bool drag_kind_to_sm_event(DragTransitionKind kind, SmEvent* out_event) {
    if (!out_event) return false;
    static const SmEvent events[] = {
        SM_EV_DRAG_SET_STATE, SM_EV_DRAG_BEGIN_DROP,
        SM_EV_DRAG_UPDATE_MOTION, SM_EV_DRAG_SET_DROP_ACTIVE,
        SM_EV_DRAG_SET_DROP_TARGET, SM_EV_DRAG_CLEAR_DROP,
    };
    if (kind < DRAG_TRANSITION_SET_STATE || kind > DRAG_TRANSITION_CLEAR_DROP) return false;
    *out_event = events[kind];
    return true;
}

static View* drag_transition_target(DragTransitionKind kind, DragTransitionArgs* args) {
    if (!args) return NULL;
    switch (kind) {
        case DRAG_TRANSITION_BEGIN_DROP:
            return args->source;
        case DRAG_TRANSITION_SET_DROP_TARGET:
            return args->drop_target;
        default:
            return args->target;
    }
}

bool drag_transition(DocState* state,
                     DragTransitionKind kind,
                     DragTransitionArgs* args) {
    if (!state) return false;
    SmEvent event;
    if (!drag_kind_to_sm_event(kind, &event)) return false;

    SmTransitionGuard sm_guard(state, SM_FAMILY_DRAG_DROP, event,
                               drag_transition_target(kind, args));
    {
        TransitionDepthScope transition_scope(state);
        switch (kind) {
            case DRAG_TRANSITION_SET_STATE:
                doc_state_set_drag_state(state, args ? args->target : NULL,
                                         args ? args->dragging : false);
                break;
            case DRAG_TRANSITION_BEGIN_DROP:
                if (!args) return false;
                if (!doc_state_begin_drag_drop(state, args->source, args->x, args->y, args->drag_data)) {
                    return false;
                }
                break;
            case DRAG_TRANSITION_UPDATE_DROP_MOTION:
                if (!args) return false;
                doc_state_update_drag_drop_motion(state, args->x, args->y);
                break;
            case DRAG_TRANSITION_SET_DROP_ACTIVE:
                doc_state_set_drag_drop_active(state, args ? args->active : false);
                break;
            case DRAG_TRANSITION_SET_DROP_TARGET:
                doc_state_set_drag_drop_target(state,
                    args ? args->drop_target : NULL,
                    args && args->has_drop_range ? &args->drop_start : NULL,
                    args && args->has_drop_range ? &args->drop_end : NULL);
                break;
            case DRAG_TRANSITION_CLEAR_DROP:
                doc_state_clear_drag_drop(state);
                break;
            default:
                return false;
        }
    }
    return finish_transition(state, &sm_guard, "drag_transition", false, false);
}

static void report_init(StateValidationReport* report) {
    if (!report) return;
    report->ok = true;
    report->failures = 0;
    report->message[0] = '\0';
}

static void report_fail(StateValidationReport* report, const char* msg) {
    if (!report) return;
    report->ok = false;
    report->failures++;
    if (report->message[0] == '\0' && msg) {
        snprintf(report->message, sizeof(report->message), "%s", msg);
    }
}

static bool same_view_position(View* a_view, int a_offset, View* b_view, int b_offset) {
    return a_view == b_view && a_offset == b_offset;
}

static DomNode* boundary_root(const DomBoundary* boundary) {
    if (!boundary || !boundary->node) return NULL;
    DomNode* root = boundary->node;
    while (root->parent) root = root->parent;
    return root;
}

static uint32_t projection_view_offset_limit(View* view);

static View* focus_validation_root(View* view) {
    View* root = view;
    while (root && root->parent) {
        root = static_cast<View*>(root->parent);
    }
    return root;
}

static bool focus_path_contains(View* focused, View* candidate) {
    View* node = focused;
    while (node) {
        if (node == candidate) return true;
        node = static_cast<View*>(node->parent);
    }
    return false;
}

static bool view_path_contains(View* target, View* candidate) {
    View* node = target;
    while (node) {
        if (node == candidate) return true;
        node = static_cast<View*>(node->parent);
    }
    return false;
}

static bool view_has_document_root(View* view) {
    if (!view) return false;
    View* root = focus_validation_root(view);
    return root != NULL;
}

static uint32_t projection_view_offset_limit(View* view) {
    if (!view) return 0;
    DomNode* node = static_cast<DomNode*>(view);
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        return dom_text_utf16_to_utf8(text, dom_text_utf16_length(text));
    }
    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        if (tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            if (elem->form) return elem->form->current_value_len;
        }
    }
    return dom_node_boundary_length(node);
}

static void validate_text_control_form_state(DocState* state, DomElement* elem,
                                             StateValidationReport* report) {
    if (!elem || !tc_is_text_control(elem)) return;
    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) {
        report_fail(report, "text control has no form state");
        return;
    }
    uint32_t selection_start = 0, selection_end = 0;
    uint8_t selection_direction = 0;
    form_control_get_selection(state, static_cast<View*>(elem), &selection_start, &selection_end, &selection_direction);
    if (selection_start > form->current_value_u16_len ||
        selection_end > form->current_value_u16_len) {
        report_fail(report, "text-control selection exceeds value length");
    }
    if (selection_start > selection_end) {
        report_fail(report, "text-control selection start exceeds end");
    }
    if (selection_direction > 2) {
        report_fail(report, "text-control selection direction is invalid");
    }
    if (form->preedit_len > 0 && form->preedit_caret > form->preedit_len) {
        report_fail(report, "text-control preedit caret exceeds preedit length");
    }
}

static void validate_transient_ui_target(View* view, const char* name,
                                         StateValidationReport* report) {
    if (!view) return;
    if (!view_has_document_root(view)) {
        report_fail(report, name);
    }
}

static View* find_live_view_by_id(DomNode* node, uint32_t view_id) {
    if (!node || view_id == 0) return NULL;
    View* view = static_cast<View*>(node);
    if (view->id == view_id) return view;
    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        DomNode* child = element->first_child;
        while (child) {
            View* found = find_live_view_by_id(child, view_id);
            if (found) return found;
            child = child->next_sibling;
        }
    }
    return NULL;
}

static View* find_doc_live_view_by_id(DocState* state, uint32_t view_id) {
    if (!state || !state->owner_store || !state->owner_store->document || view_id == 0) return NULL;
    DomDocument* doc = state->owner_store->document;
    DomNode* root = doc->root ? static_cast<DomNode*>(doc->root) : NULL;
    return find_live_view_by_id(root, view_id);
}

static void validate_focus_node(DocState* state, View* node, View* focused,
                                StateValidationReport* report,
                                uint32_t* focus_count) {
    if (!state || !node) return;

    bool expected_focus = node == focused;
    bool expected_within = focused && focus_path_contains(focused, node);
    bool expected_visible = expected_focus && state->focus && state->focus->focus_visible;

    bool store_focus = state_get_bool(state, node, STATE_FOCUS);
    bool store_within = state_get_bool(state, node, STATE_FOCUS_WITHIN);
    bool store_visible = state_get_bool(state, node, STATE_FOCUS_VISIBLE);

    if (store_focus) (*focus_count)++;
    if (store_focus != expected_focus) {
        report_fail(report, "focus pseudo-state does not match focus target");
    }
    if (store_within != expected_within) {
        report_fail(report, ":focus-within ancestry is inconsistent");
    }
    if (store_visible != expected_visible) {
        report_fail(report, ":focus-visible state is inconsistent");
    }

    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        DomNode* child = element->first_child;
        while (child) {
            validate_focus_node(state, static_cast<View*>(child), focused, report, focus_count);
            child = child->next_sibling;
        }
    }
}

static void validate_focus_invariants(DocState* state,
                                      StateValidationReport* report) {
    if (!state || !state->focus) return;

    View* focused = state->focus->current;
    if (focused && !view_has_document_root(focused)) {
        report_fail(report, "focused target is detached");
    }
    View* root = focus_validation_root(focused ? focused : state->focus->previous);
    if (!root) return;

    uint32_t focus_count = 0;
    validate_focus_node(state, root, focused, report, &focus_count);
    if (focused && focus_count != 1) {
        report_fail(report, "focused document does not have exactly one :focus target");
    }
    if (!focused && focus_count != 0) {
        report_fail(report, "inactive focus document still has :focus target");
    }
}

static void validate_hover_node(DocState* state, View* node, View* hovered,
                                StateValidationReport* report,
                                uint32_t* hover_count) {
    if (!state || !node) return;

    bool expected_hover = hovered && view_path_contains(hovered, node);
    bool store_hover = state_get_bool(state, node, STATE_HOVER);
    if (store_hover) (*hover_count)++;
    if (store_hover != expected_hover) {
        report_fail(report, ":hover ancestry chain is inconsistent");
    }

    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        DomNode* child = element->first_child;
        while (child) {
            validate_hover_node(state, static_cast<View*>(child), hovered, report, hover_count);
            child = child->next_sibling;
        }
    }
}

static void validate_hover_invariants(DocState* state,
                                      StateValidationReport* report) {
    if (!state) return;

    View* hovered = state->hover_target;
    if (hovered && !view_has_document_root(hovered)) {
        report_fail(report, "hover target is detached");
    }

    View* root = NULL;
    if (state->owner_store && state->owner_store->document) {
        DomDocument* doc = state->owner_store->document;
        root = doc->root ? static_cast<View*>(doc->root) : NULL;
    }
    if (!root) root = focus_validation_root(hovered);
    if (!root) return;

    uint32_t hover_count = 0;
    validate_hover_node(state, root, hovered, report, &hover_count);
    if (!hovered && hover_count != 0) {
        report_fail(report, "inactive hover document still has :hover target");
    }
}

static void validate_active_node(DocState* state, View* node, View* active,
                                 StateValidationReport* report,
                                 uint32_t* active_count) {
    if (!state || !node) return;

    bool expected_active = active && view_path_contains(active, node);
    bool store_active = state_get_bool(state, node, STATE_ACTIVE);
    if (store_active) (*active_count)++;
    if (store_active != expected_active) {
        report_fail(report, ":active ancestry chain is inconsistent");
    }

    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        DomNode* child = element->first_child;
        while (child) {
            validate_active_node(state, static_cast<View*>(child), active, report, active_count);
            child = child->next_sibling;
        }
    }
}

static void validate_active_invariants(DocState* state,
                                       StateValidationReport* report) {
    if (!state) return;

    View* active = state->active_target;
    if (active && !view_has_document_root(active)) {
        report_fail(report, "active target is detached");
    }

    View* root = NULL;
    if (state->owner_store && state->owner_store->document) {
        DomDocument* doc = state->owner_store->document;
        root = doc->root ? static_cast<View*>(doc->root) : NULL;
    }
    if (!root) root = focus_validation_root(active);
    if (!root) return;

    uint32_t active_count = 0;
    validate_active_node(state, root, active, report, &active_count);
    if (!active && active_count != 0) {
        report_fail(report, "inactive document still has :active target");
    }
}

static void validate_drag_invariants(DocState* state,
                                     StateValidationReport* report) {
    if (!state) return;

    if (state->is_dragging && !state->drag_target) {
        report_fail(report, "active drag has no drag target");
    }
    if (!state->is_dragging && state->drag_target) {
        report_fail(report, "inactive drag has stale drag target");
    }
    if (state->drag_target && !view_has_document_root(state->drag_target)) {
        report_fail(report, "drag target is detached");
    }

    DragDropState* drag_drop = state->drag_drop;
    if (!drag_drop) return;
    if (drag_drop->pending && drag_drop->active) {
        report_fail(report, "drag-drop is both pending and active");
    }
    if ((drag_drop->pending || drag_drop->active) && !drag_drop->source_view) {
        report_fail(report, "drag-drop session has no source view");
    }
    if (drag_drop->source_view && !view_has_document_root(drag_drop->source_view)) {
        report_fail(report, "drag-drop source is detached");
    }
    if (drag_drop->drop_target && !view_has_document_root(drag_drop->drop_target)) {
        report_fail(report, "drag-drop target is detached");
    }
    if (drag_drop->drop_target && !drag_drop->active) {
        report_fail(report, "inactive drag-drop has drop target");
    }
}

static bool editing_surface_matches(const EditingSurface* a,
                                    const EditingSurface* b) {
    if (!a || !b) return false;
    return a->kind == b->kind &&
        a->mode == b->mode &&
        a->owner == b->owner &&
        a->view == b->view;
}

static void validate_editing_interaction_invariants(DocState* state,
                                                    StateValidationReport* report) {
    if (!state) return;

    const EditingInteractionState* editing = &state->editing;
    if (!editing->has_active_surface &&
        editing->active_surface.kind != EDIT_SURFACE_NONE) {
        report_fail(report, "inactive editing surface has stale kind");
    }
    if (!editing->has_active_surface && editing->composing) {
        report_fail(report, "editing composition has no active surface");
    }

    if (editing->has_active_surface) {
        const EditingSurface* surface = &editing->active_surface;
        if (surface->kind == EDIT_SURFACE_NONE) {
            report_fail(report, "active editing surface has none kind");
        }
        if (!surface->owner) {
            report_fail(report, "active editing surface has no owner");
        } else if (!view_has_document_root(static_cast<View*>(surface->owner))) {
            report_fail(report, "active editing surface owner is detached");
        }
        if (!surface->view) {
            report_fail(report, "active editing surface has no target");
        } else if (!view_has_document_root(surface->view)) {
            report_fail(report, "active editing surface target is detached");
        } else {
            EditingSurface resolved;
            if (!editing_surface_from_target(surface->view, &resolved)) {
                report_fail(report, "active editing surface no longer resolves");
            } else if (!editing_surface_matches(surface, &resolved)) {
                report_fail(report, "active editing surface projection is stale");
            }
        }
    }
    if (editing->rich_transaction_phase == EDITING_RICH_TX_IDLE) {
        if (editing->rich_transaction_target) {
            report_fail(report, "idle rich edit transaction has stale target");
        }
    } else {
        if (!editing->rich_transaction_target) {
            report_fail(report, "active rich edit transaction has no target");
        } else if (!view_has_document_root(editing->rich_transaction_target)) {
            report_fail(report, "active rich edit transaction target is detached");
        }
    }

    if (editing->autoscroll.active != state->editing_autoscroll_active ||
        editing->autoscroll.surface != state->editing_autoscroll_surface ||
        editing->autoscroll.pointer_x != state->editing_autoscroll_pointer_x ||
        editing->autoscroll.pointer_y != state->editing_autoscroll_pointer_y ||
        editing->autoscroll.tick_last_time != state->editing_tick_last_time ||
        editing->autoscroll.caret_blink_elapsed != state->editing_caret_blink_elapsed) {
        report_fail(report, "editing autoscroll projection is stale");
    }
    if (editing->autoscroll.active && !editing->autoscroll.surface) {
        report_fail(report, "active editing autoscroll has no surface");
    }
    if (editing->autoscroll.surface &&
        !view_has_document_root(editing->autoscroll.surface)) {
        report_fail(report, "editing autoscroll surface is detached");
    }
    if (!editing->autoscroll.active &&
        (editing->autoscroll.pointer_x != 0.0f ||
         editing->autoscroll.pointer_y != 0.0f)) {
        report_fail(report, "inactive editing autoscroll has stale pointer");
    }

    // Pointer tracking starts at a collapsed press; a non-collapsed active range
    // is derived only after motion, so the two states must not be equated.
    if (editing->pointer_selecting && !editing->drag_anchor_view) {
        report_fail(report, "active editing pointer selection has no anchor");
    }
    if (!editing->pointer_selecting && !state->text_selection_press_in_range &&
        editing->drag_anchor_view) {
        report_fail(report, "inactive editing pointer selection has stale anchor");
    }
    if (editing->drag_anchor_view &&
        !view_has_document_root(editing->drag_anchor_view)) {
        report_fail(report, "editing drag anchor is detached");
    }
    if (editing->drag_anchor_offset < 0) {
        report_fail(report, "editing drag anchor offset is negative");
    }
    if (editing->drag_anchor_view &&
        (uint32_t)editing->drag_anchor_offset > projection_view_offset_limit(editing->drag_anchor_view)) {
        report_fail(report, "editing drag anchor offset exceeds target length");
    }
}

static void validate_selection_invariants(DocState* state,
                                          StateValidationReport* report) {
    if (!state) return;
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        if (!state_store_editing_selection_shadow_matches(state)) {
            report_fail(report, "editing selection shadow disagrees with text control selection");
        }
        DomElement* control = state->sel.control;
        if (!control || !tc_is_text_control(control)) {
            report_fail(report, "text control selection has invalid control");
            return;
        }
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (!form) {
            report_fail(report, "text control selection has no form state");
            return;
        }
        if (state->sel.start_u16 > state->sel.end_u16 ||
            state->sel.end_u16 > form->current_value_u16_len) {
            report_fail(report, "text control selection offsets are out of range");
        }
        DomSelectionDirection expected_direction =
            state->sel.start_u16 == state->sel.end_u16 ? DOM_SEL_DIR_NONE :
            (form->selection_direction == 2 ? DOM_SEL_DIR_BACKWARD : DOM_SEL_DIR_FORWARD);
        if (state->sel.direction != expected_direction) {
            report_fail(report, "text control selection direction is inconsistent");
        }

        return;
    }

    if (!state->dom_selection) {
        if (!state_store_editing_selection_shadow_matches(state)) {
            report_fail(report, "editing selection shadow disagrees with empty DOM selection");
        }
        return;
    }

    DomSelection* selection = state->dom_selection;
    if (!state_store_editing_selection_shadow_matches(state)) {
        report_fail(report, "editing selection shadow disagrees with DOM selection");
    }
    if (selection->range_count > 1) {
        report_fail(report, "DOM selection has more than one range");
        return;
    }

    if (selection->range_count == 0) {
        if (!dom_selection_is_collapsed(selection) || selection->direction != DOM_SEL_DIR_NONE) {
            report_fail(report, "empty DOM selection has stale scalar state");
        }
        return;
    }

    DomRange* range = selection->ranges[0];
    if (!range) {
        report_fail(report, "DOM selection range slot is empty");
        return;
    }

    DomBoundary anchor = dom_selection_anchor_boundary(selection);
    DomBoundary focus = dom_selection_focus_boundary(selection);
    if (!dom_boundary_is_valid(&anchor) ||
        !dom_boundary_is_valid(&focus) ||
        !dom_boundary_is_valid(&range->start) ||
        !dom_boundary_is_valid(&range->end)) {
        report_fail(report, "DOM selection contains invalid boundary");
    }

    DomNode* anchor_root = boundary_root(&anchor);
    DomNode* focus_root = boundary_root(&focus);
    DomNode* start_root = boundary_root(&range->start);
    DomNode* end_root = boundary_root(&range->end);
    if (!anchor_root || !focus_root || !start_root || !end_root ||
        anchor_root != focus_root || anchor_root != start_root || anchor_root != end_root) {
        report_fail(report, "DOM selection endpoints are in incompatible roots");
    }

    bool range_collapsed = dom_range_collapsed(range);
    bool selection_collapsed = dom_selection_is_collapsed(selection);
    if (selection_collapsed != range_collapsed) {
        report_fail(report, "DOM selection collapsed flag disagrees with range");
    }
    if (selection_collapsed) {
        if (selection->direction != DOM_SEL_DIR_NONE) {
            report_fail(report, "collapsed DOM selection has non-none direction");
        }
        if (dom_boundary_compare(&anchor, &focus) != DOM_BOUNDARY_EQUAL) {
            report_fail(report, "collapsed DOM selection endpoints differ");
        }
    } else {
        DomBoundaryOrder anchor_focus = dom_boundary_compare(&anchor, &focus);
        if (selection->direction == DOM_SEL_DIR_NONE) {
            report_fail(report, "non-collapsed DOM selection has no direction");
        } else if (selection->direction == DOM_SEL_DIR_FORWARD && anchor_focus == DOM_BOUNDARY_AFTER) {
            report_fail(report, "forward DOM selection direction is inconsistent");
        } else if (selection->direction == DOM_SEL_DIR_BACKWARD && anchor_focus == DOM_BOUNDARY_BEFORE) {
            report_fail(report, "backward DOM selection direction is inconsistent");
        }
    }

    if (state->editing.pointer_selecting && selection->range_count == 0) {
        report_fail(report, "active selection has no DOM range");
    }
}

static void validate_view_state_registry(DocState* state,
                                         StateValidationReport* report) {
    if (!state || !state->view_state_map) return;

    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(state->view_state_map, &iter, &item)) {
        ViewStateEntry* entry = (ViewStateEntry*)item;
        if (!entry || !entry->state) {
            report_fail(report, "view state registry has empty entry");
            continue;
        }

        ViewState* view_state = entry->state;
        if (entry->view_id == 0 || view_state->view_id == 0 || entry->view_id != view_state->view_id) {
            report_fail(report, "view state registry id is inconsistent");
        }
        View* live_view = find_doc_live_view_by_id(state, view_state->view_id);
        if (state->owner_store && state->owner_store->document) {
            if (!live_view) {
                report_fail(report, "view state registry id has no live view");
            } else if (live_view->view_state_ref && live_view->view_state_ref->view_id != view_state->view_id) {
                report_fail(report, "live view weak ViewState ref is stale");
            }
        }
        switch (view_state->kind) {
            case VIEW_STATE_BASE:
                break;
            case VIEW_STATE_SCROLL:
                if (view_state->data.scroll.max_x < 0.0f || view_state->data.scroll.max_y < 0.0f) {
                    report_fail(report, "view scroll state has negative max");
                }
                if (view_state->data.scroll.x < 0.0f || view_state->data.scroll.y < 0.0f ||
                    view_state->data.scroll.x > view_state->data.scroll.max_x ||
                    view_state->data.scroll.y > view_state->data.scroll.max_y) {
                    report_fail(report, "view scroll state is out of bounds");
                }
                if (live_view && live_view->is_element()) {
                    DomElement* live_element = lam::dom_require_element(live_view);
                    if (live_element && live_element->form &&
                        live_element->form->scroll_state_ref &&
                        live_element->form->scroll_state_ref != view_state) {
                        report_fail(report, "form control cached scroll ViewState ref is stale");
                    }
                }
                break;
            case VIEW_STATE_FORM_CONTROL: {
                if (view_state->data.form.selected_index < -1 || view_state->data.form.hover_index < -1) {
                    report_fail(report, "view form state has invalid index");
                }
                if (live_view) {
                    DomElement* live_element = live_view->is_element() ? lam::dom_require_element(live_view) : NULL;
                    if (!live_element || !live_element->form) {
                        report_fail(report, "view form state is not attached to a form control");
                    } else {
                        FormControlProp* form = live_element->form;
                        if (form->form_state_ref && form->form_state_ref != view_state) {
                            report_fail(report, "form control cached ViewState ref is stale");
                        }
                        if (form->option_count >= 0) {
                            if (view_state->data.form.selected_index >= form->option_count) {
                                report_fail(report, "view form state selected index exceeds option count");
                            }
                            if (view_state->data.form.hover_index >= form->option_count) {
                                report_fail(report, "view form state hover index exceeds option count");
                            }
                        }
                    }
                }
                if (view_state->data.form.selection_start > view_state->data.form.selection_end) {
                    report_fail(report, "view form state selection start exceeds end");
                }
                if (view_state->data.form.selection_direction > 3) {
                    report_fail(report, "view form state selection direction is invalid");
                }
                if (view_state->data.form.has_current_value &&
                    view_state->data.form.selection_end > view_state->data.form.current_value_u16_len) {
                    report_fail(report, "view form state selection exceeds text value");
                }
                if (view_state->data.form.range_value < 0.0f || view_state->data.form.range_value > 1.0f) {
                    report_fail(report, "view form state range value is out of bounds");
                }
                break;
            }
            case VIEW_STATE_CUSTOM:
                break;
            default:
                report_fail(report, "view state kind is invalid");
                break;
        }
    }
}

static void validate_focused_target_state(DocState* state,
                                          StateValidationReport* report) {
    if (!state || !state->focus || !state->focus->current) return;
    if (!is_view_programmatically_focusable(state->focus->current)) {
        report_fail(report, "focused target is not focusable");
    }
    if (state->focus->current->is_element()) {
        validate_text_control_form_state(state, lam::dom_require_element(state->focus->current), report);
    }
}

static void validate_caret_projection_state(DocState* state,
                                            StateValidationReport* report) {
    if (!state) return;
    View* view = NULL;
    int offset = 0;
    bool visible = caret_is_visible(state);
    bool has_caret = caret_get_position(state, &view, &offset);
    if (visible && !has_caret) {
        report_fail(report, "visible caret has no target view");
    }
    if (!has_caret) return;
    if (offset < 0) {
        report_fail(report, "caret offset is negative");
    }
    if ((uint32_t)offset > projection_view_offset_limit(view)) {
        report_fail(report, "caret offset exceeds target length");
    }
    bool composition_active = state->editing.composition.active;
    if (!composition_active && visible && view->is_element() &&
        state->focus && state->focus->current &&
        view != state->focus->current) {
        report_fail(report, "caret target differs from focus target");
    }
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (!tc_is_text_control(elem) && visible) {
            report_fail(report, "visible element caret target is not editable");
        }
    }
}

static void validate_selection_projection_state(DocState* state,
                                                StateValidationReport* report) {
    if (!state) return;
    View* anchor_view = NULL;
    View* focus_view = NULL;
    int anchor_offset = 0;
    int focus_offset = 0;
    bool collapsed = true;
    if (!selection_get_anchor_snapshot(state, &anchor_view, &anchor_offset, &collapsed)) return;
    selection_get_focus_snapshot(state, &focus_view, &focus_offset, NULL, NULL, NULL);
    if (anchor_offset < 0 || focus_offset < 0) {
        report_fail(report, "selection offset is negative");
    }
    if (anchor_view && (uint32_t)anchor_offset > projection_view_offset_limit(anchor_view)) {
        report_fail(report, "selection anchor offset exceeds target length");
    }
    if (focus_view && (uint32_t)focus_offset > projection_view_offset_limit(focus_view)) {
        report_fail(report, "selection focus offset exceeds target length");
    }
    if (!collapsed && (!anchor_view || !focus_view)) {
        report_fail(report, "non-collapsed selection has missing endpoints");
    }
    if (collapsed && anchor_view && focus_view &&
        !same_view_position(anchor_view, anchor_offset, focus_view, focus_offset)) {
        report_fail(report, "collapsed selection endpoints differ");
    }
    if (state->editing.pointer_selecting && !anchor_view) {
        report_fail(report, "active selection has no anchor");
    }
}

static bool editing_phase_has_mutation(EditingRichTransactionPhase phase) {
    switch (phase) {
        case EDITING_RICH_TX_MUTATED:
        case EDITING_RICH_TX_SELECTION_SET:
        case EDITING_RICH_TX_INPUT:
            return true;
        default:
            return false;
    }
}

static bool editing_phase_matches_premutation_selection(
        EditingRichTransactionPhase phase) {
    return phase == EDITING_RICH_TX_OPEN ||
        phase == EDITING_RICH_TX_BEFOREINPUT;
}

static const EditingSurface* editing_validation_surface(
        DocState* state,
        EditingSurface* resolved) {
    if (!state) return NULL;
    const EditingInteractionState* editing = &state->editing;
    if (resolved &&
        editing->rich_transaction_phase != EDITING_RICH_TX_IDLE &&
        editing->rich_transaction_target &&
        editing_surface_from_target(editing->rich_transaction_target, resolved)) {
        return resolved;
    }
    if (editing->has_active_surface &&
        editing->active_surface.kind != EDIT_SURFACE_NONE) {
        return &editing->active_surface;
    }
    if (resolved && editing->rich_transaction_target &&
        editing_surface_from_target(editing->rich_transaction_target, resolved)) {
        return resolved;
    }
    return NULL;
}

static bool editing_surface_contains_dom_boundary_for_validation(
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

static bool editing_boundary_in_false_island(const EditingSurface* surface,
                                             const DomBoundary* boundary) {
    if (!surface || !editing_surface_is_rich(surface) ||
        !surface->owner || !boundary || !boundary->node) {
        return false;
    }
    EditingHost host;
    if (!editing_host_lookup(boundary->node, &host)) return false;
    return host.host == surface->owner && host.target_in_false_island;
}

static bool editing_target_range_same(const EditingTargetRange* a,
                                      const EditingTargetRange* b) {
    if (!a || !b) return false;
    return a->start.node == b->start.node &&
        a->start.offset == b->start.offset &&
        a->end.node == b->end.node &&
        a->end.offset == b->end.offset;
}

static bool editing_surfaces_focus_compatible(const EditingSurface* active,
                                              const EditingSurface* focused) {
    if (!active || !focused) return false;
    if (editing_surface_is_rich(active) && editing_surface_is_rich(focused)) {
        return active->kind == focused->kind &&
            active->mode == focused->mode &&
            active->owner == focused->owner;
    }
    return editing_surface_matches(active, focused);
}

static void validate_editing_surface_invariant(DocState* state,
                                               StateValidationReport* report) {
    if (!state) return;
    const EditingInteractionState* editing = &state->editing;
    if (!editing->has_active_surface) {
        if (editing->active_surface.kind != EDIT_SURFACE_NONE) {
            report_fail(report, "SM_INV_EDITING_SURFACE: inactive surface has stale kind");
        }
        return;
    }

    const EditingSurface* surface = &editing->active_surface;
    bool rich_context = editing_surface_is_rich(surface) ||
        editing->rich_transaction_phase != EDITING_RICH_TX_IDLE;
    if (!rich_context) return;
    if (editing->rich_transaction_phase != EDITING_RICH_TX_IDLE &&
        !editing_surface_is_rich(surface)) {
        report_fail(report, "SM_INV_EDITING_SURFACE: rich transaction surface is not rich");
    }
    if (surface->kind == EDIT_SURFACE_NONE) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface has none kind");
    }
    if (surface->readonly) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface is read-only");
    }
    if (surface->disabled) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface is disabled");
    }
    if (!surface->owner) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface has no owner");
    } else if (!view_has_document_root(static_cast<View*>(surface->owner))) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface owner is detached");
    }
    if (!surface->view) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface has no target");
    } else if (!view_has_document_root(surface->view)) {
        report_fail(report, "SM_INV_EDITING_SURFACE: active surface target is detached");
    } else {
        EditingSurface resolved;
        if (!editing_surface_from_target(surface->view, &resolved)) {
            report_fail(report, "SM_INV_EDITING_SURFACE: active surface no longer resolves");
        } else if (!editing_surface_matches(surface, &resolved)) {
            report_fail(report, "SM_INV_EDITING_SURFACE: active surface projection is stale");
        }
    }

    View* focused = focus_get(state);
    if (focused) {
        EditingSurface focus_surface;
        if (editing_surface_from_target(focused, &focus_surface) &&
            !editing_surfaces_focus_compatible(surface, &focus_surface)) {
            report_fail(report, "SM_INV_EDITING_SURFACE: active surface disagrees with focus");
        }
    }
}

static void validate_editing_selection_host_invariant(
        DocState* state,
        StateValidationReport* report) {
    if (!state ||
        state->editing.rich_transaction_phase == EDITING_RICH_TX_IDLE) {
        return;
    }
    EditingSurface resolved;
    const EditingSurface* surface = editing_validation_surface(state, &resolved);
    if (!editing_surface_is_rich(surface)) return;

    DomSelection* selection = state->dom_selection;
    if (!selection || selection->range_count == 0) return;

    DomBoundary anchor = dom_selection_anchor_boundary(selection);
    DomBoundary focus = dom_selection_focus_boundary(selection);
    if (!editing_surface_contains_dom_boundary_for_validation(surface, &anchor) ||
        !editing_surface_contains_dom_boundary_for_validation(surface, &focus)) {
        report_fail(report, "SM_INV_EDITING_SELECTION_HOST: selection endpoint outside active host");
    }
}

static void validate_editing_false_island_invariant(
        DocState* state,
        StateValidationReport* report) {
    if (!state) return;
    const EditingInteractionState* editing = &state->editing;
    if (!editing_phase_has_mutation(editing->rich_transaction_phase)) return;

    EditingSurface resolved;
    const EditingSurface* surface = editing_validation_surface(state, &resolved);
    if (!editing_surface_is_rich(surface)) return;

    if (surface->target_in_false_island) {
        report_fail(report, "SM_INV_EDITING_FALSE_ISLAND: mutation target is inside false island");
    }
    if (!editing->rich_transaction_target_ranges_active) return;
    for (uint32_t i = 0; i < editing->rich_transaction_target_range_count && i < 4; i++) {
        const EditingTargetRangeSnapshot* range =
            &editing->rich_transaction_target_ranges[i];
        if (editing_boundary_in_false_island(surface, &range->start) ||
            editing_boundary_in_false_island(surface, &range->end)) {
            report_fail(report, "SM_INV_EDITING_FALSE_ISLAND: target range enters false island");
            return;
        }
    }
}

static void validate_editing_target_ranges_invariant(
        DocState* state,
        StateValidationReport* report) {
    if (!state) return;
    const EditingInteractionState* editing = &state->editing;
    // Stage 4B: while the substrate is synchronously dispatching `beforeinput`
    // into a script handler, the script may reconcile (split/merge/replace) the
    // editable subtree, transiently destroying the surface the native rich
    // transaction references. The transaction is inert during that window (the
    // script preventDefaults) and re-syncs to the post-reconcile selection once
    // dispatch returns, so suspend the target-range invariant here.
    if (editing->rich_transaction_in_script_dispatch) {
        return;
    }
    if (editing->rich_transaction_phase == EDITING_RICH_TX_IDLE) {
        if (editing->rich_transaction_target_ranges_active ||
            editing->rich_transaction_target_range_count != 0 ||
            editing->rich_transaction_target_ranges_required ||
            editing->rich_transaction_input_type != (uint32_t)INPUT_INTENT_NONE) {
            report_fail(report, "SM_INV_EDITING_TARGET_RANGES: idle transaction has stale target ranges");
        }
        return;
    }

    if (!editing->rich_transaction_target_ranges_active) {
        report_fail(report, "SM_INV_EDITING_TARGET_RANGES: active transaction has no target-range snapshot");
        return;
    }
    if (editing->rich_transaction_target_range_count > 4) {
        report_fail(report, "SM_INV_EDITING_TARGET_RANGES: target range count exceeds snapshot capacity");
        return;
    }
    if (!editing->rich_transaction_target_ranges_valid &&
        editing_phase_has_mutation(editing->rich_transaction_phase)) {
        report_fail(report, "SM_INV_EDITING_TARGET_RANGES: invalid target range reached mutation phase");
        return;
    }

    EditingSurface resolved;
    const EditingSurface* surface = editing_validation_surface(state, &resolved);
    if (!surface || surface->kind == EDIT_SURFACE_NONE) {
        report_fail(report, "SM_INV_EDITING_TARGET_RANGES: active transaction has no surface");
        return;
    }

    bool selection_unchanged = state->selection_mutation_seq ==
        editing->rich_transaction_selection_seq;
    if (editing->rich_transaction_target_ranges_valid &&
        selection_unchanged) {
        for (uint32_t i = 0; i < editing->rich_transaction_target_range_count; i++) {
            EditingTargetRange range;
            range.start = editing->rich_transaction_target_ranges[i].start;
            range.end = editing->rich_transaction_target_ranges[i].end;
            if (!editing_geometry_surface_contains_target_range(surface, &range)) {
                report_fail(report, "SM_INV_EDITING_TARGET_RANGES: target range is outside active surface");
                return;
            }
        }
    }

    if (!editing->rich_transaction_target_ranges_required ||
        !editing->rich_transaction_target_ranges_valid ||
        !editing_phase_matches_premutation_selection(
            editing->rich_transaction_phase) ||
        !selection_unchanged) {
        return;
    }

    InputIntent intent;
    intent.type = (InputIntentType)editing->rich_transaction_input_type;
    EditingTargetRange recomputed[4];
    memset(recomputed, 0, sizeof(recomputed));
    uint32_t count = editing_compute_target_ranges(state, surface, &intent,
                                                   recomputed, 4);
    if (count != editing->rich_transaction_target_range_count) {
        report_fail(report, "SM_INV_EDITING_TARGET_RANGES: target range count changed before mutation");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        EditingTargetRange snapshot;
        snapshot.start = editing->rich_transaction_target_ranges[i].start;
        snapshot.end = editing->rich_transaction_target_ranges[i].end;
        if (!editing_target_range_same(&recomputed[i],
                &snapshot)) {
            report_fail(report, "SM_INV_EDITING_TARGET_RANGES: target range changed before mutation");
            return;
        }
    }
}

static void validate_dom_selection_cache_invariant(DocState* state,
                                                   StateValidationReport* report) {
    if (!state) return;
    if (!state_store_editing_selection_shadow_matches(state)) {
        report_fail(report, "SM_INV_DOM_SELECTION_CACHE: selection shadow is stale");
    }
}

static void validate_selection_projection_cache_invariant(
        DocState* state,
        StateValidationReport* report) {
    validate_caret_projection_state(state, report);
    validate_selection_projection_state(state, report);
}

static void validate_input_event_order_invariant(DocState* state,
                                                 StateValidationReport* report) {
    if (!state) return;
    const EditingInteractionState* editing = &state->editing;
    switch (editing->rich_transaction_phase) {
        case EDITING_RICH_TX_IDLE:
        case EDITING_RICH_TX_OPEN:
        case EDITING_RICH_TX_BEFOREINPUT:
        case EDITING_RICH_TX_MUTATED:
        case EDITING_RICH_TX_SELECTION_SET:
        case EDITING_RICH_TX_INPUT:
            break;
        default:
            report_fail(report, "SM_INV_INPUT_EVENT_ORDER: invalid rich transaction phase");
            return;
    }
    if (editing->rich_transaction_phase != EDITING_RICH_TX_IDLE &&
        !editing->rich_transaction_target) {
        report_fail(report, "SM_INV_INPUT_EVENT_ORDER: active transaction has no target");
    }
    if (editing_phase_has_mutation(editing->rich_transaction_phase) &&
        editing->rich_transaction_target_ranges_required &&
        !editing->rich_transaction_target_ranges_valid) {
        report_fail(report, "SM_INV_INPUT_EVENT_ORDER: mutation followed invalid target ranges");
    }
}

static void validate_text_control_focus_state(DocState* state,
                                              StateValidationReport* report) {
    if (!state) return;
    DomElement* active_text_control = tc_get_active_element(state);
    View* focused = state->focus ? state->focus->current : NULL;
    DomElement* focused_element = focused && focused->is_element() ? lam::dom_require_element(focused) : NULL;
    if (focused_element && tc_is_text_control(focused_element)) {
        if (active_text_control && active_text_control != focused_element) {
            report_fail(report, "active text control differs from focus target");
        }
    } else if (active_text_control) {
        report_fail(report, "active text control exists without text-control focus");
    }
}

static void validate_dropdown_overlay_state(DocState* state,
                                            StateValidationReport* report) {
    if (!state || !state->open_dropdown) return;
    validate_transient_ui_target(state->open_dropdown, "open dropdown target is detached", report);
    if (!state->open_dropdown->is_element()) {
        report_fail(report, "open dropdown target is not an element");
    } else {
        DomElement* elem = lam::dom_require_element(state->open_dropdown);
        if (!elem->form || !form_control_is_dropdown_open(state, state->open_dropdown)) {
            report_fail(report, "open dropdown state disagrees with form control");
        }
    }
    if (state->dropdown_width < 0 || state->dropdown_height < 0) {
        report_fail(report, "open dropdown has negative dimensions");
    }
}

static void validate_context_menu_overlay_state(DocState* state,
                                                StateValidationReport* report) {
    if (!state || !state->context_menu_target) return;
    validate_transient_ui_target(state->context_menu_target, "context menu target is detached", report);
    if (!state->context_menu_target->is_element() ||
        !tc_is_text_control(lam::dom_require_element(state->context_menu_target))) {
        report_fail(report, "context menu target is not a text control");
    }
    if (state->context_menu_width < 0 || state->context_menu_height < 0) {
        report_fail(report, "context menu has negative dimensions");
    }
    if (state->context_menu_hover < -1) {
        report_fail(report, "context menu hover index is invalid");
    }
}

static void validate_dirty_tracking_state(DocState* state,
                                          StateValidationReport* report) {
    if (!state) return;
    if ((state->selection_layout_dirty || state->dirty_tracker.full_repaint ||
         state->dirty_tracker.full_reflow || dirty_has_regions(&state->dirty_tracker)) &&
        !state->needs_repaint && !state->needs_reflow) {
        report_fail(report, "dirty tracking is set without repaint or reflow flag");
    }
}

static void validate_schema_invariant_primitive(DocState* state,
                                                SmInvariantId invariant,
                                                StateValidationReport* report) {
    switch (invariant) {
        case SM_INV_FOCUSED_TARGET:
            validate_focused_target_state(state, report);
            break;
        case SM_INV_FOCUS_GRAPH:
            validate_focus_invariants(state, report);
            break;
        case SM_INV_HOVER_GRAPH:
            validate_hover_invariants(state, report);
            break;
        case SM_INV_ACTIVE_GRAPH:
            validate_active_invariants(state, report);
            break;
        case SM_INV_DRAG_GRAPH:
            validate_drag_invariants(state, report);
            break;
        case SM_INV_EDITING_INTERACTION:
            validate_editing_interaction_invariants(state, report);
            break;
        case SM_INV_VIEW_STATE_REGISTRY:
            validate_view_state_registry(state, report);
            break;
        case SM_INV_CARET_PROJECTION:
            validate_caret_projection_state(state, report);
            break;
        case SM_INV_SELECTION_PROJECTION:
            validate_selection_projection_state(state, report);
            break;
        case SM_INV_TEXT_CONTROL_FOCUS:
            validate_text_control_focus_state(state, report);
            break;
        case SM_INV_DROPDOWN_OVERLAY:
            validate_dropdown_overlay_state(state, report);
            break;
        case SM_INV_CONTEXT_MENU_OVERLAY:
            validate_context_menu_overlay_state(state, report);
            break;
        case SM_INV_DIRTY_TRACKING:
            validate_dirty_tracking_state(state, report);
            break;
        case SM_INV_DOM_SELECTION:
            validate_selection_invariants(state, report);
            break;
        case SM_INV_EDITING_SURFACE:
            validate_editing_surface_invariant(state, report);
            break;
        case SM_INV_EDITING_SELECTION_HOST:
            validate_editing_selection_host_invariant(state, report);
            break;
        case SM_INV_EDITING_FALSE_ISLAND:
            validate_editing_false_island_invariant(state, report);
            break;
        case SM_INV_EDITING_TARGET_RANGES:
            validate_editing_target_ranges_invariant(state, report);
            break;
        case SM_INV_DOM_SELECTION_CACHE:
            validate_dom_selection_cache_invariant(state, report);
            break;
        case SM_INV_SELECTION_PROJECTION_CACHE:
            validate_selection_projection_cache_invariant(state, report);
            break;
        case SM_INV_INPUT_EVENT_ORDER:
            validate_input_event_order_invariant(state, report);
            break;
        default:
            break;
    }
}

static bool schema_invariant_binding_applies(DocState* state,
                                             const StateInvariantBinding* binding) {
    if (!binding) return false;
    if (binding->state == SM_STATE_ANY) return true;
    return sm_derive_state(state, binding->family, NULL) == binding->state;
}

static bool radiant_state_validate_interaction_schema(DocState* state,
                                                      StateValidationReport* report) {
    report_init(report);
    if (!state) return true;
    selection_refresh_presentation(state);
    for (uint32_t i = 0; i < RADIANT_INVARIANT_COUNT; i++) {
        const StateInvariantBinding* binding = &RADIANT_INVARIANTS[i];
        if (!schema_invariant_binding_applies(state, binding)) continue;
        validate_schema_invariant_primitive(state, binding->invariant, report);
    }
    return !report || report->ok;
}

bool radiant_state_validate_interaction(DocState* state,
                                        StateValidationReport* report) {
    return radiant_state_validate_interaction_schema(state, report);
}

void radiant_state_assert_valid(DocState* state, const char* context) {
#ifndef NDEBUG
    view_state_prune_orphans(state);
    StateValidationReport report;
    bool ok = radiant_state_validate_interaction(state, &report);
    if (!ok) {
        log_error("state_machine: assertion failed after %s: %s",
            context ? context : "state mutation",
            report.message[0] ? report.message : "interaction invariant failed");
    }
    assert(ok && "DocState interaction invariant failed");
#else
    (void)state;
    (void)context;
#endif
}

uint64_t state_begin_event_cascade(DocState* state,
                                   EventStateLog* log,
                                   const char* cause) {
    if (state) {
        state->active_event_log = log;
        state->active_cascade_id = event_state_log_enabled(log) ?
            event_state_log_begin_cascade(log, cause ? cause : "internal") : 0;
        state->active_cascade_depth++;
        state_begin_batch(state);
        return state->active_cascade_id;
    }
    return event_state_log_begin_cascade(log, cause ? cause : "internal");
}

static void emit_validation_record(EventStateLog* log, uint64_t cascade_id,
                                   const StateValidationReport* report) {
    if (!event_state_log_enabled(log)) return;

    char buf[STATE_MACHINE_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
        report && report->ok ? "state.validated" : "state.invalid", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_bool(&w, "ok", report ? report->ok : false);
        jw_kv_uint(&w, "failures", report ? report->failures : 1);
        if (report && report->message[0]) {
            jw_kv_str(&w, "message", report->message);
        }
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

static void write_optional_view_ref(JsonWriter* w, const char* key, View* view) {
    event_state_log_write_node_ref(w, key, (const DomNode*)view);
}

static const char* editing_drag_mode_name(EditingDragMode mode) {
    switch (mode) {
        case EDITING_DRAG_CHAR: return "char";
        case EDITING_DRAG_WORD: return "word";
        case EDITING_DRAG_LINE: return "line";
        default: return "unknown";
    }
}

static const char* editing_rich_transaction_phase_name(
        EditingRichTransactionPhase phase) {
    switch (phase) {
        case EDITING_RICH_TX_IDLE: return "idle";
        case EDITING_RICH_TX_OPEN: return "open";
        case EDITING_RICH_TX_BEFOREINPUT: return "beforeinput";
        case EDITING_RICH_TX_MUTATED: return "mutated";
        case EDITING_RICH_TX_SELECTION_SET: return "selection_set";
        case EDITING_RICH_TX_INPUT: return "input";
        default: return "unknown";
    }
}

static void write_editing_surface_ref(JsonWriter* w,
                                      const EditingSurface* surface) {
    if (!surface || surface->kind == EDIT_SURFACE_NONE) {
        jw_null(w);
        return;
    }
    jw_obj_begin(w);
        editing_log_write_surface_core_fields(w, surface, false);
    jw_obj_end(w);
}

static void write_editing_interaction_snapshot(JsonWriter* w,
                                               const DocState* state) {
    jw_key(w, "editing");
    jw_obj_begin(w);
        const EditingInteractionState* editing = &state->editing;
        jw_kv_bool(w, "has_active_surface", editing->has_active_surface);
        jw_key(w, "active_surface");
        write_editing_surface_ref(w, editing->has_active_surface
            ? &editing->active_surface : NULL);
        jw_kv_bool(w, "pointer_selecting", editing->pointer_selecting);
        jw_kv_str(w, "drag_mode",
                  editing_drag_mode_name(editing->drag_mode));
        jw_key(w, "drag_anchor");
        if (editing->drag_anchor_view) {
            jw_obj_begin(w);
                write_optional_view_ref(w, "target",
                                        editing->drag_anchor_view);
                jw_kv_int(w, "offset", editing->drag_anchor_offset);
            jw_obj_end(w);
        } else {
            jw_null(w);
        }
        jw_kv_bool(w, "composing", editing->composing);
        jw_kv_str(w, "rich_transaction_phase",
                  editing_rich_transaction_phase_name(
                      editing->rich_transaction_phase));
        write_optional_view_ref(w, "rich_transaction_target",
                                editing->rich_transaction_target);
        jw_key(w, "rich_transaction_target_ranges");
        jw_obj_begin(w);
            jw_kv_bool(w, "active",
                       editing->rich_transaction_target_ranges_active);
            jw_kv_bool(w, "required",
                       editing->rich_transaction_target_ranges_required);
            jw_kv_bool(w, "valid",
                       editing->rich_transaction_target_ranges_valid);
            jw_kv_str(w, "input_type",
                      input_intent_type_name(
                          (InputIntentType)
                          editing->rich_transaction_input_type));
            jw_kv_uint(w, "selection_seq",
                       editing->rich_transaction_selection_seq);
            jw_kv_uint(w, "count",
                       editing->rich_transaction_target_range_count);
        jw_obj_end(w);
        jw_key(w, "composition");
        jw_obj_begin(w);
            jw_kv_bool(w, "active", editing->composition.active);
            jw_key(w, "surface");
            write_editing_surface_ref(w, editing->composition.active ||
                editing->composition.surface.kind != EDIT_SURFACE_NONE
                    ? &editing->composition.surface : NULL);
            jw_key(w, "anchor");
            if (editing->composition.anchor_view) {
                jw_obj_begin(w);
                    write_optional_view_ref(w, "target",
                                            editing->composition.anchor_view);
                    jw_kv_int(w, "offset",
                              editing->composition.anchor_offset);
                jw_obj_end(w);
            } else {
                jw_null(w);
            }
            jw_kv_uint(w, "preedit_len",
                       editing->composition.preedit_len);
            jw_kv_uint(w, "dom_preedit_len",
                       editing->composition.dom_preedit_len);
            jw_kv_uint(w, "commit_len",
                       editing->composition.commit_len);
            jw_kv_uint(w, "caret", editing->composition.caret);
            jw_kv_uint(w, "update_count",
                       editing->composition.update_count);
            jw_kv_bool(w, "committed",
                       editing->composition.committed);
            jw_kv_bool(w, "canceled",
                       editing->composition.canceled);
        jw_obj_end(w);
        jw_key(w, "autoscroll");
        jw_obj_begin(w);
            jw_kv_bool(w, "active", editing->autoscroll.active);
            write_optional_view_ref(w, "surface",
                                    editing->autoscroll.surface);
            jw_kv_double(w, "pointer_x", editing->autoscroll.pointer_x);
            jw_kv_double(w, "pointer_y", editing->autoscroll.pointer_y);
            jw_kv_double(w, "tick_last_time",
                         editing->autoscroll.tick_last_time);
            jw_kv_double(w, "caret_blink_elapsed",
                         editing->autoscroll.caret_blink_elapsed);
        jw_obj_end(w);
    jw_obj_end(w);
}

static void emit_state_snapshot(DocState* state,
                                EventStateLog* log,
                                uint64_t cascade_id) {
    if (!event_state_log_enabled(log)) return;

    char buf[STATE_MACHINE_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                 "state.snapshot", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        if (!state) {
            jw_kv_bool(&w, "has_state", false);
        } else {
            jw_kv_bool(&w, "has_state", true);

            jw_key(&w, "focus");
            jw_obj_begin(&w);
                if (state->focus && state->focus->current) {
                    jw_kv_str(&w, "state", "ElementFocused");
                    write_optional_view_ref(&w, "target", state->focus->current);
                    jw_kv_bool(&w, "focus_visible", state->focus->focus_visible);
                    jw_kv_bool(&w, "from_keyboard", state->focus->from_keyboard);
                    jw_kv_bool(&w, "from_mouse", state->focus->from_mouse);
                } else {
                    jw_kv_str(&w, "state", "DocumentActiveNoFocus");
                    write_optional_view_ref(&w, "target", NULL);
                }
            jw_obj_end(&w);

            jw_key(&w, "caret");
            jw_obj_begin(&w);
                View* caret_view = NULL;
                int caret_offset = 0;
                int caret_line = 0;
                int caret_column = 0;
                float caret_x = 0;
                float caret_y = 0;
                float caret_height = 0;
                if (caret_get_debug_snapshot(state, &caret_view, &caret_offset,
                        &caret_line, &caret_column, &caret_x, &caret_y,
                        &caret_height, NULL)) {
                    jw_kv_str(&w, "state", "CaretCollapsed");
                    write_optional_view_ref(&w, "target", caret_view);
                    jw_kv_int(&w, "offset", caret_offset);
                    jw_kv_int(&w, "line", caret_line);
                    jw_kv_int(&w, "column", caret_column);
                    jw_key(&w, "rect");
                    jw_obj_begin(&w);
                        jw_kv_double(&w, "x", caret_x);
                        jw_kv_double(&w, "y", caret_y);
                        jw_kv_double(&w, "w", 1.0);
                        jw_kv_double(&w, "h", caret_height);
                    jw_obj_end(&w);
                } else {
                    jw_kv_str(&w, "state", "SelectionEmpty");
                    write_optional_view_ref(&w, "target", NULL);
                }
            jw_obj_end(&w);

            jw_key(&w, "selection");
            jw_obj_begin(&w);
                View* anchor_view = NULL;
                View* focus_view = NULL;
                int anchor_offset = 0;
                int focus_offset = 0;
                bool collapsed = true;
                selection_get_anchor_snapshot(state, &anchor_view, &anchor_offset, &collapsed);
                selection_get_focus_snapshot(state, &focus_view, &focus_offset,
                    NULL, NULL, NULL);
                if (!collapsed && anchor_view && focus_view) {
                    jw_kv_str(&w, "state", "RangeSelectedForward");
                    jw_key(&w, "anchor");
                    jw_obj_begin(&w);
                        write_optional_view_ref(&w, "node", anchor_view);
                        jw_kv_int(&w, "offset", anchor_offset);
                    jw_obj_end(&w);
                    jw_key(&w, "focus");
                    jw_obj_begin(&w);
                        write_optional_view_ref(&w, "node", focus_view);
                        jw_kv_int(&w, "offset", focus_offset);
                    jw_obj_end(&w);
                    jw_kv_bool(&w, "is_collapsed", false);
                } else {
                    jw_kv_str(&w, "state", caret_view ?
                        "CaretCollapsed" : "SelectionEmpty");
                    jw_kv_bool(&w, "is_collapsed", true);
                }
            jw_obj_end(&w);

            jw_key(&w, "document_state");
            jw_obj_begin(&w);
                jw_kv_double(&w, "scroll_x", state->scroll_x);
                jw_kv_double(&w, "scroll_y", state->scroll_y);
                jw_kv_double(&w, "zoom", state->zoom_level);
                jw_kv_bool(&w, "dirty", state->is_dirty);
                jw_kv_bool(&w, "needs_reflow", state->needs_reflow);
                jw_kv_bool(&w, "needs_repaint", state->needs_repaint);
            jw_obj_end(&w);

            write_editing_interaction_snapshot(&w, state);
        }
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

bool radiant_state_settle(DocState* state,
                          EventStateLog* log,
                          uint64_t cascade_id) {
    StateValidationReport report;
    bool ok = radiant_state_validate_interaction(state, &report);

    emit_validation_record(log, cascade_id, &report);
    emit_state_snapshot(state, log, cascade_id);
    radiant_state_dump_emit_cascade(state, cascade_id);

    if (!ok && report.message[0]) {
        log_error("state_machine: invalid interaction state: %s", report.message);
    }
    return ok;
}

void state_end_event_cascade(DocState* state,
                             EventStateLog* log,
                             uint64_t cascade_id) {
    if (state) {
        state_end_batch(state);
    }
    radiant_state_settle(state, log, cascade_id);
    event_state_log_end_cascade(log, cascade_id);
    if (state && state->active_cascade_depth > 0) {
        state->active_cascade_depth--;
        if (state->active_cascade_depth == 0) {
            state->active_event_log = NULL;
            state->active_cascade_id = 0;
        }
    }
}
