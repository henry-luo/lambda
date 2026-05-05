// Phase 64 - inline Separation image pixels use grayscale tint fallback.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: ["Separation", "Spot", "DeviceGray", null] },
            { key: "BPC", value: 8 }
        ],
        data: "@"
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        tint_pixel: has(xml, "fill=\"rgb(191,191,191)\""),
        not_raw_gray: not has(xml, "fill=\"rgb(64,64,64)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)")
    })
}
