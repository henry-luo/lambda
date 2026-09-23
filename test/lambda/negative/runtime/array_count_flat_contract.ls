// S11.1.1v3: `T[n]` is `T[]` with a fixed length. Since S11.1.6v2 made a
// counted bracket an array layer, the boundary's lane, rank and certificate
// fast paths proved `int[3]` from its leaf lane and rank alone and admitted
// `[1, 2]`, on every tier.
fn dyn(v) => v
let a: int[3] = dyn([1, 2])
"bound: " ++ string(a)
