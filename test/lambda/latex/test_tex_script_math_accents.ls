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
 "a\\ne b", "a\\neq b", "\\ne^2", "\\neq_{x}", "\\hat{x}+\\bar{x}+\\vec{x}+\\dot{x}+\\ddot{x}",
 "\\dddot{x}+\\ddddot{x}+\\tilde{x}+\\acute{x}+\\breve{x}+\\check{x}+\\grave{x}",
 "\\widehat{x}+\\widetilde{x}+\\widecheck{x}+\\overline{x}+\\underline{x}+\\underbar{x}",
 "\\overbrace{x}+\\underbrace{x}+\\overbracket{x}+\\underbracket{x}",
 "\\overrightarrow{x}+\\Overrightarrow{x}+\\underrightarrow{x}+\\overleftarrow{x}+\\underleftarrow{x}",
 "\\overleftrightarrow{x}+\\underleftrightarrow{x}+\\overleftharpoon{x}+\\overrightharpoon{x}",
 "\\overgroup{x}+\\undergroup{x}+\\overlinesegment{x}+\\underlinesegment{x}+\\utilde{x}+\\mathring{x}",
 "\\hat x", "\\hat\\alpha", "\\hat", "\\overbrace{x+y}^2", "\\underbrace{\\frac{a}{b}}_i",
 "\\sqrt{\\hat{x}}", "\\hat%comment\nx", "\\hat{}"
]) check(body)^]
