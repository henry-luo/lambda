// One frame of Midtown. advance() is the pure simulation step (car, traffic, people, courier,
// camera); sync() hands its result to the host: pose targets for every moving body in one physics
// command, camera attributes, and HUD changes through the presentation channel. Bodies apply their
// targets on the next physics tick, so the camera follows the pose sent last frame, the one on screen.
import dom
import runtime: lambda.scene3d.runtime
import ps: lambda.physics.scene
import scene_dom: lambda.scene3d.dom
import t: lambda.scene3d.transform
import numbers: ~~.mod_number_text
import u: .mod_util
import city: .mod_city
import v: .mod_vehicle
import tr: .mod_traffic
import pd: .mod_peds
import ms: .mod_mission
import cam: .mod_camera
import sc: .mod_scene
import hud: .mod_hud

pub let NO_KEYS = {up: false, down: false, left: false, right: false, brake: false, back: false}
pub let START = {x: city.road(3) - city.LANE, z: city.road(1) + 26.0, heading: 0.0}
pub fn initial(start = START) {
    let car = v.spawn(start.x, start.z, start.heading);
    {token: 0, last: null, t: 0.0, started: false, paused: false, help: false, handles: null, keys: NO_KEYS, touch: NO_KEYS,
     car: car, traffic: tr.spawn(16), peds: pd.spawn(24), mission: ms.initial(), camera: cam.initial(car),
     look: "day", lights: false, auto: null, shown: {speed: "   ", timer: "   ", gear: "N", place: "", street: "", look: "", mission: "", cash: ""},
     toast: "", toast_until: 0.0, smashed: [], best_air: 0.0, top_speed: 0.0, drag: null, tour: 0.0, frames: 0}
}

// ---- controls
fn pressed(st, key) => st.keys[key] or st.touch[key]
pub fn controls(st) => {throttle: if (pressed(st, "up")) 1.0 else 0.0, brake: if (pressed(st, "down")) 1.0 else 0.0,
    steer: (if (pressed(st, "left")) 1.0 else 0.0) - (if (pressed(st, "right")) 1.0 else 0.0), handbrake: pressed(st, "brake")}
pub fn set_key(keys, key, down) => {*: keys, up: if (key == "up") down else keys.up, down: if (key == "down") down else keys.down,
    left: if (key == "left") down else keys.left, right: if (key == "right") down else keys.right,
    brake: if (key == "brake") down else keys.brake, back: if (key == "back") down else keys.back}
pub fn key_of(key) => if (key == "ArrowUp" or key == "w" or key == "W") "up" else if (key == "ArrowDown" or key == "s" or key == "S") "down"
    else if (key == "ArrowLeft" or key == "a" or key == "A") "left" else if (key == "ArrowRight" or key == "d" or key == "D") "right"
    else if (key == " ") "brake" else if (key == "c" or key == "C") "back" else null

// ---- the car on a lane: Reset parks it there; Auto drive moves it along the lanes like traffic
fn on_lane(car, a) => {*: car, x: a.x, z: a.z, y: city.ground(a.x, a.z), heading: a.heading, vx: sin(a.heading) * a.speed,
    vz: cos(a.heading) * a.speed, vy: 0.0, grounded: true, pitch: 0.0, roll: 0.0}
pub fn reset_car(st) => (let a = tr.nearest_run(st.car.x, st.car.z, st.car.heading),
    {*: st, car: {*: on_lane(st.car, a), steer: 0.0, damage: 0.0, gear: 1, rpm: v.SPEC.idle_rpm}, auto: null})
// Auto drive keeps the car's speed and hands it to the nearest lane
pub fn engage_auto(st) => (let a = tr.nearest_run(st.car.x, st.car.z, st.car.heading, v.forward_speed(st.car)),
    {*: st, car: on_lane(st.car, a), auto: a})
