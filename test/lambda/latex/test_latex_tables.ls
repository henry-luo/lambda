// test_latex_tables.ls — Regression test for LaTeX tabular parsing.

import latex: lambda.doc.latex.latex

let html = latex.render_file_to_html("test/input/test_latex_table.tex")

"1. table renders:"; contains(html, "<table class=\"latex-tabular\">")
// the |l|c|r| preamble draws its vertical rules as cell borders
"2. alignment tabs split cells:"; contains(html, "<td style=\"text-align: left;border-left:1px solid black;\"><strong>Function</strong></td><td style=\"text-align: center;border-left:1px solid black;\"><strong>Domain</strong></td><td style=\"text-align: right;border-left:1px solid black;border-right:1px solid black;\"><strong>Range</strong></td>")
"3. ordinary row splits cells:"; contains(html, "<td style=\"text-align: left;border-left:1px solid black;\">A</td><td style=\"text-align: center;border-left:1px solid black;\">B</td><td style=\"text-align: right;border-left:1px solid black;border-right:1px solid black;\">C</td>")
"4. placement argument is not rendered:"; contains(html, "[h]") == false
