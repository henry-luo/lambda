import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let sources=["\\begin{document}A \\insert0{\\unskip}B\\end{document}",
 "\\begin{document}A \\insert0{\\unskip X}B\\end{document}",
 "\\begin{document}A \\vadjust{\\unskip}B\\end{document}",
 "\\begin{document}A \\shipout\\hbox{\\unskip}B\\end{document}",
 "\\begin{document}A \\leaders\\hbox{\\unskip}\\hskip1pt B\\end{document}",
 "\\begin{document}A \\insert0{\\unskip X\\insert0{Y}Z}B\\end{document}",
 "\\begin{document}A \\insert0{\\insert0{\\unskip X}Y}B\\end{document}",
 "\\begin{document}A \\insert0{\\unskip\\def\\x{X}Y}B\\end{document}",
 "\\begin{document}A  \\insert0{\\unskip\\unskip X}B\\end{document}",
 "\\begin{document}A \\insert0{{\\unskip X}Y}B\\end{document}",
 "\\begin{document}A \\insert0{\\global\\count0=3\\unskip X}\\the\\count0 B\\end{document}",
 "\\begin{document}A \\insert0{\\vadjust{\\unskip}X}B\\end{document}",
 "\\begin{document}A \\insert0{\\vadjust{\\unskip}\\global\\def\\x{Y}X}\\x B\\end{document}"];
fn check(source,constructor) any^ {
 let result=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:constructor,echo_definitions:true,
  adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 let tree=digest.digest(result)^;
 let native_tree=parse(source,{type:"latex"}) ^ { null };
 {source:source,constructor:constructor,issue:result.issue,unsupported:result.unsupported,
  text:if(result.issue!=null)null else reconstruct.reconstruct(result)^.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,messages:result.messages==native.messages,
  tree_issue:tree.issue,tree_unsupported:tree.unsupported,
  ast:if(tree.ast==null or native_tree==null)null else tree.ast==native_tree}
}
let checked=[for(source in sources,constructor in [false,true]) check(source,constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text or not x.diagnostics or
 not x.messages or (x.ast!=true and x.tree_issue!="latex-digest-unsupported")) x],
 guarded:len([for(x in checked where x.tree_issue=="latex-digest-unsupported")x])}
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "discard-unskip-main.tex";
let child=input(root ++ "discard-unskip-child.tex","text")^;
let included_result=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,adapters:[],
 resources:{local:{'discard-unskip-child.tex':{data:child}},bundled:{}}},100000)^;
let native=input(path,"tex")^;
{issue:included_result.issue,text:reconstruct.reconstruct(included_result)^.text==native.text,
 diagnostics:included_result.diagnostic_records==native.diagnostics,messages:included_result.messages==native.messages}
