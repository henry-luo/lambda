// Phase 7 — clipping (W operator).
//
// Drives interp.render_page with a synthetic op stream that:
//   1. Builds a 100×100 rectangle path.
//   2. Marks it as the clipping region with W.
//   3. Issues `n` (no-op paint) which establishes the clip and clears
//      the path.
//   4. Draws a 200×200 filled rect that should be wrapped in
//      <g clip-path="url(#clip0)">.
//
// Expected: paths contains a <clipPath id="clip0"> followed by the
// big rect inside a <g clip-path="url(#clip0)"> wrapper.

import interp:  lambda.package.pdf.interp
import resolve: lambda.package.pdf.resolve

pn main() {
    let doc^err = input("test/input/test.pdf", 'pdf')
    let page = resolve.page_at(doc, 0)
    let ops = [
        // small clip rectangle, then W + n (no paint)
        { op: "re", operands: [50, 50, 100, 100] },
        { op: "W",  operands: [] },
        { op: "n",  operands: [] },
        // big filled rect — should be wrapped in clip group
        { op: "rg", operands: [1.0, 0.0, 0.0] },
        { op: "re", operands: [0, 0, 200, 200] },
        { op: "f",  operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 800.0)
    print({
        path_count: len(r.paths),
        text_count: len(r.texts),
        paths: (for (p in r.paths) format(p, 'xml'))
    })
}
