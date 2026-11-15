import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import registry: lambda.doc.latex.packages.registry
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "document.tex";
let source=input(path,"text")^;
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let local={'lmdnotes.sty':{data:input(root ++ "lmdnotes.sty","text")^},
    'lmdreport.cls':{data:input(root ++ "lmdreport.cls","text")^}};
fn compare(adapters,constructor_mode,native_text,native_ast) any^ {
    let expanded=profile.expand_with_kernel(source,kernel,
        {source_name:path,echo_definitions:true,adapters:adapters,constructor_argument_mode:constructor_mode,
            resources:{local:local,bundled:{}}},100000)^;
    let result=digest.digest(expanded)^;
    let text=reconstruct.reconstruct(expanded)^;
    {expansion_issue:expanded.issue, issue:result.issue, unsupported:result.unsupported,
        text_match:text.text==native_text.text, ast_match:if(result.ast==null) null else result.ast==native_ast}
}
let adapter_options={base:path,packages:registry.adapter_names(),raw:registry.RAW_ARGUMENT_COMMANDS,
    chaptered:registry.chaptered_classes()};
{without_adapters:compare([],false,input(path,"tex")^,input(path,"latex")^)^,
 with_adapters:compare(registry.adapter_names(),true,
    parse(source,{*:adapter_options,type:"tex"})^,parse(source,{*:adapter_options,type:"latex"})^)^}
