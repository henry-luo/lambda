// Run: ./lambda.exe demo book
// An ebook library in the spirit of macOS Books: classics on the shelves, a
// Continue Reading row, collections and search; a book opens in the
// lambda.doc.ebook reader, which saves the place for the next visit.
import resources: ~~.mod_resources
import app: .mod_app

app.document(url_resolve(resources.entry_uri()^, "./"), true)
