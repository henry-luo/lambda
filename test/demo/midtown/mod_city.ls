// The procedural city of Lumen Bay: a 6 x 6 grid of blocks between seven avenues and seven streets,
// right-hand traffic, signalised junctions, and a harbour beyond the south barrier. Everything here
// is a pure function of PLAN and SEED, so the scene, the driving rules and the goldens agree.
// World axes: +Y up, x east, z south; a heading h faces [sin h, cos h] in x/z.
import u: .mod_util
import rng: lambda.procart.rng

pub let N = 6                       // blocks per side
pub let PITCH = 64.0                // road centreline spacing
pub let ROAD = 6.0                  // centreline to curb
pub let WALK = 4.0                  // sidewalk width
pub let LANE = 3.0                  // lane centre to road centreline
pub let CURB = 0.15                 // sidewalk and lot height
pub let EDGE = 9.0                  // junction zone half size: crosswalk outer edge, where turns begin
pub let HALF = PITCH * N / 2.0      // outermost road centreline
pub let LIMIT = HALF + ROAD + WALK  // inner face of the perimeter barrier
pub let SHORE = LIMIT + 14.0        // harbour wall beyond the south promenade
pub let SEED = "lumen-bay"

// block kinds, one row per street band from north (z < 0) to the harbour (z > 0):
// R residential, O offices, T towers, P park, X plaza, C construction yard, S stunt lot
pub let PLAN = ["RROORR", "RPOOOR", "OOTTOC", "OOTTOO", "ROXOPR", "SRROPR"]
let KEY = rng.key(SEED)

pub fn road(i) => (i - N / 2) * PITCH
// the nearest road index to a coordinate, clamped to the grid
pub fn nearest(c) => int(max(0, min(N, round(c / PITCH + N / 2)))) ^ { 0 }
pub fn kind_at(bi, bj) any^ => slice(PLAN[bj], bi, bi + 1)
pub fn inside_city(x, z) => abs(x) < LIMIT and abs(z) < LIMIT
pub fn on_road(x, z) =>
    (abs(x - road(nearest(x))) < ROAD and abs(z) < HALF + ROAD) or (abs(z - road(nearest(z))) < ROAD and abs(x) < HALF + ROAD)

pub let BLOCKS = [for (bj in 0 to (N - 1), bi in 0 to (N - 1)) {bi: bi, bj: bj, kind: kind_at(bi, bj),
    x0: road(bi), x1: road(bi + 1), z0: road(bj), z1: road(bj + 1),
    lx0: road(bi) + ROAD + WALK, lx1: road(bi + 1) - ROAD - WALK, lz0: road(bj) + ROAD + WALK, lz1: road(bj + 1) - ROAD - WALK}]
pub fn block_at(x, z) any^ {
    let bi = int(floor(x / PITCH + N / 2)), bj = int(floor(z / PITCH + N / 2));
    if (bi < 0 or bj < 0 or bi >= N or bj >= N) null else BLOCKS[bj * N + bi]
}

// facade styles: 0 glass curtain wall, 1 stone offices, 2 brick flats, 3 white panels
pub let STYLES = 4
fn pick_style(k, kind) any^ {
    let r = rng.unit(rng.derive(k, "style"));
    if (kind == "T") (if (r < 0.7) 0 else 3)
    else if (kind == "O") (if (r < 0.45) 1 else if (r < 0.7) 0 else 3)
    else (if (r < 0.6) 2 else 3)
}
fn building(k, kind, x0, x1, z0, z1, h, base = true) =>
    {x0: x0, x1: x1, z0: z0, z1: z1, h: h, style: pick_style(k, kind), base: base, kind: kind,
     u0: rng.below(rng.derive(k, "u"), 8) / 8.0, v0: rng.below(rng.derive(k, "v"), 8) / 8.0}