fn auto_drive(car, before, after, dt) {
    let moved = on_lane(car, after)
    let yaw = u.wrap(after.heading - before.heading) / max(dt, 0.001)
    let steer = if (after.speed > 1.0) math.atan(yaw * v.SPEC.wheelbase / after.speed) else 0.0;
    {*: moved, steer: u.lerp(car.steer, u.clamp(steer, -0.6, 0.6), u.ease(10.0, dt)), spin: u.wrap(car.spin + after.speed / v.SPEC.wheel_r * dt),
     odometer: car.odometer + after.speed * dt, rpm: u.lerp(car.rpm, v.SPEC.idle_rpm + after.speed * 210.0, u.ease(6.0, dt)),
     gear: if (after.speed < 12.0) 1 else 2, impact: 0.0, landed: 0.0, airtime: 0.0, hits: [], accel: 0.0}
}
fn player_record(car) => {id: "player", player: true, x: car.x, z: car.z, heading: car.heading, hl: v.HALF_LENGTH, hw: v.HALF_WIDTH}

// ---- the pure step
fn say(st, text, t, seconds = 2.6) => {*: st, toast: text, toast_until: t + seconds}
pub fn advance(st, dt, t) {
    if (st.paused or dt <= 0.0) {*: st, t: t}
    else {
        let drive = st.started and st.auto == null
        let auto = if (st.auto == null) null else tr.step_car(st.auto, st.traffic, t, dt, false)
        let car = if (auto != null) auto_drive(st.car, st.auto, auto, dt) else v.step(st.car, if (drive) controls(st) else v.IDLE, dt, st.traffic)
        let traffic = tr.step(st.traffic, player_record(car), t, dt, car.hits)
        let peds = pd.step(st.peds, car, dt)
        let mission = if (st.started) ms.step(st.mission, car, t) else st.mission
        let jolt = max(car.impact, car.landed * 0.8)
        let rig = if (jolt > 5.0) cam.jolt(st.camera, jolt) else st.camera
        // the intro tours the skyline until the driver takes over
        let tour = st.tour + dt * 0.05
        // the camera body and the car body both apply their poses on the next tick, so the rig follows the new car
        let camera = if (st.started) cam.follow({*: rig, look_back: st.keys.back}, car, dt)
            else {*: rig, position: [sin(tour) * 230.0, 105.0, 0.0 - cos(tour) * 230.0], target: [0.0, 20.0, 0.0], fov: 52.0}
        let speed = v.speed(car)
        let next = {*: st, t: t, car: car, auto: auto, traffic: traffic, peds: peds, mission: mission, camera: camera, tour: tour,
            top_speed: max(st.top_speed, speed), frames: st.frames + 1}
        let air = car.airtime > 0.6
        let crash = car.impact > 9.0
        if (mission.message != null) say(next, mission.message, t, 3.5)
        else if (air) say({*: next, best_air: max(st.best_air, car.airtime)}, "Airtime " ++ numbers.fixed(car.airtime, 1) ++ " s" ++
            (if (car.airtime > st.best_air) "  · new best" else ""), t)
        else if (crash) say(next, "Crunch! Damage " ++ string(int(car.damage) ^ { 0 }) ++ "%", t, 1.8)
        else next
    }
}

// ---- host side: pose targets, camera, HUD
fn target(body, position, q) => {type: "target", body: body, position: position, quaternion: q}
fn spin_q(a) => [sin(a / 2.0), 0.0, 0.0, cos(a / 2.0)]
fn wheel_targets(car) {
    let q = u.quat(car.heading, car.pitch, car.roll);
    [for (k, w in v.WHEELS, let off = t.quaternion_rotate(q, [w.x, v.SPEC.wheel_r, w.z]))
        target(sc.WHEEL_IDS[k], [car.x + off[0], car.y + off[1], car.z + off[2]],
            t.quaternion_multiply(t.quaternion_multiply(q, u.quat(if (w.front) car.steer else 0.0)), spin_q(car.spin)))]
}
fn beacon_target(kind, m, t) {
    let active = m.target != null and ((kind == "pickup" and m.phase == "pickup") or (kind == "drop" and m.phase == "deliver"));
    target("beacon-" ++ kind, if (active) [m.target.x, city.CURB, m.target.z] else [0.0, sc.PARKED_Y, 0.0], u.quat(t * 1.6))
}
pub fn commands(st, eye) => [target("camera-rig", eye, cam.look(eye, st.camera.target)), target("player", [st.car.x, st.car.y, st.car.z], u.quat(st.car.heading, st.car.pitch, st.car.roll)),
    *wheel_targets(st.car), *[for (a in st.traffic) target(a.id, [a.x, 0.0, a.z], u.quat(a.heading))],
    *[for (p in st.peds) target(p.id, [p.x, p.y, p.z], u.quat(p.heading))],
    beacon_target("pickup", st.mission, st.t), beacon_target("drop", st.mission, st.t)]

