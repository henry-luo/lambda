// No import: runtime formatting must lazily load its font-independent package.
let source = "\\frac{a+b}{c}"
let ast = parse(source,'math')^
let latex = format(ast,'math')^
let ascii = format(ast,{type:'math',flavor:'ascii'})^
fn nested(value) string^ => format(parse(value,'math')^,'math-latex')^
let checks = [
    {name:"default math format is LaTeX",ok:latex=="\\frac{a + b}{c}"},
    {name:"AsciiMath routes through the package",ok:ascii=="(a + b)/(c)"},
    {name:"legacy spellings share the package",ok:format(ast,'math-latex')^==latex and format(ast,'math-ascii')^==ascii},
    {name:"empty and unknown flavors retain the LaTeX default",ok:
        format(ast,{type:'math',flavor:""})^==latex and format(ast,{type:'math',flavor:'unknown'})^==latex},
    {name:"repeated package calls retain caller state",ok:
        all([for (i in 1 to 12) format(ast,'math-ascii')^==ascii]) and source=="\\frac{a+b}{c}"},
    {name:"nested formatting and native serialization survive lazy load",ok:nested("x^2")^=="x^2" and format(42,'json')^=="42"},
    {name:"Typst math remains an explicit unsupported error",ok:(format(ast,'math-typst') ^ { "unsupported" })=="unsupported"},
    {name:"root arguments retain a single bracket pair",ok:nested("\\sqrt[3]{x}")^=="\\sqrt[3]{x}"},
    {name:"binomial operands survive source round trips",ok:nested("\\binom{n}{k}")^=="\\binom{n}{k}"}
];
[for (check in checks where check.ok!=true) check.name]
