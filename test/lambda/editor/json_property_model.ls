import j: lambda.editor.format.json
import app: lambda.editor.app

let text = " \r\n{\n  \"a\\u0062\": [1, true, {\"x\":\"a\\nb\"}],\n  \"duplicate\":null, \"duplicate\":2, \"empty\": {}\n}\n"
let loaded = j.import_text(text)^
let root = loaded.doc.tree
let arr = root.children[0].node
let first = arr.children[0].node
let nested = arr.children[2].node
let changed = j.replace_scalar(loaded.doc, first.first, first.last, "3.50e+2")^
let renamed = j.rename_key(loaded.doc, root.children[0].first, root.children[0].key_end, "new \"key\"")^
let added = j.insert_child(loaded.doc, nested.first, "new", "[null,\"雪\"]")^
let added_array = j.insert_child(loaded.doc, arr.first, "", "false")^
let empty = root.children[3].node
let added_empty = j.insert_child(loaded.doc, empty.first, "a", "1")^
let deleted_first = j.delete_child(loaded.doc, arr.children[0].node.first)^
let deleted_mid = j.delete_child(loaded.doc, arr.children[1].node.first)^
let deleted_last = j.delete_child(loaded.doc, arr.children[2].node.first)^
let deleted_object = j.delete_child(loaded.doc, root.children[1].node.first)^
let singleton = j.import_text("{\"a\":[42]}")^
let deleted_singleton = j.delete_child(singleton.doc, singleton.doc.tree.children[0].node.children[0].node.first)^
let root_value = j.import_text(" \"x\" \n")^
let root_changed = j.replace_scalar(root_value.doc, root_value.doc.tree.first, root_value.doc.tree.last, "{\"n\":1}")^
let unicode = j.import_text("{\"雪\":\"😀\",\"\\uD83D\\uDE00\":1}")^
let unicode_number = unicode.doc.tree.children[1].node
let unicode_changed = j.replace_scalar(unicode.doc, unicode_number.first, unicode_number.last, "2")^
let same = j.replace_scalar(loaded.doc, first.first, first.last, first.text)^;
[
  app.format_for_path("x.JSON").id == 'json',
  root.kind == 'object' and root.children[0].name == "ab" and len(root.children) == 4,
  arr.kind == 'array' and len(arr.children) == 3 and nested.children[0].node.text == "\"a\\nb\"",
  same.doc.text == text,
  contains(changed.doc.text, "\"a\\u0062\": [3.50e+2, true, {\"x\":\"a\\nb\"}]"),
  contains(renamed.doc.text, "\"new \\\"key\\\"\": [1, true"),
  contains(added.doc.text, "{\"x\":\"a\\nb\",\"new\":[null,\"雪\"]}"),
  contains(added_array.doc.text, "[1, true, {\"x\":\"a\\nb\"},false]"),
  contains(added_empty.doc.text, "\"empty\": {\"a\":1}"),
  contains(deleted_first.doc.text, "[true, {\"x\":\"a\\nb\"}]"),
  contains(deleted_mid.doc.text, "[1, {\"x\":\"a\\nb\"}]"),
  contains(deleted_last.doc.text, "[1, true]"),
  contains(deleted_object.doc.text, "\"duplicate\":2, \"empty\""),
  deleted_singleton.doc.text == "{\"a\":[]}",
  root_changed.doc.text == " {\"n\":1} \n" and root_changed.doc.tree.kind == 'object',
  unicode.doc.tree.children[1].name == "😀" and unicode_changed.doc.text == "{\"雪\":\"😀\",\"\\uD83D\\uDE00\":2}",
  j.import_text("{\"\\u0000x\":1}")^.doc.tree.children[0].key_literal == true,
  j.import_text("{\"a\\nb\":1}")^.doc.tree.children[0].key_literal == true,
  (j.import_text("{\"x\":1,}") ^ { null }) == null,
  (j.import_text("[1 2]") ^ { null }) == null,
  (j.import_text("1 2") ^ { null }) == null,
  (j.replace_scalar(loaded.doc, first.first, first.last, "1, 9") ^ { null }) == null,
  (j.insert_child(loaded.doc, root.first, "injected", "1,\"bad\":2") ^ { null }) == null,
  (j.rename_key(loaded.doc, root.children[0].first, root.children[0].key_end + 1, "x") ^ { null }) == null,
  (j.delete_child(loaded.doc, root.first) ^ { null }) == null
]
