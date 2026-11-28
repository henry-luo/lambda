import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let preload = profile.preload_kernel(kernel, options, 100000)^;
let sources = [
    "\\begin{document}\\'\\i!\\end{document}",
    "\\begin{document}\\'\\%!\\end{document}",
    "\\begin{document}\\'\\%\\end{document}",
    "\\begin{document}\\'\\$\\end{document}",
    "\\begin{document}\\'\\&\\end{document}",
    "\\begin{document}\\v\\j!\\end{document}",
    "\\begin{document}\\~\\foo!\\end{document}",
    "\\begin{document}é \\'\\i!\\end{document}",
    "\\begin{document}{\\'\\i!}\\end{document}",
    "\\begin{document}\\'\\`\\i!\\end{document}"
];
fn direct(source) any^ => digest.digest(profile.expand_with_kernel(source, kernel, options, 100000, preload)^)
// The tree golden was captured with the retired native engine, including offsets.
let trees = [for (source in sources) (direct(source)^).ast];
trees;
[for (i in 0 to len(sources) - 1) trees[i] == (parse(sources[i], {type:"latex"})^)]
