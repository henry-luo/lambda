import f: lambda.editor.format.rtf
import lambda.editor.core.doc

let source = "{\\rtf1\\ansi\\deflang1033\\deflangfe2052\\adeflang1025 Hello}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [text("Hello edited")])])
let output = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(output)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  contains(output, "\\deflang1033 ") and contains(output, "\\deflangfe2052 ") and contains(output, "\\adeflang1025 "),
  reopened.doc.content[0].content[0].text == "Hello edited",
  f.check_roundtrip(changed, loaded.envelope)^ == null,
  (f.import_text("{\\rtf1\\deflang Hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\deflang-1 Hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\deflang1033 }Hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1 Hello\\deflang1033 World}") ^ { null }) == null,
  (f.import_text("{\\rtf1 Hello\\lang1033 World}") ^ { null }) == null
]
