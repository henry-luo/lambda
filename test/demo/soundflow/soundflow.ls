// Run: ./lambda.exe demo soundflow
// A visual instrument after Agoston Nagy's SoundBow: draw across a field of
// strings, and each stroke replays in a loop, sounding every string it
// crosses. The drawing, the timing and the instruments are all Lambda.
import dom
import m: .mod_model
import scene: .mod_scene
import sound: .mod_sound
import synth: .mod_synth

let SAVE_DIR = "temp/soundflow"

fn audio_owner(evt) => dom.query_selector(dom.root_node(evt.target), "#sf-clock")
fn stage_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "stage")
// the stage letterboxes the 1280×800 field (preserveAspectRatio meet)
fn field_point(box, evt) => (let s = min(box.width / m.W, box.height / m.H),
    {x: (evt.clientX - box.x - (box.width - m.W * s) / 2) / s, y: (evt.clientY - box.y - (box.height - m.H * s) / 2) / s})
fn key_action(key) {
    let k = lower(string(key))
    if (k == "1" or k == "2" or k == "3") {action: "voice", value: int(k) - 1}
    else if (k == " " or k == "space" or k == "p") {action: "pause", value: 0}
    else if (k == "z" or k == "u" or k == "backspace") {action: "undo", value: 0}
    else if (k == "m") {action: "sound", value: 0}
    else if (k == "e" or k == "s") {action: "save", value: 0}
    else if (k == "l") {action: "next-layout", value: 0}
    else if (k == "=" or k == "+" or k == "]") {action: "faster", value: 0}
    else if (k == "-" or k == "_" or k == "[") {action: "slower", value: 0}
    else if (k == "h") {action: "ribbons", value: 0}
    else if (k == "escape") {action: "close-menu", value: 0}
    else null
}
// every note the current strings can play, the chosen voice first
fn wanted(st) => [for (v in [st.voice, *[for (i of m.VOICES where i != st.voice) i]])
    for (j of st.strings) {voice: m.VOICES[v].key, note: m.voice_note(st, v, j)}]

fn initial() => {st: m.initial(), sound: sound.initial(), saves: 0}

// plays whatever the step just sounded
pn hear(evt, session, next) {
    let played = sound.play(audio_owner(evt), session.sound, m.playable(next, next.hits))
    return {*: session, st: next, sound: played}
}

// a recording of the loops as they stand, and a picture of the drawing
pn save(session) {
    let st = session.st
    if (len(st.strokes) == 0) { return {*: session, st: {*: st, status: "Draw something to save first"}} }
    // never overwrite an earlier session's recording
    var n = session.saves + 1
    while (exists(SAVE_DIR ++ "/soundflow-" ++ string(n) ++ ".wav")) { n = n + 1 }
    let base = SAVE_DIR ++ "/soundflow-" ++ string(n)
    let seconds = m.export_seconds(st)
    var problem = ""
    io.mkdir(SAVE_DIR) ^ { null }
    output(synth.mixdown(m.export_plan(st), seconds), base ++ ".wav") ^ { problem = ^.message }
    output(format(scene.field(st, false), 'xml'), base ++ ".svg") ^ { problem = ^.message }
    let status = if (problem != "") "Could not save: " ++ problem
        else "Saved " ++ base ++ ".wav (" ++ string(int(seconds)) ++ " s) and .svg"
    return {*: session, saves: if (problem != "") session.saves else n, st: {*: st, status: status}}
}

pn command(evt, session, action, value) {
    if (action == "sound") { return {*: session, sound: sound.toggle(audio_owner(evt), session.sound)} }
    if (action == "save") { return save(session) }
    if (action == "next-layout") {
        return {*: session, st: m.command(session.st, "layout", (session.st.layout + 1) % len(m.LAYOUTS))}
    }
    if (action == "close-menu") { return {*: session, st: {*: session.st, menu: false}} }
    let next = m.command(session.st, action, value)
    // old tails should not ring on into a cleared or undone drawing
    if (action == "clear" or action == "undo") {
        return {*: session, st: next, sound: sound.silence(audio_owner(evt), session.sound)}
    }
    return {*: session, st: next}
}

