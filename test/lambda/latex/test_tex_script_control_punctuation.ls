import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let marks = ["!", "?", "/", "+", "=", ",", ";", "-", ":", "_", ".", "\"", "'", "(", ")", "*", "<", ">", "|", "]", "`"];
let contexts = ["definition", "format"];
fn check(mark, context) any^ {
    let prefix = "\\catcode`\\" ++ mark ++ "=11 ";
    let source = if (context == "definition") prefix ++ "\\def\\x{\\foo" ++ mark ++ "bar Q}\\begin{document}X\\end{document}"
        else prefix ++ "\\begin{document}A\\textbf{\\foo" ++ mark ++ "bar Q}Z\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (mark in marks, context in contexts) check(mark,context)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
