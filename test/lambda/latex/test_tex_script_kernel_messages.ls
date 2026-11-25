import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source,mode) any^ {
 let ex=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let tex=parse(source,{type:"tex"})^;
 {supported:ex.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}
let sources=[
 "X",
 "\\begin{document}X\\end{document}",
 "\\message{one}\\message{two}",
 "\\typeout{one}\\typeout{two}",
 "\\newcount\\foo\\message{end}",
 "\\iffalse\\ExplSyntaxOn\\fi X",
 "\\let\\ExplSyntaxOn\\relax\\ExplSyntaxOn X",
 "\\ExplSyntaxOn X\\ExplSyntaxOff",
 "\\def\\switch{\\ExplSyntaxOn}\\switch X\\ExplSyntaxOff",
 "\\csname ExplSyntaxOn\\endcsname X\\ExplSyntaxOff",
 "\\message{first}\\ExplSyntaxOn\\message{second}\\ExplSyntaxOff\\message{third}",
 "\\ProvidesExplPackage{demo}{2026/01/01}{1}{Demo}X"
];
let results=[for(s in sources,mode in [false,true]) check(s,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics])};
let requests=[for(s in ["\\RequirePackage{expl3}X","\\usepackage{expl3}X"],mode in [false,true]) check(s,mode)^];
{cases:len(requests),supported:all([for(r in requests) r.supported]),
 messages:all([for(r in requests) r.messages]),diagnostics:all([for(r in requests) r.diagnostics])};
let ordinary_requests=[for(s in ["\\RequirePackage{expl3}X","\\usepackage{expl3}X"],mode in [false,true]) {
 let ex=profile.expand_with_kernel(s,kernel,{*:options,script_expl3:false,constructor_argument_mode:mode},100000)^;
 let tex=parse(s,{type:"tex"})^;
 {supported:ex.issue==null,messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}];
{cases:len(ordinary_requests),supported:all([for(r in ordinary_requests) r.supported]),
 messages:all([for(r in ordinary_requests) r.messages]),diagnostics:all([for(r in ordinary_requests) r.diagnostics])};
let malformed=[for(s in ["\\ProvidesExplPackage","\\ProvidesExplPackage{broken}"],mode in [false,true]) {
 let ex=profile.expand_with_kernel(s,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let tex=parse(s,{type:"tex"})^;
 {recovered:ex.issue==null,messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}];
{cases:len(malformed),recovered:all([for(r in malformed) r.recovered]),
 messages:all([for(r in malformed) r.messages]),diagnostics:all([for(r in malformed) r.diagnostics])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-kernel-messages-main.tex";
let child="script-kernel-messages-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let tex=input(path,"tex")^;
 {supported:ex.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}]
