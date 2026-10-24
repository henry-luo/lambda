import fixture: ~~.fixtures.mod_export
import files: ~~.~~.mod_io
import model: ~~.~~.mod_model
import groups: ~~.~~.mod_group_data
import layout: ~~.~~.mod_object_layout
import image: lambda.editor.mod_image
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

pn main() {
    let data = fixture.load("object-layout","Layout")^
    let saved = data.saved
    let imported = data.imported
    let selected = edit_set_selection(saved.editor,node_selection(image.images(saved.editor.doc)[0].path))
    let recovered = model.execute(selected,"object.ungroup")^
    let original = files.read_document("test/demo/word/tests/object_layout.word.json")^
    let shape = edit_set_selection(original.editor,node_selection([1,0]))
    for (w in layout.wraps) {
        let edited = model.execute(shape,"object.layout",{values:{wrap:w.id,x:180,y:10,distance:12}})^
        files.export_document(edited.doc,"Object wrapping","temp/word/object-layout-" ++ w.id ++ ".pdf")^
    }
    // HTML normalizes paragraph-edge whitespace and attribute types; compare its canonical model.
    print([files.import_html_document(files.html_document(saved.editor.doc,"Layout")^)^==imported,
        groups.data(image.images(imported)[0].node)!=null,
        len(image.images(recovered.doc))==3,model.plain_text(recovered.doc)==model.plain_text(saved.editor.doc)])
}
