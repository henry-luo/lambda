// Phase 61 - inline CalGray image pixels apply Gamma like content colors.

import image: lambda.package.pdf.image

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: ["CalGray", { Gamma: 2.0 }] },
            { key: "BPC", value: 8 }
        ],
        data: "@"
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        gamma_pixel: has(xml, "fill=\"rgb(16,16,16)\""),
        not_raw_gray: not has(xml, "fill=\"rgb(64,64,64)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)")
    })
}
