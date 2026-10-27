import files: ~~.~~.mod_io
import document: ~~.~~.mod_document
import page: ~~.~~.mod_page
import docx: lambda.doc.docx
import lambda.editor.mod_doc
import lambda.editor.mod_transaction
import lambda.editor.mod_source_pos
pn main() {
    let doc = document.sample()
    let result = files.export_document(doc,"Exported studio","temp/docx-export/word.docx")^
    let loaded = files.read_document("temp/docx-export/word.docx")^
    let html = files.html_document(loaded.editor.doc,"Reopened")^
    let own = files.new_session(doc,"Original");
    [
        len(result.diagnostics)==0,
        loaded.session.path=="",
        len(loaded.warnings)==0,
        contains(html,"font-size:38.6667px") and contains(html,"Georgia"),
        docx.convert("temp/docx-export/word.docx")^.metadata.title=="Exported studio",
        page.settings(loaded.editor.doc).paper==page.settings(doc).paper,
        abs(page.settings(loaded.editor.doc).left-page.settings(doc).left)<0.3,
        own.path=="" and own.saved_doc==doc,
        len([for (entry in descendants(loaded.editor.doc) where entry.node.tag=='table') entry])==1,
        len([for (entry in descendants(loaded.editor.doc) where entry.node.tag=='tr') entry])==4,
        all([for (entry in descendants(loaded.editor.doc) where entry.node.tag=='td') contains(attrs_get(entry.node.attrs,'style'),"width:")])
    ]
}
