import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import lambda.editor.core.doc
import lambda.editor.format.model

let source = "{\\rtf1\\sb300\\sa150 First\\par Inherited\\par\\pard Plain}"
let loaded = f.import_text(source)^
let changed = node('doc', [*take(loaded.doc.content, 2), node('p', [text("Plain edited")])])
let written = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(written)^
let direct = rtf.parse(source)^
let grammar_doc = rtf.parse_grammar(source)^;
[
  [for (p in loaded.doc.content) attr_get(p, 'rtf-sb')] == [300, 300, null],
  [for (p in loaded.doc.content) attr_get(p, 'rtf-sa')] == [150, 150, null],
  [for (p in reopened.doc.content) attr_get(p, 'rtf-sb')] == [300, 300, null],
  [for (p in reopened.doc.content) attr_get(p, 'rtf-sa')] == [150, 150, null],
  contains(written, "\\sb300\\sa150") and contains(written, "\\pard Plain edited"),
  f.check_roundtrip(changed, loaded.envelope) == null,
  contains(direct[0].style, "margin-top:20.00px") and contains(direct[0].style, "margin-bottom:10.00px"),
  format(direct, 'json')^ == format(grammar_doc, 'json')^,
  (f.import_text("{\\rtf1\\sb-1 bad}") ^ { null }) == null,
  (f.import_text("{\\rtf1 text\\sa150 late}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\sb missing}") ^ { null }) == null
]
