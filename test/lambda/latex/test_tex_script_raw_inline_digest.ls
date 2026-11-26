import checks: .mod_macro_recovery_checks
import native_snapshot: ._tex_native_snapshot
let native_refs=input("test/lambda/latex/fixtures/tex_raw_inline_digest_native.json","json")^;
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let options={constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check_tree(source,reference) any^ {
    let script=checks.expand(source,options)^;
    let built=digest.digest(script)^;
    let native=reference.latex;
    let tex=reference.tex;
    {issue:script.issue,digest_issue:built.issue,unsupported:built.unsupported,native_available:native!=null,
        text_match:source==reference.source and reconstruct.reconstruct(script)^.text==tex.text,
        diagnostics_match:script.diagnostic_records==tex.diagnostics,
        native_rejection_guarded:if(native==null)built.ast==null else true,
        ast_match:if(native==null or built.ast==null)null else native_snapshot.tree_matches(built.ast,reference,source)^}
}
[for(index,body in [
 "\\lstinline|abc|", "\\lstinline*|a b|", "\\lstinline[language=C]|int x;|",
 "\\lstinline{a{b}c}", "\\Verb|abc|", "\\mint{python}|x|",
 "\\mintinline[breaklines]{python}{print(1)}", "\\lstinline||", "\\lstinline|}|",
 "\\lstinline|%x|", "\\lstinline[foo={a[b]}]{x{y}z}",
 "\\mintinline[foo={bar}]{python}|é猫|", "\\lstinline |a b|",
 "\\lstinline|\\end{document}|", "\\lstinline|\\verb+foo+|"
],nested in [false,true]) {
 let content=if(nested)"\\textbf{A" ++ body ++ "B}" else "A" ++ body ++ "B";
 check_tree("\\begin{document}" ++ content ++ "\\end{document}",native_refs.cases[index*2+if(nested)1 else 0])^
}];
[for(index,source in [
 "\\begin{document}A\\verb|x|\\lstinline|y|B\\end{document}",
 "\\begin{document}A\\mintinline{py}|x|\\verb|y|B\\end{document}",
 "\\let\\q\\lstinline\\begin{document}A\\q|x|B\\end{document}",
 "\\begin{document}\\directlua{print(1)}\\lstinline|x|\\end{document}",
 "\\begin{document}\\char92\\lstinline|x|\\char94 A\\end{document}",
 "\\begin{document}\\begin{tabular}{c}\\mintinline{c}|x|\\end{tabular}\\end{document}"
]) check_tree(source,native_refs.cases[30+index])^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-inline-tree-main.tex";
let child=input(root ++ "script-inline-tree-child.tex","text")^;
let script=checks.expand(input(path,"text")^,{*:options,source_name:path,
    resources:{local:{'script-inline-tree-child.tex':{data:child}},bundled:{}}})^;
let built=digest.digest(script)^;
let native=native_refs.local.latex;
let tex=native_refs.local.tex;
{issue:script.issue,digest_issue:built.issue,text_match:path==native_refs.local.source and reconstruct.reconstruct(script)^.text==tex.text,
    diagnostics_match:script.diagnostic_records==tex.diagnostics,
    ast_match:if(built.ast==null)null else native_snapshot.tree_matches(built.ast,native_refs.local,path)^}
