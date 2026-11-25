// The game as a pure step over scene-clock time: rowing, the fishing sequence (cast, wait,
// nibbles, a bite, the strike and release), fish that wander, school, shy from the boat and
// come to the bobber, an otter that keeps the angler company, and a camera that follows. Each
// step also lists what the host must do this frame: clips to switch, fish swim rates to change,
// and ripples and splashes to spawn. Headings are radians about +Y with forward [sin h, cos h].
import t: lambda.scene3d.transform
import u: ~~.midtown.mod_util
import w: .mod_world
import fish: .mod_fish
import angler: .mod_angler
import otter: .mod_otter

pub let NO_KEYS = {up: false, down: false, left: false, right: false}
fn hash(a, b) => (let v = sin(a * 12.9898 + b * 78.233) * 43758.5453, v - floor(v))
fn steer(h, target, rate, dt) any^ => h + u.clamp(u.wrap(target - h), 0.0 - rate * dt, rate * dt)
// boat space to world: the boat sits at (x, y, z) facing h
pub fn to_world(b, p) any^ => (let c = cos(b.h), let sn = sin(b.h), [b.x + p[0] * c + p[2] * sn, b.y + p[1], b.z - p[0] * sn + p[2] * c])
fn on_water(p) => [p[0], 0.02, p[2]]
fn spawn(emitter, p, count) => {emitter: emitter, position: on_water(p), count: count}
fn splash(p, size) => [spawn("splash", p, size), spawn("foam", p, 1), spawn(if (size > 12) "ring-l" else "ring-m", p, 1)]

// ---- starting state
fn place_fish(r, i) any^ {
    let school = r.kind == "minnow"
    let a = (if (school) 0.8 else w.roll(3000 + i, 0)^ * u.TAU), d = if (school) 0.35 else 0.15 + 0.6 * sqrt(w.roll(3000 + i, 1)^)
    let x = cos(a) * w.RX * d + (if (school) (w.roll(3000 + i, 2)^ - 0.5) * 2.0 else 0.0)
    let z = sin(a) * w.RZ * d + (if (school) (w.roll(3000 + i, 3)^ - 0.5) * 2.0 else 0.0)
    let sp = fish.SPECIES[r.kind];
    {id: r.id, i: i, kind: r.kind, L: r.length, x: x, z: z, y: 0.0 - (sp.depth[0] + sp.depth[1]) / 2.0, h: w.roll(3000 + i, 4)^ * u.TAU,
     v: 0.0, pitch: 0.0, roll: 0.0, mode: if (school) "school" else "wander", goal: [x, -0.6, z], until: 0.0, pace: 0.8, band: 1, cruise: sp.speed}
}
pub fn initial() any^ {
    let b = {x: w.START.x, z: w.START.z, h: w.START.heading, y: 0.0, v: 0.0, turn: 0.0, pitch: 0.0, roll: 0.0, rock: 0.0}
    let o = {x: w.START.x - 2.2, y: -0.05, z: w.START.z + 1.4, h: w.START.heading, v: 0.0, roll: 0.0, pitch: 0.0, mode: "follow", since: 0.0, spin: 0.0};
    {t: 0.0, frame: 0, boat: b, keys: NO_KEYS, press: false, assist: false, mode: "idle", since: 0.0, clip: "idle", after: null,
     fish: [for (i, r in fish.ROSTER) place_fish(r, i)^], target: null, nibbles: 0, next_nibble: 0.0, alone_since: 0.0, cooldown: 0.0,
     otter: o, otter_clip: "otter-swim", still_since: 0.0,
     camera: camera_rig(b, null, 1.0), zoom: 1.0, catches: 0, best: {}, toast: "", toast_until: 0.0, celebrate: null,
     next: {wake: 0.0, bow: 0.0, hull: 1.0, bob: 0.0, rise: 5.0, ambient: 1.0, otter: 0.0},
     plan: [], rates: [], spawns: []}
}

