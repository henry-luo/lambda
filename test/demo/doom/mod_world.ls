// Immutable spatial buckets; queries never write visit marks into map geometry.
import geo: .mod_geometry

pub fn motion(entries, sector_id) => [for (entry in entries where entry.sectorIndex == sector_id) entry][0]
pub fn lowest_adjacent(level, sector_id) => min([level.sectors[sector_id].floorHeight,
    *[for (line in level.linedefs,
        let front = if (line.frontSidedef >= 0) level.sidedefs[line.frontSidedef].sectorIndex else -1,
        let back = if (line.backSidedef >= 0) level.sidedefs[line.backSidedef].sectorIndex else -1,
        let other = if (front == sector_id) back else front
        where (front == sector_id or back == sector_id) and other >= 0) level.sectors[other].floorHeight]])
fn coordinate(value, origin, size, total) => max(0, min(total - 1, int(floor((value - origin) / size))))
fn bucket_box(grid, column, row) => {min_x: grid.origin.x + column * grid.size,
    min_y: grid.origin.y + row * grid.size, max_x: grid.origin.x + (column + 1) * grid.size,
    max_y: grid.origin.y + (row + 1) * grid.size}
fn index_items(items, grid) => [for (row in 0 to (grid.rows - 1), column in 0 to (grid.columns - 1),
    let box = bucket_box(grid, column, row))
    [for (i in 0 to (len(items) - 1) where geo.overlaps(items[i].bounds, box)) i]]

pub fn prepare(level, size = 128) {
    let walls = [for (i in 0 to (len(level.walls) - 1), let wall = level.walls[i])
        {*: wall, id: i, bounds: geo.bounds([wall.start, wall.end]),
            door_id: if (wall.isUpperWall) [for (door in level.doors
                where door.sectorIndex == wall.frontSectorIndex or door.sectorIndex == wall.backSectorIndex)
                door.sectorIndex][0] else null}]
    let polygons = [for (sector in level.sectorPolygons)
        {*: sector, bounds: geo.bounds(sector.boundaries[0])}]
    let sight = [for (line in level.sightLines) {*: line, bounds: geo.bounds([line.start, line.end])}]
    let origin = {x: floor(level.bounds.minX / size) * size - size,
        y: floor(level.bounds.minY / size) * size - size}
    let grid = {size: size, origin: origin,
        columns: int(ceil((level.bounds.maxX - origin.x) / size)) + 1,
        rows: int(ceil((level.bounds.maxY - origin.y) / size)) + 1}
    let links = [for (line in level.linedefs or [],
        let front = level.sidedefs[line.frontSidedef].sectorIndex,
        let back = level.sidedefs[line.backSidedef].sectorIndex
        where line.frontSidedef >= 0 and line.backSidedef >= 0 and front != back)
        {front: front, back: back, blocks: if (band(line.flags, 64) != 0) 1 else 0}]
    {*: level, walls: walls, polygons: polygons, sight: sight,
        sound_links: [for (i in 0 to (len(level.sectors) - 1)) [for (link in links
            where link.front == i or link.back == i)
            {sector: if (link.front == i) link.back else link.front, blocks: link.blocks}]],
        grid: {*: grid, walls: index_items(walls, grid), sectors: index_items(polygons, grid),
            sight: index_items(sight, grid)}}
}

fn sector_opening(map_data, game, sector_id) {
    let sector = map_data.sectors[sector_id]
    let door = motion(game.doors, sector_id)
    {bottom: sector.floorHeight, top: if (door == null) sector.ceilingHeight
        else if (door.passable) door.openHeight else door.closedHeight}
}
fn sound_flood(map_data, game, pending, visited) {
    if (len(pending) == 0) visited else {
        let current = pending[0]
        let opening = sector_opening(map_data, game, current.sector)
        let neighbors = [for (link in map_data.sound_links[current.sector],
            let other = sector_opening(map_data, game, link.sector),
            let blocks = current.blocks + link.blocks, let previous = visited[string(link.sector)]
            where blocks <= 1 and (previous == null or blocks < previous) and
                min(opening.top, other.top) > max(opening.bottom, other.bottom))
            {sector: link.sector, blocks: blocks}]
        let next = reduce([visited, *neighbors], (acc, entry) => {*: acc, [string(entry.sector)]: entry.blocks})
        sound_flood(map_data, game, [*[for (i in 1 to (len(pending) - 1)) pending[i]], *neighbors], next)
    }
}
pub fn sound_alert(map_data, game) {
    let sector = sector_at(map_data, game.player)
    if (sector == null) [] else {
        let visited = sound_flood(map_data, game, [{sector: sector.sectorIndex, blocks: 0}], {[string(sector.sectorIndex)]: 0});
        [for (sector_key at visited) int(string(sector_key))]
    }
}
pub fn query(world, kind, box) {
    let grid = world.grid
    let x0 = coordinate(box.min_x, grid.origin.x, grid.size, grid.columns)
    let x1 = coordinate(box.max_x, grid.origin.x, grid.size, grid.columns)
    let y0 = coordinate(box.min_y, grid.origin.y, grid.size, grid.rows)
    let y1 = coordinate(box.max_y, grid.origin.y, grid.size, grid.rows)
    unique([for (row in y0 to y1, column in x0 to x1) *grid[kind][row * grid.columns + column]])
}
pub fn sectors_at(world, point) => [for (i in query(world, "sectors",
    {min_x: point.x, max_x: point.x, min_y: point.y, max_y: point.y}), let sector = world.polygons[i]
    where geo.contains_point(sector.bounds, point) and geo.point_in_sector(point, sector.boundaries)) sector]
