import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let definitions=["\\def\\g{\\a@b}","\\def\\a@b{X}","\\catcode`\\:=11 \\def\\a:b{X}",
 "\\catcode`\\@=13 \\def\\g{\\a@b}","\\def~{X\n\nY}"];
let values=["2pt", "\\dimexpr1pt+2pt\\relax", "\\foo:bar", "2pt plus 1pt"];
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:true,
 adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"tex"})^;
 {tree:d.issue==null and d.ast==parse(source,{type:"latex"})^,
 text:reconstruct.reconstruct(r)^.text==n.text,diagnostics:r.diagnostic_records==n.diagnostics}
}
let source_suffix="\\begin{document}A\\f{more}\\end{document}";
let ds=[for(definition in definitions) check(definition ++ "\\def\\f{\\addtolength{\\parindent}{2pt}}" ++ source_suffix)^];
let vs=[for(value in values) check("\\def\\g{\\a@b}\\def\\f{\\addtolength{\\parindent}{" ++ value ++ "}}" ++ source_suffix)^];
let checks=[*ds,*vs];
{cases:len(checks),tree:all([for(c in checks)c.tree]),text:all([for(c in checks)c.text]),
 diagnostics:all([for(c in checks)c.diagnostics])}
