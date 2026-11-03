// City traffic: cars that keep to the right-hand lane of the grid, choose a turn at every junction,
// obey the signals, follow whatever is ahead (including the player), wait for oncoming cars before
// turning left, and stop for a moment when bumped. A car is either running down a block toward
// junction (i, j) or turning through it; its pose is a pure function of that progress.
import u: .mod_util
import city: .mod_city
import rng: lambda.procart.rng

pub let CYCLE = 25.0
pub let GREEN = 10.0
pub let AMBER = 2.5
pub let RUN = city.PITCH - 2.0 * city.EDGE        // a block's length between junction zones
let KEY = rng.key([city.SEED, "traffic"])

// signal state for a light group at scene time t; group 1 runs half a cycle after group 0
pub fn light(group, t) {
    let shifted = t + (if (group == 1) CYCLE / 2.0 else 0.0)
    let local = shifted - CYCLE * floor(shifted / CYCLE);
    if (local < GREEN) "green" else if (local < GREEN + AMBER) "amber" else "red"
}
pub fn light_group(i, j, d) => (d % 2 + (i + j) % 2) % 2

// vehicle kinds: half length and half width of the body
pub let KINDS = {sedan: {hl: 2.25, hw: 0.93}, hatch: {hl: 2.0, hw: 0.9}, van: {hl: 2.45, hw: 1.0},
    taxi: {hl: 2.25, hw: 0.93}, pickup: {hl: 2.55, hw: 1.0}}
pub let ROSTER = ["sedan", "hatch", "taxi", "van", "sedan", "pickup", "hatch", "sedan", "taxi", "sedan",
    "van", "hatch", "sedan", "pickup", "sedan", "taxi"]
pub let PAINTS = ["#e8e8e4", "#9aa3ad", "#1d2129", "#1f3b6e", "#7a2b26", "#2f5d46", "#4f8fc4", "#c9a13a"]

fn node(i, j) => [city.road(i), city.road(j)]
fn on_grid(i, j) => i >= 0 and i <= city.N and j >= 0 and j <= city.N
// 0 straight, 1 right, -1 left; the grid always offers one of these, so no car needs a U-turn
fn turn_sign(d, d2) => if (d2 == (d + 1) % 4) 1 else if (d2 == (d + 3) % 4) -1 else 0
pub fn turn_length(turn) => if (turn == 0) 2.0 * city.EDGE else (city.EDGE - turn * city.LANE) * u.PI / 2.0

// the next direction out of junction (i, j) for a car travelling d; straight is twice as likely
pub fn choose(i, j, d, k) {
    let options = [for (d2 in [d, d, (d + 1) % 4, (d + 3) % 4], let f = city.DIRS[d2]
        where on_grid(i + int(f[0]), j + int(f[1]))) d2];
    options[rng.below(k, len(options))]
}

// position and heading along the current run or turn
pub fn pose(a) {
    let b = node(a.i, a.j), f = city.DIRS[a.d]
    let r = [0.0 - f[1], f[0]]
    let h = city.heading_of_dir(a.d);
    if (a.phase == "run") {
        let back = city.PITCH - city.EDGE - a.s;
        {x: b[0] - f[0] * back + r[0] * city.LANE, z: b[1] - f[1] * back + r[1] * city.LANE, heading: h}
    } else {
        let turn = turn_sign(a.d, a.next)
        if (turn == 0) {x: b[0] + f[0] * (a.s - city.EDGE) + r[0] * city.LANE, z: b[1] + f[1] * (a.s - city.EDGE) + r[1] * city.LANE, heading: h}
        else {
            let radius = city.EDGE - turn * city.LANE
            let phi = a.s / radius
            let cx = b[0] - f[0] * city.EDGE + turn * r[0] * city.EDGE, cz = b[1] - f[1] * city.EDGE + turn * r[1] * city.EDGE;
            {x: cx + radius * (0.0 - turn * r[0] * cos(phi) + f[0] * sin(phi)),
             z: cz + radius * (0.0 - turn * r[1] * cos(phi) + f[1] * sin(phi)), heading: h - turn * phi}
        }
    }
}

// every block run that has a junction at both ends, as a spawn site
let RUNS = [for (i in 0 to city.N, j in 0 to city.N, d in 0 to 3, let f = city.DIRS[d]
    where on_grid(i - int(f[0]), j - int(f[1]))) {i: i, j: j, d: d}]
pub fn spawn(fleet = 16) {
    let picks = rng.sample(rng.derive(KEY, "spawn"), RUNS, fleet);
    [for (n, run in picks, let k = rng.derive(KEY, ["car", n]), let kind = ROSTER[n % len(ROSTER)])
        (let a = {id: "traffic-" ++ string(n), n: n, kind: kind, hl: KINDS[kind].hl, hw: KINDS[kind].hw,
            paint: if (kind == "taxi") "#f2c230" else PAINTS[rng.below(rng.derive(k, "paint"), len(PAINTS))],
            i: run.i, j: run.j, d: run.d, phase: "run", s: rng.between(rng.derive(k, "s"), 4.0, RUN - 12.0),
            next: choose(run.i, run.j, run.d, rng.derive(k, "turn0")), trips: 0, speed: 0.0,
            cruise: rng.between(rng.derive(k, "cruise"), 8.5, 12.0), bump: 0.0, wait: 0.0},
         {*: a, *: pose(a)})]
}

