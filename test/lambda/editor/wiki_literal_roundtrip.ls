import app: lambda.editor.app
import md: lambda.editor.format.markdown
import lambda.editor.core.doc
import lambda.editor.core.step

let f = app.format_for_path("example.wiki")
let source = "Hello <nowiki>[[link]] and {{template}}</nowiki>.\n"
let loaded = f.import_text(source)^
let edited = node('doc', [node('p', [text("Hello [[link]] and {{template}}. More!")])])
let written = f.export_text(edited, loaded.envelope)^
let reopened = f.import_text(written)^
let bracket_source = "A [bracket] text.\n"
let bracket = f.import_text(bracket_source)^
let link_source = "Hello [[Page|label]].\n"
let link = f.import_text(link_source)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  f.check_roundtrip(edited, loaded.envelope) == null,
  contains(written, "<nowiki>") and contains(written, "[[link]]"),
  md.normal_form(reopened.doc) == md.normal_form(edited),
  f.check_roundtrip(reopened.doc, reopened.envelope) == null,
  (f.import_text("Hello {{Template|x}}.\n") ^ { null }) == null,
  (f.import_text("Hello <span class=\"important\">text</span>.\n") ^ { null }) == null,
  f.export_text(bracket.doc, bracket.envelope)^ == bracket_source and f.check_roundtrip(bracket.doc, bracket.envelope) == null,
  f.export_text(link.doc, link.envelope)^ == link_source and f.check_roundtrip(link.doc, link.envelope) == null
]
