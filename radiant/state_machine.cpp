/* Radiant interaction state-machine boundary — Phase 3 implementation. */

#include "state_machine.hpp"
#include "state_store_internal.hpp"
#include "dom_range.hpp"
#include "dom_range_resolver.hpp"
#include "form_control.hpp"
#include "text_control.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"

#ifndef NDEBUG
#include <assert.h>
#endif
#include <stdio.h>
#include <string.h>

extern bool is_view_focusable(View* view);

#define STATE_MACHINE_RECORD_BUFSZ 2048

static void transition_enter(RadiantState* state) {
    if (state) state->transition_depth++;
}

static void transition_leave(RadiantState* state) {
    if (state && state->transition_depth > 0) state->transition_depth--;
}

bool focus_transition(RadiantState* state,
                      FocusTransitionKind kind,
                      FocusTransitionArgs* args) {
    if (!state) return false;

    transition_enter(state);
    switch (kind) {
        case FOCUS_TRANSITION_FOCUS_ELEMENT:
            focus_set(state, args ? args->target : NULL,
                      args ? args->from_keyboard : false);
            break;
        case FOCUS_TRANSITION_BLUR_CURRENT:
            focus_clear(state);
            break;
        case FOCUS_TRANSITION_MOVE:
            if (!args) { transition_leave(state); return false; }
            if (!focus_move(state, args->root, args->forward)) {
                transition_leave(state);
                return false;
            }
            break;
        default:
            transition_leave(state);
            return false;
    }
    transition_leave(state);
    radiant_state_assert_valid(state, "focus_transition");
    return radiant_state_validate_interaction(state, NULL);
}

static void state_machine_sync_selection_projection(RadiantState* state) {
    if (!state) return;
    if (state->selection && !state->selection->is_collapsed) {
        dom_selection_sync_from_legacy_selection(state);
        legacy_sync_from_dom_selection(state);
    } else if (state->caret && state->caret->view) {
        dom_selection_sync_from_legacy_caret(state);
        legacy_sync_from_dom_selection(state);
    }
}

bool caret_transition(RadiantState* state,
                      CaretTransitionKind kind,
                      CaretTransitionArgs* args) {
    if (!state || !args) return false;

    transition_enter(state);
    switch (kind) {
        case CARET_TRANSITION_COLLAPSE_TO_BOUNDARY:
            caret_set(state, args->target, args->offset);
            break;
        default:
            transition_leave(state);
            return false;
    }
    transition_leave(state);
    state_machine_sync_selection_projection(state);
    radiant_state_assert_valid(state, "caret_transition");
    return radiant_state_validate_interaction(state, NULL);
}

