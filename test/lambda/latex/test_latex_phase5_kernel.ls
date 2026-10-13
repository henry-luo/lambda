// Phase V M1 (vibe/impl/Lambda_Impl_Latex_Packages.md §5): kernel, hyperref and
// graphicx commands that were unsupported although no package is missing.
import latex: lambda.latex.latex

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let options = {base_uri: "test/lambda/latex/fixtures"}
    {html: latex.render_to_html(ast, options),
     issues: [for (issue in latex.render_result(ast, options).diagnostics)
        issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null

// ---- text commands and NFSS declarations ----
let text = run(doc("\\linespread{1.3}",
    "\\textup{u} \\textmd{m} \\textnormal{n} x\\textsuperscript{2} H\\textsubscript{2}O \\ensuremath{\\alpha}\n" ++
    "{\\fontfamily{ptm}\\selectfont T} {\\usefont{T1}{phv}{b}{it} H} {\\fontseries{zz}\\selectfont Z} " ++
    "{\\normalfont N} \\mathversion{bold}"))
"text commands:"; has(text.html, "<span class=\"latex-up\">u</span>") and has(text.html, "<sup>2</sup>") and
    has(text.html, "<sub>2</sub>") and has(text.html, "<title>\\alpha</title>")
"nfss family:"; has(text.html, "font-family:'Times New Roman',Times,serif")
"nfss usefont:"; has(text.html, "font-family:Helvetica,Arial,sans-serif;font-weight:bold;font-style:italic")
"preamble linespread:"; has(text.html, "line-height:1.56")
"text issues:"; text.issues

// ---- rules, fills and page commands ----
let fills = run(doc("", "Left\\hfill Right\n\nA\\dotfill B\n\nX \\rule[-1pt]{2cm}{0.4pt} \\centerline{c} \\indent y " ++
    "\\nopagebreak\\samepage\\enlargethispage{1em}\\raggedbottom\\sloppy\\vfill\\null\\cleardoublepage " ++
    "\\hspace{\\stretch{1}} \\markboth{Left}{Right}\\markright{Again} \\pagenumbering{roman}\\pagenumbering{arabic}"))
"fill lines:"; len(split(fills.html, "class=\"latex-fill-line\"")) - 1
"rule:"; has(fills.html, "width:2cm;height:0.398506pt;vertical-align:-0.996264pt")
"marks:"; has(fills.html, "string-set:latex-chapter &quot;Left&quot;,latex-section &quot;Right&quot;")
"fill issues:"; fills.issues

// ---- columns ----
let columns = run("\\documentclass[twocolumn]{article}\n\\begin{document}\n\\twocolumn[{Wide}]\n" ++
    "\\begin{figure*}\\caption{F}\\end{figure*}\nText\n\\end{document}\n")
"twocolumn:"; has(columns.html, "latex-document latex-article latex-twocolumn") and
    has(columns.html, "<div class=\"latex-span-all\">") and has(columns.html, "latex-figure latex-span-all")
"one-column switch:"; run(doc("", "\\twocolumn")).issues

// ---- title block ----
let title = run(doc("\\title{T\\thanks{Funded}}\\author{A \\and B\\thanks{Email}}", "\\maketitle\\begin{titlepage}P\\end{titlepage}"))
"thanks marks:"; has(title.html, "T<sup class=\"latex-thanks-ref\">*</sup>") and
    has(title.html, "<p><sup>\u2020</sup> Email</p>")
"author separator:"; has(title.html, "<span class=\"latex-and\">")
"titlepage:"; has(title.html, "latex-titlepage")
"stray thanks:"; run(doc("", "x\\thanks{y}")).issues

// ---- footnotes ----
let notes = run(doc("", "a\\footnotemark{} b\\footnotemark[5] \\footnotetext{first}\\footnotetext[5]{five} " ++
    "\\footnote{same}\\footnote{same}"))
"footnote refs:"; [for (n in ["1", "5", "2", "3"]) has(notes.html, "href=\"#fn-" ++ n ++ "\" id=\"fnref-" ++ n)]
"explicit list values:"; has(notes.html, "<li id=\"fn-5\" value=\"5\">") and has(notes.html, "<li id=\"fn-2\" value=\"2\">")
"note text:"; has(notes.html, "five ") and not has(notes.html, ">5five")

// ---- contents and hyperref ----
let links = run(doc("\\usepackage{hyperref}", "\\tableofcontents\\listoffigures\n" ++
    "\\section{S \\texorpdfstring{$x^2$}{x squared}}\\phantomsection\\addcontentsline{toc}{section}{Extra}" ++
    "\\addcontentsline{lof}{figure}{Loose}\\addtocontents{toc}{\\protect\\newpage}\n" ++
    "\\hypertarget{t1}{Target} \\hyperlink{t1}{go}"))
"contents entry:"; has(links.html, "<a href=\"#extra\">Extra</a>") and has(links.html, "<a id=\"extra\">")
"list entry:"; has(links.html, "<a href=\"#loose\">Loose</a>")
"outline title:"; has(links.html, "data-pdf-outline-title=\"S x squared\"")
"hyperlinks:"; has(links.html, "<span id=\"t1\">Target</span>") and has(links.html, "<a href=\"#t1\">go</a>")
"link issues:"; links.issues

// ---- environments ----
let envs = run(doc("", "\\begin{tabbing}\nab \\= cd \\\\\n\\> x \\\\\nz \\kill\n\\end{tabbing}\n" ++
    "\\begin{eqnarray}\na &=& b \\label{e1}\\\\\nc &\\leq& d \\nonumber\n\\end{eqnarray} See \\ref{e1}.\n" ++
    "\\begin{list}{--}{}\n\\item one\n\\item[x] two\n\\end{list}\n\\begin{trivlist}\\item plain\\end{trivlist}\\nocite{k}"))
"tabbing rows:"; len(split(envs.html, "<tr>")) - 1
"tabbing cells:"; has(envs.html, "<td> cd </td>") and has(envs.html, "<td> x </td>")
"eqnarray columns:"; has(envs.html, "text-align:right") and has(envs.html, "text-align:center") and
    has(envs.html, "text-align:left")
"eqnarray numbering:"; len(split(envs.html, "latex-align-number")) - 1
"eqnarray ref:"; has(envs.html, "href=\"#e1\">1</a>")
"list labels:"; has(envs.html, "<span class=\"latex-item-label\">\u2013</span>") and
    has(envs.html, "latex-item-label\">x </span>")
"environment issues:"; envs.issues

// ---- graphicx boxes ----
let boxes = run(doc("\\usepackage{graphicx}", "\\resizebox{\\textwidth}{!}{X} \\rotatebox[origin=c]{90}{Y} " ++
    "\\scalebox{1.5}[2]{Z} \\reflectbox{W} " ++
    "\\rotatebox{30}{\\includegraphics[page=1,width=100pt]{cropped_rotated_pages.pdf}}"))
"resizebox:"; has(boxes.html, "latex-resizebox\" style=\"display:inline-block;max-width:100%;width:100%;\"")
"rotatebox:"; has(boxes.html, "transform:rotate(-90deg);transform-origin:center center")
// a lone image turns through its own angle key, so its layout box is exact
"rotated image:"; has(boxes.html, "rotate(-30deg)")
"scalebox:"; has(boxes.html, "transform:scale(1.5,2);transform-origin:left bottom")
"reflectbox:"; has(boxes.html, "transform:scaleX(-1)")
"box issues:"; boxes.issues
