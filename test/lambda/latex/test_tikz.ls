// Native TikZ source, bounded syntax, drawing, plotting, and LaTeX handoff.
import tikz: lambda.doc.tikz.tikz
import expr: lambda.doc.tikz.expression
import latex: lambda.latex.latex

let source = "\\begin{tikzpicture}\n" ++
    "  \\draw[blue] (0,0) -- (1,1);\n" ++
    "  \\node at (1,1) {$x^2$};\n" ++
    "\\end{tikzpicture}"
let parsed = parse(source, {type: "tikz"})^
let rendered = tikz.render(source)^
let drawn = format(rendered, 'xml')

let plot_source = "\\begin{tikzpicture}\n" ++
    "\\begin{axis}[xmin=0,xmax=2,ymin=0,ymax=4,width=8cm,height=5cm,xlabel={$x$},grid=major]\n" ++
    "\\addplot[blue] coordinates {(0,0) (1,1) (2,4)};\n" ++
    "\\addlegendentry{$x^2$}\n" ++
    "\\addplot+[only marks] coordinates {(0,0) (2,4)};\n" ++
    "\\end{axis}\n\\end{tikzpicture}"
let plot = tikz.render(plot_source)^
let plot_xml = format(plot, 'xml')

let tex = "\\documentclass{article}\n\\usepackage{pgfplots}\n" ++
    "\\begin{document}\n" ++ plot_source ++ "\n\\end{document}"
let doc = parse(tex, {type: "latex"})^
let documents = [for (child in doc
    where child is element and string(name(child)) == "document") child]
let islands = [for (child in documents[0]
    where child is element and string(name(child)) == "tikzpicture") child]
let island = islands[0]
let latex_html = latex.render_to_html(doc, null)
let roundtrip = format(doc, 'latex');

let expression_source = "\\begin{axis}[domain=-2:2,samples=5]" ++
    "\\addplot[blue] {x^2};\\end{axis}"
let expression_ast = parse(expression_source, {type: "tikz"})^
let expression_plot = tikz.render(expression_source)^
let trig_source = "\\begin{axis}[domain=0:180,samples=3]" ++
    "\\addplot {sin(x)};\\end{axis}"
let trig_ast = parse(trig_source, {type: "tikz"})^
let commented_source = "\\begin{tikzpicture}% \\end{tikzpicture} ignored\n" ++
    "\\draw (0,0) % -- (99,99) ignored\n -- (1,1);\\end{tikzpicture}"
let commented_picture = parse(commented_source, {type: "tikz"})^;
let escaped_percent_tex = "\\begin{tikzpicture}\\draw (0,0) -- (1,1);" ++
    "\\% \\end{tikzpicture}"
let escaped_percent_doc = parse(escaped_percent_tex, {type: "latex"})^;

[
    string(name(parsed)) == "tikz_picture",
    string(name(parsed[0])) == "tikzpicture",
    parsed[0][0][1].x == 0.0,
    parsed[0][0][2].y == 1.0,
    contains(drawn, "<path"),
    contains(drawn, "tikz-label"),
    contains(drawn, "lambda-math"),
    contains(plot_xml, "tikz-axis"),
    // the plot has one SVG root plus a math SVG for its axis label and legend.
    len(split(plot_xml, "<svg")) == 4,
    contains(plot_xml, "x-axis"),
    contains(plot_xml, "x-grid"),
    contains(plot_xml, "tikz-legend-entry"),
    contains(plot_xml, "<circle"),
    string(name(island)) == "tikzpicture",
    contains(island.raw_source, "\\addplot+[only marks]"),
    contains(roundtrip, "\\addplot+[only marks]"),
    contains(latex_html, "tikz-axis"),
    contains(format(tikz.render("\\draw (0,0) circle (1);")^, 'xml'), "<ellipse"),
    contains(tikz.render("\\begin{axis}[unknown=1]\\addplot coordinates {(0,0) (1,1)};\\end{axis}") ^ { ^.message },
        "unsupported TikZ/PGFPlots option"),
    expr.evaluate(expression_ast[0][2][1], 2.0)^ == 4.0,
    contains(format(expression_plot, 'xml'), "tikz-axis"),
    contains(format(tikz.render(trig_source)^, 'xml'), "tikz-axis"),
    abs(expr.evaluate(trig_ast[0][2][0], 90.0)^ - 1.0) < 1e-12,
    contains(tikz.render("\\begin{axis}[samples=1]\\addplot {x};\\end{axis}") ^ { ^.message },
        "samples must be an integer"),
    contains(tikz.render("\\begin{axis}\\addplot {mystery(x)};\\end{axis}") ^ { ^.message },
        "unknown PGFPlots function"),
    contains(tikz.render("\\begin{semilogyaxis}\\addplot coordinates {(1,-1) (2,2)};\\end{semilogyaxis}") ^ { ^.message },
        "log axis requires positive coordinates"),
    contains(format(tikz.render("\\begin{axis}[xmin=0,xmax=1]\\addplot coordinates {(0,0) (2,1)};\\end{axis}")^, 'xml'),
        "<path"),
    string(name(commented_picture[0])) == "tikzpicture",
    len(commented_picture[0]) == 1,
    contains(format(tikz.render(commented_source)^, 'xml'), "<path"),
    contains(format(escaped_percent_doc, 'latex'), "\\% \\end{tikzpicture}")
]
