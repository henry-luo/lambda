// procgen.rng: keys and draws are bit-exact (Lambda_Pkg_Proc_Gen.md PG2, PG3, Appendix A).
// The key values below were checked against an independent SplitMix64/FNV-1a reference.
import rng: lambda.procgen.rng

let k = rng.key(42)

"1 keys and derivation";
[k, rng.key("world"), rng.derive(k, "trees"), rng.derive(k, ["chunk", 12, -3]), rng.derive(k, 1.5)];
[rng.at(k, 5) == rng.derive(k, 5), rng.derive(k, ["a", "b"]) != rng.derive(k, ["b", "a"])];

"2 stream is the math.random sequence";
[rng.stream(42u64)[0] == math.random(42)[0], rng.stream(u64(-7))[0] == math.random(-7)[0],
 rng.stream(rng.stream(9u64)[1])[0] == math.random(math.random(9)[1])[0]];

"3 exact draws";
[rng.unit(k), rng.below(k, 6), rng.between(k, -1.0, 1.0), rng.chance(k, 0.5)];
rng.units(k, 4);
rng.perm(k, 8);

"4 distributions";
let us = rng.units(rng.key(1), 10000)
let faces = [for (i in 0 to 5999) rng.below(rng.at(rng.key(2), i), 6)];
[all([for (u in us) u >= 0.0 and u < 1.0]), abs(math.mean(us) - 0.5) < 0.01,
 all([for (f in 0 to 5) (let c = len([for (v in faces where v == f) v]), c > 900 and c < 1100)])];

"5 selection";
let deck = [for (i in 0 to 99) i];
[sort(rng.shuffle(k, deck)) == deck, rng.shuffle(k, deck) != deck, len(unique(rng.sample(k, deck, 10))) == 10,
 rng.pick(k, ["oak", "pine", "birch"]),
 not contains([for (i in 0 to 199) rng.weighted(rng.at(k, i), ["a", "b", "c"], [1, 0, 3])], "b")];

"6 tolerance tier: moments only";
let ns = [for (i in 0 to 4999) rng.normal(rng.at(rng.key(3), i))]
let es = [for (i in 0 to 4999) rng.exponential(rng.at(rng.key(4), i), 2.0)];
[abs(math.mean(ns)) < 0.05, abs(math.deviation(ns) - 1.0) < 0.05, abs(math.mean(es) - 0.5) < 0.03]
