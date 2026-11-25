import compare: ._tex_script_recovery

let sources=[
 "\\ExplSyntaxOn\\begin{document}\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}}\\end{document}",
 "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\tl_if_eq:nnTF{A}{A}{T}{F}\\end{document}",
 "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_eq:nnTF{}{A}{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_if_blank:nTF{}{T}{F}}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\tl_count:n{a{b}c}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\tl_to_str:n{abc}\\ExplSyntaxOff\\end{document}"
];
let checks=[for(source in sources, mode in [false,true]){source:source,mode:mode,
 matches:compare.check_tree(source,{script_expl3:true,constructor_argument_mode:mode})^}];
{cases:len(checks),failures:[for(item in checks where not item.matches)item]}
