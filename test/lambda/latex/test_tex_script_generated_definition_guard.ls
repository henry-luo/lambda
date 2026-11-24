import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source="\\def\\g{\\a@b}\\def\\f{\\addtolength{\\parindent}{2pt}}\\f\\begin{document}A\\end{document}";
let expanded=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:true,
 adapters:[],resources:{local:{},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let native=parse(source,{type:"tex"})^;
{tree_matches:result.issue==null and result.ast==parse(source,{type:"latex"})^,
 text_matches:reconstruct.reconstruct(expanded)^.text==native.text,
 diagnostics_match:expanded.diagnostic_records==native.diagnostics}
