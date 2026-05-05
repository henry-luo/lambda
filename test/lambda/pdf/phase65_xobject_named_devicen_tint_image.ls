// Phase 65 - raw Image XObjects apply named DeviceN tint fallback.

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
        ColorSpace: "SpotN",
        BitsPerComponent: 8
    }
    let doc = { objects: [image_obj(165, img_dict, "@")], pages: [] }
    let page = {
        dict: { Resources: {
            ColorSpace: { SpotN: ["DeviceN", ["Spot"], "DeviceGray", null] },
            XObject: { Im0: ref(165) }
        } }
    }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 7.0, 8.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        tint_pixel: has(xml, "fill=\"rgb(191,191,191)\""),
        not_raw_gray: not has(xml, "fill=\"rgb(64,64,64)\""),
        no_img_handle: not has(xml, "href=\"img:165\""),
        kept_ctm: has(xml, "transform=\"matrix(1 0 0 1 7 8)\"")
    })
}
