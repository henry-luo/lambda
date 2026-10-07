// Phase IV (Lambda_Pkg_Latex3 §9.4): a beside-document package written in
// expl3. The request for expl3 restarts the run from the engine's expl3 format
// (bundled, unmodified expl3-code.tex). Expected text checked against pdflatex
// (TeX Live 2025).
import latex: lambda.latex.latex
import html: lambda.latex.to_html

let path = "test/lambda/latex/fixtures/local_packages/expl3.tex";
let ast = latex.parse_file(path);
let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures/local_packages"});
let out = html.to_html(result.elements) ^ { "" };

"engine packages:"; ast.tex_packages
"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"xparse optional argument:"; index_of(out, "Hello, World! and Hello, Lambda!") != null
"cs_new:"; index_of(out, "abab;") != null
"int_eval:"; index_of(out, "7;") != null
"fp_eval:"; index_of(out, "1.4142;") != null
"text_uppercase:"; index_of(out, "MIXED CASE;") != null
"seq_sort:"; index_of(out, "a, b, c;") != null
"tl_const:"; index_of(out, "expl3 ready.") != null
