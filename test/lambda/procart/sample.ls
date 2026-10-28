// procart.sample: keyed point samplers (Lambda_Pkg_Proc_Art.md §4, PG2). Section 1's
// exact values are checked by oracle.py; the rest are invariants.
import rng: lambda.procart.rng
import sample: lambda.procart.sample

let k = rng.key(7)
fn d2(a, b) => (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])

"1 exact samples";
sample.poisson(k, {width: 30.0, height: 20.0, radius: 6.0})^;
sample.jitter(k, {width: 3.0, height: 2.0, cell: 1.0, jitter: 0.5})^;
sample.halton(k, 3, {width: 1.0, height: 1.0})^;
sample.r2(k, 3, {width: 1.0, height: 1.0})^;
sample.best_candidate(k, {width: 10.0, height: 10.0, count: 4})^;
sample.weighted(k, [[0.0, 1.0], [0.0, 3.0]], 4)^;

"2 radical inverse";
[for (i in 1 to 8) sample.radical_inverse(i, 2)];
[for (i in 1 to 5) sample.radical_inverse(i, 3)];

"3 Poisson: separation, bounds and coverage";
let pts = sample.poisson(k, {width: 100.0, height: 60.0, radius: 8.0})^
let probes = [for (y in 0 to 29) for (x in 0 to 49) [float(x) * 2.0 + 1.0, float(y) * 2.0 + 1.0]];
[min([for (i, a in pts) for (j, b in pts where j > i) d2(a, b)]) >= 64.0,
 all([for (p in pts) p[0] >= 0.0 and p[0] < 100.0 and p[1] >= 0.0 and p[1] < 60.0]),
 len([for (q in probes where min([for (p in pts) d2(p, q)]) < 64.0) q]) / len(probes) > 0.99,
 sample.poisson(rng.key(8), {width: 100.0, height: 60.0, radius: 8.0})^ != pts];

"4 jitter, sequences and best candidate";
let jit = sample.jitter(k, {width: 10.5, height: 4.0, cell: 1.0})^;
[len(jit), all([for (p in jit) p[0] < 10.5 and p[1] < 4.0])];
let hs = sample.halton(k, 64, {width: 8.0, height: 8.0})^
let rs = sample.r2(k, 64, {width: 8.0, height: 8.0})^;
[for (seq in [hs, rs]) len(unique([for (p in seq) [int(floor(p[0])), int(floor(p[1]))]]))];
let bc = sample.best_candidate(k, {width: 50.0, height: 50.0, count: 40})^;
[len(bc), min([for (i, a in bc) for (j, b in bc where j > i) d2(a, b)]) > 9.0];

"5 weighted: zero weight is never drawn";
let ws = sample.weighted(rng.key(9), [[0.0, 1.0, 0.0], [2.0, 0.0, 1.0]], 400)^;
[len([for (p in ws where p[1] < 1.0 and (p[0] < 1.0 or p[0] >= 2.0)) p]), len([for (p in ws where p[1] >= 1.0 and p[0] >= 1.0 and p[0] < 2.0) p]),
 abs(len([for (p in ws where p[1] >= 1.0 and p[0] < 1.0) p]) / 400.0 - 0.5) < 0.08];

"6 rejected options";
[sample.poisson(k, {width: 10.0, height: 10.0, radius: 0}) ^ { "radius" }, sample.poisson(k, {width: -1, height: 10.0, radius: 1}) ^ { "domain" },
 sample.poisson(k, {width: 10.0, height: 10.0, radius: 1, attempts: 0}) ^ { "attempts" },
 sample.poisson(k, {width: 1e6, height: 1e6, radius: 1}) ^ { "cells" }, sample.best_candidate(k, {width: 1, height: 1, count: 5000}) ^ { "count" },
 sample.weighted(k, [[0.0]], 1) ^ { "weight" }, sample.weighted(k, [[-1.0, 2.0]], 1) ^ { "negative" }];

// PG2: full ordered samples pin conservative coverage and partial boundary cells.
"7 coverage certificates preserve keyed samples";
[for (seed in [0, 17]) for (size in [[4.5, 3.0, 1.0], [0.3, 7.0, 1.0], [6.0, 4.0, 0.75]])
    for (attempts in [1, 20])
    sample.poisson(rng.key(seed), {width: size[0], height: size[1], radius: size[2], attempts: attempts})^]
