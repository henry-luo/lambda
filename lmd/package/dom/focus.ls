// Sequential-focus policy (ES30). Native supplies a DOM-order eligibility
// snapshot and performs the canonical focus/scroll writes; this module owns
// HTML tabindex ordering and autofocus choice.
import dom

import order: lambda.dom.focus_order

pub let next_candidate = order.next_candidate

fn autofocus_candidate(candidates, i) {
    if (i >= len(candidates)) { null }
    else if (dom.has_attribute(candidates[i].node, "autofocus")) { candidates[i] }
    else { autofocus_candidate(candidates, i + 1) }
}

pub pn navigate(root, evt) {
    let forward = not evt.shift;
    let target = next_candidate(dom.focus_candidates(dom.document_element(root)), forward);
    if (target == null) { 'pass' }
    else {
        dom.scroll_into_view(target.node)^
        target.node
    }
}

pub pn autofocus(root) {
    let target = autofocus_candidate(dom.focus_candidates(dom.document_element(root)), 0);
    if (target == null) { 'pass' }
    else {
        dom.scroll_into_view(target.node)^
        target.node
    }
}

// focusinit is behavior-only: document setup has no author-visible event, but
// the package still makes the policy decision through the ordinary registry.
view <html> {}
on focusinit(evt) { autofocus(~) }
