import y: lambda.editor.format.yaml
let text = "? [1, 2]\n: list value\n? {a: 1}\n: map value\n"
let loaded = y.import_text(text)^
let first = loaded.doc.tree.children[0]
let second = loaded.doc.tree.children[1]
let changed = y.rename_key(loaded.doc, first.first, first.key_end, "[3, 4]")^
let duplicate = y.rename_key(loaded.doc, first.first, first.key_end, "{a: 1}") ^ { null }
let value = y.replace_scalar(loaded.doc, second.node.first, second.node.last, "changed")^
let removed = y.delete_child(loaded.doc, first.node.first)^
let implicit = y.import_text("[1, 2]: first\n[3, 4]: second\n")^
let implicit_key = implicit.doc.tree.children[0]
let implicit_change = y.rename_key(implicit.doc, implicit_key.first, implicit_key.key_end, "[5, 6]")^;
let flow = y.import_text("mapping: {[1, 2]: first, {a: 1}: second}\n")^
let flow_map = flow.doc.tree.children[0].node
let flow_key = flow_map.children[0]
let flow_change = y.rename_key(flow.doc, flow_key.first, flow_key.key_end, "[3, 4]")^;
let multiline_text = "?\n  - a\n  - b\n: value\nother: ok\n"
let multiline = y.import_text(multiline_text)^
let multiline_key = multiline.doc.tree.children[0]
let multiline_change = y.rename_key(multiline.doc, multiline_key.first, multiline_key.key_end, "[c, d]")^
let multiline_delete = y.delete_child(multiline.doc, multiline_key.node.first)^
let multiline_map = y.import_text("?\n  a: 1\n  b: 2\n: value\nother: ok\n")^
let map_key = multiline_map.doc.tree.children[0]
let map_change = y.rename_key(multiline_map.doc, map_key.first, map_key.key_end, "{a: 3}")^
let empty = y.import_text("?\n: value\nother: ok\n")^
let empty_key = empty.doc.tree.children[0]
let empty_change = y.rename_key(empty.doc, empty_key.first, empty_key.key_end, "[a, b]")^
let empty_comment = y.import_text("? # keep\n: value\n")^
let comment_key = empty_comment.doc.tree.children[0]
let comment_change = y.rename_key(empty_comment.doc, comment_key.first, comment_key.key_end, "foo")^;
[
  len(loaded.doc.tree.children) == 2,
  first.name == "[1, 2]" and second.name == "{a: 1}",
  changed.doc.text == "? [3, 4]\n: list value\n? {a: 1}\n: map value\n",
  duplicate == null,
  value.doc.text == "? [1, 2]\n: list value\n? {a: 1}\n: changed\n",
  removed.doc.text == "? {a: 1}\n: map value\n",
  implicit_key.name == "[1, 2]" and implicit_key.literal_key == true,
  implicit_change.doc.text == "[5, 6]: first\n[3, 4]: second\n",
  (y.rename_key(implicit.doc, implicit_key.first, implicit_key.key_end, "[3, 4]") ^ { null }) == null,
  flow_map.children[1].name == "{a: 1}" and flow_change.doc.text == "mapping: {[3, 4]: first, {a: 1}: second}\n",
  multiline_key.name == "- a\n  - b" and multiline_change.doc.text == "?\n  [c, d]\n: value\nother: ok\n",
  multiline_delete.doc.text == "other: ok\n",
  map_change.doc.text == "?\n  {a: 3}\n: value\nother: ok\n",
  empty_key.name == "null" and empty_change.doc.text == "? [a, b]\n: value\nother: ok\n",
  comment_change.doc.text == "? foo # keep\n: value\n"
]
