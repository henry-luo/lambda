// GeoJSON rendering uses shared chart appearance and pure, configurable projections.
import projection: .projection
import geometry: .geometry
import svg: .svg
import mark: .mark
import parse: .parse
import util: .util
import color: .color
import primitive: .primitive

fn position_error(points) null | error => if (not (points is array) or
    len([for (point in points where not projection.valid_position(point)) true]) > 0)
    error("chart: invalid GeoJSON longitude/latitude position") else null

fn line_error(points, ring = false) {
    let failure = position_error(points);
    if (failure is error) failure
    else if (len(points) == 0) null
    else if (len(points) < (if (ring) 4 else 2) or (ring and points[0] != points[len(points) - 1]))
        error("chart: GeoJSON lines require two positions; rings require four and must be closed")
    else if (ring and not geometry.simple_ring(points)) error("chart: geographic rings must be simple and non-self-intersecting")
    else null
}

pub fn validate(shape) {
    if (shape == null) null
    else if (not (shape is map)) error("chart: GeoJSON geometry must be an object")
    else if (shape.type == "Feature") validate(shape.geometry)
    else if (shape.type == "FeatureCollection" or shape.type == "GeometryCollection") (
        let items = if (shape.type == "FeatureCollection") shape.features else shape.geometries,
        if (not (items is array)) error("chart: GeoJSON collection requires an array") else util.first_error([for (item in items) validate(item)]))
    else if (not contains(["Point", "MultiPoint", "LineString", "MultiLineString", "Polygon", "MultiPolygon"], shape.type))
        error("chart: unsupported GeoJSON geometry type")
    else if (not (shape.coordinates is array)) error("chart: GeoJSON geometry requires coordinates")
    else if (len(shape.coordinates) == 0) null
    else if (shape.type == "Point") position_error([shape.coordinates])
    else if (shape.type == "MultiPoint") position_error(shape.coordinates)
    else if (shape.type == "LineString") line_error(shape.coordinates)
    else if (shape.type == "MultiLineString" or shape.type == "Polygon") util.first_error([for (line in shape.coordinates) line_error(line, shape.type == "Polygon")])
    else if (shape.type == "MultiPolygon") util.first_error([for (polygon in shape.coordinates)
        if (not (polygon is array)) error("chart: GeoJSON polygon requires an array of rings")
        else util.first_error([for (ring in polygon) line_error(ring, true)])])
    else error("chart: unsupported GeoJSON geometry type")
}

fn sampled(points, model, closed = false) {
    let limit = if (closed) len(points) else len(points) - 1;
    [for (index in 0 to (limit - 1),
        let values = projection.segment(points[index], points[(index + 1) % len(points)], model))
        for (slot, point in values where slot < len(values) - 1 or (not closed and index == limit - 1)) point]
}

fn world_clip(points, model) {
    let relative = [for (point in points) [point[0] - model.center[0], point[1]]];
    let first = int(floor((min(relative |> ~[0]) + 180.0) / 360.0));
    let final_copy = int(floor((max(relative |> ~[0]) + 180.0) / 360.0));
    [for (copy in first to final_copy,
        let shifted = [for (point in relative) [point[0] - float(copy) * 360.0, point[1]]],
        let clipped = geometry.clip(geometry.clip(geometry.clip(geometry.clip(shifted,
            (point) => point[0] + 180.0), (point) => 180.0 - point[0]),
            (point) => point[1] + model.latitude_limit), (point) => model.latitude_limit - point[1])
        where len(clipped) >= 3)
        [for (point in clipped) [point[0] + model.center[0], point[1]]]]
}

fn oriented_path(points, positive = true) {
    let oriented = if ((geometry.area(points) >= 0) == positive) points else reverse(points);
    svg.line_path(oriented) ++ " Z"
}

fn polygon(rings, model) {
    let pieces = [for (index, ring in rings where len(ring) > 0) (
        if (model.type == "orthographic") (
            let triangles = geometry.triangulate(ring),
            if (triangles is error) triangles else join([for (triangle in triangles)
                for (piece in projection.triangle(triangle, model)) oriented_path(piece, index == 0)], " "))
        else join([for (piece in world_clip(slice(ring, 0, len(ring) - 1), model))
            oriented_path([for (point in sampled(piece, model, true)) projection.screen(projection.raw(point, model), model)], index == 0)], " "))];
    let failure = util.first_error(pieces);
    if (failure is error) failure else join(pieces, " ")
}

fn segments(points, model) {
    let values = if (model.type == "orthographic") sampled(points, model) else points;
    [for (index in 0 to (len(values) - 2), let a = values[index], let b = values[index + 1]) (
        if (model.type == "orthographic") (
            let visible = geometry.clip_segment(projection.vector(a, model), projection.vector(b, model), (point) => point[2]),
            if (len(visible) == 2) [for (point in visible) projection.screen(point, model)])
        else (
            let relative = [[a[0] - model.center[0], a[1]], [b[0] - model.center[0], b[1]]],
            for (copy in int(floor((min(relative |> ~[0]) + 180.0) / 360.0)) to int(floor((max(relative |> ~[0]) + 180.0) / 360.0)),
                let shifted = [for (point in relative) [point[0] - float(copy) * 360.0, point[1]]],
                let clipped = geometry.clip_segment(shifted[0], shifted[1], (point) => point[0] + 180.0),
                let clipped2 = if (len(clipped) == 2) geometry.clip_segment(clipped[0], clipped[1], (point) => 180.0 - point[0]) else [],
                let clipped3 = if (len(clipped2) == 2) geometry.clip_segment(clipped2[0], clipped2[1], (point) => point[1] + model.latitude_limit) else [],
                let clipped4 = if (len(clipped3) == 2) geometry.clip_segment(clipped3[0], clipped3[1], (point) => model.latitude_limit - point[1]) else []
                where len(clipped4) == 2)
                [for (point in projection.segment([clipped4[0][0] + model.center[0], clipped4[0][1]],
                    [clipped4[1][0] + model.center[0], clipped4[1][1]], model)) projection.screen(projection.raw(point, model), model)]))] |: ~ != null
}

