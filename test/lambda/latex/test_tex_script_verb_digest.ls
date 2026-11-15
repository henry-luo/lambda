import checks: .mod_macro_recovery_checks
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let options={constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check_tree(source) any^ {
    let script=checks.expand(source,options)^;
    let built=digest.digest(script)^;
    let native=parse(source,{type:"latex"}) ^ {null};
    let tex=parse(source,{type:"tex"})^;
    {issue:script.issue,digest_issue:built.issue,unsupported:built.unsupported,native_available:native!=null,
        text_match:reconstruct.reconstruct(script)^.text==tex.text,
        diagnostics_match:script.diagnostic_records==tex.diagnostics,
        ast_match:if(native==null or built.ast==null)null else built.ast==native}
}
[for(source in [
 "\\begin{document}\\verb||\\end{document}",
 "\\begin{document}A\\verb|}|B\\end{document}",
 "\\begin{document}A\\verb|{|B\\end{document}",
 "\\begin{document}A\\verb|%x|B\\end{document}",
 "\\begin{document}\\begin{a}A\\verb|}|B\\end{a}\\end{document}",
 "\\begin{document}\\begin{itemize}\\item A\\verb|%x|B\\end{itemize}\\end{document}",
 "\\begin{document}\\begin{tabular}{c}A\\verb|&|B\\end{tabular}\\end{document}",
 "\\begin{document}\\begin{tabular}{c}A\\verb|}|B\\end{tabular}\\end{document}",
 "\\begin{document}\\begin{tabular}{c}A\\verb|%x|B\\end{tabular}\\end{document}",
 "\\begin{document}A\\verb|xyz|B\\end{document}",
 "\\begin{document}A\\verb*|x y|B\\end{document}",
 "\\begin{document}A\\textbf{B\\verb|xyz|C}D\\end{document}",
 "\\begin{document}A\\textbf{B\\verb|é猫|C}D\\end{document}",
 "\\begin{document}A\\textbf{B\\verb|}|C}D\\end{document}",
 "\\begin{document}A\\textbf{B\\verb|{x}|C}D\\end{document}",
 "\\begin{document}A\\textbf{B\\verb|%x|C}D\\end{document}",
 "\\begin{document}A\\foo[\\verb|x|]{Y}D\\end{document}",
 "\\begin{document}A\\begin{a}{\\verb|x|}B\\end{a}D\\end{document}",
 "\\def\\f{hello}\\begin{document}\\f\\verb|x|\\end{document}",
 "\\let\\q\\verb\\begin{document}A\\q|x|B\\end{document}",
 "\\let\\verbx\\verb\\begin{document}A\\verbx|x|B\\end{document}",
 "\\begin{document}\\char92\\verb|x|\\end{document}",
 "\\begin{document}\\verb|x|\\char94 A\\end{document}",
 "\\begin{document}\\verb|x|\\char126 A\\end{document}",
 "é\\begin{document}\\textbf{A\\verb|x|B}\\end{document}",
 "\\begin{document}\\textbf{A\\verb+X+\\verb!Y!B}\\end{document}",
 "\\begin{document}\\textbf{A\\verb[XYZ[B}\\end{document}"
]) check_tree(source)^];
let root="test/lambda/latex/fixtures/local_packages/";
// raw echoes followed by whitespace or comments use the full captured source span.
fn included(main_name,child_name) any^ {
    let path=root ++ main_name;
    let child=input(root ++ child_name,"text")^;
    let script=checks.expand(input(path,"text")^,{*:options,source_name:path,
        resources:{local:map([child_name,{data:child}]),bundled:{}}})^;
    let built=digest.digest(script)^;
    let native=input(path,"latex")^;
    let tex=input(path,"tex")^;
    {issue:script.issue,digest_issue:built.issue,text_match:reconstruct.reconstruct(script)^.text==tex.text,
        diagnostics_match:script.diagnostic_records==tex.diagnostics,
        ast_match:if(built.ast==null)null else built.ast==native}
}
included("script-verb-tree-main.tex","script-verb-tree-child.tex")^;
included("script-verb-tail-main.tex","script-verb-tail-child.tex")^;
included("script-verb-comment-main.tex","script-verb-comment-child.tex")^
