// D10.2.2v2: unfinished math text arguments retain their source.
let source="\\begin{document}\\begin{prooftree}\\def\\fCenter{\\mbox{\\ $\\Rightarrow$\\ }}\\BinaryInf$\\Gamma,\\fCenter\\Delta$\\end{prooftree}\\end{document}"
parse(source,"latex") ^ { ^.message }
