import j: lambda.editor.format.json

let source = "{\"a\\u0000b\":1,\"line\\nbreak\":2,\"tab\\tkey\":3,\"carriage\\rreturn\":4,\"a\\u0001b\":5,\"plain\":6}"
let loaded = j.import_text(source)^
let nodes = loaded.doc.tree.children
let first = nodes[0]
let same = j.rename_key_literal(loaded.doc, first.first, first.key_end, "\"a\\u0000b\"")^
let renamed = j.rename_key_literal(loaded.doc, first.first, first.key_end, "\"new\\nname\"")^
let edited = j.replace_scalar(loaded.doc, first.node.first, first.node.last, "7")^
let null_key = parse("\"a\\u0000b\"", 'json')^
let added = j.insert_child(loaded.doc, loaded.doc.tree.first, "added\nname", "true")^
let del_source = "{\"a" ++ chr(127) ++ "b\":1}"
let del_doc = j.import_text(del_source)^
let del_key = del_doc.doc.tree.children[0]
let del_same = j.rename_key_literal(del_doc.doc, del_key.first, del_key.key_end, del_key.name)^;
[
  len(nodes) == 6,
  all([for (entry in take(nodes, 5)^) entry.key_literal == true]),
  nodes[5].key_literal != true and nodes[5].name == "plain",
  first.name == "\"a\\u0000b\"" and first.key_value == null_key,
  same.doc.text == source,
  starts_with(renamed.doc.text, "{\"new\\nname\":1,"),
  starts_with(edited.doc.text, "{\"a\\u0000b\":7,"),
  contains(added.doc.text, "\"added\\nname\":true"),
  del_key.key_literal and del_key.name == "\"a\\u007fb\"" and del_same.doc.text == del_source,
  (j.rename_key_literal(loaded.doc, first.first, first.key_end, "123") ^ { null }) == null,
  (j.rename_key_literal(loaded.doc, first.first, first.key_end, "\"invalid\\q\"") ^ { null }) == null,
  (j.rename_key_literal(loaded.doc, first.first, first.key_end, "\"x\", \"injected\"") ^ { null }) == null
]