// FISHING_STAGE=bite: the line already out and the biggest koi nibbling under the bobber, so a
// replay reaches the strike in about a second (a development and test aid)
pub fn staged(st, stage) any^ {
    if (stage != "bite") { return st }
    let bob = bobber_world(st)^
    let koi = [for (f in st.fish where f.kind == "koi") f]
    let pick = reduce(koi, (a, f) => if (f.L > a.L) f else a)
    return {*: st, mode: "stage", target: pick.id, nibbles: 0, next_nibble: 0.8, assist: true,
        fish: [for (f in st.fish) if (f.id == pick.id) {*: f, mode: "nibble", x: bob[0] - 0.4, z: bob[2] + 0.2, y: -0.3, h: 2.0} else f]}
}

// ---- camera: above and behind the boat looking north; it eases toward the bobber while fishing
pub fn camera_rig(b, focus, zoom) => (let fx = if (focus == null) b.x else (b.x + focus[0]) / 2.0, let fz = if (focus == null) b.z - 0.5 else (b.z + focus[2]) / 2.0,
    {position: [fx, 6.6 * zoom + 0.5, fz + 4.4 * zoom], target: [fx, -0.4, fz]})
fn follow(c, want, k) => {position: [for (i in 0 to 2) c.position[i] + (want.position[i] - c.position[i]) * k],
    target: [for (i in 0 to 2) c.target[i] + (want.target[i] - c.target[i]) * k]}

// ---- the angler's state machine; entering a mode switches the boat's clip
fn enter(st, mode, now, after = null) any^ => {*: st, mode: mode, since: now, after: after, clip: mode, plan: [*st.plan, {stop: st.clip, play: mode}]}
pub fn line_out(mode) => contains(["wait", "nibble", "bite"], mode)
fn moving(keys) => keys.up or keys.down or keys.left or keys.right
pub fn bobber_world(st) any^ => to_world(st.boat, angler.CAST_SPOT)^
fn engaged(st, f) => st.target == f.id
pub fn hint(st) => if (st.mode == "idle" or st.mode == "row") "Arrows or WASD to row · Space to cast"
    else if (st.mode == "wait") "Wait for a bite… Space reels in"
    else if (st.mode == "nibble") "Something's nibbling…" else if (st.mode == "bite") "Strike! Press Space"
    else if (st.mode == "strike") (if (st.t - st.since >= angler.LIFTED) "Nice catch!" else "Reeling in!") else if (st.mode == "ready") "Space to cast · arrows to row" else ""
pub fn toggle_assist(st) => {*: st, assist: not st.assist, toast: if (st.assist) "Strike assist off" else "Strike assist on", toast_until: st.t + 1.4}

// a press of Space or a tap: cast, reel in or strike, by what is happening
pub fn press(st, now) any^ {
    let s1 = {*: st, press: false}
    if (st.mode == "idle" or st.mode == "row") {
        return if (abs(st.boat.v) > 0.7) {*: s1, toast: "Slow down to cast", toast_until: now + 1.2} else enter(s1, "to-fish", now, "cast")^
    }
    if (st.mode == "ready") { return enter(s1, "cast", now)^ }
    if (st.mode == "wait" or st.mode == "nibble") { return release_target(enter(s1, "reel", now, "ready")^, now, false)^ }
    if (st.mode == "bite" and now - st.since < angler.BITE_WINDOW) { return hook_fish(enter(s1, "strike", now)^, now)^ }
    return s1
}
fn hook_fish(st, now) any^ => {*: st, fish: [for (f in st.fish) if (f.id == st.target) {*: f, mode: "hooked"} else f],
    rates: [*st.rates, {clip: "swim-" ++ st.target, rate: 2.6, weight: 1.0}]}
// the fish of interest swims off: startled when it got away or was reeled past, calmly otherwise
fn release_target(st, now, startled) any^ => if (st.target == null) st else {*: st, target: null, cooldown: now + (if (startled) 3.0 else 1.5), alone_since: now,
    fish: [for (f in st.fish) if (f.id == st.target) {*: f, mode: if (startled) "flee" else "wander", until: now + 2.5, goal: [f.x, -0.9, f.z]} else f]}

