import p: lambda.editor.format.key_value
import csv: lambda.editor.format.delimited

let crlf = p.import_text("one=1\r\ntwo=2\r\n")^
let no_final = p.ini_descriptor.import_text("[main]\r\none=1")^
let lf = p.import_text("one=1\ntwo=2\n")^
let mixed = csv.csv_descriptor.import_text("a,b\r\nc,d\n")^
let added_crlf = p.insert_child(crlf.doc, 0, "three", "3")^
let added_no_final = p.insert_child(no_final.doc, 0, "two", "2")^
let added_lf = p.insert_child(lf.doc, 0, "three", "3")^
let added_csv = csv.insert_child(mixed.doc, -1, "", "e,f")^;
[
  added_crlf.doc.text == "one=1\r\ntwo=2\r\nthree=3\r\n",
  added_no_final.doc.text == "[main]\r\none=1\r\ntwo=2\r\n",
  added_lf.doc.text == "one=1\ntwo=2\nthree=3\n",
  ends_with(added_csv.doc.text, "e,f\n")
]
