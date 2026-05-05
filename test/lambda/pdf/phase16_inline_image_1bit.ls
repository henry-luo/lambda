// Phase 16 — inline 1-bit DeviceGray images render as pixels.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 4 },
            { key: "H", value: 1 },
            { key: "CS", value: "G" },
            { key: "BPC", value: 1 }
        ],
        data: chr(160)
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_white: has(xml, "fill=\"rgb(255,255,255)\""),
        has_black: has(xml, "fill=\"rgb(0,0,0)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        xml: xml
    })
}
