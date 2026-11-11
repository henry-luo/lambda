import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let text=join([for(t in r.output) if(t.kind == "control") "\\" ++ t.value else t.value],"");
 let n=parse(source,{type:"tex"})^;
 {case:label,issue:r.issue,script:text,native:n.text,matches:text == n.text}
}
report("main declaration", "\\usepackage{xcolor}")
report("main require", "\\RequirePackage{xcolor}")
report("macro name", "\\def\\p{xcolor}\\usepackage{\\p}")
report("macro command", "\\def\\load{\\RequirePackage{xcolor}}\\load")
report("options", "\\RequirePackage[draft]{xcolor}")
report("duplicate", "\\usepackage{xcolor}\\usepackage{xcolor}")
report("empty", "\\usepackage{}")
report("multiple", "\\usepackage{xcolor,foo}")
let root="test/lambda/latex/fixtures/local_packages/";
let doc=input(root ++ "document.tex","text")^;
let sty=input(root ++ "lmdnotes.sty","text")^;
let cls=input(root ++ "lmdreport.cls","text")^;
let r=profile.expand_with_kernel(doc,kernel,{adapters:[],resources:{local:{'lmdnotes.sty':{data:sty},'lmdreport.cls':{data:cls}},bundled:{}}},100000)^;
let text=join([for(t in r.output) if(t.kind == "control") "\\" ++ t.value else t.value],"");
let n=input(root ++ "document.tex","tex")^;
{case:"local package",issue:r.issue,script_has_declaration:contains(text,"\\usepackage{xcolor}"),native_has_declaration:contains(n.text,"\\usepackage{xcolor}"),script_echoes_foreign_require:contains(text,"\\RequirePackage{xcolor}")}
