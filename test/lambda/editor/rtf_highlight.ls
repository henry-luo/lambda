import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import styles: lambda.editor.core.inline_format
import lambda.editor.core.doc

let source = "{\\rtf1\\ansi{\\colortbl;\\red255\\green255\\blue0;\\red255\\green0\\blue0;}Plain {\\highlight1\\b Yellow} normal \\highlight1 Again\\plain still\\highlight0 off {\\cf2\\highlight1 Mixed}}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [*loaded.doc.content[0].content, text("!")])])
let written = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(written)^
let runs = [for (part in reopened.doc.content[0].content)
  {text:part.text, css:styles.span_css(part.marks), bold:any([for (m in part.marks) m.name == 'strong'])}]
let direct = rtf.parse(source)^
let grammar_doc = rtf.parse_grammar(source)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  any([for (run in runs) contains(run.text,"Yellow") and run.bold and contains(run.css,"background-color: rgb(255,255,0)")]),
  any([for (run in runs) contains(run.text,"normal") and not contains(run.css,"background-color")]),
  any([for (run in runs) contains(run.text,"Again") and contains(run.css,"background-color: rgb(255,255,0)")]),
  any([for (run in runs) contains(run.text,"still") and contains(run.css,"background-color: rgb(255,255,0)")]),
  any([for (run in runs) contains(run.text,"off") and not contains(run.css,"background-color")]),
  any([for (run in runs) contains(run.text,"Mixed") and contains(run.css,"background-color: rgb(255,255,0)") and contains(run.css,"color: rgb(255,0,0)")]),
  contains(written,"\\highlight1") and f.check_roundtrip(changed,loaded.envelope) == null,
  format(direct,'json')^ == format(grammar_doc,'json')^,
  (f.import_text("{\\rtf1{\\colortbl;\\red255\\green0\\blue0;}\\highlight2 bad}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\highlight bad}") ^ { null }) == null
]
