// Shared deterministic arena for combat, inventory and clock fixtures.
import game_state: ~~.mod_state
import world: ~~.mod_world
pub let rules = input("test/demo/doom/data/rules.json", 'json')^
pub let visuals = input("test/demo/doom/data/visuals.json", 'json')^
pub fn level(things = [], walls = [], special = 0) => {
    bounds: {minX: -512, minY: -512, maxX: 512, maxY: 512},
    playerStart: {x: 0, y: 0, floorHeight: 0, angle: math.pi / 2},
    walls: walls, things: things, doors: [], lifts: [], crushers: [], sightLines: [],
    sectors: [{floorHeight: 0, ceilingHeight: 128, specialType: special, tag: 0}],
    sectorPolygons: [{sectorIndex: 0, floorHeight: 0, ceilingHeight: 128, specialType: special,
        boundaries: [[{x: -512, y: -512}, {x: 512, y: -512}, {x: 512, y: 512}, {x: -512, y: 512}]]}],
    triggers: [], teleporters: [], linedefs: [], sidedefs: []}
pub fn thing(type_id, x = 0, y = 100) => {type: type_id, x: x, y: y, angle: 270, flags: 7}
pub fn game(level_data, skill = 3, seed = 17) => {*: game_state.new_game(level_data, rules, visuals, skill, seed),
    level_name: "E1M1", mode: "playing"}
pub fn map_data(level_data) => world.prepare(level_data)
