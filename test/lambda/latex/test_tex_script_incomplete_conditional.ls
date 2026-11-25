import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases = ["\\iftrue A", "\\iffalse A", "\\iftrue\nA\\iftrue\nB", "\\iftrue A\\iffalse B", "\\iffalse A\\else B",
    "\\ifcase1 A\\or B", "\\ifcase3 A\\or B", "\\ifnum1=1 A", "\\ifnum1=2 A", "\\ifodd3 A",
    "\\iftrue\\ifnum1=1 A", "\\def\\f{\\iftrue X}\\f", "\\unless\\iffalse A", "\\unless\\iftrue A",
    "\\iftrue A\\else B", "\\iftrue\\iffalse X\\else Y", "\\iffalse\\iftrue X", "\\ifcase0 A\\or B",
    "\\iftrue A\\fi", "\\iffalse A\\fi", "\\ifnum\\iftrue1=1 A"];
fn check(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,issue:result.issue}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    issues:all([for(c in checks)c.issue == null])}
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "incomplete-if-main.tex";
let native = input(path,"tex")^;
let message = native.diagnostics[1].message;
let name_start = (index_of(message," of ")^ + 4)^;
let name_end = index_of(message," was incomplete")^;
let native_name = slice(message,name_start,name_end)^;
let child = input(root ++ "incomplete-if-child.tex","text")^;
let result = profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:native_name,echo_definitions:true,adapters:[],
        resources:{local:{'incomplete-if-child.tex':{data:child}},bundled:{}}},100000)^;
{text:reconstruct.reconstruct(result)^.text == native.text,
    diagnostics:result.diagnostic_records == native.diagnostics,issue:result.issue}
