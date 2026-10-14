import math: lambda.doc.math.math
import svg: .mod_svg_snapshot
let tree=math.parse_mathml("<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mfrac><mi>a</mi><mi>b</mi></mfrac></math>")^;
// D7.2.8: MathML keeps a fraction rule and its vertically separated arguments.
let box=math.render_box(tree)^;
[len(svg.nodes(box.element,'rect'))==1,svg.painted_text(box.element)=="ab"]
