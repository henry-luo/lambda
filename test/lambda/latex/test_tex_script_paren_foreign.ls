import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_paren_foreign_main.tex";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{
 'script_paren_foreign_digit.tex':{data:input(root ++ "script_paren_foreign_digit.tex","text")^},
 'script_paren_foreign_pair.tex':{data:input(root ++ "script_paren_foreign_pair.tex","text")^},
 'script_paren_foreign_group.tex':{data:input(root ++ "script_paren_foreign_group.tex","text")^},
 'script_paren_foreign_word.tex':{data:input(root ++ "script_paren_foreign_word.tex","text")^}},bundled:{}};
let expanded=profile.expand_with_kernel(input(main,"text")^,kernel,
    {source_name:main,echo_definitions:true,adapters:[],resources:resources},100000)^;
let result=digest.digest(expanded)^;
let native=input(main,"latex")^;
fn commands(node) any^ => if(not (node is element)) [] else
    [*(if(string(name(node))=="mystery") [node] else []),*[for(child in node) *commands(child)^]]
{issue:result.issue,commands:if(result.ast==null)0 else len(commands(result.ast)^),
 matches_native:result.ast==native}
