// Pedestrians stroll the sidewalk loop around their block. When the player's car bears down on one,
// they hop clear of its path, wait for it to pass, and step back onto the loop.
import u: .mod_util
import city: .mod_city
import rng: lambda.procart.rng

pub let INSET = city.ROAD + 2.5                  // the loop runs mid-sidewalk
pub let SIDE = city.PITCH - 2.0 * INSET
pub let LOOP = 4.0 * SIDE
let KEY = rng.key([city.SEED, "peds"])
let HOMES = [for (b in city.BLOCKS where contains(["R", "O", "T", "X"], b.kind)^) b]

// clockwise on the map (x east, z south): north side eastward, east side southward, and so on
pub fn on_loop(b, s) {
    let t = s - LOOP * floor(s / LOOP)
    let side = int(floor(t / SIDE)) ^ { 0 }
    let along = t - side * SIDE
    let x0 = b.x0 + INSET, x1 = b.x1 - INSET, z0 = b.z0 + INSET, z1 = b.z1 - INSET;
    if (side == 0) {x: x0 + along, z: z0, heading: u.PI / 2.0}
    else if (side == 1) {x: x1, z: z0 + along, heading: 0.0}
    else if (side == 2) {x: x1 - along, z: z1, heading: 0.0 - u.PI / 2.0}
    else {x: x0, z: z1 - along, heading: u.PI}
}

pub fn spawn(crowd = 24) => [for (n in 0 to (crowd - 1), let k = rng.derive(KEY, n), let b = HOMES[n % len(HOMES)])
    (let p = {id: "ped-" ++ string(n), n: n, bi: b.bi, bj: b.bj, s: rng.between(rng.derive(k, "s"), 0.0, LOOP),
        dir: if (rng.chance(rng.derive(k, "dir"), 0.5)) 1.0 else -1.0, pace: rng.between(rng.derive(k, "pace"), 1.1, 1.6),
        ox: 0.0, oz: 0.0, gx: 0.0, gz: 0.0, hop: 0.0, scare: 0.0, gait: rng.unit(rng.derive(k, "gait")) * u.TAU, y: city.CURB},
     {*: p, *: place(p)})]

fn home(p) => city.BLOCKS[p.bj * city.N + p.bi]
fn place(p) {
    let q = on_loop(home(p), p.s)
    let h = if (p.dir > 0.0) q.heading else u.wrap(q.heading + u.PI);
    {x: q.x + p.ox, z: q.z + p.oz, heading: h}
}
// leap sideways out of the car's path, perpendicular to its heading and away from its centreline;
// a pedestrian dead ahead steps toward the street rather than into the building line
fn threat(p, car) {
    let f = u.forward(car.heading), r = u.right(car.heading)
    let dx = p.x - car.x, dz = p.z - car.z
    let df = dx * f[0] + dz * f[1], dl = dx * r[0] + dz * r[1]
    let b = home(p)
    let outward = (p.x - (b.x0 + b.x1) / 2.0) * r[0] + (p.z - (b.z0 + b.z1) / 2.0) * r[1]
    let side = if (abs(dl) > 0.6) dl else outward
    let v = sqrt(car.vx * car.vx + car.vz * car.vz);
    if (v > 2.5 and df > -1.0 and df < 4.0 + v * 0.55 and abs(dl) < 2.8) (if (side >= 0.0) [r[0], r[1]] else [0.0 - r[0], 0.0 - r[1]]) else null
}
pub fn step_ped(p, car, dt) {
    let away = if (p.scare > 0.0) null else threat(p, car)
    let scare = if (away != null) 1.6 else max(0.0, p.scare - dt)
    // the offset springs out quickly and eases back once the scare has passed
    let tx = if (away != null) u.clamp(p.ox + away[0] * 3.2, -4.5, 4.5) else if (scare > 0.0) p.gx else 0.0
    let tz = if (away != null) u.clamp(p.oz + away[1] * 3.2, -4.5, 4.5) else if (scare > 0.0) p.gz else 0.0
    let k = if (away != null or scare > 1.2) u.ease(10.0, dt) else u.ease(2.0, dt)
    let moving = scare == 0.0
    let s = if (moving) p.s + p.dir * p.pace * dt else p.s
    let hop = if (scare > 1.2) 0.45 * sin(u.PI * (1.6 - scare) / 0.4) else 0.0
    let next = {*: p, s: s, ox: u.lerp(p.ox, tx, k), oz: u.lerp(p.oz, tz, k), gx: tx, gz: tz, scare: scare, hop: max(0.0, hop)};
    // the scene's walk clip swings the limbs and bobs the figure from this pedestrian's gait phase
    {*: next, *: place(next), y: city.ground(next.x, next.z) + next.hop,
     startled: away != null}
}
pub fn step(peds, car, dt) => [for (p in peds) step_ped(p, car, dt)]