view <soundflow_app> state session: initial() {
    scene.page(session.st, {status: sound.status(session.sound), enabled: session.sound.enabled,
        played: session.sound.played, tones: len([for (k at session.sound.tones) k])})
}
on pointerdown(evt) {
    if (evt.button != 0 or dom.closest(evt.target, "button") != null or dom.closest(evt.target, ".panel") != null) { return 'pass' }
    let stage = stage_of(evt)
    let p = field_point(dom.bounding_box(stage), evt)
    let st = {*: session.st, menu: false, status: ""}
    let grab = m.grab_at(st, p.x, p.y)
    session = {*: session, st: if (grab != null) m.begin_drag(st, grab, p.x, p.y) else m.begin_stroke(st, p.x, p.y, evt.time_stamp)}
    dom.set_pointer_capture(stage, evt.pointerId)
    dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target), "soundflow"), false)
    return 'prevent-default'
}
on pointermove(evt) {
    let st = session.st
    if (st.drawing == null and st.drag == null) { return 'pass' }
    let p = field_point(dom.bounding_box(stage_of(evt)), evt)
    session = hear(evt, session, if (st.drag != null) m.drag_to(st, p.x, p.y) else m.extend(st, p.x, p.y, evt.time_stamp))
    return 'prevent-default'
}
on pointerup(evt) {
    let st = session.st
    if (st.drawing == null and st.drag == null) { return 'pass' }
    session = hear(evt, session, if (st.drag != null) m.end_drag(st, evt.time_stamp) else m.finish(st, evt.time_stamp))
    return 'prevent-default'
}
on pointercancel(evt) {
    session = {*: session, st: m.end_drag({*: m.finish(session.st, evt.time_stamp), hits: []}, evt.time_stamp)}
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target, "button[data-action]")
    if (button == null) { return 'pass' }
    let value = dom.get_attribute(button, "data-value")
    session = command(evt, session, dom.get_attribute(button, "data-action"), if (value == null) 0 else int(value))
    return 'handled'
}
on sfkey(evt) {
    session = command(evt, session, evt.detail.action, evt.detail.value)
    return 'handled'
}
on sftick(evt) {
    let wall = evt.detail
    // while nothing moves, frames only warm the tone cache
    if (m.still(session.st, wall)) {
        let missing = len([for (n in wanted(session.st) where session.sound.tones[n.voice ++ ":" ++ string(n.note)] == null) n])
        if (missing > 0) { session = {*: session, sound: sound.warm(session.sound, wanted(session.st))} }
        return 'handled'
    }
    session = hear(evt, session, m.tick(session.st, wall))
    return 'handled'
}
on sfclose(evt) {
    session = {*: session, sound: sound.silence(audio_owner(evt), session.sound)}
    return 'handled'
}

// The frame clock lives outside the app subtree, so redraws cannot retire it.
view <soundflow_shell> {
    <body apply(<soundflow_app>); <div id:"sf-clock", 'aria-hidden':"true">>
}
on load(evt) {
    let ticker = dom.get_element_by_id(evt.target, "sf-clock")
    if (ticker != null) dom.request_frame(ticker, "sfframe")
    return 'handled'
}
on sfframe(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target), "#soundflow"), {type: "sftick", detail: evt.time_stamp})
    dom.request_frame(evt.target, "sfframe")
    return 'handled'
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey or evt.repeat) { return 'pass' }
    let action = key_action(evt.key)
    if (action == null) { return 'pass' }
    dom.dispatch(dom.query_selector(dom.root_node(evt.target), "#soundflow"), {type: "sfkey", detail: action})
    return 'prevent-default'
}
on closerequest(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target), "#soundflow"), "sfclose")
    return 'pass'
}

<html lang:"en", <head <meta charset:"UTF-8"> <title "Soundflow — draw your sounds">
    <link rel:"stylesheet", href:"soundflow.css">>;
    apply(<soundflow_shell>)>
