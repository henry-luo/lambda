// test_math_html_output.ls — Test LaTeX-to-HTML rendering pipeline
// Loads math_intensive_test.tex and snapshots HTML without binary font payloads.
// Source titles retain indexed-root brackets and binomial operands.

import latex: lambda.doc.latex.latex
import snapshot: .mod_svg_snapshot

let html = latex.render_file_to_html("test/input/math_intensive_test.tex")
snapshot.normalize(html)
