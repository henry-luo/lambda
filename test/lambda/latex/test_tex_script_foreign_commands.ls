import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_command_foreign_main.tex";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{
 'script_command_foreign_rich.tex':{data:input(root ++ "script_command_foreign_rich.tex","text")^},
 'script_command_foreign_bare.tex':{data:input(root ++ "script_command_foreign_bare.tex","text")^},
 'script_command_foreign_generated.tex':{data:input(root ++ "script_command_foreign_generated.tex","text")^}},bundled:{}};
fn expanded(source,mode) any^ => profile.expand_with_kernel(source,kernel,
 {source_name:main,echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:resources},100000)
let source=input(main,"text")^;
let enabled=digest.digest(expanded(source,true)^)^;
let disabled=digest.digest(expanded(source,false)^)^;
let native=input(main,"latex")^;
let macro="\\def\\a{(1,2)}\\def\\m{\\mystery}\\begin{document}\\m\\a\\end{document}";
let generated=digest.digest(expanded(macro,true)^)^;
{included:{issue:enabled.issue,matches_native:enabled.ast==native},
 ordinary_mode:{issue:disabled.issue,unsupported:disabled.unsupported},
 generated:{issue:generated.issue,matches_native:generated.ast==parse(macro,{type:"latex"})^}}
