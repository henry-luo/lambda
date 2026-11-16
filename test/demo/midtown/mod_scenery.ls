// Scenery beyond the city: woods and lone trees, hedgerows along the farm parcels, farmsteads, a row
// of wind turbines on the hills, the lighthouse on its headland, rocks under the cliffs and boats out
// at sea. Placements are pure functions of the land and roads, so the scene, the solids and the
// goldens agree. Tree candidates are evaluated over the whole terrain grid at once (array maths).
import u: .mod_util
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import region: .mod_region
import cells: .mod_cells

fn frac(v) any^ => v - floor(v)
// a repeatable hash in [0, 1) of two numbers (or arrays of them)
pub fn hash(a, b) any^ => frac(sin(a * 12.9898 + b * 78.233) * 43758.5453)
fn covered(x, z) => abs(x) <= city.LIMIT + 4.0 and z >= 0.0 - city.LIMIT - 4.0 and z <= city.SHORE + 4.0
fn ground(x, z) any^ => land.grid_at(region.HEIGHT, x, z)

// ---- trees: one candidate per near-grid vertex, jittered within its cell. A tree stands on about half
// the wooded vertices and on one in sixty elsewhere, never near a road, in the water, on the farm
// parcels, on steep rock or on the flat around town
let N = land.NX * land.NZ
let IDS = [for (k in 0 to (N - 1)) float(k)]
let CX = land.GX + (hash(IDS, 1.7) - 0.5) * 8.0
let CZ = land.GZ + (hash(IDS, 9.1) - 0.5) * 8.0
let WOOD = region.wood_weight(CX, CZ, region.HEIGHT, land.GCZ)
let OPEN = (1.0 - region.farm_weight(CX, CZ, region.HEIGHT)) * land.step(land.flat_distance(CX, CZ) - 50.0)
let PICK = hash(IDS, 3.3)
let H = region.HEIGHT
// the steepness at each vertex from its row and column neighbours (rise per metre)
fn steep(k) => (let i = k % land.NX, let j = k div land.NX,
    if (i == 0 or j == 0 or i == land.NX - 1 or j == land.NZ - 1) 9.0
    else (abs(H[k + 1] - H[k - 1]) + abs(H[k + land.NX] - H[k - land.NX])) / (2.0 * land.STEP))
pub let TREE_KINDS = ["pine", "broad"]
pub let TREES = [for (k, w in WOOD, let x = CX[k], let z = CZ[k], let h = H[k], let p = PICK[k]
        where ((w > 0.5 and p < 0.5) or (w <= 0.5 and OPEN[k] > 0.5 and p < 0.016)) and region.ROAD_GAP[k] > 13.0 and
            h > land.SEA + 1.6 and not covered(x, z)^ and steep(k)^ < 0.8)
    {x: x, z: z, y: ground(x, z) - 0.2, s: 0.75 + 0.6 * hash(p, 4.4), yaw: hash(p, 7.7) * u.TAU,
     kind: if (h > 38.0 or hash(p, 5.5) < 0.3) "pine" else "broad"}]

// ---- hedgerows: bushes every 4.5 m along the parcel boundaries inside the farm, a third of the edges bare
let FARM = region.FARM
let CA = cos(FARM.angle)
let SA = sin(FARM.angle)
fn rotated(x, z) => [x * CA + z * SA, z * CA - x * SA]
let FARM_BOX = (let cs = [rotated(FARM.x0, FARM.z0), rotated(FARM.x1, FARM.z0), rotated(FARM.x0, FARM.z1), rotated(FARM.x1, FARM.z1)],
    {p0: min([for (c in cs) c[0]]), p1: max([for (c in cs) c[0]]), q0: min([for (c in cs) c[1]]), q1: max([for (c in cs) c[1]])})
// points on boundary lines: `fixed` lines of constant rotated coordinate, `along` the other
fn hedge_line(fixed, along_from, along_to, cross) any^ => [for (k in 0 to (int((along_to - along_from) / 4.5) ^ { 0 }), let t = along_from + k * 4.5)
    if (cross) [fixed * CA - t * SA, fixed * SA + t * CA] else [t * CA - fixed * SA, t * SA + fixed * CA]]
