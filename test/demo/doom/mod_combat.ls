// Weapons, damage and projectile rules translated from cssDOOM (GPL-2.0).
import geo: .mod_geometry
import world: .mod_world
import game_state: .mod_state
import player: .mod_player
import pickups: .mod_pickups
import mechanics: .mod_mechanics
import events: .mod_events
import weapons: .mod_weapons

pub fn actor(game, id) => [for (entry in game.actors where entry.id == id) entry][0]
pub fn replace_actor(game, next) => {*: game, actors: [for (entry in game.actors)
    if (entry.id == next.id) next else entry]}
pub fn target(game, id) => if (id == -1) game.player else actor(game, id)
pub fn equip(game, slot, rules) => if (rules.WEAPONS[string(slot)] == null or
    not contains(game.player.weapons, slot)) game else weapons.changed(game, slot)
pub fn roll_damage(game, kind) {
    let sample = game_state.roll(game.seed, if (kind == "melee") 10 else if (kind == "rocket") 8 else 3)
    {seed: sample.seed, value: sample.value * (if (kind == "melee")
        (if (pickups.active(game.player, "berserk", game.time)) 20 else 2)
        else if (kind == "rocket") 20 else 5)}
}
fn damage_player(game, amount) {
    let player_state = game.player
    if (player_state.health <= 0 or pickups.active(player_state, "invulnerability", game.time)) game
    else {
        let damage = if (game.skill == 1) floor(amount / 2) else amount
        let saved = if (player_state.armor_type == 0) 0 else min(player_state.armor,
            floor(damage / (if (player_state.armor_type == 1) 3 else 2)))
        let health = max(0, player_state.health - damage + saved)
        let next = events.sound({*: game, mode: if (health == 0) "dead" else game.mode,
            player: {*: player_state, health: health, armor: player_state.armor - saved,
                armor_type: if (player_state.armor <= saved) 0 else player_state.armor_type,
                dead_at: if (health == 0) game.time else player_state.dead_at, flash_until: game.time + 0.3}}, "DSPLPAIN")
        if (health == 0) events.sound(next, "DSPLDETH") else next
    }
}
pub fn splash(map_data, game, position, amount, source, rules, exclude_id = null) {
    let targets = [for (entry in [{*: game.player, id: -1}, *game.actors],
        let radius = if (entry.id == -1) rules.PLAYER_RADIUS else player.actor_radius(entry, rules),
        let distance = max(0, max(abs(entry.x - position.x), abs(entry.y - position.y)) - radius)
        where entry.id != exclude_id and (entry.id == -1 or
            (not entry.collected and contains(rules.SHOOTABLE, entry.type))) and distance < amount and
            world.visible(map_data, game, position, entry, rules)) events.damage(entry.id, amount - distance, source)]
    events.with_events(game, targets)
}
fn damage_actor(map_data, game, victim, amount, source, rules) {
    if (victim == null or victim.collected or victim.hp <= 0) game else {
        let hp = victim.hp - amount
        let sample = if (hp > 0 and victim.ai != null) game_state.roll(game.seed, 256)
            else {value: 256, seed: game.seed}
        let infighting = source is int and source != victim.id and victim.ai.threshold <= 0
        let next_ai = if (victim.ai == null) null else {*: victim.ai,
            state: if (hp <= 0) "dead" else if (sample.value <= victim.ai.painChance) "pain" else victim.ai.state,
            state_time: if (hp <= 0 or sample.value <= victim.ai.painChance) 0 else victim.ai.state_time,
            target: if (infighting) source else victim.ai.target,
            threshold: if (infighting) rules.INFIGHTING_THRESHOLD else victim.ai.threshold}
        let next = replace_actor({*: game, seed: sample.seed}, {*: victim, hp: hp, ai: next_ai,
            collected: hp <= 0, dead_at: if (hp <= 0) game.time else null,
            respawn_at: if (hp <= 0 and game.skill == 5 and victim.ai != null) game.time + 12 else null})
        if (hp <= 0 and victim.type == 2035)
            splash(map_data, events.sound(events.with_events(next, [{kind: "explosion", position: victim, at: game.time}]),
                "DSBAREXP", victim), victim, rules.BARREL_EXPLOSION_DAMAGE, "player", rules, victim.id)
        else if (hp <= 0) {
            let dead = events.sound(next, "DSPODTH1", victim)
            if (victim.type == 3003 and game.level_name == "E1M8" and
                not any([for (entry in dead.actors) entry.type == 3003 and not entry.collected]))
                reduce([dead, *[for (lift in dead.lifts where lift.tag == 666) lift.sectorIndex]],
                    (acc, sector_id) => mechanics.lower_lift(acc, sector_id, rules))
            else dead
        }
        else if (victim.type != 2035) events.sound(next, "DSPOPAIN", victim) else next
    }
}
fn apply_damage(map_data, game, hit, rules) => if (hit.target == -1) damage_player(game, hit.amount)
    else damage_actor(map_data, game, actor(game, hit.target), hit.amount, hit.source, rules)
