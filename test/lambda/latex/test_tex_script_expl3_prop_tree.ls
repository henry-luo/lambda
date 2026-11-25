import compare: ._tex_script_tl_head_compare
let bodies=["\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{text}[\\prop_item:Nn\\l_p_prop{k}]", "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{\\textbf{bold}}\\prop_if_in:NnTF\\l_p_prop{k}{\\prop_item:Nn\\l_p_prop{k}}{N}"];
let results=[for(body in bodies, mode in [false,true])compare.check(body,mode)^];
{cases:len(results),failures:[for(r in results where not r.text or not r.diagnostics or not r.messages or
 if(r.constructor) r.issue!=null or r.ast!=true
 else r.issue!="latex-digest-unsupported" or r.unsupported!="expl3 token-list assignment source mapping")r]}
