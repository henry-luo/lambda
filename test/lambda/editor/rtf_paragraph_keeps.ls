import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import lambda.editor.core.doc
import lambda.editor.format.model

let source = "{\\rtf1\\keep\\keepn First\\par Inherited\\par\\pard\\pagebb Second\\par\\pard Plain}"
let loaded = f.import_text(source)^
let changed = node('doc', [*take(loaded.doc.content, 3), node('p', [text("Plain edited")])])
let written = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(written)^
let grouped = f.import_text("{\\rtf1{\\keep Scoped\\par}Outside\\par\\pagebb\\par\\pard End}")^
let character_reset = f.import_text("{\\rtf1\\keep\\plain Retained}")^
let direct = rtf.parse(source)^
let grammar_doc = rtf.parse_grammar(source)^
let direct_runs = rtf.parse(source, {preserve_empty:true, editor_runs:true})^
let grammar_runs = rtf.parse_grammar(source, {preserve_empty:true, editor_runs:true})^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  [for (p in loaded.doc.content) attr_get(p, 'rtf-keep')] == [true, true, null, null],
  [for (p in loaded.doc.content) attr_get(p, 'rtf-keepn')] == [true, true, null, null],
  [for (p in loaded.doc.content) attr_get(p, 'rtf-pagebb')] == [null, null, true, null],
  [for (p in reopened.doc.content) attr_get(p, 'rtf-keep')] == [true, true, null, null],
  [for (p in reopened.doc.content) attr_get(p, 'rtf-keepn')] == [true, true, null, null],
  [for (p in reopened.doc.content) attr_get(p, 'rtf-pagebb')] == [null, null, true, null],
  contains(written, "\\keep\\keepn First") and contains(written, "\\pard\\pagebb Second"),
  f.check_roundtrip(changed, loaded.envelope) == null,
  [for (p in grouped.doc.content) attr_get(p, 'rtf-keep')] == [true, null, null, null],
  [for (p in grouped.doc.content) attr_get(p, 'rtf-pagebb')] == [null, null, true, null],
  format(direct, 'json')^ == format(grammar_doc, 'json')^,
  format(direct_runs, 'json')^ == format(grammar_runs, 'json')^,
  attr_get(character_reset.doc.content[0], 'rtf-keep') == true,
  (f.import_text("{\\rtf1 text\\keep late}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\keep0 parameter}") ^ { null }) == null
]
