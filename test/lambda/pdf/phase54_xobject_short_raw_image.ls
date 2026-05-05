// Phase 54 - short raw Image XObjects render available pixels and black gaps.

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
        Width: 2, Height: 1,
        ColorSpace: "DeviceRGB",
        BitsPerComponent: 8
    }
    let doc = { objects: [image_obj(154, img_dict, "ABC")], pages: [] }
    let page = { dict: { Resources: { XObject: { Im0: ref(154) } } } }
    let ops = [
        { op: "cm", operands: [2.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "Do", operands: [name("Im0")] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        first_rgb: has(xml, "fill=\"rgb(65,66,67)\""),
        missing_black: has(xml, "fill=\"rgb(0,0,0)\""),
        no_img_handle: not has(xml, "href=\"img:154\""),
        kept_ctm: has(xml, "transform=\"matrix(2 0 0 1 10 20)\"")
    })
}
