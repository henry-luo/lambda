import y: lambda.editor.format.yaml
let text = "1: one\ntrue: yes\nnull: none\n'1': string\n"
let loaded = y.import_text(text)^
let first = loaded.doc.tree.children[0]
let changed = y.rename_key(loaded.doc, first.first, first.key_end, "2")^
let named = y.rename_key(loaded.doc, first.first, first.key_end, "other")^
let duplicate = y.rename_key(loaded.doc, first.first, first.key_end, "true") ^ { null }
let injection = y.rename_key(loaded.doc, first.first, first.key_end, "2, 3") ^ { null };
[
  len(loaded.doc.tree.children) == 4,
  first.name == "1" and first.literal_key == true,
  changed.doc.text == "2: one\ntrue: yes\nnull: none\n'1': string\n",
  named.doc.text == "other: one\ntrue: yes\nnull: none\n'1': string\n",
  duplicate == null,
  injection == null,
  (y.import_text("1: one\n0x1: duplicate\n") ^ { null }) == null
]
