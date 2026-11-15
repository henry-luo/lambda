import checks: .mod_macro_recovery_checks
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
[for(source in [
 "A\\lstinline|abc|B", "A\\lstinline|a%}b|B", "A\\lstinline*|a b|B",
 "A\\lstinline[language=C]|int x;|B", "A\\lstinline[foo={a[b]}]{x{y}z}B",
 "A\\Verb|abc|B", "A\\Verb*[formatcom=\\bfseries]|a b|B", "A\\Verb{a{b}c}B",
 "A\\mint{python}|print(1)|B", "A\\mintinline{python}{print({1:2})}B",
 "A\\mintinline[breaklines]{python}|é猫|B", "A\\mint[foo={bar}]{c}|x%}y|B",
 "\\begin{document}\\textbf{A\\lstinline|x}|B}\\end{document}",
 "\\begin{document}\\textbf{A\\Verb|x}|B}\\end{document}",
 "\\begin{document}\\textbf{A\\mintinline{python}|x}|B}\\end{document}",
 "\\begin{a}{A\\lstinline|x}|B}\\end{a}",
 "\\lstinline|unterminated", "\\lstinline[unclosed", "\\mint{python}|unterminated",
 "\\lstinline{first\nsecond}Z"
],mode in [false,true]) checks.check(source,false,mode)^];
// native scans delimiters as bytes; partial UTF-8 reader positions remain guarded.
[for(mode in [false,true]) {
 let expanded=checks.expand("\\lstinline§abc§",{constructor_argument_mode:mode,echo_definitions:true,
     adapters:[],resources:{local:{},bundled:{}}})^;
 {issue:expanded.issue,unsupported:expanded.unsupported}
}];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-raw-inline-main.tex";
let child=input(root ++ "script-raw-inline-child.tex","text")^;
let script=checks.expand(input(path,"text")^,{source_name:path,echo_definitions:true,constructor_argument_mode:true,
    adapters:[],resources:{local:{'script-raw-inline-child.tex':{data:child}},bundled:{}}})^;
let native=input(path,"tex")^;
{issue:script.issue,text_match:reconstruct.reconstruct(script)^.text==native.text,
 diagnostics_match:script.diagnostic_records==native.diagnostics}
