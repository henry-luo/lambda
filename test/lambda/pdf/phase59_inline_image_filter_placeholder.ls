// Phase 59 - filtered inline images use a placeholder instead of raw pixels.

import image: lambda.package.pdf.image

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: "DeviceRGB" },
            { key: "BPC", value: 8 },
            { key: "F", value: name("FlateDecode") }
        ],
        data: "ABC"
    }
    let elems = image.apply_inline([1.0, 0.0, 0.0, 1.0, 0.0, 0.0], [info])
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        has_dash: has(xml, "stroke-dasharray=\"0.05,0.05\""),
        not_raw_rgb: not has(xml, "fill=\"rgb(65,66,67)\"")
    })
}
