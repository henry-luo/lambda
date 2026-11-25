// D10.2.2v2: title hook arguments are stored as raw code across environments.
let source="\\pretitle{\\begin{center}\\LARGE}\\posttitle{\\par\\end{center}\\vskip 0.5em}" ++
    "\\begin{document}Body\\end{document}";
parse(source,"latex")^
