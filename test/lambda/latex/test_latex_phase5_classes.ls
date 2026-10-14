// Phase V M10 (vibe/impl/Lambda_Impl_Latex_Packages.md §14): class profiles. A class
// gives a numbering base, an option schema and the commands it owns: KOMA-Script,
// the AMS classes and memoir; the journal classes IEEEtran, llncs, elsarticle,
// revtex and acmart; letter and exam with their own document models.
import latex: lambda.latex.latex

fn run(src) {
    let ast = latex.parse_source(src, ".") ^ { null }
    let result = latex.render_result(ast, null)
    {html: latex.render_to_html(ast, null), css: result.stylesheet, packages: [for (p in result.packages) p.key],
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.package) ++ " " ++ string(issue.item)]}
}

fn doc(cls, preamble, body) =>
    "\\documentclass" ++ cls ++ "\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn has(text, part) => index_of(text, part) != null

// ---- the class model ----
let ext = run(doc("[14pt,openany,bogus]{extbook}", "", "\\chapter{One}\\section{Two}"))
"extbook root and numbering:"; has(ext.html, "<article class=\"latex-document latex-extbook latex-book\"") and
    has(ext.html, "<span class=\"sec-num\">1.1 </span>Two")
"class options:"; ext.issues
"global option:"; run(doc("[english,11pt]{article}", "\\usepackage{babel}", "x")).issues
"implied packages:"; run(doc("{amsart}", "", "x")).packages
"unknown class:"; run(doc("{moderncv}", "", "x")).issues
"chapters in amsbook:"; [run(doc("{amsbook}", "\\let\\oldchapter\\chapter", "\\chapter{One}")).html]

// ---- KOMA-Script ----
let koma = run(doc("[parskip=half,DIV=12]{scrreprt}",
    "\\setkomafont{disposition}{\\bfseries}\\addtokomafont{section}{\\Large}\\setkomafont{pagehead}{\\small}" ++
    "\\KOMAoptions{headings=small,bogus=1}\\titlehead{Head}\\subject{Report}\\title{T}\\subtitle{Sub}\\author{A}" ++
    "\\publishers{Press}\\dedication{To all}",
    "\\maketitle\\tableofcontents\\addchap{Preface}\\chapter{C}\\section{S}\\minisec{Mini}x" ++
    "\\begin{figure}\\captionabove{Above}\\end{figure}"))
"KOMA title:"; has(koma.html, "<header class=\"latex-title\"><div class=\"latex-titlehead\">Head</div><div class=\"latex-subject\">Report</div><div class=\"title\">T</div><div class=\"latex-subtitle\">Sub</div><div class=\"author\">A</div><div class=\"latex-publishers\">Press</div></header><div class=\"latex-dedication\">To all</div>")
"KOMA headings:"; has(koma.html, "<h1 id=\"preface\" style=\"font-family:var(--latex-serif,serif);font-weight:normal;font-weight:bold;\">Preface</h1>") and
    has(koma.html, "font-weight:bold;font-size:1.3em;\"><span class=\"sec-num\">1.1 </span>S</h2>") and
    has(koma.html, "<div class=\"latex-minisec\" style=\"font-family:var(--latex-serif,serif);font-weight:normal;font-weight:bold;\">Mini</div>")
"KOMA contents:"; has(koma.html, "<a href=\"#preface\">Preface</a>")
"KOMA caption:"; has(koma.html, "Figure 1: </span><span class=\"latex-caption-text\" style=\"\">Above</span>")
"KOMA sans and parskip:"; has(koma.css, ".latex-koma h1,") and contains(koma.packages, "parskip")
"KOMA issues:"; koma.issues

// ---- the AMS classes ----
let ams = run(doc("[reqno]{amsart}", "\\title{T}\\author{A}\\address{Dept, Univ}\\email{a@u.edu}\\author{B}\\address{Lab}" ++
    "\\subjclass[2010]{11A41}\\keywords{primes}\\dedicatory{To C}",
    "\\begin{abstract}Abs\\end{abstract}\\maketitle\\section{S}x"))
"AMS:"; [ams.html]
"AMS issues:"; ams.issues

// ---- memoir ----
let memoir = run(doc("{memoir}", "\\chapterstyle{section}\\setlrmarginsandblock{1in}{1in}{*}\\newfloat[chapter]{program}{lop}{Program}",
    "\\chapter{C}\\epigraph{Words}{Someone}\\begin{program}\\caption{P}\\end{program}"))
"memoir:"; [memoir.html]
"memoir issues:"; memoir.issues
"unknown chapter style:"; run(doc("{memoir}", "\\chapterstyle{bianchi}", "\\chapter{C}")).issues

// ---- IEEEtran ----
let ieee = run(doc("[conference]{IEEEtran}", "",
    "\\title{T}\\author{\\IEEEauthorblockN{A B}\\IEEEauthorblockA{Univ\\\\City} \\and \\IEEEauthorblockN{C D}\\IEEEauthorblockA{Lab}}" ++
    "\\maketitle\\begin{abstract}Abs\\end{abstract}\\begin{IEEEkeywords}a, b\\end{IEEEkeywords}" ++
    "\\section{Intro}\\label{s:i}\\IEEEPARstart{T}{his} is.\\subsection{Sub}\\label{s:s}\\subsubsection{Deep}\\label{s:d}" ++
    "See \\ref{s:i}, \\ref{s:s}, \\ref{s:d}.\\begin{IEEEproof}Done.\\end{IEEEproof}"))
"IEEEtran:"; [ieee.html]
"IEEEtran issues:"; ieee.issues

