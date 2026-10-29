// Eager charts retain exact arc samples and build outlines only for nearest picking (S12.1.1v3).
import chart: lambda.chart.chart
import behavior: lambda.chart.behavior
import parameter: lambda.chart.parameter
import paths: lambda.chart.path_geometry
import picking: lambda.chart.picking
import coordinate: lambda.chart.coordinate
import geometry: lambda.chart.geometry

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
let projection_models = [for (settings in [{type:"polar",inner_radius:0.1,outer_radius:0.8},
    {type:"theta"},{type:"theta",direct_polar:true},{type:"radial"},{type:"helix",turns:[0.0,2.5]},
    {transform:[{type:"transpose"},{type:"fisheye",distortion:2.0}]},
    {type:"custom",project:(p) => [300.0*p[0],200.0*p[1]**2]}]) coordinate.configure(settings,300,200)];
let checks = {
    direct_rows: len(direct.data) == 2 and direct.geometry == [],
    nearest_selection: len(selected.geometry) == 2 and picking.nearest(selected.geometry,selected.geometry[0].lines[0].points[0]).row.id == "a",
    nearest_behavior: len(hovered.geometry) == 2,
    arc_samples: all([for (source in ["M10 0 A10 10 0 0 1 0 10", "M0 0 A5 3 30 1 0 20 10", "M0 0 A0 5 0 0 1 20 10", "M0 0 A5 5 0 1 1 0 0"],
        let segment = paths.parse(source)[1]) paths.sample(source)[0].points == paths.sample_curve((t) => paths.point(segment,t))]),
    prepared_projection: all([for (model in projection_models,
        let a=[20.0,30.0],let b=[250.0,170.0],let intervals=if (model.type=="helix") 20 else 8)
        coordinate.line(model,[a,b]) == paths.sample_curve((t) => coordinate.pixel(model,geometry.interpolate(a,b,t)),model.precision,intervals)]),
    warp_projection_is_local: all([for (model in projection_models,
        let point=coordinate.pixel(model,[40.0,60.0]),let image=coordinate.warp(<g <g <circle cx:40.0,cy:60.0,r:3>>>,model),
        let circle=(elements(image) |: name(~)=='circle')[0]) circle.cx==point[0] and circle.cy==point[1]]),
    nonfinite_endpoint: paths.sample_curve((t) => [t, if (t == 0) inf else 0.0]) is error,
    short_endpoint: paths.sample_curve((t) => if (t == 1) [t] else [t,t]) is error,
    nonfinite_interior: paths.sample_curve((t) => [t, if (t == 0.25) nan else 0.0]) is error,
    linear_endpoint_validation: paths.sample_curve((t) => [t,inf],0.75,1,true) is error
};
[for (label, passed in checks where passed != true) string(label)]
