import compare: ._tex_script_recovery

let bodies=[
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_right:Nn\\l_s_seq{B}\\seq_pop_right:NN\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl/\\seq_use:Nn\\l_s_seq{,}]",
 "\\seq_new:N\\l_s_seq\\seq_pop_right:NN\\l_s_seq\\l_tmpa_tl[\\meaning\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_right:Nn\\l_s_seq{B}{\\seq_gpop_right:NN\\l_s_seq\\l_tmpa_tl}[\\seq_use:Nn\\l_s_seq{,}/\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\let\\p\\seq_pop_right:NN\\p\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\afterassignment X\\seq_pop_right:NN\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\edef\\x{\\seq_pop_right:NN\\l_s_seq\\l_tmpa_tl}[\\meaning\\x]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_pop_right:cNTF{l_s_seq}\\l_tmpa_tl{Y}{N}[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\tl_set:Nn\\l_tmpa_tl{X}\\seq_pop_right:NNTF\\l_s_seq\\l_tmpa_tl{N}{Y}[\\l_tmpa_tl]",
 "\\seq_pop_right:NN\\l_missing_seq\\l_tmpa_tl",
 "[\\meaning\\seq_pop_right:NN/\\meaning\\seq_gpop_right:NN/\\meaning\\seq_pop_right:NNTF/\\meaning\\seq_pop_right:cN]"
];
compare.check_expl_bodies(bodies)^
