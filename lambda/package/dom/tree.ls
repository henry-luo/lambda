// Generic tree walkers, in Lambda (ES30, and the F32 ruling: as much as
// possible under Lambda, as little as possible native).
//
// form_of, radio_group and details_group were native `radiant.*` bodies. None
// of them is mechanism: each is *policy* expressed over ordinary DOM reads --
// which element owns this control, which radios share a group, which details
// share a name -- so each belongs here, where it can be read and changed
// without touching the engine.
//
// The definitions are the derivations recorded in lambda/dom/dom_api.def; the
// oracle test/lambda/dom_derive_forms.ls holds these and the native bodies to
// the same answers.
import dom

// same_node answers on two nodes; it is not an equality over absence, and
// `same_node(null, null)` is not true. Comparing form owners has to say so
// explicitly, or every control with no owning form fails to match every other
// one -- which silently emptied radio groups on form-less radios.
fn same_owner(a, b) { if (a == null or b == null) (a == null and b == null) else dom.same_node(a, b) }

// The form an element belongs to: an explicit form="id" wins over the
// enclosing <form>, per HTML's form-owner rules.
pub fn form_of(node) {
    if (dom.has_attribute(node, "form"))
        dom.get_element_by_id(dom.root_node(node), dom.get_attribute(node, "form"))
    else dom.closest(node, "form")
}

// Radios share a group when they share a name AND a form owner. Scoping the
// query to the form when there is one keeps an unrelated same-named radio in
// another form out of the group.
//
// The subject is INCLUDED here, and excluded from details_group below. That
// asymmetry is the established contract, not an oversight: the radio caller
// unchecks the whole group and then checks the subject, so including it costs
// nothing; the details caller closes the group and would close the element it
// just opened. The oracle pins both, because the catalog's derivations had
// guessed the wrong one in each case.
pub fn radio_group(node) {
    let owner = form_of(node);
    let scope = if (owner != null) owner else dom.root_node(node);
    let group = dom.get_attribute(node, "name");
    let peers = dom.query_selector_all(scope, "input[type=radio]");
    [for (peer in peers where dom.get_attribute(peer, "name") == group and same_owner(form_of(peer), owner)) peer]
}

// <details name=...> forms an exclusive accordion group across the document.
// The subject is excluded, as above. An *empty* name is not a group name -- two
// `<details name="">` are unrelated, not peers -- which is the distinction
// between "no attribute" and "attribute present but empty".
pub fn details_group(node) {
    let group = dom.get_attribute(node, "name");
    let peers = dom.query_selector_all(dom.root_node(node), "details[name]");
    if (group == null or group == "") [] else [for (peer in peers where dom.get_attribute(peer, "name") == group and not dom.same_node(peer, node)) peer]
}

// ---------------------------------------------------------------------------
// Composites over the engine's own reads (F32).
//
// These were catalog rows flagged as engine work with no body. Each is simply a
// pair or a triple of reads the engine already publishes, so none of them is
// mechanism: a row that can be written over other rows is derived, not part of
// the ABI. Writing them here rather than natively is the same rule as the
// walkers above -- the engine keeps what only it can know, and the shape of the
// answer is Lambda's.
// ---------------------------------------------------------------------------

// A text control's selection as one value, rather than two calls that can
// disagree if the control changes between them.
pub fn tc_selection(node) {
    { start: dom.tc_selection_start(node), end: dom.tc_selection_end(node) }
}

// Setting a text control's value is a state write; there is nothing else to it.
pub fn tc_set_value(node, value) { dom.set_state(node, "value", value) }

// Where an edit would land in a contenteditable host: the resolved node and the
// offsets within it. Null when there is no editing position at all, so callers
// test one thing instead of three.
pub fn edit_range(host) {
    let node = dom.edit_node(host);
    if (node == null) null else { node: node, start: dom.edit_start(host), end: dom.edit_end(host) }
}
