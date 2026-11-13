import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}$" ++ body ++ "$\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel,
        {constructor_argument_mode: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, expansion_issue: expanded.issue, issue: result.issue, unsupported: result.unsupported,
        native_parsed: native != null, matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in [
    "a<b", "a>b", "a<=b", "a>=b", "a< =b", "a> =b", "a<  =b", "a<%comment\n=b",
    "x_{a<b}", "x^{a>=b}", "\\frac{a<b}{c>d}", "\\downarrow[a<b]", "\\sin{a<b}",
    "<", ">", "<=", ">=", "x<^2", "x>_i", "x<=_i", "x>={2}", "a<<b", "a>>b"
]) check(body)^]
