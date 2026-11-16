import app: lambda.editor.app
import lambda.editor.core.doc
let format = app.format_for_path("test.1")
let source = ".\\\" authored comment\r\n.TH \"TEST\" \"1\" \"2020\" \"project\" \"manual\"\r\n.SH NAME\ntest - example\n"
let loaded = format.import_text(source)^
let edited = node('doc', [loaded.doc.content[0], loaded.doc.content[1], node('p', [text("test - updated")])])
let output = format.export_text(edited, loaded.envelope)^
let back = format.import_text(output)^
let title_edit = node('doc', [node('h1', [text("CHANGED")]), loaded.doc.content[1], loaded.doc.content[2]]);
[
  format.id == 'man',
  loaded.doc.content[0].content[0].text == "TEST",
  format.export_text(loaded.doc, loaded.envelope)^ == source,
  starts_with(output, ".\\\" authored comment\r\n.TH \"TEST\" \"1\" \"2020\" \"project\" \"manual\"\r\n"),
  contains(output, "test \\- updated"),
  format.check_roundtrip(edited, loaded.envelope) == null,
  back.doc.content[2].content[0].text == "test - updated",
  format.check_roundtrip(title_edit, loaded.envelope) != null
]
