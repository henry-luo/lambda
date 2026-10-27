// Coherent noise (Lambda_Pkg_Proc_Gen.md §4 procgen.noise, PG10). Point
// functions take a 512-entry permutation table from `table`; `make` prepares a
// generator (one table per octave) and `sample`/`field` evaluate it. Every
// kernel uses only integer ops and IEEE basic arithmetic, with gradient tables
// and skew factors written as constants, so all of it is exact-tier (PG2).
import rng: .rng
import core: ._field

// the classic doubled permutation: lattice lookups never need a second wrap.
pub fn table(k: u64) array {
    let p = rng.perm(k, 256);
    [*p, *p]
}

fn fade(t: float) float => t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
fn lerp(a: float, b: float, t: float) float => a + t * (b - a)
fn lattice(v: float) int => int(floor(v))

// --- value and white noise: one hashed value per lattice point ---
fn hv2(p, x: int, y: int) float => float(p[p[x] + y]) / 127.5 - 1.0
fn hv3(p, x: int, y: int, z: int) float => float(p[p[p[x] + y] + z]) / 127.5 - 1.0

pub fn white2(p, x: float, y: float) float => hv2(p, band(lattice(x), 255), band(lattice(y), 255))
pub fn white3(p, x: float, y: float, z: float) float =>
    hv3(p, band(lattice(x), 255), band(lattice(y), 255), band(lattice(z), 255))

pub fn value2(p, x: float, y: float) float {
    let xi = lattice(x)
    let yi = lattice(y)
    let a = band(xi, 255)
    let b = band(yi, 255)
    let u = fade(x - float(xi))
    let v = fade(y - float(yi));
    lerp(lerp(hv2(p, a, b), hv2(p, a + 1, b), u), lerp(hv2(p, a, b + 1), hv2(p, a + 1, b + 1), u), v)
}

pub fn value3(p, x: float, y: float, z: float) float {
    let xi = lattice(x)
    let yi = lattice(y)
    let zi = lattice(z)
    let a = band(xi, 255)
    let b = band(yi, 255)
    let c = band(zi, 255)
    let u = fade(x - float(xi))
    let v = fade(y - float(yi))
    let w = fade(z - float(zi));
    lerp(lerp(lerp(hv3(p, a, b, c), hv3(p, a + 1, b, c), u), lerp(hv3(p, a, b + 1, c), hv3(p, a + 1, b + 1, c), u), v),
        lerp(lerp(hv3(p, a, b, c + 1), hv3(p, a + 1, b, c + 1), u),
            lerp(hv3(p, a, b + 1, c + 1), hv3(p, a + 1, b + 1, c + 1), u), v), w)
}

// --- gradient (Perlin) noise ---
// eight unit gradients; the diagonal is 1/sqrt(2) as a constant, not a run-time sqrt.
fn grad2(h: int, x: float, y: float) float {
    let g = band(h, 7);
    if (g == 0) x else if (g == 1) -x else if (g == 2) y else if (g == 3) -y
    else if (g == 4) (x + y) * 0.7071067811865476 else if (g == 5) (y - x) * 0.7071067811865476
    else if (g == 6) (x - y) * 0.7071067811865476 else -(x + y) * 0.7071067811865476
}

// Perlin's improved-noise gradient: the twelve cube-edge directions, padded to sixteen.
fn grad3(h: int, x: float, y: float, z: float) float {
    let g = band(h, 15)
    let u = if (g < 8) x else y
    let v = if (g < 4) y else if (g == 12 or g == 14) x else z;
    (if (band(g, 1) == 0) u else -u) + (if (band(g, 2) == 0) v else -v)
}

// scaled by sqrt(2) so the output spans roughly [-1, 1].
pub fn perlin2(p, x: float, y: float) float {
    let xi = lattice(x)
    let yi = lattice(y)
    let a = band(xi, 255)
    let b = band(yi, 255)
    let xf = x - float(xi)
    let yf = y - float(yi)
    let u = fade(xf)
    let v = fade(yf);
    1.4142135623730951 * lerp(lerp(grad2(p[p[a] + b], xf, yf), grad2(p[p[a + 1] + b], xf - 1.0, yf), u),
        lerp(grad2(p[p[a] + b + 1], xf, yf - 1.0), grad2(p[p[a + 1] + b + 1], xf - 1.0, yf - 1.0), u), v)
}

