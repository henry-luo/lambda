import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{
 'script_definition_foreign_child.tex':{data:input(root ++ "script_definition_foreign_child.tex","text")^},
 'script_definition_catcode_child.tex':{data:input(root ++ "script_definition_catcode_child.tex","text")^},
 'script_definition_unicode_child.tex':{data:input(root ++ "script_definition_unicode_child.tex","text")^}},bundled:{}};
fn expand(source,name) any^ => profile.expand_with_kernel(source,kernel,
 {source_name:name,echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:resources},100000)
let path=root ++ "script_definition_foreign_main.tex";
let result=digest.digest(expand(input(path,"text")^,path)^)^;
let direct="\\begin{document}A\\mystery{\\def\\x{Q}\\newcommand{\\f}[1]{#1}a}T\\end{document}";
let direct_result=digest.digest(expand(direct,"<direct>")^)^;
let bad_path=root ++ "script_definition_catcode_main.tex";
let guarded=digest.digest(expand(input(bad_path,"text")^,bad_path)^)^;
let unicode_path=root ++ "script_definition_unicode_main.tex";
let unicode=digest.digest(expand(input(unicode_path,"text")^,unicode_path)^)^;
{included:{issue:result.issue,matches_native:result.ast==input(path,"latex")^},
 direct:{issue:direct_result.issue,matches_native:direct_result.ast==parse(direct,{type:"latex"})^},
 incompatible_lexing:{issue:guarded.issue,unsupported:guarded.unsupported},
 unicode_control:{issue:unicode.issue,unsupported:unicode.unsupported}}
