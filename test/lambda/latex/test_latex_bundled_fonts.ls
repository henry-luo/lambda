// Standalone documents must declare bundled faces independently of the source directory.
import latex: lambda.doc.latex.latex
import math: lambda.doc.math.math
import amssymb: lambda.doc.latex.packages.amssymb

fn stylesheet_links(node) {
    if (node is array or node is list)
        [for (child in node, link in stylesheet_links(child)) link]
    else if (node is element) {
        let own = if (name(node) == 'link' and node.rel == "stylesheet") [node] else []
        own ++ [for (child in node, link in stylesheet_links(child)) link]
    } else []
}

fn bundled_faces(node) {
    let links = stylesheet_links(node)
    {cmu: any([for (link in links) ends_with(link.href, "/package/doc/latex/fonts/cmu-combined.css")]) or false,
     katex: any([for (link in links) ends_with(link.href, "/package/doc/math/katex.css")]) or false,
     absolute: len(links) == 1 and all([for (link in links) starts_with(link.href, "/") or
        (len(link.href) > 2 and slice(link.href, 1, 2) == ":")]),
     readable: len(links) == 1 and all([for (link in links) exists(link.href)])}
}

let ast = parse("\\documentclass{article}\\begin{document}$x^2+\\int_0^1 y\\,dy$\\end{document}", "latex")^
let options = {source_path: "elsewhere/paper.tex"}
"native document:"; bundled_faces(latex.render_document(ast, options))
"standalone HTML:"; bundled_faces(parse(latex.render_to_html(ast, {*:options, standalone: true}), "html")^)
"fragment stylesheet links:"; len(stylesheet_links(latex.render(ast, {standalone: false})))
// D7.2.9 references used bundled faces without embedding their bytes.
let standalone_math = math.render_standalone(parse("x^2", {type: "math", flavor: "latex"})^)
"standalone math:"; {svg: name(standalone_math) == 'svg',
    text: contains(format(standalone_math, 'html'), "<text"),
    referenced_fonts: contains(format(standalone_math, 'html'), "@font-face") and
        not contains(format(standalone_math, 'html'), "base64,"),
    external_stylesheets: len(stylesheet_links(standalone_math))}

// Prose AMS commands must not depend on the deleted MathLive font classes.
let prose_symbol = amssymb.render_symbol("twoheadleftarrow")
"prose AMS symbol:"; {svg: name(prose_symbol) == 'svg',
    text: contains(format(prose_symbol, 'html'), "<text"),
    referenced_fonts: contains(format(prose_symbol, 'html'), "@font-face") and
        not contains(format(prose_symbol, 'html'), "base64,"),
    external_stylesheets: len(stylesheet_links(prose_symbol)),
    unknown: amssymb.render_symbol("unknown-command")}