// Perlin, "Improving Noise" (2002).
pub fn perlin3(p, x: float, y: float, z: float) float {
    let xi = lattice(x)
    let yi = lattice(y)
    let zi = lattice(z)
    let xf = x - float(xi)
    let yf = y - float(yi)
    let zf = z - float(zi)
    let u = fade(xf)
    let v = fade(yf)
    let w = fade(zf)
    let a = p[band(xi, 255)] + band(yi, 255)
    let aa = p[a] + band(zi, 255)
    let ab = p[a + 1] + band(zi, 255)
    let b = p[band(xi, 255) + 1] + band(yi, 255)
    let ba = p[b] + band(zi, 255)
    let bb = p[b + 1] + band(zi, 255);
    lerp(lerp(lerp(grad3(p[aa], xf, yf, zf), grad3(p[ba], xf - 1.0, yf, zf), u),
            lerp(grad3(p[ab], xf, yf - 1.0, zf), grad3(p[bb], xf - 1.0, yf - 1.0, zf), u), v),
        lerp(lerp(grad3(p[aa + 1], xf, yf, zf - 1.0), grad3(p[ba + 1], xf - 1.0, yf, zf - 1.0), u),
            lerp(grad3(p[ab + 1], xf, yf - 1.0, zf - 1.0), grad3(p[bb + 1], xf - 1.0, yf - 1.0, zf - 1.0), u), v), w)
}

// --- simplex noise (Gustavson, "Simplex noise demystified"; the patent expired in 2022) ---
// the twelve cube-edge gradients, decoded into scalar coefficients instead of row views.
fn simplex_dot(g: int, x: float, y: float, z: float, dimensions: int) float {
    let h = g % 12
    let a = if (h < 8) (if (band(h, 1) == 0) 1.0 else -1.0) else 0.0
    let b = if (h < 4) (if (band(h, 2) == 0) 1.0 else -1.0)
        else if (h >= 8) (if (band(h, 1) == 0) 1.0 else -1.0) else 0.0
    let xy = a * x + b * y;
    // retain zero products and left-to-right sums for exact signed-zero parity (PG2).
    if (dimensions == 2) xy else (let c = if (h < 4) 0.0 else (if (band(h, 2) == 0) 1.0 else -1.0), xy + c * z)
}

fn corner2(g: int, x: float, y: float) float {
    let t = 0.5 - x * x - y * y;
    if (t < 0.0) 0.0 else t * t * t * t * simplex_dot(g, x, y, 0.0, 2)
}

fn corner3(g: int, x: float, y: float, z: float) float {
    let t = 0.6 - x * x - y * y - z * z;
    if (t < 0.0) 0.0 else t * t * t * t * simplex_dot(g, x, y, z, 3)
}

// F2 = (sqrt(3) - 1) / 2 and G2 = (3 - sqrt(3)) / 6, as their binary64 values.
pub fn simplex2(p, x: float, y: float) float {
    let s = (x + y) * 0.3660254037844386
    let i = lattice(x + s)
    let j = lattice(y + s)
    let t = float(i + j) * 0.21132486540518713
    let x0 = x - (float(i) - t)
    let y0 = y - (float(j) - t)
    let i1 = if (x0 > y0) 1 else 0
    let j1 = 1 - i1
    let ii = band(i, 255)
    let jj = band(j, 255);
    70.0 * (corner2(p[ii + p[jj]], x0, y0) +
        corner2(p[ii + i1 + p[jj + j1]], x0 - float(i1) + 0.21132486540518713, y0 - float(j1) + 0.21132486540518713) +
        corner2(p[ii + 1 + p[jj + 1]], x0 - 1.0 + 2.0 * 0.21132486540518713, y0 - 1.0 + 2.0 * 0.21132486540518713))
}

