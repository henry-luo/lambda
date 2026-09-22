// LR01-16: satellites that read different property keys. A satellite image is
// lowered without the receiving runtime (D8.5.1v7) and is published whenever
// its worker finishes, so its key suffix gets a place in the module's key table
// only at publication. When that place was fixed at compile time, an image
// published after another one resolved its keys through the other's suffix:
// `m.zeta` read `alpha`. The readers are interleaved, so each keeps running
// after the others publish; they own one or two keys, so a shifted suffix
// cannot line up by chance.
fn read_alpha(m) => m.alpha
fn read_beta_gamma(m) => m.beta + m.gamma
fn read_delta_epsilon(m) => m.delta * m.epsilon
fn read_zeta(m) => m.zeta

// built through a spread, so no row has a literal's shape and every read goes
// through the image's own property keys
let seed = {alpha: 0}
let rows = [for (i in 1 to 12) {*: seed, alpha: i, beta: i * 10, gamma: i * 100,
    delta: i, epsilon: 2, zeta: i * 1000}]
let reads = [for (r in rows)
    [read_alpha(r), read_beta_gamma(r), read_delta_epsilon(r), read_zeta(r)]]
reads
