import model: ~~.mod_model
import files: ~~.mod_io
import stories: ~~.mod_stories
import radiant
import lambda.editor.mod_doc

pn main() {
    let doc = node('doc',[for (i in 1 to 100) node('p',[text("Paragraph " ++ string(i) ++
        ": A flowing document keeps every sentence in order across columns and pages.")])])
    let two = model.execute(model.open_editor(doc),"layout.columns",{count:2,gap:12.7,line:true})^
    let headed = model.execute(two,"layout.headerFooter",{*:stories.defaults,header:"A & B < C",footer:"Page {PAGE} of {NUMPAGES}"})^
    output(files.pdf_document(headed.doc,"Columns")^,"temp/word/columns-long.xhtml",{format:'text'})^
    let pdf = files.export_document(headed.doc,"Columns","temp/word/columns-long.pdf")^
    let defaults = radiant.export_document("<html><body>HTML fallback</body></html>","temp/word/columns-default.svg",
        {format:"svg",width:240,height:160})
    let explicit = radiant.export_document("<html><body>HTML fallback</body></html>","temp/word/columns-explicit.svg",
        {format:"svg",width:240,height:160,source_format:"html"})
    let unknown = radiant.export_document("<p>Invalid</p>","temp/word/columns-invalid.svg",{format:"svg",source_format:"xml"})
    let wrong_type = radiant.export_document("<p>Invalid</p>","temp/word/columns-invalid.svg",{format:"svg",source_format:true})
    let malformed = radiant.export_document("<html><body/></html>","temp/word/columns-invalid.svg",
        {format:"svg",source_format:"xhtml"})
    print([pdf,defaults==true,explicit==true,
        input("temp/word/columns-default.svg",'text')^==input("temp/word/columns-explicit.svg",'text')^,
        contains(unknown,"source_format"),contains(wrong_type,"source_format"),malformed is string,not exists("temp/word/columns-invalid.svg")])
}
