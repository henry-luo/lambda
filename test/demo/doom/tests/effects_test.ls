import fixture: .mod_fixture
import events: ~~.mod_events
import sim: ~~.mod_sim
import sprites: ~~.mod_sprites
import combat: ~~.mod_combat
let level = fixture.level([])
let arena = fixture.map_data(level)
let game = sim.initialize(arena, fixture.game(level))
let emitted = events.with_events(game, [for (kind in ["puff", "explosion", "fog"])
    {kind: kind, at: 0, position: {x: 100, y: 100, z: 41}}])
let audible = events.sound(emitted, "DSPISTOL")
let first = sim.step(arena, emitted, game.input, 0.1, fixture.rules)
let second = sim.step(arena, first, game.input, 0.1, fixture.rules)
let third = sim.step(arena, second, game.input, 0.1, fixture.rules)
let expired = sim.step(arena, {*: third, time: 1.7}, game.input, 0.02, fixture.rules)
let wall = {start: {x: 50, y: -100}, end: {x: 50, y: 100}, isSolid: true, bottomHeight: 0, topHeight: 128}
let wall_arena = fixture.map_data(fixture.level([], [wall]))
let missile = combat.spawn_projectile(game, {x: 0, y: 0, z: 41}, {x: 100, y: 0, z: 141},
    {speed: 1000, damage: 20, hitSound: "DSBAREXP", sound: "DSRLAUNC", sprite: "MISLA1", size: 11, is_player_rocket: true}, "player", fixture.rules)
let impact = combat.projectiles(wall_arena, {*: missile, time: 0.1}, fixture.rules)
let explosion = [for (event in impact.effects where event.kind == "explosion") event][0];
{
    identities: [for (event in audible.effects) event.id], next_id: audible.next_effect_id,
    lifetimes: [len(first.particles), len(second.particles), len(third.particles), len(expired.particles)],
    transient_keys: [for (entry in sprites.transients(first)) entry.key],
    wall_impact: [round(explosion.position.x), round(explosion.position.y), len(impact.projectiles)]
}
