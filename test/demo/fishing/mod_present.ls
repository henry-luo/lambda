// One frame handed to the host: a pose target for every moving body in one physics command, clip
// switches and swim-rate changes as one animation batch, ripples and splashes as one particle
// batch, and HUD text only when it changes. Batches share the scene's command sequence and land
// on its next command tick.
import dom
import runtime: lambda.scene3d.runtime
import ps: lambda.physics.scene
import cam: ~~.midtown.mod_camera
import u: ~~.midtown.mod_util
import sim: .mod_sim

pub pn bind(owner) {
    let root = dom.root_node(owner)
    let node = (id) => dom.get_element_by_id(root, id)
    return {owner: owner, scene: node("cove"), count: node("catch-count"), hint: node("hint"), toast: node("toast"), toast_text: node("toast-text"),
        badge: node("badge")}
}
fn target(body, position, q) => {type: "target", body: body, position: position, quaternion: q}
// fish waiting for their turn under the floor; nothing ever parks there in normal play
pub let PARKED = [0.0, -30.0, 0.0]
pub fn targets(st) any^ {
    let b = st.boat
    let o = st.otter
    let fishes = [for (f in st.fish) if (f.mode == "hooked") (let p = sim.hooked_pose(st, f, st.t)^, target(f.id, p.position, u.quat(p.yaw, p.pitch, p.roll)))
        else target(f.id, [f.x, f.y, f.z], u.quat(f.h, f.pitch, f.roll))];
    [target("boat", [b.x, b.y, b.z], u.quat(b.h, b.pitch, b.roll)), target("camera-rig", st.camera.position, cam.look(st.camera.position, st.camera.target)^),
     target("otter", [o.x, o.y, o.z], u.quat(o.h, o.pitch, o.roll)), *fishes]
}
fn identity(rows, key, id) any^ => (let hit = [for (r in rows where ((string(r[key]) == id) ^ { false })) r], if (len(hit) > 0) hit[0].identity else null)
fn envelope(snap, sequence, tick) => {session: snap.session, generation: snap.generation, service_generation: snap.service_generation,
    world_identity: snap.world_identity, revision: snap.revision, sequence: sequence, tick: tick}
// clip stops and starts, then rate/weight changes, in one batch on the next command tick
pn clip_batch(scene, st, sequence) {
    let anim = runtime.animation_snapshot(scene) ^ { null }
    if (anim == null or not (anim.clips is array)) { return sequence }
    let rows = anim.clips
    let switches = [for (p in st.plan, c in [{op: "stop", clip: p.stop}, {op: "play", clip: p.play, reset: true}], let ident = identity(rows, "clip", c.clip)^
        where ident != null) {*: c, identity: ident}]
    let rates = [for (r in st.rates, c in [{op: "rate", clip: r.clip, value: r.rate}, {op: "weight", clip: r.clip, value: r.weight}],
        let ident = identity(rows, "clip", r.clip)^ where ident != null) {*: c, identity: ident}]
    let commands = [*switches, *rates]
    if (len(commands) == 0) { return sequence }
    let sent = runtime.animation_commands(scene, {*: envelope(anim, sequence, anim.next_command_tick), commands: slice(commands, 0, min(len(commands), 256))})
    if (sent is error) { return sequence }
    return sequence + 1
}
pn particle_batch(scene, st, sequence, tick) {
    let parts = runtime.particle_snapshot(scene) ^ { null }
    if (parts == null or not (parts.emitters is array)) { return sequence }
    let spawns = [for (p in st.spawns, let ident = identity(parts.emitters, "id", p.emitter)^ where ident != null)
        {op: "spawn", emitter: p.emitter, identity: ident, count: p.count, position: p.position, *: (if (p.rotation != null) {rotation: p.rotation} else {})}]
    if (len(spawns) == 0) { return sequence }
    let sent = runtime.particle_commands(scene, {session: parts.session, generation: parts.generation, service_generation: parts.service_generation,
        sequence: sequence, tick: tick, commands: slice(spawns, 0, min(len(spawns), 128))})
    if (sent is error) { return sequence }
    return sequence + 1
}
pn words(target, text) { if ((dom.text_content(target) != text)^) dom.set_text_content(target, text) }
pn paint(target, property, value) { dom.presentation_style_set_property(target, property, value) }
// HUD: the catch count, a hint for what to do next, and a toast that pops in with a little bounce
pn hud(h, st) {
    // the mode and catch count on the page root, for replays and styling
    if ((dom.get_attribute(h.owner, "data-mode") != st.mode)^) dom.set_attribute(h.owner, "data-mode", st.mode)
    let count = string(st.catches)^
    if ((dom.get_attribute(h.owner, "data-catches") != count)^) dom.set_attribute(h.owner, "data-catches", count)
    words(h.count, count)^
    words(h.hint, sim.hint(st))^
    let left = st.toast_until - st.t
    let shown = left > 0.0
    if (shown) words(h.toast_text, st.toast)^
    let age = 2.8 - left
    let pop = if (shown) 1.0 + 0.22 * exp(-7.0 * age) * cos(16.0 * age) else 0.8
    paint(h.toast, "opacity", if (shown) string(u.clamp(left * 3.0, 0.0, 1.0)) else "0")^
    paint(h.toast, "transform", "translateX(-50%) scale(" ++ string(pop) ++ ")")^
    let since = if (st.celebrate == null) 99.0 else st.t - st.celebrate
    paint(h.badge, "transform", "scale(" ++ string(1.0 + 0.35 * exp(-6.0 * since) * cos(14.0 * since)) ++ ")")^
}
pub pn sync(st, snap) {
    let h = st.handles
    ps.command(h.scene, targets(st)^)^
    let base = if (snap is map and snap.command_sequence != null) int(snap.command_sequence) ^ { 0 } else 0
    let tick = if (snap is map) snap.next_command_tick else null
    var sequence = base + 1
    if ((len(st.plan) > 0 or len(st.rates) > 0)^) { sequence = clip_batch(h.scene, st, sequence)^ }
    if ((len(st.spawns) > 0 and tick != null)^) particle_batch(h.scene, st, sequence, tick)^
    hud(h, st)^
    return st
}
