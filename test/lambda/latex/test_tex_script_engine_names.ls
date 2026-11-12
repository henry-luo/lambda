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
 "\\jobname",
 "\\pdftexrevision",
 "\\fontname\\nullfont",
 "\\fontname\\font",
 "\\font\\my=cmr10 \\fontname\\my",
 "\\font\\my=cmr10 at 12pt \\fontname\\my",
 "\\font\\my=cmr10 at 9pt \\fontname\\my",
 "\\font\\my=cmr10 scaled 1200 \\fontname\\my",
 "\\font\\my=cmr10 \\my\\fontname\\font",
 "\\font\\my=cmr10 \\let\\alias\\my\\fontname\\alias",
 "\\font\\my=cmr10 \\textfont2=\\my \\fontname\\textfont2",
 "\\fontname\\textfont2",
 "\\edef\\x{\\fontname\\nullfont}\\x",
 "\\font\\my=cmr10 at 12pt \\edef\\x{\\fontname\\my}\\x",
 "\\edef\\x{\\jobname}\\x",
 "\\edef\\x{\\pdftexrevision}\\x",
 "\\fontname\\nullfont ABC",
 "\\jobname{}\\pdftexrevision",
 "\\font\\my=cmr10 \\meaning\\my",
 "\\font\\my=cmr10 at 12pt \\meaning\\my",
 "\\fontname\\bad X",
 "\\fontname A",
 "\\fontname",
 "\\fontname\\relax X",
 "\\fontname\\iftrue\\nullfont\\else\\bad\\fi",
 "\\font\\my=cmr10 \\def\\f{\\my}\\fontname\\f",
 "\\font\\my=cmr10 \\textfont3=\\my\\fontname\\textfont3",
 "\\font\\my=cmr10 at 10pt \\fontname\\my",
 "\\font\\my=cmr10 at 10.25pt \\fontname\\my",
 "\\font\\my=cmr10\\relax \\fontname\\my",
 "\\edef\\x{\\fontname\\nullfont}\\edef\\y{\\x}\\y",
 "\\jobname\\jobname\\pdftexrevision",
 "\\def\\x{\\fontname}\\x"
];
[for (source in sources) matches(source)^]
let fixture_root="test/lambda/latex/fixtures/local_packages/";
let fixture_resources={local:{'script_jobchild.tex':{data:input(fixture_root ++ "script_jobchild.tex","text")^}},bundled:{}};
fn file_case(name) any^ {
 let filename=fixture_root ++ name;
 let source=input(filename,"text")^;
 let result=profile.expand_with_kernel(source,kernel,
  {source_name:filename,base_path:filename,echo_definitions:true,adapters:[],resources:fixture_resources},100000)^;
 let rebuilt=reconstruct.reconstruct(result)^;
 let native=input(filename,"tex")^;
 {issue:result.issue,reconstruct_issue:rebuilt.issue,matches:rebuilt.text==native.text,
  diagnostics_match:result.diagnostic_records==native.diagnostics}
}
[for(name in ["script_jobname.tex","script.job.name.tex","script_job_noext"]) file_case(name)^]
fn safe_digestion(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;
 if(parsed.ast!=null) parsed.ast==(parse(source,{type:"latex"})^) else parsed.issue=="latex-digest-unsupported"
}
[for(source in [
 "\\begin{document}A\\jobname B\\end{document}",
 "\\begin{document}A\\textbf{\\jobname}B\\end{document}",
 "\\begin{document}A\\pdftexrevision B\\end{document}",
 "\\begin{document}A\\fontname\\nullfont B\\end{document}"])
 safe_digestion(source)^]

let included_source=input(fixture_root ++ "script_font_end.tex","text")^;
let included_resources={local:{'script_font_child.tex':{data:input(fixture_root ++ "script_font_child.tex","text")^}},bundled:{}};
let included=profile.expand_with_kernel(included_source,kernel,{source_name:fixture_root ++ "script_font_end.tex",
 echo_definitions:true,adapters:[],resources:included_resources},100000)^;
let included_rebuilt=reconstruct.reconstruct(included)^;
let included_native=input(fixture_root ++ "script_font_end.tex","tex")^;
{issue:included.issue,reconstruct_issue:included_rebuilt.issue,matches:included_rebuilt.text==included_native.text,
 diagnostics_match:included.diagnostic_records==included_native.diagnostics}

let source_stream=stream.from_reader({*:reader.reader("A",71),endline:-1});
let first_token=stream.next(source_stream)^;
let exhausted=stream.next(first_token.stream)^;
let child_stream=stream.push_reader(exhausted.stream,{*:reader.reader("B",72),endline:-1});
let child_token=stream.next(child_stream)^;
[stream.active_reader(exhausted.stream)^ == null,
 stream.raw_reader(exhausted.stream,first_token.token)^ == null,
 stream.active_reader(child_token.stream)^.file_id == 72,
 stream.raw_reader(child_token.stream,child_token.token)^.reader.file_id == 72]