// a parcel's building, set back a little and sometimes left open as a courtyard
fn parcel(k, kind, x0, x1, z0, z1, lo, hi) any^ {
    let back = rng.between(rng.derive(k, "back"), 0.0, 1.5)
    if (rng.chance(rng.derive(k, "open"), 0.12)) []
    else [building(k, kind, x0 + back, x1 - back, z0 + back, z1 - back, round(rng.between(rng.derive(k, "h"), lo, hi) / 3.5) * 3.5 + 1.0)]
}
// split [a, b] into n parcels with gaps between them
fn spans(a, b, n, gap) => (let w = (b - a - gap * (n - 1)) / n, [for (i in 0 to (n - 1)) [a + i * (w + gap), a + i * (w + gap) + w]])

fn block_buildings(b) {
    let k = rng.derive(KEY, ["block", b.bi, b.bj]);
    if (b.kind == "T") {
        // a tower on a podium-free plaza with a narrower crown
        let w = rng.between(rng.derive(k, "w"), 26.0, 32.0), d = rng.between(rng.derive(k, "d"), 26.0, 32.0)
        let cx = (b.lx0 + b.lx1) / 2.0 + rng.between(rng.derive(k, "cx"), -3.0, 3.0)
        let cz = (b.lz0 + b.lz1) / 2.0 + rng.between(rng.derive(k, "cz"), -3.0, 3.0)
        let h = rng.between(rng.derive(k, "th"), 78.0, 128.0)
        let podium = building(k, "T", cx - w / 2.0, cx + w / 2.0, cz - d / 2.0, cz + d / 2.0, round(h * 0.68))
        let inset = 3.5;
        [podium, {*: building(rng.derive(k, "crown"), "T", cx - w / 2.0 + inset, cx + w / 2.0 - inset, cz - d / 2.0 + inset, cz + d / 2.0 - inset, h, false),
            style: podium.style, crown: true}]
    } else if (b.kind == "O") {
        let wide = rng.chance(rng.derive(k, "wide"), 0.35)
        let xs = spans(b.lx0, b.lx1, 2, 2.0), zs = spans(b.lz0, b.lz1, if (wide) 1 else 2, 2.0);
        [for (i, sx in xs, j, sz in zs) *parcel(rng.derive(k, [i, j]), "O", sx[0], sx[1], sz[0], sz[1], 16.0, 46.0)]
    } else if (b.kind == "R") {
        let xs = spans(b.lx0, b.lx1, 2, 1.5), zs = spans(b.lz0, b.lz1, 3, 1.5);
        [for (i, sx in xs, j, sz in zs) *parcel(rng.derive(k, [i, j]), "R", sx[0], sx[1], sz[0], sz[1], 8.0, 21.0)]
    } else []
}
pub let BUILDINGS = [for (b in BLOCKS, bd in block_buildings(b)^) bd]

// ---- street furniture: lamps along every block side, trees on homely streets and in parks
fn side_points(b, offsets, inset) => [for (t in offsets) *[
    {x: b.x0 + t, z: b.z0 + inset, face: u.PI}, {x: b.x0 + t, z: b.z1 - inset, face: 0.0},
    {x: b.x0 + inset, z: b.z0 + t, face: 0.0 - u.PI / 2.0}, {x: b.x1 - inset, z: b.z0 + t, face: u.PI / 2.0}]]
// lamp arms reach out over the road they face
pub let LAMPS = [for (b in BLOCKS, p in side_points(b, [23.0, 41.0], ROAD + 1.0)^) p]
fn park_trees(b) {
    let k = rng.derive(KEY, ["park", b.bi, b.bj]);
    [for (i in 0 to 3, j in 0 to 3, let kk = rng.derive(k, [i, j]) where not ((i == 1 or i == 2) and (j == 1 or j == 2)))
        {x: b.lx0 + 6.0 + i * 10.7 + rng.between(rng.derive(kk, "x"), -2.0, 2.0),
         z: b.lz0 + 6.0 + j * 10.7 + rng.between(rng.derive(kk, "z"), -2.0, 2.0), s: rng.between(rng.derive(kk, "s"), 0.85, 1.3)}]
}
pub let TREES = [*[for (b in BLOCKS, p in side_points(b, [13.0, 32.0, 51.0], ROAD + 1.0)^ where b.kind == "R") {x: p.x, z: p.z, s: 0.9}],
    *[for (b in BLOCKS, t in (if (b.kind == "P") park_trees(b)^ else [])) t],
    *[for (b in BLOCKS, dx in [-14.0, 14.0], dz in [-14.0, 14.0] where b.kind == "X")
        {x: (b.lx0 + b.lx1) / 2.0 + dx, z: (b.lz0 + b.lz1) / 2.0 + dz, s: 1.1}]]

