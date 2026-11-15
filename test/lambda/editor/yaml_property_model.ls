import y: lambda.editor.format.yaml

let text = "# settings\nname: old # keep\nserver:\n  port: 80 # port\n  items: [1, {x: 2}]\nlist:\n  - name: one\n    enabled: true\n  - name: two\n    enabled: false\n"
let loaded = y.import_text(text)^
let root = loaded.doc.tree
let server = root.children[1].node
let port = server.children[0]
let items = server.children[1].node
let nested = items.children[1].node
let sequence = root.children[2].node
let first = sequence.children[0].node.children[0].node
let changed = y.replace_scalar(loaded.doc, port.node.first, port.node.last, "90")^
let renamed = y.rename_key(loaded.doc, port.first, port.key_end, "http port")^
let inserted = y.insert_child(loaded.doc, server.first, "enabled", "true")^
let inserted_root = y.insert_child(loaded.doc, root.first, "extra", "3")^
let deleted = y.delete_child(loaded.doc, port.node.first)^
let array_deleted = y.delete_child(loaded.doc, items.children[0].node.first)^
let nested_changed = y.replace_scalar(loaded.doc, nested.children[0].node.first, nested.children[0].node.last, "3")^
let nested_added = y.insert_child(loaded.doc, nested.first, "y", "true")^
let nested_deleted = y.delete_child(loaded.doc, nested.children[0].node.first)^
let list_changed = y.replace_scalar(loaded.doc, first.first, first.last, "changed")^
let list_deleted = y.delete_child(loaded.doc, sequence.children[0].node.first)^
let inline_deleted = y.delete_child(loaded.doc, first.first)^
let singleton = y.import_text("- name: one\n")^
let singleton_deleted = y.delete_child(singleton.doc, singleton.doc.tree.children[0].node.children[0].node.first)^
let only_map = y.import_text("server:\n  port: 80\n")^
let emptied_map = y.delete_child(only_map.doc, only_map.doc.tree.children[0].node.children[0].node.first)^
let only_array = y.import_text("items:\n  - one\n")^
let emptied_array = y.delete_child(only_array.doc, only_array.doc.tree.children[0].node.children[0].node.first)^
let root_map = y.import_text("name: one\n")^
let emptied_root_map = y.delete_child(root_map.doc, root_map.doc.tree.children[0].node.first)^
let root_array = y.import_text("- one\n")^
let emptied_root_array = y.delete_child(root_array.doc, root_array.doc.tree.children[0].node.first)^
let empty = y.import_text("# empty\n")^
let empty_added = y.insert_child(empty.doc, 0, "first", "1")^
let multiline = y.import_text("note: |\r\n  one\r\n  two\r\n# keep\r\nother: null\r\n")^
let multi = multiline.doc.tree.children[0].node
let same = y.replace_scalar(multiline.doc, multi.first, multi.last, multi.text)^
let multi_changed = y.replace_scalar(multiline.doc, multi.first, multi.last, "|\n  one\n  changed")^
let blank = y.import_text("name: # keep\n")^
let filled = y.replace_scalar(blank.doc, blank.doc.tree.children[0].node.first, blank.doc.tree.children[0].node.last, "value")^;
[
  loaded.doc.text == text and len(root.children) == 3,
  server.children[0].name == "port" and len(items.children) == 2,
  len(sequence.children) == 2 and sequence.children[1].node.children[0].node.text == "two",
  contains(changed.doc.text, "port: 90 # port\n"),
  contains(renamed.doc.text, "\"http port\": 80 # port\n"),
  contains(inserted.doc.text, "  \"enabled\": true\nlist:"),
  ends_with(inserted_root.doc.text, "\"extra\": 3\n"),
  not contains(deleted.doc.text, "# port") and contains(deleted.doc.text, "server:\n  items:"),
  contains(array_deleted.doc.text, "items: [{x: 2}]"),
  contains(nested_changed.doc.text, "{x: 3}"),
  contains(nested_added.doc.text, "{x: 2, \"y\": true}"),
  contains(nested_deleted.doc.text, "items: [1, {}]"),
  contains(list_changed.doc.text, "- name: changed\n    enabled: true"),
  not contains(list_deleted.doc.text, "name: one") and contains(list_deleted.doc.text, "name: two"),
  contains(inline_deleted.doc.text, "  - \n    enabled: true\n") and contains(inline_deleted.doc.text, "name: two"),
  singleton_deleted.doc.text == "- {}\n",
  emptied_map.doc.text == "server:\n  {}\n" and emptied_map.doc.tree.children[0].node.kind == 'object',
  emptied_array.doc.text == "items:\n  []\n" and emptied_array.doc.tree.children[0].node.kind == 'array',
  emptied_root_map.doc.text == "{}\n" and emptied_root_map.doc.tree.kind == 'object',
  emptied_root_array.doc.text == "[]\n" and emptied_root_array.doc.tree.kind == 'array',
  empty_added.doc.text == "# empty\n\"first\": 1\n",
  same.doc.text == multiline.doc.text,
  multi_changed.doc.text == "note: |\r\n  one\r\n  changed\r\n# keep\r\nother: null\r\n",
  filled.doc.text == "name: value # keep\n",
  (y.replace_scalar(loaded.doc, port.node.first, port.node.last, "1\ninjected: 2") ^ { null }) == null,
  (y.rename_key(loaded.doc, port.first, port.key_end, "items") ^ { null }) == null,
  (y.insert_child(loaded.doc, server.first, "port", "3") ^ { null }) == null,
  (y.import_text("x: *missing\n") ^ { null }) == null,
  (y.import_text("x: \"bad\\q\"\n") ^ { null }) == null,
  (y.import_text("x: [1,,2]\n") ^ { null }) == null,
  (y.import_text("x: 1\nx: 2\n") ^ { null }) == null,
  (y.import_text("x: &number 1\ny: *number\n") ^ { null }) == null
]
