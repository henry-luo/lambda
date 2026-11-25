// S2.5.8: unbraced math arguments consume one Unicode code point.
import mathml: lambda.doc.math.mathml
let super = <math <subsup base:"x", sup:<unicode_text value:"é">>>;
let sub = <math <subsup base:"x", sub:<unicode_text value:"β">>>;
let astral = <math <subsup base:"x", sup:<unicode_text value:"𝛼">>>;
let trailing = <math <subsup base:"x", sup:<unicode_text value:"é">> <unicode_text value:"β">>;
let xml = format(mathml.to_mathml(parse("x^é", "math")^)^, "xml")^;
let document = parse("\\begin{document}$x^é$\\end{document}", "latex")^;
[
    parse("x^é", "math")^ == super,
    parse("x_β", "math")^ == sub,
    parse("x^𝛼", "math")^ == astral,
    parse("x^éβ", "math")^ == trailing,
    parse("\\def\\u{é}x^\\u", "math")^ == super,
    contains(xml,"<msup>") and contains(xml,"é"),
    document is <latex_document> and contains(format(document,"json")^,"é")
]
