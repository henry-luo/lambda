import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import registry: lambda.doc.latex.packages.registry

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let adapters = ["xcolor", "article"];
let cases = [
    "\\usepackage{xcolor}\\begin{document}\\IfPackageLoadedTF{xcolor}{Y}{N}\\end{document}",
    "\\usepackage[foo]{xcolor}\\begin{document}\\makeatletter\\@ifpackagewith{xcolor}{foo}{Y}{N}:\\@ifpackageloaded{xcolor}{Y}{N}:\\@filelist\\makeatother\\end{document}",
    "\\usepackage{xcolor}\\usepackage{xcolor}\\begin{document}X\\end{document}",
    "\\documentclass{article}\\begin{document}\\makeatletter\\@ifclassloaded{article}{Y}{N}:\\@ifclasswith{article}{}{Y}{N}\\makeatother\\end{document}",
    "\\usepackage{nonexistentpackage}\\begin{document}\\makeatletter\\@ifpackageloaded{nonexistentpackage}{Y}{N}:\\@ifpackagewith{nonexistentpackage}{}{Y}{N}\\makeatother\\end{document}"
];
// The retired C engine was run with no adapters and no local resources.
// Production parse now supplies bundled adapters, so it is a different profile.
let unadapted_text = [
    "\\usepackage{xcolor}\\begin{document}N\\end{document}",
    "\\usepackage[foo]{xcolor}\\begin{document}Y:N:\\end{document}",
    "\\usepackage{xcolor}\\usepackage{xcolor}\\begin{document}X\\end{document}",
    "\\documentclass{article}\\begin{document}N:Y\\end{document}",
    "\\usepackage{nonexistentpackage}\\begin{document}N:Y\\end{document}"
];
fn compare(source, packages, reference) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions:true, constructor_argument_mode:true, adapters:packages, resources:{local:{}, bundled:{}}}, 100000)^;
    let actual = reconstruct.reconstruct(expanded)^;
    let native = if (len(packages) == 0) {text:reference, diagnostics:[]}
        else parse(source, {type:"tex", packages:packages})^;
    {issue:expanded.issue, text:actual.text == native.text, diagnostics:expanded.diagnostic_records == native.diagnostics}
}
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "document.tex";
let source = input(path, "text")^;
let resources = {local:{'lmdnotes.sty':{data:input(root ++ "lmdnotes.sty", "text")^},
    'lmdreport.cls':{data:input(root ++ "lmdreport.cls", "text")^}}, bundled:{}};
let expanded = profile.expand_with_kernel(source, kernel,
    {echo_definitions:true, constructor_argument_mode:true, resources:resources}, 100000)^;
let actual = reconstruct.reconstruct(expanded)^;
let native_options={base:path,packages:registry.adapter_names(),raw:registry.RAW_ARGUMENT_COMMANDS,chaptered:registry.chaptered_classes()};
let digested = digest.digest(expanded)^;
{cases:[for (i, source in cases, packages in [[],adapters]) compare(source,packages,unadapted_text[i])^],
 fixture:{issue:expanded.issue, digest_issue:digested.issue, text:actual.text == parse(source,{*:native_options,type:"tex"})^.text,
    ast:digested.ast == parse(source,{*:native_options,type:"latex"})^}}
