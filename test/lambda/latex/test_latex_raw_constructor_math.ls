// S1.8, D10.2.2v2: registered adapter arguments remain raw across math shifts.
let source="\\begin{document}\\begin{prooftree}\\def\\fCenter{\\mbox{\\ $\\Rightarrow$\\ }}" ++
    "\\BinaryInf$\\Gamma,\\fCenter\\Delta$\\end{prooftree}\\end{document}";
parse(source,{type:"latex",raw:["BinaryInf"]})^;
let shapes="\\def\\x{EXP}\\begin{document}\\rawbox*{\\x}[\\x]$\\x$ \\x\\end{document}"
parse(shapes,{type:"latex",raw:["rawbox"]})^;
// Raw aliases preserve arguments, and registry settings do not leak.
let alias="\\def\\x{OLD}\\let\\alias\\rawbox\\begin{document}\\rawbox{\\x} \\x \\alias{\\x} \\x\\end{document}"
parse(alias,{type:"latex",raw:["rawbox"]})^;
parse(shapes,"latex")^;
parse(alias,{type:"latex",raw:["rawbox"]})^;
parse("\\let\\alias\\textbf\\begin{document}\\alias{Hello}\\end{document}","latex")^
