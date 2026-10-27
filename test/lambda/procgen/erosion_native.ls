// PG7.3: compare whole fields, not just mass/range, against the retained oracle.
import rng: lambda.procgen.rng
import field: lambda.procgen.field
import terrain: lambda.procgen.terrain

"native hydraulic";
math.hydraulic is fn;
let land = field.make(9, 7, (x, y) => rng.unit(rng.at(rng.key(9), y * 9 + x)));
all([for (seed in 0 to 5) for (batch in [1, 7, 64]) for (steps in [0, 1, 24])
    terrain.hydraulic(rng.key(seed), land, {droplets: 35, batch: batch, steps: steps})^ ==
    terrain.hydraulic_reference(rng.key(seed), land, {droplets: 35, batch: batch, steps: steps})^]);
all([for (params in [{inertia: 0.0, erode: 0.0}, {inertia: 1.0}, {evaporate: 1.0},
    {capacity: 0.0, min_capacity: 0.0}, {deposit: 1.0, erode: 1.0, gravity: 0.0}])
    terrain.hydraulic(rng.key(7), land, {droplets: 100, batch: 17, *: params})^ ==
    terrain.hydraulic_reference(rng.key(7), land, {droplets: 100, batch: 17, *: params})^]);
all([for (height in [[[0.0, 0.0], [0.0, 0.0]], [[-0.0, 0.0], [0.0, -0.0]],
    [[3.0, 2.0], [1.0, 0.0]], [[-3.0, -2.0], [-1.0, 0.0]]])
    format(terrain.hydraulic(rng.key(8), height, {droplets: 30, batch: 7})^, 'json') ==
    format(terrain.hydraulic_reference(rng.key(8), height, {droplets: 30, batch: 7})^, 'json')]);
terrain.hydraulic(rng.key(1), land, {droplets: 0})^ == land;
"bounded errors";
[terrain.hydraulic(rng.key(1), [[1.0, 2.0], [3.0]]) ^ { "ragged" },
 terrain.hydraulic(rng.key(1), land, {steps: 1001}) ^ { "steps" },
 terrain.hydraulic(rng.key(1), land, {inertia: 2.0}) ^ { "rate" },
 terrain.hydraulic(rng.key(1), [[1.0, inf], [0.0, 1.0]]) ^ { "finite" },
 math.hydraulic(rng.key(1), [0.0], [2, 2, 1, 1, 1], [0.05, 4.0, 0.01, 0.3, 0.3, 0.02, 4.0]) ^ { "length" }]
