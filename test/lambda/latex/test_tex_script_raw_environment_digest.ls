import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn options(mode) => {echo_definitions:true,constructor_argument_mode:mode,
    adapters:[],resources:{local:{},bundled:{}}}
// The matrix shares an immutable kernel for each exact configuration.
let preloads=[for(mode in [false,true])profile.preload_kernel(kernel,options(mode),100000)^];
let names=["tikzpicture","verbatim","lstlisting","minted","alltt","Verbatim","BVerbatim","LVerbatim",
    "SaveVerbatim","VerbatimOut","fancyvrb","comment","filecontents","filecontents*","luacode","luacode*"];
fn check(name,body,mode) any^ {
 let header=if(name=="tikzpicture") " [scale=2]" else if(name=="lstlisting") "[language=X]"
    else if(name=="filecontents" or name=="filecontents*") "{out.tex}" else "";
 let source="\\begin{document}A\\begin{" ++ name ++ "}" ++ header ++ body ++ "\\end{" ++ name ++ "}B\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,options(mode),100000,preloads[if(mode)1 else 0])^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;let t=parse(source,{type:"tex"})^;
 {issue:d.issue,tree:d.ast==n,text:reconstruct.reconstruct(r)^.text==t.text,diagnostics:r.diagnostic_records==t.diagnostics}
}
let cases=[for(name in names,body in ["", "\n\\x{é}% raw\n"],mode in [false,true])check(name,body,mode)^];
let unsafe=["\\begin{document}\\begin{tikzpicture}X% \\end{tikzpicture}\nY\\end{tikzpicture}Z\\end{document}",
 "\\begin{document}\\begin{verbatim}X\\\\end{verbatim}Y\\end{verbatim}Z\\end{document}",
 "\\begin{document}\\begin{tikzpicture}\\begin{tikzpicture}X\\end{tikzpicture}Y\\end{tikzpicture}Z\\end{document}",
 "\\begin{document}\\begin{tikzpicture}missing",
 "\\def\\name{tikzpicture}\\begin{document}\\begin{\\name}X\\end{tikzpicture}Z\\end{document}",
 "\\let\\go\\begin\\begin{document}\\go{verbatim}X\\end{verbatim}Z\\end{document}"];
fn guarded(source,mode) any^ {
 let r=profile.expand_with_kernel(source,kernel,options(mode),100000,preloads[if(mode)1 else 0])^;
 let d=digest.digest(r)^;let n=parse(source,{type:"tex"})^;
 {guard:d.issue=="latex-digest-unsupported",text:reconstruct.reconstruct(r)^.text==n.text,
    diagnostics:r.diagnostic_records==n.diagnostics}
}
let guards=[for(source in unsafe,mode in [false,true])guarded(source,mode)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-raw-environment-main.tex";let child="script-raw-environment-child.tex";
fn foreign(mode) any^ {
 let r=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,
    constructor_argument_mode:mode,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=input(path,"latex")^;
 {ok:if(mode) d.issue==null and d.ast==n else d.issue=="latex-digest-unsupported" and d.unsupported=="command source mapping"}
}
let foreign_cases=[for(mode in [false,true])foreign(mode)^];
{cases:len(cases),supported:all([for(r in cases)r.issue==null]),tree:all([for(r in cases)r.tree]),
 text:all([for(r in cases)r.text]),diagnostics:all([for(r in cases)r.diagnostics]),
 guards:len(guards),guarded:all([for(r in guards)r.guard and r.text and r.diagnostics]),
 foreign:len(foreign_cases),foreign_ok:all([for(r in foreign_cases)r.ok])}
