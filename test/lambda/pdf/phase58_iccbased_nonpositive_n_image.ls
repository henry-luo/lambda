// Phase 58 - raw ICCBased image N <= 0 defaults to RGB like native.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: ["ICCBased", { N: 0 }] },
            { key: "BPC", value: 8 }
        ],
        data: "ABC"
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        rgb_default: has(xml, "fill=\"rgb(65,66,67)\""),
        no_black_fallback: not has(xml, "fill=\"rgb(0,0,0)\""),
        no_placeholder: not has(xml, "rgba(200,200,200,0.4)")
    })
}
