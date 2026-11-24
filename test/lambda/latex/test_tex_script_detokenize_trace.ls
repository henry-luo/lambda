import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let forms=["\\edef\\m{\\detokenize{abc}}\\m", "\\edef\\m{\\detokenize{#1}}\\m",
 "\\edef\\m{X\\detokenize{a b}Y}\\m", "\\edef\\m#1{\\detokenize{#1}#1}\\m{A}",
 "\\edef\\m{\\detokenize{\\foo}}\\m", "\\edef\\m{\\detokenize{a{b}c}}\\m",
 "\\xdef\\m{\\detokenize{é猫}}\\m", "\\edef\\m{\\detokenize{a\nb}}\\m",
 "\\edef\\m{\\detokenize{a%comment\nb}}\\m", "\\def\\d{\\detokenize{a}}\\edef\\m{\\d}\\m",
 "\\edef\\m{\\detokenize{}}\\m", "\\edef\\m{\\detokenize{##}}\\m",
 "\\def\\m{\\detokenize{a}}\\m", "\\detokenize{a}",
 "\\begin{document}\\edef\\m{\\detokenize{abc}}\\m\\end{document}",
 "\\begin{document}\\edef\\m{\\detokenize{#1}}\\m\\end{document}",
 "\\begin{document}\\edef\\m{\\detokenize{\\foo}}\\m\\end{document}",
 "\\begin{document}\\textbf{\\detokenize{abc}}\\end{document}"];
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let built=reconstruct.reconstruct(r)^; let native=parse(source,{type:"tex"})^;
 let tree=digest.digest(r)^; let native_tree=parse(source,{type:"latex"})^;
 {ok:r.issue==null and built.issue==null and tree.issue==null,
 text:built.text==native.text,diagnostics:r.diagnostic_records==native.diagnostics,tree:tree.ast==native_tree}
}
let results=[for(source in forms)check(source)^];
{cases:len(results),ok:all([for(r in results)r.ok]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics]),tree:all([for(r in results)r.tree])}
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "detokenize-trace-main.tex";
let source=input(path,"text")^;
let child=input(root ++ "detokenize-trace-child.tex","text")^;
let expanded=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],source_name:path,
 resources:{local:{'detokenize-trace-child.tex':{data:child}},bundled:{}}},100000)^;
let tree=digest.digest(expanded)^;
let tex=input(path,"tex")^;
{ok:expanded.issue==null and tree.issue==null,text:reconstruct.reconstruct(expanded)^.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:tree.ast==(input(path,"latex")^)}
let guarded_source="\\xdef\\m{\\detokenize{#1}}\\meaning\\m";
let guarded=profile.expand_with_kernel(guarded_source,kernel,
 {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let guarded_tree=digest.digest(guarded)^;
let guarded_native=parse(guarded_source,{type:"tex"})^;
{guard:guarded_tree.issue=="latex-digest-unsupported" and guarded_tree.ast==null and
 guarded_tree.unsupported=="generated text source mapping",
 text:reconstruct.reconstruct(guarded)^.text==guarded_native.text,
 diagnostics:guarded.diagnostic_records==guarded_native.diagnostics}
