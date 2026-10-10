// The math input boundary expands scoped TeX definitions before digestion.
import util: lambda.doc.math.util

fn ast(source) => parse(source, {type: "math", flavor: "latex"})^
fn text(source) => util.text_of(ast(source))

let image = util.content_items(ast("\\def\\logo{\\includegraphics[height=0.8em,alt={KA, logo}]{a_b.png}}\\logo"))[0]
let fraction = util.content_items(ast("\\def\\pair#1{#1+#1}\\frac{\\pair{x}}{2}"))[0]
let nested = util.content_items(ast("\\def\\a{x}\\frac{\\def\\a{y}\\a}{\\a}"))[0]
let original = parse("\\def\\a{x}\\a", {type: "tex"})^
let checks = [
    {name: "macro expands into a typed image", ok: name(image) == 'image_command' and image.src == "a_b.png" and
        image.options == "height=0.8em,alt={KA, logo}"},
    {name: "parameters expand inside math arguments", ok: name(fraction) == 'fraction' and
        util.text_of(fraction.numer) == "xx" and util.content_items(fraction.numer)[1].value == "+" and
        util.text_of(fraction.denom) == "2"},
    {name: "definitions retain group scope", ok: text("\\def\\a{x}{\\def\\a{y}\\a}\\a") == "yx"},
    {name: "definitions inside fractions execute with local scope", ok:
        util.text_of(nested.numer) == "y" and util.text_of(nested.denom) == "x"},
    {name: "newcommand optional arguments expand", ok: text("\\newcommand{\\pair}[2][x]{#1+#2}\\pair{y}\\pair[z]{w}") == "xyzw"},
    {name: "let aliases expand", ok: text("\\def\\a{x}\\let\\b\\a\\b") == "x"},
    {name: "comments do not introduce definitions", ok: text("a%\\def\\a{x}\nb") == "ab"},
    {name: "document expansion still retains declarations", ok: contains(original.text, "\\def\\a{x}")},
    {name: "definition diagnostics remain visible", ok: ast("\\newcommand{\\a}[12]{x}\\a").error != null}
];
[for (check in checks where check.ok != true) check.name]
