// Geometry translated from cssDOOM; GPL-2.0, see ATTRIBUTION.md.
pub type Point = {x: number, y: number}
pub fn distance_squared(a: Point, b: Point) => (a.x - b.x) ** 2 + (a.y - b.y) ** 2
pub fn cross(a: Point, b: Point) => a.x * b.y - a.y * b.x
pub fn subtract(a: Point, b: Point) => {x: a.x - b.x, y: a.y - b.y}
pub fn side(point: Point, a: Point, b: Point) => cross(subtract(point, a), subtract(b, a))
pub fn bounds(points: Point[]) {
    let xs = [for (point in points) point.x]
    let ys = [for (point in points) point.y]
    {min_x: min(xs), max_x: max(xs), min_y: min(ys), max_y: max(ys)}
}
pub fn contains_point(box, point) => point.x >= box.min_x and point.x <= box.max_x and
    point.y >= box.min_y and point.y <= box.max_y
pub fn overlaps(a, b) => a.min_x <= b.max_x and a.max_x >= b.min_x and
    a.min_y <= b.max_y and a.max_y >= b.min_y

fn edge_crosses(point: Point, a: Point, b: Point) bool => (a.y > point.y) != (b.y > point.y) and
    point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x
pub fn point_in_polygon(point: Point, polygon: Point[]) bool => len(polygon) >= 3 and
    len([for (i in 0 to (len(polygon) - 1)
        where edge_crosses(point, polygon[i], polygon[(i + 1) % len(polygon)])) i]) % 2 == 1
pub fn point_in_sector(point: Point, boundaries: Point[][]) bool => len(boundaries) > 0 and
    point_in_polygon(point, boundaries[0]) and
    not any([for (i in 1 to (len(boundaries) - 1)) point_in_polygon(point, boundaries[i])])

pub fn segment_distance_squared(point: Point, a: Point, b: Point) {
    let delta = subtract(b, a)
    let length_squared = delta.x ** 2 + delta.y ** 2
    let t = if (length_squared == 0) 0 else max(0, min(1,
        ((point.x - a.x) * delta.x + (point.y - a.y) * delta.y) / length_squared))
    distance_squared(point, {x: a.x + t * delta.x, y: a.y + t * delta.y})
}
pub fn circle_hits_segment(point: Point, radius: number, a: Point, b: Point) bool =>
    segment_distance_squared(point, a, b) < radius ** 2
pub fn crosses_segment(old: Point, next: Point, a: Point, b: Point) bool =>
    (side(old, a, b) > 0) != (side(next, a, b) > 0)

// Return the ray parameter so wall occlusion and combat share the same query.
pub fn ray_segment(origin: Point, direction: Point, a: Point, b: Point, limit: number) {
    let delta = subtract(b, a)
    let denominator = cross(direction, delta)
    if (abs(denominator) < 0.00000001) null
    else {
        let relative = subtract(a, origin)
        let t = cross(relative, delta) / denominator
        let u = cross(relative, direction) / denominator
        if (t > 0 and t < limit and u >= 0 and u <= 1) t else null
    }
}
pub fn css_position(x, y, z) => {x: x, y: -z, z: -y}
pub fn forward(angle) => {x: -math.sin(angle), y: math.cos(angle)}
pub fn right(angle) => {x: math.cos(angle), y: math.sin(angle)}
