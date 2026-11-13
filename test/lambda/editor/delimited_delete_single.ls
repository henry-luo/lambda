import csv: lambda.editor.format.delimited

let mixed = csv.csv_descriptor.import_text("a,b\r\nonly\r\nlast,field\r\n")^
let single = mixed.doc.tree.children[1].node.children[0].node
let removed = csv.delete_child(mixed.doc, single.first)^
let blank = csv.csv_descriptor.import_text("a,b\n\n")^
let blank_cell = blank.doc.tree.children[1].node.children[0].node
let blank_removed = csv.delete_child(blank.doc, blank_cell.first)^
let no_final = csv.tsv_descriptor.import_text("a\tb\nlast")^
let last_cell = no_final.doc.tree.children[1].node.children[0].node
let last_removed = csv.delete_child(no_final.doc, last_cell.first)^
let multiline = csv.csv_descriptor.import_text("a,b\r\n\"multi\nvalue\"\r\nlast,field\r\n")^
let multiline_cell = multiline.doc.tree.children[1].node.children[0].node
let multiline_removed = csv.delete_child(multiline.doc, multiline_cell.first)^
let quoted = csv.csv_descriptor.import_text("\"\"\nlast\n")^
let quoted_removed = csv.delete_child(quoted.doc, quoted.doc.tree.children[0].node.children[0].node.first)^;
[
  removed.doc.text == "a,b\r\nlast,field\r\n",
  len(removed.doc.tree.children) == 2,
  blank_removed.doc.text == "a,b\n" and len(blank_removed.doc.tree.children) == 1,
  last_removed.doc.text == "a\tb\n" and len(last_removed.doc.tree.children) == 1,
  multiline_removed.doc.text == "a,b\r\nlast,field\r\n",
  quoted_removed.doc.text == "last\n" and len(quoted_removed.doc.tree.children) == 1
]
