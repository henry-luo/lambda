/* Radiant declarative state-transition schema — first selection/IME slice. */

#include "state_schema.hpp"
#include "state_store_internal.hpp"
#include "dom_range.hpp"
#include "text_control.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"

#ifndef NDEBUG
#include <assert.h>
#endif

static const int SM_TO_SELECTION_CARET[] = {
    SEL_CARET_COLLAPSED,
    SM_STATE_SAME,
};

static const int SM_TO_SELECTION_EMPTY_OR_CARET[] = {
    SEL_EMPTY,
    SEL_CARET_COLLAPSED,
    SM_STATE_SAME,
};

static const int SM_TO_SELECTION_RANGE_OR_CARET[] = {
    SEL_CARET_COLLAPSED,
    SEL_RANGE_FORWARD,
    SEL_RANGE_BACKWARD,
    SM_STATE_SAME,
};

static const int SM_TO_SELECTION_POINTER[] = {
    SEL_POINTER_SELECTING,
    SM_STATE_SAME,
};

static const int SM_TO_IME_COMPOSING[] = {
    IME_COMPOSING,
    SM_STATE_SAME,
};

static const int SM_TO_IME_IDLE[] = {
    IME_IDLE,
    SM_STATE_SAME,
};

#define SM_RULE_TO(list_name) list_name, (uint8_t)(sizeof(list_name) / sizeof((list_name)[0]))

static const StateTransitionRule RADIANT_STATE_RULES[] = {
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_COLLAPSE_TO_BOUNDARY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_CARET), 0, NULL, 0,
      "sel.collapse_to_caret" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_START_POINTER_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_POINTER), 0, NULL, 0,
      "sel.start_pointer" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_END_POINTER_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_RANGE_OR_CARET), 0, NULL, 0,
      "sel.end_pointer" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_EXTEND_TO_BOUNDARY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_RANGE_OR_CARET), 0, NULL, 0,
      "sel.extend_to_boundary" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_EXTEND_TO_VIEW,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_RANGE_OR_CARET), 0, NULL, 0,
      "sel.extend_to_view" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_SET_BASE_AND_EXTENT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_RANGE_OR_CARET), 0, NULL, 0,
      "sel.set_base_and_extent" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_SELECT_ALL,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_RANGE_OR_CARET), 0, NULL, 0,
      "sel.select_all" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_COLLAPSE_TO_START,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_CARET), 0, NULL, 0,
      "sel.collapse_to_start" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_COLLAPSE_TO_END,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_CARET), 0, NULL, 0,
      "sel.collapse_to_end" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_CLEAR_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_EMPTY_OR_CARET), 0, NULL, 0,
      "sel.clear" },

    { SM_FAMILY_IME, SM_VC_ANY, SM_STATE_ANY, SM_EV_COMPOSITION_START,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_IME_COMPOSING), 0, NULL, 0,
      "ime.begin" },
    { SM_FAMILY_IME, SM_VC_ANY, SM_STATE_ANY, SM_EV_COMPOSITION_UPDATE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_IME_COMPOSING), 0, NULL, 0,
      "ime.update" },
    { SM_FAMILY_IME, SM_VC_ANY, SM_STATE_ANY, SM_EV_COMPOSITION_COMMIT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_IME_IDLE), 0, NULL, 0,
      "ime.commit" },
    { SM_FAMILY_IME, SM_VC_ANY, SM_STATE_ANY, SM_EV_COMPOSITION_CANCEL,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_IME_IDLE), 0, NULL, 0,
      "ime.cancel" },
};

#undef SM_RULE_TO

static const uint32_t RADIANT_STATE_RULE_COUNT =
    (uint32_t)(sizeof(RADIANT_STATE_RULES) / sizeof(RADIANT_STATE_RULES[0]));

static const char* sm_family_name(SmFamily family) {
    switch (family) {
        case SM_FAMILY_SELECTION: return "selection";
        case SM_FAMILY_IME: return "ime";
        default: return "unknown";
    }
}

static const char* sm_event_name(SmEvent event) {
    switch (event) {
        case SM_EV_COLLAPSE_TO_BOUNDARY: return "collapse_to_boundary";
        case SM_EV_START_POINTER_SELECTION: return "start_pointer_selection";
        case SM_EV_END_POINTER_SELECTION: return "end_pointer_selection";
        case SM_EV_EXTEND_TO_BOUNDARY: return "extend_to_boundary";
        case SM_EV_EXTEND_TO_VIEW: return "extend_to_view";
        case SM_EV_SET_BASE_AND_EXTENT: return "set_base_and_extent";
        case SM_EV_SELECT_ALL: return "select_all";
        case SM_EV_COLLAPSE_TO_START: return "collapse_to_start";
        case SM_EV_COLLAPSE_TO_END: return "collapse_to_end";
        case SM_EV_CLEAR_SELECTION: return "clear_selection";
        case SM_EV_COMPOSITION_START: return "composition_start";
        case SM_EV_COMPOSITION_UPDATE: return "composition_update";
        case SM_EV_COMPOSITION_COMMIT: return "composition_commit";
        case SM_EV_COMPOSITION_CANCEL: return "composition_cancel";
        default: return "unknown";
    }
}

