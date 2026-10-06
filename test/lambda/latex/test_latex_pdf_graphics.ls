// Check PDF page inclusion, TeX trim conversion and vector-export diagnostics.
import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/pdf_graphics.tex", {type: "latex"}) ^ { null }
let opts = {base_uri: "test/lambda/latex/fixtures"}
let rendered = latex.render_result(ast, opts)
let html = latex.render_to_html(ast, opts)
let vector = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures", target: "svg"})
let host_html = latex.render_to_html(ast,
    {source_path: "test/lambda/latex/fixtures/pdf_graphics.tex"})

"html diagnostics:"; [for (issue in rendered.diagnostics) issue.code]
"vector diagnostics:"; [for (issue in vector.diagnostics) issue.code]
"svg page:"; index_of(html, "<svg") != null
"trim in pixels:"; index_of(html, "clip-path:inset(") != null and index_of(html, "px") != null
"sized:"; index_of(html, "width:99.6264") != null
"crop and rotation:"; index_of(html, "viewBox=\"0 0 400 300\"") != null and
    index_of(html, "matrix(0 1 -1 0 500 -50)") != null
"selected second page:"; index_of(html, "viewBox=\"0 0 100 100\"") != null
"graphic angle:"; index_of(html, "rotate(15deg)") != null
"portable embedded image:"; index_of(html, "data:image/jpeg;base64,") != null and
    index_of(html, "img:") == null
"host source path:"; index_of(host_html, "viewBox=\"0 0 400 300\"") != null
"host standalone:"; string(name(latex.render_document(ast,
    {source_path: "test/lambda/latex/fixtures/pdf_graphics.tex"}))) == "html"
"malformed pdf located:"; all([for (issue in rendered.diagnostics) issue.offset != null])
