import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=["\\def\\m{abc", "\\def\\m{#1", "\\def\\m{#", "\\def\\m{##",
 "\\edef\\m{#1", "\\xdef\\m{#2", "\\def\\z{}\\edef\\m{#\\z ",
 "\\def\\m", "\\def\\m#1", "\\def\\m#2", "\\def\\m#1#", "\\edef\\m#",
 "\\def\\q{\\def\\m#1}\\q", "\\begin{document}\\def\\m{A#1"];
fn check(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let built=reconstruct.reconstruct(result)^; let native=parse(source,{type:"tex"})^; let tree=digest.digest(result)^;
 {ok:result.issue==null and built.issue==null,text:built.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,guard:tree.ast==null and tree.issue!=null}
}
let results=[for(source in cases)check(source)^];
{cases:len(results),ok:all([for(r in results)r.ok]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics]),guard:all([for(r in results)r.guard])}
fn foreign(path, child, closed) any^ {
 let source=input(path,"text")^;
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],source_name:path,
  resources:{local:{'runaway-child.tex':{data:child}},bundled:{}}},100000)^;
 let built=reconstruct.reconstruct(result)^; let native=input(path,"tex")^; let tree=digest.digest(result)^;
 {ok:result.issue==null and built.issue==null,text:built.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,
  tree:if(closed)tree.issue==null and tree.ast==(input(path,"latex")^) else
    tree.ast==null and tree.issue=="latex-digest-unsupported" and tree.unsupported=="unclosed environment"}
}
let root="test/lambda/latex/fixtures/local_packages/";
let child=input(root ++ "runaway-child.tex","text")^;
foreign(root ++ "runaway-main.tex",child,false)^
foreign(root ++ "runaway-closed-main.tex",child,true)^
