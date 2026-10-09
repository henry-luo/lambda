// Enemy state machine and eight-direction chase translated from cssDOOM.
import geo: .mod_geometry
import world: .mod_world
import movement: .mod_player
import game_state: .mod_state
import combat: .mod_combat
import pickups: .mod_pickups
import events: .mod_events

let DX = [1, 0.7071, 0, -0.7071, -1, -0.7071, 0, 0.7071]
let DY = [0, 0.7071, 1, 0.7071, 0, -0.7071, -1, -0.7071]
fn change(enemy, phase) => {*: enemy, ai: {*: enemy.ai, state: phase, state_time: 0.0, damage_dealt: false}}
fn can_walk(map_data, game, enemy, direction, rules) => direction < 8 and
    movement.can_move(map_data, game, enemy,
        {x: enemy.x + DX[direction] * enemy.ai.speed * enemy.ai.chaseTics / 35,
            y: enemy.y + DY[direction] * enemy.ai.speed * enemy.ai.chaseTics / 35},
        enemy.ai.radius, world.floor_height(map_data, game, enemy), rules, rules.MAX_STEP_HEIGHT, enemy.id)
fn first_walk(map_data, game, enemy, directions, rules) {
    if (len(directions) == 0) 8
    else if (can_walk(map_data, game, enemy, directions[0], rules)) directions[0]
    else first_walk(map_data, game, enemy, [for (i in 1 to (len(directions) - 1)) directions[i]], rules)
}
fn commit_direction(enemy, direction, seed) {
    let sample = if (direction < 8) game_state.roll(seed, 16) else {value: 1, seed: seed}
    {enemy: {*: enemy, ai: {*: enemy.ai, move_dir: direction,
        move_timer: (sample.value - 1) * enemy.ai.chaseTics / 35}}, seed: sample.seed}
}
fn choose_direction(map_data, game, enemy, target, rules) {
    let dx = target.x - enemy.x, dy = target.y - enemy.y
    let old = if (enemy.ai.move_dir == null) 8 else enemy.ai.move_dir
    let reverse_dir = if (old == 8) 8 else (old + 4) % 8
    let horizontal = if (dx > 10) 0 else if (dx < -10) 4 else 8
    let vertical = if (dy > 10) 2 else if (dy < -10) 6 else 8
    let diagonal = [3, 1, 5, 7][(if (dy < 0) 2 else 0) + (if (dx > 0) 1 else 0)]
    if (horizontal != 8 and vertical != 8 and diagonal != reverse_dir and
        can_walk(map_data, game, enemy, diagonal, rules)) commit_direction(enemy, diagonal, game.seed)
    else {
        let swap = math.random(game.seed)
        let axes = if (swap[0] * 255 > 200 or abs(dy) > abs(dx)) [vertical, horizontal] else [horizontal, vertical]
        let first = first_walk(map_data, game, enemy,
            [for (direction in [*axes, old] where direction != reverse_dir and direction < 8) direction], rules)
        if (first < 8) commit_direction(enemy, first, swap[1]) else {
            let order = math.random(swap[1])
            let all_directions = if (order[0] < 0.5) [0, 1, 2, 3, 4, 5, 6, 7] else [7, 6, 5, 4, 3, 2, 1, 0]
            let fallback = first_walk(map_data, game, enemy,
                [*[for (direction in all_directions where direction != reverse_dir) direction], reverse_dir], rules)
            commit_direction(enemy, fallback, order[1])
        }
    }
}
fn chase(map_data, game, enemy, target, dt, rules) {
    if (geo.distance_squared(enemy, target) <= rules.MELEE_RANGE ** 2) {enemy: enemy, seed: game.seed}
    else {
        let timer = (enemy.ai.move_timer or 0) - dt
        let selected = if (timer <= 0 or enemy.ai.move_dir == null or enemy.ai.move_dir == 8)
            choose_direction(map_data, game, enemy, target, rules)
            else {enemy: {*: enemy, ai: {*: enemy.ai, move_timer: timer}}, seed: game.seed}
        let next = selected.enemy, direction = next.ai.move_dir
        if (direction >= 8) selected else {
            let candidate = {x: next.x + DX[direction] * next.ai.speed * dt,
                y: next.y + DY[direction] * next.ai.speed * dt}
            let position = movement.slide(map_data, game, next, candidate, next.ai.radius,
                world.floor_height(map_data, game, next), rules, rules.MAX_STEP_HEIGHT, next.id)
            if (position.x == next.x and position.y == next.y)
                choose_direction(map_data, {*: game, seed: selected.seed}, next, target, rules)
            else {enemy: {*: next, x: position.x, y: position.y,
                floor: world.floor_height(map_data, game, position),
                facing: math.atan2(position.y - next.y, position.x - next.x)}, seed: selected.seed}
        }
    }
}
fn hitscan_pellet(acc, angular_size, spread_limit) {
    let a = game_state.roll(acc.seed, 256)
    let b = game_state.roll(a.seed, 256)
    let hit = abs((a.value - b.value) / 255 * spread_limit) < angular_size
    let damage = if (hit) game_state.roll(b.seed, 5) else {value: 0, seed: b.seed}
    {seed: damage.seed, amount: acc.amount + damage.value * 3}
}
fn attack(map_data, game, enemy, target, rules) {
    if (enemy.ai.attack_is_melee) {
        let demon = enemy.type == 3002 or enemy.type == 58
        let sample = game_state.roll(game.seed, if (demon) 10 else 8)
        let amount = sample.value * (if (demon) 4 else if (enemy.type == 3003) 10 else 3)
        let sounded = events.sound({*: game, seed: sample.seed}, if (demon) "DSSGTATK" else "DSCLAW", enemy)
        if (world.visible(map_data, game, enemy, target, rules))
            combat.damage(map_data, sounded, enemy.ai.target, amount, enemy.id, rules) else sounded
    } else {
        let definition = rules.ENEMY_PROJECTILES[string(enemy.type)]
        if (definition != null) {
            let origin = {*: enemy, z: world.floor_height(map_data, game, enemy) + rules.EYE_HEIGHT * 0.8}
            let aim = {*: target, z: world.floor_height(map_data, game, target) + rules.EYE_HEIGHT}
            combat.spawn_projectile(game, origin, aim,
                {*: definition, speed: definition.speed * (if (game.skill == 5) 2 else 1)}, enemy.id, rules)
        } else if (enemy.ai.pellets != null) {
            let audible = events.sound(game, enemy.ai.hitscanSound, enemy)
            if (not world.visible(map_data, game, enemy, target, rules)) audible else {
                let radius = if (enemy.ai.target == -1) rules.PLAYER_RADIUS else (target.ai.radius or rules.ENEMY_RADIUS)
                let angular_size = math.atan2(radius, math.sqrt(geo.distance_squared(enemy, target)))
                let spread_limit = math.pi / (if (enemy.ai.target == -1 and
                    pickups.active(game.player, "invisibility", game.time)) 4 else 8)
                let damage = reduce([{seed: game.seed, amount: 0}, *[for (i in 1 to enemy.ai.pellets) i]],
                    (acc, unused) => hitscan_pellet(acc, angular_size, spread_limit))
                if (damage.amount > 0) combat.damage(map_data, {*: audible, seed: damage.seed},
                    enemy.ai.target, damage.amount, enemy.id, rules) else {*: audible, seed: damage.seed}
            }
        } else game
    }
}
fn awake(map_data, game, enemy, target, rules) {
    let visible = world.visible(map_data, game, enemy, target, rules)
    let sector = world.sector_at(map_data, enemy)
    let heard = contains(game.alerted_sectors, sector.sectorIndex) and (not enemy.ai.ambush or visible)
    if ((geo.distance_squared(enemy, target) < enemy.ai.sightRange ** 2 and visible) or heard) {
        let next = combat.replace_actor(game, {*: change(enemy, "chasing"), ai: {*: change(enemy, "chasing").ai,
            reaction_timer: enemy.ai.reactionTime, los_timer: 0.0}})
        if (game.time - game.last_alert > 0.5) events.sound({*: next, last_alert: game.time}, enemy.ai.alertSound, enemy)
        else next
    } else combat.replace_actor(game, {*: enemy, ai: {*: enemy.ai, los_timer: 0.0}})
}
fn tick_enemy(map_data, game, id, dt, rules) {
    let prior = combat.actor(game, id)
    if (prior.collected) {
        if (prior.respawn_at != null and game.time >= prior.respawn_at)
            combat.replace_actor(game, {*: change(prior, "idle"), x: prior.spawn_x, y: prior.spawn_y,
                hp: prior.max_hp, collected: false, dead_at: null, respawn_at: null,
                ai: {*: change(prior, "idle").ai, target: -1, threshold: 0.0, reaction_timer: 0.0}})
        else game
    } else if (geo.distance_squared(prior, game.player) > rules.MAX_RENDER_DISTANCE ** 2) game
    else {
        let old_target = combat.target(game, prior.ai.target)
        let target_id = if (old_target == null or old_target.collected or old_target.health == 0) -1 else prior.ai.target
        let target = combat.target(game, target_id)
        let enemy = {*: prior, ai: {*: prior.ai, state_time: prior.ai.state_time + dt,
            target: target_id, threshold: if (target_id != prior.ai.target) 0 else max(0, prior.ai.threshold - dt),
            los_timer: prior.ai.los_timer + dt}}
        if (enemy.ai.state == "idle") {
            if (enemy.ai.los_timer >= rules.LINE_OF_SIGHT_CHECK_INTERVAL) awake(map_data, game, enemy, target, rules)
            else combat.replace_actor(game, enemy)
        } else if (enemy.ai.state == "chasing") {
            let moved = chase(map_data, game, enemy, target, dt, rules)
            let next = moved.enemy
            let distance = math.sqrt(geo.distance_squared(enemy, target))
            let ready = enemy.ai.reaction_timer <= 0 and game.time - enemy.ai.last_attack > enemy.ai.cooldown
            let melee = ready and enemy.ai.meleeRange != null and distance < enemy.ai.meleeRange
            let check_ranged = ready and not melee and not enemy.ai.melee and distance < enemy.ai.attackRange and
                enemy.ai.los_timer >= rules.LINE_OF_SIGHT_CHECK_INTERVAL
            let can_shoot = check_ranged and world.visible(map_data, game, enemy, target, rules)
            let sample = if (can_shoot) game_state.roll(moved.seed, 256) else {value: 0, seed: moved.seed}
            let ranged = can_shoot and sample.value - 1 >= max(0, min(200, distance - 64 - (if (enemy.ai.melee) 0 else 128)))
            let timed = {*: next, ai: {*: next.ai, reaction_timer: max(0, enemy.ai.reaction_timer - dt),
                los_timer: if (check_ranged) 0.0 else next.ai.los_timer, attack_is_melee: melee}}
            combat.replace_actor({*: game, seed: sample.seed}, if (melee or ranged) change(timed, "attacking") else timed)
        } else if (enemy.ai.state == "attacking") {
            let deal = not enemy.ai.damage_dealt and enemy.ai.state_time >= enemy.ai.attackDuration / 2
            let attacking = combat.replace_actor(game, {*: enemy, ai: {*: enemy.ai, damage_dealt: enemy.ai.damage_dealt or deal}})
            let attacked = if (deal) attack(map_data, attacking, enemy, target, rules) else attacking
            if (enemy.ai.state_time >= enemy.ai.attackDuration)
                combat.replace_actor(attacked, change({*: combat.actor(attacked, id),
                    ai: {*: combat.actor(attacked, id).ai, last_attack: game.time}}, "chasing")) else attacked
        } else if (enemy.ai.state == "pain" and enemy.ai.state_time >= enemy.ai.painDuration)
            combat.replace_actor(game, change(enemy, "chasing"))
        else combat.replace_actor(game, enemy)
    }
}
pub fn advance(map_data, game, dt, rules) => if (game.player.health <= 0) game else
    reduce([game, *[for (enemy in game.actors where enemy.ai != null and contains(rules.ENEMIES, enemy.type)) enemy.id]],
        (acc, id) => tick_enemy(map_data, acc, id, dt, rules))
