import f: lambda.editor.format.rtf
import lambda.editor.core.doc

let info = "{\\info{\\title Caf\\u233??}{\\author A\\{B\\}}{\\creatim\\yr2026\\mo11\\dy14}}"
let generator = "{\\*\\generator Lambda Editor;}"
let source = "{\\rtf1\\ansi\\uc2 " ++ info ++ generator ++ "Hello}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [text("Hello edited")])])
let output = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(output)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  contains(output, info) and contains(output, generator),
  contains(output, "\\uc2 " ++ info ++ "\\uc1 "),
  reopened.doc.content[0].content[0].text == "Hello edited",
  f.check_roundtrip(changed, loaded.envelope)^ == null,
  (f.import_text("{\\rtf1{\\info{\\title broken}Hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\info{\\title \\bin3 abc}}Hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1 Hello{\\info{\\title late}}}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\*\\unknown stuff}Hello}") ^ { null }) == null
]
