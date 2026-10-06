import latex: lambda.latex.latex
import util: lambda.latex.util
import html: lambda.latex.to_html

let ast = input("test/lambda/latex/fixtures/caption_profile.tex", "latex") ^ { null }
let result = latex.render_result(ast, {standalone: true})
let text = util.text_of(result.elements)
let serialized = html.to_html(result.elements) ^ { "" }
{unsupported: len([for (issue in result.diagnostics where issue.code == "unsupported-output") issue]),
 panels: contains(text, "(a). Left panel") and contains(text, "(b). Right panel"),
 parent: contains(text, "Figure 1. Two panels"),
 captionof: contains(text, "Table 1. Outside float"),
 references: contains(text, "panel 1a") and contains(text, "subpanel b"),
 formatting: contains(serialized, "font-weight:bold") and contains(serialized, "<em>panel</em>"),
 no_blank_separators: not contains(serialized, "<p>\n</p>") and
    not contains(serialized, "<p> </p>"),
 unnumbered: contains(text, "Unnumbered") and not contains(text, "Figure 2.")}
