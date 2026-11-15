import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(body) any^ {
    let source="\\begin{document}" ++ body ++ "\\end{document}";
    let expanded=profile.expand_with_kernel(source,kernel,options,100000)^;
    let script=digest.digest(expanded)^;
    let native=parse(source,{type:"latex"})^;
    {body:body,issue:script.issue,unsupported:script.unsupported,
        matches:if(script.ast==null)false else script.ast==native}
}
let gaps=["\n%comment\n\n", "%comment\n\n", "\n\n\n", "\n \n", "\r\n\r\n", "\n%é\r\n\n"];
let prefixes=["A", "\\textbf{A}", "\\def\\x{A}\\x", "\\section{A}"];
let matrix=[for(prefix in prefixes, gap in gaps) check(prefix ++ gap ++ "Text")^];
let special=[for(body in ["A\\par % comment\n\nB", "A\\par  % comment\r\n\nB",
    "A\\par\n  % comment\n\nB", "A%comment\r\nB", "A\\par%comment\r\n\\nB",
    "A\\par%comment\n\\section{B}", "\\quad\n%comment\n\nB",
    "\\textbf{A\n%comment\n\nB}", "\\newcommand{\\x}{A}%comment\n\nB"]) check(body)^];
let results=[*matrix,*special];
{cases:len(results),failures:[for(result in results where not result.matches) result]}
