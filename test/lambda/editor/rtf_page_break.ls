import f: lambda.editor.format.rtf
import lambda.editor.core.doc
import lambda.editor.format.model

let source = "{\\rtf1\\sb300 First\\par\\page Second\\par\\par\\page Third\\par\\page}"
let loaded = f.import_text(source)^
let blocks = loaded.doc.content
let edited = node('doc', [*take(blocks, 3), node_attrs('p', blocks[3].attrs,
  [blocks[3].content[1], text("Third edited")]), blocks[4]])
let written = f.export_text(edited, loaded.envelope)^
let reopened = f.import_text(written)^
let leading = f.import_text("{\\rtf1\\page\\page First}")^
let leading_edit = node('doc', [node('p', [*take(leading.doc.content[0].content, 4), text("First edited")])])
let leading_back = f.import_text(f.export_text(leading_edit, leading.envelope)^)^
let leading_items = [for (item in leading_back.doc.content[0].content where not (item.kind == 'text' and item.text == "")) item];
[
  [for (p in blocks) p.tag] == ['p', 'p', 'p', 'p', 'p'],
  all([for (p in blocks) for (item in p.content where item.kind == 'node') attr_get(item, 'rtf-page') == true]),
  attr_get(blocks[0], 'rtf-sb') == 300 and attr_get(blocks[1], 'rtf-sb') == 300,
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  f.check_roundtrip(edited, loaded.envelope) == null,
  [for (p in reopened.doc.content) p.tag] == ['p', 'p', 'p', 'p', 'p'],
  reopened.doc.content[3].content[2].text == "Third edited",
  [for (item in leading_items) if (item.kind == 'text') 'text' else item.tag] == ['br', 'br', 'text'],
  (f.import_text("{\\rtf1 First\\page Second}") ^ { null }) != null,
  (f.import_text("{\\rtf1 A\\page1 B}") ^ { null }) == null,
  f.check_roundtrip(node('doc', [node('hr', [])]), loaded.envelope) != null
]