pub fn sector_at(world, point) => sectors_at(world, point)[0]
pub fn floor_height(world, game, point) {
    let heights = [for (sector in sectors_at(world, point), let lift = motion(game.lifts, sector.sectorIndex))
        if (lift != null) lift.height else sector.floorHeight]
    if (len(heights) == 0) 0 else max(heights)
}
pub fn door_for(world, game, wall) => if (wall.door_id == null) null else motion(game.doors, wall.door_id)
pub fn wall_extent(game, wall) {
    let lift = if (wall.isLiftWall) motion(game.lifts, wall.liftSectorIndex) else null
    let neighbor = if (lift != null and wall.topHeight == lift.upperHeight) wall.bottomHeight else wall.topHeight
    if (lift == null) {bottom: wall.bottomHeight, top: wall.topHeight}
    else {bottom: min(neighbor, lift.height), top: max(neighbor, lift.height)}
}
pub fn shot_blocked(world, game, wall, z) {
    let door = door_for(world, game, wall)
    let extent = wall_extent(game, wall)
    if (door != null and door.passable) false
    else if (wall.isUpperWall or wall.isLowerWall or wall.isMiddleWall)
        z >= extent.bottom and z <= extent.top
    else wall.isSolid or door != null
}
pub fn ray_hit(world, game, origin, direction, z, limit) {
    let end_point = {x: origin.x + direction.x * limit, y: origin.y + direction.y * limit}
    let hits = [for (i in query(world, "walls", geo.bounds([origin, end_point])), let wall = world.walls[i],
        let distance = geo.ray_segment(origin, direction, wall.start, wall.end, limit)
        where shot_blocked(world, game, wall, z) and distance != null) {distance: distance, wall_id: i}]
    if (len(hits) == 0) null else sort(hits, (hit) => hit.distance)[0]
}

fn sight_opening(game, line) {
    let doors = [for (sector_id in [line.frontSector, line.backSector],
        let door = motion(game.doors, sector_id) where door != null and door.passable) door.openHeight]
    let lifts = [for (sector_id in [line.frontSector, line.backSector],
        let lift = motion(game.lifts, sector_id) where lift != null) lift.height]
    {bottom: max([line.openBottom, *lifts]), top: max([line.openTop, *doors])}
}
// The upstream sight cone narrows across two-sided openings, independently of
// texture opacity. Share this query between attacks, explosions and AI.
pub fn visible(world, game, origin, target, rules) {
    let distance = math.sqrt(geo.distance_squared(origin, target))
    if (distance < 1) true else {
        let direction = {x: (target.x - origin.x) / distance, y: (target.y - origin.y) / distance}
        let box = geo.bounds([origin, target])
        let solid_hits = [for (i in query(world, "walls", box), let wall = world.walls[i],
            let door = door_for(world, game, wall)
            where not (wall.isUpperWall or wall.isLowerWall or wall.isMiddleWall) and
                (wall.isSolid or (door != null and not door.passable)) and
                geo.ray_segment(origin, direction, wall.start, wall.end, distance) != null) i]
        let from_z = floor_height(world, game, origin) + rules.EYE_HEIGHT
        let to_z = floor_height(world, game, target) + rules.EYE_HEIGHT
        let crossings = [for (i in query(world, "sight", box), let line = world.sight[i],
            let t = geo.ray_segment(origin, direction, line.start, line.end, distance)
            where t != null) {opening: sight_opening(game, line), distance: t}]
        let cone = reduce([{top: to_z + rules.EYE_HEIGHT - from_z,
            bottom: to_z - rules.EYE_HEIGHT - from_z, blocked: false}, *crossings], (acc, crossing) => {
                let opening = crossing.opening
                let bottom = if (opening.bottom > from_z)
                    max(acc.bottom, (opening.bottom - from_z) / crossing.distance) else acc.bottom
                let top = if (opening.top < from_z)
                    min(acc.top, (opening.top - from_z) / crossing.distance) else acc.top
                {bottom: bottom, top: top, blocked: acc.blocked or opening.bottom >= opening.top or top <= bottom}
            })
        len(solid_hits) == 0 and not cone.blocked
    }
}
