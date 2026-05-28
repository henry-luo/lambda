#include "editing_target_range.hpp"

#include "state_store.hpp"

uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap)
{
    if (!state || !surface || !intent || !out || cap < 1) return 0;
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
