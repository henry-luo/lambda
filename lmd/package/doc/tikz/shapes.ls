// Node shapes (base, shapes.geometric) over a measured label box.
// Geometry is in picture units (cm, y up), relative to the node center.
import opts: .options

let PI = 3.141592653589793
let PX_PER_CM = 96.0 / 2.54

pub let SHAPE_KEYS = ["rectangle", "circle", "ellipse", "diamond", "regular polygon",
    "star", "trapezium", "coordinate"]

// Keys that configure a shape rather than select it.
pub let PARAMETER_KEYS = ["aspect", "regular polygon sides", "star points",
    "star point ratio", "trapezium left angle", "trapezium right angle",
    "trapezium angle", "minimum width", "minimum height", "minimum size",
    "rounded corners", "shape"]

fn selected(node) {
    let explicit = opts.value(node, "shape", null)
    let flags = [for (key in SHAPE_KEYS where opts.has(node, key) and
        opts.value(node, key, "") == "") key]
    if (explicit != null) trim(explicit)
    else if (len(flags) > 0) flags[len(flags) - 1]
    else "rectangle"
}

fn cm_option(node, key) float^ {
    let raw = opts.value(node, key, null)
    if (raw == null) 0.0 else opts.dimension_px(raw)^ / PX_PER_CM
}

fn integer_option(node, key, fallback, low, high) int^ {
    let value = opts.numeric_value(opts.value(node, key, string(fallback)))^
    if (value < float(low) or value > float(high) or float(int(value)) != value)
        raise error("TikZ " ++ key ++ " must be an integer from " ++ string(low) ++
            " to " ++ string(high))
    else int(value)
}

fn angle_option(node, key, fallback) float^ {
    let value = opts.numeric_value(opts.value(node, key,
        opts.value(node, "trapezium angle", string(fallback))))^
    if (value <= 0.0 or value >= 180.0)
        raise error("TikZ " ++ key ++ " must be between 0 and 180 degrees")
    else value
}

// Validated shape parameters of a node, independent of its measured label.
pub fn shape_spec(node) any^ {
    let shape_name = selected(node)
    let min_size = cm_option(node, "minimum size")^
    let base = {shape: shape_name,
        min_width: max([cm_option(node, "minimum width")^, min_size]),
        min_height: max([cm_option(node, "minimum height")^, min_size])}
    let rounded = opts.value(node, "rounded corners", null)
    if (shape_name == "coordinate" or shape_name == "circle" or shape_name == "ellipse") base
    else if (shape_name == "rectangle")
        {*:base, radius: if (rounded == null) 0.0
            else if (rounded == "") 4.0 * 2.54 / 72.27
            else opts.dimension_px(rounded)^ / PX_PER_CM}
    else if (rounded != null) raise error("rounded corners need a rectangle node")
    else if (shape_name == "diamond") {
        let aspect = opts.numeric_value(opts.value(node, "aspect", "1"))^
        if (aspect <= 0.0) raise error("TikZ diamond aspect must be positive")
        else {*:base, aspect: aspect}
    }
    else if (shape_name == "regular polygon")
        {*:base, sides: integer_option(node, "regular polygon sides", 5, 3, 64)^}
    else if (shape_name == "star") {
        let ratio = opts.numeric_value(opts.value(node, "star point ratio", "1.5"))^
        if (ratio <= 0.0) raise error("TikZ star point ratio must be positive")
        else {*:base, points: integer_option(node, "star points", 5, 2, 64)^, ratio: ratio}
    }
    else if (shape_name == "trapezium")
        {*:base, left: angle_option(node, "trapezium left angle", 60.0)^,
         right: angle_option(node, "trapezium right angle", 60.0)^}
    else raise error("unsupported TikZ node shape: " ++ shape_name)
}

fn ring(radius, first_degrees, total) => [for (index in 0 to (total - 1))
    (let angle = (first_degrees + 360.0 * float(index) / float(total)) * PI / 180.0,
     [radius * math.cos(angle), radius * math.sin(angle)])]

fn bounds_of(vertices) {
    let xs = [for (vertex in vertices) abs(vertex[0])]
    let ys = [for (vertex in vertices) abs(vertex[1])];
    {half_w: max(xs), half_h: max(ys)}
}

fn polygon(kind, vertices) {
    let bounds = bounds_of(vertices);
    {kind: kind, vertices: vertices, half_w: bounds.half_w, half_h: bounds.half_h}
}

