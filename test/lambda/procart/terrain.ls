// procart.terrain (Lambda_Pkg_Proc_Art.md §4, P5). Section 1's exact fields are checked by
// oracle.py; the rest are hand-worked cases and invariants.
import rng: lambda.procart.rng
import field: lambda.procart.field
import terrain: lambda.procart.terrain

fn total(f) => sum([for (row in f) sum(row)])
fn steepest(f) => max([for (y, row in f) for (x, v in row) for (d in [[1, 0], [0, 1]], let nx = x + d[0], let ny = y + d[1]
    where nx < len(row) and ny < len(f)) abs(v - f[ny][nx])])

"1 exact fields";
let small = terrain.heightmap(rng.key(1), {width: 6, height: 4, island: 0.5})^;
small;
terrain.thermal(small, {iterations: 2, talus: 0.05})^;
terrain.fill([[3.0, 3.0, 3.0, 3.0], [3.0, 0.0, 1.0, 3.0], [3.0, 2.0, 0.5, 2.5], [3.0, 3.0, 3.0, 3.0]], {epsilon: 0.25});

"2 heightmaps";
let hm = terrain.heightmap(rng.key(2), {width: 40, height: 30})^
let isle = terrain.heightmap(rng.key(2), {width: 40, height: 30, island: 0.6})^
fn border_mean(f) => math.mean([for (y, row in f) for (x, v in row where x == 0 or y == 0 or x == len(row) - 1 or y == len(f) - 1) v]);
[field.bounds(hm), field.bounds(isle)[0] >= 0.0 and field.bounds(isle)[1] <= 1.0, border_mean(isle) < border_mean(hm),
 terrain.heightmap(rng.key(2), {width: 40, height: 30})^ == hm, terrain.heightmap(rng.key(3), {width: 1, height: 30}) ^ { "size" }];

"3 erosion";
let worn = terrain.thermal(hm, {iterations: 20})^;
[abs(total(worn) - total(hm)) < 1e-9, steepest(worn) < steepest(hm), terrain.thermal(hm, {iterations: 0})^ == hm];
let rained = terrain.hydraulic(rng.key(4), isle, {droplets: 600})^;
[rained != isle, field.bounds(rained)[0] >= field.bounds(isle)[0], field.bounds(rained)[1] <= field.bounds(isle)[1],
 terrain.hydraulic(rng.key(4), isle, {droplets: 600})^ == rained, terrain.hydraulic(rng.key(4), isle, {droplets: 0})^ == isle,
 terrain.hydraulic(rng.key(4), [[1.0]]) ^ { "size" }];

"4 filling, flow and rivers";
let filled = terrain.fill(isle, {sea_level: 0.4});
fn drains(f, sea) => all([for (y, row in f) for (x, v in row where x > 0 and y > 0 and x < len(row) - 1 and y < len(f) - 1 and v > sea)
    any([for (dy in -1 to 1) for (dx in -1 to 1) f[y + dy][x + dx] < v])]);
[all([for (y, row in filled) for (x, v in row) v >= isle[y][x]]), drains(filled, 0.4), not drains(isle, 0.4)];
terrain.accumulation([[3.0, 2.0, 1.0, 0.0]]);
terrain.accumulation([[2.0, 1.0, 2.0], [1.0, 0.0, 1.0], [2.0, 1.0, 2.0]]);
let streams = terrain.rivers(isle, {sea_level: 0.4, threshold: 8});
[len(streams.segments) > 0, all([for (s in streams.segments) s[4] >= 8 and isle[int(s[1])][int(s[0])] > 0.4]),
 all([for (s in streams.segments) streams.filled[int(s[3])][int(s[2])] < streams.filled[int(s[1])][int(s[0])]])];

"5 moisture and biomes";
let water = [for (row in isle) [for (v in row) v <= 0.4]]
let wet = terrain.moisture(rng.key(5), water)^;
[field.bounds(wet)[0] >= 0.0, field.bounds(wet)[1] <= 1.0];
terrain.biomes([[0.3, 0.41, 0.6, 0.6, 0.8, 0.8, 0.95, 0.95]], [[0.5, 0.5, 0.1, 0.9, 0.2, 0.7, 0.05, 0.6]])^;
[len(terrain.biome_colors()), terrain.biome_colors().river];

"6 the whole recipe";
let world = terrain.world(rng.key(6), {width: 48, height: 32})^
let kinds = [for (row in world.biomes) for (b in row) b];
[len(world.height), len(world.height[0]), len(world.rivers) > 0,
 [for (b in kinds group by b as biome into g order by biome) [biome, len(g)]]];
[terrain.world(rng.key(6), {width: 48, height: 32, erosion: false})^.height != world.height]
