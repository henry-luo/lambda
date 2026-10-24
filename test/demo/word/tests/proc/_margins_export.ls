import model: ~~.~~.mod_model
import page: ~~.~~.mod_page
import files: ~~.~~.mod_io
import stories: ~~.~~.mod_stories
import lambda.editor.mod_doc

pn main() {
    let doc = node('doc',[for (i in 1 to 3) node_attrs('p',if (i==1) [] else [{name:'data-word-page-break',value:"true"}],
        [text("Marker " ++ string(i) ++ ": Text begins at the page margin.")])])
    let facing = model.execute(model.open_editor(doc),"layout.pageSetup",page.margin_presets[4].values)^
    let headed = model.execute(facing,"layout.headerFooter",{*:stories.defaults,header:"Facing pages",footer:"Page {PAGE} of {NUMPAGES}"})^
    files.export_document(headed.doc,"Facing pages","temp/word/margins-facing.pdf")^
    files.export_document(headed.doc,"Facing pages","temp/word/margins-facing.html")^
    let columns = model.execute(headed,"layout.columns",{count:2,gap:8})^
    files.export_document(columns.doc,"Facing columns","temp/word/margins-columns.pdf")^
    let changed = [for (p in page.margin_presets) model.execute(model.open_editor(node('doc',[node('p',[text("Margin edge")])])),"layout.pageSetup",p.values)^]
    for (i,p in page.margin_presets) {
        files.export_document(changed[i].doc,p.label,"temp/word/margins-" ++ p.id ++ ".pdf")^
    }
    print([files.import_html_document(input("temp/word/margins-facing.html",'text')^)^==headed.doc,
        all([for (p in page.margin_presets) exists("temp/word/margins-" ++ p.id ++ ".pdf")]),
        exists("temp/word/margins-columns.pdf")])
}
