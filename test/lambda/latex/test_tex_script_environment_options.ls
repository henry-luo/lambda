import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{'script_list_option_child.tex':{data:input(root ++ "script_list_option_child.tex","text")^}},bundled:{}};
fn expanded(source,name) any^ => profile.expand_with_kernel(source,kernel,
 {source_name:name,echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:resources},100000)
fn check(body) any^ {
 let source="\\begin{document}A" ++ body ++ "Z\\end{document}";
 let result=digest.digest(expanded(source,"<direct>")^)^;
 let native=parse(source,{type:"latex"})^;
 {body:body,passed:result.issue==null and result.ast==native,issue:result.issue,unsupported:result.unsupported}
}
let cases=[
 "\\begin{quote}[a]x\\end{quote}",
 "\\begin{unknown}[a[b]c]x\\end{unknown}",
 "\\begin{itemize}[a]\\item x\\end{itemize}",
 "\\begin{enumerate}[]x\\end{enumerate}",
 "\\begin{description}   [a]x\\end{description}",
 "\\begin{itemize}\n\n[a]x\\end{itemize}",
 "\\begin{itemize}[a[b]c]x\\end{itemize}",
 "\\begin{itemize}[a\\]b]x\\end{itemize}",
 "\\begin{itemize}[a%]comment\nb]x\\end{itemize}",
 "\\begin{itemize}%comment\n[a]x\\end{itemize}",
 "\\begin{itemize}[é中]x\\end{itemize}",
 "\\def\\x{a}\\begin{itemize}[\\x]x\\end{itemize}",
 "\\def\\x{[a]}\\begin{itemize}\\x x\\end{itemize}",
 "\\begin{itemize}[a]\\item x\\begin{enumerate}[b]\\item y\\end{enumerate}\\end{itemize}",
 "\\catcode`\\[=1 \\catcode`\\]=2 \\begin{itemize}[a]x\\end{itemize}",
 "\\catcode`\\[=13 \\begin{itemize}[a]x\\end{itemize}",
 "\\def\\x{]}\\begin{itemize}[a\\x x\\end{itemize}",
 "\\begin{itemize}[a\\relax b]x\\end{itemize}",
 "\\begin{itemize}\\end{itemize}",
 "\\begin{itemize}\\def\\x{Q}x\\end{itemize}",
 "\\begin{itemize}" ++ chr(11) ++ "[a]x\\end{itemize}",
 "\\begin{itemize}" ++ chr(12) ++ "[a]x\\end{itemize}",
 "\\catcode`\\%=13 \\begin{itemize}[a%b]ignored\n c]x\\end{itemize}"
];
let checked=[for(body in cases) check(body)^];
let guarded=check("\\begin{itemize}[a\\noexpand\\foo b]x\\end{itemize}")^;
let boundary_source="\\begin{document}A\\catcode`\\]=11 \\def\\x{\\foo]bar} \\catcode`\\]=12 \\begin{itemize}[a\\x]z\\end{itemize}\\end{document}";
let boundary=digest.digest(expanded(boundary_source,"<boundary>")^)^;
let path=root ++ "script_list_option_main.tex";
let included=digest.digest(expanded(input(path,"text")^,path)^)^;
{cases:len(checked),failures:[for(case in checked where not case.passed) case],
 guarded:{issue:guarded.issue,unsupported:guarded.unsupported},
 token_boundary:{issue:boundary.issue,unsupported:boundary.unsupported},
 included:{issue:included.issue,matches_native:included.ast==input(path,"latex")^}}
