/* Radiant declarative state-transition schema — transition API families. */

#include "event.hpp"
#include "state_store_internal.hpp"
#include "form_control.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"

#ifndef NDEBUG
#include <assert.h>
#endif

static const int SM_TO_DOC_LOADING[] = {
    DOC_LIFECYCLE_LOADING,
    SM_STATE_SAME,
};

static const int SM_TO_DOC_COMMITTED[] = {
    DOC_LIFECYCLE_COMMITTED,
    SM_STATE_SAME,
};

static const int SM_TO_DOC_UNLOADED[] = {
    DOC_LIFECYCLE_UNLOADED,
    SM_STATE_SAME,
};

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

static const int SM_TO_FOCUS_TARGET_OR_NONE[] = {
    FOCUS_DOC_ACTIVE_NONE,
    FOCUS_ELEMENT,
    FOCUS_TEXT_CONTROL,
    FOCUS_CONTENTEDITABLE,
    SM_STATE_SAME,
};

static const int SM_TO_FOCUS_NONE[] = {
    FOCUS_DOC_ACTIVE_NONE,
    SM_STATE_SAME,
};

static const int SM_TO_HOVER_TARGET_OR_NONE[] = {
    HOVER_NONE,
    HOVER_TARGET,
    SM_STATE_SAME,
};

static const int SM_TO_ACTIVE_TARGET_OR_NONE[] = {
    ACTIVE_NONE,
    ACTIVE_PRESSED,
    SM_STATE_SAME,
};

static const int SM_TO_DRAG_LEGACY_STATE[] = {
    DRAG_IDLE,
    DRAG_ACTIVE,
    SM_STATE_SAME,
};

static const int SM_TO_DRAG_PENDING[] = {
    DRAG_PENDING,
    SM_STATE_SAME,
};

static const int SM_TO_DRAG_MOTION_STATE[] = {
    DRAG_PENDING,
    DRAG_ACTIVE,
    DRAG_OVER_TARGET,
    SM_STATE_SAME,
};

static const int SM_TO_DRAG_CLEAR_STATE[] = {
    DRAG_IDLE,
    DRAG_ACTIVE,
    SM_STATE_SAME,
};

static const int SM_TO_SCROLL_ANY[] = {
    SCROLL_IDLE,
    SCROLL_BAR_HOVER,
    SCROLL_BAR_DRAGGING,
    SM_STATE_SAME,
};

static const int SM_TO_SCROLL_DRAGGING[] = {
    SCROLL_BAR_DRAGGING,
    SM_STATE_SAME,
};

static const int SM_TO_SCROLL_NOT_DRAGGING[] = {
    SCROLL_IDLE,
    SCROLL_BAR_HOVER,
    SM_STATE_SAME,
};

static const int SM_TO_CHECKABLE_VALUE[] = {
    CHK_UNCHECKED,
    CHK_CHECKED,
    SM_STATE_SAME,
};

static const int SM_TO_CHECKABLE_UNCHECKED[] = {
    CHK_UNCHECKED,
    SM_STATE_SAME,
};

static const int SM_TO_SELECT_OPEN[] = {
    SELCTL_OPEN,
    SM_STATE_SAME,
};

static const int SM_TO_SELECT_CLOSED[] = {
    SELCTL_CLOSED,
    SM_STATE_SAME,
};

static const int SM_TO_SELECT_SAME[] = {
    SM_STATE_SAME,
};

static const int SM_TO_RANGE_SAME[] = {
    RANGE_VALUE,
    SM_STATE_SAME,
};

static const int SM_TO_TEXT_VALUE[] = {
    TEXT_EMPTY,
    TEXT_VALUE,
    SM_STATE_SAME,
};

static const int SM_TO_TEXT_ANY[] = {
    TEXT_EMPTY,
    TEXT_VALUE,
    TEXT_SELECTION,
    SM_STATE_SAME,
};

static const int SM_TO_DROPDOWN_OPEN[] = {
    DD_OPEN,
    SM_STATE_SAME,
};

static const int SM_TO_DROPDOWN_CLOSED[] = {
    DD_CLOSED,
    SM_STATE_SAME,
};

static const int SM_TO_DROPDOWN_SAME[] = {
    SM_STATE_SAME,
};

static const int SM_TO_CONTEXT_OPEN[] = {
    CM_OPEN,
    SM_STATE_SAME,
};

static const int SM_TO_CONTEXT_CLOSED[] = {
    CM_CLOSED,
    SM_STATE_SAME,
};

static const int SM_TO_CONTEXT_HOVER[] = {
    CM_OPEN,
    CM_HOVER,
    SM_STATE_SAME,
};

