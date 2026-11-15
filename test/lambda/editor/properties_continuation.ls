import p: lambda.editor.format.key_value

let source = "# keep\r\nlong\\\r\n  key=one\\\r\n  two\r\nliteral=two\\\\\r\nnext=end\r\n"
let loaded = p.import_text(source)^
let entries = loaded.doc.tree.children
let same = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "onetwo")^
let edited = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "new")^
let renamed = p.rename_key(loaded.doc, entries[0].first, entries[0].key_end, "renamed key")^
let removed = p.delete_child(loaded.doc, entries[0].node.first)^
let added = p.insert_child(loaded.doc, 0, "tail", "added")^
let final_line = p.import_text("key=one\\\n")^
let final_edit = p.replace_scalar(final_line.doc, final_line.doc.tree.children[0].node.first,
  final_line.doc.tree.children[0].node.last, "two")^
let final_added = p.insert_child(final_line.doc, 0, "next", "end")^
let dangling = p.import_text("key=one\\")^
let dangling_added = p.insert_child(dangling.doc, 0, "next", "end")^
let suffix_edit = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "oneTWO")^
let prefix_edit = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "ONEtwo")^
let middle_edit = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "oneXtwo")^
let parity_edit = p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "on\\two")^
let cr_doc = p.import_text("first=one\rsecond=left\\\r  right\r")^
let cr_added = p.insert_child(cr_doc.doc, 0, "third", "three")^
let comment_doc = p.import_text("# comment\\\nkey=value\n")^
let unicode_doc = p.import_text("café=un\\\n  deux\n")^
let unicode_edit = p.replace_scalar(unicode_doc.doc, unicode_doc.doc.tree.children[0].node.first,
  unicode_doc.doc.tree.children[0].node.last, "unDEUX")^
let bare = p.import_text("# keep\nunassigned\nnext=done\n")^
let bare_edit = p.replace_scalar(bare.doc, bare.doc.tree.children[0].node.first,
  bare.doc.tree.children[0].node.last, "enabled")^
let bare_rename = p.rename_key(bare.doc, bare.doc.tree.children[0].first,
  bare.doc.tree.children[0].key_end, "renamed")^
let escaped_bare = p.import_text("escaped\\ ")^
let odd_bare = p.import_text("bare\\")^
let odd_bare_edit = p.replace_scalar(odd_bare.doc, odd_bare.doc.tree.children[0].node.first,
  odd_bare.doc.tree.children[0].node.last, "enabled")^
let continued_bare = p.import_text("bare\\\n")^
let continued_bare_edit = p.replace_scalar(continued_bare.doc, continued_bare.doc.tree.children[0].node.first,
  continued_bare.doc.tree.children[0].node.last, "enabled")^
let control_key = p.import_text("\u000b#name=value\n")^
let blank_continuation = p.import_text("key=one\\\n  \nnext=end\n")^
let blank_edit = p.replace_scalar(blank_continuation.doc, blank_continuation.doc.tree.children[0].node.first,
  blank_continuation.doc.tree.children[0].node.last, "two")^
let spaced_bare = p.import_text("spaced   \n")^
let spaced_edit = p.replace_scalar(spaced_bare.doc, spaced_bare.doc.tree.children[0].node.first,
  spaced_bare.doc.tree.children[0].node.last, "yes")^
let matrix_sources = ["key=one\\\n  two\nsentinel=keep\n", "key=one\\\r\n  two\r\nsentinel=keep\r\n",
  "key=one\\\r  two\rsentinel=keep\r", "key=\\\n  onetwo\nsentinel=keep\n",
  "key=onetwo\\\n  \nsentinel=keep\n", "key=one\\\\\\\n  two\nsentinel=keep\n"]
