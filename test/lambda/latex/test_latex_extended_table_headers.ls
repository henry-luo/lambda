// D10.2.2v2: table headers are attributes and cell separators remain structural.
let source = "\\begin{document}\\begin{longtable}{lc}A&B\\\\\\end{longtable}" ++
    "\\begin{tabularx}{5cm}{lX}C&D\\\\\\end{tabularx}" ++
    "\\begin{xltabular}{\\textwidth}{rX}E&F\\\\\\end{xltabular}\\end{document}"
parse(source, "latex")^
