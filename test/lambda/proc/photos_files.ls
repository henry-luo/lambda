import img: lambda.image
import editor: lambda.editor.raster
import io
import files: ~~.~~.demo.photos.mod_files

pn main() {
    io.mkdir("temp/photos") ^ { null }
    let red = img.blank({width:4,height:3,color:[1.0,0.0,0.0,1.0]})^
    let blue = img.blank({width:2,height:2,color:[0.0,0.0,1.0,1.0]})^
    var st = editor.open_session(editor.new_document({width:4,height:3})^ )^
    for (entry in [{raster:red,name:"Background"},{raster:blue,name:"Blue"},{raster:blue,name:"Copy"}]) {
        st = editor.exec(st,{id:"layer.insert",raster:entry.raster,name:entry.name,position:[-1,0]})^
    }
    files.write_document(st,"temp/photos/unmasked.psd","psd")^
    st = editor.exec(st,{id:"mask.add"})^
    let result = files.write_document(st,"temp/photos/fixture.psd","psd")^
    let restored = files.open_document("temp/photos/fixture.psd")^
    let structure = [len(editor.layers(restored.doc)) == 3,
        [for (row in editor.layers(restored.doc)) row.node.name] == ["Copy","Blue","Background"],
        img.render_frame(restored.doc)^ == img.render_frame(st.doc)^]
    files.write_document(st,"temp/photos/fixture.png","png")^
    let opened = files.open_document("temp/photos/fixture.png")^
    let flat = img.render_frame(opened.doc)^ == img.render_frame(st.doc)^
    var failed = false
    files.open_document("temp/photos/missing-image.psd") ^ { failed = true }
    let failed_open = failed and st.doc == result.snapshot
    failed = false
    output("blocking file","temp/photos/blocked")^
    files.write_document(st,"temp/photos/blocked/document.psd","psd") ^ { failed = true }
    return [structure,flat,failed_open,failed and img.read("temp/photos/fixture.psd")^.width == 4]
}
