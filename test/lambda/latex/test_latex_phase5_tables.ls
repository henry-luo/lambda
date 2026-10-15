// Phase V M5 (vibe/impl/Lambda_Impl_Latex_Packages.md §9): tables and page inserts:
// vertical rules and \newcolumntype, colortbl, hhline, dcolumn, threeparttable,
// adjustbox, pdfpages and the csvsimple engine binding.
import latex: lambda.doc.latex.latex

let BASE = "test/lambda/latex/fixtures"

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, BASE) ^ { null }
    let result = latex.render_result(ast, {base_uri: BASE})
    {html: latex.render_to_html(ast, {base_uri: BASE}),
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null
fn count(text, part) => len(split(text, part)) - 1

// ---- the column preamble: vertical rules and \newcolumntype ----
let ruled = run(doc("\\usepackage{array}\\newcolumntype{Y}{>{\\bfseries}r}\\newcolumntype{P}[1]{p{#1}}",
    "\\begin{tabular}{|l||Y|P{2cm}|}a & b & c\\end{tabular}"))
"single rule:"; has(ruled.html, "<td style=\"text-align: left;border-left:1px solid black;\">a</td>")
"double rule and new type:"; has(ruled.html, "<td style=\"text-align: right;border-left:3px double black;\">b</td>")
"new type with argument:"; has(ruled.html, "text-align: left;width:2cm;border-left:1px solid black;border-right:1px solid black;")
"preamble issues:"; ruled.issues

// ---- colortbl: column, row and cell colors and rule colors ----
let colored = run(doc("\\usepackage{colortbl}\\definecolor{soft}{HTML}{EEEEEE}",
    "\\arrayrulecolor{blue}\\begin{tabular}{|>{\\columncolor{soft}}l|c|}\\hline a & b\\\\" ++
    "\\rowcolor[gray]{0.5} c & \\cellcolor[HTML]{FF0000}d\\\\ \\multicolumn{2}{|c|}{\\cellcolor{yellow}e}\\\\" ++
    "\\arrayrulecolor{red}\\hline\\end{tabular}\\doublerulesepcolor{green}"))
"column color:"; has(colored.html, "border-left:1px solid #1976d2;background-color:#EEEEEE;\">a</td>")
"row over column:"; has(colored.html, "<tr style=\"background-color:rgb(128,128,128)\"><td style=\"text-align: left;border-left:1px solid #1976d2;\">c</td>")
"cell over row:"; has(colored.html, "background-color:#FF0000;\">d</td>")
"multicolumn cell color:"; has(colored.html, "<td colspan=\"2\" style=\"text-align: center;border-left:1px solid #1976d2;border-right:1px solid #1976d2;background-color:#fbc02d;\">e</td>")
"rule colors:"; has(colored.html, "<tr class=\"latex-colored-rule\"><td colspan=\"2\" style=\"border-top:1px solid #1976d2\">") and
    has(colored.html, "border-top:1px solid #d32f2f")
"colortbl issues:"; colored.issues
let xcolor_table = run(doc("\\usepackage[table]{xcolor}", "\\begin{tabular}{l}\\cellcolor{red}x\\end{tabular}"))
"xcolor table loads colortbl:"; has(xcolor_table.html, "background-color:#d32f2f;\">x</td>")

// ---- hhline ----
let lines = run(doc("\\usepackage{hhline}", "\\begin{tabular}{lll}a & b & c\\\\\\hhline{=~-}d & e & f\\\\\\hhline{*{3}{-}}\\end{tabular}"))
"hhline segments:"; has(lines.html, "<tr class=\"latex-hhline\"><td style=\"border-top:3px double black\"></td><td></td><td style=\"border-top:1px solid black\"></td></tr>")
"hhline repeat:"; count(lines.html, "border-top:1px solid black") == 4

// ---- dcolumn on the decimal grid ----
let decimals = run(doc("\\usepackage{dcolumn}\\newcolumntype{d}[1]{D{.}{\\cdot}{#1}}",
    "\\begin{tabular}{d{2}D{,}{,}{1.1}}1.5 & 3,2\\\\ 12.25 & 10,5\\\\ \\multicolumn{1}{c}{n} & \\end{tabular}"))
"dcolumn grid:"; has(decimals.html, "grid-template-columns:2ch 1ch 2ch;\"><span class=\"latex-si-whole\">12</span><span class=\"latex-si-point\">\u00B7</span><span class=\"latex-si-fraction\">25</span>")
"dcolumn comma:"; has(decimals.html, "<span class=\"latex-si-point\">,</span><span class=\"latex-si-fraction\">5</span>")
"dcolumn issues:"; decimals.issues

// ---- threeparttable ----
let notes = run(doc("\\usepackage{threeparttable}",
    "\\begin{table}\\begin{threeparttable}\\caption{T}\\begin{tabular}{ll}a\\tnote{a} & b\\tnote{b}\\end{tabular}" ++
    "\\begin{tablenotes}\\item[a] First note.\\item[b] Second.\\end{tablenotes}\\end{threeparttable}\\end{table}" ++
    "\\begin{tablenotes}[para,flushleft]\\item[c] C.\\item[d] D.\\end{tablenotes}"))
"tnote:"; has(notes.html, "a<sup class=\"latex-tnote\">a</sup>")
"wrapper:"; has(notes.html, "<div class=\"latex-threeparttable\"><div class=\"latex-caption latex-table-caption\"")
"notes:"; has(notes.html, "<div class=\"latex-tablenote\"><sup class=\"latex-tnote\">a</sup> First note.</div>")
"para notes:"; has(notes.html, "<div class=\"latex-tablenotes\" style=\"--latex-tnote-hang:0;\"><p><sup class=\"latex-tnote\">c</sup> C.")
"threeparttable issues:"; notes.issues

// ---- adjustbox ----
let boxes = run(doc("\\usepackage{adjustbox,xcolor}",
    "\\adjustbox{width=2cm,frame,bgcolor=yellow,valign=t}{Box}\n\\begin{adjustbox}{center,fbox}X\\end{adjustbox}\n" ++
    "\\adjustbox{width=3cm}{\\includegraphics{cropped_rotated_pages.pdf}}\n\\adjustbox{foo=1}{Y}"))
"adjustbox frame:"; has(boxes.html, "<span class=\"latex-adjustbox\" style=\"display:inline-block;width:2cm;vertical-align:top;background-color:#fbc02d;border:0.4pt solid;\">Box</span>")
"adjustbox environment:"; has(boxes.html, "<div class=\"latex-adjustbox-center\" style=\"text-align:center\"><div class=\"latex-adjustbox\" style=\"display:inline-block;border:0.4pt solid;padding:3pt;\"><p>X</p></div></div>")
"adjustbox image:"; has(boxes.html, "<span class=\"latex-adjustbox\" style=\"display:inline-block;\"><span class=\"latex-image-frame latex-image-width\" style=\"display:inline-block;vertical-align:middle;width:3cm;\">")
"adjustbox issues:"; boxes.issues

// ---- pdfpages ----
let pages = run(doc("\\usepackage{pdfpages}",
    "\\includepdf[pages={1,{},2},scale=0.5,frame,nup=2x1]{cropped_rotated_pages.pdf}\\includepdf[pages=-]{cropped_rotated_pages.pdf}"))
"pages:"; count(pages.html, "<div class=\"latex-pdfpage\"") == 5 and count(pages.html, "<svg") == 4
"blank page:"; has(pages.html, "<div class=\"latex-pdfpage\" style=\"border:0.4pt solid\"></div>")
"scaled:"; has(pages.html, "width:50%;")
"pdfpages issues:"; pages.issues

// ---- csvsimple on the engine ----
let csv = run(doc("\\usepackage{csvsimple,booktabs}\\newcommand\\hi[1]{\\textbf{#1}}" ++
    "\\csvstyle{menu}{separator=semicolon, respect all, tabular=lll, table head=Item & Price & Note\\\\}",
    "\\csvautotabular{phase5_people.csv}\n\n" ++
    "\\csvreader[tabular=|l|r|, table head=\\hline Name & Age\\\\\\hline, table foot=\\\\\\hline]{phase5_people.csv}{name=\\name, age=\\age}{\\hi{\\name} & \\age}\n\n" ++
    "\\csvreader[filter not equal={\\name}{Bob}, head to column names]{phase5_people.csv}{}{\\thecsvrow:~\\name\\ is \\age. }\n\n" ++
    "\\csvreader[menu]{phase5_menu.csv}{}{\\csvcoli & \\csvcolii & \\csvcoliii}\n\n" ++
    "\\csvautobooktabular{phase5_people.csv}"))
"autotabular:"; has(csv.html, "<td style=\"text-align: left;border-left:1px solid black;\">name</td><td style=\"text-align: left;border-left:1px solid black;border-right:1px solid black;\">age</td>")
"reader rows:"; has(csv.html, "<strong>Cy</strong></td><td style=\"text-align: right;border-left:1px solid black;border-right:1px solid black;\">27</td>")
"rows only:"; count(csv.html, "<strong>") == 3
"filter:"; has(csv.html, "1:\u00A0Ann is 31. 2:\u00A0Cy is 27.")
"respect all:"; has(csv.html, "<td>Tea &amp; cake</td><td>$3</td><td>50% off_now</td>") and has(csv.html, "<td>{x}</td>")
"autobooktabular:"; has(csv.html, "<tr class=\"latex-toprule\"></tr><tr><td>name</td><td>age</td></tr><tr class=\"latex-midrule\"></tr>")
"csvsimple issues:"; csv.issues
