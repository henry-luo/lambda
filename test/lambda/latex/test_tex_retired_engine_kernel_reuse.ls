// D10.2.2v2: cached kernel state cannot retain a previous document's definitions.
import facade: lambda.doc.latex.tex_engine.facade
let options = {type:"tex"};
let defined = parse("\\def\\sample{X}\\sample", options)^;
let later = parse("\\sample", options)^;
let independent = facade.tex("\\sample")^;
let local_x = {type:"tex", resources:{local:{'part.tex':{data:"X"}},bundled:{}}};
let local_y = {type:"tex", resources:{local:{'part.tex':{data:"Y"}},bundled:{}}};
let x = parse("\\input{part}", local_x)^;
let y = parse("\\input{part}", local_y)^;
let doc = "\\begin{document}Text \\textbf{bold}\\end{document}";
let latex = parse(doc, "latex")^;
let direct = facade.latex(doc)^;
let expl = "\\ExplSyntaxOn\\int_eval:n{2+3}\\ExplSyntaxOff";
let tex_expl = parse(expl,"tex")^;
let direct_expl = facade.tex(expl)^;
// A resource restart followed by expl3 must preserve both snapshots across parses.
let package_expl = "\\usepackage{biblatex}\\ExplSyntaxOn\\int_eval:n{2+3}\\ExplSyntaxOff";
let first_package = parse(package_expl, "tex")^;
let repeated_package = parse(package_expl, "tex")^;
[
    contains(defined.text,"X"),
    independent.issue == null and later == independent.root,
    x.text == "X  " and y.text == "Y  ",
    parse("\\input{part}", local_x)^ == x,
    direct.issue == null and latex == direct.root and parse(doc,"latex")^ == latex,
    direct_expl.issue == null and tex_expl == direct_expl.root and parse(expl,"tex")^ == tex_expl,
    contains(first_package.text, "5") and repeated_package == first_package and parse(expl,"tex")^ == tex_expl
]
