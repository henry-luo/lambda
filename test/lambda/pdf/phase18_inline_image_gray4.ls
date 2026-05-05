// Phase 18 — inline 4-bit DeviceGray images expand nibbles to RGB pixels.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 2 },
            { key: "H", value: 1 },
            { key: "CS", value: "G" },
            { key: "BPC", value: 4 }
        ],
        data: chr(175)
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_mid: has(xml, "fill=\"rgb(170,170,170)\""),
        has_white: has(xml, "fill=\"rgb(255,255,255)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        xml: xml
    })
}
