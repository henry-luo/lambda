// Phase 63 - raw Image XObjects apply Gamma after named CalRGB resolution.

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
        ColorSpace: "CalA",
        BitsPerComponent: 8
    }
    let doc = { objects: [image_obj(163, img_dict, "@A@")], pages: [] }
    let page = {
        dict: { Resources: {
            ColorSpace: { CalA: ["CalRGB", { Gamma: [2.0, 1.0, 0.5] }] },
            XObject: { Im0: ref(163) }
        } }
    }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 5.0, 6.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        gamma_pixel: has(xml, "fill=\"rgb(16,65,127)\""),
        not_raw_rgb: not has(xml, "fill=\"rgb(64,65,64)\""),
        no_img_handle: not has(xml, "href=\"img:163\""),
        kept_ctm: has(xml, "transform=\"matrix(1 0 0 1 5 6)\"")
    })
}
