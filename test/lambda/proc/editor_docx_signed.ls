import adapter: lambda.editor.format.docx
import lambda.editor.core.doc

pn main() {
  let snapshot = input("test/fixtures/edit/docx-signed.docx", 'binary')^
  let opened = adapter.open_snapshot(snapshot)^
  let same = adapter.export_tree(opened.doc, opened.envelope)^
  let edited = node('doc', [node('p', [text("altered")])])
  let denied = adapter.export_tree(edited, opened.envelope) ^ { null };
  [opened.envelope.signed, not opened.envelope.records[0].editable,
   opened.doc.content[0].tag == 'html_block', not same.changed, denied == null]
}
