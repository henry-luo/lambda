import csv: lambda.editor.format.delimited
let source = "Name,Place\r\nAlice,\"KL, MY\"\r\n"
let loaded = csv.csv_descriptor.import_text(source)^
let field = loaded.doc.tree.children[1].node.children[1].node
let name = loaded.doc.tree.children[1].node.children[0].node
let unchanged = csv.replace_scalar(loaded.doc, field.first, field.last, field.text)^
let changed = csv.replace_scalar(loaded.doc, field.first, field.last, "New York, US")^
let escaped = csv.replace_scalar(loaded.doc, name.first, name.last, "Alice \"A\"")^
let inserted = csv.insert_child(loaded.doc, loaded.doc.tree.children[1].node.first, "", "x,y")^
let added = csv.insert_child(changed.doc, -1, "", "Bob,Paris")^
let removed = csv.delete_child(added.doc, added.doc.tree.children[0].node.first)^
let multiline = csv.csv_descriptor.import_text("a,\"two\nlines\"\n")^
let multi = multiline.doc.tree.children[0].node.children[1].node
let multi_changed = csv.replace_scalar(multiline.doc, multi.first, multi.last, "new\n\"lines\"")^
let tsv = csv.tsv_descriptor.import_text("a\tb\n")^
let tsv_field = tsv.doc.tree.children[0].node.children[1].node
let tsv_changed = csv.replace_scalar(tsv.doc, tsv_field.first, tsv_field.last, "x\ty")^
let unicode = csv.csv_descriptor.import_text("😀,\"é,中\"\n")^
let unicode_field = unicode.doc.tree.children[0].node.children[1].node
let unicode_changed = csv.replace_scalar(unicode.doc, unicode_field.first, unicode_field.last, "雪,\"山\"")^;
[
  field.text == "KL, MY",
  unchanged.doc.text == source,
  changed.doc.text == "Name,Place\r\nAlice,\"New York, US\"\r\n",
  escaped.doc.text == "Name,Place\r\n\"Alice \"\"A\"\"\",\"KL, MY\"\r\n",
  inserted.doc.text == "Name,Place\r\nAlice,\"KL, MY\",\"x,y\"\r\n",
  ends_with(added.doc.text, "Bob,Paris\r\n"),
  removed.doc.text == "Alice,\"New York, US\"\r\nBob,Paris\r\n",
  multi.multiline and multi.text == "two\nlines" and multi_changed.doc.text == "a,\"new\n\"\"lines\"\"\"\n",
  (csv.replace_scalar(loaded.doc, field.first, field.last + 1, "x") ^ { null }) == null,
  (csv.csv_descriptor.import_text("\"bad\"tail,x\n") ^ { null }) == null,
  csv.csv_descriptor.import_text("a\tb\n1\t2\n")^.doc.separator == "\t",
  tsv_changed.doc.text == "a\t\"x\ty\"\n",
  unicode_field.text == "é,中" and unicode_changed.doc.text == "😀,\"雪,\"\"山\"\"\"\n"
]
