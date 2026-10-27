// procgen.wfc (Lambda_Pkg_Proc_Gen.md §4, P4): simple-tiled and overlapping models.
// Section 2's exact grids are checked by oracle.py; the rest are invariants.
import rng: lambda.procgen.rng
import grid: lambda.procgen.grid
import wfc: lambda.procgen.wfc

fn show(t) => [for (row in t) join([for (v in row) string(v)], "")]
let coast = wfc.tiled(["~", "c", "#"], [["~", "~", "x"], ["~", "~", "y"], ["~", "c", "x"], ["c", "~", "x"], ["~", "c", "y"], ["c", "~", "y"],
    ["c", "c", "x"], ["c", "c", "y"], ["c", "#", "x"], ["#", "c", "x"], ["c", "#", "y"], ["#", "c", "y"], ["#", "#", "x"], ["#", "#", "y"]], [4, 1, 3])^
let stripes = [[0, 0, 1, 0, 0, 1], [0, 0, 1, 0, 0, 1], [1, 1, 1, 1, 1, 1], [0, 0, 1, 0, 0, 1], [0, 0, 1, 0, 0, 1], [1, 1, 1, 1, 1, 1]]
let checker = wfc.tiled_from_sample([[0, 1], [1, 0]])^

"1 models";
[checker.count, checker.weights, checker.masks[0]];
[coast.count, coast.weights, coast.masks[0], coast.masks[1]];
let ov = wfc.overlapping(stripes, {n: 3, symmetry: 1})^;
[ov.count, ov.weights, ov.patterns[0]];
let maze_sample = grid.maze(rng.key(1), {width: 4, height: 4})^;
[for (sym in [1, 2, 4, 8]) wfc.overlapping(maze_sample, {n: 3, symmetry: sym})^.count];
[wfc.overlapping(stripes, {n: 2, periodic_input: false, symmetry: 1})^.count, wfc.overlapping(stripes, {n: 2, symmetry: 1})^.count];

"2 exact grids";
show(wfc.generate(rng.key(1), checker, {width: 8, height: 4})^);
show(wfc.generate(rng.key(2), coast, {width: 16, height: 6})^);
show(wfc.generate(rng.key(3), ov, {width: 12, height: 6, periodic: true})^);
wfc.run(rng.key(3), ov, {width: 6, height: 3, periodic: true})^;

"3 constraints hold";
fn pairs_ok(model, cells, wrap) => (let h = len(cells), let w = len(cells[0]),
    all([for (y in 0 to (h - 1)) for (x in 0 to (w - 1)) for (d in [0, 1]) (let nx = x + 1 - d, let ny = y + d,
        if (not wrap and (nx >= w or ny >= h)) true else model.masks[d][cells[y][x]][cells[ny % h][nx % w]] == 1)]))
// smallest-domain collapse floods from the first pick, so maps are lopsided; this one holds all three
let sea = wfc.generate(rng.at(rng.key(4), 0), coast, {width: 30, height: 20})^;
[for (t in ["~", "c", "#"]) len([for (row in sea) for (v in row where v == t) v])];
[pairs_ok(coast, wfc.run(rng.at(rng.key(4), 0), coast, {width: 30, height: 20})^, false),
 not any([for (y in 0 to 19) for (x in 0 to 29) sea[y][x] == "~" and
    any([for (d in [[1, 0], [-1, 0], [0, 1], [0, -1]], let nx = x + d[0], let ny = y + d[1] where nx >= 0 and ny >= 0 and nx < 30 and ny < 20) sea[ny][nx] == "#"])])];
let mz = wfc.overlapping(maze_sample, {n: 3, symmetry: 8})^
let out = wfc.generate(rng.key(5), mz, {width: 14, height: 10, periodic: true})^
let pattern_keys = [for (p in mz.patterns) join([for (v in p) string(v)], ",")];
// periodic output: every 3 x 3 window is one of the sample's patterns
[pairs_ok(mz, wfc.run(rng.key(5), mz, {width: 14, height: 10, periodic: true})^, true),
 all([for (y in 0 to 9) for (x in 0 to 13) join([for (j in 0 to 2) for (i in 0 to 2) string(out[(y + j) % 10][(x + i) % 14])], ",") in pattern_keys])];
let learned = wfc.tiled_from_sample([[0, 0, 1, 2, 2], [0, 0, 1, 2, 2], [1, 1, 1, 1, 1]])^;
pairs_ok(learned, wfc.run(rng.key(6), learned, {width: 12, height: 8})^, false);

"4 weights and keys";
let free = wfc.tiled(["a", "b"], [["a", "a", "x"], ["a", "b", "x"], ["b", "a", "x"], ["b", "b", "x"], ["a", "a", "y"], ["a", "b", "y"], ["b", "a", "y"], ["b", "b", "y"]], [3, 1])^
let cells = [for (row in wfc.generate(rng.key(7), free, {width: 30, height: 20})^) for (v in row) v];
[abs(len([for (v in cells where v == "a") v]) / 600.0 - 0.75) < 0.05,
 wfc.run(rng.key(8), coast, {width: 10, height: 6})^ == wfc.run(rng.key(8), coast, {width: 10, height: 6})^,
 wfc.run(rng.key(8), coast, {width: 10, height: 6})^ != wfc.run(rng.key(9), coast, {width: 10, height: 6})^];

"5 rejected input";
[wfc.run(rng.key(1), wfc.tiled(["a"], [["a", "a", "y"]])^, {width: 3, height: 2}) ^ { "contradiction" },
 wfc.run(rng.key(1), coast, {width: 0, height: 2}) ^ { "size" }, wfc.run(rng.key(1), coast, {width: 200, height: 200}) ^ { "too big" },
 wfc.tiled(["a"], [["a", "z", "x"]]) ^ { "unknown tile" }, wfc.tiled(["a"], [["a", "a", "z"]]) ^ { "axis" },
 wfc.overlapping(stripes, {n: 9}) ^ { "n" }, wfc.overlapping(stripes, {symmetry: 3}) ^ { "symmetry" },
 wfc.tiled_from_sample([]) ^ { "empty" }]
