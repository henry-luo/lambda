import vega: lambda.chart.vega

// Share the dashboard source with its original fixture; the release bundles it too.
let dashboard = input("test/lambda/chart/dashboard_data.json")^;
pub let charts = dashboard.charts
pub fn spec(entry) => if (entry.native == true) entry.spec else vega.convert(entry)
pub fn graph(entry) {
    let mark = if (entry.native == true) entry.spec.mark else entry.mark;
    let kind = if (mark is string) mark else if (mark.kind != null) mark.kind else mark.type;
    contains(["wordcloud","sankey","chord","force_graph","tree","pack","link","polygon","path"],kind)
}
pub fn live(raw) => {*:raw,interaction:{element_highlight:true,element_select:true,tooltip:true,*:(if (raw.interaction != null) raw.interaction else {})},
    state:{active:{stroke:"#317e78",stroke_width:2},selected:{fill:"#e15759",stroke:"#24334b",stroke_width:2},
        unselected:{opacity:0.3},*:(if (raw.state != null) raw.state else {})}}