static const int SM_TO_RICH_EDIT_SAME[] = {
    RICH_EDIT_IDLE,
    SM_STATE_SAME,
};

static const int SM_TO_RICH_TX_OPEN[] = {
    RICH_EDIT_TX_OPEN,
};

static const int SM_TO_RICH_BEFOREINPUT[] = {
    RICH_EDIT_BEFOREINPUT_DONE,
};

static const int SM_TO_RICH_MUTATED[] = {
    RICH_EDIT_MUTATED,
};

static const int SM_TO_RICH_SELECTION_SET[] = {
    RICH_EDIT_SELECTION_SET,
};

static const int SM_TO_RICH_INPUT[] = {
    RICH_EDIT_INPUT_DONE,
};

static const int SM_TO_RICH_IDLE[] = {
    RICH_EDIT_IDLE,
};

#define SM_RULE_TO(list_name) list_name, (uint8_t)(sizeof(list_name) / sizeof((list_name)[0]))

static const StateTransitionRule RADIANT_STATE_RULES[] = {
    { SM_FAMILY_DOCUMENT, SM_VC_ANY, SM_STATE_ANY, SM_EV_DOC_LOAD,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DOC_LOADING), 0, NULL, 0,
      "doc.load" },
    { SM_FAMILY_DOCUMENT, SM_VC_ANY, SM_STATE_ANY, SM_EV_DOC_COMMIT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DOC_COMMITTED), 0, NULL, 0,
      "doc.commit" },
    { SM_FAMILY_DOCUMENT, SM_VC_ANY, SM_STATE_ANY, SM_EV_DOC_UNLOAD,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DOC_UNLOADED), 0, NULL, 0,
      "doc.unload" },

    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_FOCUS_ELEMENT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE), 0, NULL, 0,
      "focus.element" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_BLUR_CURRENT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_NONE), 0, NULL, 0,
      "focus.blur_current" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_FOCUS_MOVE_FWD,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE), 0, NULL, 0,
      "focus.move_forward" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_FOCUS_MOVE_BACK,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE), 0, NULL, 0,
      "focus.move_backward" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_UI_FOCUS_WITH_BLUR,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE),
      SM_ACT_DISPATCH_BLUR, NULL, 0,
      "focus.ui_focus_with_blur" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_UI_FOCUS_WITH_CHANGE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE),
      SM_ACT_DISPATCH_CHANGE | SM_ACT_DISPATCH_BLUR, NULL, 0,
      "focus.ui_focus_with_change" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_UI_BLUR_WITH_BLUR,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE),
      SM_ACT_DISPATCH_BLUR, NULL, 0,
      "focus.ui_blur_with_blur" },
    { SM_FAMILY_FOCUS, SM_VC_ANY, SM_STATE_ANY, SM_EV_UI_BLUR_WITH_CHANGE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_FOCUS_TARGET_OR_NONE),
      SM_ACT_DISPATCH_CHANGE | SM_ACT_DISPATCH_BLUR, NULL, 0,
      "focus.ui_blur_with_change" },

    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_COLLAPSE_TO_BOUNDARY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_CARET), 0, NULL, 0,
      "sel.collapse_to_caret" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_START_POINTER_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_POINTER), 0, NULL, 0,
      "sel.start_pointer" },
    { SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_UI_START_POINTER_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECTION_POINTER),
      SM_ACT_DISPATCH_SELECTSTART, NULL, 0,
      "sel.ui_start_pointer" },
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

    { SM_FAMILY_HOVER, SM_VC_ANY, SM_STATE_ANY, SM_EV_HOVER_SET,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_HOVER_TARGET_OR_NONE), 0, NULL, 0,
      "hover.set" },
    { SM_FAMILY_HOVER, SM_VC_ANY, SM_STATE_ANY, SM_EV_HOVER_CLEAR,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_HOVER_TARGET_OR_NONE), 0, NULL, 0,
      "hover.clear" },

    { SM_FAMILY_ACTIVE, SM_VC_ANY, SM_STATE_ANY, SM_EV_ACTIVE_SET,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_ACTIVE_TARGET_OR_NONE), 0, NULL, 0,
      "active.set" },
    { SM_FAMILY_ACTIVE, SM_VC_ANY, SM_STATE_ANY, SM_EV_ACTIVE_CLEAR,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_ACTIVE_TARGET_OR_NONE), 0, NULL, 0,
      "active.clear" },

    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_SET_STATE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_LEGACY_STATE), 0, NULL, 0,
      "drag.set_state" },
    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_BEGIN_DROP,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_PENDING), 0, NULL, 0,
      "drag.begin_drop" },
    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_UPDATE_MOTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_MOTION_STATE), 0, NULL, 0,
      "drag.update_motion" },
    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_SET_DROP_ACTIVE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_MOTION_STATE), 0, NULL, 0,
      "drag.set_drop_active" },
    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_SET_DROP_TARGET,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_MOTION_STATE), 0, NULL, 0,
      "drag.set_drop_target" },
    { SM_FAMILY_DRAG_DROP, SM_VC_ANY, SM_STATE_ANY, SM_EV_DRAG_CLEAR_DROP,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DRAG_CLEAR_STATE), 0, NULL, 0,
      "drag.clear_drop" },

    { SM_FAMILY_SCROLL, SM_VC_ANY, SM_STATE_ANY, SM_EV_SCROLL_SET_POSITION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SCROLL_ANY), 0, NULL, 0,
      "scroll.set_position" },
    { SM_FAMILY_SCROLL, SM_VC_ANY, SM_STATE_ANY, SM_EV_SCROLL_SET_MAX,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SCROLL_ANY), 0, NULL, 0,
      "scroll.set_max" },
    { SM_FAMILY_SCROLL, SM_VC_ANY, SM_STATE_ANY, SM_EV_SCROLLBAR_HOVER,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SCROLL_ANY), 0, NULL, 0,
      "scroll.hover" },
    { SM_FAMILY_SCROLL, SM_VC_ANY, SM_STATE_ANY, SM_EV_SCROLLBAR_BEGIN_DRAG,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SCROLL_DRAGGING), 0, NULL, 0,
      "scroll.begin_drag" },
    { SM_FAMILY_SCROLL, SM_VC_ANY, SM_STATE_ANY, SM_EV_SCROLLBAR_CLEAR_DRAG,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SCROLL_NOT_DRAGGING), 0, NULL, 0,
      "scroll.clear_drag" },

    { SM_FAMILY_FORM_CHECKABLE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_CHECKED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CHECKABLE_VALUE), SM_ACT_WRITE_CHECKED, NULL, 0,
      "form_checkable.set_checked" },
    { SM_FAMILY_FORM_CHECKABLE, SM_VC_RADIO, SM_STATE_ANY, SM_EV_FORM_UNCHECK_RADIO_GROUP,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CHECKABLE_UNCHECKED),
      SM_ACT_WRITE_CHECKED | SM_ACT_UNCHECK_RADIO_GROUP, NULL, 0,
      "form_checkable.uncheck_radio_group" },
    { SM_FAMILY_FORM_CHECKABLE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_DISABLED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CHECKABLE_VALUE), 0, NULL, 0,
      "form_checkable.set_disabled" },
    { SM_FAMILY_FORM_CHECKABLE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_REQUIRED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CHECKABLE_VALUE), 0, NULL, 0,
      "form_checkable.set_required" },

    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_SELECTED_INDEX,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_SAME), 0, NULL, 0,
      "form_select.set_selected_index" },
    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_HOVER_INDEX,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_SAME), 0, NULL, 0,
      "form_select.set_hover_index" },
    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_DISABLED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_SAME), 0, NULL, 0,
      "form_select.set_disabled" },
    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_REQUIRED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_SAME), 0, NULL, 0,
      "form_select.set_required" },
    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_DROPDOWN_OPEN,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_OPEN), 0, NULL, 0,
      "form_select.dropdown_open" },
    { SM_FAMILY_FORM_SELECT, SM_VC_ANY, SM_STATE_ANY, SM_EV_DROPDOWN_CLOSE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_SELECT_CLOSED), 0, NULL, 0,
      "form_select.dropdown_close" },

    { SM_FAMILY_FORM_RANGE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_RANGE_VALUE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RANGE_SAME), 0, NULL, 0,
      "form_range.set_value" },
    { SM_FAMILY_FORM_RANGE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_DISABLED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RANGE_SAME), 0, NULL, 0,
      "form_range.set_disabled" },
    { SM_FAMILY_FORM_RANGE, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_REQUIRED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RANGE_SAME), 0, NULL, 0,
      "form_range.set_required" },

    { SM_FAMILY_FORM_TEXT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_VALUE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_VALUE), 0, NULL, 0,
      "form_text.set_value" },
    { SM_FAMILY_FORM_TEXT, SM_VC_TEXT_CONTROL, SM_STATE_ANY, SM_EV_FORM_REPLACE_TEXT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_VALUE),
      SM_ACT_DISPATCH_BEFOREINPUT | SM_ACT_DISPATCH_INPUT, NULL, 0,
      "form_text.replace_text" },
    { SM_FAMILY_FORM_TEXT, SM_VC_TEXT_CONTROL, SM_STATE_ANY, SM_EV_FORM_HISTORY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_ANY),
      SM_ACT_DISPATCH_BEFOREINPUT | SM_ACT_DISPATCH_INPUT, NULL, 0,
      "form_text.history" },
    { SM_FAMILY_FORM_TEXT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_SELECTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_ANY), 0, NULL, 0,
      "form_text.set_selection" },
    { SM_FAMILY_FORM_TEXT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_DISABLED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_ANY), 0, NULL, 0,
      "form_text.set_disabled" },
    { SM_FAMILY_FORM_TEXT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_READONLY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_ANY), 0, NULL, 0,
      "form_text.set_readonly" },
    { SM_FAMILY_FORM_TEXT, SM_VC_ANY, SM_STATE_ANY, SM_EV_FORM_SET_REQUIRED,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_TEXT_ANY), 0, NULL, 0,
      "form_text.set_required" },

    { SM_FAMILY_DROPDOWN, SM_VC_ANY, SM_STATE_ANY, SM_EV_DROPDOWN_OPEN,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DROPDOWN_OPEN), 0, NULL, 0,
      "dropdown.open" },
    { SM_FAMILY_DROPDOWN, SM_VC_ANY, SM_STATE_ANY, SM_EV_DROPDOWN_CLOSE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DROPDOWN_CLOSED), 0, NULL, 0,
      "dropdown.close" },
    { SM_FAMILY_DROPDOWN, SM_VC_ANY, SM_STATE_ANY, SM_EV_DROPDOWN_SET_GEOMETRY,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_DROPDOWN_SAME), 0, NULL, 0,
      "dropdown.set_geometry" },

    { SM_FAMILY_CONTEXT_MENU, SM_VC_ANY, SM_STATE_ANY, SM_EV_CONTEXT_MENU_OPEN,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CONTEXT_OPEN), 0, NULL, 0,
      "context_menu.open" },
    { SM_FAMILY_CONTEXT_MENU, SM_VC_ANY, SM_STATE_ANY, SM_EV_CONTEXT_MENU_CLOSE,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CONTEXT_CLOSED), 0, NULL, 0,
      "context_menu.close" },
    { SM_FAMILY_CONTEXT_MENU, SM_VC_ANY, SM_STATE_ANY, SM_EV_CONTEXT_MENU_HOVER,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_CONTEXT_HOVER), 0, NULL, 0,
      "context_menu.hover" },

    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, SM_STATE_ANY, SM_EV_RICH_TRANSACTION,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_EDIT_SAME),
      0, NULL, 0,
      "rich_edit.transaction" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_IDLE, SM_EV_EDIT_TX_BEGIN,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_TX_OPEN), 0, NULL, 0,
      "rich_edit.tx_begin" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_TX_OPEN, SM_EV_EDIT_BEFOREINPUT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_BEFOREINPUT),
      SM_ACT_DISPATCH_BEFOREINPUT, NULL, 0,
      "rich_edit.beforeinput" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_BEFOREINPUT_DONE,
      SM_EV_EDIT_MUTATE_DOM, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_MUTATED),
      SM_ACT_MUTATE_DOM, NULL, 0,
      "rich_edit.mutate_dom" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_MUTATED,
      SM_EV_EDIT_SET_SELECTION, SM_GUARD_NONE,
      SM_RULE_TO(SM_TO_RICH_SELECTION_SET), SM_ACT_SET_SELECTION, NULL, 0,
      "rich_edit.set_selection" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_BEFOREINPUT_DONE,
      SM_EV_EDIT_INPUT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_INPUT),
      SM_ACT_DISPATCH_INPUT, NULL, 0,
      "rich_edit.input_after_beforeinput" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_MUTATED,
      SM_EV_EDIT_INPUT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_INPUT),
      SM_ACT_DISPATCH_INPUT, NULL, 0,
      "rich_edit.input_after_mutation" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_SELECTION_SET,
      SM_EV_EDIT_INPUT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_INPUT),
      SM_ACT_DISPATCH_INPUT, NULL, 0,
      "rich_edit.input_after_selection" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_TX_OPEN, SM_EV_EDIT_TX_ABORT,
      SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_IDLE), 0, NULL, 0,
      "rich_edit.tx_abort" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_BEFOREINPUT_DONE,
      SM_EV_EDIT_TX_COMMIT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_IDLE),
      0, NULL, 0,
      "rich_edit.tx_commit_after_beforeinput" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_MUTATED,
      SM_EV_EDIT_TX_COMMIT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_IDLE),
      0, NULL, 0,
      "rich_edit.tx_commit_after_mutation" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_SELECTION_SET,
      SM_EV_EDIT_TX_COMMIT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_IDLE),
      0, NULL, 0,
      "rich_edit.tx_commit_after_selection" },
    { SM_FAMILY_RICH_EDIT, SM_VC_ANY, RICH_EDIT_INPUT_DONE,
      SM_EV_EDIT_TX_COMMIT, SM_GUARD_NONE, SM_RULE_TO(SM_TO_RICH_IDLE),
      0, NULL, 0,
      "rich_edit.tx_commit_after_input" },
};

