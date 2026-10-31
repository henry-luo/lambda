// Skymaster gear: an interactive study of the Cessna 337's main landing gear retraction. One
// hydraulic actuator strokes a rack; the pinion turns a cross shaft whose U-joints spin two side
// tubes half a turn, folding both spring legs. Lambda samples that single degree of freedom into a
// native clip; the scene clock plays it, and a frame loop keeps the panels and callouts in step.
// Open with ./lambda.exe view test/demo/scene3d/skymaster/skymaster.ls
import dom
import runtime: lambda.scene3d.runtime
import scene_dom: lambda.scene3d.dom
import rig: .mod_rig
import cam: .mod_camera
import sc: .mod_scene
import ui: .mod_ui
import present: .mod_present

let SPEEDS = [0.25, 2.0]
fn initial() => {seq: "landing", playing: true, speed: 1.0, at: 0.0, view: "hero", camera: cam.preset("hero"), tween: null,
    display: {labels: false, section: false, xray: true, exploded: false, fuselage: false, orbit: false},
    explode: 0.0, token: 0, last_ms: null, drag: null, stage: {width: 1280.0, height: 720.0}}

fn toggled(d, key) => {labels: d.labels != (key == "labels"), section: d.section != (key == "section"),
    xray: d.xray != (key == "xray"), exploded: d.exploded != (key == "exploded"), fuselage: d.fuselage != (key == "fuselage"),
    orbit: d.orbit != (key == "orbit")}
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "skymaster")
fn scene_of(owner) => present.node(owner, "gear-scene")
fn stage_of(owner) => (let box = dom.bounding_box(present.node(owner, "stage")),
    {width: max(1.0, float(box.width)), height: max(1.0, float(box.height))})

// the frame loop runs while anything moves: playback, a camera transition, orbit or a drag
fn busy(st) => st.playing or st.tween != null or st.display.orbit or st.drag != null
pn schedule(owner, previous, next) {
    let token = if (not busy(next)) 0 else if (previous.token > 0) previous.token else dom.request_frame(owner, "gear_frame")
    present.sync(owner, next)
    return {*: next, token: token, last_ms: if (token == 0) null else next.last_ms}
}
// the native clock is the sequence's clock: read it, and park the gear once the sequence ends
pn clocked(scene, st) {
    let snap = runtime.snapshot(scene)
    let seq = present.sequence(st)
    let at = if (snap is map and snap.time is number) float(snap.time) else st.at
    if (st.playing and at >= seq.duration) {
        runtime.seek(scene, seq.duration)
        return {*: st, at: seq.duration, playing: false}
    }
    return {*: st, at: at}
}
fn advance(st, now) {
    let dt = if (st.last_ms == null) 0.0 else rig.clamp((now - st.last_ms) / 1000.0, 0.0, 0.1)
    let tween = if (st.tween == null) null else if (st.tween.start == null) {*: st.tween, start: now} else st.tween
    let u = if (tween == null) 1.0 else (now - tween.start) / 900.0
    let camera = if (tween != null) cam.blend(tween.from, tween.to, u)
        else if (st.display.orbit and st.drag == null) cam.orbit(st.camera, 0.22 * dt, 0.0) else st.camera
    {*: st, camera: camera, tween: if (u >= 1.0) null else tween, last_ms: now}
}

pn start(owner, st, seconds) {
    let scene = scene_of(owner)
    let snap = runtime.snapshot(scene)
    let at = rig.seek_time(seconds, snap.timestep, present.sequence(st).duration)
    runtime.seek(scene, at)
    runtime.time_scale(scene, st.speed)
    if (st.playing) runtime.play(scene)
    return {*: st, at: at}
}
fn manual(st) => {*: st, view: "custom", tween: null, display: {*: st.display, orbit: false}}
pn command(owner, st, act, arg) {
    let scene = scene_of(owner)
    let seq = present.sequence(st)
    if (act == "play") {
        if (st.playing) {
            runtime.pause(scene)
            return clocked(scene, {*: st, playing: false})
        }
        // play from the top once a sequence has finished
        return start(owner, {*: st, playing: true}, if (st.at >= seq.duration - 0.01) 0.0 else st.at)
    }
    if (act == "seq") {
        if (arg == st.seq) { return st }
        // a new clip id: the native scene starts the other sequence from its first pose
        let old = dom.query_selector(scene, "animation-clip")
        if (old != null) dom.remove_child(scene, old)
        scene_dom.mount(scene, sc.clip(rig.SEQUENCES[arg]))
        let next = {*: st, seq: arg, playing: true}
        present.retitle(owner, next)
        return start(owner, next, 0.0)
    }
    if (act == "view") {
        return {*: st, view: arg, tween: {from: st.camera, to: cam.preset(arg), start: null},
            display: {*: st.display, orbit: false}}
    }
    if (act == "display") {
        let shown = toggled(st.display, arg)
        // exploding moves whole assemblies once; the scene recompiles on that one edit
        let next = {*: st, display: shown, explode: if (shown.exploded) 1.0 else 0.0}
        present.display(owner, next)
        return next
    }
    return st
}

pn slide(owner, st, id, x) {
    let box = dom.bounding_box(present.node(owner, id))
    let f = rig.clamp((x - box.x) / max(1.0, box.width), 0.0, 1.0)
    let scene = scene_of(owner)
    if (id == "speed") {
        let speed = round((SPEEDS[0] + f * (SPEEDS[1] - SPEEDS[0])) * 20.0) / 20.0
        runtime.time_scale(scene, speed)
        return {*: st, speed: speed}
    }
    // scrubbing pauses on the chosen instant; play resumes from there
    return start(owner, {*: st, playing: false}, f * present.sequence(st).duration)
}

