// Phase IV (Lambda_Pkg_Latex3 §9.6): a LaTeX document reads PGFPlots tables
// from files beside it; the render passes the document base to the TikZ bridge.
import latex: lambda.latex.latex
import html: lambda.latex.to_html

let path = "test/latex/fixtures/tikz_phase4/table_plot.tex";
let ast = latex.parse_file(path);
let result = latex.render_result(ast, {base_uri: "test/latex/fixtures/tikz_phase4"});
let out = html.to_html(result.elements) ^ { "" };

"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"pictures:"; len(split(out, "<svg")) - 1
"symbolic x coords from csv:"; index_of(out, ">north<") != null and index_of(out, ">west<") != null
"legend entries:"; index_of(out, ">Units<") != null and index_of(out, ">Q2<") != null
