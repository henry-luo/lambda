// The land as driven and drawn: the natural terrain with every road corridor carved into it (OR2),
// the driving height outside the city (OR1), and the solid rails, parapets and tunnel walls beside
// the roads (OR5). Each near-grid vertex also records how close the nearest road passes, so the
// terrain mesh can give way to the road's verge and the scenery can keep clear of the asphalt.
import u: .mod_util
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import cells: .mod_cells

pub let FLAT_BAND = 8.0           // the land beside a road is level with it this far out
pub let DROP = 9.5                // a terrain triangle with a vertex this close to a road gives way to its verge
pub let PORTAL_DROP = 15.0        // and this close around a tunnel portal
pub let VERGE = 24.0              // the verge reaches past every dropped triangle
pub let ISLAND_Y = 0.2

fn is_span(r) any^ => r.r == 0 and (roads.within("truss", r.s, 5.0) or roads.within("cable", r.s, 5.0))
fn in_bore(r) any^ => r.r == 0 and roads.within("tunnel", r.s)
fn near_portal(r) any^ => r.r == 0 and len([for (st in roads.STRUCTURES where st.kind == "tunnel" and
    (abs(r.s - st.a) < roads.PORTAL or abs(r.s - st.b) < roads.PORTAL)) st]) > 0
// the land at a point of natural height h whose nearest road is r (or null): level with the road in a
// flat band, easing back to the land by REACH; a bridge span leaves the valley under it and a tunnel
// keeps at least ten metres of hill over its bore
pub fn carved(h, r) any^ {
    if (r == null) h
    else if (is_span(r)) h
    else if (in_bore(r)) max(h, r.y + 10.5 - max(0.0, abs(r.lat) - 8.0) * 0.25)
    else (let target = r.y - 0.15, target + (h - target) * land.smooth(FLAT_BAND, roads.REACH, abs(r.lat)))
}
// whether the terrain gives way around this vertex: the verge or a portal's cap draws that ground instead
fn drops(r) any^ => if (r == null or r.r != 0 or is_span(r)) false
    else if (near_portal(r)) abs(r.lat) < PORTAL_DROP
    else if (in_bore(r)) false
    else abs(r.lat) < DROP

// ---- the near grid, carved: one record per vertex, in NATURAL's order
fn vertex(k, h) any^ {
    let x = land.X0 + (k % land.NX) * land.STEP, z = land.Z0 + (k div land.NX) * land.STEP
    let r = if (city.inside_city(x, z)) null else roads.road_at(x, z);
    {h: carved(h, r), drop: drops(r), road: if (r == null) 99.0 else abs(r.lat)}
}
// the corridor index's 32 m cells hold whole 10 m columns of vertices: (i * 10) div 32 is the cell column
let FAR_AWAY = {drop: false, road: 99.0}
let CARVED = [for (k, h in land.NATURAL, let cell = ((k div land.NX) * 10 div 32) * cells.CNX + (k % land.NX) * 10 div 32)
    if (roads.CELL_HAS_ROAD[cell]) vertex(k, h) else {*: FAR_AWAY, h: h}]
pub let HEIGHT = [for (c in CARVED) c.h]
pub let DROPPED = [for (c in CARVED) c.drop]
pub let ROAD_GAP = [for (c in CARVED) c.road]

// the terrain height anywhere: carved inside the near grid, natural beyond
pub fn terrain(x, z) any^ => if (not land.in_near(x, z)) land.natural_at(x, z)
    else (let r = if (city.inside_city(x, z)) null else roads.road_at(x, z), carved(land.grid_at(land.NATURAL, x, z), r))
// the driving height outside the city: the roundabout, then any road corridor, then the land
pub fn ground(x, z) any^ {
    if (roads.in_r1(x, z, roads.R1.apron)) (if (roads.in_r1(x, z, roads.R1.island)) ISLAND_Y else 0.0)
    else if (not land.in_near(x, z)) land.natural_at(x, z)
    else {
        let r = roads.road_at(x, z);
        if (r != null and abs(r.lat) < r.half + 1.2) r.y else carved(land.grid_at(land.NATURAL, x, z), r)
    }
}
// is (x, z) in a tunnel bore? the chase camera keeps under its roof
pub fn bore_at(x, z) any^ => (let r = roads.road_at(x, z), if (r != null and in_bore(r) and abs(r.lat) < 7.0) r else null)

