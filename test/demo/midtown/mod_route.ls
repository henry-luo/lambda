// Open-road traffic (Lambda_Demo_Midtown_Open_Road OR6): two closed lane paths, one each way round the
// loop road, each joining through its own arc of the gateway roundabout (anticlockwise on the map, as
// right-hand traffic circulates). A car is a path and an arc length along it; it keeps to a speed
// that suits the bend ahead and follows whatever is in front of it, the player included.
import u: .mod_util
import roads: .mod_roads
import rng: lambda.procart.rng
import city: .mod_city

let KEY = rng.key([city.SEED, "route"])
let R1 = roads.R1
let STEP = 3.0
// a point on the ring at angle a (x/z radians), travelling anticlockwise on the map (decreasing a)
fn ring_point(a) => {x: R1.x + R1.ring * cos(a), z: R1.z + R1.ring * sin(a), y: 0.0, heading: u.heading_of(sin(a), 0.0 - cos(a))}
fn arc(a0, a1) any^ => (let n = max(2, int(abs(a1 - a0) * R1.ring / STEP) ^ { 2 }), [for (k in 0 to n) ring_point(a0 + (a1 - a0) * k / n)])
// a Hermite join between two posed points, sampled every STEP metres (both ends excluded)
fn join(a, b) any^ {
    let l = u.dist(a.x, a.z, b.x, b.z)
    let n = max(2, int(l / STEP) ^ { 2 })
    let fa = u.forward(a.heading), fb = u.forward(b.heading);
    [for (k in 1 to (n - 1), let t = float(k) / n, let t2 = t * t, let t3 = t2 * t,
            let h00 = 2.0 * t3 - 3.0 * t2 + 1.0, let h10 = t3 - 2.0 * t2 + t, let h01 = 3.0 * t2 - 2.0 * t3, let h11 = t3 - t2)
        {x: h00 * a.x + h10 * l * fa[0] + h01 * b.x + h11 * l * fb[0], z: h00 * a.z + h10 * l * fa[1] + h01 * b.z + h11 * l * fb[1],
         y: a.y + (b.y - a.y) * t, heading: 0.0}]
}
// the loop's samples as lane points: the right-hand lane of travel forward, or of travel backward
fn lane(p, reverse) => (let h = if (reverse) u.wrap(p.heading + u.PI) else p.heading, let r = u.right(h),
    {x: p.x + r[0] * roads.LANE, z: p.z + r[1] * roads.LANE, y: p.y, heading: h})
let ENTRY = 0.38           // radians round the ring from an arm to where a car joins or leaves it
let LOOP_PTS = roads.LOOP.samples
fn forward_points() any^ {
    let loop = [for (p in LOOP_PTS) lane(p, false)]
    let enter = ring_point(roads.ARM_LOOP_IN - ENTRY), leave = ring_point(roads.ARM_LOOP_OUT + ENTRY);
    [*join(loop[len(loop) - 1], enter)^, *arc(roads.ARM_LOOP_IN - ENTRY, roads.ARM_LOOP_OUT + ENTRY)^, *join(leave, loop[0])^, *loop]
}
fn backward_points() any^ {
    let loop = reverse([for (p in LOOP_PTS) lane(p, true)])
    let enter = ring_point(roads.ARM_LOOP_OUT - ENTRY), leave = ring_point(roads.ARM_LOOP_IN - u.TAU + ENTRY);
    [*join(loop[len(loop) - 1], enter)^, *arc(roads.ARM_LOOP_OUT - ENTRY, roads.ARM_LOOP_IN - u.TAU + ENTRY)^, *join(leave, loop[0])^, *loop]
}
// a closed path: points with arc length, headings from neighbours, and a speed limit that suits the
// bend over the next 40 m (3 m/s² sideways, never above the road's limit)
pub let CRUISE = 22.0
fn path(id, pts) any^ {
    let n = len(pts)
    let s = math.cumsum([0.0, *[for (k in 1 to (n - 1)) u.dist(pts[k].x, pts[k].z, pts[k - 1].x, pts[k - 1].z)]])
    let total = s[n - 1] + u.dist(pts[0].x, pts[0].z, pts[n - 1].x, pts[n - 1].z)
    let headings = [for (k in 0 to (n - 1), let a = pts[(k + n - 1) % n], let b = pts[(k + 1) % n]) u.heading_of(b.x - a.x, b.z - a.z)]
    let bend = [for (k in 0 to (n - 1)) abs(u.wrap(headings[(k + 1) % n] - headings[k])) / max(0.5, s[min(n - 1, k + 1)] - s[k])]
    let reach = 14
    let limits = [for (k in 0 to (n - 1), let ahead = max([for (q in 0 to reach) bend[(k + q) % n]])) min(CRUISE, sqrt(3.0 / max(ahead, 0.0001)))];
    {id: id, length: total, mean: total / n, points: [for (k, p in pts) {*: p, s: s[k], heading: headings[k], limit: limits[k]}]}
}
pub let PATHS = [path("forward", forward_points()^)^, path("backward", backward_points()^)^]

// the pose at arc length s of path p: position, height, heading and the pitch of the climb
// the point index whose span holds arc length w: a guess from the mean spacing, walked a step or two
fn locate(pts, w, k) any^ => if (k > 0 and pts[k].s > w) locate(pts, w, k - 1)
    else if (k + 1 < len(pts) and pts[k + 1].s <= w) locate(pts, w, k + 1) else k
