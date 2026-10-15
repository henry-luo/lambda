// Phase V groundwork (vibe/impl/Lambda_Impl_Latex_Packages.md M0): package
// command ownership and dispatch, conflicts and fonts as catalogue data, unknown
// classes, general counters, float kinds and lists, label scoping, and the
// unmapped math command diagnostic.
import latex: lambda.doc.latex.latex
import registry: lambda.doc.latex.packages.registry
import render: lambda.doc.latex.render
import analyze: lambda.doc.latex.analyze

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn result_of(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    latex.render_result(ast, null)
}

fn html_of(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    latex.render_to_html(ast, null)
}

fn issues(src) => [for (issue in result_of(src).diagnostics)
    issue.code ++ " " ++ string(issue.package) ++ " " ++ string(issue.item)]

// ---- M0.1 ownership, dispatch and conflicts ----
// a "*" entry serves the commands a package's command_prefix names (fontawesome5); a
// class family's entry serves its classes' commands (M10)
fn owned(key) => if (registry.CATALOG[key] != null) registry.CATALOG[key].commands else registry.family_commands(key)
"handlers owned by their package:"; [for (pkg, table in render.PACKAGE_RENDER, cmd, h in table
    where string(cmd) != "*" and not contains(owned(string(pkg)), string(cmd)))
    string(pkg) ++ "." ++ string(cmd)]
"walk handlers owned:"; [for (pkg, table in analyze.PACKAGE_WALK, cmd, h in table
    where not contains(owned(string(pkg)), string(cmd))) string(pkg) ++ "." ++ string(cmd)]
"owner while active:"; registry.owner([{key: "cleveref", val: {}}], "cref")
"no owner while inactive:"; registry.owner([], "cref")
"inactive command attribution:"; issues(doc("", "\\enquote{a} \\cref{x}"))
"active package dispatch:"; index_of(html_of(doc("\\usepackage{csquotes}", "\\enquote{a}")), "latex-unsupported") == null
"conflict from catalogue:"; issues(doc("\\usepackage{natbib}\n\\usepackage{biblatex}", "Hi."))

// ---- M0.4 font requests ----
"font substitutions:"; issues(doc("\\usepackage{helvet}\n\\usepackage{cmbright}", "Hi."))
"main family css:"; index_of(html_of(doc("\\usepackage{cmbright}", "Hi.")), "Computer Modern Sans") != null

// ---- M0.5 and M0.6 classes and xparse ----
"unknown class:"; issues("\\documentclass{moderncv}\n\\begin{document}\nHi.\n\\end{document}\n")
"profile class:"; issues("\\documentclass{book}\n\\begin{document}\nHi.\n\\end{document}\n")
"xparse:"; issues(doc("\\usepackage{xparse}\n\\NewDocumentCommand{\\pair}{m}{[#1]}", "\\pair{a}"))

// ---- M0.2 counters ----
let counters = html_of(doc("\\newcounter{step}[section]",
    "\\setcounter{section}{4}\n\\section{Five}\n\\addtocounter{section}{2}\n\\section{Eight}\n" ++
    "\\refstepcounter{step}\\label{st:a}\\refstepcounter{step}\\label{st:b}\n\\section{Nine}" ++
    "\\refstepcounter{step}\\label{st:c}\nSteps \\ref{st:a}, \\ref{st:b}, \\ref{st:c}."))
"setcounter and addtocounter:"; index_of(counters, ">5 </span>Five") != null and index_of(counters, ">8 </span>Eight") != null
"newcounter reset by parent:"; index_of(counters, "#st:b\">2</a>, <a class=\"latex-ref\" href=\"#st:c\">1</a>") != null

// ---- M0.2 label scope after floats and theorems ----
let scoped = html_of(doc("", "\\section{A}\\section{B}\\section{C}\n" ++
    "\\begin{figure}\\caption{Cap}\\label{fig:a}\\end{figure}\n\\label{sec:fig}\n" ++
    "\\begin{theorem}T\\end{theorem}\\label{sec:thm}\nSee \\ref{fig:a}, \\ref{sec:fig}, \\ref{sec:thm}."))
"labels after environments:"; index_of(scoped, "#fig:a\">1</a>, <a class=\"latex-ref\" href=\"#sec:fig\">3</a>, <a class=\"latex-ref\" href=\"#sec:thm\">3</a>") != null

// ---- M0.2 appendix lettering in chaptered classes ----
let book = html_of("\\documentclass{book}\n\\begin{document}\n\\chapter{One}\n\\chapter{Two}\n\\appendix\n\\chapter{Extra}\n\\section{Detail}\n\\end{document}\n")
"book appendix:"; index_of(book, ">A </span>Extra") != null and index_of(book, ">A.1 </span>Detail") != null

// ---- M0.2 lists of floats ----
let lists_src = doc("", "\\listoffigures\n\\listoftables\n" ++
    "\\begin{figure}\\caption[Short one]{A long caption}\\label{f:1}\\end{figure}\n" ++
    "\\begin{figure}\\caption{Unlabelled}\\end{figure}\n\\begin{table}\\caption{Tab}\\label{t:1}\\end{table}")
let lists = html_of(lists_src)
"list of figures:"; index_of(lists, "<a href=\"#f:1\"><span class=\"sec-num\">1</span>Short one</a>") != null
"unlabelled entry:"; index_of(lists, "<span class=\"sec-num\">2</span>Unlabelled") != null
"list of tables:"; index_of(lists, "List of Tables") != null and index_of(lists, "#t:1") != null
"list issues:"; issues(lists_src)

// ---- M0.3 unmapped math commands ----
let math_src = doc("\\usepackage{amsmath}\n\\DeclareMathOperator{\\Tr}{Tr}",
    "$\\abs{x} + \\alpha + \\Tr A$\n\\begin{align}a &= \\norm{b}\\label{eq:a}\\end{align}")
"unmapped math:"; issues(math_src)
"unmapped math located:"; all([for (issue in result_of(math_src).diagnostics) issue.offset != null])
