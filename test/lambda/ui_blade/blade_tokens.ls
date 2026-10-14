import ui: lambda.ui.blade
import theme: lambda.ui.blade.tokens
import clay: lambda.ui.clay
import ant: lambda.ui.dtna

let values = ui.tokens()^
let local = theme.variables({primary:"#527a9c",radius:8},true)^;
[
    ["dark medieval defaults",values.primary == "#d7ad62" and values.background == "#171313" and values.font_size == 16],
    ["material tokens",values.parchment == "#e8d2a6" and values.iron == "#869196" and values.frame_width == 3],
    ["resource tokens",values.health == "#bd3c49" and values.mana == "#3a96c7" and values.slot_size == 76],
    ["derived font scale",values.font_size_heading_1 == 42],
    ["palette shades derived",values.primary_hover != values.primary and values.primary_active != values.primary],
    ["Blade variables scoped",contains(local,"--blade-primary:#527a9c;") and contains(local,"--blade-radius:8px;")],
    ["shared aliases scoped",contains(local,"--dtna-primary:#527a9c;") and contains(local,"--dtna-radius:8px;")],
    ["nested providers inherit unrelated colors",not contains(theme.variables({radius:8},true)^,"--blade-primary:") and not contains(theme.variables({radius:8},true)^,"--dtna-primary:")],
    ["decoration pixels",contains(theme.variables({slot_size:96},true)^,"--blade-slot-size:96px;")],
    ["bad color rejected",(ui.tokens({health:"red"}) or null) == null],
    ["token injection rejected",(ui.tokens({font_family:"Georgia;}"}) or null) == null],
    ["unknown token rejected",(ui.tokens({missing:1}) or null) == null],
    ["frame and slot strictly positive",(ui.tokens({frame_width:0}) or null) == null and (ui.tokens({slot_size:0}) or null) == null],
    ["dimensions finite",all([for (v in [-1,nan,inf]) (ui.tokens({slot_size:v}) or null) == null])],
    ["zero radius and spacing allowed",ui.tokens({radius:0,spacing:0})^.radius == 0],
    ["other palettes unchanged",clay.tokens()^.primary == "#6b51db" and ant.tokens()^.primary == "#1677ff"],
    ["appearance rules scoped",contains(ui.stylesheet(),".blade-root .blade-item-slot") and not contains(ui.stylesheet(),"image-rendering:pixelated")]
]