// ---- special lots
fn centre(b) => [(b.lx0 + b.lx1) / 2.0, (b.lz0 + b.lz1) / 2.0]
pub let PLAZA = [for (b in BLOCKS where b.kind == "X") (let c = centre(b), {x: c[0], z: c[1], r: 5.5})][0]
pub let YARD = [for (b in BLOCKS where b.kind == "C") b][0]
pub let STUNT = [for (b in BLOCKS where b.kind == "S") b][0]
// ramps rise along their heading from the low lip to a vertical drop; each launches across a street
pub let RAMPS = (let c = centre(STUNT), [
    {x: STUNT.lx1 - 7.5, z: c[1] + 8.0, heading: u.PI / 2.0, length: 11.0, width: 5.0, height: 1.7},
    {x: c[0] - 8.0, z: STUNT.lz0 + 6.5, heading: u.PI, length: 10.0, width: 5.0, height: 1.5}])
// the yard's half-built frame: column grid and a tower crane
pub let FRAME = (let c = centre(YARD), {x0: c[0] - 2.0, x1: c[0] + 16.0, z0: c[1] - 16.0, z1: c[1] + 4.0, floors: 3, storey: 4.0})
pub let FRAME_COLUMNS = [for (i in 0 to 3, j in 0 to 3) [FRAME.x0 + i * (FRAME.x1 - FRAME.x0) / 3.0, FRAME.z0 + j * (FRAME.z1 - FRAME.z0) / 3.0]]
pub let CRANE = (let c = centre(YARD), {x: c[0] - 12.0, z: c[1] + 12.0, height: 46.0, jib: 34.0, heading: 0.6})

// cars parked along the curb of homely and office streets, clear of the junction zones
pub let PARKED_PAINTS = ["#d8d6cf", "#7d8691", "#20242b", "#2a4c7c", "#8b3a2f", "#3a6650", "#c9a24a", "#6e5a8a"]
pub let PARKED = [for (b in BLOCKS, side in 0 to 3, slot in 0 to 1, let k = rng.derive(KEY, ["parked", b.bi, b.bj, side, slot]),
        let t = (if (slot == 0) 15.0 else 41.0) + rng.between(rng.derive(k, "t"), -3.0, 4.0)
        where (b.kind == "R" or b.kind == "O") and rng.chance(rng.derive(k, "here"), 0.45)^)
    (let curb = ROAD - 1.05, {x: if (side == 0 or side == 1) b.x0 + t else if (side == 2) b.x0 + curb else b.x1 - curb,
        z: if (side == 0) b.z0 + curb else if (side == 1) b.z1 - curb else b.z0 + t,
        heading: if (side == 0) u.PI / 2.0 else if (side == 1) 0.0 - u.PI / 2.0 else if (side == 2) 0.0 else u.PI,
        kind: if (rng.chance(rng.derive(k, "kind"), 0.3)) "hatch" else "sedan",
        paint: PARKED_PAINTS[rng.below(rng.derive(k, "paint"), len(PARKED_PAINTS))]})]

