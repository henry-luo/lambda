// one arc-distance table drives projection, inverse picking, and drag coordinates.
import c: .common
import util: lambda.chart.util
import paths: lambda.chart.path_geometry
import geometry: lambda.chart.geometry
import svg: lambda.chart.svg

fn unit(vector, fallback = [1.0,0.0]) {
    let length=math.sqrt(vector[0]*vector[0]+vector[1]*vector[1]);
    if (length==0) fallback else [vector[0]/length,vector[1]/length]
}
fn vertex_tangent(points, index) {
    let before=if (index>0) unit([points[index][0]-points[index-1][0],points[index][1]-points[index-1][1]]) else null;
    let after=if (index+1<len(points)) unit([points[index+1][0]-points[index][0],points[index+1][1]-points[index][1]]) else null;
    if (before==null) after else if (after==null) before else unit([before[0]+after[0],before[1]+after[1]],after)
}
fn table(points, index = 0, length = 0.0, result = []) {
    if (index >= len(points)) result else {
        let next = length + (if (index == 0) 0.0 else paths.distance(points[index - 1], points[index]));
        table(points, index + 1, next, [*result, {point:points[index], distance:next,tangent:vertex_tangent(points,index)}])
    }
}
fn serpent(options, width, height, vertical) {
    let levels = c.option(options, "levels", 4);
    let long = if (vertical) height else width;
    let cross = if (vertical) width else height;
    let step = cross / max(1, levels - 1);
    let radius = min(c.option(options, "turn_radius", step / 2.0), min(step / 2.0, long / 2.0));
    let point = (x,y) => if (vertical) [y,x] else [x,y];
    let a = point(radius, 0);
    svg.M(a[0], a[1]) ++ " " ++ join([for (index in 0 to (levels - 1),
        let y = index * step, let forward = index % 2 == 0,
        let x = if (forward) long - radius else radius, let edge = if (forward) long else 0,
        let b = point(x,y), let control_a = point(edge,y),
        let control_b = point(edge,y+step), let end = point(x,y+step))
        svg.L(b[0],b[1]) ++ (if (index + 1 < levels) " " ++ svg.C(control_a[0],control_a[1],
            control_b[0],control_b[1],end[0],end[1]) else "")], " ")
}

