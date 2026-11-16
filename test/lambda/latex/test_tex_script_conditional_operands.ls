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
  "\\if a\\iffalse x\\else a\\fi T\\else F\\fi",
  "\\if a\\iffalse x\\else b\\fi T\\else F\\fi",
  "\\ifcat a\\iffalse x\\else b\\fi T\\else F\\fi",
  "\\ifcat a\\iffalse x\\else 1\\fi T\\else F\\fi",
  "\\if\\iffalse x\\else a\\fi a T\\else F\\fi",
  "\\if a\\iftrue a\\else x\\fi T\\else F\\fi",
  "\\if a\\iftrue b\\else x\\fi T\\else F\\fi",
  "\\let\\test\\if\\test a\\iffalse x\\else a\\fi T\\else F\\fi",
  "\\unless\\if a\\iffalse x\\else a\\fi T\\else F\\fi",
  "\\if a\\else T\\fi X",
  "\\ifcat a\\fi X",
  "\\def\\a{X}\\ifcsname\\iffalse z\\else a\\endcsname\\fi T\\else F\\fi",
  "\\ifcsname\\iffalse z\\else missing\\endcsname\\fi T\\else F\\fi",
  "\\ifvoid0\\else F\\fi X",
  "\\ifhbox0\\else F\\fi X",
  "\\ifeof0\\else F\\fi X",
  "\\if a\\iftrue\\iffalse z\\else a\\fi\\else z\\fi T\\else F\\fi",
  "\\if a\\iftrue\\iffalse z\\else b\\fi\\else z\\fi T\\else F\\fi",
  "\\def\\inner{\\iffalse x\\else a\\fi}\\if a\\inner T\\else F\\fi",
  "\\if a\\noexpand\\undefined T\\else F\\fi",
  "\\unless\\ifcat a\\iffalse x\\else b\\fi T\\else F\\fi",
  "\\iftrue\\ifcsname\\iffalse z\\else missing\\endcsname\\fi T\\else F\\fi\\else O\\fi",
  "\\unless\\ifeof0\\else T\\fi X",
  "\\ifvoid\\iffalse 1\\else 0\\fi T\\else F\\fi"
];
let results=[for(source in sources,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics])};
let ast_sources=[
  "\\begin{document}Before \\if a\\iffalse x\\else a\\fi T\\else F\\fi After\\end{document}",
  "\\begin{document}\\if a\\iftrue b\\else x\\fi \\textbf{T}\\else \\textit{F}\\fi\\end{document}",
  "\\begin{document}\\ifvoid0\\else F\\fi X\\end{document}",
  "\\begin{document}\\def\\a{X}\\ifcsname\\iffalse z\\else a\\endcsname\\fi T\\else F\\fi\\end{document}"
];
let trees=[for(source in ast_sources,mode in [false,true]) check(source,mode,true)^];
{cases:len(trees),supported:all([for(r in trees) r.supported]),text:all([for(r in trees) r.text]),
 messages:all([for(r in trees) r.messages]),diagnostics:all([for(r in trees) r.diagnostics]),tree:all([for(r in trees) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-conditional-operand-main.tex";
let child="script-conditional-operand-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(ex)^; let tex=input(path,"tex")^; let native=input(path,"latex")^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}]
