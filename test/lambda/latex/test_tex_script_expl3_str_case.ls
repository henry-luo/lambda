// D10.2.2v2: string cases compare unexpanded tokens and preserve branch order.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// The expandable branch results were compared with TeX Live.
report("\\edef\\result{\\str_case:nnTF{b}{{a}{A}{b}{B}{b}{C}}{T}{F}/" ++
    "\\str_case:nnTF{z}{{a}{A}}{T}{F}}\\result")^;
report("\\edef\\result{\\str_case:nnT{a}{{a}{A}}{T}/\\str_case:nnT{z}{{a}{A}}{T}/" ++
    "\\str_case:nnF{a}{{a}{A}}{F}/\\str_case:nnF{z}{{a}{A}}{F}}\\result")^;
report("\\def\\foo{x}\\edef\\result{\\str_case:nn{\\foo}{{\\foo}{MATCH}{x}{EXPANDED}}/" ++
    "\\str_case:nn{z}{}X}\\result")^;
// A choice key must store the selected separator rather than its case expression.
report("\\tl_new:N\\l_separator_tl\\keys_define:nn{demo}{separator.choices:nn={comma,semicolon}" ++
    "{\\tl_set:Nx\\l_separator_tl{\\str_case:nn{#1}{{comma}{,}{semicolon}{;}}}},separator.initial:n=comma}" ++
    "[\\tl_to_str:V\\l_separator_tl]")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\str_case:nn{a}{{a}{A}}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 string case source mapping"
