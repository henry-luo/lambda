// PG7.3: the native counting kernel must reproduce the retained Lambda oracle,
// including contradictions, retries and periodic self-neighbours (D6.4.1).
import rng: lambda.procart.rng
import wfc: lambda.procart.wfc
import native_math: lambda.math

fn outcome(k, model, options, reference) =>
    if (reference) (wfc.run_reference(k, model, options) ^ { "contradiction" })
    else (wfc.run(k, model, options) ^ { "contradiction" })
fn agrees(k, model, options) bool => outcome(k, model, options, false) == outcome(k, model, options, true)
fn free_model(weights) => wfc.tiled([0, 1, 2],
    [for (d in ["x", "y"]) for (a in 0 to 2) for (b in 0 to 2) [a, b, d]], weights)^

"native kernel";
[math.wfc is fn, lambda.math.wfc is fn, native_math.wfc is fn,
 math.fft is fn, lambda.sys.len is fn, math.__missing_procgen_kernel is fn];
fn shadowed() => (let math = {wfc: 7}, math.wfc);
shadowed();
let checker = wfc.tiled_from_sample([[0, 1], [1, 0]])^
let coast = wfc.tiled_from_sample([[0, 0, 1, 2, 2], [0, 0, 1, 2, 2], [1, 1, 1, 1, 1]])^;

"periodic, degenerate and contradictory domains";
all([for (seed in 0 to 11) for (size in [[1, 1], [1, 6], [6, 1], [4, 6], [5, 3]]) for (wrap in [false, true])
    agrees(rng.key(seed), checker, {width: size[0], height: size[1], periodic: wrap, retries: 3})]);
[outcome(rng.key(1), checker, {width: 3, height: 3, periodic: true}, false),
 wfc.run(rng.key(1), wfc.tiled([7], [[7, 7, "x"], [7, 7, "y"]])^, {width: 1, height: 1, periodic: true})^];

"weighted choices and tie order";
all([for (weights in [[3, 1, 2], [0.1, 0.2, 0.7], [1, 0.25, 2], [1e-200, 2e-200, 3e-200]])
    for (seed in 0 to 7) agrees(rng.key(seed), free_model(weights), {width: 7, height: 5})]);
all([for (seed in 0 to 11) for (wrap in [false, true])
    agrees(rng.key(seed), coast, {width: 8, height: 5, periodic: wrap, retries: 4})]);

"overlapping models";
let sample = [[0, 0, 1, 0], [0, 1, 1, 0], [1, 1, 1, 1], [0, 0, 1, 0]];
all([for (symmetry in [1, 2, 4, 8]) (let model = wfc.overlapping(sample, {n: 2, symmetry: symmetry})^,
    all([for (seed in 0 to 3) for (wrap in [false, true])
        agrees(rng.key(seed), model, {width: 5, height: 4, periodic: wrap, retries: 3})]))]);

"keyed random constraint models";
all([for (i in 0 to 7) (let k = rng.key(i),
    let rules = [for (d, axis in ["x", "y"]) for (a in 0 to 2) for (b in 0 to 2)
        if (rng.chance(rng.at(k, d * 9 + a * 3 + b), 0.6)) [a, b, axis] else null] |: ~ != null,
    let model = wfc.tiled([0, 1, 2], rules, [2, 1, 3])^,
    all([for (seed in 0 to 5) for (wrap in [false, true])
        agrees(rng.key(seed), model, {width: 4, height: 3, periodic: wrap, retries: 4})]))]);

"reference fallback for other weight domains";
all([for (weights in [[0, 1, 2], [9007199254740991, 1, 1]]) for (seed in 0 to 3)
    agrees(rng.key(seed), free_model(weights), {width: 4, height: 3})]);

"boxed flat result and boundary validation";
let k = rng.key(7);
math.wfc(k, checker.weights, checker.masks, [4, 4, true, 3])^ ==
    [for (row in wfc.run_reference(k, checker, {width: 4, height: 4, periodic: true, retries: 3})^) for (p in row) p];
[math.wfc(7, checker.weights, checker.masks, [4, 4, false, 3]) ^ { "key" },
 math.wfc(k, [], [], [4, 4, false, 3]) ^ { "empty" },
 math.wfc(k, [0, 1], checker.masks, [4, 4, false, 3]) ^ { "weight" },
 math.wfc(k, [1e308, 1e308], checker.masks, [4, 4, false, 3]) ^ { "sum" },
 math.wfc(k, checker.weights, [], [4, 4, false, 3]) ^ { "masks" },
 math.wfc(k, checker.weights, [[[1]]], [4, 4, false, 3]) ^ { "shape" },
 math.wfc(k, checker.weights, checker.masks, [10001, 1, false, 3]) ^ { "size" },
 math.wfc(k, checker.weights, checker.masks, [4, 4, 1, 3]) ^ { "periodic" },
 math.wfc(k, checker.weights, checker.masks, [4, 4, false, 0]) ^ { "retries" },
 math.wfc(k, checker.weights, checker.masks, [3, 3, true, 2]) ^ { "contradiction" }];
[math.wfc(null, checker.weights, checker.masks, [4, 4, false, 3]),
 math.wfc(k, null, checker.masks, [4, 4, false, 3])]