#undef SM_RULE_TO

static const uint32_t RADIANT_STATE_RULE_COUNT =
    (uint32_t)(sizeof(RADIANT_STATE_RULES) / sizeof(RADIANT_STATE_RULES[0]));

extern const StateInvariantBinding RADIANT_INVARIANTS[] = {
    { SM_FAMILY_FOCUS, SM_STATE_ANY, SM_INV_FOCUSED_TARGET, "focus.target" },
    { SM_FAMILY_FOCUS, SM_STATE_ANY, SM_INV_FOCUS_GRAPH, "focus.graph" },
    { SM_FAMILY_HOVER, SM_STATE_ANY, SM_INV_HOVER_GRAPH, "hover.graph" },
    { SM_FAMILY_ACTIVE, SM_STATE_ANY, SM_INV_ACTIVE_GRAPH, "active.graph" },
    { SM_FAMILY_DRAG_DROP, SM_STATE_ANY, SM_INV_DRAG_GRAPH, "drag.graph" },
    { SM_FAMILY_IME, SM_STATE_ANY, SM_INV_EDITING_INTERACTION, "editing.interaction" },
    { SM_FAMILY_SCROLL, SM_STATE_ANY, SM_INV_VIEW_STATE_REGISTRY, "view_state.registry" },
    { SM_FAMILY_SELECTION, SM_STATE_ANY, SM_INV_CARET_PROJECTION, "caret.projection" },
    { SM_FAMILY_SELECTION, SM_STATE_ANY, SM_INV_SELECTION_PROJECTION, "selection.projection" },
    { SM_FAMILY_FORM_TEXT, SM_STATE_ANY, SM_INV_TEXT_CONTROL_FOCUS, "text_control.focus" },
    { SM_FAMILY_DROPDOWN, SM_STATE_ANY, SM_INV_DROPDOWN_OVERLAY, "dropdown.overlay" },
    { SM_FAMILY_CONTEXT_MENU, SM_STATE_ANY, SM_INV_CONTEXT_MENU_OVERLAY, "context_menu.overlay" },
    { SM_FAMILY_DOCUMENT, SM_STATE_ANY, SM_INV_DIRTY_TRACKING, "dirty.tracking" },
    { SM_FAMILY_SELECTION, SM_STATE_ANY, SM_INV_DOM_SELECTION, "selection.dom" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_EDITING_INTERACTION, "rich_edit.interaction" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_EDITING_SURFACE, "rich_edit.editing_surface" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_EDITING_SELECTION_HOST, "rich_edit.selection_host" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_EDITING_FALSE_ISLAND, "rich_edit.false_island" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_EDITING_TARGET_RANGES, "rich_edit.target_ranges" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_DOM_SELECTION_CACHE, "rich_edit.dom_selection_cache" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_SELECTION_PROJECTION_CACHE, "rich_edit.selection_projection_cache" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_INPUT_EVENT_ORDER, "rich_edit.input_event_order" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_CARET_PROJECTION, "rich_edit.caret" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_SELECTION_PROJECTION, "rich_edit.selection" },
    { SM_FAMILY_RICH_EDIT, SM_STATE_ANY, SM_INV_DOM_SELECTION, "rich_edit.dom_selection" },
};

