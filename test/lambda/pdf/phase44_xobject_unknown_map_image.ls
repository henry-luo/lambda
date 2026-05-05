// Phase 44 - raw Image XObjects default unknown map ColorSpaces to RGB.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }
fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn image_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: data } }
}


pn main() {
    let img_dict = {
        Type: "XObject", Subtype: "Image",
        Width: 1, Height: 1,
        ColorSpace: { Foo: "Bar" },
        BitsPerComponent: 8
    }
    let doc = {
        objects: [image_obj(84, img_dict, "ABC")],
        pages: []
    }
    let page = { dict: { Resources: { XObject: { Im0: ref(84) } } } }
    let ops = [
        { op: "cm", operands: [2.0, 0.0, 0.0, 3.0, 4.0, 5.0] },
        { op: "Do", operands: [name("Im0")] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        rgb_fallback: has(xml, "fill=\"rgb(65,66,67)\""),
        no_img_handle: not has(xml, "href=\"img:84\""),
        kept_ctm: has(xml, "transform=\"matrix(2 0 0 3 4 5)\"")
    })
}