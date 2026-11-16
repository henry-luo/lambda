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
 "\\edef\\a{\\unexpanded{#1}}\\meaning\\a",
 "\\edef\\a#1{\\unexpanded{#1}}\\a{X}",
 "\\edef\\a{\\unexpanded{##1}}\\meaning\\a",
 "\\edef\\a{\\unexpanded{#}}\\meaning\\a",
 "\\edef\\a#1{\\unexpanded{#1}#1}\\a{X}",
 "\\toks0={#1}\\edef\\a{\\the\\toks0}\\meaning\\a",
 "\\toks0={##1}\\edef\\a{\\the\\toks0}\\meaning\\a",
 "\\toks0={#1}\\edef\\a#1{\\the\\toks0/#1}\\a{X}",
 "\\def\\x{Q}\\edef\\a{\\unexpanded{#1\\x}}\\meaning\\a",
 "\\edef\\a{\\unexpanded{{#1}}}\\meaning\\a",
 "\\xdef\\a#1{\\unexpanded{#1}#1}\\a{Z}",
 "\\xdef\\a{\\unexpanded{#1}}\\meaning\\a",
 "\\toks0={#1}\\xdef\\a{\\the\\toks0}\\meaning\\a",
 "\\edef\\a{\\unexpanded{#}1}\\meaning\\a",
 "\\edef\\a#1{\\unexpanded{#}1/#1}\\a{A}",
 "\\edef\\a#1{\\unexpanded{##}1/#1}\\a{A}",
 "\\def\\x{#1}\\edef\\a{\\unexpanded\\expandafter{\\x}}\\meaning\\a"
];
let results=[for(source in sources,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics])};
let ast_sources=[
 "\\begin{document}\\edef\\a#1{\\unexpanded{#1}#1}\\a{X}\\end{document}",
 "\\begin{document}\\toks0={#1}\\edef\\a{\\the\\toks0}\\a\\end{document}",
 "\\begin{document}\\xdef\\a{\\unexpanded{#1}}\\a\\end{document}"
];
let trees=[for(source in ast_sources,mode in [false,true]) check(source,mode,true)^];
{cases:len(trees),supported:all([for(r in trees) r.supported]),text:all([for(r in trees) r.text]),
 messages:all([for(r in trees) r.messages]),diagnostics:all([for(r in trees) r.diagnostics]),tree:all([for(r in trees) r.tree])};
let nested_options={*:options,echo_definitions:false};
let nested=[
 "\\edef\\a#1{\\expanded{\\unexpanded{#1}}}\\a{X}",
 "\\edef\\a#1{\\expanded{\\unexpanded{##1}}}\\a{X}",
 "\\expanded{\\unexpanded{\\def\\a#1{#1}}}\\a{TEST}",
 "\\expanded{\\unexpanded{\\def\\a#1{#1}}}\\show\\a",
 "\\edef\\a#1{\\expanded{\\unexpanded{##1}}/#1}\\a{X}",
 "\\toks0={##1}\\edef\\a#1{\\expanded{\\the\\toks0}}\\a{Y}"
];
let nr=[for(source in nested,mode in [false,true]) {
 let ex=profile.expand_with_kernel(source,kernel,{*:nested_options,constructor_argument_mode:mode},100000)^;
 let tex=parse(source,{type:"tex"})^;
 {supported:ex.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics}
}];
{cases:len(nr),supported:all([for(r in nr) r.supported]),text:all([for(r in nr) r.text]),
 messages:all([for(r in nr) r.messages]),diagnostics:all([for(r in nr) r.diagnostics])};
let path="test/lambda/latex/fixtures/local_packages/script-expanded-parameter-main.tex";
let child="script-expanded-parameter-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input("test/lambda/latex/fixtures/local_packages/" ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(ex)^;let tex=input(path,"tex")^;let native=input(path,"latex")^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}]
