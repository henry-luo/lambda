import compare: ._tex_script_recovery
let bodies=[
 "\\long\\def\\m#1#2#3{\\exp_after:wN#1\\exp_after:wN#2\\exp_after:wN{\\exp:w\\__exp_eval_register:N#3}}\\ifx\\m\\exp_args:NNV Y\\else N\\fi",
 "\\protected\\long\\def\\m{\\exp_args:NNV\\prop_get:NnN}\\ifx\\m\\prop_get:NVN Y\\else N\\fi",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\tl_set:Nn\\l_key_tl{k}\\long\\def\\m#1#2#3{\\exp_after:wN#1\\exp_after:wN#2\\exp_after:wN{\\exp:w\\__exp_eval_register:N#3}}\\m\\prop_get:NnN\\l_p_prop\\l_key_tl\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\tl_set:Nn\\l_key_tl{k}\\protected\\long\\def\\m{\\exp_args:NNV\\prop_get:NnN}\\m\\l_p_prop\\l_key_tl\\l_tmpa_tl[\\l_tmpa_tl]"
];
compare.check_expl_bodies(bodies)^
