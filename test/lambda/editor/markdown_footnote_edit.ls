import md: lambda.editor.format.markdown
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.format.model

let source = "Read[^a].\n\n[^a]: First  **strong**.\n\n    Second paragraph.\n\nAfter.\n"
let loaded = md.import_text(source)^
let note = loaded.doc.content[1]
let outside = replace_node_at(loaded.doc, [2,0], text("Changed."))^
let outside_text = md.export_text(outside, loaded.envelope)^
let edited = replace_node_at(loaded.doc, [1,0,0], text("Edited  "))^
let written = md.export_text(edited, loaded.envelope)^
let reopened = md.import_text(written)^;
[
  note.tag == 'md_footnote' and attr_get(note,'label') == "a",
  len(note.content) == 2,
  md.export_text(loaded.doc, loaded.envelope)^ == source,
  contains(outside_text, "[^a]: First  **strong**.\n\n    Second paragraph."),
  contains(written, "[^a]: Edited  **strong**.") and contains(written, "    Second paragraph."),
  md.check_roundtrip(edited, loaded.envelope)^ == null,
  md.normal_form(reopened.doc) == md.normal_form(edited)
]