extern const uint32_t RADIANT_INVARIANT_COUNT =
    (uint32_t)(sizeof(RADIANT_INVARIANTS) / sizeof(RADIANT_INVARIANTS[0]));

[[maybe_unused]] static const char* sm_family_name(SmFamily family) {
    switch (family) {
        case SM_FAMILY_DOCUMENT: return "document";
        case SM_FAMILY_FOCUS: return "focus";
        case SM_FAMILY_SELECTION: return "selection";
        case SM_FAMILY_IME: return "ime";
        case SM_FAMILY_HOVER: return "hover";
        case SM_FAMILY_ACTIVE: return "active";
        case SM_FAMILY_DRAG_DROP: return "drag_drop";
        case SM_FAMILY_SCROLL: return "scroll";
        case SM_FAMILY_FORM_CHECKABLE: return "form_checkable";
        case SM_FAMILY_FORM_SELECT: return "form_select";
        case SM_FAMILY_FORM_RANGE: return "form_range";
        case SM_FAMILY_FORM_TEXT: return "form_text";
        case SM_FAMILY_DROPDOWN: return "dropdown";
        case SM_FAMILY_CONTEXT_MENU: return "context_menu";
        case SM_FAMILY_RICH_EDIT: return "rich_edit";
        default: return "unknown";
    }
}

