import y: lambda.editor.format.yaml
let scalar = y.import_text("--- hello\n")^
let scalar_changed = y.replace_scalar(scalar.doc, scalar.doc.tree.first, scalar.doc.tree.last, "world")^
let flow = y.import_text("--- [1, 2]\n")^
let flow_added = y.insert_child(flow.doc, flow.doc.tree.first, "", "3")^
let object_doc = y.import_text("--- {a: 1}\n")^
let map_added = y.insert_child(object_doc.doc, object_doc.doc.tree.first, "b", "2")^
let inline_mapping = y.import_text("--- a: 1\nb: 2\n")^
let field = inline_mapping.doc.tree.children[0].node
let changed = y.replace_scalar(inline_mapping.doc, field.first, field.last, "3")^;
let spaced = y.import_text("---  # first\na: 1\n...\t# end\n---\t # next\nb: 2\n")^
let spaced_field = spaced.doc.tree.children[1].node.children[0].node
let spaced_change = y.replace_scalar(spaced.doc, spaced_field.first, spaced_field.last, "4")^;
[
  scalar.doc.tree.text == "hello" and scalar_changed.doc.text == "--- world\n",
  flow.doc.tree.kind == 'array' and flow_added.doc.text == "--- [1, 2, 3]\n",
  object_doc.doc.tree.kind == 'object' and map_added.doc.text == "--- {a: 1, \"b\": 2}\n",
  len(inline_mapping.doc.tree.children) == 2 and changed.doc.text == "--- a: 3\nb: 2\n",
  parse(changed.doc.text, 'yaml')^.b == 2,
  len(spaced.doc.tree.children) == 2 and spaced_change.doc.text == "---  # first\na: 1\n...\t# end\n---\t # next\nb: 4\n"
]
