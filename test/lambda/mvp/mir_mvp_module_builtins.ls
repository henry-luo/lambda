// D8.2.4/D7.6.1: descriptor-backed native calls retain their fixed Item
// argument slots, including five-argument procedures.
import dom
pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    let node = dom.query_selector(doc, "#first")
    let text = dom.first_child(node)
    let created = dom.create_node(doc, 3, null, "native")
    dom.set_attribute(node, "data-mvp", "ready")^
    let selection = dom.document_selection(doc)
    dom.set_base_and_extent(selection, text, 0, text, 2)^
    let bounds = dom.selection_boundaries(selection);
    [
        lower(string(dom.node_name(node))) == "li",
        dom.node_value(text) == "One",
        dom.matches(node, "#first"),
        dom.get_attribute(node, "data-mvp") == "ready",
        dom.node_value(created) == "native",
        dom.same_node(bounds.anchor_node, text), bounds.anchor_offset == 0,
        dom.same_node(bounds.focus_node, text), bounds.focus_offset == 2
    ]
}
