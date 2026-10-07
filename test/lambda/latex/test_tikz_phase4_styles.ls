// Phase IV §9.6: parameterized styles (.style with #1, .style 2 args, .default,
// .append style) and pic definitions (\tikzset{name/.pic={...}}, \pic {name}).
import tikz: lambda.doc.tikz.tikz
import latex: lambda.latex.latex

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn option_text(node) => [for (child in node
    where child is element and string(name(child)) == "option")
    if (child.value == "") child.key else child.key ++ "=" ++ child.value]

fn failure(source) => tikz.render(source) ^ { ^.message }

fn parse_failure(source) => (parse(source, {type: "tikz"}) ^ { ^.message }) ^ { ^.message }

let styled = parse("\\begin{tikzpicture}[box/.style={draw=#1, thick}, box/.default=blue," ++
    "pair/.style 2 args={draw=#1, fill=#2}, box/.append style={dashed}," ++
    "outer/.style={box=#1, fill=yellow}, hash/.style={label=##1}]" ++
    "\\node[box] (a) at (0,0) {A};" ++
    "\\node[box=red] (b) at (2,0) {B};" ++
    "\\node[pair={red}{green}] (c) at (4,0) {C};" ++
    "\\node[style=box] (d) at (6,0) {D};" ++
    "\\node[outer=green] (e) at (8,0) {E};" ++
    "\\begin{scope}[box/.style={draw=#1!50}]\\node[box=red] (f) at (0,2) {F};\\end{scope}" ++
    "\\node[box=red] (g) at (2,2) {G};" ++
    "\\end{tikzpicture}", {type: "tikz"})^[0]
let styled_nodes = [for (child in styled, nested in
    if (child is element and string(name(child)) == "scope") children_named(child, "node")
    else if (child is element and string(name(child)) == "node") [child] else []) nested]

let pic_source = "\\begin{tikzpicture}" ++
    "\\tikzset{seg/.pic={\\draw[#1] (0,0) -- (1,0);\\draw[#1] (1,0) -- (1,1);}}" ++
    "\\pic at (1,2) {seg=red};\\pic at (3,0) {seg};\\draw (0,0) -- (0,1);\\end{tikzpicture}"
let pic_tree = parse(pic_source, {type: "tikz"})^[0]
let pics = children_named(pic_tree, "pic")
let pic_drawn = format(tikz.render(pic_source)^, 'xml')

let document = latex.render_to_html(parse("\\documentclass{article}\\usepackage{tikz}" ++
    "\\tikzset{box/.style={draw=#1, thick}, box/.default=red," ++
    "  every path/.style={thick}, mark/.pic={\\draw (0,0) -- (#1,0);}}\n" ++
    "\\tikzset{plain/.style={blue}}\n" ++
    "\\begin{document}\\begin{tikzpicture}\\draw[plain] (0,0) -- (1,0);" ++
    "\\draw[box=blue] (0,1) -- (1,1);\\pic at (0,2) {mark=2};\\end{tikzpicture}" ++
    "\\end{document}", {type: "latex"})^, {standalone: false});

[for (node in styled_nodes) node.id ++ ": " ++ string(option_text(node))];
[for (pic in pics) string(pic.x) ++ "," ++ string(pic.y) ++ " " ++ pic.name ++ " arg=" ++
    (if (pic.argument == null) "none" else pic.argument) ++ " paths=" ++
    string([for (path in children_named(pic, "path")) path.options_source])];
// The picture spans (0,0)-(4,3) cm; a pic body is translated to its `at` point.
[contains(pic_drawn, "M61.7953 61.7953 L99.5906 61.7953\" fill=\"none\" stroke=\"red\""),
    contains(pic_drawn, "M99.5906 61.7953 L99.5906 24\" fill=\"none\" stroke=\"red\""),
    contains(pic_drawn, "M137.386 137.386 L175.181 137.386\" fill=\"none\" stroke=\"black\""),
    contains(pic_drawn, "M175.181 137.386 L175.181 99.5906")];
[contains(document, "tikz-picture"), not contains(document, "latex-tikz-unsupported"),
    contains(document, "stroke=\"blue\""), contains(document, "stroke-width=\"1.1\"")];
[failure("\\begin{tikzpicture}[k/.code={x}]\\draw (0,0) -- (1,1);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\tikzset{s/.store in=\\x}\\draw (0,0) -- (1,1);" ++
        "\\end{tikzpicture}"),
    // A plain \tikzset key would change untracked defaults; it is diagnosed, not dropped.
    failure("\\begin{tikzpicture}\\tikzset{>=Stealth}\\draw[->] (0,0) -- (1,1);" ++
        "\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[draw] (a) {A};\\tikzset{thick}\\draw (a) -- (2,0);" ++
        "\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\tikzset{p/.style 2 args={draw=#1}}" ++
        "\\node[p=red] at (0,0) {x};\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\tikzset{p/.style={draw=#2}}" ++
        "\\node[p=red] at (0,0) {x};\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\pic at (0,0) {nope};\\end{tikzpicture}"),
    parse_failure("\\begin{tikzpicture}\\tikzset{a/.style={b}, b/.style={a}}" ++
        "\\draw[a] (0,0) -- (1,1);\\end{tikzpicture}"),
    // An error inside a pic body is reported at its \pic command.
    parse_failure("\\begin{tikzpicture}\\tikzset{q/.pic={\\draw (0,0) -- (1,1)}}" ++
        "\\pic at (0,0) {q};\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\coordinate (a) at (1,1);\\tikzset{q/.pic={\\draw (0,0) -- (1,0);}}" ++
        "\\pic at (a) {q};\\end{tikzpicture}"),
    contains(latex.render_to_html(parse("\\documentclass{article}\\usepackage{tikz}" ++
        "\\tikzset{3D/.cd, x/.store in=\\xx}\\begin{document}\\begin{tikzpicture}" ++
        "\\draw (0,0) -- (1,1);\\end{tikzpicture}\\end{document}", {type: "latex"})^,
        {standalone: false}), "unsupported TikZ style declaration: 3D/.cd")]
