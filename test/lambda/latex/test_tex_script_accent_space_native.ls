import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let preload = profile.preload_kernel(kernel, options, 100000)^;
let sources = [
 "\\begin{document}\\'\\i \\end{document}",
 "\\begin{document}\\'\\i  \\end{document}",
 "\\begin{document}\\'\\i x\\end{document}",
 "\\begin{document}\\'\\i  x\\end{document}",
 "\\begin{document}\\'\\i\nx\\end{document}",
 "\\begin{document}\\'\\i\t x\\end{document}",
 "\\begin{document}\\'\\`\\i  x\\end{document}",
 "\\begin{document}{\\'\\i  }x\\end{document}"
];
fn direct(source) any^ => digest.digest(profile.expand_with_kernel(source,kernel,options,100000,preload)^)
// Trees including whitespace are captured independently from the retired engine.
let trees = [for (source in sources) (direct(source)^).ast];
trees;
[for (i in 0 to len(sources) - 1) trees[i] == (parse(sources[i],{type:"latex"})^)];
[for (source in ["\\begin{document}\\'\\i\n\nx\\end{document}",
    "\\begin{document}\\'\\i %c\nx\\end{document}"])
    (direct(source)^).unsupported]
