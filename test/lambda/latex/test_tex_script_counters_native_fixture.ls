import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import registry: lambda.doc.latex.packages.registry
import native_snapshot: ._tex_native_snapshot
let native=input("test/lambda/latex/fixtures/tex_counters_fixture_native.json","json")^;
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "document.tex";
let source=input(path,"text")^;
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let local={'lmdnotes.sty':{data:input(root ++ "lmdnotes.sty","text")^},
    'lmdreport.cls':{data:input(root ++ "lmdreport.cls","text")^}};
fn compare(adapters,constructor_mode,reference) any^ {
    let expanded=profile.expand_with_kernel(source,kernel,
        {source_name:path,echo_definitions:true,adapters:adapters,constructor_argument_mode:constructor_mode,
            resources:{local:local,bundled:{}}},100000)^;
    let result=digest.digest(expanded)^;
    let text=reconstruct.reconstruct(expanded)^;
    {expansion_issue:expanded.issue, issue:result.issue, unsupported:result.unsupported,
        text_match:path==reference.source and source==reference.source_text and adapters==reference.adapters and
            (not constructor_mode or (registry.RAW_ARGUMENT_COMMANDS==reference.raw and registry.chaptered_classes()==reference.chaptered)) and text.text==reference.tex.text, ast_match:if(result.ast==null) null else native_snapshot.tree_matches(result.ast,reference,path)^}
}
{without_adapters:compare([],false,native.without)^,
 with_adapters:compare(registry.adapter_names(),true,native.with)^}
