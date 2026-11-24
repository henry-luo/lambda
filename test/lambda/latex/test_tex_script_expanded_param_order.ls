import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\def\\z{}\\edef\\m{#\\z 1}\\m", "\\def\\z{2}\\edef\\m#1{#\\z}\\m{A}",
 "\\def\\z{1}\\edef\\m#1{#\\z}\\m{A}", "\\def\\z{}\\xdef\\m{#\\z x}\\m",
 "\\def\\z{}\\edef\\m{#\\z \\number a}\\m", "\\def\\z{}\\edef\\m{#\\z \\number a #\\z x}\\m",
 "\\edef\\m{\\unexpanded{#1}}\\m", "\\edef\\m#1{#\\unexpanded{1}}\\m{A}",
 "\\toks0={#1}\\edef\\m{\\the\\toks0}\\m", "\\toks0={1}\\edef\\m#1{#\\the\\toks0}\\m{A}",
 "\\edef\\m{\\noexpand\\detokenize{#1}}\\m", "\\edef\\m{\\noexpand\\detokenize{#x}}\\m",
 "\\edef\\m#1{\\noexpand\\detokenize{#1}}\\m{A}", "\\edef\\m{\\detokenize{#1}}\\m"
];
fn check(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let built=reconstruct.reconstruct(result)^;
 let native=parse(source,{type:"tex"})^;
 {ok:result.issue==null and built.issue==null,text:built.text==native.text,diagnostics:result.diagnostic_records==native.diagnostics}
}
let results=[for(source in cases)check(source)^];
{cases:len(results),ok:all([for(r in results)r.ok]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics])}
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "expanded-param-main.tex";
let source=input(path,"text")^;
let child=input(root ++ "expanded-param-child.tex","text")^;
let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],source_name:path,
 resources:{local:{'expanded-param-child.tex':{data:child}},bundled:{}}},100000)^;
let built=reconstruct.reconstruct(result)^;
let native=input(path,"tex")^;
let tree=digest.digest(result)^;
{ok:result.issue==null and built.issue==null and tree.issue==null,text:built.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,tree:tree.ast==(input(path,"latex")^)}
