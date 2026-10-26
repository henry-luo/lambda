// The Books demo document for test/ui/book: no reading state is read or saved,
// so a run neither depends on nor changes the user's own library.
import resources: ~~.~~.demo.mod_resources
import app: ~~.~~.demo.book.mod_app

app.document(url_resolve(resources.entry_uri()^, "../../demo/book/"), false)
