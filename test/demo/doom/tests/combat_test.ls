import fixture: .mod_fixture
import combat: ~~.mod_combat
import pickups: ~~.mod_pickups
import world: ~~.mod_world
let rules = fixture.rules
let level = fixture.level([fixture.thing(3004)])
let arena = fixture.map_data(level)
let game = fixture.game(level)
let first = combat.fire(arena, game, rules)
let cooldown = combat.fire(arena, first, rules)
let second = combat.fire(arena, {*: first, time: 1}, rules)
let third = combat.fire(arena, {*: second, time: 2}, rules)
let wall = {start: {x: -50, y: 50}, end: {x: 50, y: 50}, isSolid: true, bottomHeight: 0, topHeight: 128}
let blocked_arena = fixture.map_data(fixture.level(level.things, [wall]))
let blocked = combat.fire(blocked_arena, game, rules)
let armored = {*: game, player: {*: game.player, armor: 30, armor_type: 1}}
let hurt = combat.damage(arena, armored, -1, 30, "enemy", rules)
let easy = combat.damage(arena, {*: armored, skill: 1}, -1, 30, "enemy", rules)
let immune = combat.damage(arena, {*: armored, player: {*: armored.player, powerups: {invulnerability: 10}}},
    -1, 1000, "enemy", rules)
let dead = combat.damage(arena, game, -1, 1000, "enemy", rules)
let barrel_level = fixture.level([fixture.thing(2035, 0, 200), fixture.thing(2035, 40, 200), fixture.thing(3004, 80, 200)])
let barrel_arena = fixture.map_data(barrel_level)
let exploded = combat.damage(barrel_arena, fixture.game(barrel_level), 0, 20, "player", rules)
let infighting = combat.damage(arena, game, 0, 1, 12, rules)
let launcher = {*: game, player: {*: game.player, weapon: 5, weapons: [1, 2, 5],
    ammo: {*: game.player.ammo, rockets: 2}}}
let rocket = combat.fire(arena, launcher, rules)
let impact = combat.projectiles(arena, {*: rocket, time: 0.1}, rules);
{
    shot: [first.player.ammo.bullets, first.actors[0].hp, second.actors[0].hp,
        cooldown.player.ammo.bullets == first.player.ammo.bullets, third.actors[0].collected],
    blocked: [blocked.actors[0].hp, world.visible(blocked_arena, game, game.player, game.actors[0], rules)],
    armor: [hurt.player.health, hurt.player.armor, hurt.player.armor_type,
        easy.player.health, easy.player.armor, immune.player.health],
    death: [dead.mode, dead.player.health, dead.player.dead_at, dead.effects[len(dead.effects) - 1].sound],
    barrels: [for (entry in exploded.actors) entry.collected],
    infighting: [infighting.actors[0].ai.target, infighting.actors[0].ai.threshold],
    rocket: [rocket.player.ammo.rockets, len(rocket.projectiles), len(impact.projectiles), impact.actors[0].collected]
}