view <gear_session> state st: initial() { ~.rendered }
on gear_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let owner = evt.target
    let ready = start(owner, {*: st, stage: stage_of(owner)}, 0.0)
    present.retitle(owner, ready)
    present.display(owner, ready)
    st = schedule(owner, st, ready)
    return 'handled'
}
on gear_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != st.token) { return 'pass' }
    let owner = evt.target
    let moved = advance(clocked(scene_of(owner), st), evt.time_stamp)
    st = schedule(owner, {*: moved, token: 0}, moved)
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-act]")
    if (button == null) { return 'pass' }
    let owner = owner_of(evt)
    let next = command(owner, st, dom.get_attribute(button, "data-act"), dom.get_attribute(button, "data-arg"))
    st = schedule(owner, st, next)
    return 'handled'
}
on pointerdown(evt) {
    if (evt.button != 0) { return 'pass' }
    let owner = owner_of(evt)
    let knob = dom.closest(evt.target, "[data-slider]")
    if (knob != null) {
        let id = dom.get_attribute(knob, "data-slider")
        dom.focus_set(knob, false)
        let next = slide(owner, {*: st, drag: {slider: id}}, id, evt.clientX)
        dom.set_pointer_capture(knob, evt.pointerId)
        st = schedule(owner, st, next)
        return 'prevent-default'
    }
    let scene = dom.closest(evt.target, "scene3d")
    if (scene == null) { return 'pass' }
    dom.focus_set(scene, false)
    dom.set_pointer_capture(scene, evt.pointerId)
    st = schedule(owner, st, {*: manual(st),
        drag: {slider: null, x: evt.clientX, y: evt.clientY, camera: st.camera}})
    return 'prevent-default'
}
on pointermove(evt) {
    if (st.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let next = if (st.drag.slider != null) slide(owner, st, st.drag.slider, evt.clientX)
        else {*: st, camera: cam.orbit(st.drag.camera, (st.drag.x - evt.clientX) * 0.008, (evt.clientY - st.drag.y) * 0.006)}
    st = schedule(owner, st, next)
    return 'prevent-default'
}
on pointerup(evt) {
    if (st.drag == null) { return 'pass' }
    dom.release_pointer_capture(evt.target, evt.pointerId)
    st = schedule(owner_of(evt), st, {*: st, drag: null})
    return 'prevent-default'
}
on pointercancel(evt) {
    st = schedule(owner_of(evt), st, {*: st, drag: null})
    return 'pass'
}
on wheel(evt) {
    if (dom.closest(evt.target, "scene3d") == null) { return 'pass' }
    st = schedule(owner_of(evt), st, {*: manual(st), camera: cam.zoom(st.camera, if (evt.deltaY < 0) 0.9 else 1.0 / 0.9)})
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = evt.key
    let knob = dom.closest(evt.target, "[data-slider]")
    if (knob != null and contains(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End"], key)) {
        let id = dom.get_attribute(knob, "data-slider")
        let direction = if (key == "ArrowRight" or key == "ArrowUp") 1.0 else -1.0
        let next = if (id == "speed") {*: st, speed: if (key == "Home") SPEEDS[0] else if (key == "End") SPEEDS[1]
            else rig.clamp(st.speed + direction * 0.05, SPEEDS[0], SPEEDS[1])}
            else start(owner, {*: st, playing: false}, if (key == "Home") 0.0 else if (key == "End") present.sequence(st).duration
                else st.at + direction * 0.1)
        if (id == "speed") runtime.time_scale(scene_of(owner), next.speed)
        st = schedule(owner, st, next)
        return 'prevent-default'
    }
    // focused buttons keep their native Space/Enter activation
    if (dom.closest(evt.target, "button, a") != null) { return 'pass' }
    let next = if (key == " ") command(owner, st, "play", null)
        else if (key == "ArrowLeft") {*: manual(st), camera: cam.orbit(st.camera, -0.12, 0.0)}
        else if (key == "ArrowRight") {*: manual(st), camera: cam.orbit(st.camera, 0.12, 0.0)}
        else if (key == "ArrowUp") {*: manual(st), camera: cam.orbit(st.camera, 0.0, 0.1)}
        else if (key == "ArrowDown") {*: manual(st), camera: cam.orbit(st.camera, 0.0, -0.1)}
        else if (key == "+" or key == "=") {*: manual(st), camera: cam.zoom(st.camera, 0.9)}
        else if (key == "-" or key == "_") {*: manual(st), camera: cam.zoom(st.camera, 1.0 / 0.9)}
        else if (lower(key) == "r") command(owner, st, "view", "hero")
        else null
    if (next == null) { return 'pass' }
    st = schedule(owner, st, next)
    return 'prevent-default'
}
on resize(evt) {
    let owner = owner_of(evt)
    if (owner == null) { return 'pass' }
    st = schedule(owner, st, {*: st, stage: stage_of(owner)})
    return 'pass'
}
on closerequest(evt) {
    if (st.token > 0) dom.cancel_frame(owner_of(evt), st.token)
    st = {*: st, token: 0, drag: null, playing: false}
    return 'pass'
}

view <gear_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "skymaster")
    if (owner != null) dom.request_frame(owner, "gear_activate")
    return 'handled'
}

let START = initial();
<html lang: "en",
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title "Skymaster gear / Scene3D">
        <link rel: "stylesheet", href: "../demo.css"> <link rel: "stylesheet", href: "skymaster.css">>
    apply(<gear_document rendered: <body class: "skymaster",
        apply(<gear_session rendered: ui.page(present.sequence(START), START.display, START.view, START.camera)>)>>)
>
