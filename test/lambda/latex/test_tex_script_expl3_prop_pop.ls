import compare: ._tex_script_recovery

let bodies=[
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_put:Nnn\\l_p_prop{b}{B}\\prop_pop:NnN\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl/\\prop_item:Nn\\l_p_prop{b}]",
 "\\prop_new:N\\l_p_prop\\tl_set:Nn\\l_tmpa_tl{old}\\prop_pop:NnN\\l_p_prop{none}\\l_tmpa_tl[\\meaning\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_pop:NnNTF\\l_p_prop{a}\\l_tmpa_tl{Y}{N}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\tl_set:Nn\\l_tmpa_tl{old}\\prop_pop:NnNTF\\l_p_prop{none}\\l_tmpa_tl{N}{Y}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_pop:cnNT{l_p_prop}{a}\\l_tmpa_tl{Y}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_pop:cnNF{l_p_prop}{none}\\l_tmpa_tl{Y}[\\meaning\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\let\\p\\prop_pop:NnN\\p\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\afterassignment X\\prop_pop:NnN\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\edef\\x{\\prop_pop:NnN\\l_p_prop{a}\\l_tmpa_tl}\\meaning\\x",
 "\\meaning\\prop_pop:NnN/\\meaning\\prop_gpop:NnN/\\meaning\\prop_pop:NnNTF/\\meaning\\prop_pop:cnN/\\meaning\\__prop_pop:NnNNnTF"
];
compare.check_expl_bodies(bodies)^
