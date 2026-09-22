import radiant
import dom

let doc = dom.load("test/js/dom_identity.html")
let root = dom.document_element(doc)
// dom.set_attribute answers nothing, as the DOM specifies, so these no longer
// chain: radiant.set_attr returned the node, which let each call feed the next.
let _id = dom.set_attribute(root, "id", "phase5-root")
let _class = dom.set_attribute(root, "class", "phase-five ready")
let _phase = dom.set_attribute(root, "data-phase", "5")
let updated = root

let doc_view = updated.owner_document
let queried_root = doc_view.document_element

{
    direct: [updated.id, updated.class_name, dom.get_attribute(updated, "data-phase"), dom.get_attribute(updated, "id")],
    document: [queried_root.id, queried_root.class_name, dom.get_attribute(queried_root, "data-phase")],
    free: radiant.free(doc)
}
