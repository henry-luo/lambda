import profile: lambda.doc.latex.tex_engine.profile
import compare: ._tex_script_recovery
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let expressions=["round(2.5,0,0)","round(2.5,0,1)","round(2.5,0,-1)",
 "round(-2.5,0,0)","round(-2.5,0,1)","round(-2.5,0,-1)","round(3.5,0,1)",
 "round(3.5,0,-1)","round(2.4,0,1)","round(2.6,0,-1)","round(2.5,0,1.5)",
 "round(1.25,1,1)","round(-1.25,1,1)","round(1250,-2,-1)","round(-1250,-2,-1)",
 "round(0.5,0,-1)","round(-0.5,0,1)","round(1.2345,1.5)"];

// Keep native comparisons independent of the migrated input route.
let native=input("test/lambda/latex/fixtures/tex_expl3_round_native.json","json")^;
let cached=compare.preload_expl3({echo_definitions:false})^;
fn check(expr,index) any^ {
 let source="\\ExplSyntaxOn\\fp_eval:n{" ++ expr ++ "}\\ExplSyntaxOff";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,adapters:[],resources:{local:{},bundled:{}}},100000,if(index==0)null else cached)^;
 let n=native.cases[index];
 {expr:expr,issue:r.issue,unsupported:r.unsupported,
  text_match:if(r.issue!=null)null else source==n.source and reconstruct.reconstruct(r)^.text==n.tex.text,
  diagnostics_match:if(r.issue!=null)null else r.diagnostic_records==n.tex.diagnostics}
}
let results=[for(index,e in expressions)check(e,index)^];
{cases:len(results),failures:[for(r in results where r.issue==null and (not r.text_match or not r.diagnostics_match)) r],
 guarded:[for(r in results where r.issue!=null) {expr:r.expr,issue:r.issue,unsupported:r.unsupported}]}
let sources=["\\ExplSyntaxOn\\begin{document}\\fp_eval:n{round(2.5,0,1)}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\fp_eval:n{round(-2.5,0,-1)}}\\ExplSyntaxOff\\end{document}"];

fn check_tree(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:true,
  echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"tex"})^;let tree=parse(source,{type:"latex"})^;
 {source:source,issue:d.issue,unsupported:d.unsupported,text_match:reconstruct.reconstruct(r)^.text==n.text,
  diagnostics_match:r.diagnostic_records==n.diagnostics,ast_match:if(d.ast==null)null else d.ast==tree}
}
let trees=[for(s in sources)check_tree(s)^];
{cases:len(trees),failures:[for(t in trees where t.issue!=null or not t.text_match or not t.diagnostics_match or not t.ast_match) t]}
