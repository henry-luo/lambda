import compare: ._tex_script_tl_head_compare

let bodies=[
 "\\tl_put_right:Nn\\l_tmpa_tl{A}\\l_tmpa_tl",
 "\\tl_put_left:Nn\\l_tmpa_tl{A}\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{B}\\tl_put_left:Nn\\l_tmpa_tl{A}\\l_tmpa_tl",
 "\\tl_put_right:Nx\\l_tmpa_tl{a\\int_eval:n{1+1}}\\l_tmpa_tl",
 "\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{A}B}",
 "\\textbf{\\tl_put_left:Nn\\l_tmpa_tl{A}B}",
 "{\\tl_gput_right:Nn\\l_tmpa_tl{A}}\\l_tmpa_tl",
 "\\let\\p\\tl_put_right:Nn\\p\\l_tmpa_tl{A}\\l_tmpa_tl"
];
let results=[for(body in bodies, mode in [false,true])compare.check(body,mode)^];
{cases:len(results),failures:[for(r in results where
 if(r.constructor) r.issue!=null or not r.text or not r.diagnostics or not r.messages or not r.ast
 else r.issue!="latex-digest-unsupported" or r.unsupported!="expl3 token-list assignment source mapping")r]}
