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

fn selection_value(node) {
    let elem = as_element(node);
    if (elem == null) "text" else (
        let value = dom.computed_style(elem, "user-select"),
        // auto uses the ancestor's selection behavior; an explicit text value overrides none.
        if (value == null or value == "" or value == "auto") selection_value(dom.parent_element(elem)) else value)
}

// The click-count and modifier table is policy. `selectLine` resolves to the
// current logical line on multiline controls and rich text; an input asks for
// the HTML select-all convention explicitly.
fn selection_operation(evt) {
    // A declined context-menu press still places its caret; an opened menu exits
    // before this default action.
    if (selection_value(evt.target) == "none") null
    else if (evt.shiftKey) "extend"
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

// A frame that names `data-selection-event` hears when a pointer gesture in its
// document settles, so its host can act on the new selection. Like link
// delegation, the event is queued in the embedding document, never run inline.
pub pn release(root) {
    let frame = dom.embedding_element(root)
    let delegated = if (frame == null) null else dom.get_attribute(frame, "data-selection-event");
    if (delegated != null and delegated != "") { dom.request_frame(frame, delegated) }
    return 'pass'
}
