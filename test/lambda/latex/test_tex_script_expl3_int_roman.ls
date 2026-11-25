// D10.2.2v2: roman conversion retains expl3's letter-category output.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Values, category codes and generated control-sequence names match TeX Live.
report("[\\int_to_roman:n{1}]/[\\int_to_roman:n{4+5}]/[\\int_to_Roman:n{1999}]/" ++
    "[\\int_to_roman:n{0}]/[\\int_to_Roman:n{-3}]")^;
report("\\edef\\word{\\int_to_roman:n{1}}\\def\\letter{i}\\edef\\other{\\string i}" ++
    "\\tl_if_eq:NNTF\\word\\letter{Y}{N}/\\tl_if_eq:NNTF\\word\\other{Y}{N}")^;
report("\\tl_set:cn{csvcol\\int_to_roman:n{1}}{A}\\use:c{csvcoli}")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\int_to_roman:n{2}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 integer source mapping"
