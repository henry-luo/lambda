// Phase 7 — inline image (BI..ID..EI) skip + synthetic op emission.
//
// Verifies that stream.parse_content_stream:
//   - Detects "BI" as the start of an inline image.
//   - Skips the binary payload between "ID" and "EI".
//   - Emits a synthetic { op: "inline_image", operands: [...] } record
//     in place of the binary segment.
//   - Continues parsing the next operator after EI normally.

import stream: lambda.package.pdf.stream

pn main() {
    // Build a tiny content stream containing one BI..EI block sandwiched
    // between two normal operators. The bytes between ID and EI mimic
    // raw pixel data (would otherwise tokenize as random garbage).
    let cs = ("q\n"
     ++ "BI\n"
     ++ "/W 2 /H 2 /CS /G /BPC 8 /F /A85 ID\n"
     ++ "@@@@@@@@@@\n"
     ++ "EI\n"
     ++ "Q\n")
    let ops = stream.parse_content_stream(cs)
    var names = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        names = names ++ [ops[i].op]
        i = i + 1
    }
    print({ count: len(ops), names: names })
}
