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
 "\\begin{equation}x+y\\end{equation}", "\\begin{equation*} x^2 \\end{equation*}",
 "\\begin{align}x&=y\\\\ z&=w\\end{align}", "\\begin{align*}  x+y \n\\end{align*}",
 "\\begin{equation}\\frac{a}{b}\\end{equation}", "A\\begin{equation} x \\end{equation}B",
 "\\begin{equation}\\left(x+y\\right)\\end{equation}", "\\begin{align}a&<b\\\\c&>d\\end{align}",
 "\\begin{equation}\\subseteq + .5\\end{equation}", "\\begin{equation}\\end{equation}",
 "\\begin{equation} a% comment\n+b \\end{equation}",
 "\\def\\m{x}\\begin{equation}\\m+1\\end{equation}",
 "\\textbf{A\\begin{equation}x\\end{equation}B}",
 "\\begin{equation} a \r\n b \\end{equation}", "\\begin{align}a\\cr b\\end{align}", "\\begin{equation}a&b\\end{equation}", "\\begin{equation}\\emptyset\\end{equation}", "\\begin{align}\\begin{split}x+y\\end{split}\\end{align}", "\\begin{align}x\\tag{1}\\end{align}", "\\begin{equation}[x]\\end{equation}",
 "\\begin{equation}a\\relax b\\end{equation}",
 "\\begin{equation}a\\ignorespaces  b\\end{equation}",
 "\\begin{equation}a{}b\\end{equation}",
 "\\begin{equation}a\\hskip1pt b\\end{equation}",
 "\\begin{equation}a\\global\\count0=3 b\\end{equation}", "\\def\\e{equation}\\begin{\\e}x\\end{equation}"
]) check(body)^]

let spaced_end = "\\begin{document}\\begin{equation}x\\end {equation}\\end{document}";
let spaced_expanded = profile.expand_with_kernel(spaced_end, kernel,
    {constructor_argument_mode: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
let spaced_result = digest.digest(spaced_expanded)^;
{spaced_end_guarded: spaced_result.ast == null and spaced_result.issue == "latex-digest-unsupported" and
    spaced_result.unsupported == "math environment delimiter source mapping"}
