import img: lambda.image
import editor: lambda.editor.raster
import files: lambda.editor.raster.files
import io
pn main() {
    io.mkdir("temp/photos") ^ { null }
    let image = img.blank({width:8,height:4,color:[1.0,0.0,0.0,0.5]})^
    let st = editor.exec(editor.open_session(editor.new_document({width:8,height:4})^)^,{id:"layer.insert",raster:image})^
    let png = files.write_document(st,"temp/photos/export-sized.png","png",{width:4})^
    let opened_png = img.render_frame(img.read("temp/photos/export-sized.png")^ )^
    let png_ok = opened_png.width == 4 and opened_png.height == 2 and img.sample(opened_png,0,0)^[3] == 128
    let jpg = files.write_document(st,"temp/photos/export-sized.jpeg","jpeg",{height:2,background:"#ffffff",quality:100})^
    let opened_jpg = img.render_frame(img.read("temp/photos/export-sized.jpeg")^ )^
    let sample = img.sample(opened_jpg,0,0)^
    let jpg_ok = opened_jpg.width == 4 and opened_jpg.height == 2 and sample[0]>245 and sample[1]>170 and sample[1]<205 and sample[3] == 255
    let high = input("temp/photos/export-sized.jpeg",'binary')^
    files.write_document(st,"temp/photos/export-sized.jpg","jpeg",{height:2,background:"#ffffff",quality:10})^
    let quality = input("temp/photos/export-sized.jpg",'binary')^ != high
    let snapshot = st.doc == png.snapshot and st.doc == jpg.snapshot and st.doc.width == 8 and len(st.hist.undo) == 1
    var failed = false
    files.write_document(st,"temp/photos/export-sized.jpeg","jpeg") ^ { failed = true }
    let preserved = failed and input("temp/photos/export-sized.jpeg",'binary')^ == high
    failed = false
    files.write_document(st,"temp/photos/export-sized.jpeg","jpeg",{background:"transparent"}) ^ { failed = true }
    let opaque = failed and input("temp/photos/export-sized.jpeg",'binary')^ == high
    return [png_ok,jpg_ok,quality,snapshot,preserved,opaque,jpg.report.export.width == 4 and jpg.report.export.quality == 100]
}
