import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}};

fn check(source, expl = false) any^ {
    let result = profile.expand_with_kernel(source, kernel, {*:options, script_expl3:expl}, 100000)^;
    let text = reconstruct.reconstruct(result)^;
    let native = parse(source, {type:"tex"})^;
    {issue:result.issue, text:text.text == native.text, diagnostics:result.diagnostic_records == native.diagnostics}
}

[for (source in [
    "\\number a", "\\number+a", "\\number--a", "\\number\\relax X", "\\number",
    "\\number'8X", "\\number\"GX", "\\count0=a\\the\\count0",
    "\\advance\\count0 by a\\the\\count0", "\\multiply\\count0 by a\\the\\count0",
    "\\def\\n{a}\\number\\n", "\\number\\undefined X", "\\number\\count a",
    "\\number{}X", "\\number%comment\nX", "\\number\\noexpand\\count0 X",
    "\\count0=2\\advance\\count0 by {}X\\the\\count0",
    "\\afterassignment Z\\count0=A X\\the\\count0", "\\number éX",
    "\\ifodd {}T\\else F\\fi", "\\number\\numexpr 2*{}\\relax"
]) check(source)^];

[for (source in [
    "\\ExplSyntaxOn\\int_sign:n{}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\int_abs:n{}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\int_eval:n{1+}X\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\int_eval:n{+a}X\\ExplSyntaxOff"
]) check(source, true)^];

let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "script-number-recovery-main.tex";
let child = input(root ++ "script-number-recovery-child.tex", "text")^;
let foreign = profile.expand_with_kernel(input(path,"text")^, kernel,
    {*:options, source_name:path, constructor_argument_mode:true,
        resources:{local:{'script-number-recovery-child.tex':{data:child}}, bundled:{}}}, 100000)^;
let native = input(path,"tex")^;
{issue:foreign.issue, text:reconstruct.reconstruct(foreign)^.text == native.text,
    diagnostics:foreign.diagnostic_records == native.diagnostics}