// ---- driving rules
let BRAKE = 4.5
fn stop_limit(distance) => sqrt(2.0 * BRAKE * max(0.0, distance))
// the closest vehicle ahead in this car's path: same-direction traffic, anything at close range, and the player
fn leader_limit(a, others) {
    let f = u.forward(a.heading), r = u.right(a.heading)
    let gaps = [for (o in others, let dx = o.x - a.x, let dz = o.z - a.z, let df = dx * f[0] + dz * f[1],
            let dl = dx * r[0] + dz * r[1], let same = cos(o.heading - a.heading) > 0.5
        where o.id != a.id and df > 0.0 and df < 32.0 and abs(dl) < 2.3 and (same or o.player == true or df < 9.0))
        df - a.hl - o.hl];
    if (len(gaps) == 0) 99.0 else max(0.0, (min(gaps) - 2.0) * 0.9)
}
// a car about to turn left waits while oncoming traffic is close to the junction
fn oncoming(a, others) {
    let f = u.forward(a.heading), r = u.right(a.heading);
    len([for (o in others, let dx = o.x - a.x, let dz = o.z - a.z, let df = dx * f[0] + dz * f[1],
            let dl = dx * r[0] + dz * r[1]
        where o.id != a.id and cos(o.heading - a.heading) < -0.7 and df > 2.0 and df < 34.0 and dl < -1.0 and dl > -9.0) o]) > 0
}
fn target_speed(a, others, t) {
    let turn = turn_sign(a.d, a.next)
    let turn_v = if (turn == 0) a.cruise else if (turn == 1) 5.0 else 6.5
    let to_line = RUN - a.s - 1.0
    let signal = if (city.junction_signalised(a.i, a.j)) light(light_group(a.i, a.j, a.d), t) else "green"
    // amber stops a car that can stop comfortably; red always does
    let halt = a.phase == "run" and (signal == "red" or (signal == "amber" and a.speed * a.speed / (2.0 * BRAKE) < to_line) or
        (turn == -1 and to_line < 14.0 and oncoming(a, others)))
    let stop_v = if (halt) stop_limit(to_line - 0.5) else 99.0
    let corner = if (a.phase == "run") sqrt(turn_v * turn_v + 2.0 * 3.0 * max(0.0, RUN - a.s)) else turn_v;
    min([a.cruise, stop_v, corner, leader_limit(a, others)])
}
// advance along the path, carrying leftover distance into the next phase
fn advance(a, distance) {
    let length = if (a.phase == "run") RUN else turn_length(turn_sign(a.d, a.next))
    let s = a.s + distance;
    if (s < length) {*: a, s: s}
    else if (a.phase == "run") advance({*: a, phase: "turn", s: 0.0}, s - length)
    else {
        let f = city.DIRS[a.next]
        let i = a.i + int(f[0]), j = a.j + int(f[1])
        let trips = a.trips + 1;
        advance({*: a, phase: "run", s: 0.0, i: i, j: j, d: a.next, trips: trips,
            next: choose(i, j, a.next, rng.derive(KEY, [a.n, trips]))}, s - length)
    }
}
pub fn step_car(a, others, t, dt, bumped) {
    let bump = if (bumped) 2.0 else max(0.0, a.bump - dt)
    let goal = if (bump > 0.0) 0.0 else target_speed(a, others, t)
    let speed = u.approach(a.speed, goal, (if (goal > a.speed) 2.6 else 8.0) * dt)
    let moved = advance({*: a, speed: speed, bump: bump, wait: if (speed < 0.2) a.wait + dt else 0.0}, speed * dt);
    {*: moved, *: pose(moved)}
}
// one tick for all traffic; player is {x, z, heading, hl, id, player: true}, hits lists the ids it struck
pub fn step(agents, player, t, dt, hits = []) {
    let others = [*agents, player];
    [for (a in agents) step_car(a, others, t, dt, contains(hits, a.id))]
}

// the lane run nearest a free-driving car, as an agent record: Reset puts the car there and
// Auto drive steers it from there. The run must start at a junction on the grid.
pub fn nearest_run(x, z, heading, speed = 0.0) {
    let d = reduce([0, 1, 2, 3], (best, k) =>
        if (abs(u.wrap(heading - city.heading_of_dir(k))) < abs(u.wrap(heading - city.heading_of_dir(best)))) k else best)
    let f = city.DIRS[d]
    let along_x = abs(f[0]) > 0.5
    let toward = if (along_x) f[0] else f[1]
    let line = city.nearest(if (along_x) z else x)
    let pos = if (along_x) x else z
    // the first junction whose entry edge still lies ahead
    let ahead = [for (k in 0 to city.N where (city.road(k) - toward * city.EDGE - pos) * toward >= 0.0) k]
    let first = if (len(ahead) == 0) (if (toward > 0.0) city.N else 0) else if (toward > 0.0) ahead[0] else ahead[len(ahead) - 1]
    let k = int(u.clamp(first, if (toward > 0.0) 1 else 0, if (toward > 0.0) city.N else city.N - 1)) ^ { 1 }
    let i = if (along_x) k else line, j = if (along_x) line else k
    let s = u.clamp(RUN - (city.road(k) - toward * city.EDGE - pos) * toward, 0.0, RUN - 0.5)
    let a = {id: "player", n: 97, kind: "sedan", hl: KINDS.sedan.hl, hw: KINDS.sedan.hw, paint: "#c0262d", player: true,
        i: i, j: j, d: d, phase: "run", s: s, next: choose(i, j, d, rng.derive(KEY, ["auto", i, j, d])), trips: 0,
        speed: max(0.0, speed), cruise: 13.0, bump: 0.0, wait: 0.0};
    {*: a, *: pose(a)}
}
