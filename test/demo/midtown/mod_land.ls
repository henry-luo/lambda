// The land around Lumen Bay: rolling farmland east of the city, a forested ridge, mountains to the
// north and hills to the west, a river that runs south to the sea, and a coast of cliffs with one
// beach bay and a lighthouse headland. Heights come from one fBm noise sampled on a 10 m near grid
// over the drivable region and a 60 m far grid out to the horizon; the far grid's points are near
// grid points, so both read the same height where they meet. The sea, the harbour and the river
// are one water surface at SEA (Lambda_Demo_Midtown_Open_Road OR2, OR4).
import u: .mod_util
import city: .mod_city
import rng: lambda.procart.rng
import noise: lambda.procart.noise

pub let SEA = -1.2
// the near grid: 10 m cells over the drivable region
pub let STEP = 10.0
pub let X0 = -420.0
pub let Z0 = -900.0
pub let NX = 235
pub let NZ = 151
pub let X1 = X0 + (NX - 1) * STEP
pub let Z1 = Z0 + (NZ - 1) * STEP
// the far grid: every sixth near line, out to the horizon; it sinks under the near grid
pub let FAR_STEP = 60.0
pub let FX0 = X0 - 36.0 * FAR_STEP
pub let FZ0 = Z0 - 30.0 * FAR_STEP
pub let FNX = 115
pub let FNZ = 87
pub let FAR_SINK = 2.5
let KEY = rng.key([city.SEED, "land"])
// one fBm, sampled on a 20 m lattice (every other near line) and bilinearly filled in between: its finest
// octave is 70 m, so the fill loses nothing and costs a quarter of the noise. Point samples read the same
// lattice coordinates. The far grid reads only the three broadest octaves (finer ones alias at 60 m);
// the rim skirt hides the metre or two where the grids meet
let LATTICE = 2.0 * STEP
let HILLS = noise.make(rng.derive(KEY, "hills"), {kind: 'simplex', octaves: 4, frequency: LATTICE / 560.0}) ^ { null }
let BROAD = noise.make(rng.derive(KEY, "hills"), {kind: 'simplex', octaves: 3, frequency: LATTICE / 560.0}) ^ { null }
let LNX = (NX + 1) div 2
let LNZ = (NZ + 1) div 2

// Branch-free helpers: built from arithmetic and abs, they take numbers or whole arrays alike, so one
// terrain formula serves a single point and the full grid (where array maths is ~200x cheaper per value)
pub fn clamp01(t) any^ => 0.5 * (abs(t) - abs(t - 1.0) + 1.0)
pub fn vmax(a, b) any^ => 0.5 * (a + b + abs(a - b))
pub fn vmin(a, b) any^ => 0.5 * (a + b - abs(a - b))
pub fn mix(a, b, k) any^ => a + (b - a) * k
pub fn smooth(a, b, x) any^ => (let t = clamp01((x - a) / (b - a)), t * t * (3.0 - 2.0 * t))
// 1 where v > 0, 0 where v < 0 (a millimetre-wide ramp between)
pub fn step(v) any^ => clamp01(v * 1000.0)

// ---- the coast: z of the shoreline as x runs east (the sea lies south, z beyond it)
pub let COAST = [[-4800.0, 250.0], [-2700.0, 236.0], [-1500.0, 226.0], [-600.0, 219.0], [-280.0, 216.6], [205.0, 216.6],
    [262.0, 244.0], [325.0, 282.0], [410.0, 296.0], [565.0, 292.0], [655.0, 318.0], [760.0, 332.0], [870.0, 326.0],
    [960.0, 316.0], [1050.0, 330.0], [1180.0, 340.0], [1300.0, 318.0], [1415.0, 290.0], [1515.0, 302.0], [1590.0, 352.0],
    [1650.0, 376.0], [1715.0, 346.0], [1810.0, 296.0], [2000.0, 302.0], [2600.0, 330.0], [6400.0, 360.0]]
// Catmull-Rom through x-ordered points; the harbour wall is exactly straight
fn hermite(p0, p1, p2, p3, t) any^ => (let t2 = t * t, let t3 = t2 * t,
    0.5 * (2.0 * p1 + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 + (3.0 * p1 - p0 - 3.0 * p2 + p3) * t3))
