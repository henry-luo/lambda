// Item and hazard rules translated from cssDOOM; see ATTRIBUTION.md.
import geo: .mod_geometry
import world: .mod_world
import game_state: .mod_state
import events: .mod_events

pub fn active(player, powerup, simulation_time) => (player.powerups[powerup] or 0) > simulation_time
fn ammo(player, kind, amount) => if (kind == null) player else {*: player,
    ammo: {*: player.ammo, [kind]: min(player.max_ammo[kind], player.ammo[kind] + amount)}}
fn apply_item(player, actor, game, rules) {
    let effect = rules.PICKUP_EFFECTS[string(actor.type)]
    let weapon = rules.WEAPON_PICKUPS[string(actor.type)]
    let key_color = rules.KEY_TYPES[string(actor.type)]
    let multiplier = if (game.skill == 1 or game.skill == 5) 2 else 1
    if (key_color != null) {*: player, keys: unique([*player.keys, key_color])}
    else if (effect.statType == "health") {
        let cap = if (actor.type == 2013 or actor.type == 2014) 200 else rules.MAX_HEALTH
        if (player.health >= cap) null else {*: player, health: min(cap, player.health + effect.amount)}
    }
    else if (effect.statType == "armor") {
        let cap = if (effect.armorClass != null) effect.armorClass * 100 else rules.MAX_ARMOR
        if (player.armor >= cap) null else {*: player,
            armor: if (effect.armorClass != null) effect.amount else min(cap, player.armor + effect.amount),
            armor_type: effect.armorClass or player.armor_type or 1}
    }
    else if (effect.statType == "ammo") {
        if (player.ammo[effect.ammoType] >= player.max_ammo[effect.ammoType]) null
        else ammo(player, effect.ammoType, effect.amount * multiplier)
    }
    else if (effect.statType == "powerup") {
        let duration = rules.POWERUP_DURATION[effect.powerup]
        {*: player, powerups: {*: player.powerups, [effect.powerup]:
            if (duration == "infinite") inf else game.time + duration},
            health: if (effect.powerup == "berserk") max(100, player.health) else player.health,
            weapon: if (effect.powerup == "berserk") 1 else player.weapon}
    }
    else if (weapon != null) ammo({*: player, weapons: unique([*player.weapons, weapon.slot]),
        weapon: weapon.slot}, weapon.ammoType, weapon.amount * multiplier)
    else if (actor.type == 8) {
        let expanded = {*: player, backpack: true, max_ammo:
            map([for (kind, capacity at rules.MAX_AMMO) *[kind, capacity * 2]])}
        ammo(ammo(ammo(expanded, "bullets", 10 * multiplier), "shells", 4 * multiplier), "rockets", multiplier)
    }
    else if (contains(rules.PICKUPS, actor.type)) player else null
}
fn collect_one(game, actor, rules) {
    let next = apply_item(game.player, actor, game, rules)
    if (next == null) game else events.sound({*: game,
        player: {*: next, pickup_until: game.time + 0.3},
        actors: [for (entry in game.actors) if (entry.id == actor.id) {*: entry, collected: true} else entry]}, "DSITEMUP")
}
pub fn collect(game, rules) => if (game.player.health <= 0) game else
    reduce([game, *[for (actor in game.actors where not actor.collected and
        geo.distance_squared(game.player, actor) < rules.PICKUP_RANGE ** 2) actor]],
        (acc, actor) => collect_one(acc, actor, rules))

pub fn expire(game) => {*: game, player: {*: game.player, powerups:
    map([for (powerup, deadline at game.player.powerups where deadline > game.time) *[powerup, deadline]])}}
pub fn hazard(map_data, game, dt, rules) {
    let sectors = [for (sector in world.sectors_at(map_data, game.player),
        let lift = world.motion(game.lifts, sector.sectorIndex))
        {*: sector, effective_floor: if (lift == null) sector.floorHeight else lift.height}]
    let sector = sort(sectors, (entry) => -entry.effective_floor)[0]
    let damage = rules.SECTOR_DAMAGE[string(sector.specialType)] or 0
    let suit = active(game.player, "radsuit", game.time)
    let sample = if (suit and contains([4, 16], sector.specialType)) game_state.roll(game.seed, 256)
        else {value: 256, seed: game.seed}
    let timer = if (damage > 0 and (not suit or sample.value <= 5)) game.player.hazard_time + dt else 0
    let damaging = timer >= 32 / 35
    let next = {*: game, seed: sample.seed, player: {*: game.player,
        hazard_time: timer - (if (damaging) 32 / 35 else 0)}}
    if (damaging) events.with_events(next, [events.damage(-1, damage, "sector")]) else next
}