[[maybe_unused]] static const char* sm_event_name(SmEvent event) {
    switch (event) {
        case SM_EV_DOC_LOAD: return "doc_load";
        case SM_EV_DOC_COMMIT: return "doc_commit";
        case SM_EV_DOC_UNLOAD: return "doc_unload";
        case SM_EV_COLLAPSE_TO_BOUNDARY: return "collapse_to_boundary";
        case SM_EV_START_POINTER_SELECTION: return "start_pointer_selection";
        case SM_EV_UI_START_POINTER_SELECTION: return "ui_start_pointer_selection";
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
        case SM_EV_FOCUS_ELEMENT: return "focus_element";
        case SM_EV_BLUR_CURRENT: return "blur_current";
        case SM_EV_FOCUS_MOVE_FWD: return "focus_move_fwd";
        case SM_EV_FOCUS_MOVE_BACK: return "focus_move_back";
        case SM_EV_UI_FOCUS_WITH_BLUR: return "ui_focus_with_blur";
        case SM_EV_UI_FOCUS_WITH_CHANGE: return "ui_focus_with_change";
        case SM_EV_UI_BLUR_WITH_BLUR: return "ui_blur_with_blur";
        case SM_EV_UI_BLUR_WITH_CHANGE: return "ui_blur_with_change";
        case SM_EV_HOVER_SET: return "hover_set";
        case SM_EV_HOVER_CLEAR: return "hover_clear";
        case SM_EV_ACTIVE_SET: return "active_set";
        case SM_EV_ACTIVE_CLEAR: return "active_clear";
        case SM_EV_DRAG_SET_STATE: return "drag_set_state";
        case SM_EV_DRAG_BEGIN_DROP: return "drag_begin_drop";
        case SM_EV_DRAG_UPDATE_MOTION: return "drag_update_motion";
        case SM_EV_DRAG_SET_DROP_ACTIVE: return "drag_set_drop_active";
        case SM_EV_DRAG_SET_DROP_TARGET: return "drag_set_drop_target";
        case SM_EV_DRAG_CLEAR_DROP: return "drag_clear_drop";
        case SM_EV_SCROLL_SET_POSITION: return "scroll_set_position";
        case SM_EV_SCROLL_SET_MAX: return "scroll_set_max";
        case SM_EV_SCROLLBAR_HOVER: return "scrollbar_hover";
        case SM_EV_SCROLLBAR_BEGIN_DRAG: return "scrollbar_begin_drag";
        case SM_EV_SCROLLBAR_CLEAR_DRAG: return "scrollbar_clear_drag";
        case SM_EV_FORM_SET_CHECKED: return "form_set_checked";
        case SM_EV_FORM_UNCHECK_RADIO_GROUP: return "form_uncheck_radio_group";
        case SM_EV_FORM_SET_VALUE: return "form_set_value";
        case SM_EV_FORM_REPLACE_TEXT: return "form_replace_text";
        case SM_EV_FORM_HISTORY: return "form_history";
        case SM_EV_FORM_SET_SELECTION: return "form_set_selection";
        case SM_EV_FORM_SET_SELECTED_INDEX: return "form_set_selected_index";
        case SM_EV_FORM_SET_RANGE_VALUE: return "form_set_range_value";
        case SM_EV_FORM_SET_HOVER_INDEX: return "form_set_hover_index";
        case SM_EV_FORM_SET_DISABLED: return "form_set_disabled";
        case SM_EV_FORM_SET_READONLY: return "form_set_readonly";
        case SM_EV_FORM_SET_REQUIRED: return "form_set_required";
        case SM_EV_DROPDOWN_OPEN: return "dropdown_open";
        case SM_EV_DROPDOWN_CLOSE: return "dropdown_close";
        case SM_EV_DROPDOWN_SET_GEOMETRY: return "dropdown_set_geometry";
        case SM_EV_CONTEXT_MENU_OPEN: return "context_menu_open";
        case SM_EV_CONTEXT_MENU_CLOSE: return "context_menu_close";
        case SM_EV_CONTEXT_MENU_HOVER: return "context_menu_hover";
        case SM_EV_RICH_TRANSACTION: return "rich_transaction";
        case SM_EV_EDIT_TX_BEGIN: return "edit_tx_begin";
        case SM_EV_EDIT_BEFOREINPUT: return "edit_beforeinput";
        case SM_EV_EDIT_MUTATE_DOM: return "edit_mutate_dom";
        case SM_EV_EDIT_SET_SELECTION: return "edit_set_selection";
        case SM_EV_EDIT_INPUT: return "edit_input";
        case SM_EV_EDIT_TX_COMMIT: return "edit_tx_commit";
        case SM_EV_EDIT_TX_ABORT: return "edit_tx_abort";
        default: return "unknown";
    }
}

