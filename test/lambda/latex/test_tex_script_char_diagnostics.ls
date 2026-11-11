import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {case:label,issue:r.issue,records:r.diagnostic_records,native_records:native.diagnostics,
  records_match:r.diagnostic_records == native.diagnostics,digest_issue:d.issue,ast_match:if(d.ast == null) null else d.ast == n}
}
report("negative", "\\begin{document}\\char-1\\end{document}")
report("too large", "\\begin{document}\\char1114112\\end{document}")
report("line two", "\\begin{document}\n\\char-42X\\end{document}")
report("two errors", "\\begin{document}\\char-1\\char1114112\\end{document}")
report("mixed supported", "\\begin{document}\\char-1\\errmessage{bad}\\end{document}")
