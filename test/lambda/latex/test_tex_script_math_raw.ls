import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}" ++ body ++ "\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, issue: result.issue, unsupported: result.unsupported,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in [
 "$n!$", "$x!!$", "$!1#2$", "$! 12$", "$!^2$", "$!_a$", "$!1+!2$",
 "$#1$", "$#12!3$", "$#1+#2$", "${!}$", "$!{x}$", "$2!+3$",
 "\\newcommand{\\m}[1]{$#1$}X", "\\newcommand{\\m}[2]{$#1+#2$}X",
 "\\newcommand{\\m}[1]{\\mbox{\\boldmath $#1$}}X",
 "\\def\\m#1{$#1!$}X", "\\newcommand{\\m}[1]{$#1^2$}X",
 "\\begin{equation}n!\\end{equation}", "\\begin{equation}!1#2\\end{equation}",
 "$!123!45$", "$!\\,1$", "$!1^2$", "$!#1_2$", "$! 1#2$",
 "$\\frac{!1}{#2}$", "\\newcommand{\\m}[1]{$!#1$}X",
 "\\newcommand{\\m}[1]{$#1!3$}X", "$!%comment\n12$",
 "\\def\\m{!1}$\\m$", "\\begin{equation}!%comment\n12\\end{equation}",
 "$\\Box$", "$\\Box{a}$", "$\\Box^2$", "$\\Box[1]{x}$",
 "\\newcommand{\\QED}{\\hfill $\\Box$}X",
 "$\\{x\\}$", "$\\{\\}\\#\\%$", "$\\_\\{$", "$\\{\\_$",
 "$\\{^2$", "$\\}_a$", "$\\{ \\}$", "\\(\\$\\%\\#\\)", "$\\&\\(\\)$",
 "$\\frac{\\{} {\\}}$", "\\newcommand{\\set}[1]{$\\{#1\\}$}X",
 "$\\{\\}^2$", "$\\_\\_^2$", "$\\{x\\}\\in A$",
 "$\\underset{a}{b}$", "$\\overset{a}{b}$", "$\\stackrel{a}{b}$",
 "$\\underset{a}{b}^2$", "$\\overset12$", "$\\stackrel{a+b}{c_d}$",
 "\\newcommand{\\m}[1]{$\\underset{#1}{x}$}X"
]) check(body)^]
