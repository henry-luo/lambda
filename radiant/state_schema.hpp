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
    SM_FAMILY_RICH_EDIT,
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

typedef enum FocusFsmState {
    FOCUS_NO_DOCUMENT = 0,
    FOCUS_DOC_INACTIVE,
    FOCUS_DOC_ACTIVE_NONE,
    FOCUS_ELEMENT,
    FOCUS_TEXT_CONTROL,
    FOCUS_CONTENTEDITABLE,
    FOCUS_SUBDOCUMENT
} FocusFsmState;

typedef enum HoverFsmState {
    HOVER_NONE = 0,
    HOVER_TARGET
} HoverFsmState;

typedef enum ActiveFsmState {
    ACTIVE_NONE = 0,
    ACTIVE_PRESSED
} ActiveFsmState;

typedef enum DragFsmState {
    DRAG_IDLE = 0,
    DRAG_PENDING,
    DRAG_ACTIVE,
    DRAG_OVER_TARGET
} DragFsmState;

typedef enum ScrollFsmState {
    SCROLL_IDLE = 0,
    SCROLL_BAR_HOVER,
    SCROLL_BAR_DRAGGING
} ScrollFsmState;

typedef enum CheckableFsmState {
    CHK_UNCHECKED = 0,
    CHK_CHECKED,
    CHK_INDETERMINATE
} CheckableFsmState;

typedef enum SelectFsmState {
    SELCTL_CLOSED = 0,
    SELCTL_OPEN
} SelectFsmState;

typedef enum RangeFsmState {
    RANGE_VALUE = 0
} RangeFsmState;

typedef enum TextFsmState {
    TEXT_EMPTY = 0,
    TEXT_VALUE,
    TEXT_SELECTION
} TextFsmState;

typedef enum DropdownFsmState {
    DD_CLOSED = 0,
    DD_OPEN
} DropdownFsmState;

typedef enum ContextMenuFsmState {
    CM_CLOSED = 0,
    CM_OPEN,
    CM_HOVER
} ContextMenuFsmState;

typedef enum RichEditFsmState {
    RICH_EDIT_IDLE = 0,
    RICH_EDIT_TX_OPEN,
    RICH_EDIT_BEFOREINPUT_DONE,
    RICH_EDIT_MUTATED,
    RICH_EDIT_SELECTION_SET,
    RICH_EDIT_INPUT_DONE
} RichEditFsmState;

typedef enum SmEvent {
    SM_EV_DOC_LOAD = 0,
    SM_EV_DOC_COMMIT,
    SM_EV_DOC_UNLOAD,
    SM_EV_COLLAPSE_TO_BOUNDARY,
    SM_EV_START_POINTER_SELECTION,
    SM_EV_UI_START_POINTER_SELECTION,
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
    SM_EV_FOCUS_ELEMENT,
    SM_EV_BLUR_CURRENT,
    SM_EV_FOCUS_MOVE_FWD,
    SM_EV_FOCUS_MOVE_BACK,
    SM_EV_UI_FOCUS_WITH_BLUR,
    SM_EV_UI_FOCUS_WITH_CHANGE,
    SM_EV_UI_BLUR_WITH_BLUR,
    SM_EV_UI_BLUR_WITH_CHANGE,
    SM_EV_HOVER_SET,
    SM_EV_HOVER_CLEAR,
    SM_EV_ACTIVE_SET,
    SM_EV_ACTIVE_CLEAR,
    SM_EV_DRAG_SET_STATE,
    SM_EV_DRAG_BEGIN_DROP,
    SM_EV_DRAG_UPDATE_MOTION,
    SM_EV_DRAG_SET_DROP_ACTIVE,
    SM_EV_DRAG_SET_DROP_TARGET,
    SM_EV_DRAG_CLEAR_DROP,
    SM_EV_SCROLL_SET_POSITION,
    SM_EV_SCROLL_SET_MAX,
    SM_EV_SCROLLBAR_HOVER,
    SM_EV_SCROLLBAR_BEGIN_DRAG,
    SM_EV_SCROLLBAR_CLEAR_DRAG,
    SM_EV_FORM_SET_CHECKED,
    SM_EV_FORM_UNCHECK_RADIO_GROUP,
    SM_EV_FORM_SET_VALUE,
    SM_EV_FORM_REPLACE_TEXT,
    SM_EV_FORM_HISTORY,
    SM_EV_FORM_SET_SELECTION,
    SM_EV_FORM_SET_SELECTED_INDEX,
    SM_EV_FORM_SET_RANGE_VALUE,
    SM_EV_FORM_SET_HOVER_INDEX,
    SM_EV_FORM_SET_DISABLED,
    SM_EV_FORM_SET_READONLY,
    SM_EV_FORM_SET_REQUIRED,
    SM_EV_DROPDOWN_OPEN,
    SM_EV_DROPDOWN_CLOSE,
    SM_EV_DROPDOWN_SET_GEOMETRY,
    SM_EV_CONTEXT_MENU_OPEN,
    SM_EV_CONTEXT_MENU_CLOSE,
    SM_EV_CONTEXT_MENU_HOVER,
    SM_EV_RICH_TRANSACTION,
    SM_EV_EDIT_TX_BEGIN,
    SM_EV_EDIT_BEFOREINPUT,
    SM_EV_EDIT_MUTATE_DOM,
    SM_EV_EDIT_SET_SELECTION,
    SM_EV_EDIT_INPUT,
    SM_EV_EDIT_TX_COMMIT,
    SM_EV_EDIT_TX_ABORT,
    SM_EV__COUNT
} SmEvent;

