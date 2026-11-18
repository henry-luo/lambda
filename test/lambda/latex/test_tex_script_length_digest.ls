import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let commands=["\\setlength{\\parindent}{25pt}","\\setlength\\parindent{25pt}",
 "\\setlength  {\\parskip} {10pt plus 1pt minus 1pt}","\\setlength{\\parindent}{1pt}",
 "\\setlength{\\undefined}{2pt}","\\setlength{bad}{2pt}","\\setlength{}{2pt}",
 "\\setlength{\\x \\y}{2pt}","\\setlength{\\parindent}{ 2pt }",
 "\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}","\\setlength{\\parindent}{2pt% note\n}",
 "\\setlength{\\parindent}{2pt}{abc}","\\setlength{\\parindent}{2pt} X",
 "\\setlength{\\parindent}{2pt}\n\nX","\\setlength{\\parindent}{2pt}%test\nX",
 "\\setlength{\\parindent}{2pt}\\textbf{X}","\\setlength{\\parindent}{2pt}é",
 "\\setlength\\parindent {2pt}","\\setlength\n\\parindent\n{2pt}",
 "\\setlength{\\parindent}{2pt}\\setlength{\\parskip}{3pt}"];
let nested=["\\textbf{A\\setlength{\\parindent}{2pt}B}",
 "{A\\setlength\\parindent{2pt}B}", "\\textit{\\setlength{\\parindent}{2pt}}",
 "\\textbf{\\setlength{\\parindent}{2pt}  X}","\\textbf{\\setlength{\\parindent}{2pt}\\%}",
 "\\section{Title \\setlength{\\parindent}{2pt} X}"];
fn check(command,mode,where) any^ {
 let source=if(where==0) command ++ "\\begin{document}A\\end{document}"
    else "\\begin{document}A" ++ command ++ "B\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,
 adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^; let t=parse(source,{type:"tex"})^;
 {issue:d.issue,tree:d.ast==n,text:reconstruct.reconstruct(r)^.text==t.text,diagnostics:r.diagnostic_records==t.diagnostics}
}
let symbols=["\\setlength\\%{2pt}","\\setlength\\\\{2pt}"];
let checks=[for(command in [*commands,*nested,*symbols],mode in [false,true],where in [0,1])check(command,mode,where)^];
let generated=["\\def\\f{\\setlength{\\parindent}{2pt}}\\f\\begin{document}A\\end{document}",
 "\\begin{document}A\\def\\f{\\setlength{\\parindent}{2pt}}\\f B\\end{document}"];
let generated_extra=["\\def\\f{\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}}\\f\\begin{document}X\\end{document}",
 "\\def\\f{\\setlength{\\escapechar}{-1}\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}}\\f\\begin{document}X\\end{document}",
 "\\def\\f{\\setlength{\\escapechar}{65}\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}}\\f\\begin{document}X\\end{document}",
 "\\catcode`\\~=13\\newlength{~}\\def\\f{\\setlength{~}{2pt}}\\f\\the~\\begin{document}X\\end{document}"];
fn generated_check(source,mode) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,
 adapters:[],resources:{local:{},bundled:{}}},100000)^; let d=digest.digest(r)^;
 let native=parse(source,{type:"tex"})^;
 r.issue==null and reconstruct.reconstruct(r)^.text==native.text and r.diagnostic_records==native.diagnostics and
    d.issue==null and d.ast==parse(source,{type:"latex"})^
}
let generated_checks=[for(source in [*generated,*generated_extra],mode in [false,true])generated_check(source,mode)^];
{cases:len(checks),supported:all([for(c in checks)c.issue==null]),tree:all([for(c in checks)c.tree]),
 text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
 generated:len(generated_checks),generated_ok:all(generated_checks)};

let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-length-digest-main.tex"; let child="script-length-digest-child.tex";
fn foreign(mode) any^ {
 let result=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,
 constructor_argument_mode:mode,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(result)^; let native=input(path,"latex")^;
 d.issue==null and d.ast==native
}
let foreign_cases=[for(mode in [false,true])foreign(mode)^];
{foreign:len(foreign_cases),foreign_ok:all(foreign_cases)}
