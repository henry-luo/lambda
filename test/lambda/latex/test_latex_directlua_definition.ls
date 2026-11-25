// S1.8, D10.2.2v2: Lua code remains captured source inside TeX definitions.
let source="\\newcommand{\\wert}[1]{\\directlua{tex.sprint(#1)}}" ++
    "\\begin{document}\\(\\directlua{addaufgabe(30)}\\)\\end{document}";
parse(source,"latex")^;
parse("\\begin{document}\\textbf{A\\directlua{B}C}\\end{document}","latex")^
