// Phase 4 — pipeline: drive interp.render_page with a hand-built op
// stream that contains q ... cm /Im0 Do Q. Verifies that the Do op:
//   - is dispatched through image.apply_do
//   - sees the live ctm (set by `cm`)
//   - emits an <image href="img:42" .../> wrapped in a <g transform=...>
//   - sits in the `paths` list (PDF user space) so it goes inside the
//     page-level y-flip group alongside vector paths
//
// Operator stream synthesized directly (no content-stream tokenizer call)
// so we don't depend on stream.parse_content_stream operand shapes here;
// that path is already covered by phase2/phase3 tests.

import interp: lambda.package.pdf.interp

fn ref(n) {
    { type: "indirect_ref", object_num: n }
}

fn ind_obj(n, dict) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, data: "" } }
}

fn name(s) { { kind: "name", value: s } }

pn main() {
    let img_dict = { Type: "XObject", Subtype: "Image", Width: 200, Height: 100 }
    let doc = {
        objects: [ ind_obj(42, img_dict) ],
        pages:   []
    }
    let page = {
        dict: {
            Resources: { XObject: { Im0: ref(42) } }
        }
    }

    // q / cm 200 0 0 100 50 600 / Do /Im0 / Q
    let ops = [
        { op: "q",  operands: [] },
        { op: "cm", operands: [200.0, 0.0, 0.0, 100.0, 50.0, 600.0] },
        { op: "Do", operands: [ name("Im0") ] },
        { op: "Q",  operands: [] }
    ]

    let r = interp.render_page(doc, page, ops, 800.0)

    print({
        text_count: len(r.texts),
        path_count: len(r.paths),
        first_xml:  format(r.paths[0], 'xml')
    })
}
