import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options = {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}};
// Every matrix row starts from the same immutable kernel state.
let preload = profile.preload_kernel(kernel,options,100000)^;
let environments = ["itemize", "enumerate", "description"];
let suffixes = ["]bar", "]", "]bar]tail"];
let gaps = ["", " "];
fn check(environment, suffix, gap) any^ {
    let source = "\\begin{document}A\\catcode`\\]=11 \\def\\x{\\foo" ++ suffix ++
        "} \\catcode`\\]=12 \\begin{" ++ environment ++ "}" ++ gap ++ "[a\\x]z\\end{" ++ environment ++ "}\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (environment in environments, suffix in suffixes, gap in gaps) check(environment,suffix,gap)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
