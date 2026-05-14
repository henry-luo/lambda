// Phase 66 - DCTDecode Image XObjects keep their JPEG bytes as data URIs.

pn main() {
    let doc^err = input("test/pdf/somatosensory.pdf", 'pdf')
    var found = null
    var i = 0
    while (i < len(doc.objects)) {
        let obj = doc.objects[i]
        if (obj.object_num == 31) {
            found = obj
        }
        i = i + 1
    }

    var uri = ""
    if (found != null) {
        if (found.content != null) {
            if (found.content.data_uri != null) {
                uri = found.content.data_uri
            }
        }
    }

    print({
        found: found != null,
        jpeg_uri: starts_with(uri, "data:image/jpeg;base64,"),
        has_payload: len(uri) > 1000
    })
}
