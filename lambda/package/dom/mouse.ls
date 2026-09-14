// Mouse default-action policy. S12.1.3 keeps the gesture decision here;
// native resolves the chosen operation against live layout and selection state.
import dom

fn as_element(node) {
    if (node == null) null
    else if (dom.node_type(node) == 1) node
    else dom.parent_element(node)
}

// A negative tabindex is excluded from sequential focus but remains a valid
// mouse-focus target, so the predicate is deliberately programmatic focus.
fn focus_target(node) {
    let elem = as_element(node);
    if (elem == null) null
    else if (dom.is_focusable(elem)) elem
    else focus_target(dom.parent_element(elem))
}

fn input_target(node) {
    let elem = as_element(node);
    if (elem == null) null else dom.closest(elem, "input")
}

// The click-count and modifier table is policy. `selectLine` resolves to the
// current logical line on multiline controls and rich text; an input asks for
// the HTML select-all convention explicitly.
fn selection_operation(evt) {
    // A declined context-menu press still places its caret; an opened menu exits
    // before this default action.
    if (evt.shiftKey) "extend"
    else if (evt.detail >= 3) {
        if (input_target(evt.target) != null) "selectAll" else "selectLine"
    }
    else if (evt.detail == 2) "selectWord"
    else "select"
}

pub pn press(root, evt) {
    // A non-focusable press clears the old focus through the same state-machine
    // transition, matching a document-text caret placement.
    dom.mouse_focus(focus_target(evt.target))
    let operation = selection_operation(evt);
    if (operation == null) 'pass' else dom.pointer_selection(root, operation)
}
