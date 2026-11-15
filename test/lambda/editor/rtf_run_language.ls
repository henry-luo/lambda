import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.format.model

fn language(part) any^ {
  let attrs = mark_value(part.marks, 'span')^
  if (attrs is array) attrs_get(attrs, 'rtf-lang') else null
}
let source = "{\\rtf1\\ansi\\deflang1033 English {\\lang1031 German {\\lang1036 French} Deutsch \\plain Reset} English}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [*loaded.doc.content[0].content, text("!")])])
let output = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(output)^
let parts = reopened.doc.content[0].content
let direct = rtf.parse(source, {editor_runs:true})^
let grammar_doc = rtf.parse_grammar(source, {editor_runs:true})^
let mixed = f.import_text("{\\rtf1{\\fonttbl{\\f0 Arial;}{\\f1 Courier;}}{\\colortbl;\\red255\\green0\\blue0;}{\\f1\\fs30\\cf1\\lang1031\\scaps Red}}")^
let mixed_edit = node('doc', [node('p', [*mixed.doc.content[0].content, text("!")])])
let mixed_output = f.export_text(mixed_edit, mixed.envelope)^
let zero = f.import_text("{\\rtf1\\lang0 Zero\\plain Default}")^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  any([for (p in parts) contains(p.text,"German") and language(p)^ == 1031]),
  any([for (p in parts) contains(p.text,"French") and language(p)^ == 1036]),
  any([for (p in parts) contains(p.text,"Deutsch") and language(p)^ == 1031]),
  any([for (p in parts) contains(p.text,"Reset") and language(p)^ == null]),
  contains(output,"\\deflang1033") and contains(output,"\\lang1031") and contains(output,"\\lang1036"),
  f.check_roundtrip(changed, loaded.envelope)^ == null,
  format(direct,'json')^ == format(grammar_doc,'json')^,
  contains(mixed_output,"\\f1\\fs30\\cf1\\lang1031\\scaps") and f.check_roundtrip(mixed_edit,mixed.envelope)^ == null,
  any([for (p in zero.doc.content[0].content) contains(p.text,"Zero") and language(p)^ == 0]),
  any([for (p in zero.doc.content[0].content) contains(p.text,"Default") and language(p)^ == null]),
  (f.import_text("{\\rtf1\\lang missing}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\lang-1 bad}") ^ { null }) == null
]