static int sm_derive_selection_state(DocState* state) {
    if (!state) return SEL_EMPTY;
    if (state->selection && state->selection->is_selecting) {
        return SEL_POINTER_SELECTING;
    }
    DomSelection* selection = state->dom_selection;
    if (!selection || selection->range_count == 0) return SEL_EMPTY;
    if (selection->is_collapsed) return SEL_CARET_COLLAPSED;
    return selection->direction == DOM_SEL_DIR_BACKWARD ?
        SEL_RANGE_BACKWARD : SEL_RANGE_FORWARD;
}

static int sm_derive_ime_state(DocState* state) {
    if (!state) return IME_IDLE;
    if (state->editing.composition.active || state->editing.composing) {
        return IME_COMPOSING;
    }
    return IME_IDLE;
}

int sm_derive_state(DocState* state, SmFamily family, View* target) {
    (void)target;
    switch (family) {
        case SM_FAMILY_SELECTION:
            return sm_derive_selection_state(state);
        case SM_FAMILY_IME:
            return sm_derive_ime_state(state);
        default:
            return 0;
    }
}

SmViewClass sm_classify_view(View* view) {
    if (!view) return SM_VC_ANY;
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && tc_is_text_control(elem)) return SM_VC_TEXT_CONTROL;
    }
    return SM_VC_ANY;
}

static bool sm_rule_matches(const StateTransitionRule* rule,
                            SmFamily family,
                            SmViewClass view_class,
                            int from_state,
                            SmEvent event) {
    if (!rule) return false;
    if (rule->family != family) return false;
    if (rule->event != event) return false;
    if (rule->from_state != SM_STATE_ANY && rule->from_state != from_state) return false;
    if (rule->view_class != SM_VC_ANY && rule->view_class != view_class) return false;
    return true;
}

static const StateTransitionRule* sm_find_rule(SmFamily family,
                                               SmViewClass view_class,
                                               int from_state,
                                               SmEvent event) {
    for (uint32_t i = 0; i < RADIANT_STATE_RULE_COUNT; i++) {
        const StateTransitionRule* rule = &RADIANT_STATE_RULES[i];
        if (sm_rule_matches(rule, family, view_class, from_state, event)) {
            return rule;
        }
    }
    return NULL;
}

static bool sm_rule_allows_to_state(const StateTransitionRule* rule,
                                    int from_state,
                                    int to_state) {
    if (!rule) return false;
    for (uint8_t i = 0; i < rule->to_state_count; i++) {
        int allowed = rule->to_states[i];
        int expected = allowed == SM_STATE_SAME ? from_state : allowed;
        if (expected == to_state) return true;
    }
    return false;
}

void sm_transition_scope_begin(SmTransitionScope* scope,
                               DocState* state,
                               SmFamily family,
                               SmEvent event,
                               View* target) {
    if (!scope) return;
    scope->state = state;
    scope->family = family;
    scope->event = event;
    scope->view_class = sm_classify_view(target);
    scope->target = target;
    scope->from_state = sm_derive_state(state, family, target);
    scope->committed = false;
}

void sm_transition_scope_commit(SmTransitionScope* scope) {
    if (scope) scope->committed = true;
}

void sm_transition_scope_end(SmTransitionScope* scope) {
#ifndef NDEBUG
    if (!scope || !scope->committed) return;
    int to_state = sm_derive_state(scope->state, scope->family, scope->target);
    const StateTransitionRule* rule = sm_find_rule(scope->family, scope->view_class,
                                                   scope->from_state, scope->event);
    if (!rule) {
        log_error("state_schema: undeclared transition family=%s event=%s from=%d to=%d",
            sm_family_name(scope->family), sm_event_name(scope->event),
            scope->from_state, to_state);
        assert(false && "Radiant state schema undeclared transition");
        return;
    }
    if (!sm_rule_allows_to_state(rule, scope->from_state, to_state)) {
        log_error("state_schema: wrong target rule=%s family=%s event=%s from=%d to=%d",
            rule->name ? rule->name : "?",
            sm_family_name(scope->family), sm_event_name(scope->event),
            scope->from_state, to_state);
        assert(false && "Radiant state schema wrong target state");
    }
#else
    (void)scope;
#endif
}
