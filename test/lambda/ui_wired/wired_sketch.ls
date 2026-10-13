import ui: lambda.ui.wired
import sketch: lambda.ui.wired.sketch

let a = <wired.button seed:12,"Sketch">
let b = <wired.button seed:13,"Sketch">
let drawing = sketch.outline(a)
let strokes = content(content(drawing)[0])
let elevated = sketch.outline(<wired.card seed:12,elevation:5>)
let divider = sketch.outline(<wired.divider seed:12>,true);
[
    ["pure seeded strokes",drawing == sketch.outline(a)],
    ["seed changes shape",drawing != sketch.outline(b)],
    ["two passes per edge",len(strokes) == 8],
    ["non scaling strokes",all([for (path in strokes) path["vector-effect"] == "non-scaling-stroke"])],
    ["quadratic pencil paths",all([for (path in strokes) starts_with(path.d,"M") and contains(path.d," Q")])],
    ["responsive canvas",drawing.viewBox == "0 0 100 100" and drawing.preserveAspectRatio == "none"],
    ["elevation strokes",len(content(content(elevated)[0])) == 12],
    ["divider strokes",len(content(content(divider)[0])) == 2],
    ["text button omits outline",len(content(ui.render(ui.button({variant:'text'},"Plain")^))) == 1],
    ["authored seed zero",sketch.outline(<wired.button seed:0>) != sketch.outline(<wired.button seed:1>)]
]