pub fn pose_on(p, s) any^ {
    let pts = PATHS[p].points
    let total = PATHS[p].length
    let w = s - total * floor(s / total)
    let k = locate(pts, w, min(len(pts) - 1, int(w / PATHS[p].mean) ^ { 0 }))^
    let a = pts[k], b = pts[(k + 1) % len(pts)]
    let span = (if (k + 1 < len(pts)) b.s else total) - a.s
    let t = u.clamp((w - a.s) / max(span, 0.001), 0.0, 1.0)
    let dh = u.wrap(b.heading - a.heading);
    {x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t, y: a.y + (b.y - a.y) * t, heading: u.wrap(a.heading + dh * t),
     pitch: 0.0 - math.atan2(b.y - a.y, max(span, 0.001)), limit: min(a.limit, b.limit)}
}

// ---- cars: kinds and sizes from city traffic, spread along each path
pub let KINDS = {sedan: {hl: 2.25, hw: 0.93}, hatch: {hl: 2.0, hw: 0.9}, suv: {hl: 2.4, hw: 1.0}, van: {hl: 2.45, hw: 1.0}, pickup: {hl: 2.55, hw: 1.0}}
let ROSTER = ["sedan", "suv", "hatch", "sedan", "van", "suv", "pickup", "sedan", "hatch", "suv", "sedan", "van", "suv", "sedan"]
pub let PAINTS = ["#e8e8e4", "#9aa3ad", "#1d2129", "#1f3b6e", "#7a2b26", "#2f5d46", "#4f8fc4", "#c9a13a"]
pub fn spawn(per_path = 7) => [for (p in 0 to 1, q in 0 to (per_path - 1), let n = p * per_path + q, let k = rng.derive(KEY, n), let kind = ROSTER[n % len(ROSTER)])
    (let a = {id: "route-" ++ string(n), n: n, kind: kind, hl: KINDS[kind].hl, hw: KINDS[kind].hw, path: p,
        paint: PAINTS[rng.below(rng.derive(k, "paint"), len(PAINTS))], s: (q + rng.between(rng.derive(k, "s"), 0.1, 0.6)) * PATHS[p].length / per_path,
        speed: 12.0, cruise: rng.between(rng.derive(k, "cruise"), 0.82, 1.0), bump: 0.0, laps: 0},
     {*: a, *: pose_on(p, a.s)})]

// the gap to the nearest vehicle ahead in this car's lane: any route car or the player, within 40 m
// ahead and 2.4 m to the side, at the same height (a car on the bridge never queues behind the river)
fn leader_gap(a, others) {
    let f = u.forward(a.heading), r = u.right(a.heading)
    let near = [for (o in others where abs(o.x - a.x) < 42.0 and abs(o.z - a.z) < 42.0 and o.id != a.id) o]
    let gaps = [for (o in near, let dx = o.x - a.x, let dz = o.z - a.z, let df = dx * f[0] + dz * f[1], let dl = dx * r[0] + dz * r[1]
        where df > 0.0 and df < 40.0 and abs(dl) < 2.4 and abs((o.y or 0.0) - (a.y or 0.0)) < 3.0 and cos(o.heading - a.heading) > 0.0)
        df - a.hl - o.hl];
    if (len(gaps) == 0) 99.0 else max(0.0, min(gaps) - 3.0)
}
pub fn step_car(a, others, dt, bumped) any^ {
    let bump = if (bumped) 2.0 else max(0.0, a.bump - dt)
    // the limit at the car's point already allows for the bend over the next 40 m
    let limit = a.limit * a.cruise
    let follow = sqrt(2.0 * 4.0 * leader_gap(a, others))
    let goal = if (bump > 0.0) 0.0 else min(limit, follow)
    let speed = u.approach(a.speed, goal, (if (goal > a.speed) 2.4 else 7.5) * dt)
    let s = a.s + speed * dt
    let total = PATHS[a.path].length;
    {*: a, speed: speed, bump: bump, s: s - total * floor(s / total), laps: a.laps + (if (s >= total) 1 else 0), *: pose_on(a.path, s)}
}
// cars step every fourth frame beyond 900 m of the player (unseen), every other beyond 350 m (a few
// pixels long), when a frame number is given
pub let CADENCE = [[900.0, 4], [350.0, 2]]
pub fn step(agents, player, dt, hits = [], frame = null) any^ => (let others = [*agents, player],
    u.staggered(agents, player.x, player.z, CADENCE, frame, dt, (a, owed) any^ => step_car(a, others, owed, contains(hits, a.id))^)^)

// the path and arc length nearest a car facing `heading`: Auto drive hands the car to that lane
pub fn nearest(x, z, heading, speed = 0.0) any^ {
    let r = roads.road_at(x, z)^
    let p = if (r == null or r.r != 0 or cos(heading - r.heading) >= 0.0) 0 else 1
    let pts = PATHS[p].points
    let k = reduce([for (q, pt in pts) q], (best, q) any^ => if (u.dist2(pts[q].x, pts[q].z, x, z)^ < u.dist2(pts[best].x, pts[best].z, x, z)^) q else best)
    let a = {id: "player", n: 99, kind: "sedan", hl: 2.25, hw: 0.93, path: p, paint: "#c0262d", player: true,
        s: pts[k].s, speed: max(0.0, speed), cruise: 0.95, bump: 0.0, laps: 0};
    {*: a, *: pose_on(p, a.s)}
}