pub fn configure(options, box, orientation = "horizontal") {
    let kind = c.text(c.option(options, "kind", "straight"));
    let precision = c.option(options, "precision", 0.5);
    let vertical = orientation == "vertical";
    let levels = c.option(options, "levels", 4);
    let turns = c.option(options, "turns", 3);
    let radius = min(box.width, box.height) / 2.0;
    let inner = c.option(options, "inner_radius", radius * 0.1);
    let outer = c.option(options, "outer_radius", radius);
    let start = c.option(options, "start_location", 0.0);
    let end = c.option(options, "end_location", 1.0);
    let invalid = util.first_error([
        if (not c.positive(box.width) or not c.positive(box.height)) c.fail("coordinate.box", "expected positive finite dimensions"),
        if (not c.positive(precision)) c.fail("coordinate.precision", "expected positive precision"),
        if (kind == "serpentine" and (not c.integral(levels) or levels < 2 or levels > 1000)) c.fail("coordinate.levels", "expected 2 to 1000 levels"),
        if (kind == "spiral" and (not c.positive(turns) or turns > 1000 or not util.finite_number(inner) or not c.positive(outer) or inner < 0 or inner >= outer)) c.fail("coordinate", "invalid spiral turns or radii"),
        if (not util.finite_number(start) or not util.finite_number(end) or start < 0 or end > 1 or start >= end) c.fail("coordinate", "locations must satisfy 0 <= start < end <= 1"),
        if (kind == "curve" and options.path == null and options.points == null and not (options.point is fn)) c.fail("coordinate.curve", "provide a path, points, or point function")]);
    if (invalid is error) invalid else {
        let points = if (kind == "straight") if (vertical) [[box.width/2.0,0.0],[box.width/2.0,box.height]]
            else [[0.0,box.height/2.0],[box.width,box.height/2.0]]
        else if (kind == "spiral") {
            let angle = util.deg_to_rad(c.option(options, "start_angle", -90));
            let finish = if (options.end_angle != null) util.deg_to_rad(options.end_angle) else angle + util.TAU * turns;
            paths.sample_curve((t) => (
                let r = util.lerp(inner,outer,t), let a = util.lerp(angle,finish,t),
                [box.width/2.0+r*math.cos(a), box.height/2.0+r*math.sin(a)]), precision, max(8,ceil(turns)*8))
        } else if (kind == "serpentine") {
            let sampled = paths.sample(serpent(options,box.width,box.height,vertical),(p)=>p,precision);
            if (sampled is error) sampled else sampled[0].points
        } else if (kind == "curve") {
            if (options.point is fn) paths.sample_curve(options.point,precision,16)
            else {
                let source = if (options.path != null) options.path else svg.line_path(options.points,
                    c.text(c.option(options,"smoothing","linear")));
                let sampled = paths.sample(source,(p)=>p,precision);
                if (sampled is error) sampled else if (len(sampled) != 1) c.fail("coordinate.path", "timeline path must be continuous") else sampled[0].points
            }
        } else c.fail("coordinate.kind", "unknown path kind");
        if (points is error) points else if (len(points) < 2 or not all([for (p in points) p is array and len(p)==2 and all(p |> util.finite_number(~))]))
            c.fail("coordinate.points", "path needs at least two finite points")
        else {
            let entries = table(points);
            let length = entries[len(entries)-1].distance;
            if (length == 0) c.fail("coordinate.path", "path has zero length") else
                {kind:kind, entries:entries, length:length, start:start, end:end,
                    reverse:options.reverse == true, origin:[c.option(box,"x",0),c.option(box,"y",0)],
                    path:svg.line_path(points), precision:precision}
        }
    }
}

pub fn project(model, fraction, offset = 0.0) {
    let t = if (model.reverse) 1.0 - fraction else fraction;
    let distance = util.lerp(model.start,model.end,util.clamp_val(t,0,1))*model.length;
    let matches = [for (index in 1 to (len(model.entries)-1) where model.entries[index].distance >= distance) index];
    let index = if (len(matches)>0) matches[0] else len(model.entries)-1;
    let a = model.entries[index-1]; let b = model.entries[index];
    let length = b.distance-a.distance;
    let local = if (length == 0) 0.0 else (distance-a.distance)/length;
    // Continuous normals keep offset lanes and projected area boundaries continuous.
    let tangent=unit(geometry.interpolate(a.tangent,b.tangent,local));
    let center = geometry.interpolate(a.point,b.point,local);
    {point:[center[0]+model.origin[0]-tangent[1]*offset,center[1]+model.origin[1]+tangent[0]*offset],
        tangent:tangent, normal:[-tangent[1],tangent[0]], angle:util.rad_to_deg(math.atan2(tangent[1],tangent[0])), fraction:fraction}
}

pub fn invert(model, point) {
    let local = [for (axis in [0,1]) point[axis]-model.origin[axis]];
    let candidates = [for (index in 1 to (len(model.entries)-1),
        let a=model.entries[index-1], let b=model.entries[index],
        let hit=geometry.nearest_segment(local,a.point,b.point))
        {*:hit,distance_along:util.lerp(a.distance,b.distance,hit.t)}];
    let hit = sort(candidates,{by:(candidate)=>candidate.distance})[0];
    let t = (hit.distance_along/model.length-model.start)/(model.end-model.start);
    {*:hit,fraction:util.clamp_val(if (model.reverse) 1.0-t else t,0,1),
        point:[for (axis in [0,1]) hit.point[axis]+model.origin[axis]]}
}
