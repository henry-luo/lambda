// Phase IV (Lambda_Pkg_Latex3 §9.4): a class and a package beside the
// document run on the TeX engine; adapters still answer what they require.
import latex: lambda.latex.latex
import html: lambda.latex.to_html

let path = "test/lambda/latex/fixtures/local_packages/document.tex";
let ast = latex.parse_file(path);
let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures/local_packages"});
let out = html.to_html(result.elements) ^ { "" };

"packages:"; [for (entry in result.packages) entry.key]
"engine packages:"; ast.tex_packages
"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"class macros:"; index_of(out, "Course MATH 220") != null
"package option:"; index_of(out, "status DRAFT") != null
"begin-document hook:"; index_of(out, "started yes") != null
"optional argument:"; index_of(out, "Tip:") != null
"delimited macro:"; index_of(out, "(a; b)") != null
"package counter:"; index_of(out, "Exercise 2.") != null
"package environment:"; index_of(out, "A remark from the package.") != null
"required adapter:"; index_of(out, "color:") != null
