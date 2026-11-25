// D10.2.2v2: generated environment names use their reconstructed source position.
let source="\\newcommand{\\boxit}[1]{\\begin{tcolorbox}[colback=cyan]#1\\end{tcolorbox}}" ++
    "\\begin{document}\\boxit{Hi}\\end{document}";
parse(source,"latex")^
