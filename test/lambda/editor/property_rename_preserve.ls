import p: lambda.editor.format.key_value

let source = "# keep\r\nname = one\r\nname=two\r\n"
let loaded = p.import_text(source)^
let entry = loaded.doc.tree.children[0]
let renamed = p.rename_key(loaded.doc, entry.first, entry.key_end, "名")^;
[
  renamed.doc.text == "# keep\r\n名 = one\r\nname=two\r\n",
  renamed.doc.tree.children[0].name == "名",
  (p.rename_key(loaded.doc, entry.first, entry.key_end - 1, "x") ^ { null }) == null,
  (p.rename_key(loaded.doc, entry.first + 1, entry.key_end, "x") ^ { null }) == null,
  (p.rename_key(loaded.doc, entry.first, entry.node.last, "x") ^ { null }) == null
]
