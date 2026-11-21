// The roads beyond the city: the gateway east along Union Street, the gateway roundabout R1, and the
// loop road that leaves R1 to the north-east, crosses the river on a truss bridge, climbs through
// the farmland, tunnels through the ridge, follows the coast over cliff tops and down past the beach,
// and comes back over the estuary on a cable-stayed bridge. A road is a run of samples about four
// metres apart; its elevation follows the land (OR3) with explicit bridge and tunnel spans.
import u: .mod_util
import city: .mod_city
import land: .mod_land
import cells: .mod_cells

pub let HALF = 4.8                    // asphalt, centreline to edge
pub let LANE = 1.85                   // lane centre from the centreline
pub let REACH = 30.0                  // how far a road reshapes the land around it
pub let SAMPLE = 4.0
pub let GRADE = 0.07
// the roundabout: lane-centre radius, half width of the ring, the raised island, the asphalt apron
pub let R1 = {x: 310.0, z: 0.0, ring: 16.0, half: 5.0, island: 11.0, apron: 30.0}
// bridge decks clear the water by at least this much
pub let DECK = {truss: 9.5, cable: 13.0}
pub let PORTAL = 16.0                 // the land is cut away this far either side of a tunnel portal

fn arm(angle) => [R1.x + cos(angle) * R1.apron, R1.z + sin(angle) * R1.apron]
pub let ARM_LOOP_OUT = -0.873        // radians in x/z: north-east
pub let ARM_LOOP_IN = u.PI / 2.0     // south
pub let ARM_CITY = u.PI              // west
// control points: x, z, and the kind of the span that starts there
let LOOP_POINTS = (let a = arm(ARM_LOOP_OUT), let b = arm(ARM_LOOP_IN), [
    [a[0], a[1], "road"], [352.0, -50.0, "road"], [385.0, -100.0, "road"], [425.0, -136.0, "truss"], [520.0, -158.0, "road"],
    [590.0, -178.0, "road"], [690.0, -205.0, "road"], [800.0, -246.0, "road"], [915.0, -296.0, "road"], [1020.0, -332.0, "road"],
    [land.TUNNEL_WEST[0], land.TUNNEL_WEST[1], "tunnel"], [land.TUNNEL_EAST[0], land.TUNNEL_EAST[1], "road"],
    [1425.0, -222.0, "road"], [1492.0, -128.0, "road"], [1512.0, -10.0, "road"], [1488.0, 100.0, "road"], [1430.0, 214.0, "road"],
    [1330.0, 280.0, "road"], [1205.0, 304.0, "road"], [1085.0, 303.0, "road"], [965.0, 285.0, "road"], [855.0, 296.0, "road"],
    [745.0, 300.0, "road"], [650.0, 284.0, "road"], [585.0, 262.0, "cable"], [395.0, 252.0, "road"], [345.0, 215.0, "road"],
    [318.0, 150.0, "road"], [310.0, 85.0, "road"], [b[0], b[1], "end"]])
let GATE_POINTS = [[city.HALF + city.ROAD, city.road(3), "road"], [R1.x - R1.apron, city.road(3), "end"]]

fn unit2(dx, dz) any^ => (let l = max(sqrt(dx * dx + dz * dz), 0.000001), [dx / l, dz / l])
fn straight(kind) => kind == "truss" or kind == "cable" or kind == "tunnel"
// the direction at each control point: along the chord of a straight span, radial at the roundabout,
// otherwise the Catmull-Rom direction
fn directions(pts, start_dir, end_dir) any^ => [for (k, p in pts, let n = len(pts))
    if (k == 0) start_dir else if (k == n - 1) end_dir
    else if (straight(p[2])) unit2(pts[k + 1][0] - p[0], pts[k + 1][1] - p[1])
    else if (straight(pts[k - 1][2])) unit2(p[0] - pts[k - 1][0], p[1] - pts[k - 1][1])
    else unit2(pts[k + 1][0] - pts[k - 1][0], pts[k + 1][1] - pts[k - 1][1])]
// one Hermite segment, sampled at least every SAMPLE metres; the last point belongs to the next segment
fn segment(a, b, da, db, kind) any^ {
    let l = sqrt(u.sq(b[0] - a[0]) + u.sq(b[1] - a[1]))
    let n = max(2, int(ceil(l / SAMPLE)) ^ { 2 });
    [for (k in 0 to (n - 1), let t = float(k) / n, let t2 = t * t, let t3 = t2 * t,
            let h00 = 2.0 * t3 - 3.0 * t2 + 1.0, let h10 = t3 - 2.0 * t2 + t, let h01 = 3.0 * t2 - 2.0 * t3, let h11 = t3 - t2)
        {x: h00 * a[0] + h10 * l * da[0] + h01 * b[0] + h11 * l * db[0], z: h00 * a[1] + h10 * l * da[1] + h01 * b[1] + h11 * l * db[1], kind: kind}]
}
// samples with arc length s and heading; headings come from the neighbouring samples
fn sampled(pts, start_dir, end_dir) any^ {
    let dirs = directions(pts, start_dir, end_dir)
    let n = len(pts)
    let raw = [*[for (k in 0 to (n - 2)) *segment(pts[k], pts[k + 1], dirs[k], dirs[k + 1], pts[k][2])],
        {x: pts[n - 1][0], z: pts[n - 1][1], kind: pts[n - 2][2]}]
    let m = len(raw)
    let s = math.cumsum([0.0, *[for (k in 1 to (m - 1)) sqrt(u.sq(raw[k].x - raw[k - 1].x) + u.sq(raw[k].z - raw[k - 1].z))]]);
    [for (k, p in raw, let a = raw[max(0, k - 1)], let b = raw[min(m - 1, k + 1)])
        {*: p, s: s[k], heading: u.heading_of(b.x - a.x, b.z - a.z)}]
}

