// Phase 17 — inline 8-bit DeviceCMYK images convert to RGB pixels.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 2 },
            { key: "H", value: 1 },
            { key: "CS", value: "CMYK" },
            { key: "BPC", value: 8 }
        ],
        data: chr(0) ++ chr(255) ++ chr(255) ++ chr(0)
           ++ chr(255) ++ chr(0) ++ chr(255) ++ chr(0)
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_red: has(xml, "fill=\"rgb(255,0,0)\""),
        has_green: has(xml, "fill=\"rgb(0,255,0)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        xml: xml
    })
}
