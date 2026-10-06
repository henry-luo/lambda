import latex: lambda.latex.latex
import html: lambda.latex.to_html

let ast = input("test/latex/samples/spacetime_diagrams.tex", {type: "latex"})^
let result = latex.render_result(ast,
    {base_uri: "test/latex/samples", standalone: true})
let output = html.to_html(result.elements)
{
    unsupported: len([for (issue in result.diagnostics
        where issue.code == "unsupported-output") issue]),
    vector: index_of(output, "<svg") != null,
    clipped: index_of(output, "<clipPath") != null and
        index_of(output, "clip-path=\"url(#tikz-clip-") != null,
    grid: index_of(output, "stroke=\"rgb(220,234,248)\"") != null,
    valid_strokes: index_of(output, "stroke=\"smooth\"") == null and
        index_of(output, "stroke=\"thick\"") == null,
    colored_curve: index_of(output, "rgb(78,154,6)") != null,
    arrows: index_of(output, " Z\"") != null,
    path_count: len(split(output, "<path")) - 1
}
