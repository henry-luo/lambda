// Fragment insertion reaches the shared DOM transaction without a JS realm (D9.5.6v2).
import dom

pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    let host = dom.query_selector(doc, "#intro")
    let fragment = dom.parse_fragment(host, "<b>bold</b> tail")
    dom.insert_before(host, fragment, dom.first_child(host))
    let inserted = dom.text_content(host)
    let tail = dom.parse_fragment(host, "<i>end</i>")
    dom.append_child(host, tail)
    print({inserted: inserted, appended: dom.text_content(host),
        empty: dom.first_child(fragment) == null and dom.first_child(tail) == null,
        owner: dom.same_node(dom.owner_document(dom.first_child(host)), doc)})
}