typedef enum SmGuardId {
    SM_GUARD_NONE = 0
} SmGuardId;

typedef enum SmInvariantId {
    SM_INV_NONE = 0,
    SM_INV_FOCUSED_TARGET,
    SM_INV_FOCUS_GRAPH,
    SM_INV_HOVER_GRAPH,
    SM_INV_ACTIVE_GRAPH,
    SM_INV_DRAG_GRAPH,
    SM_INV_EDITING_INTERACTION,
    SM_INV_VIEW_STATE_REGISTRY,
    SM_INV_CARET_PROJECTION,
    SM_INV_SELECTION_PROJECTION,
    SM_INV_TEXT_CONTROL_FOCUS,
    SM_INV_DROPDOWN_OVERLAY,
    SM_INV_CONTEXT_MENU_OVERLAY,
    SM_INV_DIRTY_TRACKING,
    SM_INV_DOM_SELECTION,
    SM_INV_EDITING_SURFACE,
    SM_INV_EDITING_SELECTION_HOST,
    SM_INV_EDITING_FALSE_ISLAND,
    SM_INV_EDITING_TARGET_RANGES,
    SM_INV_DOM_SELECTION_CACHE,
    SM_INV_SELECTION_PROJECTION_CACHE,
    SM_INV_INPUT_EVENT_ORDER,
    SM_INV__COUNT
} SmInvariantId;

typedef enum SmActionFlag {
    SM_ACT_NONE = 0,
    SM_ACT_WRITE_CHECKED = 1u << 0,
    SM_ACT_UNCHECK_RADIO_GROUP = 1u << 1,
    SM_ACT_DISPATCH_BEFOREINPUT = 1u << 2,
    SM_ACT_DISPATCH_INPUT = 1u << 3,
    SM_ACT_DISPATCH_SELECTSTART = 1u << 4,
    SM_ACT_DISPATCH_BLUR = 1u << 5,
    SM_ACT_DISPATCH_CHANGE = 1u << 6,
    SM_ACT_MUTATE_DOM = 1u << 7,
    SM_ACT_SET_SELECTION = 1u << 8
} SmActionFlag;

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

typedef struct StateInvariantBinding {
    SmFamily family;
    int state;
    SmInvariantId invariant;
    const char* name;
} StateInvariantBinding;

extern const StateInvariantBinding RADIANT_INVARIANTS[];
extern const uint32_t RADIANT_INVARIANT_COUNT;

typedef struct SmTransitionScope {
    DocState* state;
    SmFamily family;
    SmEvent event;
    SmViewClass view_class;
    View* target;
    int from_state;
    uint32_t observed_actions;
    struct SmTransitionScope* previous_scope;
    bool committed;
} SmTransitionScope;

void sm_transition_scope_begin(SmTransitionScope* scope,
                               DocState* state,
                               SmFamily family,
                               SmEvent event,
                               View* target);
void sm_transition_scope_commit(SmTransitionScope* scope);
void sm_transition_scope_end(SmTransitionScope* scope);
void sm_observe_action(DocState* state, uint32_t action);

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
