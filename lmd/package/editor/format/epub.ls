// EPUB chapters use the existing XHTML rich model and retain other members
// from the captured publication (D9.3.4, D10.1.4).
import ebook: lambda.doc.ebook
import epub: lambda.doc.ebook.epub
import xhtml: lambda.editor.format.xhtml
import package: lambda.editor.format.package

pub fn open_snapshot(snapshot) map^ {
  if (not (snapshot is binary)) raise error("editor EPUB: expected archive bytes")
  else {
    let archive = parse(snapshot, 'zip')^
    let book = ebook.parse(archive)^;
    {archive:archive, book:book}
  }
}

pub fn open_chapter(publication, index) map^ {
  if (not (index is int) or index < 0 or index >= len(publication.book.reading_order))
    raise error("editor EPUB: chapter does not exist")
  else {
    let entry = publication.book.reading_order[index]
    if (entry.media_type != "application/xhtml+xml")
      raise error("editor EPUB: this chapter is not XHTML")
    else {
      let source = epub.source_text(publication.book, entry.href)^
      let loaded = xhtml.import_text(source)^
      let mismatch = xhtml.check_roundtrip(loaded.doc, loaded.envelope)^
      if (mismatch != null) raise error("editor EPUB: " ++ mismatch)
      else {publication:publication, entry:entry, doc:loaded.doc, envelope:loaded.envelope}
    }
  }
}

// Return the captured archive unchanged on a no-op. An actual change must be
// revalidated before its chapter replaces the original member.
pub fn export_tree(chapter, doc) map^ {
  let publication = chapter.publication
  if (doc == chapter.doc) {tree:publication.archive, changed:false}
  else if (publication.book.resources["META-INF/signatures.xml"] != null)
    raise error("editor EPUB: signed publications cannot be edited without invalidating signatures")
  else {
    let mismatch = xhtml.check_roundtrip(doc, chapter.envelope)^
    if (mismatch != null) raise error("editor EPUB: " ++ mismatch)
    else {
      let written = xhtml.export_text(doc, chapter.envelope)^
      let tree = package.replace_members(publication.archive, map([(chapter.entry.href, written)]))^;
      {tree:tree, changed:true}
    }
  }
}
