#include "editing_target_range.hpp"

#include "form_control.hpp"
#include "state_store.hpp"
#include "text_control.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"

static uint32_t text_control_prev_offset(uint32_t offset) {
    return offset > 0 ? offset - 1 : 0;
}

static uint32_t text_control_next_offset(uint32_t offset, uint32_t limit) {
    return offset < limit ? offset + 1 : limit;
}

static uint32_t compute_text_control_target_ranges(DocState* state,
                                                   const EditingSurface* surface,
                                                   const EditingIntent* intent,
                                                   EditingTargetRange* out)
{
    if (!state || !surface || !surface->owner || !intent || !out) return 0;
    DomElement* elem = surface->owner;
    if (!tc_is_text_control(elem)) return 0;
    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) return 0;

    switch (intent->type) {
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
        case INPUT_INTENT_SELECT_ALL:
            return 0;
        default:
            break;
    }

    uint32_t start = 0;
    uint32_t end = 0;
    form_control_get_selection(state, static_cast<View*>(elem), &start, &end, nullptr);
    uint32_t limit = form->current_value_u16_len;
    if (start > limit) start = limit;
    if (end > limit) end = limit;
    if (start > end) {
        uint32_t tmp = start;
        start = end;
        end = tmp;
    }

    if (start == end) {
        switch (intent->type) {
            case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
                start = text_control_prev_offset(start);
                break;
            case INPUT_INTENT_DELETE_CONTENT_FORWARD:
                end = text_control_next_offset(end, limit);
                break;
            case INPUT_INTENT_DELETE_WORD_BACKWARD:
                start = 0;
                break;
            case INPUT_INTENT_DELETE_WORD_FORWARD:
                end = limit;
                break;
            case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
            case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:
                start = 0;
                break;
            case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
            case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:
                end = limit;
                break;
            default:
                break;
        }
    }

    DomNode* node = static_cast<DomNode*>(elem);
    out[0].start = { node, start };
    out[0].end = { node, end };
    return 1;
}

uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap)
{
    if (!state || !surface || !intent || !out || cap < 1) return 0;
    if (editing_surface_is_text_control(surface)) {
        return compute_text_control_target_ranges(state, surface, intent, out);
    }
    if (!editing_surface_is_rich(surface)) return 0;
    if (!state->dom_selection) return 0;

    switch (intent->type) {
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
        case INPUT_INTENT_SELECT_ALL:
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:
            return 0;
        default:
            break;
    }

    DomSelection* sel = state->dom_selection;
    DomNode* anchor = dom_selection_anchor_node(sel);
    uint32_t anchor_off = dom_selection_anchor_offset(sel);
    DomNode* focus = dom_selection_focus_node(sel);
    uint32_t focus_off = dom_selection_focus_offset(sel);
    if (!anchor || !focus) return 0;

    DomBoundary start = { anchor, anchor_off };
    DomBoundary end = { focus, focus_off };
    DomBoundaryOrder ord = dom_boundary_compare(&start, &end);
    if (ord == DOM_BOUNDARY_AFTER) {
        DomBoundary tmp = start;
        start = end;
        end = tmp;
    }

    bool collapsed = dom_selection_is_collapsed(sel);
    if (!collapsed) {
        out[0].start = start;
        out[0].end = end;
        return 1;
    }

    switch (intent->type) {
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD: {
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_CHARACTER, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_CONTENT_FORWARD: {
            DomBoundary next = dom_boundary_move(end, DOM_MOD_CHARACTER, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        case INPUT_INTENT_DELETE_WORD_BACKWARD: {
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_WORD, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_WORD_FORWARD: {
            DomBoundary next = dom_boundary_move(end, DOM_MOD_WORD, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD: {
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_DOCUMENT, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD: {
            DomBoundary next = dom_boundary_move(end, DOM_MOD_DOCUMENT, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        default:
            out[0].start = start;
            out[0].end = end;
            return 1;
    }
}
