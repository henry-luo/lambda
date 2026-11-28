import utf8: lambda.io.utf8
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import compare: ._tex_script_recovery
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
// Native source-bound captures keep this comparison independent of public input.
let native=input("test/lambda/latex/fixtures/tex_expl3_tl_blank_native.json","json")^;
let cached=compare.preload_expl3()^;
fn matches(source,index) any^ {
    let entry=native.cases[index];
    let result=compare.check_expansion(source,{script_expl3:true},entry,if(index==0)null else cached)^;
    {issue:result.issue,reconstruct_issue:result.reconstruct_issue,matches:result.text_match,
        diagnostics_match:result.diagnostics_match,messages_empty:result.messages_match}
}
let sources = [
 "\\ExplSyntaxOn\\tl_if_blank:nTF{}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{~}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{~~~}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{A}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{~A~}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{{}}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nT{~}{T}\\tl_if_blank:nF{A}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nT{A}{T}\\tl_if_blank:nF{~}{F}\\ExplSyntaxOff",
 "\\def\\x{}\\ExplSyntaxOn\\tl_if_blank:nTF{\\x}{T}{F}\\ExplSyntaxOff",
 "\\def\\x{~}\\ExplSyntaxOn\\tl_if_blank:nTF{\\x}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{é}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{~}{T}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:nTF{A}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:nTF{~}{T}{F}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:nTF{A}{T}{F}}\\x\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:nTF{~}{\\foo}{F}}\\x\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:nTF{}{\\noexpand\\foo}{F}}\\def\\foo{Z}\\x\\ExplSyntaxOff"
];
[for (index,source in sources) matches(source,index)^]

fn safe_digestion(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let parsed = digest.digest(result)^;
    if (parsed.ast != null) parsed.ast == (parse(source, {type: "latex"})^)
    else parsed.issue == "latex-digest-unsupported"
}
[for (source in [
    "\\ExplSyntaxOn\\begin{document}\\tl_if_blank:nTF{~}{T}{F}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_blank:nTF{~}{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