fn line_path(points, model) => join([for (piece in segments(points, model)) svg.line_path(piece)], " ")

fn shape_elements(shape, row, model, ctx, options) {
    let appearance = mark.style(ctx, row, options, {fill: color.default_color, stroke: "none", 'stroke-width': 1, opacity: 1.0});
    if (shape == null) []
    else if (shape.type == "Feature") shape_elements(shape.geometry, row, model, ctx, options)
    else if (shape.type == "FeatureCollection" or shape.type == "GeometryCollection") [
        for (item in (if (shape.type == "FeatureCollection") shape.features else shape.geometries))
            for (el in shape_elements(item, row, model, ctx, options)) el]
    else if (len(shape.coordinates) == 0) []
    else if (shape.type == "Point" or shape.type == "MultiPoint") [
        for (point in (if (shape.type == "Point") [shape.coordinates] else shape.coordinates),
            let projected = projection.project(point, model),
            let size = mark.appearance(ctx, "size", row, if (options.size != null) options.size else 25.0)
            where projected != null)
            if (not util.finite_number(size) or size < 0) error("chart: geographic point size must be finite and nonnegative")
            else <circle class: "geo-point", cx: projected[0], cy: projected[1], r: math.sqrt(size / util.PI), *:appearance, mark.tooltip(ctx, row)>]
    else if (shape.type == "LineString" or shape.type == "MultiLineString") [
        for (line in (if (shape.type == "LineString") [shape.coordinates] else shape.coordinates),
            let path = line_path(line, model) where path != "")
            <path class: "geo-line", d: path, *:mark.style(ctx, row, options,
                {fill: "none", stroke: color.default_color, 'stroke-width': 1, opacity: 1.0}, true), mark.tooltip(ctx, row)>]
    else [for (rings in (if (shape.type == "Polygon") [shape.coordinates] else shape.coordinates),
        let path = polygon(rings, model)) (
        if (path is error) path
        else if (path != "") <path class: "geo-shape", d: path, *:appearance, stroke: "none", 'fill-rule': "nonzero", mark.tooltip(ctx, row)>,
        // Stroke only source boundaries; mesh edges are private fill subdivisions.
        if (not (path is error) and appearance.stroke != "none" and appearance.stroke != null)
            <path class: "geo-outline", d: join([for (ring in rings) line_path(ring, model)], " "), *:appearance, fill: "none">)]
}

pub fn render(data, ctx, options) {
    let settings = if (ctx._navigation == true) ctx._projection else if (options.projection != null) options.projection else ctx._projection;
    let model = projection.configure(ctx.plot_w, ctx.plot_h, settings);
    let field = if (options.geometry_field != null) options.geometry_field else if (ctx.encoding.shape.field != null) ctx.encoding.shape.field else "geometry";
    let shapes = [for (row in data) if (row[field] != null) row[field]
        else if (ctx.encoding.longitude != null and ctx.encoding.latitude != null)
            {type: "Point", coordinates: [parse.channel_value(ctx.encoding.longitude, row), parse.channel_value(ctx.encoding.latitude, row)]}
        else null];
    let failure = util.first_error([model, for (shape in shapes) validate(shape)]);
    if (failure is error) failure else {
        let items = [for (index, shape in shapes) for (item in shape_elements(shape, data[index], model, ctx, options)) item];
        let invalid = util.first_error(items);
        if (invalid is error) invalid else svg.group_class("marks geoshape", items)
    }
}

// Scientific vectors express direction in radians and magnitude in longitude/latitude units.
pub fn vectors(data,ctx,options) {
    let settings=if (ctx._navigation==true) ctx._projection else if (options.projection!=null) options.projection else ctx._projection;
    let model=projection.configure(ctx.plot_w,ctx.plot_h,settings);
    let items=[for (row in data,
        let origin=[parse.channel_value(ctx.encoding.longitude,row),parse.channel_value(ctx.encoding.latitude,row)],
        let direction=parse.channel_value(ctx.encoding.direction,row,row[if (options.direction_field!=null) options.direction_field else "direction"]),
        let magnitude=parse.channel_value(ctx.encoding.magnitude,row,row[if (options.magnitude_field!=null) options.magnitude_field else "magnitude"]),
        let destination=primitive.vector_endpoint(origin,direction,magnitude,1.0),
        let a=projection.project(origin,model),let b=projection.project(destination,model),
        let appearance=mark.style(ctx,row,options,{fill:"none",stroke:color.default_color,'stroke-width':1.5,opacity:1.0},true))
        if (not projection.valid_position(origin) or not projection.valid_position(destination) or
            not util.finite_number(direction) or not util.finite_number(magnitude)) error("chart: geographic vector requires finite valid endpoints")
        else primitive.path_mark(line_path([origin,destination],model),appearance,
            {*:options,kind:"path",arrow:a!=null and b!=null},[mark.tooltip(ctx,row)],"geo-vector")];
    let failure=util.first_error([model,*items]);
    if (failure is error) failure else svg.group_class("marks vector",items)
}
