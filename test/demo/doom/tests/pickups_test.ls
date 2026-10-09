import fixture: .mod_fixture
import pickups: ~~.mod_pickups
let level = fixture.level([fixture.thing(2011, 0, 0), fixture.thing(2014, 0, 0),
    fixture.thing(2018, 0, 0), fixture.thing(2019, 0, 0), fixture.thing(5, 0, 0),
    fixture.thing(2001, 0, 0), fixture.thing(8, 0, 0), fixture.thing(2024, 0, 0)])
let game = fixture.game(level)
let collected = pickups.collect(game, fixture.rules)
let expired = pickups.expire({*: collected, time: 60})
let hurt = pickups.collect({*: game, player: {*: game.player, health: 50}}, fixture.rules)
let easy = pickups.collect(fixture.game(level, 1), fixture.rules)
let hazard_level = fixture.level([], [], 5)
let hazard_arena = fixture.map_data(hazard_level)
let exposed = pickups.hazard(hazard_arena, fixture.game(hazard_level), 32 / 35, fixture.rules)
let protected = pickups.hazard(hazard_arena, {*: game,
    player: {*: game.player, powerups: {radsuit: 10}}}, 1, fixture.rules);
{
    caps: [collected.player.health, collected.player.armor, collected.player.armor_type,
        collected.actors[0].collected, hurt.player.health],
    inventory: [collected.player.keys, collected.player.weapon, collected.player.weapons,
        collected.player.backpack, collected.player.max_ammo.bullets,
        collected.player.ammo.bullets, collected.player.ammo.shells, easy.player.ammo.shells],
    powerup: [pickups.active(collected.player, "invisibility", 59.9),
        pickups.active(expired.player, "invisibility", 60), len(expired.player.powerups)],
    hazard: [exposed.player.hazard_time, exposed.effects[0].amount, len(protected.effects)]
}