// ---- elevation (OR3): the land under the road, smoothed, cut down to the grade limit, raised over
// the water on bridge decks, straight through the tunnel, and level where it meets the roundabout
fn moving_average(values, w) any^ {
    let n = len(values)
    let c = math.cumsum([0.0, *values]);
    [for (k in 0 to (n - 1), let lo = max(0, k - w), let hi = min(n - 1, k + w)) (c[hi + 1] - c[lo]) / (hi - lo + 1)]
}
// cut every rise and fall steeper than GRADE (peaks come down rather than valleys filling)
fn limited(ys, ss) any^ {
    let up = reduce([[ys[0]], *[for (k in 1 to (len(ys) - 1)) k]], (acc, k) any^ =>
        [*acc, min(ys[k], acc[k - 1] + GRADE * (ss[k] - ss[k - 1]))])
    let n = len(ys);
    reverse(reduce([[up[n - 1]], *[for (k in 1 to (n - 1)) n - 1 - k]], (acc, k) any^ =>
        [*acc, min(up[k], acc[len(acc) - 1] + GRADE * (ss[k + 1] - ss[k]))]))
}
// spans of one kind: [first sample, last sample] index pairs
fn spans(pts, kind) any^ => (let starts = [for (k, p in pts where p.kind == kind and (k == 0 or pts[k - 1].kind != kind)) k],
    [for (a in starts) (let ends = [for (k in a to (len(pts) - 1) where k == len(pts) - 1 or pts[k + 1].kind != kind) k], [a, ends[0] + 1])])
fn profile(pts, flat_ends) any^ {
    let ss = [for (p in pts) p.s]
    let length = ss[len(ss) - 1]
    // the gateway and the roundabout arms meet the city level; the grade limit then eases away from it
    let level = [for (k, y in moving_average([for (p in pts) land.natural_at(p.x, p.z)], 12)^, let s = ss[k])
        if (flat_ends and (s < 20.0 or s > length - 20.0)) 0.0 else y]
    let base = limited(level, ss)
    // a bridge deck spans level between its abutments, at least DECK clear of the water, and its
    // approaches climb to it at the grade limit
    let bridges = [for (kind in ["truss", "cable"], sp in spans(pts, kind)^)
        {a: ss[sp[0]], b: ss[sp[1]], deck: max([land.SEA + DECK[kind], base[sp[0]] + 0.3, base[sp[1]] + 0.3])}]
    let decked = [for (k, y in base, let s = ss[k])
        max([y, *[for (b in bridges) b.deck - GRADE * 0.86 * max(0.0, max(b.a - s, s - b.b))]])]
    let tunnels = [for (sp in spans(pts, "tunnel")^) {a: ss[sp[0]], b: ss[sp[1]], ya: decked[sp[0]], yb: decked[sp[1]]}]
    let bored = [for (k, y in decked, let s = ss[k], let t = [for (tn in tunnels where s >= tn.a and s <= tn.b) tn])
        if (len(t) == 0) y else u.lerp(t[0].ya, t[0].yb, (s - t[0].a) / (t[0].b - t[0].a))];
    bored
}
fn road(id, pts, start_dir, end_dir, half, lane, flat_ends) any^ {
    let samples = sampled(pts, start_dir, end_dir)
    let ys = if (flat_ends) profile(samples, true) else [for (p in samples) 0.0];
    {id: id, half: half, lane: lane, samples: [for (k, p in samples) {*: p, y: ys[k], k: k}],
     length: samples[len(samples) - 1].s}
}
let OUT_DIR = [cos(ARM_LOOP_OUT), sin(ARM_LOOP_OUT)]
let IN_DIR = [0.0 - cos(ARM_LOOP_IN), 0.0 - sin(ARM_LOOP_IN)]
pub let LOOP = road("loop", LOOP_POINTS, OUT_DIR, IN_DIR, HALF, LANE, true)
pub let GATEWAY = road("gateway", GATE_POINTS, [1.0, 0.0], [1.0, 0.0], city.ROAD, city.LANE, false)
pub let ROADS = [LOOP, GATEWAY]
// bridge and tunnel spans of the loop, by arc length
pub let STRUCTURES = [for (kind in ["truss", "cable", "tunnel"], sp in spans(LOOP.samples, kind)^)
    (let a = LOOP.samples[sp[0]], let b = LOOP.samples[sp[1]], {kind: kind, a: a.s, b: b.s, start_k: sp[0], end_k: sp[1], y: a.y, y_end: b.y})]