// the chase camera pulls in front of any building between it and the car's roof
pn unblocked(scene, camera) {
    if (camera.mode == "hood" or camera.mode == "top") { return camera.position }
    let tg = camera.anchor, p = camera.position
    let d = [p[0] - tg[0], p[1] - tg[1], p[2] - tg[2]]
    let length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    if (length < 0.5) { return p }
    let hits = ps.query(scene, [{type: "ray", origin: tg, direction: d, distance: length, groups: {membership: sc.PROPS, filter: sc.STATIC}}])
    let hit = if (hits is array and len(hits) > 0) hits[0] else null
    if (hit == null or not (hit.distance is number)) { return p }
    let keep = max(1.2, float(hit.distance) - 0.6) / length
    return [tg[0] + d[0] * keep, tg[1] + d[1] * keep, tg[2] + d[2] * keep]
}

// handles to every node a frame touches, looked up once
pub pn bind(owner) {
    let root = dom.root_node(owner)
    let node = (id) => dom.get_element_by_id(root, id)
    let cells = (id, width) => [for (i in 0 to (width - 1)) [for (k in 0 to 10) node(id ++ "-" ++ string(i) ++ "-" ++ string(k))]]
    return {owner: owner, scene: node("city"), needle: node("needle"), rpm: node("rpm-fill"), damage: node("damage-fill"),
        speed: cells("speed", 3), timer: cells("timer", 3), timer_box: node("timer-box"), gears: [for (k in 0 to 6) node("gear-" ++ string(k))],
        district: node("district"), street: node("street"), look: node("look-name"), mission: node("mission"), mission_text: node("mission-text"),
        cash: node("cash"), toast: node("toast"), toast_text: node("toast-text"), map: node("map-world"), map_target: node("map-target"),
        pointer: node("map-pointer"), north: node("map-north")}
}
pn paint(target, property, value) { dom.presentation_style_set_property(target, property, value) }
pn words(target, text) { if (dom.text_content(target) != text) dom.set_text_content(target, text) }
// switch only the cells whose character changed
pn digits(cells, before, after) {
    for (i, row in cells) {
        let a = slice(before, i, i + 1), b = slice(after, i, i + 1)
        if (a != b) {
            paint(row[index_of(hud.GLYPHS, a)], "opacity", "0")
            paint(row[index_of(hud.GLYPHS, b)], "opacity", "1")
        }
    }
}
fn padded(text, width) => join(fill(max(0, width - len(text)), " "), "") ++ text
fn deg(a) => numbers.fixed(a * 180.0 / u.PI, 2) ++ "deg"
fn px(x) => numbers.fixed(x, 2) ++ "px"

