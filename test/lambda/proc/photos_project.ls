import img: lambda.image
import project: lambda.image.project
import editor: lambda.editor.raster
import files: ~~.~~.demo.photos.mod_files
import io
pn main() {
    io.mkdir("temp/photos") ^ { null }
    let st = editor.open_session(editor.new_document({width:8,height:8})^)^
    let layer = editor.exec(st,{id:"layer.new"})^
    let painted = editor.exec(layer,{id:"paint.fill",color:[0.9,0.1,0.3,0.8]})^
    let effected = editor.exec(painted,{id:"effect.add",options:{gamma:1.2}})^
    let transformed = editor.exec(effected,{id:"layer.transform",angle:30})^
    let saved = files.write_document(transformed,"temp/photos/project-test.photos","photos")^
    let loaded = files.open_document("temp/photos/project-test.photos")^
    return [img.render_frame(loaded.doc)^ == img.render_frame(transformed.doc)^,
        editor.layers(loaded.doc)[0].node.transform == editor.layers(transformed.doc)[0].node.transform,
        saved.snapshot == transformed.doc,not editor.dirty(loaded)]
}
