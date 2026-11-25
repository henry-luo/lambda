import compare: ._tex_script_tl_head_compare

let ops=["\\ignorespaces    ", "\\def\\s{ }\\ignorespaces\\s ",
 "\\let\\p\\ignorespaces\\p  ", "\\def\\s{ }\\ignorespaces\\iftrue\\s\\else X\\fi "];
fn wrap(op,shape) => if(shape==0) "A" ++ op ++ "B" else if(shape==1)
 "\\textbf{A" ++ op ++ "B}" else "\\emph{Q\\textbf{A" ++ op ++ "B}Z}";
let results=[for(op in ops, shape in [0,1,2])compare.check(wrap(op,shape),true,false)^];
{cases:len(results),failures:[for(r in results where not r.text or not r.diagnostics or not r.messages or
 (r.ast!=true and (r.issue!="latex-digest-unsupported" or r.unsupported!="inline definition token mapping")))r]}
