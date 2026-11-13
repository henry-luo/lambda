import p: lambda.editor.format.key_value

let ini = p.ini_descriptor.import_text("[main]\nvalue=old ; keep\n")^
let node = ini.doc.tree.children[0].node
let quoted = p.replace_scalar(ini.doc, node.first, node.last, "\"a;#b\"")^
let props = p.import_text("key=value\n")^
let prop = props.doc.tree.children[0].node;
[
  quoted.doc.tree.children[0].node.text == "\"a;#b\"",
  contains(quoted.doc.text, "\"a;#b\" ; keep"),
  (p.replace_scalar(ini.doc, node.first, node.last, "a;b") ^ { null }) == null,
  (p.replace_scalar(ini.doc, node.first, node.last, "a#b") ^ { null }) == null,
  (p.insert_child(ini.doc, 0, "new", "a;b") ^ { null }) == null,
  (p.replace_scalar(props.doc, prop.first, prop.last, "trailing ") ^ { null }) == null,
  (p.insert_child(props.doc, 0, "new", " leading") ^ { null }) == null,
  (p.replace_scalar(ini.doc, node.first, node.last + 1, "x") ^ { null }) == null
]
