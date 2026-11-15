import p: lambda.editor.format.key_value

let source = "; keep\r\n[server] ; section comment\r\ntext=\"alpha\r\nbeta\" ; value comment\r\nname=old\r\n"
let loaded = p.ini_descriptor.import_text(source)^
let entry = loaded.doc.tree.children[0]
let same = p.replace_scalar(loaded.doc, entry.node.first, entry.node.last, "\"alpha\nbeta\"")^
let changed = p.replace_scalar(loaded.doc, entry.node.first, entry.node.last, "\"alpha\nBETA\"")^
let expanded = p.replace_scalar(loaded.doc, entry.node.first, entry.node.last, "\"alpha\nbeta\nmore\"")^
let renamed = p.rename_key(loaded.doc, entry.first, entry.key_end, "description")^
let removed = p.delete_child(loaded.doc, entry.node.first)^
let inserted = p.insert_child(loaded.doc, 0, "notes", "\"one\ntwo\"")^
let mixed = p.ini_descriptor.import_text("[s]\r\nv=\"a\r\nb\"\r\nlast=x\n")^
let mixed_entry = mixed.doc.tree.children[0]
let mixed_changed = p.replace_scalar(mixed.doc, mixed_entry.node.first, mixed_entry.node.last, "\"a\nb\nc\"")^
let cr = p.ini_descriptor.import_text("[first]\rname=one\r[second] ; comment\rname=two\r")^
let cr_added = p.insert_child(cr.doc, 0, "third", "three")^;
[
  len(loaded.doc.tree.children) == 2 and entry.group == "server",
  entry.node.text == "\"alpha\nbeta\"" and entry.node.multiline == true,
  same.doc.text == source,
  changed.doc.text == "; keep\r\n[server] ; section comment\r\ntext=\"alpha\r\nBETA\" ; value comment\r\nname=old\r\n",
  expanded.doc.text == "; keep\r\n[server] ; section comment\r\ntext=\"alpha\r\nbeta\r\nmore\" ; value comment\r\nname=old\r\n",
  renamed.doc.text == "; keep\r\n[server] ; section comment\r\ndescription=\"alpha\r\nbeta\" ; value comment\r\nname=old\r\n",
  removed.doc.text == "; keep\r\n[server] ; section comment\r\nname=old\r\n",
  ends_with(inserted.doc.text, "notes=\"one\r\ntwo\"\r\n"),
  mixed_changed.doc.text == "[s]\r\nv=\"a\r\nb\r\nc\"\r\nlast=x\n",
  [for (e in cr.doc.tree.children) e.group] == ["first", "second"],
  cr_added.doc.text == "[first]\rname=one\r[second] ; comment\rname=two\rthird=three\r",
  (p.replace_scalar(loaded.doc, entry.node.first, entry.node.last, "bad\nother=value") ^ { null }) == null,
  (p.replace_scalar(loaded.doc, entry.node.first, entry.node.last, "\"unterminated\nvalue") ^ { null }) == null,
  (p.ini_descriptor.import_text("[broken\nname=x\n") ^ { null }) == null
]
