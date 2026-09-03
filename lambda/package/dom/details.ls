// UA behavior for <details> / <summary> — HTML 4.11.1 (ESO56).
//
// Openness is the `open` content attribute and nothing else: HTML defines no
// separate internal state for it, unlike a checkbox, whose `checked` attribute
// is only the default behind a distinct checkedness. So this template claims no
// engine state — the whole default action is one attribute write, and the
// engine already reads the attribute everywhere it matters
// (layout_node_is_hidden_by_closed_details and the synthetic-summary sizing in
// layout_block.cpp, plus the disclosure marker in resolve_htm_style.cpp).
// The reflow follows from the mutation notice `set_attr` carries.
import dom
import tree: lambda.package.dom.tree

// HTML 4.11.1 exclusive accordion: details sharing a non-empty `name` in one
// node tree may have at most one member open, so opening one closes the rest.
//
// The spec hangs this on the `open` attribute's change steps, which also catch
// `d.open = true` and setAttribute; here it runs where the package decides
// openness, which is the click. A script write therefore does not close the
// group — the same residue radio exclusivity carries (DOM_Default §5.2), and
// for a concrete reason: there is no named-attribute-change seam to claim. Of
// the ~20 DOM_JS_MUTATION_ATTRIBUTE notify sites most pass no attribute name,
// so a hook there would hand Lambda every attribute write in the document to
// filter. Nor is load-time exclusivity available: the spec's insertion rule
// wants the `init` hook, and that phase visits only form controls (EO4), which
// a <details> is not.
//
// details_group excludes the subject, so no node-identity test is needed to
// avoid closing the very element being opened, and it answers with an empty
// group for an unnamed details, which makes this a no-op rather than a case.
pn close_group_peers(details) {
    for (peer in tree.details_group(details)) {
        if (dom.has_attribute(peer, "open")) {
            dom.remove_attribute(peer, "open")
            // every element whose openness changed reports its own toggle
            dom.dispatch(peer, "toggle")
        }
    }
}

// HTML gives the disclosure control to a summary that is a *child* of a
// details; a summary nested deeper in the content is ordinary flow content.
// `details > summary` is exactly that rule, and matching it through the
// selector engine keeps this in step with resolve_css_style.cpp, which decides
// the disclosure marker with the same direct-child test.
//
// Deviation, deliberate and narrow: the spec restricts activation to the
// *first* summary child, while this accepts any direct summary child — the same
// place the marker rule stops. The selector engine has no `:first-of-type`, so
// tightening both together is one change once it does. A second summary is
// invisible while the details is closed, so the two rules can only differ on an
// already-open details.
pub pn toggle_from_summary(summary) {
    if (dom.closest(summary, "details > summary") == null) { 'pass' }
    else {
        let details = dom.parent_element(summary);
        if (details == null) { 'pass' }
        else {
            // present-or-absent, like every other boolean content attribute:
            // null removes it, "" writes it. F7 declared set_attr's removal
            // half but fn_to_cstr never yielded nullptr, so it was dead until
            // this landed — a clear wrote an empty attribute instead.
            // has_attr, not attr: presence is the question being asked. A
            // boolean attribute's value is the empty string, so `attr(..) !=
            // null` answers a *value* question and its truth rests on whether
            // an empty value round-trips as "" rather than null.
            let was_open = dom.has_attribute(details, "open");
            if (was_open) { dom.remove_attribute(details, "open") }
            else {
                // close the group before opening, so the group is never
                // momentarily two-open and every peer's toggle precedes ours
                close_group_peers(details)
                dom.set_attribute(details, "open", "")
            }
            // HTML queues `toggle` on the element whose openness changed, which
            // is the details, not the summary that was clicked.
            dom.dispatch(details, "toggle")
        }
    }
}

view <summary> {}
on click(evt) { toggle_from_summary(~) }