bool selection_transition(RadiantState* state,
                          SelectionTransitionKind kind,
                          SelectionTransitionArgs* args) {
    if (!state) return false;

    transition_enter(state);
    switch (kind) {
        case SELECTION_TRANSITION_START_POINTER_SELECTION:
            if (!args) { transition_leave(state); return false; }
            selection_start(state, args->target, args->focus_offset);
            if (state->selection) state->selection->is_selecting = true;
            break;
        case SELECTION_TRANSITION_END_POINTER_SELECTION:
            if (state->selection) state->selection->is_selecting = false;
            break;
        case SELECTION_TRANSITION_EXTEND_TO_BOUNDARY:
            if (!args) { transition_leave(state); return false; }
            selection_extend(state, args->focus_offset);
            break;
        case SELECTION_TRANSITION_EXTEND_TO_VIEW:
            if (!args) { transition_leave(state); return false; }
            selection_extend_to_view(state, args->target, args->focus_offset);
            break;
        case SELECTION_TRANSITION_SET_BASE_AND_EXTENT:
            if (!args) { transition_leave(state); return false; }
            selection_set(state, args->target, args->anchor_offset, args->focus_offset);
            break;
        case SELECTION_TRANSITION_SELECT_ALL:
            selection_select_all(state);
            break;
        case SELECTION_TRANSITION_COLLAPSE_TO_START:
            selection_collapse(state, true);
            break;
        case SELECTION_TRANSITION_COLLAPSE_TO_END:
            selection_collapse(state, false);
            break;
        case SELECTION_TRANSITION_CLEAR_SELECTION:
            selection_clear(state);
            break;
        default:
            transition_leave(state);
            return false;
    }
    transition_leave(state);
    state_machine_sync_selection_projection(state);
    radiant_state_assert_valid(state, "selection_transition");
    return radiant_state_validate_interaction(state, NULL);
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

static int boundary_legacy_offset(const DomBoundary* boundary) {
    if (!boundary || !boundary->node) return 0;
    if (boundary->node->is_text()) {
        return (int)dom_text_utf16_to_utf8((DomText*)boundary->node, boundary->offset);
    }
    return (int)boundary->offset;
}

static View* focus_validation_root(View* view) {
    View* root = view;
    while (root && root->parent) {
        root = (View*)root->parent;
    }
    return root;
}

static bool focus_path_contains(View* focused, View* candidate) {
    View* node = focused;
    while (node) {
        if (node == candidate) return true;
        node = (View*)node->parent;
    }
    return false;
}

static bool view_has_document_root(View* view) {
    if (!view) return false;
    View* root = focus_validation_root(view);
    return root != NULL;
}

static uint32_t legacy_view_offset_limit(View* view) {
    if (!view) return 0;
    DomNode* node = (DomNode*)view;
    if (node->is_text()) {
        DomText* text = (DomText*)node;
        return dom_text_utf16_to_utf8(text, dom_text_utf16_length(text));
    }
    if (node->is_element()) {
        DomElement* elem = (DomElement*)node;
        if (tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            if (elem->form) return elem->form->current_value_len;
        }
    }
    return dom_node_boundary_length(node);
}

static void validate_text_control_form_state(DomElement* elem,
                                             StateValidationReport* report) {
    if (!elem || !tc_is_text_control(elem)) return;
    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) {
        report_fail(report, "text control has no form state");
        return;
    }
    if (form->selection_start > form->current_value_u16_len ||
        form->selection_end > form->current_value_u16_len) {
        report_fail(report, "text-control selection exceeds value length");
    }
    if (form->selection_start > form->selection_end) {
        report_fail(report, "text-control selection start exceeds end");
    }
    if (form->selection_direction > 2) {
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

static void validate_focus_node(RadiantState* state, View* node, View* focused,
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
        DomElement* element = (DomElement*)node;
        if (dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS) != expected_focus) {
            report_fail(report, "DOM focus pseudo bit is inconsistent");
        }
        if (dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS_WITHIN) != expected_within) {
            report_fail(report, "DOM focus-within pseudo bit is inconsistent");
        }
        if (dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS_VISIBLE) != expected_visible) {
            report_fail(report, "DOM focus-visible pseudo bit is inconsistent");
        }

        DomNode* child = element->first_child;
        while (child) {
            validate_focus_node(state, (View*)child, focused, report, focus_count);
            child = child->next_sibling;
        }
    }
}

