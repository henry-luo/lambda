// Run: ./lambda.exe view test/demo/doom/doom.ls
// The template is cached; handlers publish simulation state without rebuilding
// level geometry (S9.1.7, S12.1.3, D4.5.1v4).
import dom
import data: .mod_data
import game_state: .mod_state
import world: .mod_world
import controls: .mod_input
import sim: .mod_sim
import scene: .mod_scene
import present: .mod_present
import effects: .mod_effects
import camera: .mod_camera
import audio: .mod_audio

let BASE = url_resolve(data.entry_uri()^, ".")
let RULES = data.rules(BASE)^
let VISUALS = data.visuals(BASE)^
let IMAGES = data.read(BASE, "data/images.json")^
let LEVEL = data.load_level(BASE, "E1M1")^
let WORLD = world.prepare(LEVEL)
let GAME = sim.initialize(WORLD, {*: game_state.new_game(LEVEL, RULES, VISUALS), level_name: "E1M1"})
let SCENE = scene.prepare(WORLD, GAME)
let PROFILE = sys.proc.self.env.DOOM_PROFILE# == "1"

fn internal_focus(evt) => dom.closest(evt.relatedTarget, "#doom") != null
pn stop_frame(owner, token) { if (token > 0) dom.cancel_frame(owner, token) }
pn publish(owner, previous, next, timestamp_ms, reset_clock = false) {
    stop_frame(owner, previous.token)
    var painted = null
    var problem = null
    var transients = next.transients
    var sounds = next.audio
    let handles = if (next.handles == null) present.bind(owner, next.map_data, next.game) else next.handles
    present.paint(owner, next.map_data, if (reset_clock) null else previous.game, next.game, handles,
        if (reset_clock) null else previous.visible, RULES, VISUALS, IMAGES, BASE) ^ { problem = ^.message } ~ { painted = ~ }
    if (problem == null) {
        effects.synchronize(handles.camera, next.transients, next.game, camera.pose(next.game).angle, BASE) ^ { problem = ^.message } ~ { transients = ~ }
    }
    if (problem == null) {
        audio.synchronize(owner, next.audio, next.game, BASE) ^ { problem = ^.message } ~ { sounds = ~ }
    }
    let token = if (problem == null and next.game.mode == "playing") dom.request_frame(owner, "doom_frame") else 0
    if (next.game.mode == "playing" and token == 0 and problem == null) { problem = "DOOM: frame request failed" }
    if (problem != null) {
        if (sounds != null) audio.close(owner, sounds)
        sounds = null
        dom.set_attribute(owner, "data-error", problem)
        present.text(handles.status, problem)
    }
    if (problem != null or next.game.mode != "playing" or next.game.spectator != "player" or next.game.autoplay)
        dom.set_relative_mouse(owner, false)
    return {*: next, handles: handles, visible: painted, transients: transients, audio: sounds, token: token,
        game: if (problem == null) next.game else {*: next.game, mode: "error", input: controls.empty()},
        clock: if (reset_clock) sim.initial_clock() else next.clock}
}
pn load_map(owner, session_value, map_name, keep_inventory = false) any^ {
    stop_frame(owner, session_value.token)
    let level = data.load_level(BASE, map_name)^
    let prepared = world.prepare(level)
    let skill = if (keep_inventory) session_value.game.skill else
        int(dom.get_state(dom.get_element_by_id(owner, "skill-picker"), "value"))
    if (not (skill is int) or skill < 1 or skill > 5) { raise error("DOOM: invalid difficulty") }
    let game = sim.initialize(prepared, if (keep_inventory)
        game_state.transition(session_value.game, level, RULES, VISUALS, map_name)
        else {*: game_state.new_game(level, RULES, VISUALS, skill), mode: "playing",
            level_name: map_name, generation: session_value.game.generation + 1})
    let described = scene.prepare(prepared, game)
    let tree = scene.tree(described, game, RULES, VISUALS, IMAGES, BASE)
    // Keep the owner and its view binding; replace level children only.
    dom.set_inner_html(owner, format([for (child in tree) child], 'html'))
    return {*: session_value, game: game, map_data: described, clock: sim.initial_clock(),
        handles: null, visible: null, transients: [], token: 0, drag: null}
}
pn action(owner, session_value, command, timestamp_ms) any^ {
    let next = if (command == "map") load_map(owner, session_value,
        dom.get_state(dom.get_element_by_id(owner, "map-picker"), "value"))^
        else {*: session_value, game: sim.action(session_value.map_data, session_value.game, command, RULES, VISUALS)}
    let committed = publish(owner, session_value, next, timestamp_ms, true)
    if (committed.game.mode == "playing" and committed.game.spectator == "player" and not committed.game.autoplay and
        not dom.set_relative_mouse(owner, true)) { raise error("DOOM: relative mouse capture failed") }
    dom.focus_set(owner, false)
    return committed
}

