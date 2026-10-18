import m: lambda.doc.math.mathml
import util: lambda.doc.math.util
import svg: .mod_svg_snapshot

fn convert(source,options=null) element^ => m.to_mathml(parse(source,'math')^,options)^
fn nodes(source,tag) array^ => svg.nodes(convert(source)^,tag)
fn text(source,tag) string^ => join([for (n in nodes(source,tag)^) util.text_of(n)],"")
let fraction=convert("\\frac{a+b}{c}")^
let empty=svg.nodes(convert("\\frac{}{x}")^,'mfrac')[0]
let sum=svg.nodes(convert("\\sum_{i=1}^n x_i")^,'munderover')[0]
let forced=svg.nodes(convert("\\int\\limits_0^1")^,'munderover')[0]
let annotation=convert("x",{display:true,source:"x < y & z"})^
let scoped=convert("{\\mathbf{x}}y\\mathrm{z}")^
let checks=[
    {name:"root carries the MathML namespace and inline display",ok:m.is_mathml(fraction) and fraction.display=="inline"},
    {name:"fraction keeps numerator, denominator and relation tokens",ok:len(svg.nodes(fraction,'mfrac'))==1 and text("\\frac{a+b}{c}",'mi')^=="abc" and text("a+b",'mo')^=="+"},
    {name:"whole numeric runs retain their numeric identity",ok:text("123.5",'mn')^=="123.5" and len(nodes("123.5",'mn')^)==1},
    {name:"TeX token boundaries and ASCII identifiers stay distinct",ok:len(nodes("xy",'mi')^)==2 and
        len(svg.nodes(m.to_mathml(parse("alpha",{type:"math",flavor:"ascii"})^)^,'mi'))==1},
    {name:"empty numerator remains a positional argument",ok:len(content(empty))==2 and len(content(empty[0]))==0},
    {name:"scripts have base, subscript and superscript in order",ok:len(content(nodes("x_1^2",'msubsup')^[0]))==3},
    {name:"root order is radicand then degree",ok:util.text_of(nodes("\\sqrt[3]{x}",'mroot')^[0])=="x3"},
    {name:"sum uses movable limits",ok:sum[0].movablelimits=="true" and util.text_of(sum)=="∑i=1n"},
    {name:"integrals keep side scripts by default",ok:len(nodes("\\int_0^1",'msubsup')^)==1},
    {name:"explicit limits disable moving to side scripts",ok:forced[0].movablelimits=="false"},
    {name:"nolimits overrides sum defaults",ok:len(nodes("\\sum\\nolimits_1^2",'msubsup')^)==1},
    {name:"binomials remove the bar and emit explicit fences",ok:nodes("\\binom{n}{k}",'mfrac')^[0].linethickness=="0" and text("\\binom{n}{k}",'mo')^=="()"},
    {name:"display fractions explicitly scope the style",ok:nodes("\\dfrac{a}{b}",'mstyle')^[0].displaystyle=="true"},
    {name:"general fractions retain thickness, fences and style",ok:nodes("\\genfrac{(}{)}{0pt}{0}{n}{k}",'mfrac')^[0].linethickness=="0pt" and text("\\genfrac{(}{)}{0pt}{0}{n}{k}",'mo')^=="()"},
    {name:"mathematical alphabets and groups retain scope",ok:join([for (n in svg.nodes(scoped,'mi')) util.text_of(n)],"")=="𝐱yz" and svg.nodes(scoped,'mi')[2].mathvariant=="normal"},
    {name:"TeX uppercase Greek stays upright",ok:nodes("\\Gamma",'mi')^[0].mathvariant=="normal"},
    {name:"text preserves its spaces and escapes through XML",ok:text("\\text{a b & c}",'mtext')^=="a b & c" and contains(format(convert("\\text{a b & c}")^,'xml')^,"&amp;")},
    {name:"accents attach to the base",ok:len(nodes("\\hat{x}",'mover')^)==1 and nodes("\\hat{x}",'mover')^[0].accent=="true"},
    {name:"over annotations retain argument order",ok:util.text_of(nodes("\\overset{a}{x}",'mover')^[0])=="xa"},
    {name:"mu kerns convert to CSS em units",ok:nodes("a\\mkern18mu b",'mspace')^[0].width=="1em"},
    {name:"negative kerns use a CSS margin",ok:starts_with(nodes("a\\!b",'mspace')^[0].style,"margin-left:-")},
    {name:"physical lengths retain the TeX point conversion",ok:abs(float(replace(nodes("a\\hspace{2pt}b",'mspace')^[0].width,"pt",""))^ - 2.0*72.0/72.27)<0.00001},
    {name:"matrices preserve rows and cells",ok:len(nodes("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}",'mtr')^)==2 and len(nodes("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}",'mtd')^)==4},
    {name:"annotation is data and display is explicit",ok:annotation.display=="block" and svg.nodes(annotation,'annotation')[0].encoding=="application/x-tex" and contains(format(annotation,'xml')^,"x &lt; y &amp; z")},
    {name:"unknown commands fail instead of disappearing",ok:((convert("\\undefinedcommand") ^ { null }) or "invalid")=="invalid"},
    {name:"parser diagnostics remain visible",ok:((m.to_mathml(<math error:"bad input">) ^ { null }) or "invalid")=="invalid"},
    {name:"unsupported extensions fail",ok:((convert("\\includegraphics{x.png}") ^ { null }) or "invalid")=="invalid"}
];
[for (check in checks where check.ok!=true) check.name]
