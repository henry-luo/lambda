// Phase 4 — image XObject (Do operator) handling.
//
// Hand-builds a fake PDF tree with one Image and one Form XObject in the
// page's /Resources/XObject map, then drives image.apply_do directly to
// validate:
//   - Image XObject → <g transform="..."><image href="img:42" .../></g>
//   - Form XObject  → <g transform="..." data-pdf-form="img:99">...</g>
//   - unknown name  → empty list
//
// The fake doc mirrors the shape input(*.pdf, 'pdf') produces:
//   doc = { objects: [...indirect_object wrappers...], pages: [page] }
//   page.dict.Resources.XObject = { Im0: indirect_ref(42), Fm0: indirect_ref(99) }
//   indirect_object(42).content = { dictionary: { Subtype: "Image", Width: 100, Height: 50 }, data: "" }

import image: lambda.package.pdf.image

fn ref(n) {
    { type: "indirect_ref", object_num: n }
}

fn ind_obj(n, dict) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: "" } }
}

fn name(s) { { kind: "name", value: s } }

pn main() {
    let img_dict  = { Type: "XObject", Subtype: "Image", Width: 100, Height: 50 }
    let form_dict = { Type: "XObject", Subtype: "Form" }

    let doc = {
        objects: [ ind_obj(42, img_dict), ind_obj(99, form_dict) ],
        pages: []
    }
    let page = {
        dict: {
            Resources: { XObject: { Im0: ref(42), Fm0: ref(99) } }
        }
    }

    // ctm = scale(100, 50) translate(72, 720) — image placed at (72,720)
    // with size 100×50 in PDF user space. PDF concat: CTM = [100 0 0 50 72 720].
    let ctm = [100.0, 0.0, 0.0, 50.0, 72.0, 720.0]

    let img_emit  = image.apply_do(doc, page, ctm, [name("Im0")])
    let form_emit = image.apply_do(doc, page, ctm, [name("Fm0")])
    let bad_emit  = image.apply_do(doc, page, ctm, [name("Nope")])

    // Direct lookup for kind detection
    let xo_img  = image.lookup_xobject(doc, page, "Im0")
    let xo_form = image.lookup_xobject(doc, page, "Fm0")
    let xo_bad  = image.lookup_xobject(doc, page, "Nope")

    print({
        img_count:   len(img_emit),
        img_xml:     format(img_emit[0], 'xml'),
        form_count:  len(form_emit),
        form_xml:    format(form_emit[0], 'xml'),
        bad_count:   len(bad_emit),
        kinds:       { im: xo_img.kind, fm: xo_form.kind, bad: (xo_bad == null) }
    })
}
