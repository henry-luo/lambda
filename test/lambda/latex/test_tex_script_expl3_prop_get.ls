import compare: ._tex_script_recovery
import expl: ._tex_expl3_compare
let bodies=[
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{Alpha}\\prop_get:NnN\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\tl_set:Nn\\l_tmpa_tl{old}\\prop_get:NnN\\l_p_prop{missing}\\l_tmpa_tl[\\meaning\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{{A}~B}\\prop_get:NnNTF\\l_p_prop{a}\\l_tmpa_tl{Y}{N}[\\meaning\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\tl_set:Nn\\l_tmpa_tl{old}\\prop_get:NnNTF\\l_p_prop{missing}\\l_tmpa_tl{N}{Y}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{Alpha}\\prop_get:cnNT{l_p_prop}{a}\\l_tmpa_tl{Y}[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_get:cnNF{l_p_prop}{missing}\\l_tmpa_tl{Y}[\\meaning\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\let\\q\\prop_get:NnN\\q\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\afterassignment X\\prop_get:NnN\\l_p_prop{a}\\l_tmpa_tl[\\l_tmpa_tl]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{A}\\edef\\x{\\prop_get:NnN\\l_p_prop{a}\\l_tmpa_tl}\\meaning\\x",
 "\\meaning\\prop_get:NnN/\\meaning\\prop_get:NnNTF/\\meaning\\prop_get:NnNT/\\meaning\\prop_get:NnNF/\\meaning\\prop_get:cnN/\\meaning\\prop_get:cnNTF/\\meaning\\__prop_get:NnnTF"
];
let valid=compare.check_expl_bodies(bodies)^;
let source="\\ExplSyntaxOn\\prop_get:NnN\\l_missing_prop{a}\\l_tmpa_tl\\ExplSyntaxOff";
let invalid=[for(mode in [false,true]) (let r=expl.expand(source,false,mode)^,
    let public_result=parse(source,{type:"tex"}) ^ {null},
    {mode:mode,issue:r.issue,unsupported:r.unsupported,public_rejected:public_result==null})];
{cases:valid.cases + len(invalid),failures:[*valid.failures,*[for(item in invalid where item.issue!="tex-unsupported-primitive" or item.unsupported!="__msg_interrupt:NnnnN" or not item.public_rejected)item]]}
