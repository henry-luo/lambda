import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let root="test/lambda/latex/fixtures/local_packages/";let path=root ++ "nested-native-foreign-main.tex";
let a="nested-native-foreign-a.tex";let b="nested-native-foreign-b.tex";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let r=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,
 constructor_argument_mode:true,adapters:[],resources:{local:{[a]:{data:input(root ++ a,"text")^},[b]:{data:input(root ++ b,"text")^}},bundled:{}}},100000)^;
let d=digest.digest(r)^;let n=input(path,"latex")^;let t=input(path,"tex")^;
{supported:d.issue==null,tree:d.ast==n,text:reconstruct.reconstruct(r)^.text==t.text,diagnostics:r.diagnostic_records==t.diagnostics}
