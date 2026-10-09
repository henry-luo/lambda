import fixture: .mod_fixture
import ai: ~~.mod_ai
import combat: ~~.mod_combat
let rules = fixture.rules
let level = fixture.level([fixture.thing(3004)])
let arena = fixture.map_data(level)
let game = fixture.game(level)
let awake = ai.advance(arena, {*: game, time: 0.2}, 0.2, rules)
let delayed = ai.advance(arena, {*: awake, time: 0.4}, 0.2, rules)
let chase = ai.advance(arena, {*: delayed, time: 1.6}, 0.2, rules)
let attacking = ai.advance(arena, {*: chase, time: 1.8}, 0.2, rules)
let attacked = ai.advance(arena, {*: attacking, time: 2.1}, 0.3, rules)
let completed = ai.advance(arena, {*: attacked, time: 2.3}, 0.2, rules)
let wall = {start: {x: -50, y: 50}, end: {x: 50, y: 50}, isSolid: true, bottomHeight: 0, topHeight: 128}
let blocked = ai.advance(fixture.map_data(fixture.level(level.things, [wall])), game, 0.2, rules)
let demon_level = fixture.level([fixture.thing(3002, 0, 50)])
let demon_arena = fixture.map_data(demon_level)
let demon_game = fixture.game(demon_level)
let melee = {*: demon_game, time: 1, actors: [{*: demon_game.actors[0],
    ai: {*: demon_game.actors[0].ai, state: "attacking", state_time: 0.0, attack_is_melee: true}}]}
let hit = ai.advance(demon_arena, melee, 0.2, rules)
let again = ai.advance(demon_arena, hit, 0.01, rules)
let nightmare = combat.damage(arena, fixture.game(level, 5), 0, 100, "player", rules)
let respawn = ai.advance(arena, {*: nightmare, time: 12}, 1 / 35, rules);
{
    wake: [awake.actors[0].ai.state, awake.actors[0].ai.reaction_timer > 0],
    chase: [delayed.actors[0].y < 100, chase.actors[0].ai.state, attacking.actors[0].ai.state,
        completed.actors[0].ai.state],
    blocked: blocked.actors[0].ai.state,
    melee: [hit.player.health < 100, hit.player.health == again.player.health, hit.actors[0].ai.damage_dealt],
    nightmare: [nightmare.actors[0].respawn_at, respawn.actors[0].hp, respawn.actors[0].collected,
        respawn.actors[0].ai.state]
}
