import compare: ._tex_script_recovery

let bodies=[
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_pop_left:NNTF\\l_s_seq\\l_tmpa_tl{Y}{N}[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\tl_set:Nn\\l_tmpa_tl{X}\\seq_pop_left:NNTF\\l_s_seq\\l_tmpa_tl{N}{Y}[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_pop_left:NNT\\l_s_seq\\l_tmpa_tl{Y}[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_pop_left:NNF\\l_s_seq\\l_tmpa_tl{Y}[\\meaning\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_pop_left:cNTF{l_s_seq}\\l_tmpa_tl{Y}{N}[\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gpop_left:NNTF\\l_s_seq\\l_tmpa_tl{Y}{N}}[\\seq_count:N\\l_s_seq/\\l_tmpa_tl]",
 "\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_pop:NNTF\\l_s_seq\\l_tmpa_tl{Y}{N}[\\l_tmpa_tl]",
 "[\\meaning\\seq_pop_left:cN/\\meaning\\seq_pop_left:NNTF/\\meaning\\seq_pop_left:NNT/\\meaning\\seq_pop_left:NNF/\\meaning\\seq_pop_left:cNTF/\\meaning\\seq_gpop_left:NNTF/\\meaning\\seq_pop:NNTF]"
];
compare.check_expl_bodies(bodies)^
