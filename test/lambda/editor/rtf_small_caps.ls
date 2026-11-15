import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import inline_format: lambda.editor.core.inline_format
import lambda.editor.core.doc

let source = "{\\rtf1 Plain {\\scaps\\b Small} normal \\scaps Enabled\\scaps0 off \\scaps Again\\plain reset}"
let loaded = f.import_text(source)^
let changed = node('doc', [node('p', [*loaded.doc.content[0].content, text("!")])])
let written = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(written)^
let parts = [for (part in reopened.doc.content[0].content)
  {text:part.text, css:inline_format.span_css(part.marks), bold:any([for (m in part.marks) m.name == 'strong'])}]
let direct = rtf.parse(source)^
let grammar_doc = rtf.parse_grammar(source)^
let mixed = f.import_text("{\\rtf1{\\fonttbl{\\f0 Arial;}{\\f1 Courier;}}{\\colortbl;\\red255\\green0\\blue0;}{\\f1\\fs30\\cf1\\scaps Red}}")^
let mixed_edit = node('doc', [node('p', [*mixed.doc.content[0].content, text("!")])])
let mixed_written = f.export_text(mixed_edit, mixed.envelope)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  any([for (part in parts) contains(part.text,"Small") and part.bold and contains(part.css,"font-variant: small-caps")]),
  any([for (part in parts) contains(part.text,"Enabled") and contains(part.css,"font-variant: small-caps")]),
  any([for (part in parts) contains(part.text,"off") and not contains(part.css,"font-variant")]),
  any([for (part in parts) contains(part.text,"Again") and contains(part.css,"font-variant: small-caps")]),
  any([for (part in parts) contains(part.text,"reset") and not contains(part.css,"font-variant")]),
  contains(written,"\\scaps") and f.check_roundtrip(changed, loaded.envelope) == null,
  format(direct,'json')^ == format(grammar_doc,'json')^,
  f.check_roundtrip(mixed_edit, mixed.envelope) == null and contains(mixed_written,"\\f1\\fs30\\cf1\\scaps"),
  (f.import_text("{\\rtf1\\caps uppercase}") ^ { null }) == null
]
