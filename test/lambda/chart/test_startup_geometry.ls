// Eager charts retain bounded projection geometry and per-instance picking (S12.1.1v3).
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
fn close(a,b) => paths.distance(a,b)<0.000000001
fn bounded_projection(model,a,b) {
    let points=coordinate.line(model,[a,b]);
    let count=len(points)-1;
    close(points[0],coordinate.pixel(model,a)) and close(points[count],coordinate.pixel(model,b)) and
        all([for (i in 0 to (count-1)) for (t in [0.125,0.25,0.5,0.75,0.875])
            paths.distance(coordinate.pixel(model,geometry.interpolate(a,b,(float(i)+t)/count)),
                geometry.interpolate(points[i],points[i+1],t))<=model.precision+0.000000001])
}
let bounded_models=[for (family in ["polar","theta","radial","helix"],
    precision in [0.05,0.75,2.5],turns in [[0.0,0.3],[0.0,2.5]],
    transform in [[],[{type:"transpose"}],[{type:"rotate",angle:0.3},{type:"translate",offset:[-0.2,0.1]}]])
    coordinate.configure({type:family,precision:precision,turns:turns,inner_radius:0.1,outer_radius:0.8,transform:transform},300,200)];
let checks = {
    direct_rows: len(direct.data) == 2 and direct.geometry == [],
    nearest_selection: len(selected.geometry) == 2 and picking.nearest(selected.geometry,selected.geometry[0].lines[0].points[0]).row.id == "a",
    nearest_behavior: len(hovered.geometry) == 2,
    arc_samples: all([for (source in ["M10 0 A10 10 0 0 1 0 10", "M0 0 A5 3 30 1 0 20 10", "M0 0 A0 5 0 0 1 20 10", "M0 0 A5 5 0 1 1 0 0"],
        let segment = paths.parse(source)[1]) paths.sample(source)[0].points == paths.sample_curve((t) => paths.point(segment,t))]),
    bounded_projection: all([for (model in bounded_models,
        pair in [[[20.0,30.0],[250.0,170.0]],[[20.0,30.0],[20.0,170.0]],[[20.0,30.0],[250.0,30.0]],
            [[-50.0,-40.0],[350.0,240.0]]]) bounded_projection(model,pair[0],pair[1])]),
    adaptive_fallback: all([for (model in slice(projection_models,5),let a=[20.0,30.0],let b=[250.0,170.0],
        let actual=coordinate.line(model,[a,b]),let reference=paths.sample_curve((t) => coordinate.pixel(model,geometry.interpolate(a,b,t)),model.precision,8))
        len(actual)==len(reference) and all([for (i,point in actual) close(point,reference[i])])]),
    straight_edges: all([for (family in ["cartesian","polar","theta","radial","helix"],
        let model=coordinate.configure({type:family},300,200),let a=[30.0,40.0],
        let b=if (family=="theta" or family=="radial") [150.0,40.0] else [30.0,160.0]) len(coordinate.line(model,[a,b]))==2]),
    warp_projection_is_local: all([for (model in projection_models,
        let point=coordinate.pixel(model,[40.0,60.0]),let image=coordinate.warp(<g <g <circle cx:40.0,cy:60.0,r:3>>>,model),
        let circle=(elements(image) |: name(~)=='circle')[0]) circle.cx==point[0] and circle.cy==point[1]]),
    nonfinite_endpoint: paths.sample_curve((t) => [t, if (t == 0) inf else 0.0]) is error,
    short_endpoint: paths.sample_curve((t) => if (t == 1) [t] else [t,t]) is error,
    nonfinite_interior: paths.sample_curve((t) => [t, if (t == 0.25) nan else 0.0]) is error,
    linear_endpoint_validation: paths.sample_curve((t) => [t,inf],0.75,1,true) is error,
    batch_error: paths.sample_curve((t) => [t,t],0.75,1,false,(ts) error => error("batch rejected")).message=="batch rejected",
    short_batch: paths.sample_curve((t) => [t,t],0.75,1,false,(ts) => []) is error
};
[for (label, passed in checks where passed != true) string(label)]
