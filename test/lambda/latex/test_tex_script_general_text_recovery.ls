import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let names = ["unexpanded", "detokenize", "scantokens"];
let tails = ["{abc}Z\\end{document}", "\\relax{abc}Z\\end{document}",
    " XZ\\end{document}", "{abc"];
fn check(name, tail) any^ {
    let source = "\\begin{document}A\\" ++ name ++ tail;
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let actual = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics, issue:result.issue,
        tree:if (actual.issue == null) actual.ast == parse(source,{type:"latex"})^ else null,
        guard:if (actual.issue == null) null else actual.unsupported}
}
let checks = [for (name in names, tail in tails) check(name,tail)^];
{cases:len(checks),text:all([for (c in checks) c.text]),diagnostics:all([for (c in checks) c.diagnostics]),
    issues:[for (c in checks) c.issue],trees:[for (c in checks) c.tree],guards:[for (c in checks) c.guard]}
let root = "test/lambda/latex/fixtures/local_packages/";
fn included(name) any^ {
    let child_name = "general-" ++ name ++ "-child.tex";
    let path = root ++ "general-" ++ name ++ "-main.tex";
    let resources = {local:map([child_name,{data:input(root ++ child_name,"text")^}]),bundled:{}};
    let result = profile.expand_with_kernel(input(path,"text")^,kernel,
        {source_name:path,echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:resources},100000)^;
    let native = input(path,"tex")^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == input(path,"latex")^}
}
[for (name in names) included(name)^]
