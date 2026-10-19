import ui: lambda.ui.wired
import t: lambda.ui.wired.tokens

let defaults = ui.tokens()^
let overrides = ui.tokens({ink:"#AABBCC",stroke_width:2,spacing:0})^;
[
    ["paper and ink defaults",defaults.ink == "#252525" and defaults.background == "#faf9f6" and defaults.stroke_width == 1.2],
    ["independent overrides",overrides.ink == "#AABBCC" and overrides.spacing == 0 and defaults.spacing == 12],
    ["length and number units",contains(t.variables()^,"--wired-stroke-width:1.2px;") and contains(t.variables()^,"--wired-line-height:1.5;")],
    ["nested inheritance",t.variables({ink:"#112233"},true)^ == "--wired-ink:#112233;"],
    ["unknown token",((ui.tokens({radius:4}) ^ { null }) or null) == null],
    ["invalid colors",all([for (color in ["red","#123","#gggggg","#abcdef;display:none"]) ((ui.tokens({ink:color}) ^ { null }) or null) == null])],
    ["finite stroke",all([for (value in [-1,0,inf,nan,"2px",null]) ((ui.tokens({stroke_width:value}) ^ { null }) or null) == null])],
    ["positive font",((ui.tokens({font_size:0}) ^ { null }) or null) == null],
    ["font declaration injection",((ui.tokens({font_family:"Arial;display:none"}) ^ { null }) or null) == null],
    ["local selectors",contains(ui.stylesheet(),".wired-sketch") and not contains(ui.stylesheet(),".bold-") and not contains(ui.stylesheet(),".dtna-")],
    ["decoration hit policy",contains(ui.stylesheet(),"pointer-events:none")]
]
