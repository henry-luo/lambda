import adapter: lambda.editor.format.docx
import package: lambda.doc.docx.package
import docx: lambda.doc.docx
import lambda.editor.core.doc

pn main() {
  let snapshot = input("test/input/docx/reference/tables.docx", 'binary')^
  let opened = adapter.open_snapshot(snapshot)^
  let before = package.open_package(opened.envelope.archive)^
  let changed = replace_node_at(opened.doc, [1, 0, 0, 0, 0], text("Edited cell"))^
  let written = adapter.export_tree(changed, opened.envelope)^
  output(written.tree, "temp/editor-docx-table.docx", {format:'zip', atomic:true})^
  let after = package.open_package("temp/editor-docx-table.docx")^
  let text = docx.extract_text("temp/editor-docx-table.docx")^
  let table = changed.content[1]
  let broken = replace_node_at(changed, [1], node_attrs('table', table.attrs, []))^
  let denied = adapter.export_tree(broken, opened.envelope) ^ { null };
  [opened.envelope.records[1].table != null, opened.doc.content[1].tag == 'table',
   written.changed, contains(text, "Edited cell"), contains(text, "Top right"),
   contains(text, "Bottom left"), contains(text, "Bottom right"),
   all([for (key,part in before.parts where (string(key)^) != before.main)
     input(part,'binary')^ == input(after.parts[key],'binary')^]), denied == null]
}
