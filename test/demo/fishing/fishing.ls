// lambda demo fishing — row a little boat around Willow Cove, cast, wait for a bite and strike.
// The scene is built once; each frame steps the game (mod_sim) and hands poses, clip switches and
// ripples to the host (mod_present).
import dom
import runtime: lambda.scene3d.runtime
import sim: .mod_sim
import present: .mod_present
import hud: .mod_hud

// FISHING_ASSIST=1 starts with the strike assist on (replays use it to land a catch)
// FISHING_STAGE=bite starts with a koi nibbling at a cast line (see sim.staged)
let ENV = sys.proc.self.env
// FISHING_PROFILE=1 prints the Lambda step and host hand-off cost of every frame
let PROFILE = ENV.FISHING_PROFILE == "1"
let START = (let st = sim.initial() ^ { null }, let assisted = if (st != null and ENV.FISHING_ASSIST == "1") {*: st, assist: true} else st,
    if (assisted != null and ENV.FISHING_STAGE != null) sim.staged(assisted, ENV.FISHING_STAGE) ^ { assisted } else assisted)
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "fishing")
fn key_of(key) => if (key == "ArrowUp" or key == "w" or key == "W") "up" else if (key == "ArrowDown" or key == "s" or key == "S") "down"
    else if (key == "ArrowLeft" or key == "a" or key == "A") "left" else if (key == "ArrowRight" or key == "d" or key == "D") "right" else null
fn set_key(keys, key, down) => {*: keys, up: if (key == "up") down else keys.up, down: if (key == "down") down else keys.down,
    left: if (key == "left") down else keys.left, right: if (key == "right") down else keys.right}

// one frame on the scene clock: step the game, hand the result to the host, ask for the next frame
pn frame(owner, st) {
    let snap = runtime.snapshot(st.handles.scene) ^ { null }
    let now = if (snap is map and snap.time is number) float(snap.time) else st.t + 1.0 / 60.0
    let dt = u_clamp(now - st.t, 0.0, 0.1)
    let started = if (PROFILE) clock() else 0
    let stepped = sim.step(st, now, dt)
    let simulated = if (PROFILE) clock() else 0
    // a failed step keeps the last good state and tries again next frame
    if (stepped is error) { return {*: st, t: now, token: dom.request_frame(owner, "fishing_frame")} }
    // a host hand-off that fails keeps the stepped game; the next frame sends everything again
    let synced = present.sync(stepped, snap) ^ { stepped }
    if (PROFILE) {
        print("FISHING_PROFILE frame=" ++ string(st.frame) ++ " step_ms=" ++ string((simulated - started) * 1000) ++
            " sync_ms=" ++ string((clock() - simulated) * 1000) ++ " mode=" ++ string(stepped.mode) ++ "\n")
    }
    return {*: synced, token: dom.request_frame(owner, "fishing_frame")}
}
fn u_clamp(x, lo, hi) => if (x < lo) lo else if (x > hi) hi else x
// a pointer drag: whether it has moved far enough to be a drag, and the orbit it has swung the camera to
fn travelled(d, evt) => abs(evt.clientX - d.x) + abs(evt.clientY - d.y) >= 5
fn orbited(d, evt) => {yaw: d.orbit.yaw - (evt.clientX - d.x) * 0.006, tilt: u_clamp(d.orbit.tilt + (evt.clientY - d.y) * 0.004, -0.6, 0.4)}

view <fishing_session> state st: START { ~.rendered }
on fishing_activate(evt) {
    if ((evt.event_phase != 2 or st.handles != null)^) { return 'pass' }
    let owner = evt.target
    st = frame(owner, {*: st, handles: present.bind(owner)})
    dom.focus_set(st.handles.scene, false)
    return 'handled'
}
on fishing_frame(evt) {
    if ((evt.event_phase != 2 or evt.detail != st.token)^) { return 'pass' }
    st = frame(evt.target, st)
    return 'handled'
}
on keydown(evt) {
    if ((st.handles == null or evt.metaKey or evt.ctrlKey or evt.altKey)^) { return 'pass' }
    let held = key_of(evt.key)
    if ((held != null)^) { st = {*: st, keys: set_key(st.keys, held, true)}; return 'prevent-default' }
    if ((lower(evt.key) == "t" and not evt.repeat)^) { st = sim.toggle_assist(st); return 'prevent-default' }
    if ((evt.key == " " or evt.key == "Enter")^) {
        if ((not evt.repeat)^) { st = {*: st, press: true} }
        return 'prevent-default'
    }
    return 'pass'
}
on keyup(evt) {
    let held = key_of(evt.key)
    if ((held == null)^) { return 'pass' }
    st = {*: st, keys: set_key(st.keys, held, false)}
    return 'prevent-default'
}
// a click or tap on the water (no drag) casts, reels in or strikes, like Space; a drag orbits the camera
// round the boat (helpers above the view: across swings it round, up and down raise and lower it)
on pointerdown(evt) {
    if ((st.handles == null or evt.button != 0)^) { return 'pass' }
    let scene = dom.closest(evt.target, "scene3d")
    if ((scene == null)^) { return 'pass' }
    dom.focus_set((st.handles.scene)^, false)
    st = {*: st, drag: {x: evt.clientX, y: evt.clientY, orbit: st.orbit, moved: false}}
    dom.set_pointer_capture(scene, (evt.pointerId)^)
    return 'prevent-default'
}
on pointermove(evt) {
    if ((st.drag == null)^) { return 'pass' }
    let d = st.drag
    if ((d.moved or travelled(d, evt))^) { st = {*: st, drag: {*: d, moved: true}, orbit: orbited(d, evt)} }
    return 'prevent-default'
}
on pointerup(evt) {
    if ((st.drag == null)^) { return 'pass' }
    let d = st.drag
    let scene = dom.query_selector(dom.root_node(evt.target), "scene3d")
    if ((scene != null)^) dom.release_pointer_capture(scene, (evt.pointerId)^)
    let clicked = not (d.moved or travelled(d, evt))
    st = {*: st, drag: null, press: st.press or clicked, orbit: if (clicked) st.orbit else orbited(d, evt)}
    return 'prevent-default'
}
on pointercancel(evt) {
    st = {*: st, drag: null}
    return 'pass'
}
on blur(evt) {
    st = {*: st, keys: sim.NO_KEYS}
    return 'pass'
}
on closerequest(evt) {
    if ((st.token > 0 and st.handles != null)^) dom.cancel_frame((st.handles.owner)^, (st.token)^)
    st = {*: st, token: 0}
    return 'pass'
}
view <fishing_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "fishing")
    if (owner != null) dom.request_frame(owner, "fishing_activate")
    return 'handled'
}
<html lang: "en",
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title "Fishing"> <link rel: "stylesheet", href: "fishing.css">>
    apply(<fishing_document rendered: <body apply(<fishing_session rendered: hud.page(START) ^ { <p "The cove could not be built."> }>)>>)
>
