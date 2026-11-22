// Exercise the shared Range parser without a JavaScript realm (D8.4.4 / D9.5.6v2).
import dom

pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    let intro = dom.query_selector(doc, "#intro")
    let r = dom.document_create_range(doc)
    dom.range_select_node_contents(r, intro)
    let fragment = dom.range_create_contextual_fragment(r, "<tr><td>x</td></tr><b>y</b>")
    let bounds = dom.range_boundaries(r)
    let body = dom.create_node(doc, 1, "tbody", "")
    dom.range_select_node_contents(r, body)
    let rows = dom.range_create_contextual_fragment(r, "<tr><td>cell</td></tr>")
    print({kind: dom.node_type(fragment), text: dom.text_content(fragment),
        owner: dom.same_node(dom.owner_document(fragment), doc),
        start: dom.same_node(bounds.start_container, intro), end: bounds.end_offset,
        preserved: dom.text_content(intro), table_text: dom.text_content(rows),
        table_name: dom.node_name(dom.first_child(rows)),
        table_owner: dom.same_node(dom.owner_document(dom.first_child(rows)), doc),
        context_empty: dom.first_child(body) == null})
}
