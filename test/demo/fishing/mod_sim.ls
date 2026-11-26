// The game as a pure step over scene-clock time: rowing, the fishing sequence (cast, wait,
// nibbles, a bite, the strike and release), fish that wander, school, shy from the boat and
// come to the bobber, a sea otter that keeps the angler company, and a camera that follows. Each
// step also lists what the host must do this frame: clips to switch, fish swim rates to change,
// and ripples and splashes to spawn. Headings are radians about +Y with forward [sin h, cos h].
import t: lambda.scene3d.transform
import u: ~~.midtown.mod_util
import w: .mod_world
import fish: .mod_fish
import angler: .mod_angler
import otter: .mod_otter
import boat: .mod_boat

pub let NO_KEYS = {up: false, down: false, left: false, right: false}
// the camera's orbit round the boat, set by dragging: yaw swings it round, tilt raises or lowers it
pub let NO_ORBIT = {yaw: 0.0, tilt: 0.0}
fn hash(a, b) => (let v = sin(a * 12.9898 + b * 78.233) * 43758.5453, v - floor(v))
// turn toward a heading: proportional near it, capped at `rate`, so the turn rate eases in and out
fn steer(h, target, rate, dt) any^ => (let d = u.wrap(target - h)^, h + u.clamp(d * min(1.0, 3.0 * dt), 0.0 - rate * dt, rate * dt)^)
// boat space to world: the boat sits at (x, y, z) facing h
pub fn to_world(b, p) any^ => (let c = cos(b.h), let sn = sin(b.h), [b.x + p[0] * c + p[2] * sn, b.y + p[1], b.z - p[0] * sn + p[2] * c])
// world to boat space on the water: [across (toward +x, the boat's left), along (forward)]
pub fn to_boat(b, x, z) => (let rx = x - b.x, let rz = z - b.z, [rx * cos(b.h) - rz * sin(b.h), rx * sin(b.h) + rz * cos(b.h)])
fn on_water(p) => [p[0], 0.02, p[2]]
fn spawn(emitter, p, count) => {emitter: emitter, position: on_water(p), count: count}
fn splash(p, size) => [spawn("splash", p, size), spawn("foam", p, 1), spawn(if (size > 12) "ring-l" else "ring-m", p, 1)]

