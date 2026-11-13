import p: lambda.editor.format.key_value
let source = "; top\r\n[server]\r\nname = \"x;#y\" ; keep\r\nport=42 # keep\r\n"
let loaded = p.ini_descriptor.import_text(source)^
let name_node = loaded.doc.tree.children[0].node
let port_node = loaded.doc.tree.children[1].node
let changed = p.replace_scalar(loaded.doc, port_node.first, port_node.last, "99")^;
[
  name_node.text == "\"x;#y\"",
  loaded.doc.tree.children[1].group == "server",
  changed.doc.text == "; top\r\n[server]\r\nname = \"x;#y\" ; keep\r\nport=99 # keep\r\n",
  (p.rename_key(loaded.doc, 0, 0, ";hidden") ^ { null }) == null,
  (p.insert_child(loaded.doc, 0, "", "3") ^ { null }) == null
]
