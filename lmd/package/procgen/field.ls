// Scalar fields (Lambda_Pkg_Proc_Gen.md PG5): `float[][]` rows, f[y][x], the
// shape of lambda.chart's grid matrices. Element-wise arithmetic (S10.2.1)
// already combines fields, so this module only adds what arithmetic cannot
// express. Everything is exact-tier (PG2): sqrt is the only non-basic op.

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
