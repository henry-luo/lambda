import checks: .mod_macro_recovery_checks
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let options={constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
[for(code in [92,94,126], pattern in [0,1,2,3,4,5], mode in [false,true] where pattern != 2 or mode) {
 let ch="\\char" ++ string(code);
 let source=if(pattern==0) "\\begin{document}" ++ ch ++ " A\\end{document}"
   else if(pattern==1) "\\begin{document}A" ++ ch ++ "\\end{document}"
   else if(pattern==2) "\\begin{document}A\\textbf{B" ++ ch ++ " C}D\\end{document}"
   else if(pattern==3) "\\begin{document}\\verb|x|" ++ ch ++ " A\\end{document}"
   else if(pattern==4) "\\begin{document}" ++ ch ++ "\\verb|x|\\end{document}"
   else "\\def\\f{" ++ ch ++ " }\\begin{document}A\\f B\\end{document}";
 let script=checks.expand(source,{*:options,constructor_argument_mode:mode})^;
 let built=digest.digest(script)^;
 let native=parse(source,{type:"latex"}) ^ {null};
 let tex=parse(source,{type:"tex"})^;
 {code:code,pattern:pattern,mode:mode,issue:script.issue,digest_issue:built.issue,unsupported:built.unsupported,
  native_available:native!=null,text_match:reconstruct.reconstruct(script)^.text==tex.text,
  diagnostics_match:script.diagnostic_records==tex.diagnostics,
  ast_match:if(native==null or built.ast==null)null else built.ast==native}
}];
// explicit legacy mode executes these argument characters instead of passing them through.
[for(code in [92,94,126]) {
 let source="\\begin{document}A\\textbf{B\\char" ++ string(code) ++ " C}D\\end{document}";
 let script=checks.expand(source,{*:options,constructor_argument_mode:false})^;
 let built=digest.digest(script)^;
 let native=parse(source,{type:"latex"})^;
 let tex=parse(source,{type:"tex"})^;
 {legacy_mode:true,code:code,issue:script.issue,digest_issue:built.issue,
  text_diff:reconstruct.reconstruct(script)^.text!=tex.text,ast_diff:built.ast!=native}
}];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-escaped-main.tex";
let child=input(root ++ "script-escaped-child.tex","text")^;
let included=checks.expand(input(path,"text")^,{*:options,source_name:path,
    resources:{local:{'script-escaped-child.tex':{data:child}},bundled:{}}})^;
let built=digest.digest(included)^;
let native=input(path,"latex")^;
let tex=input(path,"tex")^;
{issue:included.issue,digest_issue:built.issue,text_match:reconstruct.reconstruct(included)^.text==tex.text,
    diagnostics_match:included.diagnostic_records==tex.diagnostics,
    ast_match:if(built.ast==null)null else built.ast==native}
