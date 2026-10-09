// Direct resource smoke; entry_uri remains independent of the working directory.
import data: ~~.mod_data
import model: ~~.mod_state
let base = url_resolve(data.entry_uri()^, "../")
let rules = data.rules(base)^
let visuals = data.visuals(base)^
let levels = [for (map_name in data.MAPS) data.load_level(base, map_name)^]
let first = model.new_game(levels[0], rules, visuals)
let malformed = data.validate({}) ^ { ^.message };
{
    maps: len(levels), walls: [for (level in levels) len(level.walls)],
    start: [first.player.x, first.player.y, first.player.z, round(first.player.angle)],
    inventory: [first.player.health, first.player.armor, first.player.ammo.bullets, first.player.weapon],
    doors: len(first.doors), lifts: len(first.lifts),
    deterministic: first == model.new_game(levels[0], rules, visuals),
    malformed: malformed
}
