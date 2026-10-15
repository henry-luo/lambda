import ebook: lambda.doc.ebook
import presentation: lambda.doc.ebook.reader
import store: lambda.doc.ebook.state

pub fn document(snapshot,mode = "scroll") element^ {
    let book = ebook.parse(snapshot)^;
    presentation.document(book,{path:null,location:null,bookmarks:[],
        preferences:{*:store.defaults,mode:mode},message:""})^
}
