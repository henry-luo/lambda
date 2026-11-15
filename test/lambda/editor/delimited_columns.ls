import table: lambda.editor.format.delimited

let source = "name,\"role\"\r\nAda,dev\r\nLin\r\n"
let loaded = table.csv_descriptor.import_text(source)^
let inserted = table.insert_column(loaded.doc, 1)^
let removed = table.delete_column(inserted.doc, 2)^
let appended = table.insert_column(loaded.doc, 2)^
let tsv = table.tsv_descriptor.import_text("one\ttwo\nthree\tfour\n")^
let tsv_removed = table.delete_column(tsv.doc, 0)^
let quoted = table.csv_descriptor.import_text("a,\"b\nc\",d\r\nx,y,z\r\n")^
let quoted_insert = table.insert_column(quoted.doc, 1)^
let unsafe = table.csv_descriptor.import_text("first\tsecond\none\ttwo,three\n")^;
let mixed = table.csv_descriptor.import_text("a,b\r\nc,d\ne")^
let before_first = table.insert_row(mixed.doc, mixed.doc.tree.children[0].node.first)^
let before_second = table.insert_row(mixed.doc, mixed.doc.tree.children[1].node.first)^
let before_last = table.insert_row(mixed.doc, mixed.doc.tree.children[2].node.first)^
let tsv_row = table.insert_row(tsv.doc, tsv.doc.tree.children[1].node.first)^
let quoted_row = table.insert_row(quoted.doc, quoted.doc.tree.children[1].node.first)^
let quoted_tail = table.csv_descriptor.import_text("a,b\r\n\"x\ny\",z")^
let before_quoted_tail = table.insert_row(quoted_tail.doc, quoted_tail.doc.tree.children[1].node.first)^
let unsafe_first = table.csv_descriptor.import_text("a\tb\nc,d\n")^;
[
  inserted.doc.text == "name,,\"role\"\r\nAda,,dev\r\nLin,\r\n",
  removed.doc.text == "name,\r\nAda,\r\nLin,\r\n",
  appended.doc.text == "name,\"role\",\r\nAda,dev,\r\nLin,,\r\n",
  tsv_removed.doc.text == "two\nfour\n",
  (table.delete_column(loaded.doc, 0) ^ { null }) == null,
  (table.insert_column(loaded.doc, 3) ^ { null }) == null,
  (table.delete_column(loaded.doc, 2) ^ { null }) == null,
  quoted_insert.doc.text == "a,,\"b\nc\",d\r\nx,,y,z\r\n",
  (table.delete_column(unsafe.doc, 0) ^ { null }) == null,
  before_first.doc.text == ",\r\na,b\r\nc,d\ne",
  before_second.doc.text == "a,b\r\n,\nc,d\ne",
  before_last.doc.text == "a,b\r\nc,d\n,\ne",
  tsv_row.doc.text == "one\ttwo\n\t\nthree\tfour\n",
  quoted_row.doc.text == "a,\"b\nc\",d\r\n,,\r\nx,y,z\r\n",
  before_quoted_tail.doc.text == "a,b\r\n,\r\n\"x\ny\",z",
  (table.insert_row(mixed.doc, -99) ^ { null }) == null,
  (table.insert_row(unsafe_first.doc, unsafe_first.doc.tree.children[0].node.first) ^ { null }) != null
]
