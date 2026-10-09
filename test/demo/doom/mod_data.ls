// Resource IO uses lambda-io (D7.1.2v2); callers supply an explicit demo base URI.
pub let MAPS = ["E1M1", "E1M2", "E1M3", "E1M4", "E1M5", "E1M6", "E1M7", "E1M8", "E1M9"]
fn fail(message) error => error("DOOM data: " ++ message)
fn point(value) => value is map and value.x is number and value.y is number
fn sector_index(value, total) => value is int and value >= 0 and value < total

// CLI input() is cwd-relative. Resolve the actual entry argument once, rather
// than assuming the checkout's directory or changing input() semantics.
pub fn entry_uri() string^ {
    let paths = [for (argument in sys.proc.self.argv# where ends_with(argument, ".ls")) argument]
    if (len(paths) == 0) raise fail("no Lambda entry path in command line")
    else url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", paths[0])
}
pub fn read(base, relative) any^ => input(url_resolve(base, relative), 'json') ^ {
    raise fail("cannot read " ++ relative ++ ": " ++ ^.message)
}
pub fn validate(level) map^ {
    if (not (level is map)) raise fail("map must be an object")
    else if (not all([for (key in ["vertices", "linedefs", "sidedefs", "sectors", "things", "walls",
        "sectorPolygons", "doors", "lifts", "triggers", "teleporters", "crushers", "sightLines"])
        level[key] is array])) raise fail("missing map array")
    else if (len(level.sectors) == 0 or not point(level.playerStart) or
        not (level.playerStart.angle is number)) raise fail("invalid player start or empty sectors")
    else if (not all([for (wall in level.walls) point(wall.start) and point(wall.end) and
        wall.bottomHeight is number and wall.topHeight is number and
        sector_index(wall.sectorIndex, len(level.sectors))])) raise fail("invalid wall geometry or sector")
    else if (not all([for (sector in level.sectors) sector.floorHeight is number and
        sector.ceilingHeight is number and sector.ceilingHeight >= sector.floorHeight]))
        raise fail("invalid sector heights")
    else if (not all([for (sector in level.sectorPolygons) sector_index(sector.sectorIndex, len(level.sectors)) and
        sector.boundaries is array and len(sector.boundaries) > 0 and
        all([for (boundary in sector.boundaries) boundary is array and len(boundary) >= 3 and
            all([for (vertex in boundary) point(vertex)])])])) raise fail("invalid sector boundaries")
    else if (not all([for (thing in level.things) point(thing) and thing.type is int and
        thing.angle is number and thing.flags is int])) raise fail("invalid thing")
    else level
}
pub fn load_level(base, map_name) map^ {
    if (not contains(MAPS, map_name)) raise fail("unknown episode map " ++ string(map_name))
    else validate(read(base, "data/maps/" ++ map_name ++ ".json")^)^
}
pub fn catalog(base) any^ => read(base, "data/catalog.json")^
pub fn rules(base) any^ => read(base, "data/rules.json")^
pub fn visuals(base) any^ => read(base, "data/visuals.json")^
pub fn next_map(map_name, secret = false) => if (not contains(MAPS, map_name)) null
    else if (secret) "E1M9"
    else if (map_name == "E1M9") "E1M4"
    else if (map_name == "E1M8") null
    else MAPS[index_of(MAPS, map_name) + 1]
