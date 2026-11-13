import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}" ++ body ++ "\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, issue: result.issue, unsupported: result.unsupported,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in ["\\newtheorem{theorem}{Theorem}", "\\newtheorem*{remark}{Remark}",
 "\\newtheorem{lemma}[theorem]{Lemma}", "\\newtheorem{theorem}{Theorem}[section]",
 "A\\newtheorem{theorem}{Theorem}B", "\\newtheorem *{remark}{Remark}",
 "\\newtheorem* {remark}{Remark}", "\\newtheorem{abc}%comment\n{Theorem}",
 "\\newtheorem{abc}{The \\emph{Theorem}}", "\\newtheorem", "\\newtheorem*", "\\newtheorem{}{}", "\\newtheorem\t*{T}{X}", "\\newtheorem\n*{T}{X}", "\\newtheorem% hi\n*{T}{X}", "\\mystery*{X}", "\\mystery *{X}", "\\mystery\n*{X}"] ) check(body)^]

// an emitted star has no direct source position to establish adjacency.
let generated_source = "\\def\\mystar{*}\\begin{document}\\mystery\\mystar{X}\\end{document}";
let generated = profile.expand_with_kernel(generated_source, kernel,
    {constructor_argument_mode: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
let guarded = digest.digest(generated)^;
{generated_star_guarded: guarded.ast == null and guarded.issue == "latex-digest-unsupported" and
    guarded.unsupported == "command star source mapping"}
