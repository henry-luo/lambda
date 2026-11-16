import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source,mode,ast=false) any^ {
 let ex=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let tex=parse(source,{type:"tex"})^; let d=if(ast) digest.digest(ex)^ else null;
 {supported:ex.issue==null and (not ast or d.issue==null),text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,
  tree:if(ast and d.issue==null)d.ast==parse(source,{type:"latex"})^ else not ast}
}
let sources=[
 "\\chardef\\a=0 \\the\\a",
 "\\chardef\\a=65 \\the\\a",
 "\\mathchardef\\a=1234 \\the\\a",
 "\\chardef\\a=17 \\let\\b\\a \\the\\b",
 "\\chardef\\a=7 \\edef\\b{\\the\\a}\\b",
 "\\chardef\\a=7 \\showthe\\a",
 "\\mathchardef\\a=\"1234 \\showthe\\a",
 "\\chardef\\a=2 \\message{the=\\the\\a}",
 "\\chardef\\a=6 \\count0=\\the\\a \\the\\count0",
 "\\chardef\\a=2 \\ifnum\\the\\a=2 Y\\else N\\fi",
 "\\mathchardef\\a=32767 \\number\\the\\a",
 "\\chardef\\a=255 \\the\\a/\\number\\a",
 "\\chardef\\a=17 {\\chardef\\a=4 \\the\\a}\\the\\a",
 "\\chardef\\a=17 {\\global\\chardef\\a=4}\\the\\a",
 "\\chardef\\a=10 \\the\\numexpr\\the\\a+2\\relax",
 "\\chardef\\a=4 \\let\\b\\a \\ifnum\\the\\b=4 T\\else F\\fi",
 "\\mathchardef\\a=\"7000 \\edef\\b{\\the\\a}\\b",
 "\\chardef\\a=48 \\edef\\b{X\\the\\a Y}\\b"
];
let results=[for(source in sources,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics])};
let ast_sources=[
 "\\begin{document}\\chardef\\a=3 \\the\\a\\end{document}",
 "\\begin{document}\\mathchardef\\a=1234 \\the\\a\\end{document}",
 "\\begin{document}\\chardef\\a=3 \\edef\\b{\\the\\a}\\b\\end{document}",
 "\\begin{document}\\chardef\\a=3 \\ifnum\\the\\a=3 \\textbf{Yes}\\else No\\fi\\end{document}"
];
let trees=[for(source in ast_sources,mode in [false,true]) check(source,mode,true)^];
{cases:len(trees),supported:all([for(r in trees) r.supported]),text:all([for(r in trees) r.text]),
 messages:all([for(r in trees) r.messages]),diagnostics:all([for(r in trees) r.diagnostics]),tree:all([for(r in trees) r.tree])};
let path="test/lambda/latex/fixtures/local_packages/script-the-character-main.tex";let child="script-the-character-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input("test/lambda/latex/fixtures/local_packages/" ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(ex)^;let tex=input(path,"tex")^;let native=input(path,"latex")^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}]
