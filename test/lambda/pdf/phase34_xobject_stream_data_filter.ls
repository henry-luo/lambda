// Phase 34 — Image XObjects accept stream_data and stale non-pass-through filters.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }
fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn image_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, stream_data: data } }
}

pn main() {
    let img_dict = {
        Type: "XObject", Subtype: "Image",
        Width: 2, Height: 1,
        ColorSpace: "DeviceRGB",
        BitsPerComponent: 8,
        Filter: "FlateDecode"
    }
    let doc = {
        objects: [image_obj(71, img_dict, chr(255) ++ chr(0) ++ chr(0) ++ chr(0) ++ chr(255) ++ chr(0))],
        pages: []
    }
    let page = { dict: { Resources: { XObject: { Im0: ref(71) } } } }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [2.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "Do", operands: [name("Im0")] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        used_stream_data: has(xml, "fill=\"rgb(255,0,0)\"") and has(xml, "fill=\"rgb(0,255,0)\""),
        ignored_stale_filter: not has(xml, "href=\"img:71\""),
        kept_ctm: has(xml, "transform=\"matrix(2 0 0 1 10 20)\"")
    })
}
