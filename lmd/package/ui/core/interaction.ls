import dom
import c: lambda.ui.core.component

// nested components must not interpret each other's bubbling DOM events.
pub pn root(node, evt, family = "dtna") {
    let selector = "." ++ family ++ "-" ++ string(c.kind(node))
    let found = dom.closest(evt.target, selector)
    let owner = dom.closest(evt.currentTarget, selector);
    // native dispatch position scopes delegation even when nested roots omit HTML ids.
    if (found != null and found == owner) found else null
}
pub pn target(node, evt, selector, family = "dtna") {
    let found = dom.closest(evt.target, selector)
    let owner = root(node, evt, family);
    if (found != null and owner != null and
        dom.closest(found,"." ++ family ++ "-" ++ string(c.kind(node))) == owner) found else null
}
pub pn focus(node, evt, selector, family = "dtna") {
    let owner = root(node, evt, family);
    if (owner != null) dom.focus_set(dom.query_selector(owner, selector), true) else false
}
