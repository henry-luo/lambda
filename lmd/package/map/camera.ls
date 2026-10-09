import numbers: lambda.chart.numbers
import projection: lambda.chart.projection

pub let max_latitude = 85.0511287798066
let pi = 3.141592653589793
pub fn wrap(value, period = 360.0) => value - floor(value / period + 0.5) * period
pub fn mercator_y(latitude) {
    let radians = max([-max_latitude, min([max_latitude, latitude])]) * pi / 180.0;
    (1.0 - math.log(math.tan(pi / 4.0 + radians / 2.0)) / pi) / 2.0
}
fn latitude(y) => math.atan((math.exp(pi * (1.0 - 2.0 * y)) - math.exp(-pi * (1.0 - 2.0 * y))) / 2.0) * 180.0 / pi
fn value(v, fallback) => if (v == null) fallback else v
fn screen(c, dx, dy) {
    let angle = c.bearing * pi / 180.0, world = 512.0 * 2.0 ** c.zoom;
    [c.width / 2.0 + world * (math.cos(angle) * dx + math.sin(angle) * dy),
     c.height / 2.0 + world * (-math.sin(angle) * dx + math.cos(angle) * dy)]
}
pub fn camera(spec) {
    let center = value(spec.center, [0.0,0.0]);
    let zoom = value(spec.zoom, 0.0), bearing = value(spec.bearing, 0.0);
    let width = value(spec.width, 300.0), height = value(spec.height, 150.0);
    if (not projection.valid_position(center) or len(center) != 2 or
        not all([for (n in [zoom,bearing,width,height]) numbers.finite_number(n)]) or
        zoom < -2 or zoom > 22 or width <= 0 or height <= 0 or value(spec.pitch,0) != 0 or
        value(spec.projection,"mercator") != "mercator" or value(spec["world-copies"],false) != false)
        error("map: invalid Mercator camera; pitch must be zero and world-copies false")
    else {center: [wrap(center[0]),max([-max_latitude,min([max_latitude,center[1]])])],
        zoom: zoom, bearing: wrap(bearing), pitch: 0.0, width: width, height: height}
}
pub fn project(spec, position) {
    let c = camera(spec);
    if (c is error) c else if (not projection.valid_position(position)) error("map: invalid position")
    else {
        let dx = wrap((position[0] - c.center[0]) / 360.0, 1.0);
        let dy = mercator_y(position[1]) - mercator_y(c.center[1]);
        screen(c,dx,dy)
    }
}

// carry the unwrapped world offset between connected vertices, as the native painter does.
fn sequence(c, points, start, end, previous) {
    if (start >= end) {last:previous,points:[]}
    else if (end-start == 1) {
        let point = points[start];
        let dx = wrap((point[0]-c.center[0])/360.0-previous,1.0)+previous;
        {last:dx,points:[screen(c,dx,mercator_y(point[1])-mercator_y(c.center[1]))]}
    } else {
        let middle = start+int(floor((end-start)/2.0));
        let left = sequence(c,points,start,middle,previous);
        let right = sequence(c,points,middle,end,left.last);
        {last:right.last,points:[*left.points,*right.points]}
    }
}
pub fn project_sequence(c, points, anchor = 0.0) => sequence(c,points,0,len(points),anchor).points
pub fn unproject(spec, point) {
    let c = camera(spec);
    if (c is error) c else if (not (point is array) or len(point) != 2 or not all(point |> numbers.finite_number(~)))
        error("map: invalid viewport point")
    else {
        let world = 512.0 * 2.0 ** c.zoom, angle = c.bearing * pi / 180.0;
        let dx = (point[0] - c.width / 2.0) / world, dy = (point[1] - c.height / 2.0) / world;
        [wrap(c.center[0] + 360.0 * (math.cos(angle) * dx - math.sin(angle) * dy)),
         latitude(mercator_y(c.center[1]) + math.sin(angle) * dx + math.cos(angle) * dy)]
    }
}
pub fn pan(spec, dx, dy) {
    let c = camera(spec);
    if (c is error) c else if (not numbers.finite_number(dx) or not numbers.finite_number(dy)) error("map: invalid pan delta")
    else camera({*:c,center:unproject(c,[c.width/2.0-dx,c.height/2.0-dy])})
}
pub fn zoom_at(spec, zoom, point = null) {
    let c = camera(spec);
    let anchor = if (point == null) [c.width/2.0,c.height/2.0] else point;
    let location = if (c is error) c else unproject(c,anchor);
    if (location is error) location else if (not numbers.finite_number(zoom)) error("map: invalid zoom")
    else {
        let next = camera({*:c,zoom:max([-2.0,min([22.0,zoom])])});
        let projected = project(next,location);
        pan(next,anchor[0]-projected[0],anchor[1]-projected[1])
    }
}
pub fn fit_bounds(spec, bounds, padding = 0.0) {
    let c = camera(spec);
    if (c is error) c else if (not (bounds is array) or len(bounds) != 4 or
        not all(bounds |> numbers.finite_number(~)) or abs(bounds[0]) > 180 or abs(bounds[2]) > 180 or
        abs(bounds[1]) > 90 or abs(bounds[3]) > 90 or bounds[1] > bounds[3] or
        not numbers.finite_number(padding) or padding < 0 or padding * 2 >= min([c.width,c.height]))
        error("map: invalid bounds or padding")
    else {
        let east = bounds[2] + (if (bounds[2] < bounds[0]) 360.0 else 0.0);
        let dx = (east - bounds[0]) / 360.0, top = mercator_y(bounds[3]), bottom = mercator_y(bounds[1]);
        let angle = c.bearing * pi / 180.0;
        let span_x = abs(math.cos(angle)) * dx + abs(math.sin(angle)) * (bottom-top);
        let span_y = abs(math.sin(angle)) * dx + abs(math.cos(angle)) * (bottom-top);
        let scale = min([if (span_x > 0) (c.width-2*padding) / (512*span_x) else 2.0**22,
                         if (span_y > 0) (c.height-2*padding) / (512*span_y) else 2.0**22]);
        camera({*:c,center:[wrap((bounds[0]+east)/2.0),latitude((top+bottom)/2.0)],
            zoom:max([-2.0,min([22.0,math.log2(scale)])])})
    }
}
