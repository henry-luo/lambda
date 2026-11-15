import editor: lambda.editor.format.epub
import ebook: lambda.doc.ebook
import lambda.editor.core.doc

fn members(node) array^ => [for (child in content(node)^)
  if (child.kind == 'dir') *members(child)^ else child]
fn member(node, path) any^ => [for (item in members(node)^ where item.entry_path == path) item][0]

pn main() {
  let snapshot = input("test/input/ebook/minimal.epub", 'binary')^
  let publication = editor.open_snapshot(snapshot)^
  let chapter = editor.open_chapter(publication, 0)^
  let same = editor.export_tree(chapter, chapter.doc)^
  let changed_doc = replace_node_at(chapter.doc, [0, 0], text("Edited chapter"))^
  let edited = editor.export_tree(chapter, changed_doc)^
  output(edited.tree, "temp/edit-viewer/epub-chapter-edit.epub", {format:'zip', compression:'stored', atomic:true})^
  let written = input("temp/edit-viewer/epub-chapter-edit.epub", 'zip')^
  let reopened = ebook.read("temp/edit-viewer/epub-chapter-edit.epub")^
  let old_members = members(publication.archive)^
  let new_members = members(written)^
  let signed_book = {*:publication.book, resources:{*:publication.book.resources, 'META-INF/signatures.xml':"signature"}}
  let signed_chapter = {*:chapter, publication:{*:publication, book:signed_book}}
  let signed_same = editor.export_tree(signed_chapter, signed_chapter.doc)^;
  [not same.changed and same.tree == publication.archive,
   chapter.entry.href == "Books/text/one.xhtml",
   edited.changed and contains(ebook.chapter(reopened, 0)^.text, "Edited chapter"),
   len(old_members) == len(new_members) and all([for (item in old_members where item.entry_path != chapter.entry.href)
     parse(item, 'binary')^ == parse(member(written, item.entry_path)^, 'binary')^]),
   contains(ebook.chapter(reopened, 0)^.text, "Café 世界 😀"),
   (editor.open_chapter(publication, -1) ^ { null }) == null,
   (editor.open_chapter(publication, 200) ^ { null }) == null,
   not signed_same.changed,
   (editor.export_tree(signed_chapter, changed_doc) ^ { null }) == null]
}