// Trapezium outline per PGF: side extensions are 2*half_h*cot(angle); a minimum
// height or width scales the whole outline (the `stretches` keys are not admitted).
fn trapezium(spec, width, height, min_width, min_height) {
    let cot = (degrees) => 1.0 / math.tan(degrees * PI / 180.0)
    let half_h0 = height / 2.0
    let grow = if (half_h0 > 0.0 and half_h0 < min_height / 2.0) min_height / 2.0 / half_h0
        else 1.0
    let half_h1 = max([half_h0 * grow, min_height / 2.0])
    let half_w1 = width / 2.0 * grow
    let left1 = 2.0 * half_h0 * cot(float(spec.left)) * grow
    let right1 = 2.0 * half_h0 * cot(float(spec.right)) * grow
    let total = 2.0 * half_w1 + abs(left1) + abs(right1)
    let widen = if (total > 0.0 and total < min_width) min_width / total else 1.0
    let half_w = half_w1 * widen
    let half_h = half_h1 * widen
    let left = left1 * widen
    let right = right1 * widen
    // An acute angle extends the bottom edge; an obtuse angle extends the top edge.
    polygon("trapezium", [
        [0.0 - half_w - max([left, 0.0]), 0.0 - half_h],
        [half_w + max([right, 0.0]), 0.0 - half_h],
        [half_w + max([0.0 - right, 0.0]), half_h],
        [0.0 - half_w - max([0.0 - left, 0.0]), half_h]])
}

// Outline geometry for a shape spec around a `width` x `height` cm label box.
pub fn geometry(spec, width, height) {
    let shape_name = spec.shape
    let min_width = float(spec.min_width)
    let min_height = float(spec.min_height)
    // A circle encloses the label box (half diagonal); polygons and stars use an
    // incircle of 1.41421 * max(half width, half height), as PGF does.
    let half_diagonal = math.sqrt(width * width + height * height) / 2.0
    let incircle = 1.41421 * max([width, height]) / 2.0
    if (shape_name == "coordinate") {kind: "coordinate", half_w: 0.0, half_h: 0.0}
    else if (shape_name == "rectangle")
        {kind: "rectangle", half_w: max([width, min_width]) / 2.0,
         half_h: max([height, min_height]) / 2.0, radius: float(spec.radius)}
    else if (shape_name == "circle") {
        let radius = max([half_diagonal, min_width / 2.0, min_height / 2.0]);
        {kind: "ellipse", rx: radius, ry: radius, half_w: radius, half_h: radius}
    }
    else if (shape_name == "ellipse") {
        // The ellipse through the box corners keeps the box aspect ratio.
        let rx = max([width / 2.0 * math.sqrt(2.0), min_width / 2.0])
        let ry = max([height / 2.0 * math.sqrt(2.0), min_height / 2.0]);
        {kind: "ellipse", rx: rx, ry: ry, half_w: rx, half_h: ry}
    }
    else if (shape_name == "diamond") {
        // The diamond's sides pass through the label box corners; minimums apply per axis.
        let aspect = float(spec.aspect)
        let half_wide = max([width / 2.0 + aspect * height / 2.0, min_width / 2.0])
        let half_tall = max([width / 2.0 / aspect + height / 2.0, min_height / 2.0])
        polygon("diamond", [[0.0, half_tall], [half_wide, 0.0],
            [0.0, 0.0 - half_tall], [0.0 - half_wide, 0.0]])
    }
    else if (shape_name == "regular polygon") {
        let sides = int(spec.sides)
        // One side lies at the bottom; an odd polygon has a vertex at the top.
        let outer = max([incircle / math.cos(PI / float(sides)),
            min_width / 2.0, min_height / 2.0])
        polygon("regular polygon", ring(outer, 270.0 - 180.0 / float(sides), sides))
    }
    else if (shape_name == "star") {
        let points = int(spec.points)
        let ratio = float(spec.ratio)
        // A minimum size enlarges the outer radius; the inner radius keeps the ratio.
        let outer = max([incircle * ratio, min_width / 2.0, min_height / 2.0])
        let tips = ring(outer, 90.0, points)
        let valleys = ring(outer / ratio, 90.0 + 180.0 / float(points), points)
        polygon("star", [for (index in 0 to (points - 1), vertex in [tips[index], valleys[index]])
            vertex])
    }
    else trapezium(spec, width, height, min_width, min_height)
}

fn ray_hit(dx, dy, a, b) {
    // Solve center + t*d = a + s*(b - a) for t > 0 and s in [0, 1].
    let ex = b[0] - a[0]
    let ey = b[1] - a[1]
    let det = dx * (0.0 - ey) - dy * (0.0 - ex)
    if (abs(det) < 1e-12) null
    else {
        let t = (a[0] * (0.0 - ey) - a[1] * (0.0 - ex)) / det
        let s = (dx * a[1] - dy * a[0]) / det
        if (t > 0.0 and s >= -1e-9 and s <= 1.0 + 1e-9) t else null
    }
}

