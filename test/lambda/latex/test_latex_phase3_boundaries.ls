import latex: lambda.latex.latex
import util: lambda.latex.util
import html: lambda.latex.to_html

fn render(source, options = null) => latex.render_result(parse(source, "latex") ^ { null }, options)
fn issue(result, code) => any([for (entry in result.diagnostics) entry.code == code])

let accents = render("\\newcommand \\course{Caf\\'e}\\course \\H o \\v{c}")
let missing = render("\\cite{missing}")
let flags = render("\\usepackage[numbers=maybe]{natbib}\\bibliographystyle{plainnat}")
let conflict = render("\\usepackage{natbib,biblatex}")
let thai = render("\\usepackage{polyglossia}\\setdefaultlanguage{thai}ไทย", {target: "pdf"})
let counter = render("\\usepackage{lastpage}\\pageref{LastPage}", {target: "html"})
let long_table = render("\\usepackage{longtable}\\begin{longtable}{l}\\caption{First}\\label{t}\\\\ A\\\\\\end{longtable}\\ref{t}")
let style = html.to_html(<style ".a > svg{content:'</style>'}">) ^ { "" }
let stretch = render("\\usepackage{xltabular}\\begin{xltabular}{\\textwidth}{|l|X|}Left&Right\\\\\\end{xltabular}")
let tree = render("\\usepackage{bussproofs}\\begin{prooftree}\\def\\fCenter{\\mbox{\\ $\\Rightarrow$\\ }}\\AxiomC{$A$}\\UnaryInfC{$B$}\\end{prooftree}")
let spring = render("\\usepackage{tikz}\\begin{tikzpicture}\\draw[decoration={aspect=0.3,segment length=1.5mm,amplitude=3mm,coil},decorate](0,0)--(0,2);\\end{tikzpicture}")
let spring_html = html.to_html(spring.body) ^ { "" }
let aligned = render("\\usepackage{amsmath}\\begin{equation*}\\begin{split}a&=b\\\\&=c\\end{split}\\end{equation*}")
let aligned_html = html.to_html(aligned.body) ^ { "" }
let assets = render("\\usepackage{biblatex}\\begin{filecontents*}{inline.bib}\n@book{k,title={Local},year={2020}}\n\\end{filecontents*}\\addbibresource{inline.bib}\\addbibresource{absent.bib}\\includegraphics{absent.png}",
    {base_uri: "test/latex/samples"}).assets
{accents: contains(util.text_of(accents.body), "Café ő č"),
 unresolved: issue(missing, "unresolved-citation"),
 invalid_flags: issue(flags, "unsupported-natbib-profile"),
 conflicting_packages: issue(conflict, "conflicting-bibliography-packages"),
 thai_limit: issue(thai, "complex-script-layout-approximation"),
 page_limit: issue(counter, "unresolved-page-counter"),
 table_number: contains(util.text_of(long_table.body), "Table 1: First") and
    contains(util.text_of(long_table.body), "1"),
 raw_stylesheet: contains(style, ".a > svg") and contains(style, "\\3c /style"),
 table_header: not contains(util.text_of(stretch.body), "\\textwidth") and
    not contains(util.text_of(stretch.body), "|l|X|"),
 proof_definition: not issue(tree, "unsupported-output") and
    contains(html.to_html(tree.body) ^ { "" }, "latex-proof-premises"),
 coil_geometry: len(spring_html) > 0 and not contains(spring_html, "TikZ picture could not") and
    len(split(spring_html, " L")) > 30,
 nested_alignment: not contains(aligned_html, "col_sep") and
    contains(aligned_html, "lm_mtable") and contains(aligned_html, "col-align-r"),
 resource_resolution: len(assets) == 3 and all([for (asset in assets) asset.offset is int]) and
    assets[0].source == "test/latex/samples/absent.png" and assets[0].available == false and
    assets[1].origin == "inline" and assets[1].available == true and
    assets[2].source == "test/latex/samples/absent.bib" and assets[2].available == false}
