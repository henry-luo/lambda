import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let baseline = parse("", {type:"tex"})^.messages;

fn check(source) any^ {
    let script = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let native = parse(source, {type:"tex"})^;
    let rebuilt = reconstruct.reconstruct(script)^;
    script.issue == null and rebuilt.issue == null and rebuilt.text == native.text and
        script.messages == native.messages and
        script.diagnostic_records == native.diagnostics
}
[for (source in [
    "\\def\\x{A}\\unless\\expandafter\\ifx\\noexpand\\x\\x T\\else F\\fi",
    "A\\unless B", "A\\unless\\relax B", "\\unless", "\\unless\\ifcase1 A\\or B\\fi",
    "\\let\\u\\unless\\u\\relax X", "\\unless\\iftrue A\\else B\\fi",
    "\\unless\\iffalse A\\else B\\fi", "\\iftrue X\\unless\\relax Y\\else N\\fi",
    "\\iftrue X\\unless\\ifcase0 A\\or B\\fi Y\\else N\\fi",
    "\\unless\\unless\\iftrue A\\else B\\fi", "A\n\\unless\\relax B",
    "\\def\\x{A}\\unless\\x B", "\\unless\\missing X"
]) check(source)^];

fn ast(body) any^ {
    let source = "\\begin{document}" ++ body ++ "\\end{document}";
    let script = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let result = digest.digest(script)^;
    let native = parse(source, {type:"latex"})^;
    {issue: result.issue, unsupported: result.unsupported,
     matches: if (result.ast == null) false else result.ast == native}
}
[ast("A\\unless\\relax B")^, ast("\\unless\\ifcase1 A\\or B\\fi")^]