static int sm_derive_selection_state(DocState* state) {
    if (!state) return SEL_EMPTY;
    if (state->selection && state->selection->is_selecting) {
        return SEL_POINTER_SELECTING;
    }
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        if (state->sel.start_u16 == state->sel.end_u16) return SEL_CARET_COLLAPSED;
        return state->sel.direction == DOM_SEL_DIR_BACKWARD ?
            SEL_RANGE_BACKWARD : SEL_RANGE_FORWARD;
    }
    DomSelection* selection = state->dom_selection;
    if (!selection || selection->range_count == 0) return SEL_EMPTY;
    if (dom_selection_is_collapsed(selection)) return SEL_CARET_COLLAPSED;
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

static int sm_derive_focus_state(DocState* state) {
    if (!state || !state->focus) return FOCUS_NO_DOCUMENT;
    View* current = state->focus->current;
    if (!current) return FOCUS_DOC_ACTIVE_NONE;
    if (current->is_element()) {
        DomElement* elem = lam::dom_require_element(current);
        if (elem && tc_is_text_control(elem)) return FOCUS_TEXT_CONTROL;
        EditingHost host;
        if (editing_host_lookup(elem, &host) && host.host == elem) {
            return FOCUS_CONTENTEDITABLE;
        }
    }
    return FOCUS_ELEMENT;
}