// ---- the corridor index: each cell lists the road segments within REACH of it
// a segment is (road index, sample index) from sample k to k + 1
let SEGMENTS = [for (r, rd in ROADS, k in 0 to (len(rd.samples)^ - 2)) {r: r, k: k}]
pub let CELL_SEGMENTS = cells.build([for (sg in SEGMENTS, let a = ROADS[sg.r].samples[sg.k], let b = ROADS[sg.r].samples[sg.k + 1])
    [min(a.x, b.x) - REACH, min(a.z, b.z) - REACH, max(a.x, b.x) + REACH, max(a.z, b.z) + REACH]]) ^ { [] }
// whether a cell lists any segment, for a quick test per near-grid vertex
pub let CELL_HAS_ROAD = [for (ids in CELL_SEGMENTS) len(ids) > 0]

// the nearest point of road within REACH of (x, z): its road, sample, arc length s, deck height y,
// signed lateral offset (positive to the right of travel), heading and span kind; null when none
fn project(x, z, sg) any^ {
    let rd = ROADS[sg.r]
    let a = rd.samples[sg.k], b = rd.samples[sg.k + 1]
    let dx = b.x - a.x, dz = b.z - a.z
    let t = u.clamp(((x - a.x) * dx + (z - a.z) * dz) / max(dx * dx + dz * dz, 0.000001), 0.0, 1.0)
    let px = x - (a.x + dx * t), pz = z - (a.z + dz * t)
    let l = max(sqrt(dx * dx + dz * dz), 0.000001);
    {r: sg.r, k: sg.k, t: t, d2: px * px + pz * pz, lat: (px * (0.0 - dz) + pz * dx) / l, s: a.s + (b.s - a.s) * t,
     y: a.y + (b.y - a.y) * t, heading: a.heading, kind: a.kind, half: rd.half}
}
// each segment's start and direction as flat arrays, so the nearest-segment search is arithmetic only
let SEG_AX = [for (sg in SEGMENTS) ROADS[sg.r].samples[sg.k].x]
let SEG_AZ = [for (sg in SEGMENTS) ROADS[sg.r].samples[sg.k].z]
let SEG_DX = [for (sg in SEGMENTS) ROADS[sg.r].samples[sg.k + 1].x - ROADS[sg.r].samples[sg.k].x]
let SEG_DZ = [for (sg in SEGMENTS) ROADS[sg.r].samples[sg.k + 1].z - ROADS[sg.r].samples[sg.k].z]
let SEG_L2 = [for (n, dx in SEG_DX) max(dx * dx + SEG_DZ[n] * SEG_DZ[n], 0.000001)]
fn seg_d2(x, z, n) any^ {
    let ex = x - SEG_AX[n], ez = z - SEG_AZ[n], dx = SEG_DX[n], dz = SEG_DZ[n]
    let t = u.clamp((ex * dx + ez * dz) / SEG_L2[n], 0.0, 1.0)
    let px = ex - dx * t, pz = ez - dz * t;
    px * px + pz * pz
}
pub fn road_at(x, z) any^ {
    if (not land.in_near(x, z)) null
    else {
        let ids = cells.at(CELL_SEGMENTS, x, z);
        if (len(ids) == 0) null
        else {
            let distances = [for (n in ids) seg_d2(x, z, n)]
            // argmin keeps the first tied segment without a second filtering pass.
            let nearest = argmin(distances)^;
            if (distances[nearest] > REACH * REACH) null else project(x, z, SEGMENTS[ids[nearest]])
        }
    }
}
// is arc length s of the loop inside a structure of this kind, more than `margin` from its ends?
pub fn within(kind, s, margin = 0.0) any^ => len([for (st in STRUCTURES where st.kind == kind and s > st.a + margin and s < st.b - margin) st]) > 0
pub fn in_r1(x, z, r) any^ => u.dist2(x, z, R1.x, R1.z) < r * r
// the lane nearest (x, z) for a car facing `heading`: the right-hand lane of whichever way along the
// road the car points, as {x, z, y, heading}; a car far from any corridor goes to the nearest sample
pub fn nearest_lane(x, z, heading) any^ {
    let r = road_at(x, z)^
    let rd = if (r == null) LOOP else ROADS[r.r]
    let pts = LOOP.samples^
    let k = if (r != null) r.k else argmin([for (p in pts) u.dist2(p.x, p.z, x, z)^])^
    let p = rd.samples[k]
    let h = if (cos(heading - p.heading) >= 0.0) p.heading else u.wrap(p.heading + u.PI)
    let side = u.right(h);
    {x: p.x + side[0] * rd.lane, z: p.z + side[1] * rd.lane, y: p.y, heading: h}
}
