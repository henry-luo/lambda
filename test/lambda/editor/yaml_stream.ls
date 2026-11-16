import y: lambda.editor.format.yaml
let text = "%YAML 1.2\r\n--- # first\r\na: &x 1 # keep\r\nb: *x\r\n...\r\n---\r\na: &x 2\r\nb: *x\r\n"
let loaded = y.import_text(text)^
let first = loaded.doc.tree.children[0].node
let second = loaded.doc.tree.children[1].node
let changed = y.replace_scalar(loaded.doc, second.children[0].node.first, second.children[0].node.last, "&x 3")^
let renamed = y.rename_key(loaded.doc, first.children[0].first, first.children[0].key_end, "name")^
let added = y.insert_child(loaded.doc, first.first, "added", "5")^
let invalid = y.replace_scalar(loaded.doc, second.children[0].node.first, second.children[0].node.last, "&other 2") ^ { null }
let parsed = parse(changed.doc.text, 'yaml')^
let multiline = y.import_text("text: |\n  ---\n  payload\n---\nnext: 1\n")^
let scalar = y.import_text("  value\n")^;
[
  len(loaded.doc.tree.children) == 2,
  first.children[1].node.text == "*x" and second.children[0].node.text == "&x 2",
  changed.doc.text == "%YAML 1.2\r\n--- # first\r\na: &x 1 # keep\r\nb: *x\r\n...\r\n---\r\na: &x 3\r\nb: *x\r\n",
  parsed[0].b == 1 and parsed[1].b == 3,
  contains(renamed.doc.text, "\"name\": &x 1 # keep\r\n"),
  contains(added.doc.text, "\"added\": 5\r\n...\r\n---"),
  invalid == null,
  len(multiline.doc.tree.children) == 2,
  scalar.doc.tree.text == "value" and scalar.doc.tree.first == 2,
  (y.import_text("---\na: &x 1\n---\nb: *x\n") ^ { null }) == null
]
