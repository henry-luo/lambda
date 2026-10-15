import latex: lambda.doc.latex.latex

let rows = [for (i in 1 to 400) "left" ++ string(i) ++ " & right" ++ string(i) ++ "\\\\\n"] |> join("")
let source = "\\documentclass{article}\\begin{document}\\begin{tabular}{lr}" ++ rows ++
    "\\end{tabular}\\end{document}"
let html = latex.render_string_to_html(source);
[len(split(html, "<tr>")) - 1 == 400, contains(html, "left1"), contains(html, "right400")]
