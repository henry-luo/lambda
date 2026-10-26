// Point sampling (Lambda_Pkg_Proc_Gen.md §4 procgen.sample). Points are [x, y]
// floats in a width x height domain. Every draw is keyed (PG3) and every
// sampler uses only basic arithmetic, so all of it is exact-tier (PG2).
import rng: .rng
import field: .field
import core: ._field

fn frac(x: float) float => x - floor(x)
fn cells(extent: float, side: float) int => int(ceil(extent / side))

fn domain(options) map^ {
    let w = options.width
    let h = options.height;
    if (not (w is number) or not (h is number) or w <= 0 or h <= 0)
        raise error("procgen.sample: width and height must be positive numbers")
    else {width: float(w), height: float(h)}
}

// --- Poisson disk: no two points closer than `radius` ---
// The functional form of Wei's parallel dart throwing (2008): cells of side
// r/sqrt(2) hold at most one point, and cells whose indices agree mod 3 can
// never conflict, so each of the 9 phases fills its cells from the previous
// grid in one pure comprehension. Each round gives every empty cell one keyed
// candidate; `attempts` rounds approach a maximal sampling.
fn phase_of(c: int, nx: int, stride: int) int => (c % nx) % stride + stride * ((c div nx) % stride)

fn conflicts(grid, nx: int, ny: int, ci: int, cj: int, x: float, y: float, r2: float, reach: int, variable: bool) bool =>
    any([for (dj in (0 - reach) to reach) for (di in (0 - reach) to reach) (let i = ci + di, let j = cj + dj,
        // corner cells two apart are at least r away, so they are skipped
        if ((not variable and abs(di) == 2 and abs(dj) == 2) or i < 0 or j < 0 or i >= nx or j >= ny) false
        else (let q = grid[j * nx + i], q != null and (q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y) <
            (if (variable) max(r2, q[2] * q[2]) else r2)))])

fn try_cell(grid, d, s: float, r2: float, nx: int, ny: int, kr: u64, c: int) {
    let ci = c % nx
    let cj = c div nx
    let x = (float(ci) + rng.unit(rng.at(kr, 2 * c))) * s
    let y = (float(cj) + rng.unit(rng.at(kr, 2 * c + 1))) * s
    let density = if (d.density == null) 1.0 else field.sample(d.density,
        x / d.width * float(field.width(d.density) - 1), y / d.height * float(field.height(d.density) - 1))
    let radius = if (d.density == null or density <= 0.0) math.sqrt(r2) else min(d.max_radius, math.sqrt(r2) / math.sqrt(density));
    if (x >= d.width or y >= d.height or density <= 0.0 or
        conflicts(grid, nx, ny, ci, cj, x, y, if (d.density == null) r2 else radius * radius,
            if (d.reach == null) 2 else d.reach, d.density != null)) null
    else if (d.density == null) [x, y] else [x, y, radius]
}

fn poisson_phase(grid, d, s: float, r2: float, nx: int, ny: int, kr: u64, ph: int, stride: int) array =>
    [for (c, q in grid) if (q != null or phase_of(c, nx, stride) != ph) q else try_cell(grid, d, s, r2, nx, ny, kr, c)]

// the domain and phases with every value typed, so the round fold stays error-free.
fn poisson_grid(k: u64, d, width: float, height: float, r: float, attempts: int) array^ {
    let s = r * 0.7071067811865476
    let nx = cells(width, s)
    let ny = cells(height, s)
    let stride: int = if (d.density == null) 3 else int(ceil(d.max_radius / s)) + 1
    let phases: int = stride * stride;
    if (nx * ny > 4000000) raise error("procgen.sample.poisson: more than 4,000,000 cells; raise the radius")
    else {
        // step = pass * 9 + phase
        let grid = reduce([[for (c in 0 to (nx * ny - 1)) null], *[for (step in 0 to (attempts * phases - 1)) step]],
            (g, step: int) => poisson_phase(g, d, s, r * r, nx, ny, rng.at(k, step div phases), step % phases, stride));
        [for (q in grid where q != null) if (d.density == null) q else [q[0], q[1]]]
    }
}

pub fn poisson(k: u64, options) array^ {
    let d = domain(options)^
    let attempts = if (options.attempts == null) 20 else options.attempts;
    if (not (options.radius is number) or options.radius <= 0) raise error("procgen.sample.poisson: radius must be positive")
    else if (not (attempts is int) or attempts < 1 or attempts > 100)
        raise error("procgen.sample.poisson: attempts must be an int in 1..100")
    else if (options.density == null) poisson_grid(k, d, d.width, d.height, float(options.radius), attempts)^
    else {
        let maximum = if (options.max_radius == null) float(options.radius) * 4.0 else options.max_radius;
        if (not core.valid(options.density) or field.width(options.density) == 0 or
            not all([for (row in options.density) for (v in row) v >= 0 and v <= 1]) or
            not core.finite(maximum) or maximum < options.radius or maximum / options.radius > 16)
            raise error("procgen.sample.poisson: density must be in [0,1], max_radius in radius..16*radius")
        else poisson_grid(k, {*: d, density: options.density, max_radius: float(maximum),
                reach: int(ceil(maximum / (float(options.radius) * 0.7071067811865476)))},
            d.width, d.height, float(options.radius), attempts)^
    }
}

