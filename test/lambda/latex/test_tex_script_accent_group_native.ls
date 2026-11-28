import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let preload = profile.preload_kernel(kernel, options, 100000)^;
let sources = [
 "\\begin{document}\\'\\i{x}Z\\end{document}",
 "\\begin{document}\\'\\foo{x}Z\\end{document}",
 "\\begin{document}\\'\\textbf{x}Z\\end{document}",
 "\\begin{document}\\v\\foo[x]{y}Z\\end{document}",
 "\\begin{document}\\'\\foo{x}{y}Z\\end{document}",
 "\\begin{document}\\'\\foo{\\textbf{x}}Z\\end{document}",
 "\\begin{document}é \\'\\i{x}!\\end{document}",
 "\\begin{document}{\\'\\i{x}!}\\end{document}",
 "\\begin{document}\\'\\`\\i{x}!\\end{document}",
 "\\begin{document}\\'\\foo{}!\\end{document}",
 "\\begin{document}\\'\\foo(a)!\\end{document}",
 "\\begin{document}\\'\\foo{x} {y}!\\end{document}"
];
fn direct(source) any^ => digest.digest(profile.expand_with_kernel(source,kernel,options,100000,preload)^)
// Complete trees and source offsets come from the retired native engine.
let trees = [for (source in sources) (direct(source)^).ast];
trees;
[for (i in 0 to len(sources) - 1) trees[i] == (parse(sources[i],{type:"latex"})^)];
[for (source in ["\\begin{document}\\'\\textbf{x}{y}!\\end{document}"])
    (direct(source)^).unsupported]
