import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let native_refs=input("test/lambda/latex/fixtures/tex_errmessage_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let references=[for(item in native_refs.cases where item.label==label)item];
 let reference=if(len(references)==1) references[0] else null;
 let n=reference.tex;
 let d=digest.digest(r)^;
 {case:label,issue:r.issue,records:r.diagnostic_records,native_records:n.diagnostics,records_match:source==reference.source and r.diagnostic_records == n.diagnostics,
  digest_issue:d.issue,ast_match:if(d.ast == null) null else native_snapshot.tree_matches(d.ast,reference,source)^}
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
let d=digest.digest(x)^;let reference=native_refs.local;let n=reference.tex;
"local package:"; {issue:x.issue,records:x.diagnostic_records,native_records:n.diagnostics,records_match:fixture_root ++ "script_error.tex"==reference.source and fixture_source==reference.source_text and fixture_style==reference.style_text and x.diagnostic_records == n.diagnostics,root_records:if(d.ast == null) null else d.ast.tex_diagnostics,ast_match:if(d.ast == null) null else native_snapshot.tree_matches(d.ast,reference,fixture_root ++ "script_error.tex")^}
