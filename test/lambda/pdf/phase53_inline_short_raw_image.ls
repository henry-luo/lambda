// Phase 53 - short inline raw image data renders missing RGB bytes as black.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 2 },
            { key: "H", value: 1 },
            { key: "CS", value: "DeviceRGB" },
            { key: "BPC", value: 8 }
        ],
        data: "ABC"
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        first_rgb: has(xml, "fill=\"rgb(65,66,67)\""),
        missing_black: has(xml, "fill=\"rgb(0,0,0)\""),
        no_placeholder: not has(xml, "rgba(200,200,200,0.4)")
    })
}
