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
 "\\begin{table}A\\end{table}", "\\begin{table}[h]A\\end{table}", "\\begin{table}[htbp]A\\end{table}",
 "\\begin{table} [h]A\\end{table}", "\\begin{table}[h!]A\\end{table}",
 "\\begin{tabular}{lc}a & b\\\\c & d\\end{tabular}", "\\begin{tabular}{|l|c|} a&b \\\\ c&d \\\\ \\end{tabular}",
 "\\begin{tabular} a&b\\end{tabular}", "\\begin{tabular} {cc}a&b\\end{tabular}",
 "\\begin{table}[h]\\caption{Title}\\begin{tabular}{cc}a&b\\end{tabular}\\end{table}",
 "\\begin{tabular}{cc}{a & b}&c\\end{tabular}", "\\begin{tabular}{cc}$a+b$ & \\textbf{b}\\end{tabular}",
 "\\begin{tabular}{cc}a \\& b & c\\end{tabular}", "\\begin{tabular}{cc}a&b\\\\[2pt]c&d\\end{tabular}",
 "A\\begin{table}[h]B\\end{table}C", "\\textbf{A\\begin{table}[h]B\\end{table}C}",
 "\\begin{tabular}{c}\\begin{quote}a&b\\end{quote}&c\\end{tabular}",
 "\\begin{table}%comment\n[h]A\\end{table}", "\\begin{tabular}{p{2cm}c}a&b\\end{tabular}", "\\begin{table}[ h  t ]A\\end{table}", "\\begin{table}[h% comment\n t]A\\end{table}", "\\begin{table}[\\textbf{x}]A\\end{table}", "\\def\\sep{&}\\begin{tabular}{cc}A\\sep B\\end{tabular}", "\\def\\col{{cc}}\\begin{tabular}\\col A&B\\end{tabular}", "\\def\\pos{[h]}\\begin{table}\\pos A\\end{table}", "\\begin{tabular}{cc}A&\\textbf{x & y}\\end{tabular}", "\\def\\c{l}\\begin{tabular}{\\c c}A&B\\end{tabular}", "\\def\\place{h}\\begin{table}[\\place]A\\end{table}"
]) check(body)^]
