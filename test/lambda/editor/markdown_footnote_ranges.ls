import md: lambda.editor.format.markdown
import lambda.editor.core.doc
import lambda.editor.format.model
let source = "Before[^a] and [^b].\n\n[^b]: Bee\n    continued\n\n[ref]: https://example.com\n\n[^a]: Alpha\n\n[^a]: Duplicate\n\nAfter.\n"
let loaded = md.import_text(source)^
let blocks = loaded.doc.content
let modified = node('doc', [*take(blocks, len(blocks) - 1), node('p', [text("Edited.")])])
let output = md.export_text(modified, loaded.envelope)^
let again = md.import_text(output)^;
[
  len(blocks) == 6,
  attr_get(blocks[1], 'markdown') == "[^b]: Bee\n    continued",
  attr_get(blocks[2], 'markdown') == "[ref]: https://example.com",
  attr_get(blocks[3], 'markdown') == "[^a]: Alpha",
  attr_get(blocks[4], 'markdown') == "[^a]: Duplicate",
  md.export_text(loaded.doc, loaded.envelope)^ == source,
  md.check_roundtrip(modified, loaded.envelope)^ == null,
  again.doc == modified
]
