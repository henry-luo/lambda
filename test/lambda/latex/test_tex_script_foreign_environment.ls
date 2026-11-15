import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_env_foreign_main.tex";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{
 'script_env_foreign_child.tex':{data:input(root ++ "script_env_foreign_child.tex","text")^},
 'script_env_foreign_split.tex':{data:input(root ++ "script_env_foreign_split.tex","text")^}},bundled:{}};
let source=input(main,"text")^;
let expanded=profile.expand_with_kernel(source,kernel,
 {source_name:main,echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:resources},100000)^;
let result=digest.digest(expanded)^;
let native=input(main,"latex")^;
{issue:result.issue,unsupported:result.unsupported,matches_native:result.ast==native}
