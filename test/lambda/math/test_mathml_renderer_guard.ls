import math: lambda.doc.math.math
let tree=math.parse_mathml("<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mfrac><mi>a</mi><mi>b</mi></mfrac></math>")^;
// D7.2.8: the TeX renderer must not flatten a MathML fraction to plain text.
[math.render_box(tree) is error, (math.render_math(tree) or "unsupported")=="unsupported"]
