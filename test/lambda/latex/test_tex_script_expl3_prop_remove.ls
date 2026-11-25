import compare: ._tex_script_recovery

let bodies=[
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_remove:Nn\\l_p_prop{a}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}{\\prop_gremove:Nn\\l_p_prop{a}}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}{\\prop_gpop:NnN\\l_p_prop{a}\\l_tmpa_tl}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_remove:cn{l_p_prop}{a}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_remove:Nn\\l_p_prop{missing}\\prop_if_in:NnTF\\l_p_prop{missing}{N}{Y}",
 "\\meaning\\prop_remove:Nn/\\meaning\\prop_gremove:Nn/\\meaning\\prop_remove:cn"
];
let valid=compare.check_expl_bodies(bodies)^;
let invalid=[for(mode in [false,true]){mode:mode,result:compare.check_detail("\\ExplSyntaxOn\\prop_remove:Nn\\l_missing_prop{a}\\ExplSyntaxOff",{script_expl3:true,constructor_argument_mode:mode})^}];
{cases:valid.cases + len(invalid),failures:[*valid.failures,*[for(item in invalid where item.result.issue!="tex-unsupported-primitive" or item.result.unsupported!="__prop_split_wrong:Nw")item]]}
