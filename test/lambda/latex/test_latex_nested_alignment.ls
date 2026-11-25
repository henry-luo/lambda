// D10.2.2v2: nested math environments keep their own row and cell boundaries.
import latex: lambda.doc.latex.latex
import amsmath: lambda.doc.latex.packages.amsmath
let source = "\\documentclass{article}\\usepackage{amsmath}\\begin{document}" ++
    "\\begin{align}a&=\\begin{aligned}b&=c\\\\d&=e\\end{aligned}\\\\f&=g\\end{align}" ++
    "\\begin{equation}\\begin{split}x&=y\\\\z&=w\\end{split}\\end{equation}\\end{document}"
let ast = parse(source,"latex")^
let result = latex.render_result(ast, {target:"html"})^
let rendered = latex.render_to_html(ast, {target:"html"})^
"unsupported:"; [for (issue in result.diagnostics where issue.code == "unsupported-output") issue.message]
"numbered rows:"; len(split(rendered,"latex-align-number")) - 1
"equation numbers:"; len(split(rendered,"latex-eq-number")) - 1
"nested cells:"; len(split(rendered,"data-column-align")) - 1
// An outer tag remains on its own row; nested separators are not outer separators.
let rows = amsmath.rows_for(<align source:"a&=\\begin{aligned}b\\tag{N}&=c\\\\d&=e\\end{aligned}\\tag{X}\\\\f&=g">)^
"row boundaries:"; [for (row in rows) {math:row.math, tag:row.tag}]