// timed transitions within the sequence, and the splashes and rings each moment makes
fn timeline(st, now, before) any^ {
    let x = now - st.since, prev = before - st.since
    let crossed = (at) => (prev < at and x >= at) ^ { false }
    let b = st.boat
    if (st.mode == "stage") { return enter(st, "wait", now)^ }
    if (st.mode == "to-fish" and x >= 0.8) { return enter(st, if (st.after == "cast") "cast" else "ready", now)^ }
    if (st.mode == "to-row" and x >= 0.8) { return enter(st, "idle", now)^ }
    if (st.mode == "cast") {
        let landed = if (crossed(angler.CAST_LANDS)) {*: st, spawns: [*st.spawns, *splash(bobber_world(st)^, 8)], alone_since: now} else st
        return if (x >= angler.CAST_TIME) {*: enter(landed, "wait", now)^, next: {*: st.next, bob: now + 1.4}} else landed
    }
    if (st.mode == "nibble" and x >= angler.NIBBLE_TIME) { return enter(st, "wait", now)^ }
    if (st.mode == "nibble") {
        let rings = [for (at in angler.NIBBLES where (crossed(at) ^ { false })) spawn("ring-s", bobber_world(st)^, 1)]
        return {*: st, spawns: [*st.spawns, *rings]}
    }
    if (st.mode == "bite") {
        let pulled = if (crossed(0.05)) {*: st, spawns: [*st.spawns, *splash(bobber_world(st)^, 10)], boat: {*: b, rock: max(b.rock, 0.5)}} else st
        let missed = if (crossed(angler.BITE_WINDOW)) {*: release_target(pulled, now, true)^, toast: "It got away!", toast_until: now + 1.6} else pulled
        return if (x >= angler.BITE_TIME) enter(missed, "wait", now)^ else missed
    }
    if (st.mode == "reel" and x >= angler.REEL_TIME) { return if (st.after == "row") enter(st, "to-row", now)^ else enter(st, "ready", now)^ }
    if (st.mode == "strike") { return strike_timeline(st, now, x, crossed)^ }
    return st
}
fn length_cm(f) any^ => int(round(f.L * 100.0))
fn strike_timeline(st, now, x, crossed) any^ {
    let caught = [for (f in st.fish where f.id == st.target) f]
    let f = if (len(caught) > 0) caught[0] else null
    let b = st.boat
    let marks = [for (at in [0.05, 0.45, 0.85, 1.25, 1.65] where (crossed(at) ^ { false })) at]
    let fights = if (f == null or len(marks) == 0) [] else [spawn("splash", to_world(b, angler.hook(x))^, 7), spawn("ring-s", to_world(b, angler.hook(x))^, 1)]
    let lifted = if (crossed(angler.FIGHT + 0.05)) splash(to_world(b, angler.BOATSIDE)^, 14) else []
    let landed = if (crossed(angler.BACK_IN)) splash(to_world(b, angler.RELEASE)^, 14) else []
    let s1 = {*: st, spawns: [*st.spawns, *fights, *lifted, *landed], boat: if (len(lifted) > 0) {*: b, rock: 1.0} else b}
    let s2 = if (f != null and crossed(angler.LIFTED)) (let cm = length_cm(f)^, let name = fish.SPECIES[f.kind].name, let best = s1.best[f.kind] or 0,
        {*: s1, catches: s1.catches + 1, best: {*: s1.best, [f.kind]: max(best, cm)}, celebrate: now,
         toast: name ++ " · " ++ string(cm) ++ " cm" ++ (if (cm > best and best > 0) " · new best!" else ""), toast_until: now + 2.8}) else s1
    // back in the water: the fish swims off from where it splashed down
    let s3 = if (f != null and crossed(angler.BACK_IN)) (let p = to_world(b, angler.RELEASE)^, let away = math.atan2(p[0] - b.x, p[2] - b.z),
        {*: release_target({*: s2, fish: [for (g in (s2.fish)^) if ((g.id == f.id)^) {*: g, mode: "flee", x: p[0], y: -0.25, z: p[2], h: away, v: g.cruise * 2.0, until: now + 3.0} else g],
            rates: [*s2.rates, {clip: "swim-" ++ f.id, rate: 1.0, weight: 1.0}]}, now, true)^}) else s2
    return if (x >= angler.STRIKE_TIME) enter(s3, "ready", now)^ else s3
}

