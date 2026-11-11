import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let pseudo=[for(s in r.sources where s.name == "\\scantokens") {id:s.id,data:s.data}];
 let tokens=[for(t in r.output where any([for(s in pseudo) s.id == t.file])) {kind:t.kind,value:t.value,file:t.file,start:t.start,end:t.end}];
 {case:label,issue:r.issue,sources:pseudo,tokens:tokens}
}
report("formatting", "\\begin{document}\\scantokens{\\textbf{Hi}}\\end{document}")
report("multiple", "\\scantokens{A}\\scantokens{B}")
report("nested", "\\scantokens{\\scantokens{X}}")
report("unicode", "\\scantokens{λ漢}")