// Boundary point on the ray from the center toward (dx, dy), relative to the center.
pub fn border_offset(geom, dx, dy) {
    if (dx == 0.0 and dy == 0.0) [0.0, 0.0]
    else if (geom.kind == "coordinate") [0.0, 0.0]
    else if (geom.kind == "rectangle") {
        let factor = min([if (dx == 0.0) 1e100 else geom.half_w / abs(dx),
                          if (dy == 0.0) 1e100 else geom.half_h / abs(dy)]);
        [dx * factor, dy * factor]
    }
    else if (geom.kind == "ellipse") {
        let factor = 1.0 / math.sqrt((dx / geom.rx) * (dx / geom.rx) +
            (dy / geom.ry) * (dy / geom.ry));
        [dx * factor, dy * factor]
    }
    else {
        let vertex_count = len(geom.vertices)
        let hits = [for (index in 0 to (vertex_count - 1),
            let t = ray_hit(dx, dy, geom.vertices[index],
                geom.vertices[(index + 1) % vertex_count]) where t != null) t]
        // A star is star-shaped about its center, so the nearest crossing is its border.
        let t = if (len(hits) == 0) 0.0 else min(hits);
        [dx * t, dy * t]
    }
}

// Compass anchors as exact unit directions (sign pattern for rectangle corners).
let COMPASS = [{name: "east", x: 1.0, y: 0.0}, {name: "north east", x: 1.0, y: 1.0},
    {name: "north", x: 0.0, y: 1.0}, {name: "north west", x: -1.0, y: 1.0},
    {name: "west", x: -1.0, y: 0.0}, {name: "south west", x: -1.0, y: -1.0},
    {name: "south", x: 0.0, y: -1.0}, {name: "south east", x: 1.0, y: -1.0}]

fn compass_direction(anchor) {
    let matches = [for (entry in COMPASS where entry.name == anchor) entry]
    if (len(matches) == 0) null else matches[0]
}

// Anchor offset from the center: compass names, `center`, or a border angle in
// degrees. Rectangle compass anchors are box edge midpoints and corners; PGF puts
// diagonal ellipse anchors at parameter angle 45 and diagonal diamond anchors at
// side midpoints; other shapes use the border toward the compass direction.
pub fn anchor_offset(geom, anchor) any^ {
    let anchor_name = trim(anchor)
    let compass = compass_direction(anchor_name)
    let diagonal = compass != null and compass.x != 0.0 and compass.y != 0.0
    let numeric = if (compass == null and anchor_name != "center")
        opts.numeric_value(anchor_name) ^ { null } else null
    if (anchor_name == "center") [0.0, 0.0]
    else if (compass != null and geom.kind == "rectangle")
        [compass.x * geom.half_w, compass.y * geom.half_h]
    else if (diagonal and geom.kind == "ellipse")
        [compass.x * 0.707107 * geom.rx, compass.y * 0.707107 * geom.ry]
    else if (diagonal and geom.kind == "diamond")
        [compass.x * geom.half_w / 2.0, compass.y * geom.half_h / 2.0]
    else if (compass != null) border_offset(geom, compass.x, compass.y)
    else if (numeric != null)
        border_offset(geom, math.cos(numeric * PI / 180.0), math.sin(numeric * PI / 180.0))
    else raise error("unsupported TikZ anchor: " ++ anchor_name)
}

// Outline vertices or radii in px around a px center (SVG y down).
pub fn outline_svg(geom, cx, cy, px_per_cm, paint) {
    let fill_paint = if (paint.fill == null) "none" else paint.fill
    let stroke = if (paint.stroke == null) "none" else paint.stroke
    if (geom.kind == "coordinate") null
    else if (geom.kind == "rectangle")
        <rect x: cx - geom.half_w * px_per_cm, y: cy - geom.half_h * px_per_cm,
            width: 2.0 * geom.half_w * px_per_cm, height: 2.0 * geom.half_h * px_per_cm,
            rx: geom.radius * px_per_cm, ry: geom.radius * px_per_cm,
            fill: fill_paint, stroke: stroke, 'stroke-width': paint.width,
            'stroke-dasharray': paint.dash>
    else if (geom.kind == "ellipse")
        <ellipse cx: cx, cy: cy, rx: geom.rx * px_per_cm, ry: geom.ry * px_per_cm,
            fill: fill_paint, stroke: stroke, 'stroke-width': paint.width,
            'stroke-dasharray': paint.dash>
    else <polygon points: join([for (vertex in geom.vertices)
            string(cx + vertex[0] * px_per_cm) ++ "," ++ string(cy - vertex[1] * px_per_cm)], " "),
        fill: fill_paint, stroke: stroke, 'stroke-width': paint.width,
        'stroke-dasharray': paint.dash, 'stroke-linejoin': "miter">
}
