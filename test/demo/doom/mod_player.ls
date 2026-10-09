// Collision and axis sliding translated from cssDOOM (see ATTRIBUTION.md).
import geo: .mod_geometry
import world: .mod_world
import controls: .mod_input
import events: .mod_events

fn wall_blocks(map_data, game, wall, old, next, floor_height, rules) {
    let door = world.door_for(map_data, game, wall)
    if (door != null) not door.passable
    else if (wall.isUpperWall) wall.topHeight > floor_height and
        wall.bottomHeight < floor_height + rules.PLAYER_HEIGHT and
        geo.crosses_segment(old, next, wall.start, wall.end)
    else wall.isSolid == true
}
pub fn actor_radius(actor, rules) => if (actor.ai != null) actor.ai.radius
    else if (actor.type == 2035) rules.BARREL_RADIUS else actor.solid_radius or 0
pub fn can_move(map_data, game, old, next, radius, floor_height, rules, max_drop = inf, exclude_id = null) {
    let box = {min_x: next.x - radius, max_x: next.x + radius,
        min_y: next.y - radius, max_y: next.y + radius}
    let walls = [for (i in world.query(map_data, "walls", box), let wall = map_data.walls[i]
        where wall_blocks(map_data, game, wall, old, next, floor_height, rules) and
            geo.circle_hits_segment(next, radius, wall.start, wall.end)) i]
    let actors = [for (actor in game.actors, let solid_radius = actor_radius(actor, rules)
        where not actor.collected and actor.id != exclude_id and solid_radius > 0 and
            geo.distance_squared(next, actor) < (radius + solid_radius) ** 2) actor.id]
    let lifts = [for (lift in game.lifts, edge in lift.collisionEdges
        where floor_height < lift.height - rules.MAX_STEP_HEIGHT and
            geo.circle_hits_segment(next, radius, edge.start, edge.end)) lift.sectorIndex]
    let new_floor = world.floor_height(map_data, game, next)
    len(walls) == 0 and len(actors) == 0 and len(lifts) == 0 and
        new_floor - floor_height <= rules.MAX_STEP_HEIGHT and floor_height - new_floor <= max_drop
}
pub fn slide(map_data, game, old, next, radius, floor_height, rules, max_drop = inf, exclude_id = null) {
    let candidates = [next, {x: next.x, y: old.y}, {x: old.x, y: next.y}]
    let accepted = [for (candidate in candidates
        where can_move(map_data, game, old, candidate, radius, floor_height, rules, max_drop, exclude_id)) candidate]
    if (len(accepted) == 0) {x: old.x, y: old.y} else accepted[0]
}
pub fn move(map_data, game, input_state, dt, rules) {
    let player = game.player
    let multiplier = if (contains(input_state.held, "run")) rules.RUN_MULTIPLIER else 1
    let angle = player.angle + controls.axis(input_state, "turn_left", "turn_right") * rules.TURN_SPEED * multiplier * dt -
        input_state.mouse_x * 0.002
    let forward = geo.forward(angle)
    let right = geo.right(angle)
    let along = controls.axis(input_state, "forward", "back")
    let across = controls.axis(input_state, "strafe_right", "strafe_left")
    let distance = rules.MOVE_SPEED * multiplier * dt
    let candidate = {x: player.x + (forward.x * along + right.x * across) * distance,
        y: player.y + (forward.y * along + right.y * across) * distance}
    let position = slide(map_data, game, player, candidate, rules.PLAYER_RADIUS, player.floor, rules)
    let floor_height = world.floor_height(map_data, game, position)
    let next = {*: game, player: {*: player, x: position.x, y: position.y, angle: angle,
        pitch: max(-1.2, min(1.2, player.pitch + input_state.mouse_y * 0.002)),
        floor: floor_height, z: floor_height + rules.EYE_HEIGHT, moving: along != 0 or across != 0}}
    if (player.floor - floor_height > 32) events.sound(next, "DSOOF") else next
}
