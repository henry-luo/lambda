// Eager charts retain exact arc samples and build outlines only for nearest picking (S12.1.1v3).
import chart: lambda.chart.chart
import behavior: lambda.chart.behavior
import parameter: lambda.chart.parameter
import paths: lambda.chart.path_geometry
import picking: lambda.chart.picking

fn elements(node) => if (node is element) [node, for (child in content(node)) for (entry in elements(child)) entry] else []
fn frame(spec) {
    let st = parameter.initial(parameter.definitions(behavior.prepare(spec)));
    let image = chart.render_spec(spec, null, st);
    parse((elements(image) |: ~["data-chart-frame"] != null)[0]["data-chart-frame"], 'json')^
}
let base = {width:200,height:160,data:[{id:"a",x:1,y:2},{id:"b",x:3,y:4}],mark:{kind:"point"},
    encoding:{x:{field:"x",dtype:"quantitative"},y:{field:"y",dtype:"quantitative"},key:{field:"id"}}};
let direct = frame({*:base,interaction:{element_select:true,tooltip:true}});
let selected = frame({*:base,params:[{name:"pick",select:{type:"point",nearest:true}}]});
let hovered = frame({*:base,interaction:{tooltip:{nearest:true}}});
let checks = {
    direct_rows: len(direct.data) == 2 and direct.geometry == [],
    nearest_selection: len(selected.geometry) == 2 and picking.nearest(selected.geometry,selected.geometry[0].lines[0].points[0]).row.id == "a",
    nearest_behavior: len(hovered.geometry) == 2,
    arc_samples: all([for (source in ["M10 0 A10 10 0 0 1 0 10", "M0 0 A5 3 30 1 0 20 10", "M0 0 A0 5 0 0 1 20 10", "M0 0 A5 5 0 1 1 0 0"],
        let segment = paths.parse(source)[1]) paths.sample(source)[0].points == paths.sample_curve((t) => paths.point(segment,t))]),
    nonfinite_endpoint: paths.sample_curve((t) => [t, if (t == 0) inf else 0.0]) is error,
    short_endpoint: paths.sample_curve((t) => if (t == 1) [t] else [t,t]) is error,
    nonfinite_interior: paths.sample_curve((t) => [t, if (t == 0.25) nan else 0.0]) is error,
    linear_endpoint_validation: paths.sample_curve((t) => [t,inf],0.75,1,true) is error
};
[for (label, passed in checks where passed != true) string(label)]
