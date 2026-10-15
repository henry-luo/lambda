import ebook: lambda.doc.ebook
import reader: .reader
import store: .state

pub pn render_document_file(path,options = null) element^ {
    let book = ebook.read(path)^
    let saved = store.restore(book,if (options == null) {} else options)^
    return reader.document(book,saved)^
}
