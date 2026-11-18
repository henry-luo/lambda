import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let gaps=["\n", " ", "\r\n", " %comment\n", "\n\n", "\n\n%comment\n"];
let prefixes=["\\usepackage{x}","\\documentclass{article}","\\title{A}"];
let definitions=["\\def\\x{A}","\\newcommand{\\x}{A}","\\newenvironment{q}{A}{B}"];
fn check(prefix,gap,definition,mode) any^ {
 let source=prefix ++ gap ++ definition ++ "\\begin{document}X\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,
    adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let native=parse(source,{type:"tex"})^;
 {issue:d.issue,tree:d.ast==parse(source,{type:"latex"})^,
    text:reconstruct.reconstruct(r)^.text==native.text,diagnostics:r.diagnostic_records==native.diagnostics}
}
let results=[for(prefix in prefixes,gap in gaps,definition in definitions,mode in [false,true])
    check(prefix,gap,definition,mode)^];
{cases:len(results),supported:all([for(r in results)r.issue==null]),
 tree:all([for(r in results)r.tree]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics])}
