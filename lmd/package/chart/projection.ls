// Spherical projection formulas operate in radians; public positions use GeoJSON degrees.
import util: .util
import geometry: .geometry

pub fn valid_position(point) => point is array and len(point) >= 2 and
    util.finite_number(point[0]) and util.finite_number(point[1]) and abs(point[0]) <= 180 and abs(point[1]) <= 90

fn natural(longitude, latitude) {
    let p2 = latitude * latitude;
    let p4 = p2 * p2;
    // Published Natural Earth I polynomial (Patterson / Šavrič / Jenny).
    [longitude * (0.8707 - 0.131979 * p2 + p4 * (-0.013791 + p4 * (0.003971 * p2 - 0.001529 * p4))),
        latitude * (1.007226 + p2 * (0.015085 + p4 * (-0.044475 + 0.028874 * p2 - 0.005916 * p4)))]
}

pub fn vector(point, model) {
    let longitude = util.deg_to_rad(point[0] - model.center[0]);
    let latitude = util.deg_to_rad(point[1]);
    let origin = util.deg_to_rad(model.center[1]);
    [math.cos(latitude) * math.sin(longitude),
        math.cos(origin) * math.sin(latitude) - math.sin(origin) * math.cos(latitude) * math.cos(longitude),
        math.sin(origin) * math.sin(latitude) + math.cos(origin) * math.cos(latitude) * math.cos(longitude)]
}

pub fn raw(point, model) {
    let longitude = util.deg_to_rad(point[0] - model.center[0]);
    let latitude = util.deg_to_rad(point[1]);
    let origin = util.deg_to_rad(model.center[1]);
    if (model.type == "orthographic") slice(vector(point, model), 0, 2)
    else if (model.type == "mercator") [longitude, log(math.tan(util.PI / 4.0 + latitude / 2.0)) - log(math.tan(util.PI / 4.0 + origin / 2.0))]
    else if (model.type == "equirectangular") [longitude, latitude - origin]
    else if (model.type == "natural_earth") (
        let p = natural(longitude, latitude), let zero = natural(0.0, origin), [p[0], p[1] - zero[1]])
    else if (model.n == 0) [longitude * model.cos_parallel, (math.sin(latitude) - math.sin(origin)) / model.cos_parallel]
    else (
        let rho = math.sqrt(max(0.0, model.c - 2.0 * model.n * math.sin(latitude))) / model.n,
        let rho0 = math.sqrt(max(0.0, model.c - 2.0 * model.n * math.sin(origin))) / model.n,
        [rho * math.sin(model.n * longitude), rho0 - rho * math.cos(model.n * longitude)])
}

pub fn screen(point, model) => [model.translate[0] + model.scale * point[0], model.translate[1] - model.scale * point[1]]
pub fn project(point, model) {
    let longitude = point[0] - model.center[0];
    let aligned = [model.center[0] + longitude - 360.0 * floor((longitude + 180.0) / 360.0), point[1]];
    if (not valid_position(point)) error("chart: geographic positions require finite longitude/latitude within ±180/±90 degrees")
    else if (model.type == "orthographic" and vector(point, model)[2] < 0) null
    else if (abs(point[1]) > model.latitude_limit) null
    else screen(raw(aligned, model), model)
}

