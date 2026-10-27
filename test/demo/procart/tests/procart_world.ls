// The procart showcase's world model (test/demo/procart/mod_world.ls): one seed fixes everything.
import W: ~~.mod_world
import geometry: lambda.chart.geometry

let w = W.generate(2026)^
let kinds = [for (row in w.land.biomes) for (b in row) b]
fn on_land(t) => (let b = w.land.biomes[int(t.y)][int(t.x)], b != "ocean" and b != "beach");

[w.realm, w.lore, w.ruin];
w.towns |> ~.name;
[all([for (t in w.towns) on_land(t)]), len([for (t in w.towns where t.capital) t]), len(w.land.rivers) > 0,
 len(w.dungeon.rooms), len(w.emblem) > 0];
[for (b in kinds group by b as biome into g order by biome) [biome, len(g)]];
let dry = W.generate(2026, {erosion: false})^;
// erosion reshapes the land, but every name and the lore come from their own keys
[dry.land.height != w.land.height, dry.lore == w.lore, dry.dungeon == w.dungeon];
// Voronoi districts tile the map; climate changes biomes while preserving terrain.
[len(w.districts) == len(w.towns), abs(sum(w.districts |> abs(geometry.area(~))) - float(W.WIDTH * W.HEIGHT)) < 1e-6,
 len(w.land.river_polylines) > 0];
let climate = W.generate(2026, {climate: true})^;
[climate.land.height == w.land.height, climate.land.biomes != w.land.biomes,
 len(climate.land.temperature) == W.HEIGHT, w.land.temperature == null]
