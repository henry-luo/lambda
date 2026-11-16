import adapter: lambda.editor.format.docx
import package: lambda.editor.format.package
import pkg: lambda.doc.docx.package
import docx: lambda.doc.docx
import lambda.editor.core.doc

pn main() {
  let captured = input("test/input/docx/minimal.docx", 'zip')^
  let original = pkg.open_package(captured)^
  let source = input(original.parts[original.main], 'text')^
  let spaced = replace(source, "Hello, DOCX!", "Hello  DOCX!")^
  let tree = package.replace_members(captured, map([(original.main, spaced)]))^
  output(tree, "temp/editor-docx-spaces.docx", {format:'zip', atomic:true})^
  let opened = adapter.open_snapshot(input("temp/editor-docx-spaces.docx", 'binary')^ )^
  let initial = opened.doc.content[0].content[0].text
  let changed = replace_node_at(opened.doc, [0, 0], text("Hello  DOCX! edited"))^
  let written = adapter.export_tree(changed, opened.envelope)^
  output(written.tree, "temp/editor-docx-spaces-saved.docx", {format:'zip', atomic:true})^
  let reopened = adapter.open_snapshot(input("temp/editor-docx-spaces-saved.docx", 'binary')^)^;
  let multiline = package.replace_members(captured,
    map([(original.main, replace(source, "Hello, DOCX!", "Hello\nDOCX!")^)]))^
  output(multiline, "temp/editor-docx-multiline.docx", {format:'zip', atomic:true})^
  let protected = adapter.open_snapshot(input("temp/editor-docx-multiline.docx", 'binary')^ )^
  let invalid = replace_node_at(opened.doc, [0, 0], text("Hello\nDOCX!"))^;
  [opened.envelope.records[0].editable, initial == "Hello  DOCX!",
   contains(docx.extract_text("temp/editor-docx-spaces-saved.docx")^, "Hello  DOCX! edited"),
   reopened.doc.content[0].content[0].text == "Hello  DOCX! edited",
   not protected.envelope.records[0].editable, adapter.validate_model(invalid, opened.envelope) != null]
}
