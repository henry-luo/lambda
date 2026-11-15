import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(count, comment) any^ {
    let prefix = join([for (i in 1 to count) " "], "");
    let source = "\\begin{document}A" ++ prefix ++
        (if (comment) "%c\r\n\n" else "\n\n") ++ "B\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}}, 100000)^;
    let result = digest.digest(expanded)^;
    {count:count, comment:comment, issue:result.issue,
        matches:result.ast == (parse(source, {type:"latex"})^)}
}
let cases = [for (count in [63,64,65,127,128,129,255,256,257], comment in [true,false]) check(count,comment)^];
{cases:len(cases), failures:[for (item in cases where not item.matches) item]}