let matrix_values = ["", "new", "onetwo", "oneXtwo", "oneTWO", "ONEtwo", "on\\two", "\\n", "\\u0041", "two\\\\"]
let matrix = [for (s in matrix_sources) for (v in matrix_values) {
  let doc = p.import_text(s)^.doc
  let value = doc.tree.children[0].node
  let changed = p.replace_scalar(doc, value.first, value.last, v)^
  let actual = parse(changed.doc.text, 'properties')^
  let expected = parse("key=" ++ v ++ "\nsentinel=keep\n", 'properties')^
  format(actual,'json')^ == format(expected,'json')^
}];
[
  [for (entry in entries) entry.name] == ["longkey", "literal", "next"],
  [for (entry in entries) entry.node.text] == ["onetwo", "two\\\\", "end"],
  p.descriptor.export_text(loaded.doc, loaded.envelope) == source and same.doc.text == source,
  edited.doc.text == "# keep\r\nlong\\\r\n  key=new\r\nliteral=two\\\\\r\nnext=end\r\n",
  renamed.doc.text == "# keep\r\nrenamed\\ key=one\\\r\n  two\r\nliteral=two\\\\\r\nnext=end\r\n",
  removed.doc.text == "# keep\r\nliteral=two\\\\\r\nnext=end\r\n",
  ends_with(added.doc.text, "next=end\r\ntail=added\r\n"),
  final_line.doc.tree.children[0].node.text == "one" and final_edit.doc.text == "key=two\\\n",
  final_added.doc.text == "key=one\\\n\nnext=end\n" and len(final_added.doc.tree.children) == 2,
  dangling_added.doc.text == "key=one\\\n\nnext=end\n" and len(dangling_added.doc.tree.children) == 2,
  contains(suffix_edit.doc.text, "key=one\\\r\n  TWO\r\n"),
  contains(prefix_edit.doc.text, "key=ONE\\\r\n  two\r\n"),
  contains(middle_edit.doc.text, "key=one\\\r\n  Xtwo\r\n"),
  contains(parity_edit.doc.text, "key=on\\two\r\n"),
  [for (e in cr_doc.doc.tree.children) e.name] == ["first", "second"] and cr_doc.doc.tree.children[1].node.text == "leftright",
  cr_added.doc.text == "first=one\rsecond=left\\\r  right\rthird=three\r",
  len(comment_doc.doc.tree.children) == 1 and comment_doc.doc.tree.children[0].name == "key",
  unicode_edit.doc.text == "café=un\\\n  DEUX\n",
  [for (e in bare.doc.tree.children) e.name] == ["unassigned", "next"] and bare.doc.tree.children[0].node.text == "",
  bare_edit.doc.text == "# keep\nunassigned=enabled\nnext=done\n",
  bare_rename.doc.text == "# keep\nrenamed\nnext=done\n",
  escaped_bare.doc.tree.children[0].name == "escaped ",
  odd_bare.doc.tree.children[0].name == "bare" and odd_bare_edit.doc.text == "bare\\\n=enabled",
  continued_bare_edit.doc.text == "bare\\\n=enabled\n",
  len(control_key.doc.tree.children) == 1 and control_key.doc.tree.children[0].name == "\u000b#name",
  blank_continuation.doc.tree.children[0].node.text == "one" and
    blank_edit.doc.text == "key=two\\\n  \nnext=end\n",
  spaced_edit.doc.text == "spaced   yes\n" and spaced_edit.doc.tree.children[0].name == "spaced",
  (p.import_text("key=value\u0000hidden=x") ^ { null }) == null,
  (p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "bad\u0000value") ^ { null }) == null,
  (p.rename_key(loaded.doc, entries[0].first, entries[0].key_end, "bad\u0000name") ^ { null }) == null,
  (p.insert_child(loaded.doc, 0, "bad\u0000name", "value") ^ { null }) == null,
  len(matrix) == 60 and all(matrix),
  (p.import_text("bad\\nname=value\n") ^ { null }) == null,
  (p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "bad\nother=value") ^ { null }) == null,
  (p.replace_scalar(loaded.doc, entries[0].node.first, entries[0].node.last, "bad\\") ^ { null }) == null
]
