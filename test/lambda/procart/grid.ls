// procart.grid (Lambda_Pkg_Proc_Art.md §4, P2): tile grids are int[][] with 0 floor, 1 wall.
// Section 1's exact grids are checked by oracle.py; the rest are invariants.
import rng: lambda.procart.rng
import grid: lambda.procart.grid

let k = rng.key(5)
fn show(t) => [for (row in t) join([for (v in row) if (v == 1) "#" else "."], "")]
fn floor_count(t) => sum([for (row in t) for (v in row) if (v == 0) 1 else 0])
fn adjacent(a, b, w) => (a div w == b div w and abs(a - b) == 1) or abs(a - b) == w

"1 exact grids";
show(grid.random_fill(k, {width: 12, height: 4, fill: 0.5})^);
show(grid.automaton(k, {width: 24, height: 8})^);
show(grid.walk(k, {width: 16, height: 7, fill: 0.5})^);

"2 cellular automata";
let life = [[0, 1, 0, 0, 0], [0, 0, 1, 0, 0], [1, 1, 1, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]];
show(grid.evolve(life, {rule: "B3/S23", steps: 4, edge: 0})^);
show(grid.evolve([[0, 0, 0], [1, 1, 1], [0, 0, 0]], {rule: "B3/S23", steps: 1, edge: 0})^);
[grid.evolve(life, {steps: 0})^ == life, grid.evolve(life, {rule: "3/23"}) ^ { "rule" }, grid.evolve(life, {steps: -1}) ^ { "steps" }];

"3 regions and distances";
let two = [[0, 0, 1, 0], [1, 1, 1, 0], [0, 1, 0, 0]];
grid.regions(two);
grid.keep_largest(two);
grid.distances(two, [[3, 0]]);
grid.distances([[0, 1, 0]], [[0, 0], [2, 0]]);

"4 walks: a connected carve of the requested size inside a wall border";
let wk = grid.walk(rng.key(6), {width: 40, height: 20, fill: 0.35})^;
[floor_count(wk) == 240, len(grid.regions(wk).sizes),
 all([for (y, row in wk) for (x, v in row) v == 1 or (x > 0 and y > 0 and x < 39 and y < 19)])];

"5 BSP partitions tile the rectangle";
let leaves = grid.bsp(k, {width: 60, height: 40, min_size: 7})^
let cover = [for (y in 0 to 39) for (x in 0 to 59) len([for (r in leaves where x >= r[0] and x < r[0] + r[2] and y >= r[1] and y < r[1] + r[3]) r])];
[len(leaves), all([for (c in cover) c == 1]), all([for (r in leaves) r[2] >= 7 and r[3] >= 7])];
[len(grid.bsp(k, {width: 60, height: 40, max_depth: 0})^), grid.bsp(k, {width: 60, height: 40, min_size: 0}) ^ { "min_size" }];

"6 dungeons: one room per leaf, all floor connected";
let dg = grid.dungeon(rng.key(8), {width: 70, height: 40})^;
[len(dg.rooms), len(dg.corridors) == len(dg.rooms) - 1, len(grid.regions(dg.tiles).sizes),
 all([for (r in dg.rooms) r[0] >= 1 and r[1] >= 1 and r[0] + r[2] <= 69 and r[1] + r[3] <= 39])];

"7 mazes are spanning trees";
fn maze_check(m) {
    let edges = grid.maze_edges(rng.key(9), {width: 13, height: 9, method: m})^
    let tiles = grid.maze(rng.key(9), {width: 13, height: 9, method: m})^
    let dist = grid.distances(tiles, [[1, 1]]);
    [m, len(edges) == 13 * 9 - 1, all([for (e in edges) adjacent(e[0], e[1], 13)]), len(grid.regions(tiles).sizes),
     len(tiles) == 19 and len(tiles[0]) == 27,
     // in a tree every step between neighbouring floor tiles changes the distance by exactly one
     all([for (y in 1 to 17) for (x in 1 to 25) tiles[y][x] != 0 or tiles[y][x + 1] != 0 or abs(dist[y][x] - dist[y][x + 1]) == 1])]
}
[for (m in ['backtracker', 'kruskal', 'prim', 'wilson']) maze_check(m)];
grid.maze(k, {width: 3, height: 2, method: 'kruskal'})^;
[grid.maze(k, {width: 200, height: 200}) ^ { "size" }, grid.maze(k, {width: 3, height: 2, method: 'x'}) ^ { "method" }];

"8 Wilson draws uniformly from the 15 spanning trees of a 3 x 2 grid";
let trees = [for (i in 0 to 2999) join([for (e in sort(grid.maze_edges(rng.at(rng.key(10), i), {width: 3, height: 2, method: 'wilson'})^ |>
    (if (~[0] < ~[1]) ~ else [~[1], ~[0]]), (e) => e)) string(e[0]) ++ "-" ++ string(e[1])], " ")]
let counts = [for (t in trees group by t as tree into g) len(g)];
[len(counts), sum([for (c in counts) (float(c) - 200.0) * (float(c) - 200.0) / 200.0]) < 36.1]
