import profile: lambda.doc.latex.tex_engine.profile
import native_snapshot: ._tex_native_snapshot
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
// Capture the retired native parser independently of the Script input path.
let native=input("test/lambda/latex/fixtures/tex_generated_math_native.json","json")^;
fn check(source, reference) any^ =>
    native_snapshot.run(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},reference);
[for(index,source in [
    "\\def\\m{\\mathbb{R}}\\begin{document}$\\m^n$\\end{document}",
    "\\def\\m{<}\\begin{document}$a\\m=b$\\end{document}",
    "\\def\\m{=}\\begin{document}$a<\\m b$\\end{document}",
    "\\def\\m{.}\\begin{document}$\\m\\m\\m$\\end{document}",
    "\\def\\m{\\{}\\begin{document}$\\m\\}$\\end{document}",
    "\\def\\m{\\operatorname}\\begin{document}$\\m*{lim}$\\end{document}",
    "\\def\\m{!12}\\begin{document}$\\m$\\end{document}",
    "\\def\\m{}\\begin{document}$a<\\m=b$\\end{document}",
    "\\def\\m{\\frac{1}{2}}\\begin{document}\\(\\m+1\\)\\end{document}",
    "\\def\\m{\\left(x\\right)}\\begin{document}$$\\m$$\\end{document}",
    "\\catcode`\\^=12 \\def\\m{a^2}\\catcode`\\^=7 \\begin{document}$\\m$\\end{document}",
    "\\catcode`!=8 \\def\\m{a!2}\\catcode`!=12 \\begin{document}$\\m$\\end{document}"
]) check(source,native.cases[index])^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-math-gen-main.tex";
let local={'script-math-gen-child-one.tex':{data:input(root ++ "script-math-gen-child-one.tex","text")^},
    'script-math-gen-child-two.tex':{data:input(root ++ "script-math-gen-child-two.tex","text")^}};
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,adapters:[],resources:{local:local,bundled:{}}},100000)^;
native_snapshot.compare(expanded,native.local,path)^
