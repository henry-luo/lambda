import radiant
import dom

let doc = dom.load("test/js/dom_identity.html")
let root_node = dom.document_element(doc)
let _freed = radiant.free(doc)
dom.get_attribute(root_node, "id")
