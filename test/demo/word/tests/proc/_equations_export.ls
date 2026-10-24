import fixture: ~~.fixtures.mod_export
import files: ~~.~~.mod_io
import model: ~~.~~.mod_model
import equation: ~~.~~.mod_equation
import image: lambda.editor.mod_image
import lambda.editor.mod_step

pn main() {
    let data = fixture.load("equations","Equations")^
    let saved = data.saved
    let imported = data.imported
    let n = image.images(imported)[0].node
    let p = equation.settings(n)
    print([files.import_html_document(files.html_document(saved.editor.doc,"Equations")^)^==imported,
        p.source=="\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}" and p.number=="(1)",
        p.display and not p.linear and p.justify=="center" and p.size==28 and p.color=="#805bc5",
        attrs_get(n.attrs,'src')==attrs_get(image.images(saved.editor.doc)[0].node.attrs,'src'),
        model.plain_text(saved.editor.doc)=="BeforeAfter" and model.plain_text(imported)=="BeforeAfter"])
}
