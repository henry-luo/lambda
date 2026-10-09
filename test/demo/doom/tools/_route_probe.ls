// Offline route authoring uses the same fixed-step simulation as the native UI.
import data: ~~.mod_data
import game_state: ~~.mod_state
import world: ~~.mod_world
import sim: ~~.mod_sim
import controls: ~~.mod_input
let BASE = url_resolve(data.entry_uri()^, "../")
let RULES = data.rules(BASE)^
let VISUALS = data.visuals(BASE)^
let LEVEL = data.load_level(BASE, "E1M1")^
let WORLD = world.prepare(LEVEL)
let INITIAL = sim.initialize(WORLD, {*: game_state.new_game(LEVEL, RULES, VISUALS),
    level_name: "E1M1", mode: "playing"})
let REQUEST = input("temp/doom/route-request.json", 'json')^
let ROUTE = REQUEST.segments or REQUEST
fn move_segment(acc, segment) {
    let input_state = reduce([controls.empty(), *segment.keys],
        (held, key) => controls.set_key(held, key, true))
    let result = reduce([acc.game, *[for (i in 1 to segment.ticks) i]],
        (game, tick) => sim.step(WORLD, {*: game, effects: []},
            if (tick == 1) input_state else controls.consume(input_state), sim.STEP, RULES))
    let row = {label: segment.label, x: result.player.x, y: result.player.y,
        floor: result.player.floor, angle: result.player.angle, ticks: result.ticks,
        health: result.player.health, ammo: result.player.ammo, weapon: result.player.weapon,
        kills: len([for (actor in result.actors where actor.ai != null and actor.collected) actor]),
        pickups: len([for (actor in result.actors where actor.ai == null and actor.collected) actor]),
        doors: [for (door in result.doors) {id: door.sectorIndex, passable: door.passable, height: door.height}],
        lifts: [for (lift in result.lifts) {id: lift.sectorIndex, phase: lift.phase, height: lift.height}],
        transition: result.transition}
    {game: result, rows: [*acc.rows, row]}
}
let result = reduce([{game: REQUEST.initial or INITIAL, rows: []}, *ROUTE], move_segment);
format(result, 'json')
