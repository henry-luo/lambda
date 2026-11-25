import compare: ._tex_script_recovery

let bodies=[
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_right:Nn\\l_s_seq{B}\\seq_pop_left:NN\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl/\\seq_use:Nn\\l_s_seq{,}]",
 "\\seq_new:N\\l_s_seq\\seq_pop_left:NN\\l_s_seq\\l_tmpa_tl[\\meaning\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gpop_left:NN\\l_s_seq\\l_tmpa_tl}[\\seq_count:N\\l_s_seq/\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\let\\p\\seq_pop_left:NN\\p\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\afterassignment X\\seq_pop_left:NN\\l_s_seq\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\edef\\x{\\seq_pop_left:NN\\l_s_seq\\l_tmpa_tl}[\\meaning\\x/\\seq_count:N\\l_s_seq]",
 "\\seq_pop_left:NN\\l_missing_seq\\l_tmpa_tl",
 "[\\meaning\\seq_pop_left:NN/\\meaning\\seq_gpop_left:NN/\\meaning\\__seq_pop:NNNN]"
];
compare.check_expl_bodies(bodies)^
