// Phase 2 — content-stream tokenizer (stream.ls).
//
// Verifies parse_content_stream produces the expected operator sequence
// from test.pdf's flate-decoded content stream.

import resolve: lambda.package.pdf.resolve
import stream:  lambda.package.pdf.stream

pn main() {
    let doc^err = input("test/pdf/data/basic/test.pdf", 'pdf')
    let page = resolve.page_at(doc, 0)
    let bytes = resolve.page_content_bytes(doc, page)
    let ops = stream.parse_content_stream(bytes)

    var op_names = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        op_names = op_names ++ [ops[i].op]
        i = i + 1
    }

    print({
        op_count: n,
        ops: op_names
    })
}