pub fn curve_at(points, x) any^ {
    let n = len(points)
    let k = max(0, min(n - 2, len([for (p in points where p[0] <= x) p]) - 1))
    let a = points[max(0, k - 1)], b = points[k], c = points[k + 1], d = points[min(n - 1, k + 2)]
    let t = u.clamp((x - b[0]) / (c[0] - b[0]), 0.0, 1.0);
    hermite(a[1], b[1], c[1], d[1], t)
}
pub fn coast_z(x) any^ => if (x >= -280.0 and x <= 205.0) city.SHORE + 0.6 else curve_at(COAST, x)
// sandy stretches: the beach bay and the river mouth's banks
fn bump(x, centre, width) any^ => 1.0 - smooth(width * 0.55, width, abs(x - centre))
pub fn beach(x) any^ => max(bump(x, 960.0, 150.0), bump(x, 470.0, 130.0))

// ---- the river, north to south, widening into an estuary
pub let RIVER = [[505.0, -2800.0], [470.0, -1500.0], [455.0, -900.0], [492.0, -600.0], [468.0, -350.0], [474.0, -150.0],
    [452.0, 0.0], [468.0, 130.0], [488.0, 220.0], [492.0, 300.0], [480.0, 700.0]]
pub fn river_half(z) any^ => 14.0 + 54.0 * smooth(80.0, 280.0, z)
fn river_bank(z) any^ => 26.0 + 64.0 * smooth(-50.0, 220.0, z)
fn segment_d2(x, z, a, b) any^ {
    let dx = b[0] - a[0], dz = b[1] - a[1]
    let t = clamp01(((x - a[0]) * dx + (z - a[1]) * dz) / (dx * dx + dz * dz))
    let qx = a[0] + dx * t - x, qz = a[1] + dz * t - z;
    qx * qx + qz * qz
}
pub fn river_distance(x, z) any^ => math.sqrt(reduce([for (k in 0 to (len(RIVER) - 2)) segment_d2(x, z, RIVER[k], RIVER[k + 1])], (m, d) any^ => vmin(m, d)))

// ---- the ridge the loop road tunnels through, across the axis between its portals
pub let TUNNEL_WEST = [1110.0, -350.0]
pub let TUNNEL_EAST = [1330.0, -280.0]
let AXIS = (let dx = TUNNEL_EAST[0] - TUNNEL_WEST[0], let dz = TUNNEL_EAST[1] - TUNNEL_WEST[1], let l = sqrt(dx * dx + dz * dz), [dx / l, dz / l])
let MID = [(TUNNEL_WEST[0] + TUNNEL_EAST[0]) / 2.0, (TUNNEL_WEST[1] + TUNNEL_EAST[1]) / 2.0]
fn ridge(x, z) any^ {
    let px = x - MID[0], pz = z - MID[1]
    let across = (px * AXIS[0] + pz * AXIS[1]) / 100.0, along = (0.0 - px * AXIS[1] + pz * AXIS[0]) / 560.0;
    135.0 * exp(0.0 - across * across - along * along)
}

// the city, its gateway roundabout and the harbour promontory sit on flat ground at the grass level
pub let FLAT = {x0: -275.0, x1: 345.0, z0: -275.0, z1: 330.0, blend: 140.0, y: -0.03}
pub fn flat_distance(x, z) any^ => (let dx = vmax(0.0, vmax(FLAT.x0 - x, x - FLAT.x1)), let dz = vmax(0.0, vmax(FLAT.z0 - z, z - FLAT.z1)),
    math.sqrt(dx * dx + dz * dz))
fn flat_weight(x, z) any^ => smooth(0.0, FLAT.blend, flat_distance(x, z))

// the natural height at (x, z) from the noise value n there, the coastline cz above it and the beach weight
// the natural height at (x, z) from the noise value n there, the coastline cz above it and the beach
// weight; every argument may be a number or a whole-grid array
pub fn natural(x, z, n, cz, sand) any^ {
    let w = flat_weight(x, z)
    let c = z - cz
    let land = 13.0 + 19.0 * n + smooth(-470.0, -1250.0, z) * (210.0 + 170.0 * n) + smooth(-320.0, -1100.0, x) * (60.0 + 50.0 * n) +
        smooth(1780.0, 2700.0, x) * (70.0 + 60.0 * n) + ridge(x, z) + 9.0 * smooth(-320.0, -130.0, c) * (1.0 - sand)
    let h = FLAT.y + (land - FLAT.y) * w
    // cliffs fall to a shelving sea floor; a beach shelves gently through the waterline. The harbour wall is sheer.
    let floor = vmax(-30.0, -5.5 - 0.07 * vmax(0.0, c))
    let cliff = mix(h, floor, smooth(-2.0 - 22.0 * w, 5.0, c))
    let shelf = vmax(floor, 1.4 - 0.085 * (c + 12.0))
    let shore = mix(cliff, mix(h, shelf, smooth(-120.0, -45.0, c)), sand * w)
    let half = river_half(z);
    mix(-4.5, shore, smooth(half, half + river_bank(z), river_distance(x, z)))
}