view <doom_session> state session: {game: ~.game, map_data: ~.map_data, clock: sim.initial_clock(),
    handles: null, visible: null, token: 0, commands: controls.empty(), transients: [], drag: null, audio: null} {
    ~.rendered
}
on doom_activate(evt) {
    if (evt.event_phase != 2 or session.handles != null) { return 'pass' }
    session = publish(evt.target, session, session, evt.time_stamp, true)
    dom.focus_set(evt.target, false)
    return 'handled'
}
on doom_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token) { return 'pass' }
    let started = if (PROFILE) clock() else 0
    let result = sim.frame(session.map_data, session.game, session.clock, evt.time_stamp, RULES)
    let simulated = if (PROFILE) clock() else 0
    var next = {*: session, *: result, token: 0}
    if (next.game.transition != null and next.game.time >= next.game.transition.at) {
        if (next.game.transition.map == null) { next = {*: next, game: {*: next.game, mode: "won"}} }
        else { next = load_map(evt.target, next, next.game.transition.map, true)^ }
    }
    session = publish(evt.target, session, next, evt.time_stamp, next.handles == null)
    if (PROFILE) {
        print("DOOM_PROFILE timestamp_ms=" ++ format(evt.time_stamp, 'json') ++
            " generation=" ++ string(session.game.generation) ++ " map=" ++ session.game.level_name ++
            " simulation_ms=" ++ string((simulated - started) * 1000) ++
            " presentation_ms=" ++ string((clock() - simulated) * 1000) ++
            " ticks=" ++ string(session.game.ticks) ++
            " visible_planes=" ++ string(len([for (shown in session.visible where shown) shown])) ++
            " total_planes=" ++ string(len(session.map_data.planes)) ++ "\n")
    }
    return 'handled'
}
on click(evt) {
    let owner = dom.closest(evt.target, "#doom")
    let button = dom.closest(evt.target, "[data-action]")
    if (owner == null or button == null) { return 'pass' }
    session = action(owner, session, dom.get_attribute(button, "data-action"), evt.time_stamp)^
    return 'handled'
}
on keydown(evt) {
    let owner = dom.closest(evt.target, "#doom")
    let command = controls.command(evt.key, session.game.spectator)
    if (owner == null or command == "" or evt.altKey or evt.metaKey) { return 'pass' }
    if (contains(["pause", "restart", "spectator"], command)) {
        // Movement clears on pause; physical command keys stay latched until
        // release, so native key repeat cannot trigger a second command.
        let commands = controls.set_key(session.commands, evt.key, true, session.game.spectator)
        let next = {*: session, commands: controls.consume(commands)}
        session = if (contains(commands.edges, command)) action(owner, next, command, evt.time_stamp)^ else next
    } else {
        session = {*: session, game: {*: session.game, autoplay: false,
            input: controls.set_key(if (session.game.autoplay) controls.empty() else session.game.input,
                evt.key, true, session.game.spectator)}}
    }
    return 'prevent-default'
}
on keyup(evt) {
    if (controls.command(evt.key, session.game.spectator) == "") { return 'pass' }
    session = {*: session, commands: controls.set_key(session.commands, evt.key, false, session.game.spectator),
        game: {*: session.game, input: controls.set_key(session.game.input, evt.key, false, session.game.spectator)}}
    return 'prevent-default'
}
on wheel(evt) {
    let owner = dom.closest(evt.target, "#doom")
    if (owner == null or session.game.spectator == "player") { return 'pass' }
    session = publish(owner, session, {*: session, game: camera.wheel(session.game, evt.deltaY)}, evt.time_stamp)
    return 'prevent-default'
}
on pointerdown(evt) {
    if (session.game.spectator != "top" or dom.closest(evt.target, "#viewport") == null or evt.button != 0) { return 'pass' }
    session = {*: session, drag: {x: evt.clientX, y: evt.clientY}}
    if (not dom.set_pointer_capture(evt.target, evt.pointerId)) { raise error("DOOM: pointer capture failed") }
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null) { return 'pass' }
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "doom")
    let game = camera.drag(session.game, evt.clientX - session.drag.x, evt.clientY - session.drag.y)
    session = publish(owner, session, {*: session, game: game, drag: {x: evt.clientX, y: evt.clientY}}, evt.time_stamp)
    return 'prevent-default'
}
on mousemove(evt) {
    let owner = dom.closest(evt.target, "#doom")
    if (owner == null or session.game.mode != "playing" or session.game.spectator != "player" or
        not dom.relative_mouse_active(owner)) { return 'pass' }
    session = {*: session, game: {*: session.game,
        input: controls.mouse(session.game.input, evt.movementX, evt.movementY)}}
    return 'prevent-default'
}
on mousedown(evt) {
    if (session.game.mode != "playing" or session.game.spectator != "player" or evt.button != 0) { return 'pass' }
    let owner = dom.closest(evt.target, "#doom")
    if (owner == null or dom.closest(evt.target, "#viewport") == null) { return 'pass' }
    if (not dom.set_relative_mouse(owner, true)) { raise error("DOOM: relative mouse capture failed") }
    session = {*: session, game: {*: session.game, autoplay: false,
        input: controls.set_key(if (session.game.autoplay) controls.empty() else session.game.input, "mouse1", true)}}
    return 'prevent-default'
}
on pointerup(evt) { session = {*: session, drag: null}; return 'pass' }
on pointercancel(evt) { session = {*: session, drag: null}; return 'pass' }
on mouseup(evt) {
    session = {*: session, drag: null, game: {*: session.game, input: controls.set_key(session.game.input, "mouse1", false)}}
    return 'pass'
}
on blur(evt) {
    // Moving focus to a game button must not pause before that button's click.
    if (internal_focus(evt)) { return 'pass' }
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "doom")
    if (owner != null and session.game.mode == "playing") { session = action(owner, session, "pause", evt.time_stamp)^ }
    session = {*: session, commands: controls.empty(), drag: null, game: {*: session.game, input: controls.empty()}}
    return 'pass'
}
on doom_blur(evt) {
    let owner = dom.closest(evt.target, "#doom")
    dom.set_relative_mouse(owner, false)
    if (session.game.mode == "playing") { session = action(owner, session, "pause", evt.time_stamp)^ }
    session = {*: session, commands: controls.empty(), drag: null, game: {*: session.game, input: controls.empty()}}
    return 'handled'
}
on closerequest(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "doom")
    if (owner != null) {
        stop_frame(owner, session.token)
        if (session.audio != null) audio.close(owner, session.audio)
        dom.set_relative_mouse(owner, false)
    }
    session = {*: session, token: 0, game: {*: session.game, input: controls.empty(), mode: "closed"}}
    return 'pass'
}

view <doom_document> { ~.rendered }
on blur(evt) {
    if (internal_focus(evt)) { return 'pass' }
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "doom")
    if (owner != null) dom.dispatch(owner, "doom_blur")
    return 'pass'
}
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "doom")
    if (owner != null) dom.request_frame(owner, "doom_activate")
    return 'handled'
}

<html lang:"en", <head <meta charset:"UTF-8"> <title "DOOM — Lambda Script / Radiant">
    <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>>
    apply(<doom_document rendered:<body apply(<doom_session game:GAME, map_data:SCENE,
        rendered:scene.tree(SCENE, GAME, RULES, VISUALS, IMAGES, BASE)>)>>)
>
