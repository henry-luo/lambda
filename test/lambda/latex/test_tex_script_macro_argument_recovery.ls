import checks: .mod_macro_recovery_checks
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
[for(source in [
 "\\def\\m#1{X}\\m", "\\def\\m#1{X}\\m   ", "\\def\\m#1{X}\\m{A",
 "\\def\\m#1{X}\\m{A  ", "\\def\\m#1{X}\\m{A\n", "\\def\\m#1{X}\\m{A\r\n",
 "\\def\\m#1;{X}\\m A", "\\def\\m#1;{X}\\m A  ", "\\def\\m#1;{X}\\m A\n",
 "\\def\\m#1{X}\\m\n\nB", "\\def\\m#1{X}\\m\\par B",
 "\\def\\m#1;{X}\\m A\\par B", "\\def\\m#1{X}\\m{A\n\nB}C",
 "\\long\\def\\m#1{X}\\m\n\nB", "\\def\\m#1{X}\\m}B",
 "\\def\\m#1;{X}\\m A}B", "\\def\\m#1#2{<#1/#2>}\\m A",
 "\\newcommand{\\m}[1]{X}\\m\n\nB", "\\newcommand*{\\m}[1]{X}\\m\n\nB",
 "\\def\\m#1{X}\\let\\alias\\m\\alias}B", "\\catcode`~=13 \\def~#1{X}~}B",
 "é\\def\\m#1{X}\\m{A", "\\def\\m##{<#1>}\\m A#",
 "\\def\\f#1{[#1]}\\f{a\\par b}",
 "\\newcommand*\\x[1]{#1}\\x{a\\par b}", "\\def\\f#1{#1}\\edef\\x{\\f}"
]) checks.check(source)^];
[for(source in [
 "\\begin{document}\\def\\m#1{X}A\\m\n\nB\\end{document}",
 "\\begin{document}\\def\\m#1{X}A\\m\\par B\\end{document}",
 "\\begin{document}\\def\\m#1;{X}A\\m C\\par B\\end{document}"
]) checks.check(source,true)^];
[for(source in [
 "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_set:Nn}\\end{document}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\let\\q\\tl_set:Nn\\begin{document}\\textbf{\\q}\\end{document}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\let\\q\\tl_set:Nn\\begin{document}\\textbf{\\q\\l_x_tl}\\end{document}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\let\\q\\tl_set:Nn\\begin{document}\\textbf{\\q\\l_x_tl{A}}\\end{document}\\ExplSyntaxOff"
]) checks.check(source,false,true,true)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-runaway-main.tex";
let child=input(root ++ "script-runaway-child.tex","text")^;
let script=checks.expand(input(path,"text")^,{source_name:path,echo_definitions:true,adapters:[],
    resources:{local:{'script-runaway-child.tex':{data:child}},bundled:{}}})^;
let native=input(path,"tex")^;
let rebuilt=reconstruct.reconstruct(script)^;
{issue:script.issue,text_match:rebuilt.text==native.text,diagnostics_match:script.diagnostic_records==native.diagnostics}
fn long_name(name,via_csname=false) any^ {
    let target=if(via_csname) "\\csname " ++ name ++ "\\endcsname" else "\\" ++ name;
    let definition=if(via_csname) "\\expandafter\\def" ++ target else "\\def" ++ target;
    let source=definition ++ "#1{X}" ++ target ++ "{A";
    let script=checks.expand(source,options)^;
    let native=parse(source,{type:"tex"})^;
    let rebuilt=reconstruct.reconstruct(script)^;
    {issue:script.issue,text_match:rebuilt.text==native.text,
        diagnostics_match:script.diagnostic_records==native.diagnostics,
        message_bytes_match:if(len(script.diagnostic_records)==0 or len(native.diagnostics)==0)null
            else binary(script.diagnostic_records[0].message)^ == binary(native.diagnostics[0].message)^}
}
[for(n in [101,126,127,200]) long_name(join([for(i in 1 to n) "a"],""))^];
long_name(join([for(i in 1 to 125) "a"],"") ++ "é",true)^
