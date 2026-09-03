// Context-menu policy (F10, ES18 scope rule).
//
// The menu binds at <body> for the same reason the IME session does: a document
// has at most one open context menu, so the cardinality of the state is the
// document's, not the control's. Storage is already document-scoped in DocState;
// what moved here is the two decisions native used to make — which target
// deserves a menu, and which of its items are live.
//
// The five commands themselves did NOT need moving: cut, delete, paste and
// select-all already route through `dispatch_form_text_replace` /
// `dispatch_form_text_paste` / `dispatch_form_select_all`, which fire
// `beforeinput` and land in editing.apply. So the exec half has been package
// policy since F5; only the gate in front of it was still native.
import dom

// One bit per item, in CtxMenuItem order (event.hpp): Cut, Copy, Paste,
// Delete, Select All. Written as literals because Lambda has no shift operator,
// which reads better here anyway — the mapping to the enum is visible.
let CUT = 1
let COPY = 2
let PASTE = 4
let DELETE = 8
let SELECT_ALL = 16

fn bit(present, value) { if (present) value else 0 }

// The enable rules. Evaluated once when the menu opens, which is what lets the
// paint pass read a cached mask instead of calling into Lambda per item — the
// quiescence rule F8 exists to protect. Caching is sound here because the menu
// is modal: nothing these rules read can change while it is up.
pub fn enabled_mask(elem, has_clip) {
    if (dom.get_state(elem, "disabled")) { 0 }
    else {
        let readonly = dom.get_state(elem, "readonly");
        let has_sel = dom.tc_selection_start(elem) != dom.tc_selection_end(elem);
        let value = dom.get_state(elem, "value");
        let has_val = value != null and len(value) > 0;
        bit(has_sel and not readonly, CUT) +
        bit(has_sel, COPY) +
        bit(has_clip and not readonly, PASTE) +
        bit(has_sel and not readonly, DELETE) +
        bit(has_val, SELECT_ALL)
    }
}

// Which targets get a menu. Native used to hard-code `tc_is_text_control`; the
// rule is the same for now, but it is a rule rather than a gate, so widening it
// to contenteditable or a plain document selection is a change here rather than
// in C++.
pub pn open_for(body) {
    let target = dom.context_menu_target(body);
    if (target == null) { 'pass' }
    else if (not dom.tc_value(target)) { 'pass' }
    else {
        let clip = dom.clipboard_text();
        dom.open_context_menu(target, enabled_mask(target, clip != null))
    }
}
