// D10.2.2v2: the preface is parsed before the column body.
let src="\\begin{document}\\begin{multicols}{3}[\\subsection{Spread}][2em]Body text.\\end{multicols}\\end{document}"
parse(src,"latex")^
