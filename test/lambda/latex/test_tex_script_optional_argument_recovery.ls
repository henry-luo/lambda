import checks: .mod_macro_recovery_checks
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
// optional arguments diagnose failed scans with native's synthetic control name.
[for(source in [
 "\\newcommand{\\m}[1][D]{(#1)}\\m[", "\\newcommand{\\m}[1][D]{(#1)}\\m[X",
 "\\newcommand{\\m}[1][D]{(#1)}\\m[X}", "\\newcommand{\\m}[1][D]{(#1)}\\m[X}Y",
 "\\newcommand{\\m}[1][D]{(#1)}\\m[{X", "\\newcommand{\\m}[1][D]{(#1)}\\m[{X}]Y",
 "\\newcommand{\\m}[2][D]{(#1/#2)}\\m[X}Y",
 "\\newcommand{\\m}[1][D]{(#1)}\\let\\n\\m\\n[X}Y",
 "\\newcommand*{\\m}[1][D]{(#1)}\\m[X}Y",
 "\\newenvironment{mine}[1][D]{(#1)}{B}\\begin{mine}[X}Y\\end{mine}",
 "\\newenvironment{mine}[1][D]{(#1)}{B}\\begin{mine}[X",
 "\\newenvironment{mine}[1][D]{(#1)}{B}\\begin{a}{\\begin{mine}[Q}Y\\end{mine}}\\end{a}",
 "\\newcommand{\\m}[1][D]{(#1)}\\begin{document}\\textbf{\\m[X}Y}\\end{document}"
],mode in [false,true]) checks.check(source,false,mode)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-optional-main.tex";
let child=input(root ++ "script-optional-child.tex","text")^;
let script=checks.expand(input(path,"text")^,{source_name:path,echo_definitions:true,adapters:[],
    resources:{local:{'script-optional-child.tex':{data:child}},bundled:{}}})^;
let native=input(path,"tex")^;
{issue:script.issue,text_match:reconstruct.reconstruct(script)^.text==native.text,
    diagnostics_match:script.diagnostic_records==native.diagnostics}