pn hud_frame(h, st) {
    let car = st.car
    let kmh = v.speed(car) * 3.6
    let speed = padded(string(int(round(kmh)) ^ { 0 }), 3)
    digits(h.speed, st.shown.speed, speed)
    paint(h.needle, "transform", "rotate(" ++ numbers.fixed(-120.0 + 240.0 * u.clamp(kmh / 220.0, 0.0, 1.05), 2) ++ "deg)")
    paint(h.rpm, "transform", "scaleX(" ++ numbers.fixed(u.clamp(car.rpm / v.SPEC.red_rpm, 0.05, 1.0), 3) ++ ")")
    paint(h.damage, "transform", "scaleX(" ++ numbers.fixed(car.damage / 100.0, 3) ++ ")")
    let gear = v.gear_label(car)
    if (gear != st.shown.gear) {
        for (k, g in hud.GEARS) { if (g == st.shown.gear or g == gear) paint(h.gears[k], "opacity", if (g == gear) "1" else "0") }
    }
    // the map turns so the car always points up
    let turn = car.heading - u.PI
    paint(h.map, "transform", "rotate(" ++ deg(turn) ++ ") scale(" ++ numbers.fixed(hud.MAP_SCALE, 3) ++ ") translate(" ++ px(0.0 - car.x) ++ ", " ++ px(0.0 - car.z) ++ ")")
    paint(h.north, "transform", "rotate(" ++ deg(turn) ++ ")")
    let m = st.mission
    let tgt = m.target
    if (tgt != null) {
        paint(h.map_target, "transform", "translate(" ++ px(tgt.x) ++ ", " ++ px(tgt.z) ++ ")")
        let dx = tgt.x - car.x, dz = tgt.z - car.z
        let sx = dx * cos(turn) - dz * sin(turn), sz = dx * sin(turn) + dz * cos(turn)
        let far = sqrt(dx * dx + dz * dz) * hud.MAP_SCALE > hud.MAP_RADIUS - 8.0
        paint(h.pointer, "transform", "rotate(" ++ deg(math.atan2(sz, sx) + u.PI / 2.0) ++ ")")
        paint(h.pointer, "opacity", if (far) "1" else "0")
    }
    let timer = if (m.phase == "deliver") padded(string(int(ceil(ms.time_left(m, st.t))) ^ { 0 }), 3) else "   "
    digits(h.timer, st.shown.timer, timer)
    paint(h.toast, "opacity", numbers.fixed(u.clamp((st.toast_until - st.t) * 2.0, 0.0, 1.0), 2))
}
// words change only when the situation does
pn hud_words(h, st) {
    let car = st.car
    if (st.frames % 15 == 0) {
        words(h.district, city.district(car.x, car.z))
        words(h.street, city.street(car.x, car.z))
    }
    words(h.look, sc.LOOKS[st.look].label ++ " · " ++ cam.LABELS[st.camera.mode] ++ (if (st.auto != null) " · AUTO" else ""))
    let m = st.mission
    let text = if (m.phase == "pickup") "Pick up the parcel on " ++ m.target.name
        else if (m.phase == "deliver") "Deliver to " ++ m.target.name else "Press M to pick up a parcel"
    words(h.mission_text, text)
    words(h.cash, "$" ++ string(m.cash) ++ (if (m.delivered > 0) "  ·  " ++ string(m.delivered) ++ " delivered" else ""))
    scene_dom.attr(h.mission, "data-phase", m.phase)
    paint(h.timer_box, "opacity", if (m.phase == "deliver") "1" else "0")
    paint(h.map_target, "opacity", if (m.target != null) "1" else "0")
    if (st.toast != "") words(h.toast_text, st.toast)
}
pub pn sync(st) {
    let h = st.handles
    ps.command(h.scene, commands(st, unblocked(h.scene, st.camera)))
    hud_frame(h, st)
    hud_words(h, st)
    let gear = v.gear_label(st.car)
    let timer = if (st.mission.phase == "deliver") padded(string(int(ceil(ms.time_left(st.mission, st.t))) ^ { 0 }), 3) else "   "
    return {*: st, shown: {*: st.shown, speed: padded(string(int(round(v.speed(st.car) * 3.6)) ^ { 0 }), 3), timer: timer, gear: gear}}
}

// the native scene clock drives the signals; reading it keeps traffic in step with the lamps
pub pn scene_time(st, dt) {
    let snap = runtime.snapshot(st.handles.scene)
    return if (snap is map and snap.time is number) float(snap.time) else st.t + dt
}
// lighting and headlight edits, made once per change
pub pn apply_edits(st, edits) {
    let root = dom.root_node(st.handles.owner)
    for (e in edits) {
        let target = dom.get_element_by_id(root, e.id)
        if (target != null) scene_dom.attr(target, e.key, e.value)
    }
}
