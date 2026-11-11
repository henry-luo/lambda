import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source=input(root ++ "script_provenance.tex","text")^;
let style=input(root ++ "script_provenance.sty","text")^;
let x=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{'script_provenance.sty':{data:style}},bundled:{}}},100000)^;
let d=digest.digest(x)^;let native=input(root ++ "script_provenance.tex","latex")^;
"local package:"; {issue:d.issue,matches_native:d.ast == native,script_packages:d.ast.tex_packages,native_packages:native.tex_packages,script_diagnostics:d.ast.tex_diagnostics,native_diagnostics:native.tex_diagnostics}
let empty=profile.expand_with_kernel("\\begin{document}Hi\\end{document}",kernel,{resources:{local:{},bundled:{}}},100000)^;
let e=digest.digest(empty)^;let n=parse("\\begin{document}Hi\\end{document}",{type:"latex"})^;
"no package:"; {issue:e.issue,matches_native:e.ast == n,diagnostics:e.ast.tex_diagnostics,packages:e.ast.tex_packages}
