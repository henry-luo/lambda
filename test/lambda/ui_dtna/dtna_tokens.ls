import ui: lambda.ui.dtna
import t: lambda.ui.dtna.tokens
let blue = ui.palette("#1677ff")^
let theme = ui.tokens({primary:"#722ed1",radius:0,spacing:0})^;
[blue, theme.primary,theme.primary_hover,theme.primary_active,theme.primary_background,
    theme.radius,theme.spacing,
    t.scoped_variables({spacing:12})^,
    (ui.palette("invalid") or null) == null,
    (ui.tokens({font_size:inf}) or null) == null,
    (ui.tokens({font_family:"sans-serif;color:red"}) or null) == null]
