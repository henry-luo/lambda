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
 "\\begin{document}A\\shipout\\hbox{B}C\\end{document}",
 "\\begin{document}A\\shipout\\vbox{B}C\\end{document}",
 "\\begin{document}A\\shipout\\vtop{B}C\\end{document}",
 "\\begin{document}\\shipout\\hbox{}Z\\end{document}",
 "\\count0=1\\begin{document}A\\shipout\\hbox{\\global\\count0=2 B}\\the\\count0\\end{document}",
 "\\count0=1\\begin{document}A\\shipout\\hbox{\\count0=2 B}\\the\\count0\\end{document}",
 "\\everyhbox{X}\\begin{document}\\shipout\\hbox{B}C\\end{document}",
 "\\everyvbox{X}\\begin{document}\\shipout\\vbox{B}C\\end{document}",
 "\\begin{document}\\shipout\\hbox{\\aftergroup X A}B\\end{document}",
 "\\begin{document}\\shipout\\hbox{A\\hbox{B}C}D\\end{document}",
 "\\begin{document}\\shipout\\hbox to 10pt{B}C\\end{document}",
 "\\begin{document}\\shipout\\hbox spread 10pt{B}C\\end{document}",
 "\\let\\s=\\shipout\\begin{document}\\s\\hbox{B}C\\end{document}",
 "\\begin{document}\\shipout\\relax\\hbox{B}C\\end{document}",
 "\\begin{document}\\shipout\\hbox{\\verb|}%|}Z\\end{document}",
 "\\begin{document}\\shipout\\hbox{\\raise1pt\\hbox{B}}Z\\end{document}",
 "\\begin{document}\\hbox{A\\shipout\\hbox{B}C}Z\\end{document}",
 "\\begin{document}$x\\shipout\\hbox{A}y$\\end{document}",
 "\\begin{document}\\shipout\\hbox{é漢}Z\\end{document}",
 "\\begin{document}A\\shipout\\hbox{X \\unskip Y}B\\end{document}",
 "\\begin{document}A\\shipout\\hbox{\\global\\def\\x{X}}\\x\\end{document}"
];
[for (source in sources) matches(source)^]
let native_baseline=parse("",{type:"tex"})^.messages;
fn message_case(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 let messages=slice(native.messages,len(native_baseline),len(native.messages))^;
 {issue:result.issue,messages_match:result.messages==messages,diagnostics_match:result.diagnostic_records==native.diagnostics}
}
[for(source in [
 "\\begin{document}A\\shipout\\hbox{\\message{hello}B}C\\end{document}",
 "\\begin{document}A\\shipout\\hbox{\\immediate\\write16{hello}B}C\\end{document}",
 "\\newlinechar=64\\begin{document}\\shipout\\vbox{\\message{A@B}}C\\end{document}",
 "\\begin{document}\\shipout\\hbox{\\errmessage{bad}}C\\end{document}"])
 message_case(source)^]
fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}A\\shipout\\hbox{B}C\\end{document}",
 "\\def\\x{A}\\shipout\\hbox{B}\\begin{document}\\x\\end{document}",
 "\\begin{document}$x\\shipout\\hbox{A}y$\\end{document}"])
 safe_digestion(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_shipout_main.tex","text")^;
let fixture_resources={local:{'script_shipout_child.tex':{data:input(fixture_root ++ "script_shipout_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{source_name:fixture_root ++ "script_shipout_main.tex",
 echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_shipout_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
