import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source, opts={}) any^ {
 let r=profile.expand_with_kernel(source,kernel,{*:opts, echo_definitions:true, resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 {issue:d.issue, unsupported:d.unsupported, matches_native:if(d.ast == null) null else d.ast == (parse(source,{type:"latex"})^)}
}
"argument:"; report("\\def\\a#1{\\textbf{#1}}\\begin{document}\\a{X}\\end{document}")
"arguments after text:"; report("\\def\\a#1#2{\\textit{#1/#2}}\\begin{document}Hi \\a{X}{Y}!\\end{document}")
"nested macro:"; report("\\def\\outerzz{\\innerzz}\\def\\innerzz{\\emph{Hi}}\\begin{document}\\outerzz\\end{document}")
"nested formatting:"; report("\\def\\a{\\textbf{A\\emph{B}}}\\begin{document}\\a\\end{document}")
"repeated macro:"; report("\\def\\a{\\textbf{X}}\\begin{document}\\a/\\a\\end{document}")
"unicode before call:"; report("\\def\\a{\\textbf{X}}\\begin{document}é\\a\\end{document}")
"delimited argument:"; report("\\def\\a#1!{\\textit{#1}}\\begin{document}\\a X!\\end{document}")
"parameter macro:"; report("\\def\\a#1{\\emph{#1}}\\def\\b{Y}\\begin{document}\\a{\\b}\\end{document}")
"direct outer synthetic inner:"; report("\\def\\a{\\emph{X}}\\begin{document}\\textbf{A\\a B}\\end{document}")
"conditional:"; report("\\def\\a{\\textbf{X}}\\begin{document}\\iftrue\\a\\fi\\end{document}")
"expandafter:"; report("\\def\\a{\\textbf{X}}\\begin{document}\\expandafter\\a\\relax\\end{document}")
"optional parameter:"; report("\\newcommand{\\a}[2][D]{\\textbf{#1/#2}}\\begin{document}\\a[X]{Y}\\end{document}")
"futurelet lookahead:"; report("\\def\\a{\\textbf{X}}\\begin{document}\\futurelet\\next\\a!\\end{document}")
"aftergroup:"; report("\\def\\a{\\textbf{X}}\\begin{document}{\\aftergroup\\a}Y\\end{document}")
"nested argument lookahead:"; report("\\def\\a#1{\\textbf{#1}}\\def\\b#1{\\a{#1}}\\begin{document}\\b{X}Y\\end{document}")
"argument formatting repeated:"; report("\\def\\a#1{\\textbf{#1}\\textit{#1}}\\begin{document}\\a{Z}\\end{document}")
"case shift:"; report("\\begin{document}\\uppercase{\\textbf{Hi}}\\end{document}")
"scantokens:"; report("\\begin{document}\\scantokens{\\textbf{Hi}}\\end{document}")
"computed control sequence:"; report("\\def\\a{\\csname textbf\\endcsname{X}}\\begin{document}\\a\\end{document}")
"empty macro argument:"; report("\\def\\a#1{\\textbf{#1}}\\begin{document}\\a{}\\end{document}")
"multiple nested macros:"; report("\\def\\aaa{\\bbb}\\def\\bbb{\\ccc}\\def\\ccc{\\emph{Z}}\\begin{document}\\aaa\\end{document}")
"empty substitution only:"; report("\\def\\aaa#1{#1}\\begin{document}X\\aaa{}Y\\end{document}")
"empty and nonempty arguments:"; report("\\def\\aaa#1#2{\\textbf{#1/#2}}\\begin{document}\\aaa{}{B}\\end{document}")
"repeated empty argument:"; report("\\def\\aaa#1{\\textbf{#1#1}}\\begin{document}\\aaa{}\\end{document}")
"empty optional argument:"; report("\\newcommand{\\aaa}[2][]{\\textbf{#1/#2}}\\begin{document}\\aaa{B}\\end{document}")
"explicit empty optional:"; report("\\newcommand{\\aaa}[2][X]{\\textbf{#1/#2}}\\begin{document}\\aaa[]{B}\\end{document}")
"lowercase origin:"; report("\\begin{document}\\lowercase{\\textit{ABC}}\\end{document}")
"case control nested:"; report("\\begin{document}\\uppercase{A\\emph{bc}d}\\end{document}")
"case zero mapping:"; report("\\uccode`a=0\\begin{document}\\uppercase{\\textbf{ab}}\\end{document}")
