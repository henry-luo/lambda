import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let heads = ["\\newenvironment{x}", "\\newcommand{\\x}", "\\providecommand{\\x}", "\\renewcommand{\\x}"];
let gaps = ["\n\n", " \n\n", "%comment\n\n", "\r\n\r\n"];
fn check(head, gap) any^ {
    let source = "\\begin{document}A" ++ head ++ gap ++ "{A}{B}X\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (head in heads, gap in gaps) check(head,gap)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "definition-paragraph-main.tex";
let child = input(root ++ "definition-paragraph-child.tex","text")^;
let included = profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,constructor_argument_mode:true,echo_definitions:true,adapters:[],
        resources:{local:{'definition-paragraph-child.tex':{data:child}},bundled:{}}},100000)^;
let native = input(path,"tex")^;
let tree = digest.digest(included)^;
{text:reconstruct.reconstruct(included)^.text == native.text,
    diagnostics:included.diagnostic_records == native.diagnostics,
    tree:tree.issue == null and tree.ast == input(path,"latex")^}
fn nested(wrapper, body) any^ {
    let source = "\\begin{document}A\\" ++ wrapper ++ "{before" ++ body ++ "}Z\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let nested_bodies = ["\\newenvironment {x}{A}{B}X", "\\newenvironment{x} {A}{B}X",
    "\\newenvironment{x}{A} {B}X", "\\newenvironment{x}[1] {A}{B}X", "\\newcommand{\\x} {A}{B}X",
    "\\newenvironment{x}\n\n{A}{B}X", "\\newenvironment{x}%comment\n\n{A}{B}X"];
let nested_checks = [for (wrapper in ["textbf", "emph", "unknown"], body in nested_bodies) nested(wrapper,body)^];
{cases:len(nested_checks),text:all([for(c in nested_checks)c.text]),
    diagnostics:all([for(c in nested_checks)c.diagnostics]),tree:all([for(c in nested_checks)c.tree])}
