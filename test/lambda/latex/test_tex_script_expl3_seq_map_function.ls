import compare: ._tex_script_recovery

let bodies=[
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b,c}\\def\\f#1{[#1]}\\seq_map_function:NN\\l_tmpa_seq\\f",
 "\\seq_clear:N\\l_tmpa_seq\\def\\f#1{[#1]}A\\seq_map_function:NN\\l_tmpa_seq\\f B",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b,c,d,e,f,g}\\def\\f#1{[#1]}\\seq_map_function:cN{l_tmpa_seq}\\f",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b,c}\\def\\f#1{[#1]\\seq_map_break:}\\seq_map_function:NN\\l_tmpa_seq\\f X",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b,c}\\def\\f#1{[#1]\\seq_map_break:n{Z}}\\seq_map_function:NN\\l_tmpa_seq\\f X",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b,c}\\def\\f#1{[#1]}\\edef\\x{\\seq_map_function:NN\\l_tmpa_seq\\f}\\meaning\\x",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b}\\def\\f#1{[#1]}\\let\\q\\seq_map_function:NN\\q\\l_tmpa_seq\\f",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b}\\seq_set_from_clist:Nn\\l_tmpb_seq{1,2}\\def\\inner#1{<#1>}\\def\\outer#1{[#1:\\seq_map_function:NN\\l_tmpb_seq\\inner]}\\seq_map_function:NN\\l_tmpa_seq\\outer",
 "\\seq_set_from_clist:Nn\\l_tmpa_seq{a,b}\\seq_set_from_clist:Nn\\l_tmpb_seq{1,2}\\def\\inner#1{<#1>\\seq_map_break:n{Q}}\\def\\outer#1{[#1:\\seq_map_function:NN\\l_tmpb_seq\\inner]}\\seq_map_function:NN\\l_tmpa_seq\\outer",
 "\\meaning\\seq_map_function:NN/\\meaning\\seq_map_function:cN/\\meaning\\seq_map_break:/\\meaning\\seq_map_break:n",
 "\\seq_map_function:NN\\l_missing_seq\\f"
];
compare.check_expl_bodies(bodies)^
