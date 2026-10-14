// Exercise every formula in the viewable HTML gallery using its actual MathML.
import math: lambda.doc.math.math
import svg: .mod_svg_snapshot
let document=input("test/html/mathml_gallery.html",'html')^
let formulas=svg.nodes(document,'math')
let results=[for (i,formula in formulas) (let measured=math.render_box(formula),
    {index:i,ok:measured is map and measured.width>0.0 and measured.height>0.0 and
        measured.depth>=0.0 and measured.width-measured.width==0.0 and
        measured.height-measured.height==0.0 and measured.depth-measured.depth==0.0})]
let checks=[{index:-1,ok:len(formulas)==10},*results];
[for (check in checks where check.ok!=true) check.index]
