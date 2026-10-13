// Phase V M8 (vibe/impl/Lambda_Impl_Latex_Packages.md §12): math packages. physics
// and mathtools' macro layer are engine bindings; bm, cancel, nicefrac, xfrac and
// mhchem are math constructs; mathtools tag forms are equation numbering.
import latex: lambda.latex.latex

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

// the source the engine hands the digester, between \begin{document} and
// \end{document}, in a list so each expansion prints on its own
fn expanded(src) {
    let text = (parse(src, {type: "tex"}) ^ { null }).text
    let start = index_of(text, "\\begin{document}\n") + 17;
    [trim(slice(text, start, index_of(text, "\n\\end{document}")))]
}

fn run(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    {html: latex.render_to_html(ast, null),
     issues: [for (issue in latex.render_result(ast, null).diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null
fn count(text, part) => len(split(text, part)) - 1

// ---- physics: bracing, vectors, operators, derivatives, Dirac notation, matrices ----
let p = "\\usepackage{physics}"
"braces:"; expanded(doc(p, "$\\qty(a) \\qty\\big[b] \\abs{c} \\abs*{d} \\norm{v} \\eval{x}_0^1 \\order{x^2} \\comm{A}{B}$"))
"vectors:"; expanded(doc(p, "$\\vb{a} \\vb*{\\theta} \\va{a} \\vu{a} \\grad\\Psi \\div{\\vb{a}} \\curl(\\vb{a}) \\laplacian\\Psi \\divisionsymbol$"))
"operators:"; expanded(doc(p, "$\\sin x \\sin[2](x) \\exp(y) \\tr\\rho \\Res[f] \\Re{z} \\real \\pv{f} \\qq{if} \\qc$"))
"derivatives:"; expanded(doc(p, "$\\dd x \\dd{x} \\dd[3]{x} \\dv{x} \\dv[n]{f}{x} \\dv*{f}{x} \\pdv{f}{x}{y} \\fdv{F}{g} \\var(E)$"))
"dirac:"; expanded(doc(p, "$\\ket{\\psi} \\bra{\\phi}\\ket{\\psi} \\braket{a}{b} \\ketbra{a}{b} \\expval{A} \\expval{A}{\\Psi} \\ev**{A}{\\Psi} \\mel*{n}{H}{m}$"))
"matrices:"; expanded(doc(p, "$\\mqty(a & b) \\mqty*(a) \\smqty[a] \\mdet{a} \\imat{2} \\xmat*{a}{2}{2} \\xmat*{a}{2}{1} \\zmat{1}{2} \\pmat{y} \\dmat[0]{1,2} \\admat{1,2} \\dmat{1,a&b\\\\c&d}$"))
"options:"; expanded(doc("\\usepackage[notrig,italicdiff,arrowdel]{physics}", "$\\sin(x) \\dv{f}{x} \\grad$"))
"in arguments:"; expanded(doc(p, "\\section{On $\\abs{x}$}\\begin{figure}\\caption{$\\ket{0}$}\\end{figure}\\footnote{$\\mqty(\\imat{2})$}"))
let physics_doc = run(doc(p, "$\\qty(\\frac{a}{b}) \\dv{f}{x} \\bra{\\phi}\\ket{\\psi} \\mqty(\\imat{3}) \\sin[2](x)$"))
"physics renders:"; count(physics_doc.html, "<svg") == 1 and not has(physics_doc.html, "latex-unsupported")
"physics issues:"; physics_doc.issues
"physics and siunitx:"; run(doc("\\usepackage{physics,siunitx}", "x")).issues

// ---- mathtools: paired delimiters, macros, environments, tag forms ----
let m = "\\usepackage{mathtools}\n\\DeclarePairedDelimiter\\ceil{\\lceil}{\\rceil}\n" ++
    "\\DeclarePairedDelimiterX\\set[2]{\\{}{\\}}{#1 \\;\\delimsize\\vert\\; #2}"
"paired delimiters:"; expanded(doc(m, "$\\ceil{x} \\ceil*{x} \\ceil[\\big]{x} \\set{a}{b} \\set*{a}{b} \\set[\\Bigg]{a}{b}$"))
"macros:"; expanded(doc(m, "$\\prescript{14}{6}{C} \\splitfrac{a}{b} \\adjustlimits\\lim_{n}\\sup_{m} \\smashoperator{\\sum_i} \\cramped{x}$"))
"move left:"; expanded(doc(m, "\\begin{align}\\MoveEqLeft[3] a \\\\ &= b\\end{align}"))
let tags = run(doc("\\usepackage{mathtools}\\newtagform{brackets}[\\textbf]{[}{]}\\newtagform{plain}{<}{>}",
    "\\begin{equation}a\\label{a}\\end{equation}\\usetagform{brackets}\\begin{equation}b\\label{b}\\end{equation}" ++
    "\\usetagform{plain}\\begin{align}c\\\\d\\tag{x}\\end{align}\\usetagform{default}\\begin{equation}e\\end{equation}" ++
    "\\eqref{a} \\eqref{b}"))
"default tag:"; has(tags.html, "<span class=\"latex-eq-number\">(1)</span>")
"tag form:"; has(tags.html, "<span class=\"latex-eq-number\"><span>[<span style=\"font-weight:bold\">2</span>]</span></span>")
"align tag form:"; has(tags.html, "<span class=\"latex-align-number\">&lt;3&gt;</span>") and has(tags.html, "&lt;x&gt;")
"form ends:"; has(tags.html, "<span class=\"latex-eq-number\">(5)</span>")
"eqref forms:"; has(tags.html, "href=\"#a\">(1)</a>") and has(tags.html, "href=\"#b\"><span>[<span style=\"font-weight:bold\">2</span>]</span></a>")
"tag issues:"; tags.issues
let tools = run(doc("\\usepackage{mathtools}\\mathtoolsset{showonlyrefs}\\newtagform{odd}[\\scshape]{(}{)}",
    "$a \\coloneqq b$ \\[\\begin{psmallmatrix}a\\end{psmallmatrix} \\begin{dcases*}x & if $x>0$\\end{dcases*} " ++
    "\\begin{multlined}a\\\\b\\end{multlined} \\begin{rgathered}a\\end{rgathered} \\begin{unknownenv}x\\end{unknownenv}\\]"))
"mathtools renders:"; count(tools.html, "<svg") == 2
"mathtools issues:"; tools.issues

// ---- bm, cancel, nicefrac, xfrac ----
let shapes = run(doc("\\usepackage{bm,cancel,nicefrac,xfrac}",
    "$\\bm{x} \\cancelto{0}{x} \\nicefrac{1}{2}$ and \\nicefrac{3}{4} or \\sfrac{5}{6} in text."))
"shapes render:"; count(shapes.html, "<svg") == 3 and has(shapes.html, "data-math-kind=\"cancelto\"") and
    count(shapes.html, "data-math-kind=\"slanted-fraction\"") == 3
"shapes issues:"; shapes.issues

// ---- mhchem ----
let chem = run(doc("\\usepackage[version=4]{mhchem}",
    "Water is \\ce{H2O}, $\\ce{2H2 + O2 -> 2H2O}$, \\ce{Na+(aq)} and \\pu{123 kJ/mol}."))
"chemistry renders:"; count(chem.html, "<svg") == 4
"chemistry issues:"; chem.issues