// ---- rowing
fn row_boat(st, dt) any^ {
    let b = st.boat
    let rowing = st.mode == "idle" or st.mode == "row"
    let thrust = if (not rowing) 0.0 else (if (st.keys.up) 1.0 else 0.0) - (if (st.keys.down) 0.6 else 0.0)
    let turn_in = if (not rowing) 0.0 else (if (st.keys.left) 1.0 else 0.0) - (if (st.keys.right) 1.0 else 0.0)
    let v = b.v + (thrust * 1.4 - b.v * (if (rowing) 0.85 else 2.2)) * dt
    let turn = u.approach(b.turn, turn_in * 0.85, 2.6 * dt)
    let h = b.h + turn * dt
    let nx = b.x + sin(h) * v * dt, nz = b.z + cos(h) * v * dt
    let dir = if (v < 0.0) -1.0 else 1.0
    let ahead = [nx + sin(h) * 1.3 * dir, nz + cos(h) * 1.3 * dir]
    let blocked = not (w.navigable(ahead[0], ahead[1]) ^ { false }) or any([for (r in w.OBSTACLES) u.dist(ahead[0], ahead[1], r.x, r.z) < r.r + 0.35])
    let rock = max(0.0, b.rock - dt * 0.6);
    {*: b, x: if (blocked) b.x else nx, z: if (blocked) b.z else nz, h: h, v: if (blocked) -0.25 * v else v, turn: turn, rock: if (blocked and abs(v) > 0.4) 0.7 else rock,
     y: 0.012 * sin(st.t * 1.7) + 0.03 * rock * sin(st.t * 9.0), pitch: -0.025 * (thrust - v * 0.3) + 0.04 * rock * sin(st.t * 7.0 + 1.0),
     roll: 0.015 * sin(st.t * 1.3) + 0.06 * turn * v + 0.06 * rock * sin(st.t * 8.0)}
}
fn rowing_mode(st, now) any^ => if (st.mode == "idle" and moving(st.keys)) enter(st, "row", now)^
    else if (st.mode == "row" and not moving(st.keys)) enter(st, "idle", now)^
    else if (st.mode == "ready" and moving(st.keys)) enter(st, "to-row", now)^
    else if (line_out(st.mode) and moving(st.keys)) release_target(enter(st, "reel", now, "row")^, now, st.mode == "bite")^
    else st
// rings from the oar blades as they bite and leave the water, a wake while under way, and a lap at the hull at rest
fn boat_rings(st, now, dt) any^ {
    let b = st.boat
    let phase = if (st.mode == "row") (now - st.since) / angler.ROW_PERIOD else 0.0
    let prev = phase - dt / angler.ROW_PERIOD
    let entry = st.mode == "row" and floor(phase - 0.02) != floor(prev - 0.02)
    let exit = st.mode == "row" and floor(phase - 0.5) != floor(prev - 0.5)
    let blades = angler.blades(angler.row(phase))
    let strokes = if (entry) [for (p in blades) spawn("ring-s", to_world(b, p)^, 1)] else if (exit) [for (p in blades) spawn("splash", to_world(b, p)^, 3)] else []
    let wake = if (abs(b.v) > 0.35 and now >= st.next.wake) [spawn("ring-m", to_world(b, [0.0, 0.0, -1.15])^, 1)] else []
    let bow = if (abs(b.v) > 0.6 and now >= st.next.bow) [spawn("ring-s", to_world(b, [0.0, 0.0, 1.2])^, 1)] else []
    let hull = if (abs(b.v) < 0.2 and now >= st.next.hull) [spawn("ring-m", [b.x, 0.0, b.z], 1)] else []
    let bob = if (st.mode == "wait" and now >= st.next.bob) [spawn("ring-s", bobber_world(st)^, 1)] else [];
    {*: st, spawns: [*st.spawns, *strokes, *wake, *bow, *hull, *bob],
     next: {*: st.next, wake: if (len(wake) > 0) now + 0.42 else st.next.wake, bow: if (len(bow) > 0) now + 0.6 else st.next.bow,
        hull: if (len(hull) > 0) now + 3.3 else st.next.hull, bob: if (len(bob) > 0) now + 2.7 else st.next.bob}}
}

