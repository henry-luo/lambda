import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let commands = ["textbf", "emph"];
let suffixes = ["]bar", "]"];
let gaps = ["", " ", "\n", "\n\n", "%comment\n\n ", " %comment\r\n  "];
fn check(command, suffix, gap) any^ {
    let source = "\\catcode`\\]=11 \\begin{document}A\\" ++ command ++ "{\\foo" ++ suffix ++ gap ++ "X}Z\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (command in commands, suffix in suffixes, gap in gaps) check(command,suffix,gap)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
