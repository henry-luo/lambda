import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let resources = {local: {'sample.sty': {data: "\\def\\sample{Y}"}}, bundled: {}};
fn output(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, resources: resources, adapters: ["geometry"]}, 100000)^;
    {text: join([for (token in result.output) token.value], ""), issue: result.issue}
}
let single = output("\\usepackage{expl3}A")^;
let duplicate = output("\\usepackage{expl3}\\usepackage{expl3}A")^;
let first = output("\\usepackage{expl3,foreign}A")^;
let trailing = output("\\usepackage{foreign,expl3}A")^;
let multiple = output("\\usepackage{expl3,foreign,other}A")^;
let foreign = output("\\usepackage{foreign}A")^;
let adapter = output("\\usepackage{expl3,geometry}A")^;
let local = output("\\usepackage{expl3,sample,foreign}\\sample")^;
[single.issue == null and single.text == "A ", duplicate.issue == null and duplicate.text == "A ",
 first.issue == null and first.text == "usepackage{foreign}A ", trailing.issue == null and trailing.text == "usepackage{foreign}A ",
 multiple.issue == null and multiple.text == "usepackage{foreign,other}A ",
 foreign.issue == null and foreign.text == "usepackage{foreign}A ",
 adapter.issue == null and adapter.text == "usepackage{geometry}A ",
 local.issue == null and local.text == "usepackage{foreign}Y"]
