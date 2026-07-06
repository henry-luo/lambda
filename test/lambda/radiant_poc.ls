import radiant;

let doc = radiant.load("test/js/dom_identity.html")
let root_node = radiant.root(doc)
let updated = radiant.set_attr(root_node, "data-poc", "ok")
let value = radiant.attr(updated, "data-poc")
[value, radiant.free(doc)][0]
