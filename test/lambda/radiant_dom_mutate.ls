import radiant;

let doc = radiant.load("test/js/dom_identity.html")
let root = radiant.root(doc)
let with_id = radiant.set_attr(root, "id", "phase5-root")
let with_class = radiant.set_attr(with_id, "class", "phase-five ready")
let updated = radiant.set_attr(with_class, "data-phase", "5")

let doc_view = updated.owner_document
let queried_root = doc_view.document_element

{
    direct: [updated.id, updated.class_name, radiant.attr(updated, "data-phase"), radiant.attr(updated, "id")],
    document: [queried_root.id, queried_root.class_name, radiant.attr(queried_root, "data-phase")],
    free: radiant.free(doc)
}