// loose props for the physics to scatter: crate pyramids and a cone slalom in the stunt lot,
// barrels, cones and crates in the yard, and wheelie bins beside some lamps
fn pyramid(x, z) => [for (row in 0 to 2, k in 0 to (2 - row)) {kind: "crate", x: x + (k - (2 - row) / 2.0) * 1.05, y: CURB + 0.5 + row * 1.0, z: z, yaw: 0.0}]
pub let PROPS = (let c = centre(STUNT), (let y = centre(YARD), [
    *pyramid(c[0] + 6.0, c[1] - 12.0), *pyramid(c[0] - 12.0, c[1] - 6.0),
    *[for (k in 0 to 7) {kind: "cone", x: c[0] - 14.0 + k * 3.6, y: CURB, z: c[1] + 15.0 + (if (k % 2 == 0) 0.6 else -0.6), yaw: 0.0}],
    *[for (k in 0 to 3) {kind: "barrel", x: RAMPS[0].x - 3.0 + k * 0.8, y: CURB + 0.45, z: RAMPS[0].z + 4.2, yaw: 0.0}],
    *[for (k in 0 to 5) {kind: "barrel", x: y[0] - 14.0 + (k % 3) * 0.75, y: CURB + 0.45, z: y[1] - 6.0 + (k div 3) * 0.75, yaw: 0.0}],
    *[for (k in 0 to 5) {kind: "cone", x: y[0] - 16.0 + k * 2.4, y: CURB, z: y[1] + 2.0, yaw: 0.0}],
    *[for (k in 0 to 2, h in 0 to 1) {kind: "crate", x: y[0] + 6.0 + k * 1.05, y: CURB + 0.5 + h * 1.0, z: y[1] + 12.0, yaw: 0.0}],
    *[for (n, p in LAMPS where n % 17 == 3) (let side = u.right(p.face), {kind: "bin", x: p.x + side[0] * 1.2 - u.forward(p.face)[0] * 1.6,
        y: CURB + 0.46, z: p.z + side[1] * 1.2 - u.forward(p.face)[1] * 1.6, yaw: p.face})]]))

// ---- ground height: roads at 0, sidewalks and lots on the curb, ramps on top
fn ramp_height(r, x, z) {
    let f = u.forward(r.heading), side = u.right(r.heading)
    let a = (x - r.x) * f[0] + (z - r.z) * f[1], c = (x - r.x) * side[0] + (z - r.z) * side[1];
    if (abs(a) <= r.length / 2.0 and abs(c) <= r.width / 2.0) CURB + r.height * (a + r.length / 2.0) / r.length else null
}
pub fn ground(x, z) {
    let ramps = [for (r in RAMPS, let h = ramp_height(r, x, z) where h != null) h];
    if (len(ramps) > 0) ramps[0] else if (on_road(x, z)) 0.0 else CURB
}

// travel directions in x/z: east, south, west, north (index d); the right of d is d + 1
pub let DIRS = [[1.0, 0.0], [0.0, 1.0], [-1.0, 0.0], [0.0, -1.0]]
pub fn heading_of_dir(d) any^ => u.heading_of(DIRS[d][0], DIRS[d][1])

// ---- collision: axis-aligned boxes [x0, z0, x1, z1, top] the car cannot drive through
fn post(x, z, r, top) => [x - r, z - r, x + r, z + r, top]
pub let SIGNAL_POLE = 6.7
pub fn junction_signalised(i, j) => not ((i == 0 or i == N) and (j == 0 or j == N))
// far-side mast arms: one per approach, on the departing corner to the approaching driver's right
pub let SIGNALS = [for (i in 0 to N, j in 0 to N, d in 0 to 3, let f = DIRS[d], let r = [0.0 - f[1], f[0]]
        where junction_signalised(i, j) and i - f[0] >= 0 and i - f[0] <= N and j - f[1] >= 0 and j - f[1] <= N)
    {i: i, j: j, d: d, group: (d % 2 + (i + j) % 2) % 2,
     x: road(i) + f[0] * (EDGE + 0.5) + r[0] * SIGNAL_POLE, z: road(j) + f[1] * (EDGE + 0.5) + r[1] * SIGNAL_POLE}]

