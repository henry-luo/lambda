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
 "\\pdfuniformdeviate 100",
 "\\pdfsetrandomseed 1\\relax\\pdfuniformdeviate 100",
 "\\pdfnormaldeviate",
 "\\the\\pdfrandomseed",
 "\\pdfresettimer\\the\\pdfelapsedtime",
 "\\pdfuniformdeviate100,\\pdfuniformdeviate100,\\pdfuniformdeviate100",
 "\\pdfsetrandomseed0\\relax\\pdfuniformdeviate100",
 "\\pdfsetrandomseed-1\\relax\\pdfuniformdeviate100",
 "\\pdfsetrandomseed2147483647\\relax\\pdfuniformdeviate100",
 "\\pdfsetrandomseed1\\relax\\the\\pdfrandomseed",
 "\\pdfsetrandomseed-1\\relax\\the\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax\\pdfuniformdeviate0/\\the\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax\\pdfuniformdeviate-5/\\the\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax\\pdfnormaldeviate/\\the\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax{\\pdfuniformdeviate10}\\pdfuniformdeviate10",
 "\\pdfsetrandomseed1\\relax{\\pdfsetrandomseed2}\\the\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax\\count0=\\pdfrandomseed\\the\\count0",
 "\\pdfsetrandomseed1\\relax\\number\\pdfrandomseed",
 "\\pdfsetrandomseed1\\relax\\ifnum\\pdfrandomseed=1 T\\else F\\fi",
 "\\pdfsetrandomseed1\\relax\\edef\\x{\\pdfuniformdeviate10}\\x",
 "\\edef\\x{\\pdfnormaldeviate}\\x",
 "\\edef\\x{\\the\\pdfelapsedtime}\\x",
 "\\pdfsetrandomseed1\\relax\\pdfuniformdeviate\\numexpr3+7\\relax",
 "\\pdfsetrandomseed1\\relax\\count0=10 \\pdfuniformdeviate\\count0",
 "\\pdfsetrandomseed1\\relax\\pdfuniformdeviate\\pdfuniformdeviate100",
 "\\pdfsetrandomseed1\\relax\\pdfuniformdeviate1\\relax\\the\\pdfrandomseed",
 "\\pdfresettimer\\number\\pdfelapsedtime",
 "\\pdfsetrandomseed1\\relax\\pdfresettimer\\the\\pdfrandomseed",
 "\\afterassignment X\\pdfsetrandomseed1\\count0=2 Y",
 "\\edef\\x{\\the\\count0}\\x",
 "\\edef\\x{\\number\\count0}\\x",
 "\\edef\\x{\\the\\tolerance}\\x",
 "\\edef\\x{\\the\\inputlineno}\\x",
 "\\edef\\x{\\the\\eTeXversion}\\x",
 "\\edef\\x{\\the\\dimen0}\\x",
 "\\edef\\x{\\the\\skip0}\\x",
 "\\edef\\x{\\the\\muskip0}\\x",
 "\\edef\\x{\\the\\parindent}\\x",
 "\\edef\\x{\\the\\parskip}\\x",
 "\\edef\\x{\\the\\catcode`A}\\x",
 "\\edef\\x{\\the\\numexpr1+2\\relax}\\x",
 "\\edef\\x{\\the\\dimexpr1pt+2pt\\relax}\\x",
 "\\edef\\x{\\the\\glueexpr1pt plus 2pt\\relax}\\x",
 "\\edef\\x{\\the\\fontdimen2\\nullfont}\\x",
 "\\edef\\x{\\the\\hyphenchar\\nullfont}\\x",
 "\\edef\\x{\\the\\currentgrouplevel}\\x",
 "\\edef\\x{\\the\\interactionmode}\\x",
 "\\edef\\x{\\the\\pagegoal}\\x",
 "\\def\\n{0}\\edef\\x{\\the\\count\\n}\\x",
 "\\edef\\x{\\the\\count0}\\edef\\y{\\x}\\y",
 "\\edef\\x{\\the\\dimen0}\\edef\\y{\\x}\\y",
 "\\pdfsetrandomseed1\\relax\\edef\\x{\\the\\pdfrandomseed}\\x",
 "\\toks0={A}\\edef\\x{\\the\\toks0}\\x",
 "\\toks0={\\foo}\\edef\\x{\\the\\toks0}\\x",
 "\\toks0={A{B}}\\edef\\x{\\the\\toks0}\\x",
 "\\def\\x{A}\\toks0={\\x}\\edef\\z{\\the\\toks0}\\def\\x{B}\\z",
 "\\toks0={A}\\edef\\x{\\the\\toks0}\\edef\\y{\\x}\\y",
 "\\global\\edef\\x{\\the\\count0}\\x",
 "\\long\\edef\\x{\\the\\dimen0}\\x",
 "\\protected\\edef\\x{\\the\\count0}\\meaning\\x",
 "\\toks0={A}\\global\\edef\\x{\\the\\toks0}\\x"
];
[for (source in sources) matches(source)^]
fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}A\\pdfsetrandomseed1\\pdfuniformdeviate10 B\\end{document}",
 "\\begin{document}A\\pdfresettimer\\the\\pdfelapsedtime B\\end{document}",
 "\\edef\\x{\\the\\count0}\\begin{document}\\x\\end{document}",
 "\\toks0={A}\\edef\\x{\\the\\toks0}\\begin{document}\\x\\end{document}",
 "\\begin{document}\\textbf{\\the\\pdfrandomseed}\\end{document}"])
 safe_digestion(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_source=input(fixture_root ++ "script_random_main.tex","text")^;
let fixture_resources={local:{'script_random_child.tex':{data:input(fixture_root ++ "script_random_child.tex","text")^}},bundled:{}};
let fixture=profile.expand_with_kernel(fixture_source,kernel,{source_name:fixture_root ++ "script_random_main.tex",
 echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
let fixture_rebuilt=reconstruct.reconstruct(fixture)^;
let fixture_native=input(fixture_root ++ "script_random_main.tex","tex")^;
{issue:fixture.issue,reconstruct_issue:fixture_rebuilt.issue,matches:fixture_rebuilt.text==fixture_native.text,
 diagnostics_match:fixture.diagnostic_records==fixture_native.diagnostics}
