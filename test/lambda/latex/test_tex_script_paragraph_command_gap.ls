import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let gaps=["\n\n", " \n\n", "\t\n\n", "\r\n\r\n", "\n\n\n", "\n\n \n",
    "% comment\n\n", " \n% comment\n\n", "\n \n"];
let prefixes=["\\usepackage{x}", "\\begin{document}\\bigskip",
    "\\begin{document}\\foo{X}", "\\begin{document}\\textbf{X}"];
fn check(source, mode) any^ {
    let r=profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:mode,resources:{local:{},bundled:{}}},100000)^;
    let d=digest.digest(r)^; let native=parse(source,{type:"latex"})^; let tex=parse(source,{type:"tex"})^;
    {issue:d.issue,tree:d.ast==native,text:reconstruct.reconstruct(r)^.text==tex.text,
        diagnostics:r.diagnostic_records==tex.diagnostics}
}
let cases=[for(a in prefixes,gap in gaps,mode in [false,true])
    check(a ++ gap ++ (if(starts_with(a,"\\usepackage")) "\\title{A}" else
        "\\begin{tabular}{l}A\\end{tabular}\\end{document}"),mode)^];
let abort_source="\\usepackage{x}\n\n\\def\\x{(1,2)}\\mystery\\x";
let abort_cases=[for(mode in [false,true]) check(abort_source,mode)^];
let comment_prefixes=["\\usepackage{x}","\\documentclass{article}","\\title{A}"];
let comment_gaps=[" %comment\n", "%comment\n", "  %comment\r\n", "\n\n%comment\n",
    "\n\n%comment\n\n", "\n\n%a\n%b\n"];
let comment_cases=[for(prefix in comment_prefixes, gap in comment_gaps, next in ["\\foo{Y}","\\title{Y}"], mode in [false,true])
    check(prefix ++ gap ++ next ++ "\\begin{document}X\\end{document}",mode)^];
let results=[*cases,*abort_cases,*comment_cases];
{cases:len(results),supported:all([for(r in results)r.issue==null]),
 tree:all([for(r in results)r.tree]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics])}