pub let OBSTACLES = [
    *[for (b in BUILDINGS) [b.x0, b.z0, b.x1, b.z1, b.h]],
    *[for (p in LAMPS) post(p.x, p.z, 0.2, 7.5)],
    *[for (t in TREES) post(t.x, t.z, 0.3, 4.0)],
    *[for (s in SIGNALS) post(s.x, s.z, 0.2, 6.0)],
    post(PLAZA.x, PLAZA.z, PLAZA.r, 1.0),
    *[for (c in FRAME_COLUMNS) post(c[0], c[1], 0.3, FRAME.floors * FRAME.storey)],
    post(CRANE.x, CRANE.z, 1.2, CRANE.height),
    *[for (c in PARKED, let lng = abs(sin(c.heading)) > 0.5, let hl = if (c.kind == "hatch") 2.0 else 2.25)
        [c.x - (if (lng) hl else 0.93), c.z - (if (lng) 0.93 else hl), c.x + (if (lng) hl else 0.93), c.z + (if (lng) 0.93 else hl), 1.4]],
    // the perimeter barrier
    [0.0 - LIMIT - 1.0, 0.0 - LIMIT - 1.0, LIMIT + 1.0, 0.0 - LIMIT, 1.1], [0.0 - LIMIT - 1.0, LIMIT, LIMIT + 1.0, LIMIT + 1.0, 1.1],
    [0.0 - LIMIT - 1.0, 0.0 - LIMIT, 0.0 - LIMIT, LIMIT, 1.1], [LIMIT, 0.0 - LIMIT, LIMIT + 1.0, LIMIT, 1.1]]

// a uniform cell index: each cell lists the obstacles overlapping it
pub let CELL = 16.0
pub let CELLS = int(2.0 * (LIMIT + 2.0) / CELL) + 1
fn cell_of(c) => int(max(0, min(CELLS - 1, floor((c + LIMIT + 2.0) / CELL)))) ^ { 0 }
let CELL_PAIRS = [for (k, o in OBSTACLES, cx in cell_of(o[0])^ to cell_of(o[2])^, cz in cell_of(o[1])^ to cell_of(o[3])^)
    {cell: cz * CELLS + cx, k: k}]
let CELL_MAP = map([for (p in CELL_PAIRS group by p.cell into g) (string(cell), g |> ~.k)])
pub fn near_obstacles(x, z, r) any^ {
    let ids = [for (cx in cell_of(x - r) to cell_of(x + r), cz in cell_of(z - r) to cell_of(z + r),
        k in (CELL_MAP[string(cz * CELLS + cx)^] or [])) k];
    [for (k in unique(ids)) OBSTACLES[k]]
}

// ---- names for the HUD
pub fn district(x, z) any^ {
    let b = block_at(x, z)
    if (z > LIMIT) "Harbour Promenade"
    else if (b == null) "City Limits"
    else if (b.kind == "T") "Downtown"
    else if (b.kind == "O") "Midtown"
    else if (b.kind == "R") (if (b.bj <= 2) "Northside" else "Harbourside")
    else if (b.kind == "P") (if (b.bj <= 2) "Linden Park" else "Seawall Park")
    else if (b.kind == "X") "Union Plaza"
    else if (b.kind == "C") "Foundry Yard"
    else "Stunt Lot"
}
let STREETS = ["Cedar", "Mercer", "Lantern", "Union", "Granite", "Harbor", "Quay"]
let AVENUES = ["1st", "2nd", "3rd", "4th", "5th", "6th", "7th"]
pub fn street(x, z) {
    let i = nearest(x), j = nearest(z)
    let ax = abs(x - road(i)), az = abs(z - road(j));
    if (ax < ROAD and az < ROAD) AVENUES[i] ++ " Ave & " ++ STREETS[j] ++ " St"
    else if (ax < az) AVENUES[i] ++ " Avenue" else STREETS[j] ++ " Street"
}
