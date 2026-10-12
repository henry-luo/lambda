// Regressions found in test/input/latex-showcase.tex: calc's \setlength under
// the TeX engine, the multicols preface, \/ and \slash, framed pictures, and
// textgreek/textcomp/gensymb symbols.
import latex: lambda.latex.latex
import html: lambda.latex.to_html

fn render(body) {
    let doc = "\\documentclass{article}\n\\usepackage{calc,multicol,textgreek,textcomp,gensymb}\n" ++
        "\\begin{document}\n" ++ body ++ "\n\\end{document}"
    let result = latex.render_result(latex.parse_source(doc, null), null);
    // textgreek, textcomp and gensymb are not catalogued packages; only unsupported output counts here
    {html: html.to_html(result.elements),
     unsupported: [for (issue in result.diagnostics where issue.code == "unsupported-output") issue.message]}
}

// calc redefines \setlength; inside a passed-through \frame argument it must
// neither leak assignment code nor lose the unit length the picture uses
let framed = render("\\frame{\\setlength{\\unitlength}{2mm}\n\\begin{picture}(10,5)\\put(0,0){\\line(1,0){10}}\\end{picture}}")
// at top level the redefined \setlength is still echoed for the script
let top = render("\\setlength{\\unitlength}{2mm}\n\\begin{picture}(10,5)\\put(0,0){\\circle{2}}\\end{picture}")
let multicols = render("\\begin{multicols}{3}[\\subsection{Spread}]\nBody text.\n\\end{multicols}")
let plain_cols = render("\\begin{multicols}{2}\nOnly body.\n\\end{multicols}")
let ligatures = render("f\\/i and a\\slash b")
let symbols = render("\\textomikron \\textOmikron \\perthousand \\textcopyright")
let boxes = render("\\noindent\\fbox{\\verb|\\mbox{|x\\verb|}|}\n\n\\framebox[2cm][r]{Bummer, I am too wide}")

;[
    {name: "framed picture has no unsupported output", ok: framed.unsupported == []},
    {name: "framed picture uses the frame's unit length", ok: contains(framed.html, "viewBox=\"0 0 200 100\"")},
    {name: "framed picture draws its line", ok: contains(framed.html, "<line")},
    {name: "frame is a box around the picture", ok: contains(framed.html, "class=\"latex-frame\" style=\"display:inline-block;")},
    {name: "top-level calc setlength reaches the picture", ok: contains(top.html, "viewBox=\"0 0 200 100\"")},
    {name: "multicols preface spans the columns", ok: contains(multicols.html,
        "<div class=\"latex-multicols-preface\"><h3")},
    {name: "multicols preface heading is numbered", ok: contains(multicols.html, "<span class=\"sec-num\">0.1 </span>Spread")},
    {name: "multicols uses its column count", ok: contains(multicols.html, "column-count:3")},
    {name: "multicols header is not body text", ok: not contains(multicols.html, "3[") and not contains(multicols.html, "]\n")},
    {name: "multicols without preface", ok: contains(plain_cols.html, "column-count:2") and
        not contains(plain_cols.html, "latex-multicols-preface")},
    {name: "italic correction renders nothing", ok: contains(ligatures.html, "fi and a") and ligatures.unsupported == []},
    {name: "slash renders a breakable solidus", ok: contains(ligatures.html, "<span class=\"latex-slash\">/<wbr></span>")},
    {name: "textgreek, textcomp and gensymb symbols", ok: symbols.unsupported == [] and contains(symbols.html, "ο Ο ‰ ©")},
    {name: "noindent removes the paragraph indent", ok: contains(boxes.html, "<p style=\"text-indent:0\">")},
    {name: "a sized box is its width from the outside", ok: contains(boxes.html, "box-sizing:border-box")},
    {name: "[r] content overflows to the left", ok: contains(boxes.html, "left:100%;transform:translateX(-100%)")}
]
