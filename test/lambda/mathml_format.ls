// No math-package import: format must lazily initialize it in this evaluator.
let source="\\frac{a+b}{c}"
let ast=parse(source,'math')^
let first=format(ast,{type:"math",flavor:"mathml"})^
let repeated=[for (i in 1 to 12) format(ast,{type:"math",flavor:"mathml"})^]
let legacy=format(ast,'math-mathml')^
fn outer(value) string^ => format(parse(value,'math')^,{type:"math",flavor:"mathml"})^
let checks=[
    {name:"runtime format exports through the package",ok:contains(first,"<mfrac>") and contains(first,"<mo>+</mo>")},
    {name:"repeated calls preserve package state",ok:all([for (value in repeated) value==first])},
    {name:"legacy flavor spelling uses the same exporter",ok:legacy==first},
    {name:"caller bindings and native formatting survive the nested load",ok:source=="\\frac{a+b}{c}" and format(42,'json')^=="42" and contains(outer("x^2")^,"<msup>")},
    {name:"errors propagate through format",ok:(format(parse("\\undefinedcommand",'math')^,{type:"math",flavor:"mathml"}) or "invalid")=="invalid"}
];
[for (check in checks where check.ok!=true) check.name]
