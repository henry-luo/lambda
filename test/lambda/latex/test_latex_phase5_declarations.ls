// Phase V M2 (vibe/impl/Lambda_Impl_Latex_Packages.md §6): packages whose
// commands already render, math symbol and alphabet tables, and the small
// pifont, marvosym, placeins, indentfirst and bookmark adapters.
import latex: lambda.latex.latex

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let result = latex.render_result(ast, null)
    {html: latex.render_to_html(ast, null), stylesheet: result.stylesheet,
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null

// ---- catalogue rows ----
"declared packages:"; run(doc("\\usepackage{textcomp,gensymb,nameref,epstopdf,amscd,cancel,latexsym," ++
    "stmaryrd,esint,upgreek,bbm,dsfont,placeins,indentfirst,pifont,marvosym}", "Hi.")).issues

// ---- gensymb text and math forms ----
let gensymb = run(doc("\\usepackage{gensymb}", "20\\degree C, 5\\celsius, 3\\ohm, 2\\micro m, 1\\perthousand. " ++
    "$90\\degree + 5\\celsius + \\ohm + \\micro + \\perthousand$"))
"gensymb text:"; has(gensymb.html, "20\u00B0 C") and has(gensymb.html, "5\u2103") and has(gensymb.html, "3\u2126")
"gensymb issues:"; gensymb.issues

// ---- math symbols and alphabets ----
let symbols = run(doc("\\usepackage{amssymb,latexsym,stmaryrd,esint,upgreek,bbm,dsfont,amscd}",
    "$\\Box \\Diamond A \\lhd B \\unrhd C \\leadsto D \\mho \\Join \\sqsubset \\square \\lozenge \\leqslant$\n" ++
    "$\\llbracket x \\rrbracket \\lightning \\shortrightarrow \\Mapsto \\boxast \\bigsqcap$\n" ++
    "$$\\fint f \\ointclockwise g \\sqint h$$\n" ++
    "$\\upalpha \\upbeta \\Upgamma \\triangleleft \\diamond \\bigtriangleup$\n" ++
    "$\\mathbbm{1}_A \\mathds{R}^n {\\cal L}$\n" ++
    "$$\\begin{CD} A @>f>> B \\\\ @VgVV @VVhV \\\\ C @>>k> D \\end{CD}$$"))
"math issues:"; symbols.issues
"upright greek:"; has(symbols.html, "font-style=\"normal\">\u03B1")
"calligraphic switch:"; has(symbols.html, "KaTeX_Caligraphic")

// ---- placeins ----
"float barrier:"; run(doc("\\usepackage[section]{placeins}", "A\\FloatBarrier B")).issues
"barrier needs placeins:"; run(doc("", "A\\FloatBarrier B")).issues

// ---- indentfirst ----
"indentfirst css:"; has(run(doc("\\usepackage{indentfirst}", "\\section{S}Text")).stylesheet,
    "h2 + p,.latex-document h3 + p")
"no indentfirst css:"; has(run(doc("", "\\section{S}Text")).stylesheet, "{text-indent:1.5em;}")

// ---- bookmark ----
let marks = run(doc("\\usepackage{bookmark}", "\\bookmark[level=section]{Plain}\\bookmark[dest=x]{Target}"))
"bookmark outline:"; has(marks.html, "data-pdf-outline-title=\"Plain\" data-pdf-outline-level=\"1\"")
"bookmark issues:"; marks.issues

// ---- pifont ----
let dings = run(doc("\\usepackage{pifont}", "\\ding{51} \\ding{55} \\ding{999}\n" ++
    "\\begin{dinglist}{43}\\item a\\item[x] b\\end{dinglist}\n" ++
    "\\begin{dingautolist}{172}\\item one\\item two\\end{dingautolist}"))
"ding glyphs:"; has(dings.html, "<span class=\"latex-ding\">\u2713</span>") and has(dings.html, "\u2717")
"dinglist labels:"; has(dings.html, "latex-item-label\">\u261E</span>") and has(dings.html, "latex-item-label\">x </span>")
"dingautolist labels:"; has(dings.html, "latex-item-label\">\u2460</span>") and has(dings.html, "latex-item-label\">\u2461</span>")
"ding issues:"; dings.issues

// ---- marvosym ----
let marvo = run(doc("\\usepackage{marvosym}", "\\Letter\\ \\EUR\\ \\Mobilefone"))
"marvosym glyphs:"; has(marvo.html, "\u2709") and has(marvo.html, "\u20AC")
"marvosym issues:"; marvo.issues
