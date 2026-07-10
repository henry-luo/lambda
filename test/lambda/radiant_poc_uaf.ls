import radiant;

let doc = radiant.load("test/js/dom_identity.html")
let root_node = radiant.root(doc)
let _freed = radiant.free(doc)
radiant.attr(root_node, "id")
