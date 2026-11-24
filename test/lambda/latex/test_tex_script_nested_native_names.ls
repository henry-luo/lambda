import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let definitions=["\\def\\g{\\def\\x{\\a@b}}", "\\def\\g{\\def\\a@b{X}}", "\\catcode`\\:=11 \\def\\g{\\def\\x{\\a:b}}", "\\catcode`\\@=13 \\def\\g{\\def\\x{\\a@b}}", "\\def\\g{\\def\\x{X\n\nY}}", "\\def\\g{\\def\\x{\\a@b{X}}}"];
fn check(definition,mode) any^ {
 let source=definition ++ "\\def\\f{\\addtolength{\\parindent}{2pt}}\\f\\begin{document}A\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"tex"})^;
 {definition:definition,mode:mode,issue:d.issue,unsupported:d.unsupported,tree:d.ast==parse(source,{type:"latex"})^,text:reconstruct.reconstruct(r)^.text==n.text,diagnostics:r.diagnostic_records==n.diagnostics}
}
let cases=[for(d in definitions,m in [false,true])check(d,m)^];
{cases:len(cases),tree:all([for(c in cases where c.mode)c.issue==null and c.tree]),
 guarded:all([for(c in cases where not c.mode)c.issue=="latex-digest-unsupported"]),
 text:all([for(c in cases)c.text]),diagnostics:all([for(c in cases)c.diagnostics])}
