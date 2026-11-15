import f: lambda.editor.format.rtf
import lambda.editor.core.doc

let source = "{\\rtf1\\ansi\\paperw12240\\paperh15840\\margl1440\\margr720\\margt1080\\margb900\\gutter180\\deftab720\\landscape Hello}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [text("Hello edited")])])
let written = f.export_text(changed, loaded.envelope)^
let back = f.import_text(written)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  contains(written, "\\paperw12240 ") and contains(written, "\\paperh15840 "),
  contains(written, "\\margl1440 ") and contains(written, "\\margr720 "),
  contains(written, "\\margt1080 ") and contains(written, "\\margb900 "),
  contains(written, "\\gutter180 ") and contains(written, "\\deftab720 "),
  contains(written, "\\landscape ") and back.doc.content[0].content[0].text == "Hello edited",
  f.check_roundtrip(changed, loaded.envelope) == null,
  (f.import_text("{\\rtf1\\paperw0 bad}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\margl-1 bad}") ^ { null }) == null,
  (f.import_text("{\\rtf1 text\\paperw12240 late}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\landscape text}}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\landscape1 bad}") ^ { null }) == null
]
