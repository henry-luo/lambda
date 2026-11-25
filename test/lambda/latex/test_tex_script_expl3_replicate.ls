// D10.2.2v2: repetition follows native expandable decimal processing.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Counts, nested expansion and protected tokens were compared with TeX Live.
report("[\\prg_replicate:nn{0}{ab}]/[\\prg_replicate:nn{3}{ab}]/" ++
    "[\\prg_replicate:nn{12}{x}]/[\\prg_replicate:nn{21}{y}]")^;
report("\\def\\foo{Q}\\edef\\word{\\prg_replicate:nn{2+1}{\\foo}}\\word/" ++
    "\\edef\\word{\\prg_replicate:nn{2}{\\exp_not:n{\\foo}}}[\\tl_to_str:V\\word]/" ++
    "\\prg_replicate:nn{2}{\\prg_replicate:nn{3}{z}}")^;
report("\\prg_replicate:nn{-1}{x}TAIL")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\prg_replicate:nn{2}{x}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 replication source mapping"
