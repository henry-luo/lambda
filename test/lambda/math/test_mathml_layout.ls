import math: lambda.doc.math.math
import svg: .mod_svg_snapshot
import font: lambda.doc.math.font
import radiant

fn tree(body,display="block") element^ => math.parse_mathml("<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\""++display++"\">"++body++"</math>")^
fn close(a,b) => abs(a-b)<0.00001
let bytes=input("test/lambda/math/fonts/NotoSansMath-Regular.ttf",'binary')^
let options={font_family:"CoreMath",fonts:[{font_family:"CoreMath",data:bytes}],font_size:20}
let facts=radiant.math_metrics({font_family:"CoreMath",font_size:1000},[ord("a"),ord("b"),ord("x")],options.fonts)
let c=facts.constants
let a=facts.glyphs[0]
let b=facts.glyphs[1]
let source=tree("<mfrac><mi mathvariant=\"normal\">a</mi><mi mathvariant=\"normal\">b</mi></mfrac>")^
let fraction=math.render_box(source,options)^
let geometry=svg.geometry(fraction.element)
let numerator=[for (entry in geometry where entry.text=="a") entry][0]
let denominator=[for (entry in geometry where entry.text=="b") entry][0]
let rule=svg.nodes(fraction.element,'rect')[0]
// Independent Core §3.3.2 formulas, including its fixed CSS-pixel padding.
let up=max(c.fraction_numerator_display_style_shift_up,
    c.axis_height+c.fraction_rule_thickness/2.0+c.fraction_num_display_style_gap_min+max(0.0,a.ink.bottom))
let down=max(c.fraction_denominator_display_style_shift_down,
    c.fraction_rule_thickness/2.0+c.fraction_denom_display_style_gap_min+max(0.0,0.0-b.ink.top)-c.axis_height)
let barless=math.render_box(tree("<mfrac linethickness=\"0\"><mi>a</mi><mi>b</mi></mfrac>")^,options)^
let plus_prefix=math.render_box(tree("<mo form=\"prefix\">+</mo>")^,options)^
let plus_infix=math.render_box(tree("<mo form=\"infix\">+</mo>")^,options)^
let nested=math.render_box(tree("<mrow><mi>x</mi><msup><mi>y</mi><msup><mi>z</mi><mn>2</mn></msup></msup></mrow>")^,options)^
let inline_sum=math.render_box(tree("<munderover><mo>∑</mo><mn>0</mn><mi>n</mi></munderover>","inline")^,options)^
let block_sum=math.render_box(tree("<munderover><mo>∑</mo><mn>0</mn><mi>n</mi></munderover>")^,options)^
let annotated=math.render_box(tree("<semantics><mfrac><mi>a</mi><mi>b</mi></mfrac><annotation encoding=\"application/x-tex\">DO NOT PAINT</annotation></semantics>")^,options)^
let matrix=math.render_box(tree("<mrow><mo>(</mo><mtable><mtr><mtd><mi>a</mi></mtd><mtd><mi>b</mi></mtd></mtr><mtr><mtd><mi>c</mi></mtd><mtd><mi>d</mi></mtd></mtr></mtable><mo>)</mo></mrow>")^)^
let checks=[
    {name:"fraction uses measured Core numerator shift",ok:close(numerator.y,0.0-up)},
    {name:"fraction uses measured Core denominator shift",ok:close(denominator.y,down)},
    {name:"fraction rule uses selected MATH constants",ok:close(rule.height,c.fraction_rule_thickness) and close(rule.y,0.0-c.axis_height-c.fraction_rule_thickness/2.0)},
    {name:"fraction adds one CSS pixel per side",ok:close(fraction.width,max(a.advance,b.advance)/1000.0+2.0/options.font_size)},
    {name:"barless fraction retains separate arguments",ok:len(svg.nodes(barless.element,'rect'))==0 and barless.height>0.0 and barless.depth>0.0},
    {name:"operator forms use Core dictionary spacing",ok:close(plus_infix.width-plus_prefix.width,8.0/18.0)},
    {name:"nested scripts retain all painted children",ok:len(svg.nodes(nested.element,'text'))==4 and nested.height>0.5},
    {name:"movable limits distinguish compact and normal styles",ok:inline_sum.width>block_sum.width and block_sum.height+block_sum.depth>inline_sum.height+inline_sum.depth},
    {name:"annotations remain inert",ok:svg.painted_text(annotated.element)=="𝑎𝑏"},
    {name:"matrix paints four cells and two fences",ok:len(svg.nodes(matrix.element,'text'))==6 and matrix.height>1.0 and matrix.depth>0.5},
    {name:"root degree uses a deeper script style",ok:math.render_box(tree("<mroot><mi>x</mi><mn>3</mn></mroot>")^,options).width>0.5},
    {name:"empty fraction arguments remain valid",ok:math.render_box(tree("<mfrac><mrow/><mi>x</mi></mfrac>")^,options).height>=0.0},
    {name:"invalid argument count follows Core row fallback",ok:svg.painted_text(math.render_math(tree("<mfrac><mi>a</mi></mfrac>")^,options)^)=="𝑎"},
    {name:"MathML never acquires TeX companion constants",ok:font.prepare(source,{*:options,notation:"mathml"})^.tex==null},
    {name:"unsupported layout elements report errors",ok:math.render_box(tree("<menclose><mi>x</mi></menclose>")^,options) is error}
];
[for (check in checks where check.ok!=true) check.name]
