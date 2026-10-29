// Keyed counter-based randomness (Lambda_Pkg_Proc_Art.md PG3, Appendix A).
// A draw is a pure hash of a u64 key, and keys derive from a parent key plus a
// path, so results never depend on evaluation order. The mixer is the
// SplitMix64 finalizer of lib/math_utils.h; only u64 wrapping arithmetic
// (S4.1.5), bxor and ushr are used, so every draw is exact-tier (PG2).

fn mix64(z: u64) u64 {
    let a = bxor(z, ushr(z, 30)) * 0xbf58476d1ce4e5b9u64
    let b = bxor(a, ushr(a, 27)) * 0x94d049bb133111ebu64;
    bxor(b, ushr(b, 31))
}

fn golden() u64 => 0x9e3779b97f4a7c15u64

// FNV-1a 64 over code points: names are short, and code points avoid an encode step.
fn fnv_from(s: string, h: u64, i: int) u64 =>
    if (i >= len(s)) h else fnv_from(s, bxor(h, u64(ord(slice(s, i, i + 1)))) * 0x100000001b3u64, i + 1)
fn fnv(s: string) u64 => fnv_from(s, 0xcbf29ce484222325u64, 0)

// ints keep their bits; any other part hashes its canonical text, so keys never fail.
fn part(p) u64 {
    if (p is string) fnv(p)
    else if (p is int or p is u64 or p is i64) u64(p)
    else fnv(format(p, 'json'))
}

fn step_bits(k: u64, bits: u64, i: int) u64 => mix64(bxor(k, bits) + golden() * u64(i + 1))
fn step(k: u64, p, i: int) u64 => step_bits(k, part(p), i)

// float in [0, 1) from the top 53 bits, the same mapping as math_splitmix64.
fn to_unit(z: u64) float => float(ushr(z, 11)) * 1.1102230246251565e-16

pub fn key(seed) u64 => mix64(part(seed) + golden())

fn derive_from(k: u64, path, i: int) u64 =>
    if (i >= len(path)) k else derive_from(step(k, path[i], i), path, i + 1)

// one part, or an array of parts folded with position-dependent multipliers.
pub fn derive_reference(k: u64, path) u64 => if (path is array) derive_from(k, path, 0) else step(k, path, 0)

fn native_part(p) bool => p is string or p is int or p is u64 or p is i64
pub fn derive(k: u64, path) u64 {
    let parts=if (path is array) path else [path];
    if (len(parts)<=1000000 and all(parts |> native_part(~))) math.rng_keys(k,[parts])[0]
    else derive_reference(k,path)
}
// the boundary validates the batch once; JSON-valued parts and larger work use the oracle.
pub fn derive_many(k: u64, paths) array => math.rng_keys(k,paths) ^ {
    [for (path in paths) derive_reference(k,path)]
}

// the infallible int case of derive: at(k, i) == derive(k, i).
pub fn at(k: u64, i: int) u64 => step_bits(k, u64(i), 0)

pub fn unit(k: u64) float => to_unit(mix64(k))

// the SplitMix64 sequence: [value, next]; stream(u64(s))[0] equals math.random(s)[0].
pub fn stream(k: u64) array {
    let s = k + golden();
    [to_unit(mix64(s)), s]
}

pub fn units_reference(k: u64, n: int) array => [for (i in 0 to (n - 1)) unit(at(k, i))]
pub fn units(k: u64, n: int) array => if (n>=0 and n<=1000000)
    math.rng_uniform([k],[for (i in 0 to (n-1)) i]) else units_reference(k,n)
pub fn units_for(keys,indices) array => if (len(keys)*len(indices)<=1000000)
    math.rng_uniform(keys,indices) else [for (k in keys) for (i in indices) unit(at(k,i))]

// floor(unit * n): bias is at most n / 2^53, far below any generator's resolution.
pub fn below(k: u64, n: int) int => int(floor(unit(k) * float(n)))
pub fn between(k: u64, lo: float, hi: float) float => lo + unit(k) * (hi - lo)
pub fn chance(k: u64, p: float) bool => unit(k) < p
pub fn pick(k: u64, items) => items[below(k, len(items))]

pub fn weighted(k: u64, items, weights) {
    let cumulative = math.cumsum(weights)
    let target = unit(k) * float(cumulative[len(cumulative) - 1]);
    items[[for (i, c in cumulative where c > target) i][0]]
}

// sort by keyed uniform ranks: a uniform permutation without mutation.
pub fn shuffle(k: u64, items) array => [for (e in sort([for (i, x in items) {r: unit(at(k, i)), x: x}],
    (e) => e.r)) e.x]
pub fn sample(k: u64, items, m: int) array => [for (i, x in shuffle(k, items) where i < m) x]
pub fn perm(k: u64, n: int) array => shuffle(k, [for (i in 0 to (n - 1)) i])

// tolerance tier (PG2): log and cos agree across platforms only to libm accuracy.
pub fn normal(k: u64, mean = 0.0, deviation = 1.0) float {
    let u1 = unit(at(k, 0))
    let u2 = unit(at(k, 1));
    float(mean) + float(deviation) * math.sqrt(-2.0 * math.log(1.0 - u1)) * math.cos(2.0 * math.pi * u2)
}
pub fn exponential(k: u64, rate = 1.0) float => -math.log(1.0 - unit(k)) / float(rate)
