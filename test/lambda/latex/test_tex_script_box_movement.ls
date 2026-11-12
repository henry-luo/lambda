import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
fn matches(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let rebuilt = reconstruct.reconstruct(result)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, reconstruct_issue: rebuilt.issue, matches: rebuilt.text == native.text,
        diagnostics_match: result.diagnostic_records == native.diagnostics, messages_empty: len(result.messages) == 0}
}
let sources = [
 "\\begin{document}A\\raise1pt\\hbox{B}C\\end{document}",
 "\\begin{document}A\\lower2pt\\hbox{B}C\\end{document}",
 "\\begin{document}A\\moveleft1pt\\vbox{B}C\\end{document}",
 "\\begin{document}A\\moveright1pt\\hbox{B}C\\end{document}",
 "\\begin{document}\\raise0pt\\hbox{}\\end{document}",
 "\\begin{document}\\raise-2pt\\hbox{A}\\end{document}",
 "\\begin{document}\\lower-2pt\\hbox{A}\\end{document}",
 "\\begin{document}\\raise1.5pt\\vbox{A}\\end{document}",
 "\\begin{document}\\raise1pt\\vtop{A}\\end{document}",
 "\\begin{document}\\moveleft1pt\\hbox{A}\\end{document}",
 "\\begin{document}\\moveright1pt\\vtop{A}\\end{document}",
 "\\begin{document}\\raise1pt\\hbox to 5pt{A}\\end{document}",
 "\\begin{document}\\raise1pt\\hbox spread 5pt{A}\\end{document}",
 "\\dimen0=2pt\\begin{document}\\raise\\dimen0\\hbox{A}\\end{document}",
 "\\begin{document}\\raise\\dimexpr1pt+2pt\\relax\\hbox{A}\\end{document}",
 "\\def\\d{1pt}\\begin{document}\\lower\\d\\hbox{A}\\end{document}",
 "\\let\\shift=\\raise\\begin{document}\\shift1pt\\hbox{A}\\end{document}",
 "\\everyhbox{X}\\begin{document}\\raise1pt\\hbox{A}\\end{document}",
 "\\everyvbox{Y}\\begin{document}\\moveleft1pt\\vbox{A}\\end{document}",
 "\\count0=1\\begin{document}\\raise1pt\\hbox{\\count0=2 A}\\the\\count0\\end{document}",
 "\\count0=1\\begin{document}\\raise1pt\\hbox{\\global\\count0=2 A}\\the\\count0\\end{document}",
 "\\begin{document}\\raise1pt\\relax\\hbox{A}\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{A\\hbox{B}C}\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{é}Z\\end{document}",
 "\\begin{document}A\\raise1pt\\hbox{B}Z\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{\\aftergroup X A}B\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{A\\raise2pt\\hbox{B}C}D\\end{document}",
 "\\begin{document}\\moveleft1pt\\hbox{A\\raise2pt\\hbox{B}C}D\\end{document}",
 "\\begin{document}\\hbox{A\\moveleft2pt\\vbox{B}C}D\\end{document}",
 "\\begin{document}\\lower-0.5pt\\hbox{A}\\end{document}",
 "\\begin{document}$x\\raise1pt\\hbox{A}y$\\end{document}",
 "\\everyhbox{X}\\begin{document}\\moveright1pt\\hbox{A}B\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{A{BC}D}Z\\end{document}",
 "\\begin{document}\\moveleft1pt\\hbox{}Z\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{\\verb|}%|}Z\\end{document}"
];
[for (source in sources) matches(source)^]

fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}A\\raise1pt\\hbox{B}C\\end{document}",
 "\\def\\x{Hi}\\moveleft1pt\\hbox{A}\\begin{document}\\x\\end{document}",
 "\\begin{document}$x\\lower1pt\\hbox{A}y$\\end{document}"])
 safe_digestion(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_move_main.tex","text")^;
let fixture_resources={local:{'script_move_child.tex':{data:input(fixture_root ++ "script_move_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{source_name:fixture_root ++ "script_move_main.tex",
 echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_move_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
