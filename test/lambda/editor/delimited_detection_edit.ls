import csv: lambda.editor.format.delimited

let original = csv.csv_descriptor.import_text("first\tsecond\none\ttwo\n")^
let first = original.doc.tree.children[0].node.children[0].node
let second = original.doc.tree.children[0].node.children[1].node
let changed = csv.replace_scalar(original.doc, first.first, first.last, "red, green, blue")^
let reopened = csv.csv_descriptor.import_text(changed.doc.text)^
let viewed = parse(changed.doc.text, 'csv')^
let rejected = csv.delete_child(original.doc, second.first) ^ { null }
let empty = csv.csv_descriptor.import_text("")^
let unsafe_first = csv.insert_child(empty.doc, -1, "", "a\tb") ^ { null }
let comma = csv.csv_descriptor.import_text("first,second\none,two\n")^
let comma_field = comma.doc.tree.children[0].node.children[0].node
let comma_changed = csv.replace_scalar(comma.doc, comma_field.first, comma_field.last, "a\tb\tc")^
let added_tab = csv.insert_child(original.doc, original.doc.tree.children[0].node.first, "", "a,b,c")^
let added_comma = csv.insert_child(comma.doc, comma.doc.tree.children[0].node.first, "", "a\tb\tc")^
let isolated = csv.csv_descriptor.import_text("a\tb\nsolo\n")^
let removed_first = csv.delete_child(isolated.doc, isolated.doc.tree.children[0].node.first)^
let tabbed = csv.tsv_descriptor.import_text("first\tsecond\none\ttwo\n")^
let tsv_value = tabbed.doc.tree.children[0].node.children[0].node
let tsv_changed = csv.replace_scalar(tabbed.doc, tsv_value.first, tsv_value.last, "a,b,c")^;
[
  original.doc.separator == "\t",
  changed.doc.text == "\"red, green, blue\"\tsecond\none\ttwo\n",
  reopened.doc.separator == "\t" and reopened.doc.tree.children[0].node.children[0].node.text == "red, green, blue",
  viewed[0]["red, green, blue"] == "one" and viewed[0].second == "two",
  rejected == null,
  unsafe_first == null,
  comma_changed.doc.text == "\"a\tb\tc\",second\none,two\n" and csv.csv_descriptor.import_text(comma_changed.doc.text)^.doc.separator == ",",
  added_tab.doc.text == "first\tsecond\t\"a,b,c\"\none\ttwo\n",
  added_comma.doc.text == "first,second,\"a\tb\tc\"\none,two\n",
  removed_first.doc.text == "solo\n" and removed_first.doc.separator == ",",
  tsv_changed.doc.text == "a,b,c\tsecond\none\ttwo\n" and tsv_changed.doc.separator == "\t"
]
