import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let commands = ["expanded", "unexpanded", "detokenize", "scantokens"];
let openings = ["{X}", "\\relax{X}", "\\noexpand\\unknown{X}", "\\open X}",
    "\\act X}", "\\noexpand\\act X}", " X}"];
fn check(command, opening) any^ {
    let source = "\\let\\open={\\catcode`\\~=13 \\let~={\\let\\act=~\\begin{document}A\\" ++
        command ++ opening ++ "Z\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics, issue:result.issue,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (command in commands, opening in openings) check(command,opening)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    issues:all([for(c in checks)c.issue == null]),tree:all([for(c in checks)c.tree])}
