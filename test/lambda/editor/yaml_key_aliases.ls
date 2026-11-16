import y: lambda.editor.format.yaml
let text = "first: &key 1\nsecond: &other 2\n*key: value\n"
let loaded = y.import_text(text)^
let entry = loaded.doc.tree.children[2]
let changed = y.rename_key(loaded.doc, entry.first, entry.key_end, "*other")^
let invalid = y.rename_key(loaded.doc, entry.first, entry.key_end, "*missing") ^ { null }
let injected = y.rename_key(loaded.doc, entry.first, entry.key_end, "*key\ninjected") ^ { null };
[
  entry.name == "*key" and entry.literal_key == true,
  changed.doc.text == "first: &key 1\nsecond: &other 2\n*other: value\n",
  invalid == null,
  injected == null,
  (y.import_text("first: &key 1\n*key: a\n1: b\n") ^ { null }) == null
]
