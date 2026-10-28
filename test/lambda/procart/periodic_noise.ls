// S10.2.4 / S12.1.1v3: periodic samples must be finite numbers, not equal error values.
import rng: lambda.procart.rng
import noise: lambda.procart.noise

fn finite(v) bool => v is number and v-v == 0.0;

[for (kind in ['simplex', 'perlin', 'value', 'white', 'cellular'])
    (let gen = noise.make(rng.key(4), {kind: kind, fractal: 'fbm', octaves: 3,
        period: [8.0, 6.0, 4.0], warp: {amplitude: 2.0, frequency: 0.03}})^,
     let samples = [noise.sample(gen, -3.25, 2.25), noise.sample3(gen, -3.25, 2.25, 1.25)],
     [all([for (v in samples) finite(v)]),
      samples[0] == noise.sample(gen, 4.75, 8.25),
      samples[1] == noise.sample3(gen, 4.75, 8.25, 5.25)])];

let gen = noise.make(rng.key(2026), {kind: 'simplex', fractal: 'fbm', octaves: 3,
    frequency: 0.1, period: [128.0, 128.0]})^
let pixels = noise.field(gen, 3, 2);
[len(pixels) == 2, len(pixels[0]) == 3, all([for (row in pixels) for (v in row) finite(v)])]
