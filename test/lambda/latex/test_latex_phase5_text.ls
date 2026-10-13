// Phase V M3 (vibe/impl/Lambda_Impl_Latex_Packages.md §7): text and paragraph
// packages: xspace, soul, ulem, setspace, parskip, ragged2e, lipsum, blindtext,
// paralist, fancyvrb, minted, framed, mdframed, fancybox, boxedminipage,
// footmisc and appendix.
import latex: lambda.latex.latex

fn doc(preamble, body, docclass = "article") =>
    "\\documentclass{" ++ docclass ++ "}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let result = latex.render_result(ast, null)
    {html: latex.render_to_html(ast, null), css: result.stylesheet,
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null
fn count(text, part) => len(split(text, part)) - 1

// ---- xspace: a space unless punctuation, a group or a space follows ----
let spaced = run(doc("\\usepackage{xspace}\\newcommand\\eg{e.g.\\xspace}\\newcommand\\TT{TT\\xspace}",
    "An \\eg example, \\eg. And \\eg, too (\\TT) \\TT{} \\TT~y.\\footnote{see \\eg this}"))
"xspace:"; has(spaced.html, "An e.g. example, e.g.. And e.g., too (TT) TT TT\u00A0y.")
"xspace in a note:"; has(spaced.html, "see e.g. this")
"xspace issues:"; spaced.issues

// ---- soul: highlight, underline, strike, letterspacing, caps and colors ----
let soul = run(doc("\\usepackage{soul,xcolor}\\sethlcolor{cyan}",
    "\\hl{high} \\ul{under} \\st{struck} \\so{spaced} \\caps{Caps} \\setulcolor{red}\\ul{red} \\texthl{alias}"))
"soul hl:"; has(soul.html, "<span class=\"latex-hl\" style=\"background-color:#00bcd4\">high</span>")
"soul so and caps:"; has(soul.html, "letter-spacing:0.25em") and has(soul.html, "font-variant:small-caps")
"soul colors:"; has(soul.html, "text-decoration-color:#d32f2f\">red</span>") and
    count(soul.html, "background-color:#00bcd4") == 2
"soul issues:"; soul.issues

// ---- ulem: decorations, \emph underlined until \normalem ----
let ulem = run(doc("\\usepackage{ulem}",
    "\\emph{e} \\uuline{uu} \\uwave{w} \\sout{s} \\xout{x} \\dashuline{d} \\normalem\\emph{i} \\ULforem\\emph{again}"))
"ulem emph:"; has(ulem.html, "<span class=\"latex-uline\" style=\"text-decoration:underline\">e</span>") and
    has(ulem.html, "<em>i</em>") and has(ulem.html, ">again</span>")
"ulem lines:"; has(ulem.html, "underline double") and has(ulem.html, "underline wavy") and
    has(ulem.html, "underline dashed")
"ulem issues:"; ulem.issues
let normalem = run(doc("\\usepackage[normalem]{ulem}", "\\emph{italic}"))
"normalem option:"; has(normalem.html, "<em>italic</em>")

// ---- setspace: the option, environments and declarations ----
let spacing = run(doc("\\usepackage[onehalfspacing]{setspace}",
    "One.\n\n\\begin{doublespace}Two.\\end{doublespace}\n\n\\begin{spacing}{1.4}Three.\\end{spacing}\n\n\\doublespacing Five.\n\nSix."))
"onehalfspacing:"; has(spacing.html, "<article class=\"latex-document latex-article\" lang=\"en\" style=\"line-height:1.5")
"doublespace:"; has(spacing.html, "<div class=\"latex-doublespace\" style=\"line-height:2.0004\">")
"spacing:"; has(spacing.html, "<div class=\"latex-spacing\" style=\"line-height:1.68\"><p>Three.</p></div>")
"declaration holds:"; has(spacing.html, "<p style=\"line-height:2.0004\"> Five.</p>") and
    count(spacing.html, "line-height:2.0004") == 3
"setspace issues:"; spacing.issues

// ---- parskip and ragged2e ----
let parskip = run(doc("\\usepackage[skip=10pt]{parskip}", "A\n\nB"))
"parskip:"; has(parskip.css, "text-indent:0;margin:9.96264pt 0;")
let ragged = run(doc("\\usepackage{ragged2e}",
    "\\begin{FlushRight}right\\end{FlushRight}\n\\begin{justify}just\\end{justify}"))
"ragged2e:"; has(ragged.html, "<div class=\"latex-FlushRight\" style=\"text-align:right;hyphens:auto\">") and
    has(ragged.html, "text-align:justify;hyphens:auto")
"ragged2e issues:"; ragged.issues

// ---- lipsum and blindtext: bundled public-domain filler ----
let filler = run(doc("\\usepackage{lipsum,blindtext}", "\\lipsum[2-3]\n\nIn text \\lipsum*[1] end.\n\n\\blindtext"))
"filler blocks:"; count(filler.html, "class=\"latex-filler\"")
"filler issues:"; filler.issues

// ---- paralist ----
let lists = run(doc("\\usepackage{paralist}",
    "\\begin{compactenum}[(a)]\\item x \\item y\\end{compactenum}\n" ++
    "Text \\begin{inparaenum}[(i)]\\item first \\item second\\end{inparaenum} after.\n" ++
    "\\begin{asparaenum}\\item Para one. \\item Para two.\\end{asparaenum}\n" ++
    "\\begin{enumerate}[{Step} 1:]\\item go\\end{enumerate}"))
"compactenum:"; has(lists.html, ">(b)</span>")
"inparaenum:"; has(lists.html, "Text <span class=\"latex-inpara\">") and has(lists.html, ">(ii)</span> second</span></span> after.")
"asparaenum:"; has(lists.html, "<p class=\"latex-runin-item\"><span class=\"latex-item-label\">2.</span> Para two.</p>")
"enumerate template:"; has(lists.html, "<span class=\"latex-item-label\">Step 1:</span> go")
"paralist issues:"; lists.issues

// ---- fancyvrb and minted through the listings renderer ----
let verbatim = run(doc("\\usepackage{fancyvrb}\\fvset{frame=single}",
    "\\begin{Verbatim}[numbers=left,commandchars=\\\\\\{\\}]\nx = 1 % not a comment\ny = {2}\n\\end{Verbatim}\n" ++
    "Inline \\Verb|a_b%c| and \\Verb*{x y}."))
"Verbatim:"; has(verbatim.html, "<span class=\"latex-listing-source\">x = 1 % not a comment</span>") and
    has(verbatim.html, "border:1px solid currentColor")
"Verb:"; has(verbatim.html, ">a_b%c</code>") and has(verbatim.html, ">x y</code>")
"fancyvrb issues:"; verbatim.issues
let minted = run(doc("\\usepackage{minted}",
    "\\begin{listing}\\begin{minted}[linenos,frame=lines]{python}\ndef f(x):\n    return x % 2\n\\end{minted}\\caption{Py}\\end{listing}\n" ++
    "Use \\mintinline{c}{int x = {0};} or \\mint{bash}|echo $HOME|\n\\listoflistings"))
"minted:"; has(minted.html, "<code class=\"language-python\">") and has(minted.html, "Listing 1: ")
"mintinline:"; has(minted.html, "<code class=\"latex-code language-c\" style=\"font-family:monospace;\">int x = {0};</code>")
"mint:"; has(minted.html, "<code class=\"language-bash\">echo $HOME</code>")
"list of listings:"; has(minted.html, "List of Listings")
"minted issues:"; minted.issues

// ---- framed, mdframed, fancybox and boxedminipage on the frame-box model ----
let frames = run(doc("\\usepackage{framed,mdframed,fancybox,boxedminipage,xcolor}\\definecolor{shadecolor}{rgb}{0.9,0.9,0.9}" ++
    "\\mdfdefinestyle{warn}{linecolor=red,linewidth=2pt}\\newmdenv[backgroundcolor=yellow,topline=false]{note}",
    "\\begin{framed}F\\end{framed}\\begin{shaded}S\\end{shaded}\\begin{leftbar}L\\end{leftbar}\n" ++
    "\\begin{mdframed}[style=warn,frametitle={Title},roundcorner=4pt,foo=1]M\\end{mdframed}\n\\begin{note}N\\end{note}\n" ++
    "\\shadowbox{sb} \\doublebox{db} \\Ovalbox{OB}\n\\begin{boxedminipage}{3cm}BM\\end{boxedminipage}"))
"framed:"; has(frames.html, "<div class=\"latex-framed\" style=\"border:0.4pt solid;padding:9pt;\">") and
    has(frames.html, "background-color:rgb(230,230,230)") and has(frames.html, "border-left:3pt solid")
"mdframed style:"; has(frames.html, "border:1.99253pt solid #d32f2f") and
    has(frames.html, "<div class=\"latex-mdframed-title\">Title</div>")
"newmdenv:"; has(frames.html, "background-color:#fbc02d;border-right:") and not has(frames.html, "border-top:0.4pt")
"fancybox:"; has(frames.html, "box-shadow:4pt 4pt 0 currentColor") and has(frames.html, "border:3px double") and
    has(frames.html, "border:0.8pt solid;padding:3pt;border-radius:0.6em")
"boxedminipage:"; has(frames.html, "<div class=\"latex-boxedminipage\" style=\"display:inline-block;vertical-align:middle;width:3cm;")
"frame issues:"; frames.issues

// ---- footmisc: symbol marks, the rule and \footref ----
let notes = run(doc("\\usepackage[symbol,norule,perpage]{footmisc}",
    "One\\footnote{First\\label{fn:a}} two\\footnote{Second} again\\footref{fn:a}, \\ref{fn:a}, \\footref{nope}."))
"symbol marks:"; has(notes.html, "id=\"fnref-1\">*</a>") and has(notes.html, "id=\"fnref-2\">\u2020</a>")
"footref:"; has(notes.html, "again<sup class=\"latex-footnote-ref\"><a href=\"#fn:a\">*</a></sup>") and
    has(notes.html, "<a class=\"latex-ref\" href=\"#fn:a\">*</a>")
"symbol list:"; has(notes.html, "<ol style=\"list-style:none\"><li id=\"fn-1\"><sup class=\"latex-footnote-mark\">*</sup> First")
"norule:"; not has(notes.html, "<hr>")
"footmisc issues:"; notes.issues
let plain_note = run(doc("", "One\\footnote{First\\label{x}} \\ref{x}."))
"label in a note:"; has(plain_note.html, "<a class=\"latex-ref\" href=\"#x\">1</a>")

// ---- appendix ----
let report = run(doc("\\usepackage[toc,page,title,titletoc]{appendix}",
    "\\tableofcontents\\chapter{Intro}Text\\section{Sec}\n" ++
    "\\begin{subappendices}\\section{Proof}\\label{s:p}\\subsection{Detail}\\end{subappendices}\n" ++
    "\\begin{appendices}\\chapter{Extra}\\label{c:e}\\ref{c:e} \\ref{s:p}\\section{Sub}\\end{appendices}\n" ++
    "\\chapter{After}\\begin{appendices}\\chapter{Second}\\end{appendices}", "report"))
"subappendices:"; has(report.html, "<h2 id=\"proof\"><span class=\"sec-num\">1.A </span>Proof</h2>") and
    has(report.html, "<span class=\"sec-num\">1.A.1 </span>Detail")
"appendices title:"; has(report.html, "<h1 id=\"extra\"><span class=\"sec-num\">Appendix A </span>Extra</h1>") and
    has(report.html, "<span class=\"sec-num\">A.1 </span>Sub")
"appendix refs:"; has(report.html, "href=\"#c:e\">A</a>") and has(report.html, "href=\"#s:p\">1.A</a>")
"numbering resumes:"; has(report.html, "<span class=\"sec-num\">2 </span>After")
"letters continue:"; has(report.html, "<span class=\"sec-num\">Appendix B </span>Second")
"toc head:"; count(report.html, "<li class=\"toc-l1\"><a href=\"#appendices") == 2 and
    has(report.html, "<span class=\"sec-num\">Appendix A</span>Extra")
"appendix page:"; count(report.html, "<h1 class=\"latex-appendix-page\">Appendices</h1>") == 2
"appendix issues:"; report.issues
let article = run(doc("\\usepackage{appendix}",
    "\\section{Intro}\\begin{subappendices}\\subsection{Lemma}\\end{subappendices}\n" ++
    "\\appendixpage\\addappheadtotoc\\begin{appendices}\\section{Extra}\\end{appendices}\\section{Back}"))
"article subappendices:"; has(article.html, "<span class=\"sec-num\">1.A </span>Lemma")
"article appendices:"; has(article.html, "<span class=\"sec-num\">A </span>Extra") and
    has(article.html, "<span class=\"sec-num\">2 </span>Back")
"head anchor:"; has(article.html, "<a id=\"appendices\"></a>")
"article issues:"; article.issues
