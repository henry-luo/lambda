import md: lambda.editor.format.markdown
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.source_pos
import lambda.editor.core.commands

let loaded = md.import_text("This is a short paragraph for testing.\n")^
let st = {doc: loaded.doc, schema: md.descriptor.schema,
  selection: text_selection(pos([0,0], 10), pos([0,0], 26))}
let plain = cmd_insert_link(st, "https://example.org", "", "")^
let titled = cmd_insert_link(st, "https://example.org", "Example", "")^;
[
  md.check_roundtrip(plain.doc_after, loaded.envelope)^ == null,
  md.check_roundtrip(titled.doc_after, loaded.envelope)^ == null,
  contains(md.export_text(plain.doc_after, loaded.envelope)^, "](https://example.org)"),
  contains(md.export_text(titled.doc_after, loaded.envelope)^, "\"Example\"")
]
