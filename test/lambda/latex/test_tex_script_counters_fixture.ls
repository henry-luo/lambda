import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "document.tex";
let source=input(path,"text")^;
let style=input(root ++ "lmdnotes.sty","text")^;
let class=input(root ++ "lmdreport.cls","text")^;
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,
 resources:{local:{'lmdnotes.sty':{data:style},'lmdreport.cls':{data:class}},bundled:{}}},100000)^;
let text=join([for(t in r.output) if(t.kind == "control") "\\" ++ t.value else t.value],"");
let native=input(path,"tex")^;
let ast=digest.digest(r)^;
{expansion_issue:r.issue, first_exercise:contains(text,"Exercise 1.") and contains(native.text,"Exercise 1."),
 second_exercise:contains(text,"Exercise 2.") and contains(native.text,"Exercise 2."),
 digest_issue:ast.issue, digest_unsupported:ast.unsupported}
