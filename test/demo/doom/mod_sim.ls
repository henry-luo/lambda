// Fixed gameplay clock; presentation frames never decide collision deadlines.
import game_state: .mod_state
import controls: .mod_input
import world: .mod_world
import player: .mod_player
import mechanics: .mod_mechanics
import combat: .mod_combat
import pickups: .mod_pickups
import ai: .mod_ai
import events: .mod_events
import weapons: .mod_weapons
import camera: .mod_camera

pub let STEP = 1 / 35
pub fn initial_clock() => {last_ms: null, remainder: 0.0, dropped: 0.0}
pub fn clock_frame(previous, timestamp_ms, playing = true) {
    if (not playing) {*: initial_clock(), dropped: previous.dropped, steps: 0}
    else {
        let elapsed = if (previous.last_ms == null) 0 else max(0, (timestamp_ms - previous.last_ms) / 1000)
        let accumulated = previous.remainder + elapsed
        let available = int(floor((accumulated + 0.000000001) / STEP))
        {last_ms: timestamp_ms, remainder: max(0, accumulated - available * STEP),
            steps: min(4, available), dropped: previous.dropped + max(0, available - 4) * STEP}
    }
}
pub fn initialize(map_data, game) => {*: game, particles: [], actors: [for (entry in game.actors)
    {*: entry, floor: world.floor_height(map_data, game, entry)}]}
pub fn action(map_data, game, command, rules, visuals) {
    if (command == "restart") initialize(map_data, {*: game_state.new_game(map_data, rules, visuals, game.skill, game.initial_seed or 1),
        mode: "playing", level_name: game.level_name, generation: game.generation + 1})
    else if (command == "pause") {*: game, mode: if (game.mode == "playing") "paused"
        else if (game.mode == "ready" or game.mode == "paused") "playing" else game.mode, input: controls.empty()}
    else if (command == "spectator") camera.cycle(game)
    else game
}
pub fn step(map_data, game, input_state, dt, rules) {
    if (game.mode != "playing") game else {
        let timed = {*: game, time: game.time + dt, ticks: game.ticks + 1}
        let observed = camera.advance(mechanics.advance(map_data, timed, dt, rules), input_state, dt)
        let moving = if (game.spectator == "top") observed else player.move(map_data, observed, input_state, dt, rules)
        let triggered = mechanics.teleport(map_data, mechanics.walk_triggers(map_data, moving, game.player, rules), game.player, rules)
        let used = if (contains(input_state.edges, "use")) mechanics.use(map_data, triggered, rules) else triggered
        let equipped = reduce([used, *[for (edge in input_state.edges where starts_with(edge, "weapon_")) int(slice(edge, 7))]],
            (acc, slot) => combat.equip(acc, slot, rules))
        let picked = pickups.collect(pickups.expire(equipped), rules)
        let collected = if (picked.player.weapon == equipped.player.weapon) picked else
            weapons.changed({*: picked, player: {*: picked.player, weapon: equipped.player.weapon}}, picked.player.weapon)
        let firing = if (contains(input_state.held, "fire")) combat.fire(map_data, collected, rules) else collected
        let enemies = ai.advance(map_data, firing, dt, rules)
        let result = combat.resolve_damage(map_data,
            pickups.hazard(map_data, combat.projectiles(map_data, enemies, rules), dt, rules), rules)
        let particles = unique([for (effect in [*(game.particles or []), *result.effects],
            let lifetime = events.visual_lifetime(effect.kind)
            where lifetime > 0 and result.time - effect.at < lifetime) effect])
        {*: result, particles: particles}
    }
}
pub fn frame(map_data, game, previous_clock, timestamp_ms, rules) {
    let next_clock = clock_frame(previous_clock, timestamp_ms, game.mode == "playing")
    let stepped = reduce([{game: {*: game, effects: []}, input: game.input}, *[for (i in 1 to next_clock.steps) i]],
        (acc, unused) => {game: step(map_data, acc.game, acc.input, STEP, rules), input: controls.consume(acc.input)})
    {clock: next_clock, game: {*: stepped.game, input: if (stepped.game.mode == "playing") stepped.input else controls.empty()}}
}
