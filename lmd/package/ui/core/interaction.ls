import dom

// nested components must not interpret each other's bubbling DOM events.
pub pn root(node, evt) {
    let selector = ".dtna-" ++ string(node.kind)
    let found = dom.closest(evt.target, selector)
    let owner = dom.closest(evt.currentTarget, selector);
    // native dispatch position scopes delegation even when nested roots omit HTML ids.
    if (found != null and found == owner) found else null
}
pub pn target(node, evt, selector) {
    let found = dom.closest(evt.target, selector)
    let owner = root(node, evt);
    if (found != null and owner != null and
        dom.closest(found,".dtna-" ++ string(node.kind)) == owner) found else null
}
pub pn focus(node, evt, selector) {
    let owner = root(node, evt);
    if (owner != null) dom.focus_set(dom.query_selector(owner, selector), true) else false
}
