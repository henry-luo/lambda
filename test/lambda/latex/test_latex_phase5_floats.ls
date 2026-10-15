// Phase V M4 (vibe/impl/Lambda_Impl_Latex_Packages.md §8): float kinds from
// packages and \newfloat, algorithms (algpseudocode, algorithmic, algorithm2e),
// subfig, wrapfig, rotating and landscape.
import latex: lambda.doc.latex.latex

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src, target = null) {
    let ast = latex.parse_source(src, "test/lambda/latex/fixtures") ^ { null }
    let options = if (target == null) null else {target: target}
    {html: latex.render_to_html(ast, null),
     issues: [for (issue in latex.render_result(ast, options).diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null
fn count(text, part) => len(split(text, part)) - 1

// ---- placement options no longer leak into the float ----
let placed = run(doc("", "\\begin{figure}[htbp]X\\caption{C}\\end{figure}"))
"placement dropped:"; has(placed.html, "<p>X</p>") and not has(placed.html, "[htbp]")

// ---- the float package ----
let program = run(doc("\\usepackage{float}\\floatstyle{boxed}\\newfloat{program}{thp}{lop}[section]" ++
    "\\floatname{program}{Program}\\floatstyle{plaintop}\\restylefloat{table}",
    "\\section{S}\\listof{program}{List of Programs}\\begin{program}[H]P\\caption{Prog}\\label{p}\\end{program}" ++
    "\\ref{p}\\begin{table}[H]\\caption{T}\\end{table}"))
"newfloat caption:"; has(program.html, "Program 1.1: ")
"newfloat style:"; has(program.html, "latex-float latex-float-program latex-float-style-boxed")
"listof:"; has(program.html, "<div class=\"toc-title\">List of Programs</div>") and has(program.html, "href=\"#p\"")
"restylefloat:"; has(program.html, "latex-table-wrapper latex-float-style-plaintop")
"float issues:"; program.issues

// ---- algorithm with algpseudocode ----
let pseudo = run(doc("\\usepackage{algorithm,algpseudocode}\n\\algrenewcommand\\algorithmicrequire{\\textbf{Input:}}",
    "\\listofalgorithms\\begin{algorithm}[H]\\caption{Euclid}\\label{alg:e}\\begin{algorithmic}[1]\n" ++
    "\\Require $a, b$\n\\Procedure{Euclid}{$a,b$}\n\\State $r\\gets a \\bmod b$ \\Comment{remainder}\n" ++
    "\\While{$r\\not=0$}\n\\State $a \\gets b$\n\\EndWhile\\label{line:w}\n\\State \\Return $b$ \\Call{Gcd}{$a$}\n" ++
    "\\EndProcedure\n\\end{algorithmic}\\end{algorithm}\nSee \\ref{alg:e}, line \\ref{line:w}."))
"algorithm float:"; has(pseudo.html, "latex-float latex-float-algorithm latex-float-style-ruled") and
    has(pseudo.html, "Algorithm 1: ")
"list of algorithms:"; has(pseudo.html, "List of Algorithms")
"numbered lines:"; count(pseudo.html, "class=\"latex-alg-number\">") == 8 and has(pseudo.html, ">7:</span>")
"nesting:"; has(pseudo.html, "padding-left:3em")
"keywords:"; has(pseudo.html, "<strong class=\"latex-alg-keyword\">end</strong> <strong class=\"latex-alg-keyword\">while</strong>")
"require override:"; has(pseudo.html, "<span class=\"latex-alg-keyword\"><strong>Input:</strong></span>")
"comment and call:"; has(pseudo.html, "\u25B7 remainder") and has(pseudo.html, "<span class=\"latex-alg-name\">Gcd</span>")
"line reference:"; has(pseudo.html, "href=\"#line:w\">5</a>")
"pseudocode issues:"; pseudo.issues

// ---- the older algorithmic package ----
let old = run(doc("\\usepackage{algorithmic}", "\\begin{algorithmic}\\STATE $x$ \\IF{$x$} \\STATE y \\ENDIF \\RETURN z \\end{algorithmic}"))
"algorithmic keywords:"; has(old.html, ">if</strong>") and has(old.html, ">then</strong>") and has(old.html, ">return</strong>")
"algorithmic issues:"; old.issues

// ---- algorithm2e ----
let a2e = run(doc("\\usepackage[ruled,linesnumbered]{algorithm2e}",
    "\\begin{algorithm}\\caption{Two}\\label{a2}\\KwIn{x}\\KwOut{y}\n\\eIf{$x>0$}{a\\;}{b\\;}\n" ++
    "\\For{$i=1$ \\KwTo $n$}{c\\;\\tcp{note}}\n\\Repeat{$k=0$}{d\\;}\n\\KwRet{$y$}\\;\n\\end{algorithm}\nSee \\ref{a2}."))
// two io lines, if/a/else/b/end, for/c/comment/end, repeat/d/until, return
"algorithm2e lines:"; count(a2e.html, "class=\"latex-alg-line\"") == 15 and has(a2e.html, ">15</span>")
"algorithm2e io:"; has(a2e.html, ">Input:</strong> x")
"algorithm2e blocks:"; has(a2e.html, ">else</strong>") and has(a2e.html, ">until</strong>") and has(a2e.html, ">to</strong>")
"algorithm2e statements:"; has(a2e.html, "a;</div>") and has(a2e.html, "// note")
"algorithm2e reference:"; has(a2e.html, "href=\"#a2\">1</a>")
"algorithm2e issues:"; a2e.issues
let quiet = run(doc("\\usepackage[noend]{algorithm2e}\\DontPrintSemicolon",
    "\\begin{algorithm}\\If{$x$}{a\\;}\\end{algorithm}"))
"no semicolon, no end:"; has(quiet.html, ">a</div>") and not has(quiet.html, ">end</strong>")
"algorithm conflict:"; run(doc("\\usepackage{algorithm,algorithm2e}", "x")).issues

// ---- subfig and subfigure ----
let subs = run(doc("\\usepackage{subfig}",
    "\\begin{figure}\\subfloat[First\\label{s1}]{A}\\subfloat[List][Second]{B}\\caption{Both}\\end{figure} \\subref{s1}"))
"subfloat captions:"; has(subs.html, "(a) </span><span class=\"latex-caption-text\" style=\"\">First") and
    has(subs.html, "(b) </span><span class=\"latex-caption-text\" style=\"\">Second")
"subfloat reference:"; has(subs.html, "<a href=\"#s1\">a</a>")
"subfig issues:"; subs.issues
"subfigure command:"; has(run(doc("\\usepackage{subfigure}",
    "\\begin{figure}\\subfigure[One]{X}\\caption{F}\\end{figure}")).html, "(a) ")
"subfig conflict:"; run(doc("\\usepackage{subfig,subcaption}", "x")).issues

// ---- wrapfig ----
let wrap = run(doc("\\usepackage{wrapfig}", "\\begin{wrapfigure}{l}[1pt]{0.4\\textwidth}W\\caption{Wrap}\\end{wrapfigure}" ++
    "\\begin{wraptable}{r}{3cm}T\\caption{WT}\\end{wraptable}"))
"wrapfigure:"; has(wrap.html, "latex-wrapfigure\" style=\"float:left;width:40%;")
"wraptable:"; has(wrap.html, "float:right;width:3cm;") and has(wrap.html, "Table 1: ")
"wrap issues:"; wrap.issues

// ---- rotating and landscape ----
let turned = run(doc("\\usepackage{rotating,pdflscape}",
    "\\begin{sidewaysfigure}S\\caption{Side}\\end{sidewaysfigure}\\begin{turn}{45}R\\end{turn}" ++
    "\\begin{landscape}L\\end{landscape}"), "pdf")
"sideways float:"; has(turned.html, "Figure 1: ")
"turn:"; has(turned.html, "transform:rotate(-45deg)")
"rotating issues:"; turned.issues
