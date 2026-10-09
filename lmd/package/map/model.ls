import cameras: .camera
import geo: lambda.chart.geo
import expressions: .expression
import util: lambda.chart.util

fn value(v, fallback) => if (v == null) fallback else v
fn text(v) => (v is string or v is symbol) and len(string(v)) > 0
fn source_error(source) {
    if (source.type != "geojson" and source.type != 'geojson') error("map: only inline GeoJSON sources are implemented")
    else if (source.src != null or source.url != null) error("map: external sources are not yet implemented")
    else if (any([for (k,v in source where k is string or k is symbol) not contains(["id","type","data"],string(k))]))
        error("map: unsupported source attribute")
    else if (not (source.data is map)) error("map: source data must be typed GeoJSON")
    else geo.validate(source.data)
}
fn layer_error(layer, sources) {
    let kind = string(layer.type);
    if (not contains(["background","fill","line","circle"],kind)) error("map: unsupported layer type")
    else if (any([for (k,v in layer where k is string or k is symbol)
        not contains(["id","type","source","paint","layout","minzoom","maxzoom","metadata","filter","source-layer"],string(k))]))
        error("map: unsupported layer attribute")
    else if (kind != "background" and len([for (s in sources where string(s.id) == string(layer.source)) s]) != 1)
        error("map: layer requires an existing source")
    else expressions.validate(layer)
}
pub fn normalize(model) {
    if (not (model is element) or name(model) != 'geomap') error("map: expected a geomap element")
    else {
        let camera = cameras.camera(model);
        let children = content(model);
        let sources = [for (child in children where child is element and name(child) == 'source') child];
        let layers = [for (child in children where child is element and name(child) == 'layer') child];
        let invalid = util.first_error([for (child in children)
            if (not (child is element) or not contains(['source','layer'],name(child))) error("map: invalid map child")
            else if (not text(child.id)) error("map: source and layer IDs must be nonempty text")
            else if (len([for (other in children where name(other) == name(child) and string(other.id) == string(child.id)) other]) != 1)
                error("map: duplicate source or layer ID")
            else if (name(child) == 'source') source_error(child) else layer_error(child,sources)]);
        if (camera is error) camera else if (invalid is error) invalid
        else <geomap *:map([for (k,v in model where k is string or k is symbol) (string(k),v)]),
            *:camera, projection:"mercator", 'world-copies':false, *children>
    }
}
