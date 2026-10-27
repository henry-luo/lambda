// procgen.lsystem (Lambda_Pkg_Proc_Gen.md §4, P3). Rewriting is exact-tier; the turtle
// uses cos/sin (tolerance tier, PG2), so its coordinates are printed rounded.
import rng: lambda.procgen.rng
import lsys: lambda.procgen.lsystem

fn r9(v) => round(v * 1e9) / 1e9
fn tidy(seg) => [for (v in slice(seg, 0, 4)) r9(v)] ++ [seg[4]]

"1 deterministic rewriting";
[for (n in 0 to 7) len(lsys.expand("A", {A: "AB", B: "A"}, n)^)];
[lsys.expand("FX", {X: "X+YF+", Y: "-FX-Y"}, 3)^];
[lsys.expand("F", {F: "F+F-F-F+F"}, 2)^];
[lsys.expand("ab", {}, 3)^];

"2 stochastic rewriting is keyed";
[lsys.expand("F", {F: [[1, "F[+F]"], [2, "F[-F]F"]]}, 2, rng.key(1))^];
[lsys.expand("F", {F: [[1, "F[+F]"], [2, "F[-F]F"]]}, 2, rng.key(1))^ == lsys.expand("F", {F: [[1, "F[+F]"], [2, "F[-F]F"]]}, 2, rng.key(1))^];
let picks = [for (i in 0 to 2999) lsys.expand("F", {F: [[1, "a"], [3, "b"]]}, 1, rng.at(rng.key(2), i))^];
[abs(len([for (p in picks where p == "b") p]) / 3000.0 - 0.75) < 0.03];

"3 parametric modules";
let tree = lsys.expand([["A", 4.0]], {A: (m) => if (m[1] > 1.0) [["F", m[1]], "[", "+", ["A", m[1] * 0.5], "]", ["A", m[1] * 0.5]] else [["F", m[1]]]}, 3)^;
tree;
lsys.expand(["B", "x"], {B: (m) => "BB"}, 2)^;

"4 turtle";
let square = lsys.turtle("F+F+F+F", {step: 2.0})^;
[[for (s in square.segments) tidy(s)], [for (v in square.end) r9(v)]];
let plant = lsys.turtle("F[+F[-F]]F", {angle: 30.0, heading: 90.0})^;
[for (s in plant.segments) tidy(s)];
[for (line in lsys.polylines(plant.segments)) len(line)];
let gapped = lsys.turtle("FfF|F")^;
[len(gapped.segments), len(lsys.polylines(gapped.segments)), [for (v in gapped.end) r9(v)]];
[for (s in lsys.turtle([["F", 3.0], ["+", 45.0], ["F", 1.0], "-", "G"], {angle: 45.0})^.segments) tidy(s)];
lsys.turtle("XYZ")^;

"5 rejected input";
[lsys.turtle("F[F") ^ { "unbalanced" }, lsys.turtle("F]F[") ^ { "unbalanced" }, lsys.expand("F", {F: "FF"}, 20) ^ { "cap" },
 lsys.expand("F", {}, -1) ^ { "order" }, lsys.expand("F", {F: [[1, "a"]]}, 1) ^ { "key" }, lsys.expand(3, {}, 1) ^ { "axiom" }]
