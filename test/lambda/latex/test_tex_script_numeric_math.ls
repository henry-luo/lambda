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
 "\\begin{document}$a\\mathchar\"7161 b$\\end{document}",
 "\\begin{document}$a\\delimiter\"123456 b$\\end{document}",
 "\\begin{document}$a\\radical\"123456 b$\\end{document}",
 "\\begin{document}$a\\mathaccent\"1234 b$\\end{document}",
 "\\begin{document}A\\mathchar65 B\\end{document}",
 "\\begin{document}A\\mathchar0 B\\end{document}",
 "\\begin{document}A\\mathchar-1 B\\end{document}",
 "\\count0=65\\begin{document}A\\mathchar\\count0 B\\end{document}",
 "\\begin{document}A\\mathchar\\numexpr60+5\\relax B\\end{document}",
 "\\def\\n{65}\\begin{document}A\\mathchar\\n B\\end{document}",
 "\\let\\m=\\mathchar\\begin{document}A\\m65 B\\end{document}",
 "\\begin{document}A{\\mathchar65 B}C\\end{document}",
 "\\begin{document}$a\\mathchar65\\mathchar66 b$\\end{document}",
 "\\begin{document}$a\\radical0{b}c$\\end{document}",
 "\\begin{document}$a\\mathaccent0{b}c$\\end{document}",
 "\\begin{document}é\\mathchar65 字\\end{document}",
 "\\begin{document}$\\mathchar`A B$\\end{document}",
 "\\begin{document}$\\mathchar\\eTeXversion B$\\end{document}",
 "\\begin{document}\\iftrue\\mathchar65\\else Z\\fi B\\end{document}",
 "\\def\\x{\\mathchar65}\\begin{document}\\x B\\end{document}",
 "\\edef\\x{\\mathchar65}\\begin{document}\\x B\\end{document}",
 "\\begin{document}A\\mathchar65\\relax B\\end{document}",
 "\\begin{document}A\\delimiter0 B\\radical0 C\\mathaccent0 D\\end{document}",
 "\\begin{document}$a\\mathchar\\numexpr1+2\\relax b$\\end{document}",
 "\\mathchar65",
 "\\delimiter0",
 "\\radical65",
 "\\mathaccent0",
 "\\def\\x{\\mathchar65}\\x",
 "\\mathchar65\\mathchar66",
 "\\begin{document}A\\vcenter{$\\mathchar65 B$}C\\end{document}",
 "\\begin{document}$\\vcenter{\\mathchar65 B}$\\end{document}"
];
[for (source in sources) matches(source)^]

fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}$a\\mathchar65 b$\\end{document}",
 "\\begin{document}A\\radical0 B\\end{document}",
 "\\def\\x{Hi}\\mathchar65\\begin{document}\\x\\end{document}"])
 safe_digestion(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_numeric_math_main.tex","text")^;
let fixture_resources={local:{'script_numeric_math_child.tex':{data:input(fixture_root ++ "script_numeric_math_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{source_name:fixture_root ++ "script_numeric_math_main.tex",
 echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_numeric_math_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
