// Small geometry operations shared by geographic clipping and triangulation.
pub fn interpolate(a, b, t) => [for (index, value in a) value + (b[index] - value) * t]
pub fn cross(a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
pub fn area(points) => sum([for (index, point in points,
    let next = points[(index + 1) % len(points)]) point[0] * next[1] - next[0] * point[1]]) / 2.0

// A signed-distance callback lets the same clipping step handle planar and 3D boundaries.
pub fn clip(points, distance) {
    [for (index, b in points,
        let a = points[(index + len(points) - 1) % len(points)],
        let da = distance(a), let db = distance(b)) (
        if ((da < 0) != (db < 0)) interpolate(a, b, da / (da - db)),
        if (db >= 0) b)] |: ~ != null
}

pub fn clip_segment(a, b, distance) {
    let da = distance(a);
    let db = distance(b);
    if (da < 0 and db < 0) []
    else if (da < 0) [interpolate(a, b, da / (da - db)), b]
    else if (db < 0) [a, interpolate(a, b, da / (da - db))]
    else [a, b]
}

fn in_triangle(p, a, b, c) => cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0

fn on_segment(a, b, p) => cross(a, b, p) == 0 and p[0] >= min(a[0], b[0]) and p[0] <= max(a[0], b[0]) and
    p[1] >= min(a[1], b[1]) and p[1] <= max(a[1], b[1])

fn intersects(a, b, c, d) {
    let ac = cross(a, b, c);
    let ad = cross(a, b, d);
    let ca = cross(c, d, a);
    let cb = cross(c, d, b);
    (ac * ad < 0 and ca * cb < 0) or on_segment(a, b, c) or on_segment(a, b, d) or on_segment(c, d, a) or on_segment(c, d, b)
}

pub fn simple_ring(points) => len([for (index in 0 to (len(points) - 2))
    for (other in (index + 2) to (len(points) - 2) where not (index == 0 and other == len(points) - 2) and
        intersects(points[index], points[index + 1], points[other], points[other + 1])) true]) == 0

fn ears(points) {
    if (len(points) < 3) []
    else if (len(points) == 3) if (cross(points[0], points[1], points[2]) == 0) [] else [points]
    else {
        let candidates = [for (index, b in points,
            let ai = (index + len(points) - 1) % len(points), let ci = (index + 1) % len(points),
            let a = points[ai], let c = points[ci]
            where cross(a, b, c) >= 0 and (cross(a, b, c) == 0 or
                len([for (other, p in points where other != ai and other != index and other != ci and in_triangle(p, a, b, c)) p]) == 0)) index];
        if (len(candidates) == 0) error("chart: geographic ring must be simple and non-self-intersecting")
        else {
            let index = candidates[0];
            let triangle = [points[(index + len(points) - 1) % len(points)], points[index], points[(index + 1) % len(points)]];
            let rest = ears([for (other, point in points where other != index) point]);
            if (rest is error) rest else [*(if (cross(triangle[0], triangle[1], triangle[2]) != 0) [triangle] else []), *rest]
        }
    }
}

pub fn triangulate(ring) {
    let outline = if (len(ring) > 1 and ring[0] == ring[len(ring) - 1]) slice(ring, 0, len(ring) - 1) else ring;
    let points = [for (index, point in outline where index == 0 or point != outline[index - 1]) point];
    ears(if (area(points) < 0) reverse(points) else points)
}

// Properties remain directly addressable by ordinary chart encodings; GeoJSON metadata wins collisions.
pub fn records(value) {
    if (value.type == "FeatureCollection") if (not (value.features is array)) error("chart: GeoJSON FeatureCollection requires a features array")
        else [for (feature in value.features) {*:if (feature.properties is map) feature.properties else {}, *:feature}]
    else if (value.type == "Feature") [{*:if (value.properties is map) value.properties else {}, *:value}]
    else if (value is map and value.type != null) [{geometry: value}]
    else if (value is array) [for (row in value) if (row.type == "Feature") {*:if (row.properties is map) row.properties else {}, *:row}
        else if (row.type != null and row.geometry == null) {geometry: row} else row]
    else value
}