static int sm_derive_hover_state(DocState* state) {
    return state && state->hover_target ? HOVER_TARGET : HOVER_NONE;
}

static int sm_derive_active_state(DocState* state) {
    return state && state->active_target ? ACTIVE_PRESSED : ACTIVE_NONE;
}

static int sm_derive_drag_state(DocState* state) {
    if (!state) return DRAG_IDLE;
    DragDropState* drag_drop = state->drag_drop;
    if (drag_drop && drag_drop->source_view) {
        if (drag_drop->drop_target) return DRAG_OVER_TARGET;
        if (drag_drop->active) return DRAG_ACTIVE;
        if (drag_drop->pending) return DRAG_PENDING;
    }
    if (state->is_dragging) return DRAG_ACTIVE;
    return DRAG_IDLE;
}

static int sm_derive_scroll_state(DocState* state, View* target) {
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, target, &interaction);
    if (interaction.h_dragging || interaction.v_dragging) return SCROLL_BAR_DRAGGING;
    if (interaction.h_hovered || interaction.v_hovered) return SCROLL_BAR_HOVER;
    return SCROLL_IDLE;
}

static int sm_derive_checkable_state(DocState* state, View* target) {
    return form_control_get_checked(state, target) ? CHK_CHECKED : CHK_UNCHECKED;
}

static int sm_derive_select_state(DocState* state, View* target) {
    return form_control_is_dropdown_open(state, target) ? SELCTL_OPEN : SELCTL_CLOSED;
}

static int sm_derive_range_state(DocState* state, View* target) {
    (void)state;
    (void)target;
    return RANGE_VALUE;
}

static int sm_derive_text_state(DocState* state, View* target) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t len = 0;
    form_control_get_selection(state, target, &start, &end, NULL);
    if (start != end) return TEXT_SELECTION;
    const char* value = form_control_get_value(state, target, &len);
    return value && len > 0 ? TEXT_VALUE : TEXT_EMPTY;
}

static int sm_derive_dropdown_state(DocState* state) {
    return state && state->open_dropdown ? DD_OPEN : DD_CLOSED;
}

static int sm_derive_context_menu_state(DocState* state) {
    if (!state || !state->context_menu_target) return CM_CLOSED;
    return state->context_menu_hover >= 0 ? CM_HOVER : CM_OPEN;
}

static int sm_derive_document_state(DocState* state) {
    return state ? state->lifecycle : DOC_LIFECYCLE_UNINITIALIZED;
}

