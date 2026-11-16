import adapter: lambda.editor.format.docx
import package: lambda.editor.format.package
import pkg: lambda.doc.docx.package
import docx: lambda.doc.docx
import lambda.editor.core.doc

pn main() {
  let captured = input("test/input/docx/minimal.docx", 'zip')^
  let original = pkg.open_package(captured)^
  let source = input(original.parts[original.main], 'text')^
  let old = "<w:body><w:p><w:r><w:t xml:space=\"preserve\">Hello, DOCX!</w:t></w:r></w:p></w:body>"
  let empty = replace(source, old, "<w:body/>")^
  let tree = package.replace_members(captured, map([(original.main, empty)]))^
  output(tree, "temp/editor-docx-empty.docx", {format:'zip', atomic:true})^
  let opened = adapter.open_snapshot(input("temp/editor-docx-empty.docx", 'binary')^)^
  let new_doc = node('doc', [node('p', [text("New document")])])
  let saved = adapter.export_tree(new_doc, opened.envelope)^
  output(saved.tree, "temp/editor-docx-empty-saved.docx", {format:'zip', atomic:true})^;
  [empty != source, len(opened.doc.content) == 1, opened.doc.content[0].tag == 'p',
   saved.changed, contains(docx.extract_text("temp/editor-docx-empty-saved.docx")^, "New document")]
}
