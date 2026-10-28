// Scalar fields (Lambda_Pkg_Proc_Art.md PG5): `float[][]` rows, f[y][x], the
// shape of lambda.chart's grid matrices. Element-wise arithmetic (S10.2.1)
// already combines fields, so this module only adds what arithmetic cannot
// express. Everything is exact-tier (PG2): sqrt is the only non-basic op.
import core: ._field
import mesh: lambda.chart.mesh
import contour: lambda.chart.mesh_contour

pub fn make(width: int, height: int, f) array =>
    [for (y in 0 to (height - 1)) [for (x in 0 to (width - 1)) f(x, y)]]

pub fn transform(field, f) array => [for (row in field) [for (v in row) f(v)]]
pub fn width(field) int => if (len(field) == 0) 0 else len(field[0])
pub fn height(field) int => len(field)
pub fn bounds(field) array => [min([for (row in field) min(row)]), max([for (row in field) max(row)])]

// rescale to [0, 1]; a constant field maps to zeros.
pub fn normalize(field) array {
    let b = bounds(field);
    if (b[1] == b[0]) transform(field, (v) => 0.0) else (field - b[0]) / (b[1] - b[0])
}

pub fn remap(field, lo: float, hi: float) array => normalize(field) * (hi - lo) + lo
pub fn clamp(field, lo: float, hi: float) array => transform(field, (v) => max(lo, min(hi, v)))
pub fn terrace(field, steps: int) array => transform(field, (v) => floor(v * float(steps)) / float(steps))
// 1 where v >= level, else 0: the runtime's threshold.
pub fn threshold(field, level: float) array^ => lambda.sys.threshold(field, level)^

// distance from the centre, 1.0 at the midpoint of the nearer edge pair: an island falloff mask.
pub fn radial(width: int, height: int) array {
    let cx = float(width - 1) / 2.0
    let cy = float(height - 1) / 2.0
    let r = float(min(width, height) - 1) / 2.0;
    make(width, height, (x, y) => math.sqrt((float(x) - cx) * (float(x) - cx) + (float(y) - cy) * (float(y) - cy)) / r)
}

fn at_clamped(field, x: int, y: int) float =>
    field[max(0, min(height(field) - 1, y))][max(0, min(width(field) - 1, x))]

// bilinear interpolation, clamped at the edges.
pub fn sample(field, x: float, y: float) float {
    let xi = int(floor(x))
    let yi = int(floor(y))
    let u = x - float(xi)
    let v = y - float(yi)
    let top = at_clamped(field, xi, yi) + u * (at_clamped(field, xi + 1, yi) - at_clamped(field, xi, yi))
    let bottom = at_clamped(field, xi, yi + 1) + u * (at_clamped(field, xi + 1, yi + 1) - at_clamped(field, xi, yi + 1));
    top + v * (bottom - top)
}

// separable box blur with edge replication: the runtime's native running-sum blur.
pub fn blur(field, radius: int) array^ => if (radius <= 0) field else lambda.sys.blur(field, 2 * radius + 1)^

// piecewise-linear control points [input, output], clamped beyond their endpoints.
pub fn curve(field, stops) array^ {
    if (not core.valid(field) or not (stops is array) or len(stops) < 2 or
        not all([for (i, s in stops) s is array and len(s) == 2 and core.finite(s[0]) and core.finite(s[1]) and
            (i == 0 or s[0] > stops[i - 1][0])])) raise error("procart.field.curve: expected a finite field and increasing control points")
    else transform(field, (v) => if (v <= stops[0][0]) float(stops[0][1])
        else if (v >= stops[len(stops) - 1][0]) float(stops[len(stops) - 1][1])
        else (let i = [for (j, s in stops where s[0] > v) j][0], let a = stops[i - 1], let b = stops[i],
            float(a[1]) + (float(v) - float(a[0])) / float(b[0] - a[0]) * float(b[1] - a[1])))
}

// central differences inside, one-sided at edges; singleton axes have zero derivative.
pub fn gradient(field, spacing = [1.0, 1.0]) map^ {
    if (not core.valid(field) or not (spacing is array) or len(spacing) != 2 or
        not all([for (s in spacing) core.finite(s) and s > 0])) raise error("procart.field.gradient: expected a finite field and positive spacing")
    else (let w = width(field), let h = height(field),
        {dx: make(w, h, (x, y) => if (w < 2) 0.0 else
            float(field[y][min(w - 1, x + 1)] - field[y][max(0, x - 1)]) / (float(min(w - 1, x + 1) - max(0, x - 1)) * spacing[0])),
         dy: make(w, h, (x, y) => if (h < 2) 0.0 else
            float(field[min(h - 1, y + 1)][x] - field[max(0, y - 1)][x]) / (float(min(h - 1, y + 1) - max(0, y - 1)) * spacing[1]))})
}

pub fn slope(field, spacing = [1.0, 1.0]) array^ =>
    (let g = gradient(field, spacing)^, make(width(field), height(field), (x, y) => math.sqrt(g.dx[y][x] * g.dx[y][x] + g.dy[y][x] * g.dy[y][x])))

// Felzenszwalb-Huttenlocher lower envelope of squared-distance parabolas.
fn envelope_add(f, st, q: int) map {
    let n = len(st.sites);
    if (n == 0) {sites: [q], cuts: [-inf]}
    else (let v = st.sites[n - 1], let cut = (f[q] + float(q * q) - f[v] - float(v * v)) / float(2 * (q - v)),
        if (cut <= st.cuts[n - 1]) envelope_add(f, {sites: slice(st.sites, 0, n - 1), cuts: slice(st.cuts, 0, n - 1)}, q)
        else {sites: [*st.sites, q], cuts: [*st.cuts, cut]})
}
fn cut_at(cuts, x: float, lo: int, hi: int) int =>
    if (lo + 1 >= hi) lo else (let m = (lo + hi) div 2,
        if (cuts[m] <= x) cut_at(cuts, x, m, hi) else cut_at(cuts, x, lo, m))
fn distance_line(f) array {
    let sites = [for (i, v in f where v != inf) i]
    let envelope = reduce([{sites: [], cuts: []}, *sites], (st, q: int) => envelope_add(f, st, q));
    [for (x in 0 to (len(f) - 1)) if (len(sites) == 0) inf else
        (let v = envelope.sites[cut_at(envelope.cuts, float(x), 0, len(envelope.cuts))], float((x - v) * (x - v)) + f[v])]
}

// Euclidean distance to the nearest nonzero/true mask cell; no source gives inf.
pub fn distance_transform(mask) array^ {
    if (not core.valid(mask, true) or width(mask) > 4096 or height(mask) > 4096)
        raise error("procart.field.distance_transform: expected a finite rectangular mask with axes at most 4096")
    else if (width(mask) == 0) mask
    else (let horizontal = [for (row in mask) distance_line([for (v in row) if (v == false or v == 0) inf else 0.0])],
        let squared = transpose([for (column in transpose(horizontal)) distance_line(column)]),
        transform(squared, (v) => math.sqrt(v)))
}

// reuse chart's keyed triangle intersections and polyline stitching (PG9).
pub fn contours(field, levels, options = {}) array^ {
    if (not core.valid(field) or width(field) < 2 or height(field) < 2)
        raise error("procart.field.contours: expected a finite field of at least 2 x 2")
    else (let raw = mesh.grid_data(core.grid_data(field)),
        let paths = contour.contours(raw, "value", levels, options),
        if (paths is error) raise paths else paths)
}
