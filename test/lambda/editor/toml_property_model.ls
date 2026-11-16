import t: lambda.editor.format.toml

let text = "# settings\r\ntitle = \"old\" # keep\r\n[server]\r\nport = 80 # port\r\nitems = [1, {x = 2}]\r\n[[workers]]\r\nname = 'one'\r\n[[workers]]\r\nname = 'two'\r\n"
let loaded = t.import_text(text)^
let root = loaded.doc.tree
let server = root.children[1].node
let port = server.children[0]
let items = server.children[1].node
let nested = items.children[1].node
let changed = t.replace_scalar(loaded.doc, port.node.first, port.node.last, "1_000")^
let renamed = t.rename_key(loaded.doc, port.first, port.key_end, "http port")^
let inserted = t.insert_child(loaded.doc, server.first, "enabled", "true")^
let inserted_root = t.insert_child(loaded.doc, root.first, "extra", "3")^
let deleted = t.delete_child(loaded.doc, port.node.first)^
let array_deleted = t.delete_child(loaded.doc, items.children[0].node.first)^
let nested_changed = t.replace_scalar(loaded.doc, nested.children[0].node.first, nested.children[0].node.last, "3")^
let nested_added = t.insert_child(loaded.doc, nested.first, "y", "true")^
let nested_deleted = t.delete_child(loaded.doc, nested.children[0].node.first)^
let empty = t.import_text("")^
let empty_added = t.insert_child(empty.doc, 0, "first", "1")^
let multiline = t.import_text("text = \"\"\"one\r\ntwo\"\"\" # keep\r\n")^
let multi = multiline.doc.tree.children[0].node
let same = t.replace_scalar(multiline.doc, multi.first, multi.last, multi.text)^
let multi_changed = t.replace_scalar(multiline.doc, multi.first, multi.last, "\"\"\"one\nchanged\"\"\"")^;
[
  loaded.doc.text == text and len(root.children) == 4,
  server.children[0].name == "port" and len(items.children) == 2,
  root.children[2].name == "workers" and root.children[3].node.children[0].node.text == "'two'",
  contains(changed.doc.text, "port = 1_000 # port\r\n"),
  contains(renamed.doc.text, "\"http port\" = 80 # port\r\n"),
  contains(inserted.doc.text, "\"enabled\" = true\r\n[[workers]]"),
  contains(inserted_root.doc.text, "\"extra\" = 3\r\n[server]"),
  not contains(deleted.doc.text, "# port") and contains(deleted.doc.text, "[server]\r\nitems ="),
  contains(array_deleted.doc.text, "items = [{x = 2}]"),
  contains(nested_changed.doc.text, "{x = 3}"),
  contains(nested_added.doc.text, "{x = 2, \"y\" = true}"),
  contains(nested_deleted.doc.text, "items = [1, {}]"),
  empty_added.doc.text == "\"first\" = 1\n",
  same.doc.text == multiline.doc.text,
  multi_changed.doc.text == "text = \"\"\"one\r\nchanged\"\"\" # keep\r\n",
  (t.replace_scalar(loaded.doc, port.node.first, port.node.last, "01") ^ { null }) == null,
  (t.replace_scalar(loaded.doc, port.node.first, port.node.last, "1\ninjected=2") ^ { null }) == null,
  (t.rename_key(loaded.doc, port.first, port.key_end, "items") ^ { null }) == null,
  (t.insert_child(loaded.doc, server.first, "port", "3") ^ { null }) == null,
  (t.import_text("x = \"bad\\q\"\n") ^ { null }) == null,
  (t.import_text("[a]\n[a]\n") ^ { null }) == null,
  (t.import_text("x = 1__2\n") ^ { null }) == null
]
