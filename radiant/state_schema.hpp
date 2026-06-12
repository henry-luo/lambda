#ifndef RADIANT_STATE_SCHEMA_HPP
#define RADIANT_STATE_SCHEMA_HPP

#include <stdint.h>
#include <stdbool.h>
#include "state_store.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SmFamily {
    SM_FAMILY_DOCUMENT = 0,
    SM_FAMILY_FOCUS,
    SM_FAMILY_SELECTION,
    SM_FAMILY_IME,
    SM_FAMILY_HOVER,
    SM_FAMILY_ACTIVE,
    SM_FAMILY_DRAG_DROP,
    SM_FAMILY_SCROLL,
    SM_FAMILY_FORM_CHECKABLE,
    SM_FAMILY_FORM_SELECT,
    SM_FAMILY_FORM_RANGE,
    SM_FAMILY_FORM_TEXT,
    SM_FAMILY_DROPDOWN,
    SM_FAMILY_CONTEXT_MENU,
    SM_FAMILY__COUNT
} SmFamily;

typedef enum SmViewClass {
    SM_VC_ANY = 0,
    SM_VC_FOCUSABLE,
    SM_VC_TEXT_CONTROL,
    SM_VC_CHECKBOX,
    SM_VC_RADIO,
    SM_VC_SELECT,
    SM_VC_RANGE,
    SM_VC_FILE,
    SM_VC_SCROLLABLE,
    SM_VC_LINK,
    SM_VC_DOCUMENT
} SmViewClass;

typedef enum SelectionFsmState {
    SEL_EMPTY = 0,
    SEL_CARET_COLLAPSED,
    SEL_RANGE_FORWARD,
    SEL_RANGE_BACKWARD,
    SEL_POINTER_SELECTING,
    SEL_KEYBOARD_EXTENDING
} SelectionFsmState;

typedef enum ImeFsmState {
    IME_IDLE = 0,
    IME_COMPOSING,
    IME_COMMITTED
} ImeFsmState;

typedef enum SmEvent {
    SM_EV_COLLAPSE_TO_BOUNDARY = 0,
    SM_EV_START_POINTER_SELECTION,
    SM_EV_END_POINTER_SELECTION,
    SM_EV_EXTEND_TO_BOUNDARY,
    SM_EV_EXTEND_TO_VIEW,
    SM_EV_SET_BASE_AND_EXTENT,
    SM_EV_SELECT_ALL,
    SM_EV_COLLAPSE_TO_START,
    SM_EV_COLLAPSE_TO_END,
    SM_EV_CLEAR_SELECTION,
    SM_EV_COMPOSITION_START,
    SM_EV_COMPOSITION_UPDATE,
    SM_EV_COMPOSITION_COMMIT,
    SM_EV_COMPOSITION_CANCEL,
    SM_EV__COUNT
} SmEvent;

typedef enum SmGuardId {
    SM_GUARD_NONE = 0
} SmGuardId;

typedef enum SmInvariantId {
    SM_INV_NONE = 0
} SmInvariantId;

#define SM_STATE_ANY  (-1)
#define SM_STATE_SAME (-2)

typedef struct StateTransitionRule {
    SmFamily family;
    SmViewClass view_class;
    int from_state;
    SmEvent event;
    SmGuardId guard;
    const int* to_states;
    uint8_t to_state_count;
    uint32_t actions;
    const SmInvariantId* invariants;
    uint16_t invariant_count;
    const char* name;
} StateTransitionRule;

typedef struct SmTransitionScope {
    DocState* state;
    SmFamily family;
    SmEvent event;
    SmViewClass view_class;
    View* target;
    int from_state;
    bool committed;
} SmTransitionScope;

void sm_transition_scope_begin(SmTransitionScope* scope,
                               DocState* state,
                               SmFamily family,
                               SmEvent event,
                               View* target);
void sm_transition_scope_commit(SmTransitionScope* scope);
void sm_transition_scope_end(SmTransitionScope* scope);

int sm_derive_state(DocState* state, SmFamily family, View* target);
SmViewClass sm_classify_view(View* view);

#ifdef __cplusplus
} /* extern "C" */

struct SmTransitionGuard {
#ifndef NDEBUG
    SmTransitionScope scope;

    SmTransitionGuard(DocState* state, SmFamily family, SmEvent event, View* target) {
        sm_transition_scope_begin(&scope, state, family, event, target);
    }

    void commit() {
        sm_transition_scope_commit(&scope);
    }

    ~SmTransitionGuard() {
        sm_transition_scope_end(&scope);
    }
#else
    SmTransitionGuard(DocState* state, SmFamily family, SmEvent event, View* target) {
        (void)state;
        (void)family;
        (void)event;
        (void)target;
    }

    void commit() {}
#endif
};
#endif

#endif /* RADIANT_STATE_SCHEMA_HPP */