// ---- llncs ----
let llncs = run(doc("[runningheads]{llncs}", "",
    "\\title{T}\\author{A\\inst{1}\\orcidID{0000-0001} \\and B\\inst{2}}\\institute{U1 \\email{a@u}\\and U2}" ++
    "\\titlerunning{T}\\authorrunning{A and B}\\maketitle\\begin{abstract}Abs\\keywords{one \\and two}\\end{abstract}" ++
    "\\begin{theorem}P\\end{theorem}\\begin{note}N\\end{note}"))
"llncs:"; [llncs.html]
"llncs issues:"; llncs.issues

// ---- elsarticle ----
let els = run(doc("[preprint,12pt]{elsarticle}", "\\journal{J Phys}",
    "\\begin{frontmatter}\\title{T\\tnoteref{t1}}\\tnotetext[t1]{Funded}\\author[a]{A\\corref{c1}}\\ead{a@x}" ++
    "\\author[a,b]{B\\fnref{f1}}\\cortext[c1]{Corresponding}\\fntext[f1]{Note}\\address[a]{U}\\address[b]{L}" ++
    "\\begin{abstract}Abs\\end{abstract}\\begin{keyword}k1 \\sep k2\\end{keyword}\\end{frontmatter}\\section{S}x"))
"elsarticle:"; [els.html]
"elsarticle issues:"; els.issues

// ---- revtex ----
let rev = run(doc("[aps,superscriptaddress]{revtex4-2}", "",
    "\\title{T}\\author{A}\\affiliation{U}\\email{a@u}\\author{B}\\affiliation{L}\\affiliation{U}" ++
    "\\begin{abstract}Abs\\end{abstract}\\keywords{k}\\maketitle\\section{S}x"))
"revtex superscript addresses:"; [rev.html]
"revtex grouped addresses:"; has(run(doc("{revtex4-2}", "", "\\title{T}\\author{A}\\author{B}\\affiliation{U}\\maketitle")).html,
    "<div class=\"latex-authblock\"><div class=\"latex-authors\">A and B</div><div class=\"latex-affil\">U</div></div>")

// ---- acmart ----
let acm = run(doc("[sigconf]{acmart}", "\\setcopyright{acmcopyright}\\acmDOI{10.1/2}",
    "\\title{T}\\author{A}\\affiliation{\\institution{U}\\city{C}\\country{X}}\\email{a@u}" ++
    "\\begin{abstract}Abs\\end{abstract}\\ccsdesc[500]{Computing~ML}\\ccsdesc[300]{Info~IR}\\keywords{k1, k2}" ++
    "\\begin{teaserfigure}\\caption{Teaser}\\end{teaserfigure}\\maketitle\\section{S}x\\begin{figure}\\caption{F}\\end{figure}"))
"acmart title:"; has(acm.html, "<div class=\"latex-affil\"><div class=\"latex-institution\">U</div><span class=\"latex-place\">C</span><span class=\"latex-place\">X</span></div><div class=\"latex-affil latex-email\">a@u</div>")
"acmart teaser first:"; has(acm.html, "</header><figure class=\"latex-figure\"><div class=\"latex-caption latex-figure-caption\" style=\"text-align:center;white-space:pre-line;\"><span class=\"latex-caption-label\" style=\"\">Figure 1: </span><span class=\"latex-caption-text\" style=\"\">Teaser</span></div></figure><div class=\"latex-abstract\">")
"acmart concepts and keywords:"; has(acm.html, "<div class=\"latex-ccsdesc\"><span class=\"latex-field-head\">CCS Concepts:</span> <span class=\"latex-ccs-concept\" style=\"font-weight:bold\">• Computing → ML; </span><span class=\"latex-ccs-concept\">• Info → IR.</span></div><div class=\"latex-keywords\"><span class=\"latex-field-head\">Keywords:</span> k1, k2</div>")
"acmart figures after the teaser:"; has(acm.html, "Figure 2: </span><span class=\"latex-caption-text\" style=\"\">F</span>")
"acmart issues:"; acm.issues

// ---- letter ----
let letter = run(doc("{letter}", "\\address{1 Road\\\\Town}\\signature{Me}\\date{1 May}",
    "\\begin{letter}{Them\\\\There}\\opening{Dear X,}Body.\\closing{Yours,}\\ps{PS: more}\\encl{CV}\\cc{Y}\\end{letter}"))
"letter:"; [letter.html]
"letter issues:"; letter.issues

// ---- exam ----
let exam_src = "Total: \\numpoints\\ \\points{} in \\numquestions\\ questions.\\begin{questions}\\question[2] What?" ++
    "\\begin{parts}\\part[1] A\\part[3] B\\end{parts}\\question Why?\\begin{choices}\\choice X\\CorrectChoice Y\\end{choices}" ++
    "\\begin{checkboxes}\\choice P\\CorrectChoice Q\\end{checkboxes}\\begin{solution}Because.\\end{solution}\\end{questions}"
"exam:"; [run(doc("{exam}", "", exam_src)).html]
"exam with answers:"; has(run(doc("[answers]{exam}", "", exam_src)).html,
    "<li class=\"latex-correct\"> Q</li></ol><div class=\"latex-solution\"><strong>Solution:</strong> <p>Because.</p></div>")
"printanswers:"; has(run(doc("{exam}", "\\printanswers\\bracketedpoints", exam_src)).html, "<span class=\"latex-points\">[2 points]</span>")
"exam issues:"; run(doc("{exam}", "\\firstpageheader{a}{b}{c}\\qformat{Q}", "\\part{Stray}")).issues

// ---- natbib without a bibliography has no style to approximate ----
"natbib alone:"; run(doc("{article}", "\\usepackage{natbib}", "x")).issues