// ---- fish
fn habitat(x, z) any^ => (w.depth(x, z) ^ { 0.0 }) > 0.5 and (w.shore(x, z) ^ { 2.0 }) < 0.88
fn wander_goal(f, now) any^ => (let a = hash(f.i * 7.1, now) * u.TAU, let d = 2.0 + 5.0 * hash(f.i * 3.3, now + 1.7),
    let x = f.x + cos(a) * d, let z = f.z + sin(a) * d, let sp = fish.SPECIES[f.kind],
    let deep = sp.depth[0] + (sp.depth[1] - sp.depth[0]) * hash(f.i, now + 4.2),
    let ok = habitat(x, z)^, [if (ok) x else f.x * 0.7, 0.0 - min(deep, max(0.25, (w.depth(x, z) ^ { 1.0 }) - 0.18)), if (ok) z else f.z * 0.7])
fn avoid(f, h) any^ => (let close = [for (r in w.OBSTACLES where (u.dist(f.x + sin(h) * 0.9, f.z + cos(h) * 0.9, r.x, r.z) ^ { 1000.0 }) < r.r + 0.5) r],
    if (len(close) == 0) h else (let r = close[0], let away = math.atan2(f.x - r.x, f.z - r.z), h + u.wrap(away - h) * 0.6))