// ---- the near grid, row by row from Z0; vertex (i, j) sits at (X0 + i STEP, Z0 + j STEP)
pub let COLUMNS = [for (i in 0 to (NX - 1), let x = X0 + i * STEP) {x: x, cz: coast_z(x), sand: beach(x)}]
let COARSE = if (HILLS == null) [] else [for (row in noise.field(HILLS, LNX, LNZ) ^ { [] }, n in row) n]
fn fill(k) => (let i = k % NX, let j = k div NX, let a = (j div 2) * LNX + i div 2, let di = i % 2, let dj = (j % 2) * LNX,
    (COARSE[a] + COARSE[a + di] + COARSE[a + dj] + COARSE[a + dj + di]) / 4.0)
// the whole grid at once, as arrays in vertex order
pub let GX = [for (j in 0 to (NZ - 1), c in COLUMNS) c.x]
pub let GZ = [for (j in 0 to (NZ - 1), c in COLUMNS) Z0 + j * STEP]
pub let GCZ = [for (j in 0 to (NZ - 1), c in COLUMNS) c.cz]
pub let GSAND = [for (j in 0 to (NZ - 1), c in COLUMNS) c.sand]
pub let NATURAL = natural(GX, GZ, [for (k in 0 to (NX * NZ - 1)) fill(k)], GCZ, GSAND) ^ { [] }

// a value of a near-grid array at (x, z), linear over the two triangles each cell is cut into
// (corners a=(i,j), b=(i+1,j), c=(i,j+1), d=(i+1,j+1); the cut runs from b to c)
pub fn in_near(x, z) any^ => x >= X0 and x < X1 and z >= Z0 and z < Z1
pub fn grid_at(values, x, z) any^ {
    let gx = (x - X0) / STEP, gz = (z - Z0) / STEP
    let i = int(floor(gx)) ^ { 0 }, j = int(floor(gz)) ^ { 0 }
    let fx = gx - i, fz = gz - j
    let a = j * NX + i
    if (fx + fz <= 1.0) values[a] + (values[a + 1] - values[a]) * fx + (values[a + NX] - values[a]) * fz
    else values[a + NX + 1] + (values[a + NX] - values[a + NX + 1]) * (1.0 - fx) + (values[a + 1] - values[a + NX + 1]) * (1.0 - fz)
}
pub fn noise_at(x, z) any^ => if (HILLS == null) 0.0 else noise.sample(HILLS, (x - X0) / LATTICE, (z - Z0) / LATTICE)
// the natural height anywhere: the near grid where it reaches, the formula beyond
pub fn natural_at(x, z) any^ => if (in_near(x, z)) grid_at(NATURAL, x, z) else natural(x, z, noise_at(x, z), coast_z(x), beach(x))

// ---- the far grid; inside the near grid's reach it sinks out of sight
fn inside_near(x, z) any^ => x > X0 and x < X1 and z > Z0 and z < Z1
pub let FAR_COLUMNS = [for (i in 0 to (FNX - 1), let x = FX0 + i * FAR_STEP) {x: x, cz: coast_z(x), sand: beach(x)}]
fn broad_at(x, z) any^ => if (BROAD == null) 0.0 else noise.sample(BROAD, (x - X0) / LATTICE, (z - Z0) / LATTICE)
pub let FGX = [for (j in 0 to (FNZ - 1), c in FAR_COLUMNS) c.x]
pub let FGZ = [for (j in 0 to (FNZ - 1), c in FAR_COLUMNS) FZ0 + j * FAR_STEP]
pub let FGCZ = [for (j in 0 to (FNZ - 1), c in FAR_COLUMNS) c.cz]
pub let FGSAND = [for (j in 0 to (FNZ - 1), c in FAR_COLUMNS) c.sand]
pub let FAR_INSIDE = [for (k, x in FGX) inside_near(x, FGZ[k])]
let FAR_RAW = natural(FGX, FGZ, [for (k, x in FGX) broad_at(x, FGZ[k])], FGCZ, FGSAND) ^ { [] }
pub let FAR = [for (k, h in FAR_RAW) h - (if (FAR_INSIDE[k]) FAR_SINK else 0.0)]
