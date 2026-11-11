import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let d=digest.digest(r)^;let native=parse(source,{type:"latex"})^;
 {case:label,issue:r.issue,records:r.diagnostic_records,native_records:n.diagnostics,records_match:r.diagnostic_records == n.diagnostics,
  digest_issue:d.issue,ast_match:if(d.ast == null) null else d.ast == native}
}
report("simple", "\\begin{document}A\\errmessage{bad}B\\end{document}")
report("multiline", "\\begin{document}\nA\\errmessage{bad}B\\end{document}")
report("expanded", "\\def\\x{oops}\\begin{document}\\errmessage{\\x}X\\end{document}")
report("two errors", "\\begin{document}\\errmessage{one}X\\errmessage{two}\\end{document}")
report("unicode message", "\\begin{document}\\errmessage{λ漢}X\\end{document}")
report("no endline", "\\endlinechar=-1\\begin{document}\\errmessage{bad}X\\end{document}")
report("pseudo file", "\\begin{document}\\scantokens{\\errmessage{pseudo}}X\\end{document}")
report("mixed diagnostic pending", "\\begin{document}\\newcounter{n}\\setcounter{n}{1\\relax A}\\errmessage{bad}\\end{document}")
report("long message pending", "\\begin{document}\\errmessage{" ++ join([for(i in 1 to 512) "x"], "") ++ "}\\end{document}")
report("empty message", "\\begin{document}\\errmessage{}X\\end{document}")
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_error.tex","text")^;
let fixture_style=input(fixture_root ++ "script_error.sty","text")^;
let x=profile.expand_with_kernel(fixture_source,kernel,{adapters:[],resources:{local:{'script_error.sty':{data:fixture_style}},bundled:{}}},100000)^;
let d=digest.digest(x)^;let n=input(fixture_root ++ "script_error.tex","tex")^;
let nl=input(fixture_root ++ "script_error.tex","latex")^;
"local package:"; {issue:x.issue,records:x.diagnostic_records,native_records:n.diagnostics,records_match:x.diagnostic_records == n.diagnostics,root_records:if(d.ast == null) null else d.ast.tex_diagnostics,ast_match:if(d.ast == null) null else d.ast == nl}
