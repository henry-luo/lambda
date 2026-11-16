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
  "\\number\\pdftexversion",
  "\\the\\pdftexversion",
  "\\number-\\pdftexversion",
  "\\count0=\\pdftexversion\\the\\count0",
  "\\the\\numexpr\\pdftexversion+1\\relax",
  "\\ifnum\\pdftexversion>100 T\\else F\\fi",
  "\\let\\v\\pdftexversion\\number\\v",
  "\\edef\\v{\\the\\pdftexversion}\\v",
  "\\message{version=\\the\\pdftexversion}",
  "\\showthe\\pdftexversion",
  "\\number\\eTeXversion/\\the\\eTeXversion",
  "\\pdftexrevision/\\eTeXrevision",
  "\\let\\v\\pdftexversion\\showthe\\v",
  "\\count0=1\\advance\\count0 by\\pdftexversion\\the\\count0",
  "\\count0=2\\multiply\\count0 by\\pdftexversion\\the\\count0",
  "\\count0=280\\divide\\count0 by\\pdftexversion\\the\\count0",
  "\\ifnum\\pdftexversion=140 T\\else F\\fi",
  "\\ifnum\\pdftexversion<0 T\\else F\\fi",
  "\\dimen0=\\pdftexversion sp\\the\\dimen0",
  "\\the\\dimexpr\\pdftexversion pt\\relax",
  "\\let\\v\\pdftexversion\\ifnum\\v>0 Y\\else N\\fi",
  "\\edef\\v{\\number\\pdftexversion/\\pdftexrevision}\\v",
  "\\message{e=\\the\\eTeXversion\\eTeXrevision,p=\\the\\pdftexversion\\pdftexrevision}",
  "\\showthe\\eTeXversion",
  "\\pdftexversion",
  "\\pdftexversion=5 Z",
  "\\pdftexversion Z",
  "X\\pdftexversion=5Y",
  "\\let\\v\\pdftexversion\\v=4X",
  "\\global\\pdftexversion=2X",
  "\\afterassignment A\\pdftexversion=4B",
  "\\eTeXversion=3X"
];
let results=[for(source in sources,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics])};
let ast_sources=[
  "\\begin{document}A\\the\\pdftexversion B\\end{document}",
  "\\begin{document}\\ifnum\\pdftexversion>100 \\textbf{Yes}\\else No\\fi\\end{document}",
  "\\begin{document}\\let\\v\\pdftexversion\\number\\v\\end{document}",
  "\\begin{document}\\number\\eTeXversion/\\pdftexrevision\\end{document}"
];
let trees=[for(source in ast_sources,mode in [false,true]) check(source,mode,true)^];
{cases:len(trees),supported:all([for(r in trees) r.supported]),text:all([for(r in trees) r.text]),
 messages:all([for(r in trees) r.messages]),diagnostics:all([for(r in trees) r.diagnostics]),tree:all([for(r in trees) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-engine-identity-main.tex";let child="script-engine-identity-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(ex)^;let tex=input(path,"tex")^;let native=input(path,"latex")^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}]
let diagnostic_path=root ++ "script-engine-identity-diagnostic-main.tex";
let diagnostic_child="script-engine-identity-diagnostic-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(diagnostic_path,"text")^,kernel,{*:options,source_name:diagnostic_path,constructor_argument_mode:mode,
  resources:{local:{[diagnostic_child]:{data:input(root ++ diagnostic_child,"text")^}},bundled:{}}},100000)^;
 let tex=input(diagnostic_path,"tex")^;
 {supported:ex.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}]
