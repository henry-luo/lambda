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
 "\\begin{document}A\\verb|abc|Z\\end{document}",
 "\\begin{document}A\\verb|a_%\\\\ b|Z\\end{document}",
 "\\begin{document}A\\verb*+x y+Z\\end{document}",
 "\\begin{document}\\verb|{}[]$#&^~%|\\end{document}",
 "\\begin{document}A\\verb||B\\end{document}",
 "\\begin{document}A\\verb#abc#Z\\end{document}",
 "\\begin{document}A\\verb%abc%Z\\end{document}",
 "\\begin{document}A\\verb|é漢字|Z\\end{document}",
 "\\begin{document}A\\textbf{X\\verb|{}%|Y}Z\\end{document}",
 "\\begin{document}A\\verb|abc|\\verb+def+Z\\end{document}",
 "\\def\\x{Z}\\begin{document}\\verb|\\x|\\x\\end{document}",
 "\\begin{document}A\\verb|a b|  Z\\end{document}",
 "\\begin{document}A\\verb|A|\\textbf{Z}\\end{document}",
 "\\begin{document}A\\verb+abc+%hi\nB\\end{document}",
 "\\begin{document}A\\verb|x|\r\nB\\end{document}",
 "\\let\\v=\\verb\\begin{document}A\\v|é_%|Z\\end{document}"
];
[for (source in sources) matches(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_verb_main.tex","text")^;
let fixture_resources={local:{'script_verb_child.tex':{data:input(fixture_root ++ "script_verb_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_verb_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
fn bounded(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 {issue:result.issue,unsupported:result.unsupported}
}
[for(source in [
 "\\begin{document}\\verb|unterminated\nX\\end{document}",
 "\\begin{document}\\verbéxé\\end{document}",
 "\\def\\x{\\verb|ABC|}\\begin{document}\\x\\end{document}",
 "\\begin{document}\\lstinline|x|\\end{document}"])
 bounded(source)^]
let document=profile.expand_with_kernel("\\begin{document}\\verb|x|\\end{document}",kernel,
 {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let ast=digest.digest(document)^;
{issue:ast.issue,unsupported:ast.unsupported}
fn definition_spans(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let rebuilt=reconstruct.reconstruct(result)^;
 len(rebuilt.definition_spans)
}
[definition_spans("\\begin{document}\\verb|x|\\end{document}")^,
 definition_spans("\\def\\x{X}\\begin{document}\\verb|x|\\end{document}")^]