static void validate_focus_invariants(RadiantState* state,
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

static void validate_selection_invariants(RadiantState* state,
                                          StateValidationReport* report) {
    if (!state || !state->dom_selection) return;

    DomSelection* selection = state->dom_selection;
    if (selection->range_count > 1) {
        report_fail(report, "DOM selection has more than one range");
        return;
    }

    if (selection->range_count == 0) {
        if (selection->anchor.node || selection->focus.node || !selection->is_collapsed ||
            selection->direction != DOM_SEL_DIR_NONE) {
            report_fail(report, "empty DOM selection has stale anchor/focus state");
        }
        return;
    }

    DomRange* range = selection->ranges[0];
    if (!range) {
        report_fail(report, "DOM selection range slot is empty");
        return;
    }

    if (!dom_boundary_is_valid(&selection->anchor) ||
        !dom_boundary_is_valid(&selection->focus) ||
        !dom_boundary_is_valid(&range->start) ||
        !dom_boundary_is_valid(&range->end)) {
        report_fail(report, "DOM selection contains invalid boundary");
    }

    DomNode* anchor_root = boundary_root(&selection->anchor);
    DomNode* focus_root = boundary_root(&selection->focus);
    DomNode* start_root = boundary_root(&range->start);
    DomNode* end_root = boundary_root(&range->end);
    if (!anchor_root || !focus_root || !start_root || !end_root ||
        anchor_root != focus_root || anchor_root != start_root || anchor_root != end_root) {
        report_fail(report, "DOM selection endpoints are in incompatible roots");
    }

    bool range_collapsed = dom_range_collapsed(range);
    if (selection->is_collapsed != range_collapsed) {
        report_fail(report, "DOM selection collapsed flag disagrees with range");
    }
    if (selection->is_collapsed) {
        if (selection->direction != DOM_SEL_DIR_NONE) {
            report_fail(report, "collapsed DOM selection has non-none direction");
        }
        if (dom_boundary_compare(&selection->anchor, &selection->focus) != DOM_BOUNDARY_EQUAL) {
            report_fail(report, "collapsed DOM selection endpoints differ");
        }
    } else {
        DomBoundaryOrder anchor_focus = dom_boundary_compare(&selection->anchor, &selection->focus);
        if (selection->direction == DOM_SEL_DIR_NONE) {
            report_fail(report, "non-collapsed DOM selection has no direction");
        } else if (selection->direction == DOM_SEL_DIR_FORWARD && anchor_focus == DOM_BOUNDARY_AFTER) {
            report_fail(report, "forward DOM selection direction is inconsistent");
        } else if (selection->direction == DOM_SEL_DIR_BACKWARD && anchor_focus == DOM_BOUNDARY_BEFORE) {
            report_fail(report, "backward DOM selection direction is inconsistent");
        }
    }

    if (state->selection) {
        SelectionState* legacy = state->selection;
        if ((DomNode*)legacy->anchor_view != selection->anchor.node ||
            (DomNode*)legacy->focus_view != selection->focus.node ||
            legacy->anchor_offset != boundary_legacy_offset(&selection->anchor) ||
            legacy->focus_offset != boundary_legacy_offset(&selection->focus) ||
            legacy->is_collapsed != selection->is_collapsed) {
            report_fail(report, "legacy selection projection is stale");
        }
        if (legacy->is_selecting && selection->range_count == 0) {
            report_fail(report, "active legacy selection has no DOM range");
        }
    }

    if (state->caret && selection->is_collapsed) {
        if ((DomNode*)state->caret->view != selection->focus.node ||
            state->caret->char_offset != boundary_legacy_offset(&selection->focus)) {
            report_fail(report, "legacy caret projection is stale");
        }
    }
}

bool radiant_state_validate_interaction(RadiantState* state,
                                        StateValidationReport* report) {
    report_init(report);

    if (!state) {
        return true;
    }

    if (state->focus && state->focus->current) {
        if (!is_view_focusable(state->focus->current)) {
            report_fail(report, "focused target is not focusable");
        }
        if (state->focus->current->is_element()) {
            validate_text_control_form_state((DomElement*)state->focus->current, report);
        }
    }

    validate_focus_invariants(state, report);

    if (state->caret) {
        if (state->caret->visible && !state->caret->view) {
            report_fail(report, "visible caret has no target view");
        }
        if (state->caret->char_offset < 0) {
            report_fail(report, "caret offset is negative");
        }
        if (state->caret->view &&
            (uint32_t)state->caret->char_offset > legacy_view_offset_limit(state->caret->view)) {
            report_fail(report, "caret offset exceeds target length");
        }
        if (state->caret->view && state->caret->view->is_element() &&
            state->focus && state->focus->current &&
            state->caret->view != state->focus->current) {
            report_fail(report, "caret target differs from focus target");
        }
        if (state->caret->view && state->caret->view->is_element()) {
            DomElement* elem = (DomElement*)state->caret->view;
            if (!tc_is_text_control(elem) && state->caret->visible) {
                report_fail(report, "visible element caret target is not editable");
            }
        }
    }

    if (state->selection) {
        SelectionState* sel = state->selection;
        if (sel->anchor_offset < 0 || sel->focus_offset < 0) {
            report_fail(report, "selection offset is negative");
        }
        if (sel->anchor_view &&
            (uint32_t)sel->anchor_offset > legacy_view_offset_limit(sel->anchor_view)) {
            report_fail(report, "selection anchor offset exceeds target length");
        }
        if (sel->focus_view &&
            (uint32_t)sel->focus_offset > legacy_view_offset_limit(sel->focus_view)) {
            report_fail(report, "selection focus offset exceeds target length");
        }
        if (!sel->is_collapsed && (!sel->anchor_view || !sel->focus_view)) {
            report_fail(report, "non-collapsed selection has missing endpoints");
        }
        if (sel->is_collapsed && sel->anchor_view && sel->focus_view &&
            !same_view_position(sel->anchor_view, sel->anchor_offset,
                                sel->focus_view, sel->focus_offset)) {
            report_fail(report, "collapsed selection endpoints differ");
        }
        if (sel->is_selecting && !sel->anchor_view) {
            report_fail(report, "active selection has no anchor");
        }
    }

    DomElement* active_text_control = tc_get_active_element();
    View* focused = state->focus ? state->focus->current : NULL;
    if (focused && focused->is_element() && tc_is_text_control((DomElement*)focused)) {
        if (active_text_control && active_text_control != (DomElement*)focused) {
            report_fail(report, "active text control differs from focus target");
        }
    } else if (active_text_control) {
        report_fail(report, "active text control exists without text-control focus");
    }

    if (state->open_dropdown) {
        validate_transient_ui_target(state->open_dropdown, "open dropdown target is detached", report);
        if (!state->open_dropdown->is_element()) {
            report_fail(report, "open dropdown target is not an element");
        } else {
            DomElement* elem = (DomElement*)state->open_dropdown;
            if (!elem->form || !elem->form->dropdown_open) {
                report_fail(report, "open dropdown state disagrees with form control");
            }
        }
        if (state->dropdown_width < 0 || state->dropdown_height < 0) {
            report_fail(report, "open dropdown has negative dimensions");
        }
    }

    if (state->context_menu_target) {
        validate_transient_ui_target(state->context_menu_target, "context menu target is detached", report);
        if (!state->context_menu_target->is_element() ||
            !tc_is_text_control((DomElement*)state->context_menu_target)) {
            report_fail(report, "context menu target is not a text control");
        }
        if (state->context_menu_width < 0 || state->context_menu_height < 0) {
            report_fail(report, "context menu has negative dimensions");
        }
        if (state->context_menu_hover < -1) {
            report_fail(report, "context menu hover index is invalid");
        }
    }

    if ((state->selection_layout_dirty || state->dirty_tracker.full_repaint ||
         state->dirty_tracker.full_reflow || dirty_has_regions(&state->dirty_tracker)) &&
        !state->needs_repaint && !state->needs_reflow) {
        report_fail(report, "dirty tracking is set without repaint or reflow flag");
    }

    validate_selection_invariants(state, report);

    return !report || report->ok;
}

void radiant_state_assert_valid(RadiantState* state, const char* context) {
#ifndef NDEBUG
    StateValidationReport report;
    bool ok = radiant_state_validate_interaction(state, &report);
    if (!ok) {
        log_error("state_machine: assertion failed after %s: %s",
            context ? context : "state mutation",
            report.message[0] ? report.message : "interaction invariant failed");
    }
    assert(ok && "RadiantState interaction invariant failed");
#else
    (void)state;
    (void)context;
#endif
}

uint64_t state_begin_event_cascade(RadiantState* state,
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

static void emit_state_snapshot(RadiantState* state,
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
                if (state->caret && state->caret->view) {
                    jw_kv_str(&w, "state", "CaretCollapsed");
                    write_optional_view_ref(&w, "target", state->caret->view);
                    jw_kv_int(&w, "offset", state->caret->char_offset);
                    jw_kv_int(&w, "line", state->caret->line);
                    jw_kv_int(&w, "column", state->caret->column);
                    jw_key(&w, "rect");
                    jw_obj_begin(&w);
                        jw_kv_double(&w, "x", state->caret->x);
                        jw_kv_double(&w, "y", state->caret->y);
                        jw_kv_double(&w, "w", 1.0);
                        jw_kv_double(&w, "h", state->caret->height);
                    jw_obj_end(&w);
                } else {
                    jw_kv_str(&w, "state", "SelectionEmpty");
                    write_optional_view_ref(&w, "target", NULL);
                }
            jw_obj_end(&w);

            jw_key(&w, "selection");
            jw_obj_begin(&w);
                if (state->selection && !state->selection->is_collapsed) {
                    jw_kv_str(&w, "state", "RangeSelectedForward");
                    jw_key(&w, "anchor");
                    jw_obj_begin(&w);
                        write_optional_view_ref(&w, "node", state->selection->anchor_view);
                        jw_kv_int(&w, "offset", state->selection->anchor_offset);
                    jw_obj_end(&w);
                    jw_key(&w, "focus");
                    jw_obj_begin(&w);
                        write_optional_view_ref(&w, "node", state->selection->focus_view);
                        jw_kv_int(&w, "offset", state->selection->focus_offset);
                    jw_obj_end(&w);
                    jw_kv_bool(&w, "is_collapsed", false);
                } else {
                    jw_kv_str(&w, "state", state->caret && state->caret->view ?
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
        }
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

bool radiant_state_settle(RadiantState* state,
                          EventStateLog* log,
                          uint64_t cascade_id) {
    StateValidationReport report;
    bool ok = radiant_state_validate_interaction(state, &report);

    emit_validation_record(log, cascade_id, &report);
    emit_state_snapshot(state, log, cascade_id);

    if (!ok && report.message[0]) {
        log_error("state_machine: invalid interaction state: %s", report.message);
    }
    return ok;
}

void state_end_event_cascade(RadiantState* state,
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
