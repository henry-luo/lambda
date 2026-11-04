// Modal focus uses the same eligibility and tabindex ordering as native Tab (D7.2.5).
import dom
import focus: lambda.dom.focus

pub pn trap(root,evt) {
    if (root==null) { return false }
    let target = focus.next_candidate(dom.focus_candidates(root),not evt.shiftKey)^
    if (target==null) { return false }
    dom.focus_set(target.node,false)
    return true
}

// Return a dismissal request to the app; it decides whether and where focus returns.
pub pn dialog_key(root,evt) {
    if (evt.key=="Tab") { return if (trap(root,evt)^) 'handled' else 'pass' }
    return if (evt.key=="Escape") 'dismiss' else 'pass'
}
