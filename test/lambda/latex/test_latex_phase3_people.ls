import latex: lambda.latex.latex
import html: lambda.latex.to_html

let ast = input("test/latex/samples/tikzpeople.tex", {type: "latex"})^
let result = latex.render_result(ast,
    {base_uri: "test/latex/samples", standalone: true})
let output = html.to_html(result.elements)
{
    unsupported: len([for (issue in result.diagnostics
        where issue.code == "unsupported-output") issue]),
    shape_approximation: len([for (issue in result.diagnostics
        where issue.code == "tikzpeople-shape-approximation") issue]),
    pictures: len(split(output, "tikzpeople-picture")) - 1,
    gallery_shapes: len(split(output, "tikzpeople-gallery-item")) - 1,
    pin_text: contains(output, "All clear; you're good to go.") and
        contains(output, "We've been good!") and contains(output, "Moving in."),
    mirrored: contains(output, "translate(100 0) scale(-1 1)"),
    custom_color: contains(output, "rgb(71,171,76)"),
    labels: contains(output, "Fairy Godmother") and contains(output, "The Bride")
}
