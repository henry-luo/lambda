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
[
  inserted.doc.text == "name,,\"role\"\r\nAda,,dev\r\nLin,\r\n",
  removed.doc.text == "name,\r\nAda,\r\nLin,\r\n",
  appended.doc.text == "name,\"role\",\r\nAda,dev,\r\nLin,,\r\n",
  tsv_removed.doc.text == "two\nfour\n",
  (table.delete_column(loaded.doc, 0) ^ { null }) == null,
  (table.insert_column(loaded.doc, 3) ^ { null }) == null,
  (table.delete_column(loaded.doc, 2) ^ { null }) == null,
  quoted_insert.doc.text == "a,,\"b\nc\",d\r\nx,,y,z\r\n",
  (table.delete_column(unsafe.doc, 0) ^ { null }) == null
]
