import world: ~~.mod_world
import mechanics: ~~.mod_mechanics
let boundary = [{x: 0, y: 0}, {x: 128, y: 0}, {x: 128, y: 128}, {x: 0, y: 128}]
let map_data = world.prepare({bounds: {minX: 0, minY: 0, maxX: 256, maxY: 128},
    sectors: [{tag: 7}, {tag: 0}],
    sectorPolygons: [{sectorIndex: 0, floorHeight: 0, boundaries: [boundary]}],
    walls: [{start: {x: 128, y: 0}, end: {x: 128, y: 128}, frontSectorIndex: 1,
        backSectorIndex: 0, isUpperWall: true, texture: "DOOR1", linedefIndex: 0}],
    doors: [{sectorIndex: 1}], sightLines: [], linedefs: [{specialType: 0, sectorTag: 0}],
    triggers: [{start: {x: 128, y: 0}, end: {x: 128, y: 128}, sectorTag: 7, specialType: 10}],
    teleporters: [{start: {x: 128, y: 0}, end: {x: 128, y: 128}, destX: 64, destY: 64,
        destAngle: 90, oneShot: true}]})
let rules = {DOOR_CLOSE_DELAY: 4000, LIFT_RAISE_DELAY: 3000, USE_RANGE: 64, LIFT_USE_SPECIAL: 62,
    SWITCH_ON_PREFIX: "SW2", SWITCH_OFF_PREFIX: "SW1", EXIT_SPECIAL: 11, SECRET_EXIT_SPECIAL: 51,
    PLAYER_RADIUS: 16, BARREL_RADIUS: 16, EYE_HEIGHT: 41, SHOOTABLE: [3004]}
let game = {time: 0.0, level_name: "E1M1", player: {x: 64, y: 64, floor: 0, keys: [], angle: -math.pi / 2},
    doors: [{sectorIndex: 1, phase: "closed", height: 0, closedHeight: 0, openHeight: 128,
        keyRequired: "blue", moved_at: 0, deadline: 0, passable: false}],
    lifts: [{sectorIndex: 0, tag: 7, phase: "raised", height: 64, upperHeight: 64, lowerHeight: 0,
        oneWay: false, deadline: 0, moved_at: 0, move_from: 64}],
    crushers: [{sectorIndex: 0, active: true, height: 40, topHeight: 128, crushHeight: 8,
        speed: "fast", direction: -1, damage_time: 0}],
    actors: [{id: 8, type: 3004, x: 64, y: 64, collected: false, ai: {radius: 20}}],
    triggered: [], switches: [], effects: [], transition: null}
let locked = mechanics.open_door(game, 1, rules)
let keyed = {*: game, player: {*: game.player, keys: ["blue"]}, crushers: []}
let opening = mechanics.use(map_data, keyed, rules)
let before = mechanics.advance(map_data, {*: opening, time: 0.79}, 0, rules)
let clearance = mechanics.advance(map_data, {*: opening, time: 0.8}, 0, rules)
let opened = mechanics.advance(map_data, {*: opening, time: 1.0}, 0, rules)
let reset = mechanics.open_door({*: opened, time: 3.0}, 1, rules)
let closing = mechanics.advance(map_data, {*: reset, time: 7.0}, 0, rules)
let closed = mechanics.advance(map_data, {*: closing, time: 8.0}, 0, rules)
let lift = mechanics.lower_lift(keyed, 0, rules)
let halfway = mechanics.advance(map_data, {*: lift, time: 0.5}, 0, rules)
let lowered = mechanics.advance(map_data, {*: lift, time: 1.0}, 0, rules)
let raising = mechanics.advance(map_data, {*: lowered, time: 3.0}, 0, rules)
let raised = mechanics.advance(map_data, {*: raising, time: 4.0}, 0, rules)
let crushed = mechanics.advance(map_data, game, 0.125, rules)
let crossed = {*: keyed, player: {*: keyed.player, x: 136}}
let trigger = mechanics.walk_triggers(map_data, crossed, {x: 120, y: 64}, rules)
let teleported = mechanics.teleport(map_data, crossed, {x: 120, y: 64}, rules)
let switch_map = {*: map_data, walls: [{*: map_data.walls[0], texture: "SW1BRCOM"}],
    linedefs: [{specialType: 11, sectorTag: 0}]}
let exit = mechanics.use(switch_map, keyed, rules);
{
    key: [locked.doors[0].phase, locked.effects[0].sound],
    door: [before.doors[0].passable, clearance.doors[0].passable, opened.doors[0].height,
        reset.doors[0].deadline, closing.doors[0].phase, closed.doors[0].phase, closed.doors[0].height],
    lift: [halfway.lifts[0].height, lowered.lifts[0].height, raising.lifts[0].phase, raised.lifts[0].height],
    crush: [crushed.crushers[0].height, crushed.effects[0].amount, crushed.effects[0].target],
    trigger: [trigger.lifts[0].phase, trigger.triggered],
    teleport: [teleported.player.x, teleported.player.y, teleported.player.angle,
        teleported.effects[0].amount, teleported.triggered],
    exit: exit.transition
}
