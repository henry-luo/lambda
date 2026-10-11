// A small reactive player: seek visible enemies/pickups and turn away from walls.
import controls: .mod_input
import geo: .mod_geometry
import world: .mod_world
import movement: .mod_player

fn first_visible(map_data, game, candidates, index, rules) {
    if (index >= len(candidates)) null
    else if (world.visible(map_data, game, game.player, candidates[index], rules)) candidates[index]
    else first_visible(map_data, game, candidates, index + 1, rules)
}
pub fn decide(map_data, game, dt, rules) {
    let player = game.player
    let targets = sort([for (actor in game.actors where not actor.collected and
        ((actor.ai != null and actor.hp > 0) or contains(rules.PICKUPS, actor.type)) and
        geo.distance_squared(player, actor) < rules.MAX_RENDER_DISTANCE ** 2 and
        (actor.ai != null or geo.distance_squared(player, actor) > rules.PICKUP_RANGE ** 2)) actor],
        (actor) => (if (actor.ai != null) 0 else 1000000) + geo.distance_squared(player, actor))
    // Stop at the first visible nearby candidate instead of scanning the whole map.
    let target = first_visible(map_data, game, slice(targets, 0, 8), 0, rules)
    let desired = if (target == null) player.angle else math.atan2(player.x - target.x, target.y - player.y)
    let delta = math.atan2(math.sin(desired - player.angle), math.cos(desired - player.angle))
    let turn = max(-rules.TURN_SPEED * dt, min(rules.TURN_SPEED * dt, delta))
    let forward = geo.forward(player.angle + turn)
    let probe = {x: player.x + forward.x * 48, y: player.y + forward.y * 48}
    let clear = movement.can_move(map_data, game, player, probe, rules.PLAYER_RADIUS, player.floor, rules, rules.MAX_STEP_HEIGHT)
    let enemy = target != null and target.ai != null
    let weapon = rules.WEAPONS[string(player.weapon)]
    let close = enemy and geo.distance_squared(player, target) < min(140, weapon.range * 0.65) ** 2
    let empty_ammo = weapon.ammoType != null and player.ammo[weapon.ammoType] < weapon.ammoPerShot
    // Generate use edges periodically so doors still activate after an earlier miss.
    {*:controls.empty(), held: [if (clear and not close) "forward",
        if (not clear) "turn_left", if (enemy and abs(delta) < 0.15) "fire"],
        edges: [if (game.ticks % 17 == 0) "use", if (empty_ammo) "weapon_1"],
        mouse_x: if (clear) -turn / 0.002 else 0.0}
}
