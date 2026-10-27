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

// each bit certifies a quarter cell covered by an accepted point. Every
// future distance is bounded by the same rounded farthest-corner operations.
fn cover_pair(dx2: float, dy02: float, dy12: float, r2: float) int =>
    (if (dx2 + dy02 < r2) 1 else 0) + (if (dx2 + dy12 < r2) 2 else 0)

fn cell_coverage(qx: float, qy: float, x0: float, y0: float, x1: float, y1: float, r2: float) int {
    let mx = x0 + (x1 - x0) * 0.5
    let my = y0 + (y1 - y0) * 0.5
    let dx0 = max(abs(qx - x0), abs(qx - mx))
    let dx1 = max(abs(qx - mx), abs(qx - x1))
    let dy0 = max(abs(qy - y0), abs(qy - my))
    let dy1 = max(abs(qy - my), abs(qy - y1));
    cover_pair(dx0 * dx0, dy0 * dy0, dy1 * dy1, r2) +
        4 * cover_pair(dx1 * dx1, dy0 * dy0, dy1 * dy1, r2)
}

// zero is a free candidate; bit 4 marks a conflict, low bits certify coverage.
fn conflict_span(grid, x: float, y: float, r2: float, variable: bool, first: int, stop: int,
        x0: float, y0: float, x1: float, y1: float) int {
    if (first > stop) 0 else {
        let q = grid[first]
        let status: int = if (q is array) (let qx = float(q[0]), let qy = float(q[1]),
            let dx = qx - x, let dy = qy - y,
            if (dx * dx + dy * dy < (if (variable) max(r2, float(q[2]) * float(q[2])) else r2))
                (16 + (if (variable) 0 else cell_coverage(qx, qy, x0, y0, x1, y1, r2))) else 0) else 0;
        if (status != 0) status else conflict_span(grid, x, y, r2, variable, first + 1, stop, x0, y0, x1, y1)
    }
}

fn conflict_rows(grid, nx: int, ci: int, cj: int, x: float, y: float, r2: float,
        reach: int, variable: bool, row: int, stop: int, x0: float, y0: float, x1: float, y1: float) int {
    if (row > stop) 0 else {
        // constant-radius corner cells two apart are at least r away.
        let trim = if (not variable and abs(row - cj) == 2) 1 else 0
        let first: int = row * nx + max(0, ci - reach + trim)
        let stop_cell: int = row * nx + min(nx - 1, ci + reach - trim)
        let status = conflict_span(grid, x, y, r2, variable, first, stop_cell, x0, y0, x1, y1);
        if (status != 0) status else
            conflict_rows(grid, nx, ci, cj, x, y, r2, reach, variable, row + 1, stop, x0, y0, x1, y1)
    }
}

fn conflicts(grid, nx: int, ny: int, ci: int, cj: int, x: float, y: float, r2: float,
        reach: int, variable: bool, x0: float, y0: float, x1: float, y1: float) int {
    let first: int = max(0, cj - reach)
    let stop: int = min(ny - 1, cj + reach);
    conflict_rows(grid, nx, ci, cj, x, y, r2, reach, variable, first, stop, x0, y0, x1, y1)
}

fn try_cell(grid, d, s: float, r2: float, nx: int, ny: int, kr: u64, c: int, covered: int) {
    let ci = c % nx
    let cj = c div nx
    let x = (float(ci) + rng.unit(rng.at(kr, 2 * c))) * s
    let y = (float(cj) + rng.unit(rng.at(kr, 2 * c + 1))) * s
    let density = if (d.density == null) 1.0 else field.sample(d.density,
        x / d.width * float(field.width(d.density) - 1), y / d.height * float(field.height(d.density) - 1))
    let radius = if (d.density == null or density <= 0.0) math.sqrt(r2) else min(d.max_radius, math.sqrt(r2) / math.sqrt(density));
    if (x >= d.width or y >= d.height or density <= 0.0) covered
    else {
        let status = conflicts(grid, nx, ny, ci, cj, x, y, if (d.density == null) r2 else radius * radius,
            if (d.reach == null) 2 else d.reach, d.density != null,
            float(ci) * s, float(cj) * s, float(ci + 1) * s, float(cj + 1) * s);
        // coverage only grows: accepted points never move or disappear.
        if (status != 0) bor(covered, band(status, 15))
        else if (d.density == null) [x, y] else [x, y, radius]
    }
}

fn poisson_phase(grid, d, s: float, r2: float, nx: int, ny: int, kr: u64, ph: int, stride: int) array {
    // copy untouched rows in bulk; only one row residue changes.
    let px = ph % stride
    let py = ph div stride;
    [for (j in 0 to (ny - 1)) *(if (j % stride != py) slice(grid, j * nx, (j + 1) * nx)
        else [for (i in 0 to (nx - 1)) (let c = j * nx + i, let q = grid[c],
            if (q is array or q == 15 or i % stride != px) q else try_cell(grid, d, s, r2, nx, ny, kr, c, q))])]
}

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
        let grid = reduce([[for (c in 0 to (nx * ny - 1)) 0], *[for (step in 0 to (attempts * phases - 1)) step]],
            (g, step: int) => poisson_phase(g, d, s, r * r, nx, ny, rng.at(k, step div phases), step % phases, stride));
        [for (q in grid where q is array) if (d.density == null) q else [q[0], q[1]]]
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
