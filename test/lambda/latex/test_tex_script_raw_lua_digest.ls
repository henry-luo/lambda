import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 {body:"\\directlua{hello}Z",supported:true},
 {body:"\\luaexec{A {B} C}Z",supported:true},
 {body:"\\directlua {}Z",supported:true},
 {body:"é\\directlua{λ}B",supported:true},
 {body:"\\def\\q{abc}\\directlua\\q",supported:true},
 {body:"\\def\\q{}\\directlua\\q X",supported:true},
 {body:"\\directlua{a}% x\n Z",supported:true},
 {body:"\\directlua{a}\n\nX",supported:true},
 {body:"\\directlua X",supported:true},
 {body:"\\directlua  \\textbf{X}",supported:true},
 {body:"\\directlua{a}\\lstinline|x|",supported:true},
 {body:"\\def\\luaexec#1{EXPANDED}\\luaexec{X}",supported:true},
 {body:"\\directlua\n{X}",supported:false},
 {body:"\\directlua{\\end{document}}Z",supported:false},
 {body:"\\textbf{A\\directlua{B}C}",supported:false}
];
fn check(c) any^ {
 let source="\\begin{document}" ++ c.body ++ "\\end{document}";
 let ex=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let tree=digest.digest(ex)^; let native=parse(source,{type:"latex"}) ^ {null}; let tex=parse(source,{type:"tex"})^;
 {supported:(tree.issue==null)==c.supported,text:reconstruct.reconstruct(ex)^.text==tex.text,
 diagnostics:ex.diagnostic_records==tex.diagnostics,tree:if(c.supported)tree.ast==native else tree.ast==null}
}
let results=[for(c in cases)check(c)^];
{cases:len(results),supported:all([for(r in results)r.supported]),text:all([for(r in results)r.text]),
 diagnostics:all([for(r in results)r.diagnostics]),tree:all([for(r in results)r.tree])}

let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "raw-lua-main.tex";
let source=input(path,"text")^;
let included=input(root ++ "raw-lua-inc.tex","text")^;
let expanded=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:true,
 adapters:[],source_name:path,resources:{local:{'raw-lua-inc.tex':{data:included}},bundled:{}}},100000)^;
let tree=digest.digest(expanded)^;
let tex=input(path,"tex")^;
{supported:tree.issue==null,text:reconstruct.reconstruct(expanded)^.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:tree.ast==(input(path,"latex")^)}
