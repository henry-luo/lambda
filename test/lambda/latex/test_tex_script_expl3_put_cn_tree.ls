import compare: ._tex_script_recovery
[for(source in [
 "\\ExplSyntaxOn\\tl_put_left:cn{l_tmpa_tl}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_gput_right:cn{l_tmpa_tl}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:cn{l_s_seq}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\q\\seq_gput_right:cn\\q{l_s_seq}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) compare.check_tree_or_guard(source,"expl3 token-list assignment source mapping",{script_expl3:true})^]
