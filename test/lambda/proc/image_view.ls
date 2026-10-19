import img: lambda.image
import viewer: lambda.doc.image

pn main() {
    let path = "test/input/image/layered.psd"
    let document = img.read(path)^
    let pixels = img.render_frame(document)^
    print([pixels.r[8, 8] == 255, pixels.b[8, 56] == 255, pixels.r[8, 80] == 255])
    print("\n")
    let page = viewer.render_document_file(path)^
    let body = page[1]
    let picture = body[0]
    print([body['data-rdt-document'], picture.width, picture.height,
        starts_with(picture.src, "data:image/png;base64,")])
    print("\n")
    output(b'\x00', "temp/image_view_broken.psd")^
    var failed = false
    viewer.render_document_file("temp/image_view_broken.psd") ^ { failed = true }
    print(failed)
    print("\n")
    return true
}
