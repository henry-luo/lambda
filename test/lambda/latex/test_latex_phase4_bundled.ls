// Phase IV (Lambda_Pkg_Latex3 §9.4): bundled LPPL programming packages
// (kvoptions, etoolbox, ifthen, xkeyval, calc) run unmodified on the TeX
// engine. Expected values were checked against pdflatex (TeX Live 2025).
import latex: lambda.latex.latex
import html: lambda.latex.to_html

let path = "test/lambda/latex/fixtures/local_packages/bundled.tex";
let ast = latex.parse_file(path);
let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures/local_packages"});
let out = html.to_html(result.elements) ^ { "" };

"engine packages:"; ast.tex_packages
"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"kvoptions:"; index_of(out, "in green") != null and index_of(out, "<strong") != null
"ifthen:"; index_of(out, "alpha, other") != null
"etoolbox:"; index_of(out, "is-x, not-x") != null and index_of(out, "stored value") != null
"appto:"; index_of(out, "Hello, world") != null
"keyval:"; index_of(out, "0pt, 12mm") != null
"xkeyval:"; index_of(out, "tone=warm") != null
"calc:"; index_of(out, "85.35825pt/12") != null
"whiledo:"; index_of(out, "loop: 10.") != null
