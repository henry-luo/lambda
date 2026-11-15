import f: lambda.editor.format.rtf
import rtf: lambda.doc.rtf
import lambda.editor.core.doc
import lambda.editor.format.model

let source = "{\\rtf1\\qc First{\\b \\page Second}\\page Third\\par After}"
let loaded = f.import_text(source)^
let first = loaded.doc.content[0]
let edited = node('doc', [node_attrs('p', first.attrs, drop(first.content, 1)), loaded.doc.content[1]])
let written = f.export_text(edited, loaded.envelope)^
let reopened = f.import_text(written)^
let only = f.import_text("{\\rtf1\\page\\page}")^
let only_edited = node('doc', [node('p', [*only.doc.content[0].content, text("new")])])
let only_back = f.import_text(f.export_text(only_edited, only.envelope)^)^
let only_items = [for (item in only_back.doc.content[0].content where not (item.kind == 'text' and item.text == "")) item]
let reopened_items = [for (item in reopened.doc.content[0].content where not (item.kind == 'text' and item.text == "")) item]
let leading_style = f.import_text("{\\rtf1\\page\\qc Center}")^
let leading_edit = node('doc', [node_attrs('p', leading_style.doc.content[0].attrs,
  [leading_style.doc.content[0].content[1], text("Centered")])])
let viewed = rtf.parse(source)^
let grammar_doc = rtf.parse_grammar(source)^;
[
  len(loaded.doc.content) == 2 and first.tag == 'p',
  [for (item in first.content) if (item.kind == 'text') 'text' else item.tag] == ['text', 'br', 'text', 'br', 'text'],
  all([for (item in first.content where item.kind == 'node') attr_get(item, 'rtf-page') == true]),
  attr_get(first, 'align') == "center",
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  f.check_roundtrip(edited, loaded.envelope)^ == null,
  len(reopened.doc.content) == 2 and reopened_items[0].tag == 'br',
  contains(written, "\\page") and contains(written, "\\par"),
  len(only_back.doc.content) == 1 and [for (item in only_items) if (item.kind == 'text') 'text' else item.tag] == ['br','br','text'],
  attr_get(leading_style.doc.content[0], 'align') == "center" and f.check_roundtrip(leading_edit,leading_style.envelope)^ == null and
    contains(f.export_text(leading_edit,leading_style.envelope)^,"\\page"),
  format(viewed,'json')^ == format(grammar_doc,'json')^ and any([for (block in content(viewed)^) name(block) == 'hr']),
  (f.import_text("{\\rtf1 First\\page\\qc Second}") ^ { null }) == null,
  (f.import_text("{\\rtf1 First\\page1 Second}") ^ { null }) == null
]