// the second and third simplex corners, by the rank order of the offsets.
fn order3(x: float, y: float, z: float) array {
    if (x >= y) (if (y >= z) [1, 0, 0, 1, 1, 0] else if (x >= z) [1, 0, 0, 1, 0, 1] else [0, 0, 1, 1, 0, 1])
    else (if (y < z) [0, 0, 1, 0, 1, 1] else if (x < z) [0, 1, 0, 0, 1, 1] else [0, 1, 0, 1, 1, 0])
}

// F3 = 1/3 and G3 = 1/6, as their binary64 values.
pub fn simplex3(p, x: float, y: float, z: float) float {
    let s = (x + y + z) * 0.3333333333333333
    let i = lattice(x + s)
    let j = lattice(y + s)
    let k = lattice(z + s)
    let t = float(i + j + k) * 0.16666666666666666
    let x0 = x - (float(i) - t)
    let y0 = y - (float(j) - t)
    let z0 = z - (float(k) - t)
    let o = order3(x0, y0, z0)
    let ii = band(i, 255)
    let jj = band(j, 255)
    let kk = band(k, 255)
    let g1 = 0.16666666666666666
    let g2 = 2.0 * 0.16666666666666666
    let g3 = 3.0 * 0.16666666666666666;
    32.0 * (corner3(p[ii + p[jj + p[kk]]], x0, y0, z0) +
        corner3(p[ii + o[0] + p[jj + o[1] + p[kk + o[2]]]],
            x0 - float(o[0]) + g1, y0 - float(o[1]) + g1, z0 - float(o[2]) + g1) +
        corner3(p[ii + o[3] + p[jj + o[4] + p[kk + o[5]]]],
            x0 - float(o[3]) + g2, y0 - float(o[4]) + g2, z0 - float(o[5]) + g2) +
        corner3(p[ii + 1 + p[jj + 1 + p[kk + 1]]], x0 - 1.0 + g3, y0 - 1.0 + g3, z0 - 1.0 + g3))
}

// --- cellular (Worley) noise: distances to jittered feature points, one per cell ---
fn metric_distance(metric, dx: float, dy: float, dz: float) float {
    if (metric == 'manhattan') abs(dx) + abs(dy) + abs(dz)
    else if (metric == 'chebyshev') max(abs(dx), max(abs(dy), abs(dz)))
    else math.sqrt(dx * dx + dy * dy + dz * dz)
}

// a feature coordinate in [0, 1) from one table entry; offsets pick unrelated entries per axis.
fn jitter_of(p, h: int, offset: int, jitter: float) float => 0.5 + ((float(p[h + offset]) + 0.5) / 256.0 - 0.5) * jitter

// [f1, f2]: the nearest and second-nearest feature distances.
pub fn cellular2(p, x: float, y: float, jitter = 1.0, metric = 'euclidean') array {
    let xi = lattice(x)
    let yi = lattice(y)
    let ds = sort([for (dx in -1 to 1) for (dy in -1 to 1)
        (let cx = xi + dx, let cy = yi + dy, let h = p[p[band(cx, 255)] + band(cy, 255)],
            metric_distance(metric, float(cx) + jitter_of(p, h, 0, jitter) - x,
                float(cy) + jitter_of(p, h, 101, jitter) - y, 0.0))]);
    [ds[0], ds[1]]
}

pub fn cellular3(p, x: float, y: float, z: float, jitter = 1.0, metric = 'euclidean') array {
    let xi = lattice(x)
    let yi = lattice(y)
    let zi = lattice(z)
    let ds = sort([for (dx in -1 to 1) for (dy in -1 to 1) for (dz in -1 to 1)
        (let cx = xi + dx, let cy = yi + dy, let cz = zi + dz,
            let h = p[p[p[band(cx, 255)] + band(cy, 255)] + band(cz, 255)],
            metric_distance(metric, float(cx) + jitter_of(p, h, 0, jitter) - x,
                float(cy) + jitter_of(p, h, 101, jitter) - y, float(cz) + jitter_of(p, h, 211, jitter) - z))]);
    [ds[0], ds[1]]
}

// --- generators: kind + fractal + optional domain warp ---
fn either(value, fallback) => if (value == null) fallback else value

fn cell_value(gen, pair) float {
    if (gen.cell == 'f2') pair[1] else if (gen.cell == 'f2-f1') pair[1] - pair[0] else pair[0]
}

