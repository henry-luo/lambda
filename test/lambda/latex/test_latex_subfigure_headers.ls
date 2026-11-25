// D10.2.2v2: subfigure headers remain metadata and never enter the body.
let source = "\\begin{document}\\begin{subfigure}[b]{0.45\\textwidth}Hi\\end{subfigure}" ++
    "\\begin{subtable} {3cm}There\\end{subtable}\\end{document}"
parse(source, "latex")^
