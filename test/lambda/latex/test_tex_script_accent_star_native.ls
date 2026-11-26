import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let preload = profile.preload_kernel(kernel,options,100000)^;
let constructor_preload = profile.preload_kernel(kernel,{*:options,constructor_argument_mode:true},100000)^;
let sources = [
    "\\begin{document}\\'\\foo*!\\end{document}",
    "\\begin{document}\\v\\foo*{x}!\\end{document}",
    "\\begin{document}\\~\\foo*[a]{x} Z\\end{document}",
    "\\begin{document}{\\'\\foo*{x}{y}}Z\\end{document}",
    "\\begin{document}é \\'\\foo*{x}!\\end{document}",
    "\\begin{document}\\'\\`\\foo*{x}!\\end{document}",
    "\\begin{document}\\'\\newtheorem*{x} Z\\end{document}",
    "\\begin{document}\\'\\A@x!\\end{document}",
    "\\begin{document}\\'\\A@x*{x}!\\end{document}",
    "\\begin{document}{\\v\\foo@bar*[a]{x}}Z\\end{document}",
    "\\begin{document}\\'\\textbf@x{hello}!\\end{document}",
    "\\begin{document}\\'\\@abc!\\end{document}"
];
fn direct(source, constructor = false) any^ =>
    digest.digest(profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:constructor},
        100000,if (constructor) constructor_preload else preload)^)
// The tree golden was captured independently with the retired native parser.
let trees = [for (source in sources) (direct(source)^).ast];
trees;
[for (i in 0 to len(sources) - 1) trees[i] == (parse(sources[i],{type:"latex"})^)];
[for (i in 0 to len(sources) - 1) trees[i] == (direct(sources[i],true)^).ast];
[for (source in ["\\begin{document}\\'\\textbf*{x}\\end{document}",
    "\\begin{document}\\'\\section*{b}\\end{document}"])
    (direct(source)^).unsupported]
