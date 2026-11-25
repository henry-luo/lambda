import compare: ._tex_script_recovery
pub fn check(body,constructor,expl=true) any^ {
 let source=(if(expl) "\\ExplSyntaxOn" else "") ++ "\\begin{document}" ++ body ++
  (if(expl) "\\ExplSyntaxOff" else "") ++ "\\end{document}";
 let result=compare.inspect_tree(source,{script_expl3:expl,constructor_argument_mode:constructor})^;
 {body:body,constructor:constructor,issue:result.issue,unsupported:result.unsupported,
  text:result.text,diagnostics:result.diagnostics,messages:result.messages,ast:result.ast}
}
