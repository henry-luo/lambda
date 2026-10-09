import models: .model
import cameras: .camera
import geometry: lambda.chart.geometry
import numbers: lambda.chart.numbers
import expressions: .expression
import util: lambda.chart.util

fn value(v, fallback) => if (v == null) fallback else v
fn features(data) => if (data.type == "FeatureCollection") data.features
    else if (data.type == "Feature") [data]
    else [{type:"Feature",properties:{},geometry:data}]
fn sources(model, source_id) => [for (child in content(model) where name(child) == 'source' and string(child.id) == string(source_id)) child]
fn source_records(model, source_id) {
    let source = sources(model,source_id);
    if (len(source) != 1) error("map: query source does not exist")
    else [for (index, feature in features(source[0].data))
        {source:string(source_id),feature_id:if (feature.id != null) feature.id else index,feature:feature,geometry:feature.geometry}]
}
pub fn query_source(model, source_id) {
    let checked = models.normalize(model);
    if (checked is error) checked else source_records(checked,source_id)
}
fn polygon_lines(camera, coordinates) {
    let anchor = if (len(coordinates) == 0 or len(coordinates[0]) == 0) 0.0 else
        cameras.wrap((coordinates[0][0][0]-camera.center[0])/360.0,1.0);
    [for (ring in coordinates) {closed:true,points:cameras.project_sequence(camera,ring,anchor)}]
}
fn outlines(camera, shape) {
    let kind = shape.type, coordinates = shape.coordinates;
    if (shape == null or len(coordinates) == 0 and kind != "GeometryCollection") []
    else if (kind == "GeometryCollection") [for (child in shape.geometries) *outlines(camera,child)]
    else if (kind == "Point") [{point:true,points:[cameras.project(camera,coordinates)]}]
    else if (kind == "MultiPoint") [for (point in coordinates) {point:true,points:[cameras.project(camera,point)]}]
    else if (kind == "LineString") [{closed:false,points:cameras.project_sequence(camera,coordinates)}]
    else if (kind == "MultiLineString") [for (line in coordinates) {closed:false,points:cameras.project_sequence(camera,line)}]
    else if (kind == "Polygon") [{polygon:true,rings:polygon_lines(camera,coordinates)}]
    else if (kind == "MultiPolygon") [for (polygon in coordinates) {polygon:true,rings:polygon_lines(camera,polygon)}]
    else []
}
fn distance(a,b) => sqrt((a[0]-b[0])**2+(a[1]-b[1])**2)
fn stroke_hit(points, point, width, closed) {
    any([for (i in 0 to (len(points)-2),let a=points[i],let b=points[i+1],
        let squared=(b[0]-a[0])**2+(b[1]-a[1])**2,
        let t=if (squared==0) -1.0 else ((point[0]-a[0])*(b[0]-a[0])+(point[1]-a[1])*(b[1]-a[1]))/squared)
        t>=0 and t<=1 and geometry.distance_segment(point,a,b)<=width]) or
    any([for (i,p in points where closed or (i>0 and i<len(points)-1)) distance(p,point)<=width])
}
fn hit(shape, kind, size, point, tolerance) {
    if (kind == "circle") shape.point == true and size > 0 and
        distance(shape.points[0],point) <= size+tolerance
    else if (kind == "fill") shape.polygon == true and
        (len([for (ring in shape.rings where geometry.contains_point(ring.points,point)) ring]) % 2 == 1 or
        any([for (ring in shape.rings) stroke_hit(ring.points,point,tolerance,true)]))
    else if (kind == "line") size>0 and
        any([for (line in (if (shape.polygon == true) shape.rings else if (shape.point == true) [] else [shape]))
            stroke_hit(line.points,point,size/2.0+tolerance,line.closed == true)])
    else false
}
fn query_layer(model, camera, layer, point, tolerance) {
    let records = reverse(source_records(model,layer.source));
    let paints = expressions.evaluate_features(layer,records |> ~.feature,camera.zoom);
    if (paints is error) paints
    else [for (index,record in records, let paint=paints[index] where paint.visible and paint.alpha > 0 and
        any([for (shape in outlines(camera,record.geometry)) hit(shape,string(layer.type),paint.size,point,tolerance)]))
        {*:record,layer:string(layer.id)}]
}
pub fn query_rendered(model, point, options = {}) {
    let checked = models.normalize(model);
    let camera = if (checked is error) checked else cameras.camera(checked);
    let tolerance = value(options.radius,0.0);
    if (checked is error) checked else if (not (point is array) or len(point) != 2 or
        not all(point |> numbers.finite_number(~)) or not numbers.finite_number(tolerance) or tolerance < 0)
        error("map: invalid query point or radius")
    else if (options.layers != null and not (options.layers is array)) error("map: query layers must be an array")
    else if (point[0]<0 or point[1]<0 or point[0]>camera.width or point[1]>camera.height) []
    else {
        let layers = [for (layer in reverse(content(checked)) where name(layer) == 'layer' and
            string(layer.type) != "background" and
            (options.layers == null or contains(options.layers,string(layer.id))))
            query_layer(checked,camera,layer,point,tolerance)];
        let invalid = util.first_error(layers);
        if (invalid is error) invalid else [for (records in layers) *records]
    }
}
