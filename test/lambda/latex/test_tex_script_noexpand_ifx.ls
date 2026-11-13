import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {source:source,issue:r.issue,unsupported:r.unsupported,reconstruct_issue:b.issue,text_match:if(r.issue==null)b.text==n.text else null,
 messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\x T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\relax T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand\\relax\\relax T\\else F\\fi",
 "\\def\\x{A}\\let\\y\\x\\expandafter\\ifx\\noexpand\\y\\x T\\else F\\fi",
 "\\def\\x{A}\\let\\y\\relax\\expandafter\\ifx\\noexpand\\x\\y T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand\\missing\\relax T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand\\missing\\missing T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand a a T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand a b T\\else F\\fi",
 "\\expandafter\\ifx\\noexpand\\the\\relax T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\expandafter\\relax\\noexpand\\x T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\expandafter\\x\\noexpand\\x T\\else F\\fi",
 "\\catcode`\\~=13\\def~{A}\\expandafter\\ifx\\noexpand~\\relax T\\else F\\fi",
 "\\catcode`\\~=13\\def~{A}\\expandafter\\ifx\\noexpand~~ T\\else F\\fi",
 "\\def\\x{A}\\iftrue\\expandafter\\ifx\\noexpand\\x\\relax T\\else F\\fi\\else N\\fi",
 "\\def\\x{A}\\ifx\\x\\x T\\else F\\fi",
 "\\def\\x{A}\\ifx\\x\\relax T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\x T\\else F\\fi X",
 "\\newcount\\n\\expandafter\\ifx\\noexpand\\n\\n T\\else F\\fi",
 "\\newcount\\n\\expandafter\\ifx\\noexpand\\n\\relax T\\else F\\fi",
 "\\newdimen\\d\\expandafter\\ifx\\noexpand\\d\\d T\\else F\\fi",
 "\\newtoks\\t\\expandafter\\ifx\\noexpand\\t\\t T\\else F\\fi",
 "\\newskip\\s\\expandafter\\ifx\\noexpand\\s\\s T\\else F\\fi",
 "\\chardef\\c=65\\relax\\expandafter\\ifx\\noexpand\\c\\c T\\else F\\fi",
 "\\mathchardef\\c=65\\relax\\expandafter\\ifx\\noexpand\\c\\c T\\else F\\fi",
 "\\newcount\\n\\n=123\\relax\\number\\noexpand\\n"
]) check(source)^]

fn ast(body) any^ {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (body in [
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\x T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\relax T\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\relax\\textbf{T}\\else F\\fi",
 "\\def\\x{A}\\expandafter\\ifx\\noexpand\\x\\x T\\else\\section{F}\\fi X"
]) ast(body)^]
