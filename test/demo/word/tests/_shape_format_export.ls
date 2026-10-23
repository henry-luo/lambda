import files: ~~.mod_io
import model: ~~.mod_model
import shapes: ~~.mod_shapes
import image: lambda.editor.mod_image
import lambda.editor.mod_step

pn main() {
    let saved = files.read_document("temp/word/shape-format.word.json")^
    let imported = files.import_html_document(input("temp/word/shape-format.html",'text')^)^
    let n = image.images(imported)[0].node
    let p = shapes.settings(n)
    // HTML normalizes whitespace and attribute types; compare canonical imported models.
    print([files.import_html_document(files.html_document(saved.editor.doc,"Shapes")^)^==imported,
        p.text=="Final sign" and p.font=="Georgia" and p.direction==90 and p.bold and p.italic,
        p.fill_style=="gradient" and p.fill2=="#dcebff" and p.dash=="dashed" and p.effect=="shadow" and p.opacity==85,
        attrs_get(n.attrs,'alt')=="Studio sign" and attrs_get(n.attrs,'src')==attrs_get(image.images(saved.editor.doc)[0].node.attrs,'src'),
        model.plain_text(saved.editor.doc)==model.plain_text(files.read_document("test/demo/word/tests/object_layout.word.json")^.editor.doc)])
}
