import radiant;

let doc = radiant.load("test/js/dom_identity.html")
let root = radiant.root(doc)
let body = root.last_element_child
let main = body.first_element_child
let intro = main.first_element_child

{
  root_name: root.node_name,
  local_name: root.local_name,
  node_type: root.node_type,
  is_connected: root.is_connected,
  child_count: root.child_element_count,
  first_name: root.first_element_child.node_name,
  last_name: root.last_element_child.node_name,
  next_name: root.first_element_child.next_element_sibling.node_name,
  parent_name: main.parent_node.node_name,
  text_name: intro.first_child.node_name,
  text_value: intro.first_child.node_value,
  document_root: root.owner_document.document_element.node_name,
  keys: [for (k at root where k == 'node_name' or k == 'class_name' or k == 'first_child' or k == 'owner_document') k],
  node: type(root),
  free: radiant.free(doc)
}
