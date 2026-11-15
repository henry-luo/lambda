import package: lambda.editor.format.package
import docx: lambda.doc.docx
import ebook: lambda.doc.ebook

fn members(node) array^ => [for (child in content(node)^)
  if (child.kind == 'dir') *members(child)^ else child]
fn member(node, path) any^ => [for (item in members(node)^ where item.entry_path == path) item][0]
fn unchanged(original, written, changed) any^ {
  let old = members(original)^
  let new = members(written)^;
  len(old) == len(new) and all([for (item in old where item.entry_path != changed)
    parse(item, 'binary')^ == parse(member(written, item.entry_path), 'binary')^])
}
pn main() {
  let doc = input("test/input/docx/minimal.docx", 'zip')^
  let doc_path = "word/document.xml"
  let xml = parse(member(doc, doc_path), 'text')^
  let updated = replace(xml, "Hello, DOCX!", "Edited DOCX!")^
  let tree = package.replace_members(doc, map([(doc_path, updated)]))^
  output(tree, "temp/edit-viewer/package-edited.docx", {format:'zip', atomic:true})^
  let written = input("temp/edit-viewer/package-edited.docx", 'zip')^
  let book = input("test/input/ebook/minimal.epub", 'zip')^
  let chapter = "Books/text/one.xhtml"
  let html = parse(member(book, chapter), 'text')^
  let changed = replace(html, "First chapter", "Edited Chapter")^
  let book_tree = package.replace_members(book, map([(chapter, changed)]))^
  output(book_tree, "temp/edit-viewer/package-edited.epub", {format:'zip', compression:'stored', atomic:true})^
  let book_written = input("temp/edit-viewer/package-edited.epub", 'zip')^
  let loaded_book = ebook.read("temp/edit-viewer/package-edited.epub")^;
  [updated != xml, changed != html,
   unchanged(doc, written, doc_path)^,
   contains(docx.extract_text("temp/edit-viewer/package-edited.docx")^, "Edited DOCX!"),
   unchanged(book, book_written, chapter)^,
   contains(ebook.chapter(loaded_book, 0)^.text, "Edited Chapter"),
   member(doc, doc_path)^.mode == member(written, doc_path)^.mode,
   member(book, chapter)^.mode == member(book_written, chapter)^.mode,
   (package.replace_members(doc, {'missing.xml':"x"}) ^ { null }) == null,
   (package.replace_members(doc, {'word/document.xml':1}) ^ { null }) == null]
}
