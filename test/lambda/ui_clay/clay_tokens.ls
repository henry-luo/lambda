import ui: lambda.ui.clay
import theme: lambda.ui.clay.tokens
import ant: lambda.ui.dtna

let tokens = ui.tokens()^
let variables = theme.variables({primary:"#287b63",radius:24},true)^;
[
    ["clay defaults",tokens.primary == "#6b51db" and tokens.background == "#f4f0f9" and tokens.surface_radius == 26],
    ["shared palette derived",tokens.primary_background == "#f5f0ff" and tokens.font_size_heading_1 == 38],
    ["colors validated",(ui.tokens({mint:"red"}) or null) == null],
    ["injection rejected",(ui.tokens({font_family:"Arial;}"}) or null) == null],
    ["unknown tokens rejected",(ui.tokens({unknown:1}) or null) == null],
    ["finite shadows",all([for (value in [-1,nan,inf]) (ui.tokens({shadow_size:value}) or null) == null])],
    ["zero flat surface allowed",ui.tokens({shadow_size:0,surface_radius:0,radius:0,spacing:0})^.shadow_size == 0],
    ["font size positive",(ui.tokens({font_size:0}) or null) == null],
    ["clay variables scoped",contains(variables,"--clay-primary:#287b63;") and contains(variables,"--clay-radius:24px;")],
    ["engine aliases scoped",contains(variables,"--dtna-primary:#287b63;") and contains(variables,"--dtna-radius:24px;")],
    ["nested inheritance",not contains(theme.variables({radius:8},true)^,"--dtna-primary:") and not contains(theme.variables({radius:8},true)^,"--clay-primary:")],
    ["palette API shared",ui.palette("#6b51db")^ == ant.palette("#6b51db")^],
    ["base package untouched",ant.tokens()^.primary == "#1677ff" and ant.tokens()^.radius == 6],
    ["clay rules scoped",contains(ui.stylesheet(),".clay-root .dtna-card,") and contains(ui.stylesheet(),"--clay-pressed:")]
]