static int sm_derive_rich_edit_state(DocState* state) {
    if (!state) return RICH_EDIT_IDLE;
    switch (state->editing.rich_transaction_phase) {
        case EDITING_RICH_TX_OPEN:
            return RICH_EDIT_TX_OPEN;
        case EDITING_RICH_TX_BEFOREINPUT:
            return RICH_EDIT_BEFOREINPUT_DONE;
        case EDITING_RICH_TX_MUTATED:
            return RICH_EDIT_MUTATED;
        case EDITING_RICH_TX_SELECTION_SET:
            return RICH_EDIT_SELECTION_SET;
        case EDITING_RICH_TX_INPUT:
            return RICH_EDIT_INPUT_DONE;
        case EDITING_RICH_TX_IDLE:
        default:
            break;
    }
    return RICH_EDIT_IDLE;
}

int sm_derive_state(DocState* state, SmFamily family, View* target) {
    switch (family) {
        case SM_FAMILY_DOCUMENT:
            return sm_derive_document_state(state);
        case SM_FAMILY_FOCUS:
            return sm_derive_focus_state(state);
        case SM_FAMILY_SELECTION:
            return sm_derive_selection_state(state);
        case SM_FAMILY_IME:
            return sm_derive_ime_state(state);
        case SM_FAMILY_HOVER:
            return sm_derive_hover_state(state);
        case SM_FAMILY_ACTIVE:
            return sm_derive_active_state(state);
        case SM_FAMILY_DRAG_DROP:
            return sm_derive_drag_state(state);
        case SM_FAMILY_SCROLL:
            return sm_derive_scroll_state(state, target);
        case SM_FAMILY_FORM_CHECKABLE:
            return sm_derive_checkable_state(state, target);
        case SM_FAMILY_FORM_SELECT:
            return sm_derive_select_state(state, target);
        case SM_FAMILY_FORM_RANGE:
            return sm_derive_range_state(state, target);
        case SM_FAMILY_FORM_TEXT:
            return sm_derive_text_state(state, target);
        case SM_FAMILY_DROPDOWN:
            return sm_derive_dropdown_state(state);
        case SM_FAMILY_CONTEXT_MENU:
            return sm_derive_context_menu_state(state);
        case SM_FAMILY_RICH_EDIT:
            return sm_derive_rich_edit_state(state);
        default:
            return 0;
    }
}

SmViewClass sm_classify_view(View* view) {
    if (!view) return SM_VC_ANY;
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && tc_is_text_control(elem)) return SM_VC_TEXT_CONTROL;
        FormControlProp* form = elem ? elem->form : NULL;
        if (form) {
            switch (form->control_type) {
                case FORM_CONTROL_CHECKBOX: return SM_VC_CHECKBOX;
                case FORM_CONTROL_RADIO: return SM_VC_RADIO;
                case FORM_CONTROL_SELECT: return SM_VC_SELECT;
                case FORM_CONTROL_RANGE: return SM_VC_RANGE;
                default: break;
            }
        }
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

[[maybe_unused]] static const StateTransitionRule* sm_find_rule(SmFamily family,
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

[[maybe_unused]] static bool sm_rule_allows_to_state(const StateTransitionRule* rule,
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
    scope->observed_actions = 0;
    scope->previous_scope = state ? state->sm_active_transition : NULL;
    scope->committed = false;
    if (state) state->sm_active_transition = scope;
}

void sm_transition_scope_commit(SmTransitionScope* scope) {
    if (scope) scope->committed = true;
}

void sm_observe_action(DocState* state, uint32_t action) {
#ifndef NDEBUG
    if (!state || !state->sm_active_transition) return;
    state->sm_active_transition->observed_actions |= action;
#else
    (void)state;
    (void)action;
#endif
}

void sm_transition_scope_end(SmTransitionScope* scope) {
#ifndef NDEBUG
    if (!scope) return;
    if (scope->state && scope->state->sm_active_transition == scope) {
        scope->state->sm_active_transition = scope->previous_scope;
    }
    if (!scope->committed) return;
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
    uint32_t missing_actions = rule->actions & ~scope->observed_actions;
    if (missing_actions != 0) {
        log_error("state_schema: missing action rule=%s family=%s event=%s missing=0x%x observed=0x%x",
            rule->name ? rule->name : "?",
            sm_family_name(scope->family), sm_event_name(scope->event),
            missing_actions, scope->observed_actions);
        assert(false && "Radiant state schema missing action/effect");
    }
#else
    (void)scope;
#endif
}
