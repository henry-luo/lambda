import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases = ["\\iftrue\n\\iffalse x", "\\iftrue\n\\iftrue x", "\\iftrue\\iffalse\\iftrue x", "\\iffalse\\iftrue\\iftrue x",
    "\\iftrue\\ifcase3 a\\or b", "\\ifnum\\iftrue1=0 A", "\\ifnum0=\\iftrue1 A", "\\ifnum0=\\iffalse1\\else2 A",
    "\\iftrue\\iftrue X\\else Y", "\\iftrue X\\else\\iftrue Y", "\\ifcase3 x\\or y\\else z",
    "\\def\\f{\\iftrue X}\\iftrue\\f", "\\iftrue\n\\iftrue\n\\iftrue X", "\\ifcase0 A\\or\\iftrue B",
    "\\ifcase0 A\\or B\\else C", "\\iftrue\\iffalse\\else\\iftrue X"];
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
