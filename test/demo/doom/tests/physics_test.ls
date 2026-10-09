import geo: ~~.mod_geometry
import world: ~~.mod_world
import player: ~~.mod_player
let left = [{x: 0, y: 0}, {x: 128, y: 0}, {x: 128, y: 256}, {x: 0, y: 256}]
let right = [{x: 128, y: 0}, {x: 256, y: 0}, {x: 256, y: 256}, {x: 128, y: 256}]
let map_data = world.prepare({bounds: {minX: 0, minY: 0, maxX: 256, maxY: 256},
    walls: [{start: {x: 128, y: 0}, end: {x: 128, y: 256}, isSolid: true,
        isUpperWall: true, bottomHeight: 0, topHeight: 72, frontSectorIndex: 1, backSectorIndex: 0}],
    sectorPolygons: [{sectorIndex: 0, floorHeight: 0, boundaries: [left]},
        {sectorIndex: 1, floorHeight: 32, boundaries: [right]}],
    doors: [{sectorIndex: 1}], sightLines: []}, 64)
let rules = {PLAYER_RADIUS: 16, PLAYER_HEIGHT: 56, MAX_STEP_HEIGHT: 24, BARREL_RADIUS: 16}
let closed = {doors: [{sectorIndex: 1, passable: false}], lifts: [], actors: []}
let opened = {*: closed, doors: [{sectorIndex: 1, passable: true}]}
let lowered = {*: opened, lifts: [{sectorIndex: 1, height: 24, upperHeight: 32, collisionEdges: []}]}
let old = {x: 80, y: 80}
let next = {x: 120, y: 100};
{
    buckets: len(world.query(map_data, "walls", geo.bounds([{x: 0, y: 0}, {x: 256, y: 256}]))),
    floors: [world.floor_height(map_data, closed, old), world.floor_height(map_data, closed, {x: 160, y: 80}),
        world.floor_height(map_data, lowered, {x: 160, y: 80}), world.floor_height(map_data, closed, {x: -10, y: -10})],
    slide: player.slide(map_data, closed, old, next, 16, 0, rules),
    door: [player.can_move(map_data, closed, old, next, 16, 0, rules),
        player.can_move(map_data, opened, old, next, 16, 0, rules)],
    step: [player.can_move(map_data, opened, old, {x: 140, y: 80}, 16, 0, rules),
        player.can_move(map_data, lowered, old, {x: 140, y: 80}, 16, 0, rules)],
    ray: [world.ray_hit(map_data, closed, old, {x: 1, y: 0}, 41, 100).distance,
        world.ray_hit(map_data, opened, old, {x: 1, y: 0}, 41, 100)]
}
