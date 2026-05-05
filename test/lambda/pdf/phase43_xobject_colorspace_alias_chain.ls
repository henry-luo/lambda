// Phase 43 - Image XObjects resolve named ColorSpace aliases recursively.

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
        ColorSpace: "A",
        BitsPerComponent: 8
    }
    let doc = {
        objects: [image_obj(83, img_dict, chr(0) ++ chr(255) ++ chr(255) ++ chr(0))],
        pages: []
    }
    let page = {
        dict: { Resources: {
            ColorSpace: { A: "B", B: { N: 4 } },
            XObject: { Im0: ref(83) }
        } }
    }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 11.0, 12.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        alias_chain: has(xml, "fill=\"rgb(255,0,0)\""),
        no_img_handle: not has(xml, "href=\"img:83\""),
        kept_ctm: has(xml, "transform=\"matrix(1 0 0 1 11 12)\"")
    })
}