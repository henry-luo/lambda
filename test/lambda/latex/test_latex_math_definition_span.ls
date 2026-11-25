// D10.2.2v2: math digestion reads the emitted bytes of a definition echo.
parse("\\begin{document}\\[\\def\\foo#1{#1}%\n\\inference{a}{b}\\]\\end{document}",
    {type:"latex",raw:["inference"]})^;
parse("\\begin{document}\\[\\def\\foo#1{$#1$} X\\]\\end{document}","latex")^
