// Phase 33 — Form XObjects use their own Resources and stream_data bytes.

import interp: lambda.package.pdf.interp

fn ref(n) { { type: "indirect_ref", object_num: n } }
fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn form_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, stream_data: data } }
}

pn main() {
    let form_dict = {
        Type: "XObject", Subtype: "Form",
        Matrix: [1, 0, 0, 1, 30, 40],
        Resources: { Font: { FF: { BaseFont: "Courier-Bold" } } }
    }
    let doc = {
        objects: [form_obj(70, form_dict, "BT /FF 12 Tf 1 0 0 1 5 6 Tm (InForm) Tj ET")],
        pages: []
    }
    let page = {
        dict: { Resources: { XObject: { Fm0: ref(70) } } }
    }
    let ops = [{ op: "Do", operands: [name("Fm0")] }]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.texts[0], 'xml')
    print({
        text_count: len(r.texts),
        path_count: len(r.paths),
        used_stream_data: has(xml, "InForm"),
        used_form_matrix: has(xml, "x=\"35\"") and has(xml, "y=\"54\""),
        used_form_font: has(xml, "Courier") and has(xml, "font-weight=\"bold\"")
    })
}
