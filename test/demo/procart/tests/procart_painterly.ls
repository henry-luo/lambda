// painterly brushes, image analysis and stroke plans are pure and replay from a seed.
import P: ~~.mod_painterly

fn finite(xs) bool => all([for (v in xs) v - v == 0.0])
fn in_unit(xs) bool => all([for (v in xs) v >= 0.0 and v <= 1.0]);

// brushes: fixed tiles per id, coverage in [0, 1], one or ANGLES stamp tiles per layer
[for (b in P.BRUSHES) (let tile = P.brush(b.id),
    [shape(tile) == [64, 64], in_unit(flatten(tile)), max(flatten(tile)) > 0.5, tile == P.brush(b.id),
     len(P.brush_set(tile, 15, false)) == 1, len(P.brush_set(tile, 15, true)) == P.ANGLES,
     len(P.brush_set(tile, 15, true)[3]) == 225])];
[P.brush("round") != P.brush("bristle"), P.brush("bristle") != P.brush("flat")];

// edge direction is a right angle from the gradient: vertical, horizontal and diagonal edges
[P.angle_bin({xx:[1.0],xy:[0.0],yy:[0.0]}, 0), P.angle_bin({xx:[0.0],xy:[0.0],yy:[1.0]}, 0),
 P.angle_bin({xx:[1.0],xy:[1.0],yy:[1.0]}, 0)];

let photo = input("test/demo/photo/assets/mountains.jpg", 'binary')^
let src = P.source(photo, 160)^
let field = P.analyze(src)
let n = src.width * src.height;
[src.width, src.height, len(src.linear) == 4 * n, in_unit(src.linear),
 len(field.sharpness) == n, max(field.sharpness) == 1.0, in_unit(field.sharpness),
 finite(field.xx) and finite(field.xy) and finite(field.yy), min(field.xx) >= 0.0 and min(field.yy) >= 0.0];

// plans: replayable, seed-sensitive, one or two layers, detail strokes drawn to sharp pixels
[for (r in P.CATALOG) (let plan = P.plan(field, 2026, r.id, 2),
    [len(plan.layers) == r.layers, plan == P.plan(field, 2026, r.id, 2),
     plan.layers[0].pixels != P.plan(field, 2027, r.id, 2).layers[0].pixels,
     len(plan.layers[0].pixels) == P.DENSITY[1], len(plan.layers[0].colors) == 3 * P.DENSITY[1],
     all([for (p in plan.layers[0].pixels) p >= 0 and p < n]),
     all([for (l in plan.layers) all([for (k in l.bins) k >= 0 and k < (if (r.oriented) P.ANGLES else 1)])]),
     all([for (l in plan.layers) finite(l.colors) and min(l.colors) >= 0.0 and max(l.colors) <= 1.15])])];

let detail = P.plan(field, 2026, "painterly", 2).layers[1]
let mean = sum(field.sharpness) / float(n);
[detail.size == P.BRUSH_SIZE div 4, len(detail.pixels) == P.DENSITY[1],
 sum([for (p in detail.pixels) field.sharpness[p]]) / float(len(detail.pixels)) > 1.5 * mean,
 len(unique(P.plan(field, 2026, "oriented", 2).layers[0].bins)) > 1]