// what a fish wants this frame: a point to swim to and a speed
fn intent(f, st, leader, now) any^ {
    let sp = fish.SPECIES[f.kind]
    let b = st.boat
    let bob = bobber_world(st)^
    if (f.mode == "approach") {
        let a = math.atan2(f.x - bob[0], f.z - bob[2])
        let far = (u.dist(f.x, f.z, bob[0], bob[2]) ^ { 0.0 }) > 4.0
        return {goal: [bob[0] + sin(a) * 0.3, -0.22, bob[2] + cos(a) * 0.3], speed: sp.speed * (if (far) 1.5 else 0.75)}
    }
    if (f.mode == "nibble" or (f.mode == "bite")) { return {goal: [bob[0], -0.16, bob[2]], speed: 0.12} }
    if (f.mode == "flee") {
        let a = math.atan2(f.x - b.x, f.z - b.z)
        return {goal: [f.x + sin(a) * 3.0, -1.0, f.z + cos(a) * 3.0], speed: sp.speed * 2.6}
    }
    if (f.mode == "rise") { return {goal: [f.x + sin(f.h) * 0.6, -0.06, f.z + cos(f.h) * 0.6], speed: sp.speed} }
    if (f.mode == "school" and leader != null and leader.id != f.id) {
        let k = f.i - leader.i, a = leader.h + 2.4 + k * 0.85, r = 0.35 + 0.12 * k
        return {goal: [leader.x - sin(leader.h) * 0.4 + cos(a) * r, leader.y + 0.06 * sin(k * 1.3), leader.z - cos(leader.h) * 0.4 + sin(a) * r], speed: leader.v * 1.15 + 0.1}
    }
    return {goal: f.goal, speed: sp.speed * f.pace}
}
fn step_fish(f, st, leader, now, dt) any^ {
    if (f.mode == "hooked") { return f }
    let sp = fish.SPECIES[f.kind]
    let b = st.boat
    let near_boat = u.dist(f.x, f.z, b.x, b.z) < 1.9
    let calm = f.mode == "wander" or f.mode == "school" or f.mode == "rise"
    let mode = if (calm and near_boat and st.target != f.id) "flee" else if (f.mode == "flee" and now > f.until) "wander" else f.mode
    let f1 = if (mode != f.mode) {*: f, mode: mode, until: now + 2.0} else f
    let arrived = u.dist(f1.x, f1.z, f1.goal[0], f1.goal[2]) < 0.6
    let f2 = if (f1.mode == "wander" and (arrived or now > f1.until)) {*: f1, goal: wander_goal(f1, now)^, until: now + 5.0 + 6.0 * hash(f1.i, now),
        pace: if (hash(f1.i + 0.5, now) < 0.25) 0.15 else 0.6 + 0.4 * hash(f1.i + 0.9, now)} else f1
    let want = intent(f2, st, leader, now)^
    let dx = want.goal[0] - f2.x, dz = want.goal[2] - f2.z
    let dist = sqrt(dx * dx + dz * dz)
    let target_h = avoid(f2, if (dist > 0.05) math.atan2(dx, dz) else f2.h)^
    let turn_rate = 2.6 - 1.5 * u.clamp(f2.L, 0.0, 0.8)
    let h = steer(f2.h, target_h, turn_rate, dt)^
    let speed = min(want.speed, if (dist < 0.4) dist * 1.5 + 0.05 else want.speed)
    let v = u.approach(f2.v, speed, 1.2 * dt)
    let nx = f2.x + sin(h) * v * dt, nz = f2.z + cos(h) * v * dt
    let ok = habitat(nx, nz)^ or f2.mode == "approach" or f2.mode == "nibble" or f2.mode == "bite" or f2.mode == "rise"
    let floor_y = w.floor_y(nx, nz) ^ { -1.0 }
    let y = u.clamp(f2.y + u.clamp(want.goal[1] - f2.y, -0.25 * dt, 0.25 * dt) * (if (f2.mode == "rise") 3.0 else 1.0), floor_y + 0.12, -0.05)
    let turning = u.wrap(h - f2.h) / max(dt, 0.001);
    {*: f2, x: if (ok) nx else f2.x, z: if (ok) nz else f2.z, y: y, h: if (ok) h else h + 1.2 * dt, v: if (ok) v else v * 0.5,
     pitch: u.clamp(0.0 - (y - f2.y) / max(dt * max(v, 0.1), 0.001) * 0.6, -0.4, 0.4), roll: u.clamp(0.0 - turning * 0.12, -0.35, 0.35),
     goal: if (ok) f2.goal else [f2.x * 0.6, f2.goal[1], f2.z * 0.6], band: if (v < 0.12) 0 else if (v > sp.speed * 1.6) 2 else 1}
}
// a tail-beat that suits the speed: sculling at a hover, quick strokes in a dash
fn swim_rates(before, after) any^ => [for (i, f in after where f.band != before[i].band and f.mode != "hooked")
    {clip: "swim-" ++ f.id, rate: [0.55, 1.0, 2.2][f.band], weight: [0.35, 1.0, 1.0][f.band]}]

