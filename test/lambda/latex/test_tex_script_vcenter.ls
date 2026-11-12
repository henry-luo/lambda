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
 "\\begin{document}$a\\vcenter{b}c$\\end{document}",
 "\\begin{document}$a\\vcenter to 2pt{b}c$\\end{document}",
 "\\begin{document}$a\\vcenter spread 2pt{b}c$\\end{document}",
 "\\begin{document}A\\vcenter{B}C\\end{document}",
 "\\begin{document}\\vcenter{}Z\\end{document}",
 "\\begin{document}\\vcenter{A{B}C}Z\\end{document}",
 "\\dimen0=2pt\\begin{document}\\vcenter to\\dimen0{A}Z\\end{document}",
 "\\def\\d{2pt}\\begin{document}\\vcenter spread\\d{A}Z\\end{document}",
 "\\let\\v=\\vcenter\\begin{document}\\v{A}Z\\end{document}",
 "\\everyvbox{X}\\begin{document}\\vcenter{A}Z\\end{document}",
 "\\everyhbox{X}\\begin{document}\\vcenter{\\hbox{A}}Z\\end{document}",
 "\\count0=1\\begin{document}\\vcenter{\\count0=2 A}\\the\\count0\\end{document}",
 "\\count0=1\\begin{document}\\vcenter{\\global\\count0=2 A}\\the\\count0\\end{document}",
 "\\begin{document}\\vcenter{\\aftergroup X A}B\\end{document}",
 "\\begin{document}\\vcenter{A\\vcenter{B}C}Z\\end{document}",
 "\\begin{document}\\vcenter{A\\raise1pt\\hbox{B}C}Z\\end{document}",
 "\\begin{document}\\vcenter{\\verb|}%|}Z\\end{document}",
 "\\begin{document}$\\vcenter{\\hbox{é}}$\\end{document}",
 "\\begin{document}\\vcenter\\relax{A}Z\\end{document}",
 "\\begin{document}\\hbox{A\\vcenter{B}C}Z\\end{document}",
 "\\begin{document}$a\\vcenter{\\frac{1}{2}}b$\\end{document}",
 "\\begin{document}\\shipout\\hbox{\\vcenter{A}}Z\\end{document}",
 "\\begin{document}\\vcenter{A\\shipout\\hbox{B}C}Z\\end{document}",
 "\\begin{document}\\raise1pt\\hbox{A\\vcenter{B}C}Z\\end{document}",
 "\\begin{document}\\vcenter{A\\moveleft1pt\\hbox{B}C}Z\\end{document}",
 "\\begin{document}\\vcenter to 1pt{A}\\vcenter spread 2pt{B}Z\\end{document}"
];
[for (source in sources) matches(source)^]

fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}$a\\vcenter{b}c$\\end{document}",
 "\\begin{document}A\\vcenter{B}C\\end{document}",
 "\\def\\x{A}\\vcenter{B}\\begin{document}\\x\\end{document}"])
 safe_digestion(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_vcenter_main.tex","text")^;
let fixture_resources={local:{'script_vcenter_child.tex':{data:input(fixture_root ++ "script_vcenter_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{source_name:fixture_root ++ "script_vcenter_main.tex",
 echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_vcenter_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
