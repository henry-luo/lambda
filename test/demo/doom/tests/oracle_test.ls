// Portable results produced by the clean upstream checkout, never JS at runtime.
import fixture: .mod_fixture
import geo: ~~.mod_geometry
import combat: ~~.mod_combat
import pickups: ~~.mod_pickups
import game_state: ~~.mod_state
import mechanics: ~~.mod_mechanics
let oracle = input("test/demo/doom/reference/upstream-oracle.json", 'json')^
fn point(args, offset = 0) => {x: args[offset], y: args[offset + 1]}
fn geometry_case(entry) {
    let args = entry.args
    let actual = if (entry.kind == "polygon") geo.point_in_polygon(point(entry.point), entry.polygon)
        else if (entry.kind == "circle") geo.circle_hits_segment(point(args), args[2], point(args, 3), point(args, 5))
        else geo.ray_segment(point(args), point(args, 2), point(args, 4), point(args, 6), args[8]) != null
    actual == entry.expected
}
fn result(game) => {health: game.player.health, armor: game.player.armor,
    armor_type: game.player.armor_type, dead: game.player.health == 0, dead_at: game.player.dead_at,
    sounds: [for (entry in game.effects where entry.kind == "sound") entry.sound]}
let level = fixture.level()
let arena = fixture.map_data(level)
let initial = fixture.game(level)
fn damage_case(entry) {
    let input_data = entry.input
    let game = {*: initial, time: input_data.time, skill: input_data.skill,
        player: {*: initial.player, health: input_data.health, armor: input_data.armor,
            armor_type: input_data.armor_type, dead_at: if (input_data.dead) input_data.time else null,
            powerups: if (input_data.immune) {invulnerability: input_data.time + 10} else {}}}
    result(combat.damage(arena, game, -1, input_data.amount, "oracle", fixture.rules)) == entry.expected
}
// Match the controlled upstream stream's threshold branch; RNG algorithms differ.
let bypass_seed = [for (seed in 1 to 512 where game_state.roll(seed, 256).value <= 5) seed][0]
let protected_seed = [for (seed in 1 to 512 where game_state.roll(seed, 256).value > 5) seed][0]
fn hazard_case(entry) {
    let input_data = entry.input
    let hazard_arena = fixture.map_data(fixture.level([], [], input_data.special))
    let game = {*: initial, seed: if (input_data.random != null and input_data.random < 5 / 256)
        bypass_seed else protected_seed, player: {*: initial.player,
            powerups: if (input_data.suit) {radsuit: 10} else {}}}
    let next = combat.resolve_damage(hazard_arena,
        pickups.hazard(hazard_arena, game, input_data.dt, fixture.rules), fixture.rules)
    let expected = entry.expected
    result(next) == {health: expected.health, armor: expected.armor, armor_type: expected.armor_type,
        dead: expected.dead, dead_at: expected.dead_at, sounds: expected.sounds} and
        abs(next.player.hazard_time - expected.hazard_time) < 0.00000001
}
let door_level = {*: level, doors: [{sectorIndex: 1, floorHeight: 0, closedHeight: 0,
    openHeight: 128, keyRequired: "blue"}]}
let occupied_arena = fixture.map_data({*: level,
    sectorPolygons: [{*: level.sectorPolygons[0], sectorIndex: 1}]})
fn door_step(acc, entry) {
    let advanced = mechanics.advance(if (entry.inside) occupied_arena else arena,
        {*: acc.game, time: entry.at, effects: []}, 0, fixture.rules)
    let keyed = if (entry.action == "key") {*: advanced, player: {*: advanced.player, keys: ["blue"]}}
        else advanced
    let next = if (contains(["key", "use"], entry.action)) mechanics.open_door(keyed, 1, fixture.rules) else keyed
    let door = next.doors[0]
    let actual = {open: contains(["opening", "open"], door.phase), passable: door.passable,
        sounds: [for (event in next.effects where event.kind == "sound") event.sound]}
    {game: next, checks: [*acc.checks, actual == entry.expected]}
}
let door_result = reduce([{game: fixture.game(door_level), checks: []}, *oracle.doors], door_step);
{
    geometry: [for (entry in oracle.geometry) geometry_case(entry)],
    damage: [for (entry in oracle.damage) damage_case(entry)],
    hazards: [for (entry in oracle.hazards) hazard_case(entry)],
    doors: door_result.checks
}
