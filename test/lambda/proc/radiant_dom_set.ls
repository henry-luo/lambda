import radiant
import dom

pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    let root = dom.document_element(doc)
    root.set("id", "phase5-root")
    root.set("class_name", "phase-five ready")
    root.set("data-phase", "5")

    let doc_view = root.owner_document
    let queried_root = doc_view.document_element

    print([root.id, root.class_name, dom.get_attribute(root, "data-phase"), dom.get_attribute(root, "id")])
    print("\n")
    print([queried_root.id, queried_root.class_name, dom.get_attribute(queried_root, "data-phase")])
    print("\n")
    print(radiant.free(doc))
}
