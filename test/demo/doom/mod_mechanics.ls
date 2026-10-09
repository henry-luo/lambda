// cssDOOM mechanics use simulation deadlines instead of browser timers.
import geo: .mod_geometry
import world: .mod_world
import data: .mod_data
import events: .mod_events

fn crossed(old, next, line) => (geo.side(old, line.start, line.end) > 0) !=
    (geo.side(next, line.start, line.end) > 0)
fn progress(entry, simulation_time, duration, target, eased) {
    let t = max(0, min(1, (simulation_time - entry.moved_at) / duration))
    let fraction = if (eased) t * t * (3 - 2 * t) else t
    entry.move_from + (target - entry.move_from) * fraction
}
fn map_events(entries, advance) {
    let updates = [for (entry in entries) advance(entry)]
    {entries: [for (update in updates) update.value], effects: [for (update in updates) *update.effects]}
}
fn door_tick(door, game, sector_id, rules) {
    let should_close = (door.phase == "opening" or door.phase == "open") and game.time >= door.deadline
    let occupied = sector_id == door.sectorIndex
    let closing = should_close and not occupied
    let next = if (should_close and occupied) {*: door, deadline: game.time + rules.DOOR_CLOSE_DELAY / 1000}
        else if (closing) {*: door, phase: "closing", passable: false,
            moved_at: game.time, move_from: door.height} else door
    let height = if (next.phase == "opening" or next.phase == "closing")
        progress(next, game.time, 1, if (next.phase == "opening") next.openHeight else next.closedHeight, true)
        else next.height
    let finished = game.time - next.moved_at >= 1
    {value: {*: next, height: height,
        passable: next.phase == "open" or (next.phase == "opening" and game.time - next.moved_at >= 0.8),
        phase: if (finished and next.phase == "opening") "open"
            else if (finished and next.phase == "closing") "closed" else next.phase},
        effects: if (closing) [{kind: "sound", sound: "DSDORCLS"}] else []}
}
fn lift_tick(lift, game) {
    let raising = lift.phase == "lowered" and not lift.oneWay and game.time >= lift.deadline
    let next = if (raising) {*: lift, phase: "raising", moved_at: game.time, move_from: lift.height} else lift
    let moving = next.phase == "lowering" or next.phase == "raising"
    let duration = next.duration or 1
    let height = if (moving) progress(next, game.time, duration,
        if (next.phase == "lowering") next.lowerHeight else next.upperHeight, true) else next.height
    {value: {*: next, height: height,
        phase: if (moving and game.time - next.moved_at >= duration)
            (if (next.phase == "lowering") "lowered" else "raised") else next.phase},
        effects: if (raising) [{kind: "sound", sound: "DSPSTOP"}] else []}
}
fn crusher_tick(crusher, game, sector_id, dt) {
    let speed = if (crusher.speed == "fast") 64 else 32
    let height = if (crusher.active) max(crusher.crushHeight,
        min(crusher.topHeight, crusher.height + speed * dt * crusher.direction)) else crusher.height
    let crushing = crusher.active and sector_id == crusher.sectorIndex and height - game.player.floor <= 41
    let timer = if (crushing) crusher.damage_time + dt else 0
    let damaging = timer >= 4 / 35
    {value: {*: crusher, height: height,
        direction: if (height <= crusher.crushHeight) 1 else if (height >= crusher.topHeight) -1 else crusher.direction,
        damage_time: timer - (if (damaging) 4 / 35 else 0)},
        effects: if (damaging) [events.damage(-1, 10, "crusher")] else []}
}
pub fn advance(map_data, game, dt, rules) {
    let sector = world.sector_at(map_data, game.player)
    let doors = map_events(game.doors, (entry) => door_tick(entry, game, sector.sectorIndex, rules))
    let lifts = map_events(game.lifts, (entry) => lift_tick(entry, game))
    let crushers = map_events(game.crushers, (entry) => crusher_tick(entry, game, sector.sectorIndex, dt))
    events.with_events({*: game, doors: doors.entries, lifts: lifts.entries, crushers: crushers.entries},
        [*doors.effects, *lifts.effects, *crushers.effects])
}
pub fn open_door(game, sector_id, rules) {
    let door = world.motion(game.doors, sector_id)
    if (door == null) game
    else if (door.keyRequired != null and not contains(game.player.keys, door.keyRequired))
        events.sound(game, "DSOOF")
    else {
        let already_open = door.phase == "opening" or door.phase == "open"
        let next = if (already_open) {*: door, deadline: game.time + rules.DOOR_CLOSE_DELAY / 1000}
            else {*: door, phase: "opening", passable: false, moved_at: game.time,
                move_from: door.height, deadline: game.time + rules.DOOR_CLOSE_DELAY / 1000}
        let changed = {*: game, doors: [for (entry in game.doors) if (entry.sectorIndex == sector_id) next else entry]}
        if (already_open) changed else events.sound(changed, "DSDOROPN")
    }
}
pub fn lower_lift(game, sector_id, rules) {
    let lift = world.motion(game.lifts, sector_id)
    if (lift == null or lift.phase == "lowered" or lift.phase == "lowering") game
    else events.sound({*: game, lifts: [for (entry in game.lifts)
        if (entry.sectorIndex == sector_id) {*: entry, phase: "lowering", moved_at: game.time,
            move_from: entry.height, deadline: game.time + rules.LIFT_RAISE_DELAY / 1000} else entry]}, "DSPSTART")
}
pub fn activate_tag(map_data, game, tag, rules) {
    let doors = reduce([game, *[for (door in game.doors where map_data.sectors[door.sectorIndex].tag == tag) door.sectorIndex]],
        (acc, sector_id) => open_door(acc, sector_id, rules))
    let lifts = reduce([doors, *[for (lift in game.lifts where lift.tag == tag) lift.sectorIndex]],
        (acc, sector_id) => lower_lift(acc, sector_id, rules))
    {*: lifts, crushers: [for (crusher in lifts.crushers)
        if (map_data.sectors[crusher.sectorIndex].tag == tag) {*: crusher, active: true} else crusher]}
}
fn check_point(player, rules) {
    let forward = geo.forward(player.angle)
    {x: player.x + forward.x * rules.USE_RANGE / 2, y: player.y + forward.y * rules.USE_RANGE / 2}
}
pub fn use(map_data, game, rules) {
    let point = check_point(game.player, rules)
    let nearby = [for (wall in map_data.walls where
        geo.segment_distance_squared(point, wall.start, wall.end) < rules.USE_RANGE ** 2) wall]
    let switch_wall = [for (wall in nearby where wall.texture != null and
        (starts_with(wall.texture, rules.SWITCH_ON_PREFIX) or starts_with(wall.texture, rules.SWITCH_OFF_PREFIX))) wall][0]
    let line = if (switch_wall == null) null else map_data.linedefs[switch_wall.linedefIndex]
    let switched = if (switch_wall == null) game else {
        let toggle = {*: game, switches: if (contains(game.switches, switch_wall.id))
            [for (id in game.switches where id != switch_wall.id) id] else [*game.switches, switch_wall.id]}
        if (line.specialType == rules.EXIT_SPECIAL or line.specialType == rules.SECRET_EXIT_SPECIAL)
            {*: toggle, transition: {map: data.next_map(game.level_name, line.specialType == rules.SECRET_EXIT_SPECIAL),
                at: game.time + 1}}
        else if (line.sectorTag > 0) activate_tag(map_data, toggle, line.sectorTag, rules) else toggle
    }
    let door_wall = [for (wall in nearby where wall.door_id != null and
        (map_data.linedefs[wall.linedefIndex].sectorTag or 0) <= 0) wall][0]
    let opened = if (door_wall == null) switched else open_door(switched, door_wall.door_id, rules)
    let lift_wall = [for (wall in nearby where map_data.linedefs[wall.linedefIndex].specialType == rules.LIFT_USE_SPECIAL) wall][0]
    if (lift_wall == null) opened else {
        let tag = map_data.linedefs[lift_wall.linedefIndex].sectorTag
        reduce([opened, *[for (lift in opened.lifts where lift.tag == tag) lift.sectorIndex]],
            (acc, sector_id) => lower_lift(acc, sector_id, rules))
    }
}
pub fn walk_triggers(map_data, game, old, rules) => reduce([game, *[for (i in 0 to (len(map_data.triggers) - 1),
    let trigger = map_data.triggers[i], let key = "lift:" ++ string(i)
    where not contains(game.triggered, key) and crossed(old, game.player, trigger)) {trigger: trigger, key: key}]],
    (acc, item) => {
        let next = reduce([acc, *[for (lift in acc.lifts where lift.tag == item.trigger.sectorTag) lift.sectorIndex]],
            (value, sector_id) => lower_lift(value, sector_id, rules))
        if (contains([10, 53, 36], item.trigger.specialType)) {*: next, triggered: [*next.triggered, item.key]} else next
    })
