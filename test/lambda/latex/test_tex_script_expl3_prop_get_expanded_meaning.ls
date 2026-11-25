import compare: ._tex_script_recovery
let bodies=[
 "\\meaning\\prop_get:NVN/\\meaning\\prop_get:NvN/\\meaning\\prop_get:NoN/\\meaning\\prop_get:NeN/\\meaning\\prop_get:NxN/\\meaning\\prop_get:cVN/\\meaning\\prop_get:cvN/\\meaning\\prop_get:coN/\\meaning\\prop_get:ceN/\\meaning\\prop_get:cxN/\\meaning\\prop_get:cnc",
 "\\meaning\\exp_args:NNV/\\meaning\\exp_args:NNv/\\meaning\\exp_args:NNe/\\meaning\\exp_args:NcV/\\meaning\\exp_args:Ncv/\\meaning\\exp_args:Nco/\\meaning\\exp_args:Nce/\\meaning\\exp_args:Ncx/\\meaning\\exp_args:Ncnc",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\tl_set:Nn\\l_key_tl{k}\\let\\q\\prop_get:NVN\\q\\l_p_prop\\l_key_tl\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\tl_set:Nn\\l_key_tl{missing}\\tl_set:Nn\\l_tmpa_tl{old}\\prop_get:NVNTF\\l_p_prop\\l_key_tl\\l_tmpa_tl{N}{Y}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\tl_set:Nn\\l_key_tl{k}\\edef\\x{\\prop_get:NVN\\l_p_prop\\l_key_tl\\l_tmpa_tl}\\meaning\\x"
];
compare.check_expl_bodies(bodies)^