// --- one jittered point per square cell of side `cell` ---
pub fn jitter(k: u64, options) array^ {
    let d = domain(options)^
    let size: float = if (options.cell is number) float(options.cell) else 0.0
    let amount: float = if (options.jitter == null) 1.0 else float(options.jitter);
    if (size <= 0.0) raise error("procgen.sample.jitter: cell must be positive")
    else (let nx = cells(d.width, size), let ny = cells(d.height, size),
        [for (j in 0 to (ny - 1)) for (i in 0 to (nx - 1)) (let c = j * nx + i,
            let x = (float(i) + 0.5 + (rng.unit(rng.at(k, 2 * c)) - 0.5) * amount) * size,
            let y = (float(j) + 0.5 + (rng.unit(rng.at(k, 2 * c + 1)) - 0.5) * amount) * size,
            if (x < d.width and y < d.height) [x, y] else null)] |: ~ != null)
}

// --- low-discrepancy sequences ---
// the base-b digit reversal of i as one exact division of two ints.
fn radical_from(i: int, base: int, num: int, den: int) float =>
    if (i == 0) float(num) / float(den) else radical_from(i div base, base, num * base + i % base, den * base)

pub fn radical_inverse(i: int, base: int) float => radical_from(i, base, 0, 1)

// Halton points (bases 2 and 3) from index 1, shifted by a keyed Cranley-Patterson rotation.
pub fn halton(k: u64, n: int, options) array^ {
    let d = domain(options)^
    let u = rng.unit(rng.at(k, 0))
    let v = rng.unit(rng.at(k, 1));
    [for (i in 1 to n) [frac(radical_inverse(i, 2) + u) * d.width, frac(radical_inverse(i, 3) + v) * d.height]]
}

// Roberts' R2 sequence: steps of 1/g and 1/g^2 for the plastic number g, written as binary64 constants.
pub fn r2(k: u64, n: int, options) array^ {
    let d = domain(options)^
    let u = rng.unit(rng.at(k, 0))
    let v = rng.unit(rng.at(k, 1));
    [for (i in 1 to n) [frac(u + float(i) * 0.7548776662466927) * d.width,
        frac(v + float(i) * 0.5698402909980532) * d.height]]
}

// --- Mitchell's best candidate: each point is the farthest of `candidates` keyed tries ---
fn nearest2(points, x: float, y: float) float =>
    if (len(points) == 0) 1e300 else min([for (q in points) (q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y)])

fn best_of(points, d, k: u64, i: int, m: int) array {
    let tries = [for (t in 0 to (m - 1)) (let kt = rng.at(rng.at(k, i), t),
        let x = rng.unit(rng.at(kt, 0)) * d.width, let y = rng.unit(rng.at(kt, 1)) * d.height, [nearest2(points, x, y), x, y])]
    // the first of equally far candidates wins, so ties stay deterministic
    let far = max([for (c in tries) c[0]])
    let pick = [for (c in tries where c[0] == far) c][0];
    [pick[1], pick[2]]
}

pub fn best_candidate(k: u64, options) array^ {
    let d = domain(options)^
    let n = options.count
    let m = if (options.candidates == null) 10 else options.candidates;
    if (not (n is int) or n < 0 or n > 4000) raise error("procgen.sample.best_candidate: count must be an int in 0..4000")
    else if (not (m is int) or m < 1) raise error("procgen.sample.best_candidate: candidates must be a positive int")
    else reduce([[], *[for (i in 0 to (n - 1)) i]], (points, i) => [*points, best_of(points, d, k, i, m)])
}

// --- density-weighted sampling over a field: P(cell) is proportional to its value ---
fn first_above(cumulative, target: float, lo: int, hi: int) int {
    if (lo >= hi) lo
    else (let mid = (lo + hi) div 2, if (cumulative[mid] > target) first_above(cumulative, target, lo, mid)
        else first_above(cumulative, target, mid + 1, hi))
}

// points in field coordinates: cell (x, y) covers [x, x + 1) x [y, y + 1).
pub fn weighted(k: u64, field, n: int) array^ {
    let w = if (len(field) == 0) 0 else len(field[0])
    let values = [for (row in field) for (v in row) float(v)]
    let cumulative = math.cumsum(values)
    let total = if (len(values) == 0) 0.0 else cumulative[len(values) - 1];
    if (any([for (v in values) v < 0.0])) raise error("procgen.sample.weighted: field values must be non-negative")
    else if (total <= 0.0) raise error("procgen.sample.weighted: field has no positive weight")
    else [for (i in 0 to (n - 1)) (let c = first_above(cumulative, rng.unit(rng.at(k, 3 * i)) * total, 0, len(values) - 1),
        [float(c % w) + rng.unit(rng.at(k, 3 * i + 1)), float(c div w) + rng.unit(rng.at(k, 3 * i + 2))])]
}
