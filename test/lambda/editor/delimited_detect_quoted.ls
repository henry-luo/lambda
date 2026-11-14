import d: lambda.editor.format.delimited

let csv = "\"a\t\tb\",c\n1,2\n"
let multi = "\"first\n\t\t\"\"quoted\"\"\",next\n3,4\n"
let tsv = "\"a,,b\"\tc\n1\t2\n"
let one = d.csv_descriptor.import_text(csv)^
let two = d.csv_descriptor.import_text(multi)^
let three = d.csv_descriptor.import_text(tsv)^
let cell = one.doc.tree.children[0].node.children[1].node
let changed = d.replace_scalar(one.doc, cell.first, cell.last, "new")^;
[
  one.doc.separator == "," and len(one.doc.tree.children[0].node.children) == 2,
  one.doc.tree.children[0].node.children[0].node.text == "a\t\tb",
  two.doc.separator == "," and two.doc.tree.children[0].node.children[0].node.text == "first\n\t\t\"quoted\"",
  three.doc.separator == "\t" and len(three.doc.tree.children[0].node.children) == 2,
  changed.doc.text == "\"a\t\tb\",new\n1,2\n"
]
