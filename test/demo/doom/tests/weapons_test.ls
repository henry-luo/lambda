import fixture: .mod_fixture
import weapons: ~~.mod_weapons
import combat: ~~.mod_combat
import controls: ~~.mod_input
let images = input("test/demo/doom/data/images.json", 'json')^
let level = fixture.level()
let arena = fixture.map_data(level)
let initial = fixture.game(level)
let equipped = combat.equip({*: initial, player: {*: initial.player, weapons: [1, 2, 3],
    ammo: {*: initial.player.ammo, shells: 4}}}, 3, fixture.rules)
let early = combat.fire(arena, equipped, fixture.rules)
let late = combat.fire(arena, {*: equipped, time: 0.4}, fixture.rules)
let moving = {*: initial, input: controls.set_key(controls.empty(), "w", true)};
[
    weapons.displayed_slot(equipped), weapons.displayed_slot({*: equipped, time: 0.19}),
    weapons.displayed_slot({*: equipped, time: 0.2}), weapons.switching({*: equipped, time: 0.4}),
    early.player.ammo.shells, late.player.ammo.shells,
    weapons.pose(late, fixture.rules, images).firing,
    weapons.pose(moving, fixture.rules, images).animation,
    weapons.pose({*: initial, mode: "dead"}, fixture.rules, images).hidden,
    weapons.pose({*: initial, mode: "paused"}, fixture.rules, images).play_state,
    [for (health in [100, 80, 79, 60, 59, 40, 39, 20, 19, 0]) weapons.face_row(health)],
    weapons.pose({*: initial, player: {*: initial.player, powerups: {invisibility: 1}}}, fixture.rules, images).opacity,
    weapons.viewport_filter({*: initial, player: {*: initial.player, powerups: {radsuit: 1}}})
]
