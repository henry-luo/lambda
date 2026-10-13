// Phase V M9 (vibe/impl/Lambda_Impl_Latex_Packages.md §13): fonts: the text font
// packages, fontspec, unicode-math and fontawesome5. No face beyond Computer and
// Latin Modern is bundled (§17.3), so every other face is a reported substitution.
import latex: lambda.latex.latex

fn doc(preamble, body) =>
    "\\documentclass{article}\n" ++ preamble ++ "\n\\begin{document}\n" ++ body ++ "\n\\end{document}\n"

fn run(src) {
    let ast = latex.parse_source(src, ".") ^ { null }
    let result = latex.render_result(ast, null)
    {html: latex.render_to_html(ast, null), css: result.stylesheet,
     issues: [for (issue in result.diagnostics) issue.code ++ " " ++ string(issue.item)]}
}

fn has(text, part) => index_of(text, part) != null

// ---- the families read the root's variables ----
let plain = run(doc("", "\\textsf{s} \\texttt{t} {\\ttfamily d}"))
"stylesheet variables:"; has(plain.css, "--latex-mono: 'Computer Modern Typewriter', 'Latin Modern Mono', monospace;") and
    has(plain.css, "code.latex-code {\n  font-family: var(--latex-mono, monospace);")
"declarations read them:"; has(plain.html, "<span class=\"latex-ttfamily\" style=\"font-family:var(--latex-mono,monospace)\">")

// ---- the text font packages ----
let times = run(doc("\\usepackage{times}", "x"))
"times families:"; has(times.html, "style=\"font-family:'TeX Gyre Termes','Times New Roman',Times,serif;--latex-serif:'TeX Gyre Termes','Times New Roman',Times,serif;--latex-sans:'TeX Gyre Heros',Helvetica,Arial,sans-serif;--latex-mono:'TeX Gyre Cursor','Courier New',Courier,monospace;\"")
"times issues:"; times.issues
"math packages:"; run(doc("\\usepackage{mathpazo}", "x")).issues
"mono package:"; has(run(doc("\\usepackage{inconsolata}", "x")).html, "style=\"--latex-mono:Inconsolata,monospace;\"")
"sans as default:"; has(run(doc("\\usepackage[default]{sourcesanspro}", "x")).html,
    "style=\"font-family:'Source Sans Pro','Source Sans 3',sans-serif;--latex-serif:'Source Sans Pro','Source Sans 3',sans-serif;--latex-sans:")

// ---- fontspec ----
let spec = run(doc("\\usepackage{fontspec}\\setmainfont[Numbers=OldStyle]{EB Garamond}\\setmonofont[Scale=0.9]{Fira Code}" ++
    "\\setsansfont{Latin Modern Sans}\\newfontfamily\\headingfont[Color=FF0000,Letters=SmallCaps]{Inter}" ++
    "\\newfontfamily\\oddfont[Path=./fonts/,Ligatures=Rare]{Odd}",
    "{\\headingfont Heading} plain {\\fontspec{Courier New} cn} \\texttt{x}"))
"setters:"; has(spec.html, "style=\"font-family:'EB Garamond',serif;--latex-serif:'EB Garamond',serif;--latex-sans:'Latin Modern Sans',sans-serif;--latex-mono:'Fira Code',monospace;font-variant-numeric:oldstyle-nums;\"")
"family switch:"; has(spec.html, "<span class=\"latex-fontswitch\" style=\"font-family:'Inter',serif;color:#FF0000;font-variant:small-caps;\">")
"fontspec switch:"; has(spec.html, "<span class=\"latex-fontswitch\" style=\"font-family:'Courier New',serif;\">")
"mono features:"; has(spec.css, ".latex-document .latex-ttfamily,.latex-document .latex-listing pre{font-size:0.9em;}")
"fontspec issues:"; spec.issues

// ---- unicode-math ----
let unimath = run(doc("\\usepackage[math-style=ISO]{unicode-math}\\setmathfont{XITS Math}",
    "$\\symbf{x} + \\symcal{L} + \\symbb{R} + \\symbfit{v}$"))
"unicode-math issues:"; unimath.issues
"bundled math face:"; run(doc("\\usepackage{unicode-math}\\setmathfont{Latin Modern Math}", "$x$")).issues

// ---- fontawesome5 ----
let icons = run(doc("\\usepackage{fontawesome5}", "\\faEnvelope{} \\faGithub{} \\faIcon{phone} \\faMapMarker"))
"icons:"; has(icons.html, "<span class=\"latex-icon\" title=\"envelope\">\u2709</span> <span class=\"latex-icon\" title=\"github\">[github]</span> <span class=\"latex-icon\" title=\"phone\">\u260E</span> <span class=\"latex-icon\" title=\"map-marker\">")
"icon issues:"; icons.issues
"no icons without the package:"; run(doc("", "\\faGithub")).issues
