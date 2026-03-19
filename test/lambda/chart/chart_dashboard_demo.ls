// chart_dashboard_demo.ls — 6-chart demo version for README screenshot.
//
// Usage:
//   ./lambda.exe render test/lambda/chart/chart_dashboard_demo.ls -o doc/demo3.png

import vega:  lambda.package.chart.vega
import chart: lambda.package.chart.chart

let dashboard_file = "test/lambda/chart/dashboard_demo.json"
let dashboard = input(dashboard_file)^

fn render_svg(c) {
    let spec = vega.convert(c)
    let svg  = chart.render_spec(spec)
    format(svg, 'xml')
}

fn make_card(c) {
    let label   = if (c.label) c.label else ""
    let svg_str = render_svg(c)
    "<div class=\"card\"><p class=\"card-label\">" ++ label ++ "</p>" ++ svg_str ++ "</div>"
}

let title    = if (dashboard.title)    dashboard.title    else "Chart Dashboard"
let subtitle = if (dashboard.subtitle) dashboard.subtitle else ""
let cols     = if (dashboard.columns)  string(int(dashboard.columns)) else "3"

let cards_html = (dashboard.charts | make_card(~)) | join("")

let css =
    "*{box-sizing:border-box;margin:0;padding:0}" ++
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#f0f2f5;color:#222}" ++
    "header{background:linear-gradient(135deg,#1a1a2e 0%,#16213e 60%,#0f3460 100%);" ++
        "color:#fff;padding:36px 48px 32px;text-align:center}" ++
    "header h1{font-size:28px;font-weight:700;letter-spacing:-.3px;margin-bottom:6px}" ++
    "header p{font-size:14px;opacity:.6;font-weight:400}" ++
    "main{padding:28px 32px;max-width:1440px;margin:0 auto}" ++
    ".grid{display:grid;grid-template-columns:repeat(" ++ cols ++ ",1fr);gap:22px}" ++
    ".card{background:#fff;border-radius:12px;" ++
        "box-shadow:0 1px 4px rgba(0,0,0,.06),0 4px 16px rgba(0,0,0,.07);" ++
        "padding:18px 14px 14px}" ++
    ".card-label{font-size:10px;font-weight:700;text-transform:uppercase;" ++
        "letter-spacing:1.2px;color:#999;margin-bottom:10px;padding-left:2px}" ++
    "svg{display:block;margin:0 auto}" ++
    "@media(max-width:1100px){.grid{grid-template-columns:repeat(2,1fr)}}" ++
    "@media(max-width:700px){.grid{grid-template-columns:1fr}}"

"<!DOCTYPE html>" ++
"<html lang=\"en\">" ++
"<head>" ++
"<meta charset=\"UTF-8\">" ++
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" ++
"<title>" ++ title ++ "</title>" ++
"<style>" ++ css ++ "</style>" ++
"</head>" ++
"<body>" ++
"<header><h1>" ++ title ++ "</h1><p>" ++ subtitle ++ "</p></header>" ++
"<main><div class=\"grid\">" ++ cards_html ++ "</div></main>" ++
"</body></html>"
