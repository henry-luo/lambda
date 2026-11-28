import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import native_snapshot: ._tex_native_snapshot
let native=input("test/lambda/latex/fixtures/tex_endline_source_native.json","json")^;
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{'stream-data.txt':{data:input(root ++ "script_stream_data.dat","text")^}},bundled:{}};
let options={echo_definitions:true,adapters:[],resources:resources};
let legacy=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:false},100000)^;
let constructor=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:true},100000)^;
fn check(source,case,mode,stream) any^ {
 let expanded=profile.expand_with_kernel(source,kernel,{*:options,source_name:root ++ "document.tex",constructor_argument_mode:mode},100000,if(mode)constructor else legacy)^;
 let built=digest.digest(expanded)^;
 let guarded=stream and not mode and built.issue=="latex-digest-unsupported" and built.unsupported=="input stream command source mapping";
 let matched=built.issue==null and native_snapshot.tree_matches(built.ast,case,source)^;
 {ok:source==case.source and reconstruct.reconstruct(expanded)^.text==case.tex.text and expanded.diagnostic_records==case.tex.diagnostics and (guarded or matched),guarded:guarded,matched:matched}
}
let stream_checks=[for(case in native.stream_cases,mode in [false,true]) {
 let prefix=if(case.code=="negative") "\\endlinechar=-1\\relax" else if(case.code=="space") "\\endlinechar=32\\relax" else "";
 let setup=if(case.stream) "\\openin0=stream-data.txt\\relax\\readline0 to\\sample" else "";
 let body=if(case.stream) "X\\sample" else "X";
 let source=prefix ++ setup ++ "\\begin{document}" ++ body ++ (if(case.newline) "\n" else "") ++ "\\end{document}";
 check(source,case,mode,case.stream)^
}];
let gaps=["A\nB","A\n\\textbf{B}","A\n\nB","A\r\nB","A \n B","A\n\\section{B}","A\n\\begin{quote}B\\end{quote}","A\n\\end{document}","A\n\n\\end{document}","A\r\n\\end{document}","A \n \\end{document}"];
let prefixes=["","\\endlinechar=-1\\relax","\\endlinechar=32\\relax"];
let gap_checks=[for(pi,prefix in prefixes,gi,gap in gaps,mode in [false,true]) {
 let source=prefix ++ "\\begin{document}" ++ gap ++ (if(contains(gap,"\\end{document}")) "" else "\\end{document}");
 check(source,native.gap_cases[pi*len(gaps)+gi],mode,false)^
}];
let extra=["A\n$B$","A\n{B}","A\n\\foo{B}","A\n% comment\nB",
    "A\n\n\\textit{B}","A\n\\begin{quote}B\\end{quote}","A\n\\verb|B|","\\textbf{A\nB}"];
let extra_checks=[for(pi,prefix in ["","\\endlinechar=-1\\relax"],ei,body in extra,mode in [false,true]) {
 let source=prefix ++ "\\begin{document}" ++ body ++ "\\end{document}";
 check(source,native.extra_cases[pi*len(extra)+ei],mode,false)^
}];
let checks=[*stream_checks,*gap_checks,*extra_checks];
{cases:len(checks),matched:len([for(c in checks where c.matched)c]),guarded:len([for(c in checks where c.guarded)c]),failures:[for(c in checks where not c.ok)c]}
