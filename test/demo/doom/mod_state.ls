// Game values replace upstream shared mutable state (S9.1.7, S9.1.4).
import controls: .mod_input
import world: .mod_world
import camera: .mod_camera

pub fn roll(seed, sides) {
    let sample = math.random(seed)
    {value: 1 + int(floor(sample[0] * sides)), seed: sample[1]}
}
pub fn enabled(thing, skill) => band(thing.flags, 16) == 0 and
    band(thing.flags, if (skill <= 2) 1 else if (skill == 3) 2 else 4) != 0
fn ai_state(stats, thing, skill, offset) => if (stats == null) null else {
    *: stats, state: "idle", state_time: 0.0, los_timer: offset,
    last_attack: 0.0, damage_dealt: false, reaction_timer: 0.0,
    ambush: band(thing.flags, 8) != 0, target: -1, threshold: 0.0,
    speed: stats.speed * (if (skill == 5) 2 else 1),
    reactionTime: stats.reactionTime / (if (skill == 5) 2 else 1),
    attackDuration: stats.attackDuration / (if (skill == 5) 2 else 1),
    painDuration: stats.painDuration / (if (skill == 5) 2 else 1),
    cooldown: stats.cooldown / (if (skill == 5) 2 else 1)
}
fn spawn_actor(acc, thing, rules, skill) {
    let stats = rules.ENEMY_AI_STATS[string(thing.type)]
    let sample = if (stats != null) math.random(acc.seed) else [0.0, acc.seed]
    let hp = rules.THING_HEALTH[string(thing.type)] or 0
    let actor = {*: thing, spawn_x: thing.x, spawn_y: thing.y, collected: false, hp: hp, max_hp: hp,
        solid_radius: rules.SOLID_THING_RADIUS[string(thing.type)] or 0,
        facing: thing.angle * math.pi / 180, dead_at: null,
        ai: ai_state(stats, thing, skill, sample[0] * rules.LINE_OF_SIGHT_CHECK_INTERVAL)}
    {actors: [*acc.actors, actor], seed: sample[1]}
}
pub fn new_game(level, rules, visuals, skill = 1, seed = 1) {
    let things = [for (i in 0 to (len(level.things) - 1), let thing = level.things[i]
        where enabled(thing, skill) and (visuals.THING_SPRITES[string(thing.type)] != null or
            visuals.THING_NAMES[string(thing.type)] != null)) {*: thing, id: i}]
    let spawned = reduce([{actors: [], seed: seed}, *things],
        (acc, thing) => spawn_actor(acc, thing, rules, skill))
    let boss_floors = [for (i in 0 to (len(level.sectors) - 1), let sector = level.sectors[i],
        let lowest_floor = if (sector.tag == 666) world.lowest_adjacent(level, i) else sector.floorHeight
        where sector.tag == 666 and lowest_floor < sector.floorHeight)
        {sectorIndex: i, tag: 666, upperHeight: sector.floorHeight, lowerHeight: lowest_floor,
            oneWay: true, duration: 2, collisionEdges: []}]
    {mode: "ready", autoplay: false, skill: skill, time: 0.0, ticks: 0, seed: spawned.seed, initial_seed: seed,
        player: {x: level.playerStart.x, y: level.playerStart.y,
            floor: level.playerStart.floorHeight or 0,
            z: (level.playerStart.floorHeight or 0) + rules.EYE_HEIGHT,
            angle: level.playerStart.angle - math.pi / 2, pitch: 0.0,
            health: 100, armor: 0, armor_type: 0,
            ammo: {bullets: 50, shells: 0, rockets: 0, cells: 0}, max_ammo: rules.MAX_AMMO,
            backpack: false, weapon: 2, weapon_from: 2, switch_at: null, weapons: [1, 2], keys: [], powerups: {},
            dead_at: null, next_fire: 0.0, hazard_time: 0.0, flash_until: 0.0},
        actors: spawned.actors, projectiles: [], next_projectile_id: 0,
        doors: [for (door in level.doors) {*: door, phase: "closed", height: door.closedHeight,
            passable: false, deadline: 0.0, moved_at: 0.0}],
        lifts: [for (lift in [*level.lifts, *boss_floors] where lift.upperHeight > lift.lowerHeight)
            {*: lift, phase: "raised", height: lift.upperHeight, deadline: 0.0, moved_at: 0.0,
                move_from: lift.upperHeight}],
        crushers: [for (crusher in level.crushers where crusher.topHeight > crusher.crushHeight)
            {*: crusher, height: crusher.topHeight, direction: -1, active: false, damage_time: 0.0}],
        triggered: [], switches: [], teleport_until: 0.0, transition: null,
        input: controls.empty(), effects: [], next_effect_id: 0, spectator: "player", camera_control: camera.initial("player"), generation: 1,
        alerted_sectors: [], last_alert: 0.0}
}
pub fn transition(game, level, rules, visuals, map_name) {
    let fresh = new_game(level, rules, visuals, game.skill, game.seed)
    {*: fresh, mode: "playing", autoplay: game.autoplay, generation: game.generation + 1,
        player: {*: fresh.player, health: game.player.health, armor: game.player.armor,
            armor_type: game.player.armor_type, ammo: game.player.ammo, max_ammo: game.player.max_ammo,
            backpack: game.player.backpack, weapons: game.player.weapons, weapon: game.player.weapon},
        level_name: map_name}
}