// the bobber draws one fish at a time: it swims up, nibbles once or twice, then takes it
fn fishing(st, now) any^ {
    if (not line_out(st.mode)) { return st }
    let bob = bobber_world(st)^
    if (st.target == null) {
        if (st.mode != "wait" or now < st.cooldown) { return st }
        // minnows school past but are too small to take the hook
        let close = [for (f in st.fish where f.mode == "wander" and f.kind != "minnow" and (u.dist(f.x, f.z, bob[0], bob[2]) ^ { 1000.0 }) < 6.5) f]
        // a lone bobber draws the nearest fish over after a few seconds; until then any fish close by may wander up
        let lured = now - st.alone_since > 4.0
        let pool = if (len(close) > 0 and not lured) close else [for (f in st.fish where f.mode == "wander" and f.kind != "minnow") f]
        if (len(pool) == 0 or (not lured and hash(st.frame, 3.0) > 0.03)) { return st }
        let ranked = [for (f in pool) {f: f, d: u.dist(f.x, f.z, bob[0], bob[2]) ^ { 1000.0 }}]
        let pick = if (lured) reduce(ranked, (a, r) => if ((r.d < a.d) ^ { false }) r else a).f else pool[int(floor(hash(st.frame, 7.0) * len(pool)))]
        return {*: st, target: pick.id, fish: [for (f in st.fish) if (f.id == pick.id) {*: f, mode: "approach"} else f], nibbles: 1 + int(floor(hash(st.frame, 9.0) * 2.5))}
    }
    let mine = [for (f in st.fish where f.id == st.target) f]
    if (len(mine) == 0) { return {*: st, target: null} }
    let f = mine[0]
    if (f.mode == "approach" and u.dist(f.x, f.z, bob[0], bob[2]) < 0.5) {
        return {*: st, fish: [for (g in st.fish) if (g.id == f.id) {*: g, mode: "nibble"} else g], next_nibble: now + 0.6 + hash(st.frame, 1.0) * 1.2}
    }
    if (f.mode == "nibble" and st.mode == "wait" and now >= st.next_nibble) {
        if (st.nibbles > 0) { return {*: enter(st, "nibble", now)^, nibbles: st.nibbles - 1, next_nibble: now + angler.NIBBLE_TIME + 0.5 + hash(st.frame, 2.0) * 1.4} }
        return {*: enter(st, "bite", now)^, fish: [for (g in st.fish) if (g.id == f.id) {*: g, mode: "bite"} else g]}
    }
    return st
}
// every few seconds a fish near the view rises to sip at the surface and leaves a ring
fn rises(st, now) any^ {
    let c = st.camera.target
    let s1 = if (now < st.next.rise) st else (let pool = [for (f in st.fish where f.mode == "wander" and (u.dist(f.x, f.z, c[0], c[2]) ^ { 1000.0 }) < 9.0) f],
        if (len(pool) == 0) {*: st, next: {*: st.next, rise: now + 2.0}}
        else (let pick = pool[int(floor(hash(st.frame, 5.0) * len(pool)))],
            {*: st, fish: [for (f in st.fish) if (f.id == pick.id) {*: f, mode: "rise", until: now + 4.0} else f], next: {*: st.next, rise: now + 4.0 + 4.0 * hash(st.frame, 6.0)}}))
    let sipped = [for (f in s1.fish where f.mode == "rise" and (f.y > -0.12 or now > f.until)) f]
    let ambient = if (now >= s1.next.ambient) [spawn("ring-s", [c[0] + (hash(st.frame, 11.0) - 0.5) * 14.0, 0.0, c[2] + (hash(st.frame, 12.0) - 0.5) * 9.0], 1)] else [];
    {*: s1, fish: [for (f in s1.fish) if (f.mode == "rise" and (f.y > -0.12 or now > f.until)) {*: f, mode: "wander", goal: [f.x, -0.9, f.z], until: now + 3.0} else f],
     spawns: [*s1.spawns, *[for (f in sipped where f.y > -0.12) spawn("ring-s", [f.x + sin(f.h) * f.L * 0.5, 0.0, f.z + cos(f.h) * f.L * 0.5], 1)], *ambient],
     next: {*: s1.next, ambient: if (len(ambient) > 0) now + 1.2 + 2.0 * hash(st.frame, 13.0) else s1.next.ambient}}
}

