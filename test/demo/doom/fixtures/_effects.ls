import dom
import data: ~~.mod_data
import effects: ~~.mod_effects
let BASE = url_resolve(data.entry_uri()^, "../")
let PARTICLES = [for (i, kind in ["puff", "explosion", "fog"])
    {id: i, kind: kind, at: 0, position: {x: (i - 1) * 60, y: 100, z: 0}}]
let PROJECTILE = {id: 7, sprite: "MISLA1", size: 11, x: 0, y: 100, z: 30,
    start_x: 0, start_y: 100, start_z: 30, dx: 0, dy: 1, dz: 0, born_at: 0, lifetime: 5, speed: 50}

// Keep the pure scene value stable while handlers retain native node handles.
let TREE = <main id:"probe",
        <button id:"spawn", "Spawn"> <button id:"turn", "Turn"> <button id:"clear", "Clear">
        <div id:"viewport", style:"width:640px;height:336px;perspective:320px;position:relative;overflow:hidden;",
            <div id:"scene", style:"position:absolute;left:320px;top:168px;transform:translateZ(320px);">
        >
    >
view <effect_probe> state live: [], angle: 0.0 { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#probe")
    if (owner == null) { return 'pass' }
    let command = dom.get_attribute(evt.target, "id")
    let camera = dom.get_element_by_id(owner, "scene")
    if (command == "spawn") {
        live = effects.synchronize(camera, live, {time: 0, projectiles: [PROJECTILE], particles: PARTICLES}, angle, BASE)^
    } else if (command == "turn") {
        angle = 0.25
        live = effects.synchronize(camera, live, {time: 0.1, projectiles: [PROJECTILE], particles: PARTICLES}, angle, BASE)^
    } else if (command == "clear") {
        live = effects.synchronize(camera, live, {time: 2, projectiles: [], particles: []}, angle, BASE)^
    }
    return 'handled'
}

<html <head <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>> <body apply(<effect_probe rendered:TREE>)>>
