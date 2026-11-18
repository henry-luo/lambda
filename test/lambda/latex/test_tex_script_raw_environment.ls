import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let names=["tikzpicture","verbatim","lstlisting","minted","alltt","Verbatim","BVerbatim","LVerbatim",
    "SaveVerbatim","VerbatimOut","fancyvrb","comment","filecontents","filecontents*","luacode","luacode*"];
let direct=[for(name in names) "\\def\\x{EXPANDED}\\begin{document}A\\begin{" ++ name ++
    "}[o]{f}é文 \\x{A}% raw\n\\end{" ++ name ++ "}B\\x\\end{document}"];
let edge=["\\let\\go\\begin\\begin{document}\\go{verbatim}\\bad%comment\n\\end{verbatim}X\\end{document}",
    "\\AtBeginDocument{\\def\\x{A}}\\begin{document}\\begin{verbatim}\\x\\end{verbatim}\\x\\end{document}",
    "\\def\\m{\\begin{tikzpicture}}\\begin{document}\\m X\\end{tikzpicture}Y\\end{document}",
    "\\begin{document}A\\begin{verbatim}é文%X\n\\end{verbatim}B\\end{document}",
    "\\begin{document}\\begin{tikzpicture}one\\end{tikzpicture}X\\begin{tikzpicture}two\\end{tikzpicture}Y\\end{document}",
    "\\begin{document}\\begin{tikzpicture}a\\end{verbatim}b\\end{tikzpicture}X\\end{document}",
    "\\begin{document}\\begin{tikzpicture}missing",
    "\\begin{document}\\begin{verbatim}missing   ",
    "\\begin{document}\\begin{verbatim}missing\n",
    "\\begin{document}\\begin{verbatim}missing\r\n",
    "\\begin{document}\\begin{verbatim}missing\n  ",
    "\\def\\name{tikzpicture}\\begin{document}\\begin{\\name}\\textbf{X}\\end{tikzpicture}Z\\end{document}"];
fn compare(source,mode) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,
    adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 {issue:r.issue,text:reconstruct.reconstruct(r)^.text==native.text,
    diagnostics:r.diagnostic_records==native.diagnostics,messages:r.messages==native.messages}
}
let cases=[for(source in [*direct,*edge], mode in [false,true])compare(source,mode)^];
let root="test/lambda/latex/fixtures/local_packages/";
let fixtures=[{main:"script-raw-environment-main.tex",child:"script-raw-environment-child.tex"},
    {main:"script-raw-environment-missing-main.tex",child:"script-raw-environment-missing-child.tex"}];
fn included(fixture,mode) any^ {
 let path=root ++ fixture.main;let child=fixture.child;
 let r=profile.expand_with_kernel(input(path,"text")^,kernel,{echo_definitions:true,constructor_argument_mode:mode,
    source_name:path,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let native=input(path,"tex")^;let parsed=digest.digest(r)^;
 let tree=input(path,"latex") ^ {null};
 let tree_ok=if(tree==null) parsed.issue=="latex-digest-unsupported" else if(mode) parsed.ast==tree
    else parsed.issue=="latex-digest-unsupported" and parsed.unsupported=="command source mapping";
 {issue:r.issue,text:reconstruct.reconstruct(r)^.text==native.text,diagnostics:r.diagnostic_records==native.diagnostics,
    messages:r.messages==native.messages,guard:tree_ok}
}
let foreign=[for(fixture in fixtures,mode in [false,true])included(fixture,mode)^];
{cases:len(cases),ok:all([for(r in cases)r.issue==null and r.text and r.diagnostics and r.messages]),
 foreign:len(foreign),foreign_ok:all([for(r in foreign)r.issue==null and r.text and r.diagnostics and r.messages and r.guard])}
