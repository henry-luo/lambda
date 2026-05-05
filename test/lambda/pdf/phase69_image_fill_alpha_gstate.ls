// Phase 69 - ExtGState ca applies to inline and XObject image painting.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }
fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn ind_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: data } }
}

pn main() {
    let img_dict = {
        Type: "XObject", Subtype: "Image",
        Width: 1, Height: 1,
        ColorSpace: "DeviceRGB",
        BitsPerComponent: 8
    }
    let doc = {
        objects: [ind_obj(42, img_dict, "ABC")],
        pages: []
    }
    let page = {
        dict: {
            Resources: {
                ExtGState: { A35: { ca: 0.35 } },
                XObject: { Im0: ref(42) }
            }
        }
    }
    let inline_info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: "DeviceRGB" },
            { key: "BPC", value: 8 }
        ],
        data: "DEF"
    }
    let ops = [
        { op: "gs", operands: [name("A35")] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 20.0, 0.0] },
        { op: "inline_image", operands: [inline_info] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xobj_xml = format(r.paths[0], 'xml')
    let inline_xml = format(r.paths[1], 'xml')
    print({
        path_count: len(r.paths),
        xobject_alpha: has(xobj_xml, "opacity=\"0.35\""),
        inline_alpha: has(inline_xml, "opacity=\"0.35\""),
        xobject_pixel: has(xobj_xml, "fill=\"rgb(65,66,67)\""),
        inline_pixel: has(inline_xml, "fill=\"rgb(68,69,70)\"")
    })
}