// ---- solids (OR5): oriented boxes {x, z, f, r, hl, hw, top} in the vehicle's box format
fn solid(ax, az, bx, bz, hw, top) any^ {
    let dx = bx - ax, dz = bz - az
    let l = max(sqrt(dx * dx + dz * dz), 0.01)
    let h = u.heading_of(dx, dz);
    {x: (ax + bx) / 2.0, z: (az + bz) / 2.0, f: u.forward(h), r: u.right(h), hl: l / 2.0, hw: hw, top: top}
}
// a run of boxes along one side of the loop between samples a and b, `lat` metres off the centreline
fn side_run(a, b, lat, hw, rise) any^ {
    let pts = roads.LOOP.samples;
    [for (k in a to (b - 1), let p = pts[k], let q = pts[min(b, k + 2)], let pr = u.right(p.heading), let qr = u.right(q.heading) where (k - a) % 2 == 0)
        solid(p.x + pr[0] * lat, p.z + pr[1] * lat, q.x + qr[0] * lat, q.z + qr[1] * lat, hw, max(p.y, q.y) + rise)]
}
// a guardrail stands where the land falls away more than 2.5 m beside a stretch of open road
pub let RAIL_LAT = 5.7
fn falls(p, side) any^ => (let pr = u.right(p.heading), p.y - terrain(p.x + pr[0] * side * 20.0, p.z + pr[1] * side * 20.0) > 3.0)
fn railed(k, side) any^ => (let p = roads.LOOP.samples[k], p.kind == "road" and falls(p, side))
// runs of consecutive railed samples on each side, as [first, last, side]
fn rail_runs(side) any^ {
    let n = len(roads.LOOP.samples)^
    let flags = [for (k in 0 to (n - 1)) railed(k, side)]
    let starts = [for (k in 0 to (n - 1) where flags[k] and (k == 0 or not flags[k - 1])) k];
    [for (a in starts) (let ends = [for (k in a to (n - 1) where flags[k] and (k == n - 1 or not flags[k + 1])) k], [max(0, a - 2), min(n - 1, ends[0] + 2), side])]
}
pub let RAILS = [*rail_runs(1.0)^, *rail_runs(-1.0)^]
pub let SOLIDS = [
    *[for (st in roads.STRUCTURES, side in [-1.0, 1.0] where st.kind != "tunnel") *side_run(st.start_k, st.end_k, side * 6.6, 0.25, 1.2)^],
    *[for (st in roads.STRUCTURES, side in [-1.0, 1.0] where st.kind == "tunnel") *side_run(st.start_k, st.end_k, side * 6.3, 0.3, 7.0)^],
    *[for (rl in RAILS) *side_run(rl[0], rl[1], rl[2] * RAIL_LAT, 0.15, 0.8)^],
    // the monument on the roundabout's island
    (let m = roads.R1, solid(m.x - 2.4, m.z, m.x + 2.4, m.z, 2.4, 14.0))]

let SOLID_CELLS = cells.build([for (b in SOLIDS, let e = b.hl + b.hw) [b.x - e, b.z - e, b.x + e, b.z + e]]) ^ { [] }
pub fn near_solids(x, z, r) any^ => [for (n in cells.near(SOLID_CELLS, x, z, r)^) SOLIDS[n]]

// ---- land use, branch-free like the terrain formula (numbers or whole-grid arrays): farm parcels in
// the valley east of the river and woods on the hills. Weights run 0 to 1; a point caller tests > 0.5
fn frac(v) any^ => v - floor(v)
pub let FARM = {x0: 540.0, x1: 1000.0, z0: -520.0, z1: 175.0, angle: 0.32, w: 72.0, d: 46.0, top: 42.0}
pub let CROPS = ["wheat", "green", "plowed", "canola", "meadow", "hay", "green", "wheat"]
let FARM_COS = cos(FARM.angle)
let FARM_SIN = sin(FARM.angle)
pub fn farm_weight(x, z, h) any^ => land.step(x - FARM.x0) * land.step(FARM.x1 - x) * land.step(z - FARM.z0) * land.step(FARM.z1 - z) *
    land.step(FARM.top - h) * land.step(h - 1.0)
// the parcel under (x, z): its grid cell (the farm grid turns by FARM.angle) and position within it
pub fn parcel(x, z) any^ => (let px = (x * FARM_COS + z * FARM_SIN) / FARM.w, let pz = (z * FARM_COS - x * FARM_SIN) / FARM.d,
    {i: floor(px), j: floor(pz), u: frac(px), v: frac(pz)})
// a parcel's crop, as an index into CROPS
pub fn crop_index(p) any^ => floor(frac(sin(p.i * 12.9898 + p.j * 78.233) * 43758.5453) * len(CROPS))
// woods grow where this field is high, more readily up the hills, never on the farm, by the sea or on the flat
pub fn woods(x, z) any^ => 0.5 + 0.5 * sin(x / 97.0 + 1.7 * sin(z / 131.0)) * cos(z / 83.0 - 0.9 * sin(x / 151.0))
pub fn wood_weight(x, z, h, cz) any^ => land.step(woods(x, z) - 0.66 + 0.24 * land.smooth(25.0, 90.0, h)) * land.step(h - 2.0) *
    (1.0 - farm_weight(x, z, h)) * land.step(cz - 55.0 - z) * land.step(land.flat_distance(x, z) - 60.0)
