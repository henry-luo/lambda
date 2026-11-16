import adapter: lambda.editor.format.docx
import docx: lambda.doc.docx
import pkg: lambda.doc.docx.package
import lambda.editor.core.doc

pn main() {
  let snapshot = input("test/fixtures/edit/docx-preserved.docx", 'binary')^
  let opened = adapter.open_snapshot(snapshot)^
  let baseline = pkg.open_package(opened.envelope.archive)^
  let same = adapter.export_tree(opened.doc, opened.envelope)^
  let bold = text_marked("Changed & bold", [{name:'strong', value:true}])
  let changed = replace_node_at(opened.doc, [0, 0], bold)^
  let written = adapter.export_tree(changed, opened.envelope)^
  output(written.tree, "temp/editor-docx-preserved.docx", {format:'zip', atomic:true})^
  let reloaded = pkg.open_package("temp/editor-docx-preserved.docx")^
  let original_xml = input(baseline.parts[baseline.main], 'text')^
  let updated_xml = input(reloaded.parts[reloaded.main], 'text')^
  let removed = node('doc', [changed.content[0]])
  let protected_change = replace_node_at(changed, [1], node('p', [text("bad")]))^
  let bad_edit = adapter.export_tree(protected_change, opened.envelope) ^ { null }
  let bad_remove = adapter.export_tree(removed, opened.envelope) ^ { null }
  let inserted = node('doc', [changed.content[0], node('p', [text("Inserted")]), changed.content[1]])
  let insert_out = adapter.export_tree(inserted, opened.envelope)^
  output(insert_out.tree, "temp/editor-docx-insert.docx", {format:'zip', atomic:true})^
  let delete_safe = node('doc', [opened.doc.content[1]])
  let delete_out = adapter.export_tree(delete_safe, opened.envelope)^
  output(delete_out.tree, "temp/editor-docx-delete.docx", {format:'zip', atomic:true})^;
  [len(opened.doc.content) == 2, opened.envelope.records[0].editable,
   not opened.envelope.records[1].editable, opened.doc.content[1].tag == 'html_block',
   not same.changed, same.tree == opened.envelope.archive, written.changed,
   contains(docx.extract_text("temp/editor-docx-preserved.docx")^, "Changed & bold"),
   contains(updated_xml, "<w:b w:val=\"1\""),
   contains(original_xml, "<w:unknown>") and contains(updated_xml, "<w:unknown>"),
   len(baseline.parts) == len(reloaded.parts) and all([for (key,part in baseline.parts
     where (string(key)^) != baseline.main) input(part,'binary')^ == input(reloaded.parts[key],'binary')^]),
   bad_edit == null, bad_remove == null,
   contains(docx.extract_text("temp/editor-docx-insert.docx")^, "Inserted"),
   not contains(docx.extract_text("temp/editor-docx-delete.docx")^, "Hello, DOCX!"),
   contains(docx.extract_text("temp/editor-docx-delete.docx")^, "Protected content")]
}
