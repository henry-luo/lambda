import compare: ._tex_script_recovery
import expl: ._tex_expl3_compare

let bodies=[
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_remove:Nn\\l_p_prop{a}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}{\\prop_gremove:Nn\\l_p_prop{a}}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}{\\prop_gpop:NnN\\l_p_prop{a}\\l_tmpa_tl}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\prop_remove:cn{l_p_prop}{a}\\prop_if_in:NnTF\\l_p_prop{a}{N}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_remove:Nn\\l_p_prop{missing}\\prop_if_in:NnTF\\l_p_prop{missing}{N}{Y}",
 "\\meaning\\prop_remove:Nn/\\meaning\\prop_gremove:Nn/\\meaning\\prop_remove:cn"
];
let valid=compare.check_expl_bodies(bodies)^;
let source="\\ExplSyntaxOn\\prop_remove:Nn\\l_missing_prop{a}\\ExplSyntaxOff";
let invalid=[for(mode in [false,true]) (let r=expl.expand(source,false,mode)^,
    let public_result=parse(source,{type:"tex"}) ^ {null},
    {mode:mode,issue:r.issue,unsupported:r.unsupported,public_rejected:public_result==null})];
{cases:valid.cases + len(invalid),failures:[*valid.failures,*[for(item in invalid where item.issue!="tex-unsupported-primitive" or item.unsupported!="__msg_interrupt:NnnnN" or not item.public_rejected)item]]}
