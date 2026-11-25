import compare: ._tex_script_fp_compare
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let expressions=["tan(0)","tan(-0)","tan(1)","tan(-1)","tan(3.141592653589793)",
 "tan(1.570796326794896)","tan(1.570796326794897)","tan(1.570796326794898)",
 "tan(1000)","tan(-1000)","tan(1e10)","tan(1e16)","tan(1e17)","tan(1e18)","tan(1e20)",
 "tan(1e25)","tan(1e30)","tan(1e35)"];
let results=[for(expr in expressions) compare.check(expr)^];
{cases:len(results),failures:[for(result in results where result.issue==null and
 (not result.text_match or not result.messages_match or not result.diagnostics_match)) result],
 guarded:[for(result in results where result.issue!=null) {expr:result.expr,issue:result.issue,unsupported:result.unsupported}]}
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let sources=["\\ExplSyntaxOn\\begin{document}\\fp_eval:n{tan(1)}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\fp_eval:n{tan(-1)}}\\ExplSyntaxOff\\end{document}"];
fn check_tree(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:true,
  echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"tex"})^;
 {source:source,issue:d.issue,text:reconstruct.reconstruct(r)^.text==n.text,
  diagnostics:r.diagnostic_records==n.diagnostics,ast:if(d.ast==null)null else d.ast==parse(source,{type:"latex"})^}
}
let trees=[for(source in sources)check_tree(source)^];
{cases:len(trees),failures:[for(tree in trees where tree.issue!=null or not tree.text or not tree.diagnostics or tree.ast!=true) tree]}
