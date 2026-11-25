import compare: ._tex_script_recovery
[for(source in [
 "\\ExplSyntaxOn\\tl_set_eq:cN{l_tmpa_tl}\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_gset_eq:Nc\\l_tmpa_tl{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_set_eq:cc{l_tmpa_tl}{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\seq_gset_eq:cc\\q{l_tmpa_tl}{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) compare.check_tree_or_guard(source,"expl3 token-list assignment source mapping",{script_expl3:true})^]
