// Paint compatibility is a value probe; ordinary crossfades must not raise (S7.4.1, S12.1.1v3).
import animation: lambda.chart.animation
import color: lambda.color
import chart: lambda.chart.chart

fn elements(node) => if (node is element) [node, for (child in content(node)) for (entry in elements(child)) entry] else []
fn image(attrs) => <svg width:100,height:100,<rect x:0,y:0,width:20,height:20,*:attrs>>
let source = image({fill:"none",stroke:"#ff0000"});
let target = image({fill:"#0000ff",stroke:"none"});
let faded = animation.sample(source,target,150);
let solid = animation.sample(image({fill:"#ff0000"}),image({fill:"#0000ff"}),150);
let alpha = animation.sample(image({fill:"transparent"}),image({fill:"#0000ff"}),150);
let missing = animation.sample(image({}),image({fill:"#0000ff"}),150);
let reference = animation.sample(image({fill:"url(#old)"}),image({fill:"url(#new)"}),150);
let dashboard = input("test/lambda/chart/dashboard_data.json")^;
let story = (dashboard.charts |: ~.label == "Keyframe morph sample")[0].spec;
let checks = {
    supported_probe: all([for (value in ["#fff","#1234","#11223344","RGB(255,0,127)","rgba(100%,0%,0%,50%)","transparent",[0.1,0.2,0.3,0.4]])
        color.try_parse(value) == color.parse(value)^]),
    unsupported_probe: all([for (value in [null,"none","url(#paint)","currentColor","red","#zzzzzz","rgb(0,no,0)","rgba(1,2,3,2)",[0,0,0,2]]) color.try_parse(value) == null]),
    noncolor_crossfade: len(elements(faded) |: ~.class == "chart-crossfade") == 1 and
        (elements(faded) |: name(~) == 'rect')[0].fill == "none" and (elements(faded) |: name(~) == 'rect')[1].fill == "#0000ff",
    color_interpolation: (elements(solid) |: name(~) == 'rect')[0].fill == "rgba(127.5,0,127.5,1)",
    alpha_interpolation: (elements(alpha) |: name(~) == 'rect')[0].fill == "rgba(0,0,255,0.5)",
    missing_paint: missing is element,
    entering_container: len(elements(animation.sample(null,target,150)) |: name(~) == 'rect') == 1,
    unresolved_resources: len(elements(reference) |: ~.class == "chart-crossfade") == 1,
    dashboard_morph: all([for (time_ms in [0,250,500,750,1000]) chart.render_frame(story,time_ms) is element])
};
[for (label, passed in checks where passed != true) string(label)]
