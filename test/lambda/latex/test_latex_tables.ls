// test_latex_tables.ls — Regression test for LaTeX tabular parsing.

import latex: lambda.latex.latex

let html = latex.render_file_to_html("test/input/test_latex_table.tex")

"1. table renders:"; contains(html, "<table class=\"latex-tabular\">")
"2. alignment tabs split cells:"; contains(html, "<td><strong>Function</strong></td><td style=\"text-align: center\"><strong>Domain</strong></td><td style=\"text-align: right\"><strong>Range</strong></td>")
"3. ordinary row splits cells:"; contains(html, "<td>A</td><td style=\"text-align: center\">B</td><td style=\"text-align: right\">C</td>")
"4. placement argument is not rendered:"; contains(html, "[h]") == false