fn base2(gen, p, x: float, y: float) float {
    if (gen.kind == 'simplex') simplex2(p, x, y)
    else if (gen.kind == 'perlin') perlin2(p, x, y)
    else if (gen.kind == 'value') value2(p, x, y)
    else if (gen.kind == 'white') white2(p, x, y)
    else cell_value(gen, cellular2(p, x, y, gen.jitter, gen.metric))
}

fn base3(gen, p, x: float, y: float, z: float) float {
    if (gen.kind == 'simplex') simplex3(p, x, y, z)
    else if (gen.kind == 'perlin') perlin3(p, x, y, z)
    else if (gen.kind == 'value') value3(p, x, y, z)
    else if (gen.kind == 'white') white3(p, x, y, z)
    else cell_value(gen, cellular3(p, x, y, z, gen.jitter, gen.metric))
}

// octave shaping (FastNoiseLite conventions): ridged folds the signal, billow inverts the fold.
fn fold_octave(fractal, n: float) float {
    if (fractal == 'ridged') 1.0 - 2.0 * abs(n) else if (fractal == 'billow') 2.0 * abs(n) - 1.0 else n
}

// amplitudes and frequencies are running products, never pow(), so they stay exact.
fn running(first: float, factor: float, n: int) array =>
    math.cumprod([first, *[for (i in 1 to (n - 1)) factor]])

pub fn make(k: u64, spec = {}) map^ {
    let kind = either(spec.kind, 'simplex')
    let fractal = either(spec.fractal, if (spec.octaves == null) 'none' else 'fbm')
    let octaves = if (fractal == 'none') 1 else either(spec.octaves, 4);
    if (not contains(['simplex', 'perlin', 'value', 'white', 'cellular'], kind))
        raise error("procgen.noise: unknown kind " ++ string(kind))
    else if (not contains(['none', 'fbm', 'ridged', 'billow'], fractal))
        raise error("procgen.noise: unknown fractal " ++ string(fractal))
    else if (not (octaves is int) or octaves < 1 or octaves > 16)
        raise error("procgen.noise: octaves must be an int in 1..16")
    else if (spec.period != null and (not (spec.period is array) or not (len(spec.period) in [2, 3]) or
        not all([for (p in spec.period) core.finite(p) and p > 0])))
        raise error("procgen.noise: period must contain two or three positive finite lengths")
    else {
        let amps = running(1.0, float(either(spec.gain, 0.5)), octaves);
        {kind: kind, fractal: fractal, octaves: octaves, period: spec.period,
         frequencies: running(float(either(spec.frequency, 0.03125)), float(either(spec.lacunarity, 2.0)), octaves),
         amplitudes: amps, total: sum(amps),
         jitter: float(either(spec.jitter, 1.0)), metric: either(spec.metric, 'euclidean'),
         cell: either(spec.cell, 'f1'),
         tables: [for (o in 0 to (octaves - 1)) table(rng.derive(k, ["octave", o]))],
         warp: if (spec.warp == null) null
             else {amplitude: float(either(spec.warp.amplitude, 1.0)),
                   frequency: float(either(spec.warp.frequency, 1.0)),
                   tables: [table(rng.derive(k, "warp-x")), table(rng.derive(k, "warp-y")), table(rng.derive(k, "warp-z"))]}}
    }
}

fn octaves2(gen, x: float, y: float) float {
    let fractal = gen.fractal
    let tables = gen.tables
    let frequencies = gen.frequencies
    let amplitudes = gen.amplitudes
    let total = gen.total;
    if (fractal == 'none') base2(gen, tables[0], x * frequencies[0], y * frequencies[0])
    else sum([for (o in 0 to (gen.octaves - 1))
        amplitudes[o] * fold_octave(fractal, base2(gen, tables[o], x * frequencies[o], y * frequencies[o]))]) /
        total
}

fn octaves3(gen, x: float, y: float, z: float) float {
    let fractal = gen.fractal
    let tables = gen.tables
    let frequencies = gen.frequencies
    let amplitudes = gen.amplitudes
    let total = gen.total;
    if (fractal == 'none') base3(gen, tables[0], x * frequencies[0], y * frequencies[0], z * frequencies[0])
    else sum([for (o in 0 to (gen.octaves - 1)) amplitudes[o] * fold_octave(fractal,
        base3(gen, tables[o], x * frequencies[o], y * frequencies[o], z * frequencies[o]))]) / total
}

