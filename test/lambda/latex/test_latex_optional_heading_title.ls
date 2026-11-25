// D10.2.2v2: a heading may omit its title without consuming following text.
let source="\\begin{document}\\begin{parts}\\part[1] A\\part B\\end{parts}" ++
    "\\section [Short] {Long}Text\\end{document}";
parse(source,"latex")^
