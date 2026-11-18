import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}};

fn check(source) any^ {
    let result = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, text: reconstruct.reconstruct(result)^.text == native.text,
        messages: result.messages == native.messages, diagnostics: result.diagnostic_records == native.diagnostics}
}

[for (source in [
    "\\ifnum1 1 T\\else F\\fi X", "\\ifnum1 2 T\\else F\\fi X",
    "\\ifnum1 a T\\else F\\fi X", "\\ifnum0 a T\\else F\\fi X",
    "\\unless\\ifnum1 2 T\\else F\\fi X",
    "\\def\\n{1}\\ifnum1 \\n T\\else F\\fi X",
    "\\ifdim1pt 1pt T\\else F\\fi X", "\\ifdim1pt 2pt T\\else F\\fi X",
    "\\iftrue\\ifnum0 a T\\else F\\fi X\\else Z\\fi",
    "\\ifnum1 \\relax T\\else F\\fi X"
]) check(source)^]
