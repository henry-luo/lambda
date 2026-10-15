// Phase V M6 (vibe/impl/Lambda_Impl_Latex_Packages.md §10): front matter, headings
// and contents: titlesec, sectsty, tocloft, minitoc, titling, authblk and varioref.
import latex: lambda.doc.latex.latex

fn doc(docclass, preamble, body) =>
    "\\documentclass{" ++ docclass ++ "}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src, options = null) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let result = latex.render_result(ast, options)
    {html: latex.render_to_html(ast, null),
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null

// ---- titlesec ----
let titled = run(doc("report", "\\usepackage[sf,compact]{titlesec}\\usepackage{xcolor}" ++
    "\\titleformat{\\chapter}[display]{\\normalfont\\huge\\bfseries\\filcenter}{\\chaptertitlename\\ \\thechapter}{20pt}{\\Huge\\MakeUppercase}[\\titlerule]" ++
    "\\titleformat{\\section}{\\Large\\bfseries\\color{blue}}{\\thesection.}{1em}{}\\titleformat*{\\subsection}{\\large\\itshape}" ++
    "\\titlespacing{\\section}{0pt}{*4}{*1.5}\\titleformat{\\subsubsection}[runin]{\\bfseries}{}{0pt}{\\foo}",
    "\\chapter{Intro}Text\\section{Sec}Body\\subsection{Sub}\\subsubsection{Run}In\\appendix\\chapter{Extra}"))
"display chapter:"; has(titled.html, "<span class=\"sec-num\" style=\"display:block;margin-bottom:19.9253pt;\">Chapter 1</span><span class=\"latex-title-text\" style=\"font-size:2.2em;text-transform:uppercase;\">Intro</span>")
"chapter format:"; has(titled.html, "font-size:1.8em;font-weight:bold;text-align:center;border-bottom:0.4pt solid;")
"hang section:"; has(titled.html, "<span class=\"sec-num\" style=\"margin-right:1em;\">1.1.</span>Sec</h2>")
"section style:"; has(titled.html, "<h2 id=\"sec\" class=\"latex-indent-after\" style=\"font-family:var(--latex-sans,sans-serif);margin-top:0.8em;margin-bottom:0.4em;font-size:1.3em;font-weight:bold;color:#1976d2;margin-left:0pt;margin-top:4ex;margin-bottom:1.5ex;\">")
"starred format keeps the label:"; has(titled.html, "font-size:1.1em;font-style:italic;\"><span class=\"sec-num\">1.1.1 </span>Sub</h3>")
"appendix chapter name:"; has(titled.html, ">Appendix A</span>")
"titlesec issues:"; titled.issues

// ---- sectsty ----
let fonts = run(doc("article", "\\usepackage{sectsty,xcolor}\\allsectionsfont{\\sffamily}\\sectionfont{\\color{red}}",
    "\\section{S}\\subsection{T}"))
"sectsty:"; has(fonts.html, "<h2 id=\"s\" style=\"color:#d32f2f;\">") and has(fonts.html, "<h3 id=\"t\" style=\"font-family:var(--latex-sans,sans-serif);\">")
"sectsty issues:"; fonts.issues

// ---- tocloft and minitoc; \contentsname and \protect\numberline in the kernel ----
let toc = run(doc("report", "\\usepackage{tocloft,minitoc}" ++
    "\\renewcommand{\\cftchapfont}{\\bfseries\\sffamily}\\renewcommand{\\cftsecpresnum}{S}\\renewcommand{\\cftsecaftersnum}{.}" ++
    "\\renewcommand{\\cftsecleader}{\\cftdotfill{\\cftdotsep}}\\cftsetindents{sec}{2em}{3em}\\setlength{\\cftbeforechapskip}{1em}" ++
    "\\renewcommand{\\contentsname}{Table of Contents}\\renewcommand{\\cfttoctitlefont}{\\Huge\\bfseries}" ++
    "\\newlistof{example}{exp}{List of Examples}\\renewcommand{\\mtctitle}{In this chapter}\\setcounter{minitocdepth}{1}",
    "\\dominitoc\\tableofcontents\\listofexample\\chapter{One}\\minitoc\\section{A}\\subsection{A1}\\section{B}" ++
    "\\refstepcounter{example}\\addcontentsline{exp}{example}{\\protect\\numberline{\\theexample}First example}" ++
    "\\addcontentsline{toc}{section}{\\protect\\numberline{9}Extra entry}\\chapter{Two}\\section{C}"))
"toc title:"; has(toc.html, "<div class=\"toc-title\" style=\"font-size:2.2em;font-weight:bold;\">Table of Contents</div>")
"entry font and skip:"; has(toc.html, "<li class=\"toc-l1\" style=\"font-weight:bold;font-family:var(--latex-sans,sans-serif);margin-top:1em;\">")
"entry number:"; has(toc.html, "<li class=\"toc-l2\" style=\"padding-left:2em;\"><a href=\"#a\"><span class=\"sec-num\" style=\"display:inline-block;min-width:3em\">S1.1.</span>A</a></li>")
"numberline after protect:"; has(toc.html, "<span class=\"sec-num\" style=\"display:inline-block;min-width:3em\">S9.</span>Extra entry")
"newlistof:"; has(toc.html, "<nav class=\"latex-toc latex-list-of latex-listofexample\"><div class=\"toc-title\">List of Examples</div><ul><li class=\"toc-l2\"><a href=\"#first-example\"><span class=\"sec-num\">1</span>First example</a>")
"minitoc:"; has(toc.html, "<nav class=\"latex-toc latex-minitoc\"><div class=\"toc-title\">In this chapter</div><ul><li class=\"toc-l2\"") and
    not has(toc.html, "latex-minitoc\"><div class=\"toc-title\">In this chapter</div><ul><li class=\"toc-l2\" style=\"padding-left:2em;\"><a href=\"#a\"><span class=\"sec-num\" style=\"display:inline-block;min-width:3em\">S1.1.</span>A</a></li><li class=\"toc-l3\"")
"contents issues:"; toc.issues

// ---- titling and authblk ----
let front = run(doc("article", "\\usepackage[auth-sc]{authblk}\\usepackage{titling}" ++
    "\\title{T}\\author[1,2]{Ann \\textsc{Lee}}\\author[2]{Bob}\\author[1]{Cy}\\affil[1]{Uni X}\\affil[2]{Lab \\& Co}" ++
    "\\pretitle{\\begin{center}\\LARGE}\\posttitle{\\par\\end{center}\\vskip 0.5em}\\setlength{\\droptitle}{-4em}" ++
    "\\renewcommand\\Affilfont{\\itshape\\small}\\renewcommand{\\maketitlehookb}{\\centering Draft}\\date{2026}",
    "\\maketitle\nSee \\thetitle."))
"droptitle:"; has(front.html, "<header class=\"latex-title\" style=\"margin-top:-4em\">")
"pre and post code:"; has(front.html, "<div class=\"title\" style=\"font-size:1.5em;text-align:center;margin-bottom:0.5em;\">T</div>")
"hook:"; has(front.html, "<div class=\"latex-title-hook\"><div class=\"latex-centering\" style=\"text-align:center\"> Draft</div></div>")
"authors:"; has(front.html, "<div class=\"latex-authors\" style=\"font-variant:small-caps;\">Ann <span class=\"latex-sc\">Lee</span><sup class=\"latex-affil-mark\">1,2</sup>, Bob<sup class=\"latex-affil-mark\">2</sup>, and Cy<sup class=\"latex-affil-mark\">1</sup></div>")
"affiliations:"; has(front.html, "<div class=\"latex-affiliations\" style=\"font-style:italic;font-size:0.85em;\"><div class=\"latex-affil\"><sup class=\"latex-affil-mark\">1</sup> Uni X</div>")
"thetitle:"; has(front.html, "See <span class=\"latex-thetitle\">T</span>.")
"front issues:"; front.issues
let blocks = run(doc("article", "\\usepackage{authblk}\\title{T}\\author{Ann}\\author{Bob}\\affil{Uni X}\\author{Cy}\\affil{Lab}", "\\maketitle"))
"author blocks:"; has(blocks.html, "<div class=\"latex-authblock\"><div class=\"latex-authors\">Ann and Bob</div><div class=\"latex-affil\">Uni X</div></div><div class=\"latex-authblock\"><div class=\"latex-authors\">Cy</div>")

// ---- varioref ----
let refs = doc("article", "\\usepackage{varioref}",
    "\\section{A}\\label{s:a}\\section{B}\\label{s:b}See \\vref{s:a}, \\Vpageref{s:a}, \\vrefrange{s:a}{s:b}, \\vref{nope}.")
let continuous = run(refs)
"vref:"; has(continuous.html, "<span class=\"latex-vref\"><a class=\"latex-ref\" href=\"#s:a\">1</a> on page <a class=\"latex-target-page\" href=\"#s:a\"></a></span>")
"vrefrange:"; has(continuous.html, "<a class=\"latex-ref\" href=\"#s:b\">2</a> on pages <a class=\"latex-target-page\" href=\"#s:a\"></a>\u2013<a class=\"latex-target-page\" href=\"#s:b\"></a>")
"continuous issues:"; continuous.issues
"paged issues:"; run(refs, {target: "pdf", paged: true}).issues
let named = run(doc("article", "\\usepackage{varioref,cleveref}", "\\section{A}\\label{s:a}See \\vref{s:a}."))
"with cleveref:"; has(named.html, "<span class=\"latex-cref-group\">section\u00A0<a class=\"latex-ref latex-cref\" href=\"#s:a\">1</a></span></span> on page")
