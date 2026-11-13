import p: lambda.editor.format.key_value
let source = "# keep\r\nempty=   \r\né=old  \r\né:second\r\n"
let loaded = p.import_text(source)^
let empty = loaded.doc.tree.children[0].node
let first = p.replace_scalar(loaded.doc, empty.first, empty.last, "hello")^
let duplicate = first.doc.tree.children[2].node
let second = p.replace_scalar(first.doc, duplicate.first, duplicate.last, "new")^
let removed = p.delete_child(second.doc, second.doc.tree.children[1].node.first)^
let added = p.insert_child(removed.doc, 0, "added", "true")^;
[
  first.doc.text == "# keep\r\nempty=hello   \r\né=old  \r\né:second\r\n",
  second.doc.text == "# keep\r\nempty=hello   \r\né=old  \r\né:new\r\n",
  removed.doc.text == "# keep\r\nempty=hello   \r\né:new\r\n",
  ends_with(added.doc.text, "added=true\n"),
  (p.import_text("a=one\\\ntwo\n") ^ { null }) == null,
  (p.rename_key(loaded.doc, 0, 0, "bad key") ^ { null }) == null,
  (p.rename_key(loaded.doc, 0, 0, "#comment") ^ { null }) == null
]