// ---- the otter: keeps the boat company, floats on its back when the boat rests, rolls for joy at a catch
fn step_otter(st, now, dt) any^ {
    let o = st.otter, b = st.boat
    let still = now - st.still_since
    let side = [b.x + cos(b.h) * 2.2 - sin(b.h) * 1.0, b.z - sin(b.h) * 2.2 - cos(b.h) * 1.0]
    let dx = side[0] - o.x, dz = side[1] - o.z
    let dist = sqrt(dx * dx + dz * dz)
    let floating = still > 5.0 and dist < 2.5 and st.mode != "strike"
    let joy = st.celebrate != null and now - st.celebrate < 1.1
    let speed = if (floating) 0.08 else u.clamp(dist * 0.9, 0.0, 2.0)
    let target_h = if (floating) o.h + 0.15 * dt else if (dist > 0.3) math.atan2(dx, dz) else b.h
    let h = steer(o.h, target_h, 2.2, dt)^
    let v = u.approach(o.v, speed, 1.5 * dt)
    let nx = o.x + sin(h) * v * dt, nz = o.z + cos(h) * v * dt
    let wet = (w.shore(nx, nz) ^ { 2.0 }) < 0.97 and (w.depth(nx, nz) ^ { 0.0 }) > 0.25
    let roll_goal = if (joy) o.roll + 9.0 * dt else if (floating) math.pi else 0.0
    let roll = if (joy) roll_goal else o.roll + u.clamp(u.wrap(roll_goal - o.roll), -3.0 * dt, 3.0 * dt)
    let clip = if (floating and abs(u.wrap(roll - math.pi)) < 0.6) "otter-float" else "otter-swim"
    let rings = if (now >= st.next.otter) [spawn("ring-s", [o.x + sin(o.h) * 0.4, 0.0, o.z + cos(o.h) * 0.4], 1)] else []
    let joy_splash = if (st.celebrate != null and now - st.celebrate < dt * 1.5) splash([o.x, 0.0, o.z], 9) else [];
    {*: st, otter: {*: o, x: if (wet) nx else o.x, z: if (wet) nz else o.z, h: h, v: if (wet) v else 0.0, roll: roll,
        y: if (floating) -0.075 else -0.085 + 0.01 * sin(now * 3.0), pitch: 0.0},
     plan: if (clip != st.otter_clip) [*st.plan, {stop: st.otter_clip, play: clip}] else st.plan, otter_clip: clip,
     spawns: [*st.spawns, *rings, *joy_splash], next: {*: st.next, otter: if (len(rings) > 0) now + (if (floating) 3.0 else if (v > 0.3) 0.45 else 2.0) else st.next.otter}}
}

// ---- one frame
pub fn step(st, now, dt) any^ {
    // with the strike assist on, a bite is struck a beat after the float goes under
    let assisted = st.assist and st.mode == "bite" and now - st.since > 0.3
    let s0 = {*: st, t: now, frame: st.frame + 1, plan: [], rates: [], spawns: [], press: st.press or assisted}
    let s1 = if (st.press) press(s0, now)^ else s0
    let s2 = rowing_mode(s1, now)^
    let s3 = timeline(s2, now, now - dt)^
    let s4 = {*: s3, boat: row_boat(s3, dt)^, still_since: if (abs(s3.boat.v) > 0.15 or moving(s3.keys)) now else s3.still_since}
    let leaders = [for (f in s4.fish where f.mode == "school") f]
    let leader = if (len(leaders) > 0) leaders[0] else null
    let fishes = [for (f in s4.fish) step_fish(f, s4, leader, now, dt)^]
    let s5 = fishing({*: s4, fish: fishes, rates: [*s4.rates, *swim_rates(s4.fish, fishes)^]}, now)^
    let s6 = rises(boat_rings(s5, now, dt)^, now)^
    let s7 = step_otter(s6, now, dt)^
    let focus = if (line_out(s7.mode) or s7.mode == "cast") bobber_world(s7)^ else null
    let zoom = u.approach(s7.zoom, if (s7.mode == "strike") 0.78 else 1.0, 0.5 * dt);
    {*: s7, zoom: zoom, camera: follow(s7.camera, camera_rig(s7.boat, focus, zoom), u.ease(2.4, dt))}
}
// the hooked fish's pose in the world while the strike plays
pub fn hooked_pose(st, f, now) any^ => (let c = angler.catch_pose(now - st.since, f.L)^, let p = to_world(st.boat, c.p)^,
    {position: p, yaw: st.boat.h + c.yaw, pitch: c.pitch, roll: c.roll})