pub fn configure(width, height, options = {}) {
    let kind = if (options is string) options else if (options.type != null) options.type else "mercator";
    let normalized = if (contains(["naturalEarth1", "natural-earth", "naturalEarth"], kind)) "natural_earth" else kind;
    let center = if (options.center != null) options.center else [0.0, 0.0];
    let parallels = if (options.parallels != null) options.parallels else [29.5, 45.5];
    let precision = if (options.precision != null) options.precision else 0.75;
    let padding = if (options.padding != null) options.padding else 0.0;
    let limit = if (normalized == "mercator") math.atan((math.exp(util.PI) - math.exp(-util.PI)) / 2.0) * 180.0 / util.PI else 90.0;
    if (not contains(["mercator", "equirectangular", "albers", "orthographic", "natural_earth"], normalized)) error("chart: unsupported geographic projection")
    else if (not valid_position(center) or abs(center[1]) > limit) error("chart: projection center lies outside its geographic extent")
    else if (not (parallels is array) or len(parallels) != 2 or
        len([for (parallel in parallels where not util.finite_number(parallel) or abs(parallel) >= 90) true]) > 0)
        error("chart: Albers parallels must be two latitudes strictly between the poles")
    else if (not util.finite_number(precision) or precision <= 0 or not util.finite_number(padding) or padding < 0 or
        width <= 2.0 * padding or height <= 2.0 * padding) error("chart: projection precision and available size must be positive")
    else {
        let sin0 = math.sin(util.deg_to_rad(parallels[0]));
        let n = (sin0 + math.sin(util.deg_to_rad(parallels[1]))) / 2.0;
        let base = {type: normalized, center: center, parallels: parallels, n: n, c: 1.0 + sin0 * (2.0 * n - sin0),
            cos_parallel: math.cos(util.deg_to_rad(parallels[0])), latitude_limit: limit, precision: precision};
        let critical = if (normalized == "albers" and n != 0) [for (index in -2 to 2,
            let angle = float(index) * 90.0 / n where abs(angle) <= 180.0) angle] else [];
        let candidates = if (normalized == "orthographic") [[-1.0, -1.0], [1.0, 1.0]]
            else [for (latitude in [-limit, 0.0, limit]) for (longitude in [-180.0, 0.0, 180.0, *critical])
                raw([center[0] + longitude, latitude], base)];
        let bounds = [min(candidates |> ~[0]), min(candidates |> ~[1]), max(candidates |> ~[0]), max(candidates |> ~[1])];
        let factor = if (options.scale != null) options.scale else min((width - 2.0 * padding) / (bounds[2] - bounds[0]),
            (height - 2.0 * padding) / (bounds[3] - bounds[1]));
        let offset = if (options.translate != null) options.translate else if (options.center != null) [width / 2.0, height / 2.0] else
            [width / 2.0 - factor * (bounds[0] + bounds[2]) / 2.0, height / 2.0 + factor * (bounds[1] + bounds[3]) / 2.0];
        if (not util.finite_number(factor) or factor <= 0 or not (offset is array) or len(offset) != 2 or
            len([for (value in offset where not util.finite_number(value)) true]) > 0) error("chart: projection scale and translation must be finite, with positive scale")
        else {*:base, scale: factor, translate: offset, step: math.sqrt(2.0 * precision / factor) * 180.0 / util.PI}
    }
}

pub fn span(a, b) => abs(a[0] - b[0]) + abs(a[1] - b[1])

// GeoJSON edges interpolate in longitude/latitude (RFC 7946 §3.1.1), not on great circles.
pub fn segment(a, b, model) {
    let middle = geometry.interpolate(a, b, 0.5);
    let pa = raw(a, model);
    let pb = raw(b, model);
    let pm = raw(middle, model);
    let deviation = model.scale * math.sqrt((pm[0] - (pa[0] + pb[0]) / 2.0) ** 2 + (pm[1] - (pa[1] + pb[1]) / 2.0) ** 2);
    if ((span(a, b) <= model.step and deviation <= model.precision) or middle == a or middle == b) [a, b]
    else {
        let left = segment(a, middle, model);
        [*slice(left, 0, len(left) - 1), *segment(middle, b, model)]
    }
}

// Clip a finely subdivided spherical triangle before its orthographic projection.
pub fn triangle(points, model) {
    let lengths = [for (index, point in points) span(point, points[(index + 1) % 3])];
    let longest = max(lengths);
    let vectors = [for (point in points) vector(point, model)];
    let invisible = max(vectors |> ~[2]) + util.deg_to_rad(longest) < 0;
    if (invisible) []
    else if (longest <= model.step) {
        let visible = geometry.clip(vectors, (point) => point[2]);
        if (len(visible) < 3) [] else [[for (point in visible) screen(point, model)]]
    } else {
        let edge = index_of(lengths, longest);
        let a = points[edge];
        let b = points[(edge + 1) % 3];
        let c = points[(edge + 2) % 3];
        let middle = geometry.interpolate(a, b, 0.5);
        [*triangle([a, middle, c], model), *triangle([middle, b, c], model)]
    }
}
