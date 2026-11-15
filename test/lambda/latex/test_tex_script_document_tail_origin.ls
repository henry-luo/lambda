import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let expanded=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let result=digest.digest(expanded)^;
 let rebuilt=reconstruct.reconstruct(expanded)^;
 let native=parse(source,{type:"tex"})^;
 let ast=parse(source,{type:"latex"}) ^ { null };
 {source:source,issue:result.issue,unsupported:result.unsupported,text_match:rebuilt.text==native.text,
  ast_match:if(result.ast==null or ast==null)null else result.ast==ast}
}
let cases=["\\begin{document}X\\end{document}tail",
 "\\def\\finish{\\end{document}}\\begin{document}A\\finish tail",
 "\\def\\name{document}\\begin{document}A\\end{\\name}tail",
 "\\AtEndDocument{Y}\\begin{document}A\\end{document}tail",
 "\\AtEndDocument{}\\begin{document}A\\end{document}tail",
 "\\AtEndDocument{\\relax}\\begin{document}A\\end{document}tail",
 "\\AtEndDocument{\\def\\x{Y}\\x}\\begin{document}A\\end{document}tail",
 "\\def\\h{Y}\\AtEndDocument{\\h}\\begin{document}A\\end{document}tail",
 "\\AtEndDocument{A}\\AtEndDocument{B}\\begin{document}X\\end{document}tail",
 "\\def\\finish{\\end{document}T}\\begin{document}A\\finish tail",
 "\\def\\finish#1{\\end{document}}\\begin{document}A\\finish{x}tail",
 "\\def\\z{\\end}\\begin{document}A\\z{document}tail",
 "\\def\\a{docu}\\def\\b{ment}\\begin{document}A\\end{\\a\\b}tail",
 "\\def\\finish{\\end{document}}\\begin{document}A\\finish\n\nTAIL",
 "\\def\\finish{\\end{document}}\\begin{document}A\\finish% hi\nTAIL",
 "\\begin{document}A\\end{document}% tail\nX",
 "\\begin{document}A\\end{document}\n\nTAIL",
 "\\let\\finish\\end\\begin{document}A\\finish{document}tail",
 "\\begin{document}A\\expandafter\\end\\expandafter{document}tail"];
let direct=[for(source in cases) check(source)^];
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_tail_foreign_main.tex";
let child="script_tail_foreign_child.tex";
let resources={local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}};
let expanded=profile.expand_with_kernel(input(main,"text")^,kernel,
 {source_name:main,echo_definitions:true,adapters:[],resources:resources},100000)^;
let result=digest.digest(expanded)^;
let rebuilt=reconstruct.reconstruct(expanded)^;
let native=input(main,"tex")^;
let ast=input(main,"latex")^;
{cases:len(direct),failures:[for(case in direct where not case.text_match or (case.issue==null and case.ast_match!=true)) case],
 guarded:[for(case in direct where case.issue!=null) {source:case.source,issue:case.issue,unsupported:case.unsupported}],
 foreign:{issue:result.issue,text_match:rebuilt.text==native.text,ast_match:result.ast==ast}}
