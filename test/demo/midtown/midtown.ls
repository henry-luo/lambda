// Midtown: an open-city driving sandbox on native Scene3D. Drive Lumen Bay's grid of towers,
// parks and harbour streets among lane-keeping traffic and pedestrians, jump the stunt ramps, scatter
// the cones, and run courier jobs against the clock. The genre follows open-world driving games such
// as the OpenLiberty project's Godot vehicle prototype; the city, rules and art are original.
// Open with ./lambda.exe demo midtown
import dom
import runtime: lambda.scene3d.runtime
import u: .mod_util
import ms: .mod_mission
import cam: .mod_camera
import sc: .mod_scene
import hud: .mod_hud
import present: .mod_present

let PROFILE = sys.proc.self.env.MIDTOWN_PROFILE# == "1"
// MIDTOWN_SPAWN=x,z,heading starts the car elsewhere, for replays of a particular corner of the city
fn spawn_at(text) => (let parts = [for (p in split(text or "", ",") ^ { [] }) float(p) ^ { null }],
    if (len(parts) == 3 and all([for (p in parts) p is number])) {x: parts[0], z: parts[1], heading: parts[2]} else present.START)
let START = present.initial(spawn_at(sys.proc.self.env.MIDTOWN_SPAWN#))
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "midtown")

// one frame: step the city, hand the result to the host, and ask for the next frame
pn frame(owner, st, now) {
    let dt = if (st.last == null) 0.0 else u.clamp((now - st.last) / 1000.0, 0.0, 0.05)
    let started = if (PROFILE) clock() else 0
    let t = present.scene_time(st, dt)
    let stepped = present.advance(st, dt, t)
    let simulated = if (PROFILE) clock() else 0
    let next = present.sync(stepped)
    if (PROFILE) {
        print("MIDTOWN_PROFILE frame_ms=" ++ string(if (st.last == null) 0.0 else now - st.last) ++ " step_ms=" ++ string((simulated - started) * 1000) ++
            " sync_ms=" ++ string((clock() - simulated) * 1000) ++ "\n")
    }
    let token = if (next.paused) 0 else dom.request_frame(owner, "drive_frame")
    return {*: next, last: if (next.paused) null else now, token: token}
}
pn begin(owner, st, auto) {
    let scene = dom.get_element_by_id(dom.root_node(owner), "city")
    dom.set_attribute(owner, "data-started", "true")
    dom.focus_set(scene, false)
    present.apply_edits(st, [{id: "cam", key: "fov", value: cam.FOV.chase}])
    let started = {*: st, started: true, camera: cam.cut_to(st.camera, st.car, "chase")}
    return if (auto) toggle_auto(started) else started
}
fn toggle_auto(st) => if (st.auto != null) {*: st, auto: null} else present.engage_auto(st)

// commands from buttons and keys
pn command(owner, st, act) {
    let h = st.handles
    if (act == "start") { return begin(owner, st, false) }
    if (act == "auto-start") { return begin(owner, st, true) }
    if (not st.started) { return st }
    if (act == "look") {
        let look = sc.next_look(st.look)
        let lights = look != "day"
        present.apply_edits(st, sc.look_edits(look, lights))^
        return {*: st, look: look, lights: lights, toast: sc.LOOKS[look].label, toast_until: st.t + 1.5}
    }
    if (act == "lights") {
        present.apply_edits(st, sc.lights_edits(not st.lights))^
        return {*: st, lights: not st.lights, toast: if (st.lights) "Headlights off" else "Headlights on", toast_until: st.t + 1.5}
    }
    if (act == "camera") {
        let mode = cam.next_mode(st.camera.mode)
        present.apply_edits(st, [{id: "cam", key: "fov", value: cam.FOV[mode]}])
        return {*: st, camera: cam.cut_to(st.camera, st.car, mode), toast: cam.LABELS[mode] ++ " camera", toast_until: st.t + 1.2}
    }
    if (act == "courier") {
        let m = if (st.mission.phase == "idle") ms.begin(st.mission, st.car) else ms.stop(st.mission)
        return {*: st, mission: m, toast: m.message, toast_until: st.t + 3.0}
    }
    if (act == "auto") {
        let next = toggle_auto(st)
        return {*: next, toast: if (next.auto != null) "Auto drive: hands off" else "You have the wheel", toast_until: st.t + 2.0}
    }
    if (act == "reset") { return {*: present.reset_car(st), toast: "Back on the road", toast_until: st.t + 1.5} }
    if (act == "pause") {
        if (st.paused) {
            runtime.play(h.scene)^
            dom.set_attribute(owner, "data-paused", "false")
            return {*: st, paused: false, last: null, token: dom.request_frame(owner, "drive_frame")}
        }
        runtime.pause(h.scene)^
        dom.set_attribute(owner, "data-paused", "true")
        if (st.token > 0) dom.cancel_frame(owner, st.token)
        return {*: st, paused: true, token: 0, keys: present.NO_KEYS, touch: present.NO_KEYS}
    }
    if (act == "help") {
        dom.set_attribute(owner, "data-help", if (st.help) "false" else "true")
        return {*: st, help: not st.help}
    }
    return st
}
pn pressed_states(owner, st) {
    for (b in hud.BUTTONS) {
        let node = dom.get_element_by_id(dom.root_node(owner), "btn-" ++ b.act)
        let on = if (b.act == "lights") st.lights else if (b.act == "auto") st.auto != null
            else if (b.act == "courier") st.mission.phase != "idle" else if (b.act == "pause") st.paused
            else if (b.act == "help") st.help else false
        if (node != null) dom.set_attribute(node, "aria-pressed", if (on) "true" else "false")
    }
}

view <drive_session> state st: START { ~.rendered }
on drive_activate(evt) {
    if ((evt.event_phase != 2 or st.handles != null)^) { return 'pass' }
    let owner = evt.target
    let bound = {*: st, handles: present.bind(owner)}
    st = frame(owner, bound, evt.time_stamp)
    return 'handled'
}
on drive_frame(evt) {
    if ((evt.event_phase != 2 or evt.detail != st.token)^) { return 'pass' }
    st = frame(evt.target, {*: st, token: 0}, evt.time_stamp)
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-act]")
    if ((button == null or st.handles == null)^) { return 'pass' }
    let owner = owner_of(evt)
    st = command(owner, st, dom.get_attribute(button, "data-act"))
    pressed_states(owner, st)
    // keys keep driving the car after a button click
    if ((st.started)^) dom.focus_set(st.handles.scene, false)^
    return 'handled'
}
on keydown(evt) {
    if ((st.handles == null or evt.metaKey or evt.ctrlKey or evt.altKey)^) { return 'pass' }
    let owner = owner_of(evt)
    let key = evt.key
    let held = present.key_of(key)
    if ((held != null)^) {
        // a driving key takes the wheel back from Auto drive
        let manual = if (st.auto != null and held != "back" and held != "brake") {*: st, auto: null, toast: "You have the wheel", toast_until: st.t + 1.5} else st
        st = {*: manual, keys: present.set_key(manual.keys, held, true)}
        if ((manual.auto == null and st.started)^) pressed_states(owner, st)^
        return 'prevent-default'
    }
    let lower_key = lower(key)
    let act = if (key == "Enter" and not st.started) "start" else if (key == "Escape" or lower_key == "p") "pause"
        else if (lower_key == "n") "look" else if (lower_key == "l" or lower_key == "z") "lights" else if (lower_key == "v") "camera"
        else if (lower_key == "m") "courier" else if (lower_key == "o") "auto" else if (lower_key == "r") "reset"
        else if (lower_key == "h") "help" else null
    if ((act == null or evt.repeat)^) { return if (act == null) 'pass' else 'prevent-default' }
    st = command(owner, st, act)
    pressed_states(owner, st)
    return 'prevent-default'
}
on keyup(evt) {
    let held = present.key_of(evt.key)
    if ((held == null)^) { return 'pass' }
    st = {*: st, keys: present.set_key(st.keys, held, false)}
    return 'prevent-default'
}
on pointerdown(evt) {
    if ((st.handles == null or evt.button != 0)^) { return 'pass' }
    let pedal = dom.closest(evt.target, "[data-hold]")
    if (pedal != null) {
        dom.set_pointer_capture(pedal, evt.pointerId)
        let held = dom.get_attribute(pedal, "data-hold")
        let manual = if (st.auto != null and held != "brake") {*: st, auto: null} else st
        st = {*: manual, touch: present.set_key(manual.touch, held, true), drag: {pedal: held}}
        return 'prevent-default'
    }
    if (dom.closest(evt.target, "scene3d") == null) { return 'pass' }
    dom.focus_set(st.handles.scene, false)^
    dom.set_pointer_capture(st.handles.scene, evt.pointerId)
    st = {*: st, drag: {pedal: null, x: evt.clientX, y: evt.clientY}}
    return 'prevent-default'
}
on pointermove(evt) {
    if ((st.drag == null or st.drag.pedal != null)^) { return 'pass' }
    st = {*: st, camera: cam.drag(st.camera, (evt.clientX - st.drag.x) * 0.006, (evt.clientY - st.drag.y) * 0.003),
        drag: {*: st.drag, x: evt.clientX, y: evt.clientY}}
    return 'prevent-default'
}
on pointerup(evt) {
    if ((st.drag == null)^) { return 'pass' }
    dom.release_pointer_capture(evt.target, evt.pointerId)
    st = {*: st, touch: if (st.drag.pedal != null) present.set_key(st.touch, st.drag.pedal, false) else st.touch, drag: null}
    return 'prevent-default'
}
on pointercancel(evt) {
    st = {*: st, touch: present.NO_KEYS, drag: null}
    return 'pass'
}
// props the car scatters
on physicscontactstart(evt) {
    let d = evt.detail
    if ((d == null or st.handles == null)^) { return 'pass' }
    let other = if (d.body_a == "player") d.body_b else if (d.body_b == "player") d.body_a else null
    if (other == null or not starts_with(other, "prop-")^ or contains(st.smashed, other)^) { return 'pass' }
    let smashed = [*st.smashed, other]
    st = {*: st, smashed: smashed, toast: string(len(smashed)) ++ " props scattered", toast_until: st.t + 1.4}
    return 'handled'
}
on blur(evt) {
    st = {*: st, keys: present.NO_KEYS, touch: present.NO_KEYS, drag: null}
    return 'pass'
}
on closerequest(evt) {
    if ((st.token > 0 and st.handles != null)^) dom.cancel_frame(st.handles.owner, st.token)^
    st = {*: st, token: 0, paused: true}
    return 'pass'
}

view <drive_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "midtown")
    if (owner != null) dom.request_frame(owner, "drive_activate")
    return 'handled'
}

<html lang: "en",
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title "Midtown"> <link rel: "stylesheet", href: "midtown.css">>
    apply(<drive_document rendered: <body class: "midtown", apply(<drive_session rendered: hud.page(START)>)>>)
>