let HEDGE_POINTS = [*[for (i in (int(floor(FARM_BOX.p0 / FARM.w)) ^ { 0 }) to (int(ceil(FARM_BOX.p1 / FARM.w)) ^ { 0 }), p in hedge_line(i * FARM.w, FARM_BOX.q0, FARM_BOX.q1, true)^,
        let edge = floor((p[0] * (0.0 - SA) + p[1] * CA) / FARM.d) where hash(float(i), edge)^ > 0.33) p],
    *[for (j in (int(floor(FARM_BOX.q0 / FARM.d)) ^ { 0 }) to (int(ceil(FARM_BOX.q1 / FARM.d)) ^ { 0 }), p in hedge_line(j * FARM.d, FARM_BOX.p0, FARM_BOX.p1, false)^,
        let edge = floor((p[0] * CA + p[1] * SA) / FARM.w) where hash(edge, float(j) + 0.5)^ > 0.33) p]]
fn road_clear(x, z, r) any^ => (let rd = roads.road_at(x, z)^, rd == null or abs(rd.lat) > r)
pub let BUSHES = [for (p in HEDGE_POINTS, let y = ground(p[0], p[1]) where land.in_near(p[0], p[1])^ and region.farm_weight(p[0], p[1], y)^ > 0.5 and road_clear(p[0], p[1], 9.0)^)
    {x: p[0], z: p[1], y: y - 0.15, s: 0.8 + 0.5 * hash(p[0], p[1]), yaw: hash(p[1], p[0]) * u.TAU}]

// ---- farmsteads beside the loop road: a farmhouse facing the road, a barn and a silo behind it
fn beside(s, lat) any^ {
    let pts = roads.LOOP.samples^
    let p = pts[len([for (q in pts where q.s < s) q])]
    let r = u.right(p.heading);
    {x: p.x + r[0] * lat, z: p.z + r[1] * lat, heading: u.wrap(p.heading + (if (lat > 0.0) u.PI / 2.0 else 0.0 - u.PI / 2.0))}
}
pub let FARMS = [for (spot in [[318.0, 46.0], [455.0, -52.0], [560.0, 50.0], [690.0, -46.0], [2230.0, 46.0]], let b = beside(spot[0], spot[1])^)
    {*: b, y: ground(b.x, b.z)}]

// ---- wind turbines along the hills north of the farm valley, each with its rotor's phase
pub let TURBINES = [for (k in 0 to 6, let x = 560.0 + k * 78.0, let z = -610.0 + 35.0 * sin(k * 1.3))
    {x: x, z: z, y: ground(x, z), heading: 0.35, phase: k * 0.37}]
pub let TURBINE_HUB = 62.0
// ---- the lighthouse on its headland, rocks along the cliff foot, boats out at sea
pub let LIGHTHOUSE = (let x = 1652.0, let z = land.coast_z(1652.0) - 32.0, {x: x, z: z, y: ground(x, z)})
pub let ROCKS = [for (k in 0 to 90, let x = 570.0 + k * 15.0, let sand = land.beach(x)^ where sand < 0.35)
    (let z = land.coast_z(x) + 1.0 + 6.0 * hash(float(k), 2.0), {x: x, z: z, y: land.SEA - 0.4, s: 1.6 + 3.2 * hash(float(k), 3.0), yaw: hash(float(k), 4.0) * u.TAU})]
pub let BOATS = [for (k, b in [[420.0, 520.0], [760.0, 640.0], [1080.0, 470.0], [1380.0, 600.0], [1720.0, 520.0], [140.0, 330.0]])
    {id: "boat-" ++ string(k), x: b[0], z: b[1], heading: hash(float(k), 9.0) * u.TAU, phase: k * 0.71}]

// ---- solids for the car: tree trunks within reach of a road, the farm buildings, towers and the lighthouse
fn post(x, z, r, top) any^ => {x: x, z: z, f: [0.0, 1.0], r: [-1.0, 0.0], hl: r, hw: r, top: top}
pub let SOLIDS = [*[for (t in TREES) post(t.x, t.z, 0.35 * t.s, t.y + 6.0)],
    *[for (f in FARMS, let fw = u.forward(f.heading), let rt = u.right(f.heading)) *[
        {x: f.x, z: f.z, f: fw, r: rt, hl: 5.0, hw: 6.5, top: f.y + 9.0},
        {x: f.x - fw[0] * 24.0, z: f.z - fw[1] * 24.0, f: fw, r: rt, hl: 9.0, hw: 7.0, top: f.y + 11.0}]],
    *[for (t in TURBINES) post(t.x, t.z, 2.2, t.y + 70.0)],
    post(LIGHTHOUSE.x, LIGHTHOUSE.z, 3.6, LIGHTHOUSE.y + 30.0)]
let SOLID_CELLS = cells.build([for (b in SOLIDS, let e = b.hl + b.hw) [b.x - e, b.z - e, b.x + e, b.z + e]]) ^ { [] }
pub fn near_solids(x, z, r) any^ => [for (n in cells.near(SOLID_CELLS, x, z, r)^) SOLIDS[n]]