pub fn resolve_damage(map_data, game, rules) {
    let hit_index = [for (i in 0 to (len(game.effects) - 1) where game.effects[i].kind == "damage") i][0]
    if (hit_index == null) game else {
        let hit = game.effects[hit_index]
        let remaining = {*: game, effects: [for (i in 0 to (len(game.effects) - 1) where i != hit_index) game.effects[i]]}
        resolve_damage(map_data, apply_damage(map_data, remaining, hit, rules), rules)
    }
}
pub fn damage(map_data, game, target_id, amount, source, rules) =>
    resolve_damage(map_data, events.with_events(game, [events.damage(target_id, amount, source)]), rules)

fn hitscan_target(game, direction, distance_limit, rules) => sort([for (entry in game.actors,
    let distance = math.sqrt(geo.distance_squared(game.player, entry)),
    let dot = if (distance == 0) 1 else ((entry.x - game.player.x) * direction.x +
        (entry.y - game.player.y) * direction.y) / distance
    where not entry.collected and contains(rules.SHOOTABLE, entry.type) and distance <= distance_limit and dot >= 0.99)
    {actor: entry, distance: distance}], (hit) => hit.distance)[0].actor
fn puff(map_data, game, position, target_hit, rules) {
    let distance = math.sqrt(geo.distance_squared(game.player, position))
    let point = if (distance <= 1) position else {x: position.x + (game.player.x - position.x) * 8 / distance,
        y: position.y + (game.player.y - position.y) * 8 / distance}
    let floor_height = world.floor_height(map_data, game, if (target_hit) position else point)
    events.with_events(game, [{kind: "puff", position: {*: point,
        z: floor_height + rules.EYE_HEIGHT * (if (target_hit) 0.5 else 1)}, at: game.time}])
}
fn shot(map_data, game, direction, weapon, rules) {
    let victim = hitscan_target(game, direction, weapon.range, rules)
    if (victim != null and world.visible(map_data, game, game.player, victim, rules)) {
        let sample = roll_damage(game, if (weapon.damageType == "pellets") "hitscan" else weapon.damageType)
        let hit = damage(map_data, {*: game, seed: sample.seed}, victim.id, sample.value, "player", rules)
        if (weapon.hitscan) puff(map_data, hit, victim, true, rules) else hit
    } else if (weapon.hitscan) {
        let wall_hit = world.ray_hit(map_data, game, game.player, direction, game.player.z, weapon.range)
        if (wall_hit == null) game else puff(map_data, game,
            {x: game.player.x + direction.x * wall_hit.distance, y: game.player.y + direction.y * wall_hit.distance}, false, rules)
    } else game
}
fn pellet(map_data, game, weapon, rules) {
    let a = game_state.roll(game.seed, 256)
    let b = game_state.roll(a.seed, 256)
    let angle = game.player.angle + (a.value - b.value) / 255 * math.pi / 8
    shot(map_data, {*: game, seed: b.seed}, geo.forward(angle), weapon, rules)
}
pub fn spawn_projectile(game, origin, destination, definition, source, rules) {
    let distance = math.sqrt(geo.distance_squared(origin, destination) + (destination.z - origin.z) ** 2)
    let projectile = {*: definition, id: game.next_projectile_id, source: source,
        start_x: origin.x, start_y: origin.y, start_z: origin.z, x: origin.x, y: origin.y, z: origin.z,
        dx: (destination.x - origin.x) / max(0.0001, distance),
        dy: (destination.y - origin.y) / max(0.0001, distance),
        dz: (destination.z - origin.z) / max(0.0001, distance), born_at: game.time, lifetime: 5}
    events.sound({*: game, projectiles: [*game.projectiles, projectile],
        next_projectile_id: game.next_projectile_id + 1}, definition.sound, origin)
}
pub fn fire(map_data, game, rules) {
    let weapon = rules.WEAPONS[string(game.player.weapon)]
    if (game.player.health <= 0 or weapons.switching(game) or game.time < game.player.next_fire or weapon == null or
        (weapon.ammoType != null and game.player.ammo[weapon.ammoType] < weapon.ammoPerShot)) game
    else {
        let firing = {*: game, shot_at: game.time, alerted_sectors: world.sound_alert(map_data, game),
            player: {*: game.player, next_fire: game.time + weapon.fireRate / 1000,
            ammo: if (weapon.ammoType == null) game.player.ammo else {*: game.player.ammo,
                [weapon.ammoType]: game.player.ammo[weapon.ammoType] - weapon.ammoPerShot}}}
        let direction = geo.forward(game.player.angle)
        if (weapon.damageType == "rocket") {
            let sample = roll_damage(firing, "rocket")
            let origin = {*: game.player, z: game.player.floor + rules.EYE_HEIGHT * 0.8}
            spawn_projectile({*: firing, seed: sample.seed}, origin,
                {x: origin.x + direction.x, y: origin.y + direction.y, z: origin.z},
                {speed: rules.PLAYER_ROCKET_SPEED, damage: sample.value, hitSound: "DSBAREXP", sound: weapon.sound,
                    sprite: "MISLA1", size: 11, is_player_rocket: true}, "player", rules)
        }
        else {
            let audible = events.sound(firing, weapon.sound, game.player)
            if (weapon.damageType == "pellets") reduce([audible, *[for (i in 1 to weapon.pellets) i]],
                (acc, unused) => pellet(map_data, acc, weapon, rules))
            else shot(map_data, audible, direction, weapon, rules)
        }
    }
}
fn projectile_tick(map_data, game, projectile, rules) {
    let elapsed = game.time - projectile.born_at
    let next = {*: projectile, x: projectile.start_x + projectile.dx * projectile.speed * elapsed,
        y: projectile.start_y + projectile.dy * projectile.speed * elapsed,
        z: projectile.start_z + projectile.dz * projectile.speed * elapsed}
    let distance = math.sqrt(geo.distance_squared(projectile, next))
    let horizontal = if (distance <= 0) {x: 0, y: 0} else
        {x: (next.x - projectile.x) / distance, y: (next.y - projectile.y) / distance}
    let wall_hit = if (distance <= 0) null else world.ray_hit(map_data, game, projectile,
        horizontal, next.z, distance)
    let floor_height = world.floor_height(map_data, game, next)
    let victim = if (not projectile.is_player_rocket and geo.distance_squared(next, game.player) < 24 ** 2)
        {*: game.player, id: -1} else [for (entry in game.actors,
            let radius = if (entry.ai == null) rules.ENEMY_RADIUS else entry.ai.radius
            where not entry.collected and entry.id != projectile.source and contains(rules.SHOOTABLE, entry.type) and
                geo.distance_squared(next, entry) < (24 + radius) ** 2) entry][0]
    let impact = wall_hit != null or next.z <= floor_height or victim != null
    let removed = {*: game, projectiles: [for (entry in game.projectiles where entry.id != projectile.id) entry]}
    if (elapsed >= projectile.lifetime) removed
    else if (impact) {
        // The ray parameter measures horizontal distance, while projectile
        // direction is normalized in 3D; use the ray's own unit direction.
        let position = if (wall_hit != null) {x: projectile.x + horizontal.x * (wall_hit.distance - 25),
            y: projectile.y + horizontal.y * (wall_hit.distance - 25), z: projectile.z}
            else {*: next, z: max(next.z, floor_height)}
        let exploded = events.sound(events.with_events(removed,
            [{kind: "explosion", position: position, at: game.time}]), projectile.hitSound, position)
        let sample = if (victim != null and not projectile.is_player_rocket) game_state.roll(game.seed, 8)
            else {seed: game.seed, value: 0}
        let damaged = if (victim == null) exploded else damage(map_data, {*: exploded, seed: sample.seed}, victim.id,
            if (projectile.is_player_rocket) projectile.damage else sample.value * projectile.missileDamage,
            projectile.source, rules)
        if (projectile.is_player_rocket) resolve_damage(map_data,
            splash(map_data, damaged, position, rules.ROCKET_SPLASH_DAMAGE, "player", rules), rules) else damaged
    }
    else {*: game, projectiles: [for (entry in game.projectiles) if (entry.id == next.id) next else entry]}
}
pub fn projectiles(map_data, game, rules) => reduce([game, *game.projectiles],
    (acc, projectile) => projectile_tick(map_data, acc, projectile, rules))
