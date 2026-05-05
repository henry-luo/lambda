// Phase 22 — unfiltered raw Image XObjects render as pixels like native C++.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }

fn ind_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: data } }
}

fn name(s) { { kind: "name", value: s } }

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let img_dict = {
        Type: "XObject", Subtype: "Image",
        Width: 2, Height: 1,
        ColorSpace: "DeviceRGB",
        BitsPerComponent: 8
    }
    let doc = {
        objects: [ ind_obj(42, img_dict, "ABCDEF") ],
        pages: []
    }
    let page = {
        dict: { Resources: { XObject: { Im0: ref(42) } } }
    }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [2.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let elems = r.paths
    let xml = format(elems[0], 'xml')
    print({
        count: len(elems),
        has_outer: has(xml, "transform=\"matrix(2 0 0 1 10 20)\""),
        has_first: has(xml, "fill=\"rgb(65,66,67)\""),
        has_second: has(xml, "fill=\"rgb(68,69,70)\""),
        has_img_handle: has(xml, "href=\"img:42\""),
        xml: xml
    })
}
