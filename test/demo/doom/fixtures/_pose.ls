// Offline capture fixture: use the actual scene/presenter and export its writes.
import dom
import data: ~~.mod_data
import game_state: ~~.mod_state
import world: ~~.mod_world
import sim: ~~.mod_sim
import scene: ~~.mod_scene
import present: ~~.mod_present
import camera: ~~.mod_camera
import mechanics: ~~.mod_mechanics
let BASE = url_resolve(data.entry_uri()^, "../")
let RULES = data.rules(BASE)^
let VISUALS = data.visuals(BASE)^
let IMAGES = data.read(BASE, "data/images.json")^
let REQUEST = input("temp/doom/pose-request.json", 'json')^
let POSE = [for (entry in data.read(BASE, "reference/poses.json")^.poses where entry.name == REQUEST.name) entry][0]
let LEVEL = data.load_level(BASE, POSE.map)^
let WORLD = world.prepare(LEVEL)
let INITIAL = sim.initialize(WORLD, {*: game_state.new_game(LEVEL, RULES, VISUALS, POSE.skill),
    level_name: POSE.map, mode: "paused", spectator: POSE.camera, camera_control: camera.initial(POSE.camera)})
let LOCATED = {*: INITIAL, player: {*: INITIAL.player, *: (POSE.player or {})}}
let FLOOR = world.floor_height(WORLD, LOCATED, LOCATED.player)
let MOVERS = if (POSE.door != null) mechanics.open_door(LOCATED, POSE.door, RULES)
    else if (POSE.lift != null) mechanics.lower_lift(LOCATED, POSE.lift, RULES) else LOCATED
let GAME = mechanics.advance(WORLD, {*: MOVERS, time: POSE.time,
    player: {*: MOVERS.player, floor: FLOOR, z: FLOOR + RULES.EYE_HEIGHT}}, 0, RULES)
let SCENE = scene.prepare(WORLD, GAME)

pn author_style(node, property, value) bool^ {
    dom.style_set_property(node, property, value)
    if (dom.style_get_property(node, property) == null)
        raise error("Pose export rejected " ++ property)
    return true
}
pn freeze(owner) {
    output({name: POSE.name, base: BASE, html: dom.outer_html(dom.document_element(owner))},
        "temp/doom/pose-export.json", 'json')^
    dom.set_attribute(owner, "data-captured", POSE.name)
}
view <pose_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "doom")
    let handles = present.bind(owner, SCENE, GAME)
    present.paint(owner, SCENE, null, GAME, handles, null, RULES, VISUALS, IMAGES, BASE, author_style)^
    // A paired capture measures the filtered actor against identical geometry.
    if (REQUEST.hide_actor != null)
        author_style(dom.get_element_by_id(owner, "sprite-" ++ string(REQUEST.hide_actor)), "visibility", "hidden")^
    freeze(owner)
    return 'handled'
}
<html <head <meta charset:"UTF-8"> <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>
    <style ("#doom *{animation-play-state:paused!important;animation-delay:-" ++ string(POSE.time) ++
        "s!important}#overlay{display:none!important}")>>
    apply(<pose_document rendered:<body scene.tree(SCENE, GAME, RULES, VISUALS, IMAGES, BASE)>>)>
