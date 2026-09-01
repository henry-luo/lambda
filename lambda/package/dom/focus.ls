// Sequential-focus policy (ES30). Native supplies a DOM-order eligibility
// snapshot and performs the canonical focus/scroll writes; this module owns
// HTML tabindex ordering and autofocus choice.
import radiant

fn before(a, b) {
    if (a.tab_index > 0 and b.tab_index <= 0) { true }
    else if (a.tab_index <= 0 and b.tab_index > 0) { false }
    else if (a.tab_index != b.tab_index) { (a.tab_index < b.tab_index) }
    else { (a.order < b.order) }
}

fn sequential(candidate) { candidate.sequential }

fn extremum(candidates, i, best, forward) {
    if (i >= len(candidates)) { best }
    else {
        let candidate = candidates[i];
        let better = best == null or
            (forward and before(candidate, best)) or
            (not forward and before(best, candidate));
        let next = if (sequential(candidate) and better) candidate else best;
        extremum(candidates, i + 1, next, forward)
    }
}

fn focused_candidate(candidates, i) {
    if (i >= len(candidates)) { null }
    else if (radiant.focused(candidates[i].node)) { candidates[i] }
    else { focused_candidate(candidates, i + 1) }
}

fn successor(candidates, current, i, best, forward) {
    if (i >= len(candidates)) { best }
    else {
        let candidate = candidates[i];
        let is_after = if (forward) before(current, candidate) else before(candidate, current);
        let better = best == null or
            (forward and before(candidate, best)) or
            (not forward and before(best, candidate));
        let next = if (sequential(candidate) and is_after and better) candidate else best;
        successor(candidates, current, i + 1, next, forward)
    }
}

fn next_candidate(candidates, forward) {
    let current = focused_candidate(candidates, 0);
    let after = if (current == null) null else successor(candidates, current, 0, null, forward);
    if (after != null) { after } else { extremum(candidates, 0, null, forward) }
}

fn autofocus_candidate(candidates, i) {
    if (i >= len(candidates)) { null }
    else if (radiant.has_attr(candidates[i].node, "autofocus")) { candidates[i] }
    else { autofocus_candidate(candidates, i + 1) }
}

pub pn navigate(root, evt) {
    let forward = not evt.shift;
    let target = next_candidate(radiant.focus_candidates(radiant.document_root(root)), forward);
    if (target == null) { 'pass' }
    else {
        radiant.focus_set(target.node, true)
        radiant.scroll_into_view(target.node)
    }
}

pub pn autofocus(root) {
    let target = autofocus_candidate(radiant.focus_candidates(radiant.document_root(root)), 0);
    if (target == null) { 'pass' }
    else {
        radiant.focus_set(target.node, false)
        radiant.scroll_into_view(target.node)
    }
}

// focusinit is behavior-only: document setup has no author-visible event, but
// the package still makes the policy decision through the ordinary registry.
view <html> {}
on focusinit(evt) { autofocus(~) }
