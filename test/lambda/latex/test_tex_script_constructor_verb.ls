import checks: .mod_macro_recovery_checks
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
// verbatim delimiters may contain braces without changing the argument depth.
[for(source in [
 "\\textbf{A\\verb|x|B}", "\\textbf{A\\verb|}|B}",
 "\\textbf{A\\verb|{|B}", "\\textbf{A\\verb*|x y|B}",
 "\\foo[A\\verb|}|B]z", "\\begin{a}{A\\verb|}|B}\\end{a}",
 "\\newenvironment{mine}{A}{B}\\textbf{\\begin{mine}X\\verb|}|Y\\end{mine}}",
 "\\begin{document}\\textbf{A\\verb|x|B}\\end{document}",
 "\\begin{document}\\textbf{A\\verb|}|B}\\end{document}"
]) checks.check(source,false,true)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-verb-main.tex";
let child=input(root ++ "script-verb-child.tex","text")^;
let script=checks.expand(input(path,"text")^,{source_name:path,echo_definitions:true,constructor_argument_mode:true,
    adapters:[],resources:{local:{'script-verb-child.tex':{data:child}},bundled:{}}})^;
let native=input(path,"tex")^;
{issue:script.issue,text_match:reconstruct.reconstruct(script)^.text==native.text,
    diagnostics_match:script.diagnostic_records==native.diagnostics}
