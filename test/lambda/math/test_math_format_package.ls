import writer: lambda.doc.math.format
import mathml: lambda.doc.math.mathml

let ast = parse("\\frac{a}{b}+\\alpha",'math')^
let choices = ["sin(x)","cos(y)","lim_{x\\to0}","sum_{i=0}^n", "\\varepsilon+\\leq+\\infty"]
let checks = [
    {name:"public LaTeX writer",ok:writer.to_latex(ast)=="\\frac{a}{b} + \\alpha"},
    {name:"public AsciiMath writer",ok:writer.to_ascii(ast)=="(a)/(b) + alpha"},
    {name:"package formatter agrees with runtime routes",ok:all([for (source in choices) {
        let tree=parse(source,'math')^;
        writer.format(tree,{flavor:'ascii'})^==format(tree,'math-ascii')^ and
            writer.format(tree)^==format(tree,'math-latex')^
    }])},
    {name:"MathML uses the same font-independent formatter",ok:
        writer.format(ast,{flavor:'mathml'})^==format(mathml.to_mathml(ast)^,'xml')^},
    {name:"MathML conversion options reach the exporter",ok:
        contains(format(ast,{type:'math',flavor:'mathml',display:true,source:"a/b"})^,"display=\"block\"") and
        contains(writer.format(ast,{flavor:'mathml',source:"a/b"})^,"<annotation encoding=\"application/x-tex\">a/b</annotation>")},
    {name:"invalid package options return errors",ok:(writer.format(ast,42) ^ { "invalid" })=="invalid"},
    {name:"symbols and numeric atoms",ok:writer.to_latex(<math ['alpha',2,'col_sep',3,'row_sep']>)=="\\alpha 2  &  3  \\\\ "},
    {name:"ASCII words cross scripted token boundaries",ok:
        writer.to_ascii(<math ["s","i",<subsup base:"n",sup:"2">]>)=="sin^2"},
    {name:"longest ASCII word can end in a scripted token",ok:
        writer.to_ascii(<math ["e","m","p","t","y","s","e",<subsup base:"t",sub:"i">]>)=="emptyset_i"},
    {name:"long unknown token runs preserve boundaries",ok:
        writer.to_ascii(<math [for (i in 1 to 200) "z"]>)==join([for (i in 1 to 200) "z"]," ")},
    {name:"nested ASCII function parentheses coalesce",ok:writer.to_ascii(parse("sin(cos(x))",'math')^)=="sin(cos(x))"},
    {name:"single commands and multiletter scripts keep TeX braces",ok:
        writer.to_latex(<subsup base:"x",sub:"ab",sup:<command name:"alpha">>)=="x_{ab}^\\alpha"},
    {name:"unknown elements retain child traversal",ok:writer.to_latex(<unknown ["a","b"]>)=="a b"}
];
[for (check in checks where check.ok!=true) check.name]
