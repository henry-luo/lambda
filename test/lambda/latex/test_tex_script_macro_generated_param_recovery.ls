import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=["\\def\\m{#1}\\m",
"\\def\\m{#x}\\m",
"\\def\\m{#}\\m",
"\\def\\m{##1}\\m",
"\\def\\m#1{#2/#1}\\m{A}",
"\\def\\m#1{#1/#1}\\m{A}",
"\\long\\def\\m#1{#x#1}\\m{A}",
"\\gdef\\m{#1}\\m",
"\\def\\q{\\def\\m{#1}}\\q\\m",
"\\def\\q{\\def\\m{#x}}\\q\\m",
"\\def\\q{\\def\\m{#}}\\q\\m",
"\\def\\q{\\def\\m{##1}}\\q\\m",
"\\def\\q{\\def\\m#1{#2/#1}}\\q\\m{A}",
"\\def\\q#1{\\def\\m{#1}}\\q{A}\\m",
"\\def\\q#1{\\def\\m{##1/#1}}\\q{A}\\m",
"\\def\\q{\\gdef\\m{#x}}\\q\\m",
"\\def\\q{\\def\\r{\\def\\m{#x}}\\r}\\q\\m",
"\\def\\q{\\def\\m{#1}\\def\\n{#x}}\\q\\m\\n",
"\\def\\q{\\def\\m}\\q{#1}\\m",
"\\def\\q{\\def\\m}\\q{#x}\\m",
"\\def\\q#1{\\def\\m{#1}}\\q{#1}\\m",
"\\def\\q#1{\\def\\m{#1}}\\q{#x}\\m",
"\\def\\q#1{\\def\\m{A#1B}}\\q{#2}\\m",
"\\def\\q#1{\\def\\m{#1}}\\q{\r\n#2}\r\n\\m",
"é\\def\\q#1{\\def\\m{#1}}\\q{#1}\\m"];
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
let path=root ++ "raw-param-main.tex";
let source=input(path,"text")^;
let child=input(root ++ "raw-param-child.tex","text")^;
let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],source_name:path,
 resources:{local:{'raw-param-child.tex':{data:child}},bundled:{}}},100000)^;
let built=reconstruct.reconstruct(result)^;
let native=input(path,"tex")^;
let tree=digest.digest(result)^;
{ok:result.issue==null and built.issue==null and tree.issue==null,text:built.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,tree:tree.ast==(input(path,"latex")^)}