// ---- starting state
fn place_fish(r, i) any^ {
    let school = (fish.schools(r.kind) ^ { false })
    // each school starts together in its own corner of the cove
    let a = (if (school) (if (r.kind == "tang") 2.6 else 0.8) else w.roll(3000 + i, 0)^ * u.TAU), d = if (school) 0.38 else 0.15 + 0.6 * sqrt(w.roll(3000 + i, 1)^)
    // most loners start in view, just past the boat's shy radius and on the open-water side of the jetty (the
    // bank behind it is too shallow), so the cove opens with life around the player
    let near = not school and w.roll(3000 + i, 5)^ < 0.65
    let nd = 2.2 + 3.4 * w.roll(3000 + i, 1)^
    let nx = w.START.x + cos(a) * nd, nz = w.START.z - 0.8 - abs(sin(a)) * nd * 0.8
    let wet = near and (habitat(nx, nz) ^ { false })
    let x = if (wet) nx else cos(a) * w.RX * d + (if (school) (w.roll(3000 + i, 2)^ - 0.5) * 2.0 else 0.0)
    let z = if (wet) nz else sin(a) * w.RZ * d + (if (school) (w.roll(3000 + i, 3)^ - 0.5) * 2.0 else 0.0)
    let sp = fish.SPECIES[r.kind]
    let y = if ((fish.bottom(r.kind) ^ { false })) (w.floor_y(x, z) ^ { -1.5 }) + 0.28 else 0.0 - (sp.depth[0] + sp.depth[1]) / 2.0;
    {id: r.id, i: i, kind: r.kind, L: r.length, x: x, z: z, y: y, h: w.roll(3000 + i, 4)^ * u.TAU,
     v: 0.0, pitch: 0.0, roll: 0.0, mode: if (school) "school" else "wander", goal: [x, -0.6, z], until: 0.0, pace: 0.8, band: 1, cruise: sp.speed}
}
pub fn initial() any^ {
    let b = {x: w.START.x, z: w.START.z, h: w.START.heading, y: 0.0, v: 0.0, turn: 0.0, pitch: 0.0, roll: 0.0, rock: 0.0}
    // the otter starts at its spot on whichever side of the boat is open water
    let left = otter_spot(b, 1.0)
    let side = if ((otter_water(left[0], left[1], false) ^ { false })) 1.0 else -1.0
    let at = otter_spot(b, side)
    let o = {x: at[0], y: otter.FLOAT_Y, z: at[1], h: w.START.heading, v: 0.0, roll: math.pi, pitch: -0.05,
        act: "float", since: 0.0, side: side, y0: otter.FLOAT_Y, next_dive: 9.0, band: 0, escape: 0.0};
    {t: 0.0, frame: 0, boat: b, keys: NO_KEYS, oar_keys: NO_KEYS, press: false, assist: false, mode: "idle", since: 0.0, clip: "idle", after: null,
     fish: [for (i, r in fish.ROSTER) place_fish(r, i)^], target: null, nibbles: 0, next_nibble: 0.0, alone_since: 0.0, cooldown: 0.0,
     otter: o, otter_clip: "otter-float", still_since: 0.0,
     camera: camera_rig(b, null, 1.0)^, orbit: NO_ORBIT, drag: null, zoom: 1.0, catches: 0, best: {}, toast: "", toast_until: 0.0, celebrate: null,
     next: {wake: 0.0, bow: 0.0, hull: 1.0, bob: 0.0, rise: 5.0, ambient: 1.0, otter: 0.0, swirl: 0.0},
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
// the camera looks down on the boat (or between it and the bobber) from behind; dragging orbits it: yaw
// swings it round, tilt raises it toward overhead or lowers it, within limits
pub fn camera_rig(b, focus, zoom, orbit = NO_ORBIT) any^ => (let fx = if (focus == null) b.x else (b.x + focus[0]) / 2.0,
    let fz = if (focus == null) b.z - 0.5 else (b.z + focus[2]) / 2.0,
    let up = 6.6 * zoom + 0.9, let back = 4.4 * zoom, let r = sqrt(up * up + back * back),
    let el = max(0.45, min(1.45, math.atan2(up, back) + orbit.tilt)),
    {position: [fx + sin(orbit.yaw) * r * cos(el), -0.4 + r * sin(el), fz + cos(orbit.yaw) * r * cos(el)], target: [fx, -0.4, fz]})
fn follow(c, want, k) => {position: [for (i in 0 to 2) c.position[i] + (want.position[i] - c.position[i]) * k],
    target: [for (i in 0 to 2) c.target[i] + (want.target[i] - c.target[i]) * k]}

// ---- the angler's state machine; entering a mode switches the boat's clip
fn enter(st, mode, now, after = null) any^ => {*: st, mode: mode, since: now, after: after, clip: mode, plan: [*st.plan, {stop: st.clip, play: mode}]}
pub fn line_out(mode) => contains(["wait", "nibble", "bite"], mode)
fn moving(keys) => keys.up or keys.down or keys.left or keys.right
// the oars out at the sides (resting, rowing, or easing to rest) rather than shipped for fishing
fn oars_out(st) => st.mode == "idle" or st.mode == "row" or st.mode == "rest-oars"
pub fn bobber_world(st) any^ => to_world(st.boat, angler.CAST_SPOT)^
fn engaged(st, f) => st.target == f.id
pub fn hint(st) => if (oars_out(st)) "Arrows or WASD to row · Space to cast"
    else if (st.mode == "wait") "Wait for a bite… Space reels in"
    else if (st.mode == "nibble") "Something's nibbling… wait for the float to go under" else if (st.mode == "bite") "Strike! Press Space"
    else if (st.mode == "strike") (if (st.t - st.since >= angler.LIFTED) "Nice catch!" else "Reeling in!") else if (st.mode == "ready") "Space to cast · arrows to row" else ""
pub fn toggle_assist(st) => {*: st, assist: not st.assist, toast: if (st.assist) "Strike assist off" else "Strike assist on", toast_until: st.t + 1.4}

// a press of Space or a tap: cast, reel in or strike, by what is happening
pub fn press(st, now) any^ {
    let s1 = {*: st, press: false}
    if (oars_out(st)) {
        return if (abs(st.boat.v) > 0.7) {*: s1, toast: "Slow down to cast", toast_until: now + 1.2} else enter(s1, "to-fish", now, "cast")^
    }
    if (st.mode == "ready") { return enter(s1, "cast", now)^ }
    // with a fish nibbling, a press is only too soon: the fish stays and the float still goes under
    // (rowing still reels in), so reacting to a twitch never loses the catch
    let nibbling = st.mode == "nibble" or (st.mode == "wait" and len([for (f in st.fish where engaged(st, f)^ and f.mode == "nibble") f]) > 0)
    if (nibbling) { return {*: s1, toast: "Not yet… wait for the float to go under", toast_until: now + 1.6} }
    if (st.mode == "wait") { return release_target(enter(s1, "reel", now, "ready")^, now, false)^ }
    if (st.mode == "bite" and now - st.since < angler.BITE_WINDOW) { return hook_fish(enter(s1, "strike", now)^, now)^ }
    return s1
}
fn hook_fish(st, now) any^ => {*: st, fish: [for (f in st.fish) if (f.id == st.target) {*: f, mode: "hooked"} else f],
    rates: [*st.rates, {clip: "swim-" ++ st.target, rate: 2.6, weight: 1.0}]}
// the fish of interest swims off: startled when it got away or was reeled past, calmly otherwise
fn release_target(st, now, startled) any^ => if (st.target == null) st else {*: st, target: null, cooldown: now + (if (startled) 2.0 else 1.0), alone_since: now,
    fish: [for (f in st.fish) if (f.id == st.target) {*: f, mode: if (startled) "flee" else "wander", until: now + 2.5, goal: [f.x, -0.9, f.z]} else f]}

// timed transitions within the sequence, and the splashes and rings each moment makes
fn timeline(st, now, before) any^ {
    let x = now - st.since, prev = before - st.since
    let crossed = (at) => (prev < at and x >= at) ^ { false }
    let b = st.boat
    if (st.mode == "stage") { return enter(st, "wait", now)^ }
    if (st.mode == "to-fish" and x >= 0.8) { return enter(st, if (st.after == "cast") "cast" else "ready", now)^ }
    if (st.mode == "to-row" and x >= 0.8) { return enter(st, "idle", now)^ }
    if (st.mode == "rest-oars" and x >= angler.REST_TIME) { return enter(st, "idle", now)^ }
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
    let rowing = oars_out(st)
    // a stroke finishing after the keys are let go still pulls the way it was rowing
    let keys = if (st.mode == "row" and not moving(st.keys)) st.oar_keys else st.keys
    // each drive pushes the boat on and it glides through the recovery (averages about 1)
    let f = (st.t - st.since) / angler.ROW_PERIOD
    let surge = if (st.mode != "row") 1.0 else if (f - floor(f) < angler.DRIVE) 0.55 + 1.6 * sin(math.pi * (f - floor(f)) / angler.DRIVE) else 0.55
    let thrust = if (not rowing) 0.0 else ((if (keys.up) 1.0 else 0.0) - (if (keys.down) 0.6 else 0.0)) * surge
    let turn_in = if (not rowing) 0.0 else (if (keys.left) 1.0 else 0.0) - (if (keys.right) 1.0 else 0.0)
    let v = b.v + (thrust * 1.4 - b.v * (if (rowing) 0.85 else 2.2)) * dt
    let turn = u.approach(b.turn, turn_in * 0.85, 2.6 * dt)
    let h = b.h + turn * dt
    let nx = b.x + sin(h) * v * dt, nz = b.z + cos(h) * v * dt
    // the whole hull and the oars' reach must stay clear at the new pose: a move that would put any of it
    // aground, on a rock or into the jetty is refused and the boat bumps back a little. A turn on the spot
    // may still swing clear, and a boat already caught (it never should be) may move to get out
    let rocks = [for (r in w.OBSTACLES where abs(r.x - b.x) < r.r + 3.0 and abs(r.z - b.z) < r.r + 3.0) r]
    let free = hull_clear(b, nx, nz, h, rocks)^
    let turnable = free or hull_clear(b, b.x, b.z, h, rocks)^
    let blocked = not free and hull_clear(b, b.x, b.z, b.h, rocks)^
    let held = blocked and not turnable
    let rock = max(0.0, b.rock - dt * 0.6);
    {*: b, x: if (blocked) b.x else nx, z: if (blocked) b.z else nz, h: if (held) b.h else h, v: if (blocked) -0.25 * v else v, turn: if (held) 0.0 else turn,
     rock: if (blocked and abs(v) > 0.4) 0.7 else rock,
     y: 0.012 * sin(st.t * 1.7) + 0.03 * rock * sin(st.t * 9.0), pitch: -0.025 * (thrust - v * 0.3) + 0.04 * rock * sin(st.t * 7.0 + 1.0),
     roll: 0.015 * sin(st.t * 1.3) + 0.06 * turn * v + 0.06 * rock * sin(st.t * 8.0)}
}
// where the oar blades reach through a stroke (boat space [across, along]): the catch, mid-drive and the finish
let OAR_REACH = [for (k in [0.0, 0.5, 1.0], p in (angler.blades(angler.row(k * angler.DRIVE)) ^ { [] })) [p[0], p[2]]]
// the hull at a pose: every point of its outline on water deeper than its draft, clear of the emergent rocks
// near it and of the jetty; and the blades clear of the jetty too (sampled at their centres, so with room
// for their outer end), since they reach 1.3 m out and a boat alongside it would row through its deck
fn hull_clear(b, x, z, h, rocks) any^ => (let pose = {*: b, x: x, z: z, h: h},
    all([for (p in boat.PLANFORM, let q = to_world(pose, [p[0], 0.0, p[1]])^) (w.navigable(q[0], q[2]) ^ { false })
        and not (w.by_jetty(q[0], q[2], 0.12) ^ { true }) and not any([for (r in rocks) (u.dist(q[0], q[2], r.x, r.z) ^ { 0.0 }) < r.r + 0.25])])
    and not any([for (p in OAR_REACH, let q = to_world(pose, [p[0], 0.0, p[1]])^) w.by_jetty(q[0], q[2], 0.4) ^ { true }]))
// letting go of the keys finishes the stroke under way, then the oars ease to rest; rowing again picks up at once
// (`before` is the last frame's time: a slow frame steps further than the clamped dt and must not skip the stroke's end)
fn stroke_done(st, now, before) => floor((now - st.since) / angler.ROW_PERIOD) > floor((max(before, st.since) - st.since) / angler.ROW_PERIOD)
fn rowing_mode(st, now, before) any^ => if ((st.mode == "idle" or st.mode == "rest-oars") and moving(st.keys)) enter(st, "row", now)^
    else if (st.mode == "row" and not moving(st.keys) and stroke_done(st, now, before)) enter(st, "rest-oars", now)^
    else if (st.mode == "ready" and moving(st.keys)) enter(st, "to-row", now)^
    else if (line_out(st.mode) and moving(st.keys)) release_target(enter(st, "reel", now, "row")^, now, st.mode == "bite")^
    else st
fn bite(p) any^ => [spawn("ring-m", p, 1), spawn("splash", p, 3)]
fn lift_out(p) any^ => [spawn("splash", p, 5), spawn("ring-s", p, 1)]
// rings from the oar blades as they bite and leave the water, a wake while under way, and a lap at the hull at rest
fn boat_rings(st, now, dt) any^ {
    let b = st.boat
    let phase = if (st.mode == "row") (now - st.since) / angler.ROW_PERIOD else 0.0
    let prev = phase - dt / angler.ROW_PERIOD
    let entry = st.mode == "row" and floor(phase - 0.02) != floor(prev - 0.02)
    let exit = st.mode == "row" and floor(phase - angler.DRIVE) != floor(prev - angler.DRIVE)
    let blades = [for (p in angler.blades(angler.row(phase))^) to_world(b, p)^]
    // the blades bite with a ring, a foam crown and a few drops, trail swirls as they pull through, and
    // lift out at the finish with a splash
    let pulling = st.mode == "row" and phase - floor(phase) > 0.06 and phase - floor(phase) < angler.DRIVE - 0.04
    let strokes = if (entry) [for (p in blades, e in bite(p)^) e] else if (exit) [for (p in blades, e in lift_out(p)^) e] else []
    let swirls = if (pulling and now >= st.next.swirl) [for (p in blades) spawn("ring-s", p, 1)] else []
    // the hull's own ripples are ovals along the boat, centred toward the stern
    let wake = if (abs(b.v) > 0.35 and now >= st.next.wake) [spawn("wake-ring", to_world(b, [0.0, 0.0, -1.0])^, 1)] else []
    let bow = if (abs(b.v) > 0.6 and now >= st.next.bow) [spawn("ring-s", to_world(b, [0.0, 0.0, 1.2])^, 1)] else []
    let hull = if (abs(b.v) < 0.2 and now >= st.next.hull) [spawn("hull-ring", to_world(b, [0.0, 0.0, -0.55])^, 1)] else []
    let bob = if (st.mode == "wait" and now >= st.next.bob) [spawn("ring-s", bobber_world(st)^, 1)] else [];
    {*: st, spawns: [*st.spawns, *strokes, *swirls, *wake, *bow, *hull, *bob],
     next: {*: st.next, swirl: if (len(swirls) > 0) now + 0.3 else st.next.swirl, wake: if (len(wake) > 0) now + 0.65 else st.next.wake, bow: if (len(bow) > 0) now + 0.6 else st.next.bow,
        hull: if (len(hull) > 0) now + 3.3 else st.next.hull, bob: if (len(bob) > 0) now + 2.7 else st.next.bob}}
}

// ---- fish
fn habitat(x, z) any^ => (w.depth(x, z) ^ { 0.0 }) > 0.5 and (w.shore(x, z) ^ { 2.0 }) < 0.88
// where a wandering fish heads next: usually a spot just outside the boat's shy radius (fish are curious,
// and the cove's life stays where the player is looking), otherwise somewhere near itself
fn wander_goal(f, b, now) any^ => (let a = hash(f.i * 7.1, now) * u.TAU, let near_boat = hash(f.i * 5.3, now + 0.9) < 0.72,
    let d = if (near_boat) 2.2 + 3.4 * hash(f.i * 3.3, now + 1.7) else 2.0 + 5.0 * hash(f.i * 3.3, now + 1.7),
    let cx = if (near_boat) b.x else f.x, let cz = if (near_boat) b.z else f.z,
    let x = cx + cos(a) * d, let z = cz + sin(a) * d, let sp = fish.SPECIES[f.kind],
    let deep = sp.depth[0] + (sp.depth[1] - sp.depth[0]) * hash(f.i, now + 4.2),
    // a spot that lands in the shallows or on the bank swings to the same distance on the cove side, so a
    // boat moored near the beach still draws fish instead of sending them back to the middle
    let n = max(0.5, sqrt(cx * cx + cz * cz)), let x2 = cx - cx / n * d + sin(a) * 0.8, let z2 = cz - cz / n * d + cos(a) * 0.8,
    let ok = habitat(x, z)^, let ok2 = habitat(x2, z2)^,
    let gx = if (ok) x else if (ok2) x2 else f.x * 0.7, let gz = if (ok) z else if (ok2) z2 else f.z * 0.7,
    // rays glide a hand's breadth above the floor; the rest keep to their depth band
    [gx, if ((fish.bottom(f.kind) ^ { false })) (w.floor_y(gx, gz) ^ { -1.5 }) + 0.25 else 0.0 - min(deep, max(0.25, (w.depth(x, z) ^ { 1.0 }) - 0.18)), gz])
fn avoid(f, h) any^ => (let close = [for (r in w.OBSTACLES where (u.dist(f.x + sin(h) * 0.9, f.z + cos(h) * 0.9, r.x, r.z) ^ { 1000.0 }) < r.r + 0.5) r],
    if (len(close) == 0) h else (let r = close[0], let away = math.atan2(f.x - r.x, f.z - r.z), h + u.wrap(away - h) * 0.6))
// what a fish wants this frame: a point to swim to and a speed
fn intent(f, st, leader, now) any^ {
    let sp = fish.SPECIES[f.kind]
    let b = st.boat
    let bob = bobber_world(st)^
    if (f.mode == "approach") {
        let a = math.atan2(f.x - bob[0], f.z - bob[2])
        // it hurries over and only settles to a cautious pace in the last stretch
        let far = (u.dist(f.x, f.z, bob[0], bob[2]) ^ { 0.0 }) > 1.5
        return {goal: [bob[0] + sin(a) * 0.3, -0.22, bob[2] + cos(a) * 0.3], speed: sp.speed * (if (far) 2.0 else 0.9)}
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
fn first_schooling(all, kind) => (let found = [for (f in all where f.mode == "school" and f.kind == kind) f], if (len(found) > 0) found[0] else null)
fn step_fish(f, st, leader, now, dt) any^ {
    if (f.mode == "hooked") { return f }
    let sp = fish.SPECIES[f.kind]
    let b = st.boat
    let near_boat = u.dist(f.x, f.z, b.x, b.z) < 1.9
    let calm = f.mode == "wander" or f.mode == "school" or f.mode == "rise"
    // a startled schooler rejoins its school afterwards; otherwise every pass of the boat thinned the schools for good
    let settled = if ((fish.schools(f.kind) ^ { false })) "school" else "wander"
    let mode = if (calm and near_boat and st.target != f.id) "flee" else if (f.mode == "flee" and now > f.until) settled else f.mode
    let f1 = if (mode != f.mode) {*: f, mode: mode, until: now + 2.0} else f
    let arrived = u.dist(f1.x, f1.z, f1.goal[0], f1.goal[2]) < 0.6
    let leading = f1.mode == "school" and leader != null and leader.id == f1.id
    let f2 = if ((f1.mode == "wander" or leading) and (arrived or now > f1.until)) {*: f1, goal: wander_goal(f1, st.boat, now)^, until: now + 5.0 + 6.0 * hash(f1.i, now),
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
    let climb = (if (f2.mode == "rise") 3.0 else 1.0) * 0.25 * dt
    let y = u.clamp(f2.y + u.clamp((want.goal[1] - f2.y) * min(1.0, 1.5 * dt), 0.0 - climb, climb), floor_y + 0.12, -0.05)
    // the body tilts with its climb and banks into turns, eased so neither snaps between frames
    let turning = u.wrap(h - f2.h) / max(dt, 0.001)
    let pitch_goal = u.clamp(0.0 - math.atan2((y - f2.y) / max(dt, 0.001), max(v, 0.15)), -0.35, 0.35)
    let roll_goal = u.clamp(0.0 - turning * 0.1, -0.3, 0.3);
    {*: f2, x: if (ok) nx else f2.x, z: if (ok) nz else f2.z, y: y, h: if (ok) h else h + 1.2 * dt, v: if (ok) v else v * 0.5,
     pitch: f2.pitch + (pitch_goal - f2.pitch) * u.ease(4.0, dt), roll: f2.roll + (roll_goal - f2.roll) * u.ease(3.0, dt),
     goal: if (ok) f2.goal else [f2.x * 0.6, f2.goal[1], f2.z * 0.6], band: speed_band(f2.band, v, sp.speed)}
}
// a tail-beat that suits the speed: sculling at a hover, quick strokes in a dash
// speed bands with hysteresis, so a fish cruising near a threshold does not flip its tail-beat every frame
fn speed_band(was, v, cruise) => if (was == 0) (if (v > 0.18) 1 else 0) else if (was == 2) (if (v < cruise * 1.3) 1 else 2)
    else (if (v < 0.1) 0 else if (v > cruise * 1.6) 2 else 1)
fn swim_rates(before, after) any^ => [for (i, f in after where f.band != before[i].band and f.mode != "hooked")
    {clip: "swim-" ++ f.id, rate: [0.55, 1.0, 2.2][f.band], weight: [0.45, 1.0, 1.0][f.band]}]

// the bobber draws one fish at a time: it swims up, nibbles once or twice, then takes it
fn fishing(st, now) any^ {
    if (not line_out(st.mode)) { return st }
    let bob = bobber_world(st)^
    if (st.target == null) {
        if (st.mode != "wait" or now < st.cooldown) { return st }
        // schools and rays pass by; only the biting species take the hook
        let close = [for (f in st.fish where f.mode == "wander" and (fish.bites(f.kind) ^ { false }) and (u.dist(f.x, f.z, bob[0], bob[2]) ^ { 1000.0 }) < 6.5) f]
        // a lone bobber draws the nearest fish over after a couple of seconds; until then any fish close by may wander up
        let lured = now - st.alone_since > 2.5
        let pool = if (len(close) > 0 and not lured) close else [for (f in st.fish where f.mode == "wander" and (fish.bites(f.kind) ^ { false })) f]
        if (len(pool) == 0 or (not lured and hash(st.frame, 3.0) > 0.05)) { return st }
        let ranked = [for (f in pool) {f: f, d: u.dist(f.x, f.z, bob[0], bob[2]) ^ { 1000.0 }}]
        let pick = if (lured) reduce(ranked, (a, r) => if ((r.d < a.d) ^ { false }) r else a).f else pool[int(floor(hash(st.frame, 7.0) * len(pool)))]
        return {*: st, target: pick.id, fish: [for (f in st.fish) if (f.id == pick.id) {*: f, mode: "approach"} else f], nibbles: 1 + int(floor(hash(st.frame, 9.0) * 1.5))}
    }
    let mine = [for (f in st.fish where f.id == st.target) f]
    if (len(mine) == 0) { return {*: st, target: null} }
    let f = mine[0]
    if (f.mode == "approach" and u.dist(f.x, f.z, bob[0], bob[2]) < 0.5) {
        return {*: st, fish: [for (g in st.fish) if (g.id == f.id) {*: g, mode: "nibble"} else g], next_nibble: now + 0.5 + hash(st.frame, 1.0) * 0.7}
    }
    if (f.mode == "nibble" and st.mode == "wait" and now >= st.next_nibble) {
        if (st.nibbles > 0) { return {*: enter(st, "nibble", now)^, nibbles: st.nibbles - 1, next_nibble: now + angler.NIBBLE_TIME + 0.4 + hash(st.frame, 2.0) * 0.8} }
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

// ---- the otter: floats on its back beside the boat with a clam on its chest and paddles on its back
// to keep up. Now and then, and whenever the boat rows hard, it rolls over and dives, swims under the
// boat (to its other side when idle) and surfaces, rolling onto its back again. A catch earns a happy roll.
pub let SPIN_TIME = 1.0
// how near the otter's root may come to an oarlock while the oars work: a blade's reach, the otter's own
// reach, and a little water between
let OAR_CLEAR = boat.OAR_OUT + otter.REACH + 0.25
// beside the stern quarter, or beside the bow quarter while the boat backs up (out of its way); both about
// a quarter metre outside OAR_CLEAR, so a blade sweeping through its stroke passes well clear
fn otter_spot(b, side) => (let back = if (b.v < -0.12) -1.6 else 1.4,
    [b.x + side * cos(b.h) * 2.3 - sin(b.h) * back, b.z - side * sin(b.h) * 2.3 - cos(b.h) * back])
// the boat bearing down on the otter: in its path (forward or astern) within a couple of seconds' travel.
// Returns the side to escape to (1 or -1, the boat's left or right), or 0 when it is clear
fn bearing_down(o, b) => (let p = to_boat(b, o.x, o.z), let ahead = if (b.v < 0.0) 0.0 - p[1] else p[1],
    if (abs(b.v) > 0.15 and abs(p[0]) < 1.25 and ahead > -1.6 and ahead < 1.6 + 2.0 * abs(b.v)) (if (p[0] >= 0.0) 1.0 else -1.0) else 0.0)
// the otter inside the reach of the oar on its side with the boat lying at `at`: the side to paddle clear
// to, or 0. Once making off it goes a little further, so the way back to its spot does not graze the reach again
fn oar_reach(o, at) => (let p = to_boat(at, o.x, o.z), let side = if (p[0] >= 0.0) 1.0 else -1.0,
    let dx = p[0] - side * boat.OARLOCK[0], let dz = p[1] - boat.OARLOCK[2],
    let limit = if (o.escape == side) OAR_CLEAR + 0.4 else OAR_CLEAR, if (dx * dx + dz * dz < limit * limit) side else 0.0)
// it reacts to where the oars are and to where they will be a moment on: a turn swings an oar round faster
// than the otter drifts, and backing up carries the oars down on it as it paddles for the bow quarter
let OAR_LOOK = 0.7
fn in_oars(o, b) => (let here = oar_reach(o, b), if (here != 0.0) here
    else oar_reach(o, {*: b, x: b.x + sin(b.h) * b.v * OAR_LOOK, z: b.z + cos(b.h) * b.v * OAR_LOOK, h: b.h + b.turn * OAR_LOOK}))
let OTTER_CLIPS = {float: "otter-float", spin: "otter-float", paddle: "otter-paddle", swim: "otter-swim", dive: "otter-dive",
    surface: "otter-surface", ['to-paddle']: "otter-to-paddle", ['to-float']: "otter-to-float"}
fn crossed(k0, k1, mark) => k0 < mark and k1 >= mark
// water the otter can be in: deep enough for what it is doing, and on the surface its body clear of the jetty
fn otter_water(x, z, under) any^ => (w.shore(x, z) ^ { 2.0 }) < 0.97 and (w.depth(x, z) ^ { 0.0 }) > (if (under) 0.6 else 0.3)
    and (under or not (w.by_jetty(x, z, 0.7) ^ { true }))
// the boat lies across the straight way from the otter to its spot (it goes under, never through the hull)
fn across_boat(o, b, spot) => (let ax = spot[0] - o.x, let az = spot[1] - o.z, let len2 = max(0.0001, ax * ax + az * az),
    let k = max(0.0, min(1.0, ((b.x - o.x) * ax + (b.z - o.z) * az) / len2)),
    let dx = o.x + ax * k - b.x, let dz = o.z + az * k - b.z, len2 > 0.64 and dx * dx + dz * dz < 1.44)
// what the otter does next, from how long it has been at it and where the boat is
fn otter_act(o, st, now, dt, dist, danger) any^ {
    let t = now - o.since
    // only rowing hard ahead sends it under; backing up it paddles out of the way
    let fast = st.boat.v > 0.9
    let cheer = st.celebrate != null and now - st.celebrate < dt * 1.5
    if (o.act == "dive") { return if (t >= otter.DIVE_TIME) "swim" else "dive" }
    if (o.act == "surface") { return if (t >= otter.SURFACE_TIME) "float" else "surface" }
    if (o.act == "spin") { return if (t >= SPIN_TIME) "float" else "spin" }
    if (o.act == "to-paddle") { return if (t >= otter.TO_PADDLE_TIME) "paddle" else "to-paddle" }
    if (o.act == "to-float") { return if (t >= otter.TO_FLOAT_TIME) "float" else "to-float" }
    if (o.act == "swim") {
        // surface on a stroke boundary, where the swim pose matches the surfacing clip's first pose
        let stroke = floor(t / otter.SWIM_PERIOD) > floor((t - dt) / otter.SWIM_PERIOD)
        return if (stroke and ((t > 2.5 and dist < 1.0 and not fast) or t > 10.0)) "surface" else "swim"
    }
    let blocked = across_boat(o, st.boat, otter_spot(st.boat, o.side))
    if (o.act == "paddle") { return if (danger) "paddle" else if (dist < 0.6 or fast or blocked) "to-float" else "paddle" }
    return if (danger) "to-paddle" else if (cheer) "spin" else if (fast or blocked or (now > o.next_dive and dist < 2.0)) "dive" else if (dist > 1.1) "to-paddle" else "float"
}
fn step_otter(st, now, dt) any^ {
    let o1 = st.otter, b = st.boat
    // keep to the boat's other side while this one is shallows or jetty
    let near_spot = otter_spot(b, o1.side), far_spot = otter_spot(b, 0.0 - o1.side)
    let o0 = if (not (otter_water(near_spot[0], near_spot[1], false)^) and otter_water(far_spot[0], far_spot[1], false)^) {*: o1, side: 0.0 - o1.side} else o1
    let spot0 = otter_spot(b, o0.side)
    // afloat, it gets out of the boat's path, and out of an oar's reach while the oars are out
    let free = o0.act == "float" or o0.act == "paddle" or o0.act == "to-paddle"
    let threat = if (free) bearing_down(o0, b) else 0.0
    let oars = if (threat == 0.0 and free and oars_out(st)) in_oars(o0, b) else 0.0
    let escape = if (threat != 0.0) threat else oars
    let act = otter_act(o0, st, now, dt, u.dist(o0.x, o0.z, spot0[0], spot0[1]) ^ { 0.0 }, escape != 0.0)^
    let fresh = act != o0.act
    // an idle dive heads under the boat for its other side; an escape keeps to the side it gets clear on
    let side = if (escape != 0.0) escape else if (fresh and act == "dive" and abs(b.v) <= 0.9 and not across_boat(o0, b, spot0)) 0.0 - o0.side else o0.side
    let o = if (fresh) {*: o0, act: act, since: now, side: side, y0: o0.y,
        next_dive: if (act == "float" and o0.act == "surface") now + 14.0 + 14.0 * hash(now, 7.0) else o0.next_dive} else o0
    let t = now - o.since
    // escaping the hull, it makes for a point well out to the side; escaping an oar, straight out from the
    // boat's middle, the one way that gets clear of an oar swinging round in a turn; otherwise its spot by the boat
    let out = max(0.01, sqrt((o.x - b.x) ** 2 + (o.z - b.z) ** 2))
    let spot = if (threat != 0.0) [o.x + escape * cos(b.h) * 1.8, o.z - escape * sin(b.h) * 1.8]
        else if (oars != 0.0) [o.x + (o.x - b.x) / out * 1.8, o.z + (o.z - b.z) / out * 1.8] else otter_spot(b, o.side)
    let dx = spot[0] - o.x, dz = spot[1] - o.z
    let dist = sqrt(dx * dx + dz * dz)
    let toward = math.atan2(dx, dz)
    let under = act == "swim" or act == "dive"
    let fast = abs(b.v) > 0.9
    let speed = if (act == "paddle") (if (escape != 0.0) 1.4 else min(1.3, 0.4 + dist * 0.8)) else if (act == "to-paddle") (if (escape != 0.0) 0.7 else 0.35)
        else if (act == "dive") 0.5
        else if (act == "swim") (if (fast) max(1.0, abs(b.v) * 1.15) else min(1.0, 0.3 + dist * 0.6)) else if (act == "surface") 0.3 else 0.0
    let turning = act == "paddle" or act == "to-paddle" or under
    let h = if (turning) steer(o.h, toward, if (under) 2.5 else 3.5, dt)^ else o.h
    let v = u.approach(o.v, speed, 2.6 * dt)
    // floating drifts gently toward the spot without turning
    let drift = if (act == "float" and dist > 0.05) min(0.2, dist * 0.3) * dt / dist else 0.0
    let nx = o.x + sin(h) * v * dt + dx * drift, nz = o.z + cos(h) * v * dt + dz * drift
    let floor = w.floor_y(nx, nz) ^ { -2.0 }
    let wet = otter_water(nx, nz, under)^
    let k = t / (if (act == "dive") otter.DIVE_TIME else otter.SURFACE_TIME), k0 = (t - dt) / (if (act == "dive") otter.DIVE_TIME else otter.SURFACE_TIME)
    let dive = otter.dive_root(k), rise = otter.surface_root(k, o.y0)
    let swim_y = max(otter.SWIM_Y, floor + 0.2)
    let y = if (act == "dive") dive.y else if (act == "surface") rise.y else if (act == "swim") o.y + (swim_y - o.y) * u.ease(2.0, dt)
        else otter.FLOAT_Y + 0.012 * sin(now * 1.7)
    let pitch = if (act == "dive") dive.pitch else if (act == "surface") rise.pitch
        else if (act == "swim") o.pitch + (u.clamp((o.y - swim_y) * 3.0, -0.5, 0.5) - o.pitch) * u.ease(3.0, dt) else -0.05
    let roll = if (act == "dive") dive.roll else if (act == "surface") rise.roll else if (act == "swim") 0.0
        else if (act == "spin") math.pi + u.TAU * (t / SPIN_TIME) * (t / SPIN_TIME) * (3.0 - 2.0 * t / SPIN_TIME)
        else math.pi + (if (act == "paddle") 0.07 * sin(now * u.TAU / otter.PADDLE_PERIOD) else 0.05 * sin(now * 0.9))
    let ahead = [nx + sin(h) * 0.35, 0.0, nz + cos(h) * 0.35], behind = [nx - sin(h) * 0.5, 0.0, nz - cos(h) * 0.5], centre = [nx, 0.0, nz]
    // rings while it floats or paddles, bubbles over it under water, splashes as it dives, surfaces and rolls
    let tick = now >= st.next.otter
    let rings = if (not tick) [] else if (act == "paddle") [spawn("ring-s", behind, 1)] else if (act == "swim" and y > -0.7) [spawn("ring-s", centre, 1)]
        else if (act == "float" or act == "spin") [spawn("ring-s", centre, 1)] else []
    let bursts = if (act == "dive" and crossed(k0, k, 0.2)) [spawn("ring-m", centre, 1)] else if (act == "dive" and crossed(k0, k, 0.62)) splash(behind, 8)
        else if (act == "surface" and crossed(k0, k, 0.35)) [spawn("ring-m", ahead, 1), spawn("foam", ahead, 1)]
        else if (act == "spin" and crossed((t - dt) / SPIN_TIME, t / SPIN_TIME, 0.45)) splash(centre, 12) else []
    let clip = OTTER_CLIPS[act]
    // a fast paddle kicks quicker
    let band = if (act == "paddle" and v > 0.7) 1 else 0
    let tempo = if (act == "paddle" and (band != o.band or clip != st.otter_clip)) [{clip: "otter-paddle", rate: [1.0, 1.7][band], weight: 1.0}] else [];
    {*: st, otter: {*: o, x: if (wet) nx else o.x, z: if (wet) nz else o.z, h: h, v: if (wet) v else 0.0, y: y, pitch: pitch, roll: roll, band: band,
        escape: escape},
     plan: if (clip != st.otter_clip) [*st.plan, {stop: st.otter_clip, play: clip}] else st.plan, otter_clip: clip, rates: [*st.rates, *tempo],
     spawns: [*st.spawns, *rings, *bursts],
     next: {*: st.next, otter: if (len(rings) > 0) now + (if (act == "paddle") 0.4 else if (act == "swim") 1.1 else 2.6) else st.next.otter}}
}

// ---- one frame
pub fn step(st, now, dt) any^ {
    // with the strike assist on, a bite is struck a beat after the float goes under
    let assisted = st.assist and st.mode == "bite" and now - st.since > 0.3
    let s0 = {*: st, t: now, frame: st.frame + 1, plan: [], rates: [], spawns: [], press: st.press or assisted}
    let s1 = if (st.press) press(s0, now)^ else s0
    let s2 = rowing_mode(s1, now, st.t)^
    let s3 = timeline(s2, now, now - dt)^
    let s4 = {*: s3, boat: row_boat(s3, dt)^, still_since: if (abs(s3.boat.v) > 0.15 or moving(s3.keys)) now else s3.still_since,
        oar_keys: if (s3.mode == "row" and moving(s3.keys)) s3.keys else s3.oar_keys}
    // each school follows its first member still schooling
    let leads = {minnow: first_schooling(s4.fish, "minnow"), tang: first_schooling(s4.fish, "tang")}
    let fishes = [for (f in s4.fish) step_fish(f, s4, if (f.kind == "minnow" or f.kind == "tang") leads[f.kind] else null, now, dt)^]
    let s5 = fishing({*: s4, fish: fishes, rates: [*s4.rates, *swim_rates(s4.fish, fishes)^]}, now)^
    let s6 = rises(boat_rings(s5, now, dt)^, now)^
    let s7 = step_otter(s6, now, dt)^
    let focus = if (line_out(s7.mode) or s7.mode == "cast") bobber_world(s7)^ else null
    let zoom = u.approach(s7.zoom, if (s7.mode == "strike") 0.78 else 1.0, 0.5 * dt);
    {*: s7, zoom: zoom, camera: follow(s7.camera, camera_rig(s7.boat, focus, zoom, s7.orbit)^, u.ease(2.4, dt))}
}
// the hooked fish's pose in the world while the strike plays
pub fn hooked_pose(st, f, now) any^ => (let c = angler.catch_pose(now - st.since, f.L)^, let p = to_world(st.boat, c.p)^,
    {position: p, yaw: st.boat.h + c.yaw, pitch: c.pitch, roll: c.roll})
