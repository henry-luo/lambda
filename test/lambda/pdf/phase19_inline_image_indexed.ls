// Phase 19 — inline Indexed images map packed indexes through the palette.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 2 },
            { key: "H", value: 1 },
            { key: "CS", value: ["Indexed", "DeviceRGB", 1, "ABCDEF"] },
            { key: "BPC", value: 4 }
        ],
        data: chr(1)
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_first: has(xml, "fill=\"rgb(65,66,67)\""),
        has_second: has(xml, "fill=\"rgb(68,69,70)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        xml: xml
    })
}