// domain warp: offset the sample point by simplex noise before evaluating the generator.
fn sample_raw(gen, x: float, y: float) float {
    if (gen.warp == null) octaves2(gen, x, y)
    else (let w = gen.warp, let wx = x * w.frequency, let wy = y * w.frequency,
        octaves2(gen, x + w.amplitude * simplex2(w.tables[0], wx, wy), y + w.amplitude * simplex2(w.tables[1], wx, wy)))
}

fn sample3_raw(gen, x: float, y: float, z: float) float {
    if (gen.warp == null) octaves3(gen, x, y, z)
    else (let w = gen.warp, let wx = x * w.frequency, let wy = y * w.frequency, let wz = z * w.frequency,
        octaves3(gen, x + w.amplitude * simplex3(w.tables[0], wx, wy, wz),
            y + w.amplitude * simplex3(w.tables[1], wx, wy, wz), z + w.amplitude * simplex3(w.tables[2], wx, wy, wz)))
}

// quintic cross-fades preserve seam derivatives for smooth kernels;
// this also tiles fractals and domain warp without transcendental embeddings (PG2).
fn periodic(gen, coordinates, dimension: int) float {
    let count = min(dimension, len(gen.period))
    let wrapped = [for (i in 0 to (dimension - 1)) if (i < count)
        coordinates[i] - floor(coordinates[i] / gen.period[i]) * gen.period[i] else coordinates[i]]
    let t = [for (i in 0 to (count - 1)) fade(wrapped[i] / gen.period[i])];
    sum([for (corner in 0 to ([1, 2, 4, 8][count] - 1))
        (let bits = [for (i in 0 to (count - 1)) band(corner, [1, 2, 4][i]) != 0],
         let point = [for (i in 0 to (dimension - 1)) wrapped[i] - (if (i < count and bits[i]) gen.period[i] else 0.0)],
         // reductions live under math; an unresolved call poisons every periodic sample.
         let weight = math.prod([for (i in 0 to (count - 1)) if (bits[i]) t[i] else 1.0 - t[i]]),
         weight * (if (dimension == 2) sample_raw(gen, point[0], point[1]) else sample3_raw(gen, point[0], point[1], point[2])))])
}

pub fn sample(gen, x: float, y: float) float =>
    if (gen.period == null) sample_raw(gen, x, y) else periodic(gen, [x, y], 2)
pub fn sample3(gen, x: float, y: float, z: float) float =>
    if (gen.period == null) sample3_raw(gen, x, y, z) else periodic(gen, [x, y, z], 3)

// prepare each octave's table once per row, then reduce along the octave axis.
// sum(..., 0) preserves each point's octave order and bounds scratch to one row.
fn octave_row(gen, p, width: int, y: float, frequency: float, amplitude: float, fractal, weighted: bool) array =>
    [for (x in 0 to (width - 1)) (let n = base2(gen, p, float(x) * frequency, y * frequency),
        if (weighted) amplitude * fold_octave(fractal, n) else n)]

fn field_row(gen, width: int, y: float) array {
    let tables = gen.tables
    let frequencies = gen.frequencies
    let amplitudes = gen.amplitudes
    let fractal = gen.fractal;
    if (fractal == 'none') octave_row(gen, tables[0], width, y, float(frequencies[0]), 1.0, fractal, false)
    else (let layers = [for (o in 0 to (gen.octaves - 1))
        octave_row(gen, tables[o], width, y, float(frequencies[o]), float(amplitudes[o]), fractal, true)],
        sum(layers, 0) / gen.total)
}

// warp and periodic blending retain the point sampler; ordinary fields batch each row (PG5).
pub fn field(gen, width: int, height: int) array =>
    if (gen.warp == null and gen.period == null and width > 0 and height > 0)
        [for (y in 0 to (height - 1)) field_row(gen, width, float(y))]
    else [for (y in 0 to (height - 1)) [for (x in 0 to (width - 1)) sample(gen, float(x), float(y))]]
