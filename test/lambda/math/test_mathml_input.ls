import m: lambda.doc.math.mathml
import util: lambda.doc.math.util

let uri=m.namespace
let source="<math xmlns=\""++uri++"\"><mtext> a  b </mtext><mfrac><mi>x</mi><mrow/></mfrac><semantics><mi>y</mi><annotation encoding=\"application/x-tex\">\\unknown</annotation></semantics></math>"
let tree=m.parse_mathml(source)^
let prefixed=m.parse_mathml("<m:math xmlns:m=\""++uri++"\"><m:mi>x</m:mi></m:math>")^
let checks=[
    {name:"strict MathML parsing returns the actual root",ok:name(tree)=='math' and m.is_mathml(tree)},
    {name:"prefixed namespace bindings identify MathML",ok:m.is_mathml(prefixed) and m.local_name(prefixed[0])=="mi"},
    {name:"significant text spaces survive",ok:util.text_of(tree[0])==" a  b "},
    {name:"empty argument elements survive",ok:len(content(tree[1]))==2 and name(tree[1][1])=='mrow'},
    {name:"annotations remain inert data",ok:util.text_of(tree[2][1])=="\\unknown"},
    {name:"exporting MathML preserves the tree",ok:m.to_mathml(tree)^==tree},
    {name:"display overrides retain prefixed namespace declarations",ok:m.parse_mathml(format(m.to_mathml(prefixed,{display:true})^,'xml')^)^.display=="block"},
    {name:"supported tree XML roundtrip",ok:m.parse_mathml(format(tree,'xml')^)^==tree},
    {name:"type/flavor and compound input dispatch agree",ok:parse(source,'math-mathml')^==tree},
    {name:"internal AST is a separate input kind",ok:not m.is_mathml(parse("x",'math')^)},
    {name:"wrong namespace fails",ok:((m.parse_mathml("<math xmlns=\"urn:wrong\"><mi>x</mi></math>") ^ { null }) or "invalid")=="invalid"},
    {name:"missing namespace fails",ok:((m.parse_mathml("<math><mi>x</mi></math>") ^ { null }) or "invalid")=="invalid"},
    {name:"undeclared prefix fails",ok:((m.parse_mathml("<m:math/>") ^ { null }) or "invalid")=="invalid"},
    {name:"malformed nesting fails",ok:((m.parse_mathml("<math xmlns=\""++uri++"\"><mi>x</math>") ^ { null }) or "invalid")=="invalid"},
    {name:"multiple roots fail",ok:((m.parse_mathml(source++source) ^ { null }) or "invalid")=="invalid"},
    {name:"undeclared XML entities fail",ok:((m.parse_mathml("<math xmlns=\""++uri++"\"><mi>&alpha;</mi></math>") ^ { null }) or "invalid")=="invalid"},
    {name:"Unicode character references decode",ok:util.text_of(m.parse_mathml("<math xmlns=\""++uri++"\"><mi>&#x3B1;</mi></math>")^) == "α"}
];
[for (check in checks where check.ok!=true) check.name]
