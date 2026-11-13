import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {constructor_argument_mode: true, echo_definitions: true,
    adapters: [], resources: {local: {}, bundled: {}}};
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = parse(source, {type: "tex"})^;
    {issue: expanded.issue, unsupported: expanded.unsupported, reconstruction_issue: built.issue,
        text_match: if (expanded.issue == null and built.issue == null) built.text == native.text else null,
        diagnostics_match: expanded.diagnostic_records == native.diagnostics,
        actual: if (expanded.issue != null or built.issue != null or built.text == native.text) null else built.text,
        native: if (expanded.issue != null or built.issue != null or built.text == native.text) null else native.text}
}
[for (source in [
    "\\def\\q{\\quad}\\begin{document}\\textbf{\\q A}\\end{document}",
    "\\def\\q{\\quad}\\begin{document}\\textbf{X\\q A}B\\end{document}",
    "\\def\\q#1{\\quad#1}\\begin{document}\\textbf{\\q{A B}}C\\end{document}",
    "\\count0=0\\begin{document}\\textbf{\\global\\count0=7 X}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\mystery{\\global\\count0=7 X}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\textbf[\\global\\count0=7 X]{Y}\\the\\count0\\end{document}",
    "\\begin{document}A\\textbf{B\\hyphenation{words}C}D\\end{document}",
    "\\def\\q{ABC}\\begin{document}\\textbf{A\\q B}C\\end{document}",
    "\\def\\q{ABC}\\begin{document}\\textbf{A\\noexpand\\q B}C\\end{document}",
    "\\def\\q#1{#1#1}\\begin{document}\\textbf{\\q{A B}}C\\end{document}",
    "\\count0=0\\begin{document}\\textbf{\\iftrue A\\global\\count0=7 B\\else C\\fi}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\textbf{A\\textit{B\\global\\count0=7 C}D}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\begin{foo}[\\global\\count0=7]A\\end{foo}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}$\\mystery{A\\global\\count0=7 B}$\\the\\count0\\end{document}",
    "\\begin{document}\\textbf{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\def\\setlength#1#2{BAD}\\begin{document}\\textbf{\\setlength{\\foo}{X}}\\end{document}",
    "\\def\\addtolength#1#2{BAD}\\begin{document}\\mystery{\\addtolength{\\foo}{X}}\\end{document}",
    "\\def\\setcounter#1#2{BAD}\\begin{document}\\textbf{\\setcounter{foo}{7}}\\end{document}",
    "\\def\\addtocounter#1#2{BAD}\\begin{document}\\mystery{\\addtocounter{foo}{7}}\\end{document}",
    "\\def\\stepcounter#1{BAD}\\begin{document}\\textbf{\\stepcounter{foo}}\\end{document}",
    "\\def\\foo{EXPANDED}\\begin{document}\\directlua{\\foo}\\end{document}",
    "\\def\\foo{EXPANDED}\\begin{document}\\textbf{\\directlua{\\foo}}\\end{document}",
    "\\count0=0\\begin{document}\\textbf*{A\\global\\count0=7 B}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\textbf {A\\global\\count0=7 B}\\the\\count0\\end{document}",
    "\\count0=0\\begin{document}\\mystery[A{B}\\global\\count0=7]{C}\\the\\count0\\end{document}",
    "\\count0=0\\lambdaconstructor\\custom\\begin{document}\\custom{\\global\\count0=7 X}\\the\\count0\\end{document}",
    "\\def\\q{ABC}\\begin{document}\\mystery{é\\q λ}\\end{document}",
    "\\count0=0\\begin{document}\\textbf{A\\begin{quote}B\\end{quote}\\global\\count0=7 C}\\the\\count0\\end{document}",
    "\\begin{document}\\textbf{A\\verb|x|B}C\\end{document}",
    "\\newenvironment{mine}{A}{B}\\begin{document}\\textbf{X\\begin{mine}Y\\end{mine}Z}\\end{document}"
]) check(source)^]

fn check_expl(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, {*:options, script_expl3: true}, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = parse(source, {type: "tex"})^;
    {issue: expanded.issue, unsupported: expanded.unsupported, reconstruction_issue: built.issue,
        text_match: if (expanded.issue == null and built.issue == null) built.text == native.text else null,
        diagnostics_match: if (expanded.issue == null) expanded.diagnostic_records == native.diagnostics else null,
        actual: if (expanded.issue != null or built.issue != null or built.text == native.text) null else built.text,
        native: if (expanded.issue != null or built.issue != null or built.text == native.text) null else native.text}
}
[for (source in [
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{A}\\begin{document}\\textbf{\\tl_use:N\\l_x_tl}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{A}\\begin{document}\\textbf{\\tl_set:Nn\\l_x_tl{B}X}\\tl_use:N\\l_x_tl\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{A}\\begin{document}\\textbf{\\tl_gset:Nn\\l_x_tl{B}X}\\tl_use:N\\l_x_tl\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{EXPANDED}\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_set:Nn\\l_x_tl{\\foo}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{EXPANDED}\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_gset:Nn\\l_x_tl{{\\foo}~Y}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\let\\q\\tl_set:Nn\\begin{document}\\textbf{\\q\\l_x_tl{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nn\\unknown{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_set:Nn\\l_x_tl{}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\noexpand\\tl_set:Nn\\l_x_tl{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\cs_set:Npn\\tl_set:Nn#1#2{USER}\\begin{document}\\textbf{\\tl_set:Nn{A}{B}}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_put_right:Nn\\l_x_seq{A}\\begin{document}\\textbf{\\seq_count:N\\l_x_seq}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_if_empty:NTF\\l_x_tl{Y}{N}}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\begin{document}\\textbf{\\tl_set:Nx\\l_x_tl{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\begin{document}\\textbf{\\seq_put_right:Nn\\l_x_seq{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\let\\q\\seq_put_right:Nn\\begin{document}\\textbf{\\q\\l_x_seq{B}X}\\end{document}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\begin{document}\\textbf{\\noexpand\\seq_put_right:Nn\\l_x_seq{B}X}\\end{document}\\ExplSyntaxOff"
]) check_expl(source)^]
