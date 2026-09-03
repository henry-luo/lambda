import radiant
import dom

let doc = dom.load("test/js/dom_identity.html")
let root_node = dom.document_element(doc)
// dom.set_attribute answers nothing, as the DOM specifies; radiant.set_attr
// returned the node so callers could chain. Read the node itself instead.
let _set = dom.set_attribute(root_node, "data-poc", "ok")
let value = dom.get_attribute(root_node, "data-poc");
[value, radiant.free(doc)][0]
