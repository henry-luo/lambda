import dom

// nested components must not interpret each other's bubbling DOM events.
pub pn root(node, evt) {
    let found = dom.closest(evt.target, ".dtna-" ++ string(node.kind));
    if (found != null and dom.get_attribute(found, "id") == node.props.id) found else null
}
pub pn target(node, evt, selector) {
    let found = dom.closest(evt.target, selector);
    if (found != null and root(node, evt) != null and root(node, {target:found}) != null) found else null
}
pub pn focus(node, evt, selector) {
    let owner = root(node, evt);
    if (owner != null) dom.focus_set(dom.query_selector(owner, selector), true) else false
}
