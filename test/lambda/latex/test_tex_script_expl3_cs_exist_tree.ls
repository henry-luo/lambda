import compare: ._tex_script_recovery

let sources=[
 "\\ExplSyntaxOn\\cs_if_exist:NTF\\unknown{\\def\\foo{Y}}{\\def\\foo{N}}\\ExplSyntaxOff\\begin{document}\\foo\\end{document}",
 "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{\\def\\bar{Y}}{\\def\\bar{N}}\\ExplSyntaxOff\\begin{document}\\bar\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_exist:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\cs_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\cs_if_exist:NTF\\relax{\\def\\foo{A}\\foo}{B}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\cs_if_exist:cTF{missing}{B}{\\def\\foo{A}\\foo}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\cs_if_exist:NTF\\relax{\\def\\foo{A}\\foo}{B}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\cs_if_exist:NTF\\relax{\\def\\foo#1{[#1]}\\foo{X}}{B}\\ExplSyntaxOff\\end{document}"
];
let checks=[for(source in sources, mode in [false,true]){source:source,mode:mode,
 matches:compare.check_tree(source,{script_expl3:true,constructor_argument_mode:mode})^}];
{cases:len(checks),failures:[for(item in checks where not item.matches)item]}