pub fn teleport(map_data, game, old, rules) {
    let crossing = [for (i in 0 to (len(map_data.teleporters) - 1), let target = map_data.teleporters[i],
        let key = "teleport:" ++ string(i)
        where not contains(game.triggered, key) and crossed(old, game.player, target)) {target: target, key: key}][0]
    if (crossing == null) game else {
        let target = crossing.target
        let position = {x: target.destX, y: target.destY}
        let floor_height = world.floor_height(map_data, game, position)
        let damage = [for (actor in game.actors, let radius = rules.PLAYER_RADIUS +
            (if (actor.ai != null) actor.ai.radius else rules.BARREL_RADIUS)
            where not actor.collected and contains(rules.SHOOTABLE, actor.type) and
                abs(actor.x - position.x) < radius and abs(actor.y - position.y) < radius)
            events.damage(actor.id, 10000, "player")]
        events.sound(events.with_events({*: game, player: {*: game.player, x: position.x, y: position.y,
            floor: floor_height, z: floor_height + rules.EYE_HEIGHT, angle: (target.destAngle - 90) * math.pi / 180},
            triggered: if (target.oneShot) [*game.triggered, crossing.key] else game.triggered,
            teleport_until: game.time + 0.25}, [*damage, {kind: "fog", position: old, at: game.time},
                {kind: "fog", position: position, at: game.time}]), "DSTELEPT", position)
    }
}
