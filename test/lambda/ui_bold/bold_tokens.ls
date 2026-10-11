import ui: lambda.ui.bold
import t: lambda.ui.bold.tokens

let defaults = ui.tokens()^
let custom = ui.tokens({primary:"#AABBCC",radius:0,shadow_offset:8})^;
[
    ["bold defaults",defaults.primary == "#facc15" and defaults.border_width == 2 and defaults.shadow_offset == 4],
    ["independent overrides",custom.primary == "#AABBCC" and custom.radius == 0 and defaults.radius == 4],
    ["length units",contains(t.variables()^,"--bold-shadow-offset:4px;") and contains(t.variables()^,"--bold-line-height:1.5;")],
    ["nested inheritance",t.variables({radius:0},true)^ == "--bold-radius:0px;"],
    ["unknown token rejected",(ui.tokens({unknown:1}) or null) == null],
    ["invalid colors rejected",all([for (color in ["red","#gggggg","#123","#abcdef;display:none"])
        (ui.tokens({primary:color}) or null) == null])],
    ["invalid dimensions rejected",all([for (value in [-1,inf,nan,"4px",null]) (ui.tokens({shadow_offset:value}) or null) == null])],
    ["zero control height rejected",(ui.tokens({control_height:0}) or null) == null],
    ["CSS declaration rejected",(ui.tokens({font_family:"Arial;display:none"}) or null) == null],
    ["family stylesheet",contains(ui.stylesheet(),".bold-button:active") and not contains(ui.stylesheet(),".dtna-")]
]
