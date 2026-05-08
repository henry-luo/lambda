/* Radiant interaction state-machine boundary — Phase 3 implementation. */

#include "state_machine.hpp"
#include "dom_range.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"

#include <stdio.h>
#include <string.h>

extern bool is_view_focusable(View* view);

#define STATE_MACHINE_RECORD_BUFSZ 2048

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
    }

    if (state->caret) {
        if (state->caret->visible && !state->caret->view) {
            report_fail(report, "visible caret has no target view");
        }
        if (state->caret->char_offset < 0) {
            report_fail(report, "caret offset is negative");
        }
        if (state->caret->view && state->focus && state->focus->current &&
            state->caret->view != state->focus->current) {
            report_fail(report, "caret target differs from focus target");
        }
    }

    if (state->selection) {
        SelectionState* sel = state->selection;
        if (sel->anchor_offset < 0 || sel->focus_offset < 0) {
            report_fail(report, "selection offset is negative");
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

    if (state->dom_selection && state->dom_selection->range_count > 1) {
        report_fail(report, "DOM selection has more than one range");
    }

    return !report || report->ok;
}

uint64_t state_begin_event_cascade(RadiantState* state,
                                   EventStateLog* log,
                                   const char* cause) {
    if (state) {
        state_begin_batch(state);
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
}
