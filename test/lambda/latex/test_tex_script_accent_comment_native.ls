import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let preload = profile.preload_kernel(kernel,options,100000)^;
let sources = [
 "\\begin{document}\\'% hi\n{e}\\end{document}",
 "\\begin{document}\\v% hi\n{e}\\end{document}",
 "\\begin{document}\\'\\`% hi\n{a}\\end{document}",
 "\\begin{document}\\'%\n{e}\\end{document}",
 "\\begin{document}\\'  % hello 123\nX\\end{document}",
 "\\begin{document}\\'% hello\n\\textbf{X}\\end{document}",
 "\\begin{document}é \\'% café\n{a}\\end{document}",
 "\\begin{document}{\\'% hi\n{e}}X\\end{document}"
];
fn direct(source) any^ => digest.digest(profile.expand_with_kernel(source,kernel,options,100000,preload)^)
// The captured native trees retain the comment's percent sign as the accent argument.
let trees = [for (source in sources) (direct(source)^).ast];
trees;
[for (i in 0 to len(sources) - 1) trees[i] == (parse(sources[i],{type:"latex"})^)];
[for (source in ["\\begin{document}\\'% $x$\nX\\end{document}",
    "\\begin{document}\\'% \\foo\nX\\end{document}",
    "\\begin{document}\\'% {a}\nX\\end{document}",
    "\\begin{document}\\'% hi\n\nX\\end{document}"])
    (direct(source)^).unsupported]
