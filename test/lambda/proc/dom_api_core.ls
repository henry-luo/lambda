// The catalog's CORE surface from Lambda (ES39/ES44, F28): node creation,
// fragment parsing, node-value writes, direct VArray child lists and attribute
// enumeration. Everything here runs the same bodies the JS surface runs; the
// point of the test is that a Lambda-only script can drive them with no realm.
import dom

pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    let root = dom.document_element(doc)
    let body = dom.query_selector(root, "body")
    let intro = dom.get_element_by_id(root, "intro")

    // create_node is the one creator: element (1), text (3), comment (8), fragment (11)
    let section = dom.create_node(root, 1, "section", null)
    let note = dom.create_node(root, 3, null, "made in lambda")
    let remark = dom.create_node(root, 8, null, "a comment")
    dom.set_attribute(section, "id", "made")
    dom.set_attribute(section, "data-kind", "core")
    dom.append_child(section, note)
    dom.append_child(section, remark)
    dom.append_child(body, section)

    // parse_fragment parses in the context's document; its children move on append
    let frag = dom.parse_fragment(section, "<b>bold</b> and <i>italic</i>")
    let frag_kids = len(dom.child_nodes(frag))
    dom.append_child(section, dom.first_child(frag))

    // set_node_value rewrites a text node in one mutation
    dom.set_node_value(note, "rewritten")

    // D7.4.5v2: Lambda reads the same owner-backed child VArray as the DOM adapter.
    let kids_before = dom.child_nodes(section)
    dom.remove(remark)

    print(
        {
          section_type: dom.node_type(section),
          note_type: dom.node_type(note),
          remark_type: dom.node_type(remark),
          frag_type: dom.node_type(frag),
          frag_children_parsed: frag_kids,
          attribute_names: dom.attribute_names(section),
          held_child_nodes_len_after_remove: len(kids_before),
          child_nodes_now: len(dom.child_nodes(section)),
          children_now: len(dom.children(section)),
          first_child_value: dom.node_value(dom.first_child(section)),
          last_child_name: dom.node_name(dom.last_child(section)),
          serialized_inner: dom.serialize(section, false),
          serialized_outer: dom.serialize(section, true),
          by_id_roundtrip: dom.node_name(dom.get_element_by_id(root, "made")),
          parent_element_of_note: dom.node_name(dom.parent_element(note)),
          replaced: dom.node_name(dom.replace_child(section, dom.create_element(root, "em"), note)),
          inner_after_replace: dom.inner_html(section),
          matches_section: dom.matches(section, "section[data-kind=core]"),
          // S5.4.3 keeps `==` structural; DOM identity remains the same_node operation.
          structural_eq_other: dom.get_element_by_id(root, "made") == dom.get_element_by_id(root, "intro"),
          same_node_self: dom.same_node(section, dom.get_element_by_id(root, "made")),
          same_node_other: dom.same_node(section, intro),
          equal_node_clone: dom.equal_node(dom.create_element(root, "em"), dom.create_element(root, "em"))
        }
    )
}
