// Phase 46 - raw Image XObjects default unknown name/array ColorSpaces to RGB.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }
fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn image_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: data } }
}


fn img_dict(cs) {
    {
        Type: "XObject", Subtype: "Image",
        Width: 1, Height: 1,
        ColorSpace: cs,
        BitsPerComponent: 8
    }
}

pn main() {
    let doc = {
        objects: [
            image_obj(85, img_dict("Mystery"), "ABC"),
            image_obj(86, img_dict(["MysteryArray", {}]), "DEF")
        ],
        pages: []
    }
    let page = { dict: { Resources: { XObject: { ImA: ref(85), ImB: ref(86) } } } }
    let ops = [
        { op: "Do", operands: [name("ImA")] },
        { op: "Do", operands: [name("ImB")] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml0 = format(r.paths[0], 'xml')
    let xml1 = format(r.paths[1], 'xml')
    print({
        count: len(r.paths),
        unknown_name_rgb: has(xml0, "fill=\"rgb(65,66,67)\""),
        unknown_array_rgb: has(xml1, "fill=\"rgb(68,69,70)\""),
        no_name_handle: not has(xml0, "href=\"img:85\""),
        no_array_handle: not has(xml1, "href=\"img:86\"")
    })
}