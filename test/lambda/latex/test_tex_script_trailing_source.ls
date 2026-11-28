import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
// Keep the retired native trees independent of the current Script parser.
let native=input("test/lambda/latex/fixtures/tex_trailing_source_native.json","json")^;
fn check(source, reference) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let same=if(d.ast==null) source==reference.source else native_snapshot.tree_matches(d.ast,reference,source)^;
 {issue:if(source==reference.source) d.issue else "snapshot-source-mismatch",
  unsupported:d.unsupported,matches:if(d.ast==null)null else same,
  actual:if(d.ast==null or same)null else d.ast,native:if(d.ast==null or same)null else reference.latex}
}
[for (index,source in [
 "\\begin{document}X\\end{document}",
 "\\begin{document}X\\end{document}tail",
 "\\begin{document}X\\end{document} ",
 "\\begin{document}X\\end{document}\n",
 "\\begin{document}X\\end{document}\n\n",
 "\\begin{document}X\\end{document}%comment\n",
 "\\ExplSyntaxOn\\begin{document}X\\end{document}\\ExplSyntaxOff"
]) check(source,native.cases[index])^]

fn check_tail(tail, reference) any^ { check("\\begin{document}X\\end{document}" ++ tail,reference) }
[for (index,tail in ["", "a", "a b", " ", "\n", "\n\n", "\n\n\n", "a\n\nb", "a\n\n\nb", " \n\nx", "x\n\n ",
 "a\r\nb", "a\r\n\r\nb", "a\n  \nb", " \t  ", "é Ω 🙃", "[a]& #_^~", "!?:;", "a\n\nb\n\nc", "\n\n\n\n"])
 check_tail(tail,native.cases[index+7])^]
