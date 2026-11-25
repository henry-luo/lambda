import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let cases = [
    "\\def\\foo ab#1{[#1]}\\foo aX!",
    "\\def\\foo ab#1{[#1]}\\foo X!",
    "\\def\\foo ab#1{[#1]}\\foo a",
    "\\def\\foo ab#1{[#1]}\\foo ab{Y}!",
    "\\def\\foo a{Y}\\foo XZ",
    "\\def\\foo a{Y}\\foo aZ",
    "\\def\\foo ab#1{[#1]}\\foo a\\endinput",
    "\\def\\foo ab#1{[#1]}\\let\\bar\\foo\\bar aX!",
    "\\endlinechar=-1 \\def\\foo abc#1{[#1]}\\foo a",
    "\\endlinechar=-1 \\def\\foo abc#1{[#1]}\\foo",
    "\\endlinechar=-1 \\def\\foo abc#1{[#1]}\\foo abX"
];

fn check(source) any^ {
    let script = profile.expand_with_kernel(source, kernel,
        {echo_definitions:true, resources:{local:{}, bundled:{}}}, 100000)^;
    let native = parse(source, {type:"tex"})^;
    {text:reconstruct.reconstruct(script)^.text == native.text,
        diagnostics:script.diagnostic_records == native.diagnostics, issue:script.issue}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks), text:all([for (c in checks) c.text]),
    diagnostics:all([for (c in checks) c.diagnostics]), issues:[for (c in checks) c.issue]}
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "macro-prefix-main.tex";
let child = input(root ++ "macro-prefix-child.tex","text")^;
let result = profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,constructor_argument_mode:true,adapters:[],
        resources:{local:{'macro-prefix-child.tex':{data:child}},bundled:{}}},100000)^;
let native = input(path,"tex")^;
let tree = digest.digest(result)^;
{text:reconstruct.reconstruct(result)^.text == native.text,
    diagnostics:result.diagnostic_records == native.diagnostics,
    tree:tree.issue == null and tree.ast == input(path,"latex")^}
