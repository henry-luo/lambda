import p: lambda.editor.format.key_value

let source = "# keep\na\\=b=old\nkey\\ space : two\npath\\\\name=three\nodd\\\\\\:key=four\nunicode\\u0041=five\nspace value\n"
let loaded = p.import_text(source)^
let entries = loaded.doc.tree.children
let changed = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "new")^
let renamed = p.rename_key(changed.doc, changed.doc.tree.children[1].first, changed.doc.tree.children[1].key_end, "renamed key=1")^
let added = p.insert_child(renamed.doc, 0, "#new:key", "six")^
let controls = p.insert_child(added.doc, 0, "tab\tform\ffeed", "seven")^;
[
  [for (entry in entries) entry.name] == ["a=b", "key space", "path\\name", "odd\\:key", "unicodeA", "space"],
  [for (entry in entries) entry.node.text] == ["old", "two", "three", "four", "five", "value"],
  changed.doc.text == "# keep\na\\=b=new\nkey\\ space : two\npath\\\\name=three\nodd\\\\\\:key=four\nunicode\\u0041=five\nspace value\n",
  renamed.doc.tree.children[1].name == "renamed key=1",
  contains(renamed.doc.text, "renamed\\ key\\=1 : two"),
  added.doc.tree.children[6].name == "#new:key",
  contains(added.doc.text, "\\#new\\:key=six"),
  controls.doc.tree.children[7].name == "tab\tform\ffeed" and contains(controls.doc.text, "tab\\tform\\ffeed=seven"),
  (p.import_text("continued\\\n key=value\n") ^ { null }) == null
]
