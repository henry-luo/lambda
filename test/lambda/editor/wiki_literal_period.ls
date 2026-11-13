import wiki: lambda.doc.wiki
import app: lambda.editor.app

let source = "= Title =\n\nHello ''world''.\n"
let format = app.format_for_path("example.wiki")
let loaded = format.import_text(source)^
let period = wiki.format(parse("Hello. 1. a.b\n", 'markdown')^, {flavor:"mediawiki"})^
let special = wiki.format(<doc <p "A [bracket] and <tag>">>, {flavor:"mediawiki"})^;
[
  format.export_text(loaded.doc, loaded.envelope)^ == source,
  format.check_roundtrip(loaded.doc, loaded.envelope) == null,
  contains(period, "Hello. 1. a.b") and not contains(period, "<nowiki>"),
  contains(special, "<nowiki>"),
  (format.import_text("Hello <nowiki>*literal*</nowiki>.\n") ^ { null }) != null
]
