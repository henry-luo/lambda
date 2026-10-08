// Phase IV §9.6: groupplots (\begin{groupplot}[group style={group size=C by R}]
// with \nextgroupplot), including inherited axis defaults and LaTeX handoff.
import tikz: lambda.doc.tikz.tikz
import plots: lambda.doc.tikz.pgfplots
import latex: lambda.latex.latex

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn option_text(node) => [for (child in node
    where child is element and string(name(child)) == "option")
    if (child.value == "") child.key else child.key ++ "=" ++ child.value]

fn failure(source) => tikz.render(source) ^ { ^.message }

fn parse_failure(source) => (parse(source, {type: "tikz"}) ^ { ^.message }) ^ { ^.message }

let picture = "\\begin{tikzpicture}" ++
    "\\begin{groupplot}[group style={group size=2 by 2, horizontal sep=2cm}," ++
    " width=5cm, height=4cm]" ++
    "\\nextgroupplot[title=A]\\addplot coordinates {(0,0) (1,1)};" ++
    "\\nextgroupplot[ybar, enlarge x limits=0.5]\\addplot coordinates {(1,2) (2,3)};" ++
    "\\nextgroupplot\\addplot table {x y\n0 1\n4 2\n};\\addlegendentry{t}" ++
    "\\end{groupplot}\\end{tikzpicture}"
// Inherited `every axis` defaults precede each member's group and own options.
let source = "\\pgfplotsset{every axis/.append style={grid=major}}" ++ picture
let tree = children_named(parse(source, {type: "tikz"})^, "tikzpicture")[0]
let group = children_named(tree, "groupplot")[0]
let members = children_named(group, "axis")
let html = format(tikz.render(source)^, 'xml')
let domains = [for (part in split(html, "data-x-domain=\"") where not starts_with(part, "<"))
    slice(part, 0, index_of(part, "\""))]

let document = latex.render_to_html(parse("\\documentclass{article}\\usepackage{pgfplots}" ++
    "\\usepgfplotslibrary{groupplots}\\begin{document}" ++ picture ++ "\\end{document}",
    {type: "latex"})^, {standalone: false});

[group.options_source, len(members)];
[for (member in members) option_text(member)];
[for (member in members) len(children_named(member, "plot"))];
[contains(html, "tikz-groupplot"), contains(html, "data-group-size=\"2 by 2\""),
    contains(html, "grid-template-columns:repeat(2,auto);column-gap:75.5906px;row-gap:37.7953px"),
    len(split(html, "class=\"tikz-axis\"")) - 1, contains(html, "width:188.976px")];
domains;
[for (member in members) (let plan = plots.resolve_axis(member, null, null, ["group style"])^,
    [for (series in plan.series) series.kind])];
[contains(document, "tikz-groupplot"), not contains(document, "latex-tikz-unsupported")];
[failure("\\begin{tikzpicture}\\begin{groupplot}[group style={group size=1 by 1}]" ++
        "\\nextgroupplot\\addplot coordinates {(0,0)};\\nextgroupplot" ++
        "\\addplot coordinates {(0,0)};\\end{groupplot}\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\begin{groupplot}\\nextgroupplot" ++
        "\\addplot coordinates {(0,0)};\\end{groupplot}\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\begin{groupplot}[group style={group size=2 by 1," ++
        " xlabels at=edge bottom}]\\nextgroupplot\\addplot coordinates {(0,0)};" ++
        "\\end{groupplot}\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\begin{groupplot}[group style={group size=0 by 1}]" ++
        "\\nextgroupplot\\addplot coordinates {(0,0)};\\end{groupplot}\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\begin{groupplot}[group style={group size=2 by 1}]" ++
        "\\addplot coordinates {(0,0)};\\end{groupplot}\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\begin{groupplot}[group style={group size=2 by 1}]" ++
        "\\nextgroupplot\\addplot coordinates {(0,0)};\\end{tikzpicture}")]
