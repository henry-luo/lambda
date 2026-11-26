// Willow Cove as pure data: the shore outline, the floor's depth and colour, and where rocks,
// lily pads, plants and the jetty sit. Water lies at y = 0; x runs east and z south, and the
// camera looks north. Everything is seeded, so the cove is the same on every run.
import noise: lambda.procart.noise
import rng: lambda.procart.rng
import color: lambda.color
import u: ~~.midtown.mod_util

// a literal sRGB hex in linear light, the space vertex colours are shaded in (a malformed literal shows magenta)
pub fn col(hex) => u.lin(slice(color.parse(hex) ^ { [1.0, 0.0, 1.0, 1.0] }, 0, 3))

pub let SEED = 20261125
let K = rng.key(SEED) ^ { u64(SEED) }
let P = noise.table(rng.key(SEED + 1))
let Q = noise.table(rng.key(SEED + 2))
// the j-th seeded unit number of item i
pub fn roll(i: int, j: int) float^ => rng.unit(rng.at(K, i * 64 + j))
pub fn between(i: int, j: int, lo: float, hi: float) float^ => lo + roll(i, j)^ * (hi - lo)

// ---- shape: an irregular ellipse; shore() is 0 at the centre and 1 on the waterline
pub let RX = 21.0
pub let RZ = 15.0
pub let EXTENT = [30.0, 22.5]          // terrain half-size
pub fn shore(x: float, z: float) float => (let a = math.atan2(z, x),
    sqrt(u.sq(x / RX) + u.sq(z / RZ)) * (1.0 + 0.07 * sin(3.0 * a + 0.6) + 0.045 * sin(5.0 * a + 2.1) + 0.03 * sin(9.0 * a)))
fn smooth(e: float) float => (let c = u.clamp(e, 0.0, 1.0), c * c * (3.0 - 2.0 * c))
// floor height: a bowl ~2.2 m deep that shoals to a beach at the waterline, then grassy banks
pub fn floor_y(x: float, z: float) float^ {
    let r = shore(x, z)
    let bumps = 0.3 * noise.perlin2(P, x * 0.11, z * 0.11)^ + 0.1 * noise.perlin2(Q, x * 0.37, z * 0.37)^
    let base = if (r <= 1.0) -0.15 - 2.0 * smooth((1.0 - r) / 0.55) else min(0.9, -0.15 + (r - 1.0) * 4.5)
    let roughness = if (r <= 1.0) 0.35 + 0.65 * smooth((1.0 - r) / 0.3) else 0.6
    return base + bumps * roughness
}
pub fn depth(x: float, z: float) float^ => max(0.0, 0.0 - floor_y(x, z)^)

// floor colour in linear light: grass banks, a beach, sand shoals with moss, greener deeps
let GRASS = u.lin([0.42, 0.58, 0.27])
let GRASS_DARK = u.lin([0.27, 0.44, 0.2])
let BEACH = u.lin([0.83, 0.78, 0.64])
let SAND = u.lin([0.76, 0.72, 0.58])
let MOSS = u.lin([0.38, 0.5, 0.3])
let DEEP = u.lin([0.27, 0.36, 0.24])
fn mix3(a, b, k: float) => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * k]
pub fn floor_color(x: float, z: float, y: float) any^ {
    let patch = 0.5 + 0.5 * noise.perlin2(Q, x * 0.23 + 7.0, z * 0.23 - 3.0)^
    let speck = 0.5 + 0.5 * noise.perlin2(P, x * 1.7, z * 1.7)^
    if (y > 0.1) mix3(mix3(GRASS, GRASS_DARK, patch), BEACH, smooth((0.26 - y) / 0.16) * 0.85)
    else if (y > -0.12) mix3(BEACH, SAND, speck * 0.4)
    else (let deep = smooth((0.0 - y - 0.5) / 1.4), let moss = smooth((patch - 0.45) / 0.25),
        mix3(mix3(mix3(SAND, MOSS, moss), DEEP, deep * 0.75), BEACH, speck * 0.15))
}

// ---- furniture: positions are polar fractions of the shore radius so everything stays wet
fn polar(i: int, lo: float, hi: float) any^ => (let a = roll(i, 0)^ * u.TAU, let f = lo + (hi - lo) * sqrt(roll(i, 1)^),
    {x: cos(a) * RX * f, z: sin(a) * RZ * f, a: a})
// the jetty reaches into the south shore; the boat starts beside it
pub let JETTY = {x: -2.6, z0: 16.2, z1: 11.4, width: 1.5, deck: 0.38}
pub let START = {x: 0.2, z: 10.2, heading: math.pi}
fn clear_of_start(p) bool^ => u.dist(p.x, p.z, START.x, START.z) > 4.5 and u.dist(p.x, p.z, JETTY.x, (JETTY.z0 + JETTY.z1) / 2.0) > 3.5

let HEAPS = [[-14.5, -6.0], [13.0, 5.5], [6.0, -11.0], [-8.0, 7.5], [16.5, -3.5]]
// rocks: radius r, squash h (height / width), yaw; they rest sunk a little into the floor
fn rock(i: int) any^ {
    // half the rocks gather in three heaps near the banks
    let heap = HEAPS[i % 5]
    let p = if (i % 2 == 0) polar(100 + i, 0.3, 1.0)^ else (let a = roll(100 + i, 0)^ * u.TAU, let d = 0.6 + 2.2 * roll(100 + i, 1)^,
        {x: heap[0] + cos(a) * d, z: heap[1] + sin(a) * d})
    let r = between(100 + i, 2, 0.4, 1.15)^ * (0.75 + 0.55 * shore(p.x, p.z))
    let h = between(100 + i, 3, 0.45, 0.8)^
    let y = floor_y(p.x, p.z)^ - 0.25 * r * h
    return {i: i, x: p.x, z: p.z, y: y, r: r, h: h, yaw: roll(100 + i, 4)^ * u.TAU, top: y + r * h}
}
pub let ROCKS = [for (i in 0 to 71, let k = rock(i) ^ { null } where k != null and (clear_of_start(k) ^ { false })) k]
// rocks whose tops reach near the surface stop the boat, the otter and fish
pub let OBSTACLES = [for (k in ROCKS where k.top > -0.35) {x: k.x, z: k.z, r: k.r * 0.95}]

// water lilies: pads in clusters near the banks and scattered singles across the shallows; about one in
// five carries a white, pink or yellow lily, and pads vary from fresh green to old gold
let LILY_COLOURS = ["white", "pink", "yellow", "pink"]
fn pad(n: int, cx: float, cz: float, spread: float) any^ => (let a = roll(n, 0)^ * u.TAU, let d = sqrt(roll(n, 1)^) * spread,
    {x: cx + cos(a) * d, z: cz + sin(a) * d, r: between(n, 2, 0.13, 0.3)^, yaw: roll(n, 3)^ * u.TAU,
     flower: if (roll(n, 4)^ < 0.2) LILY_COLOURS[int(floor(roll(n, 5)^ * 4.0))] else null, age: roll(n, 6)^})
fn cluster(c: int) any^ => (let p = polar(300 + c, 0.55, 0.93)^, [for (j in 0 to 15 where (roll(400 + c * 20 + j, 7) ^ { 1.0 }) < 0.85) pad(400 + c * 20 + j, p.x, p.z, 2.8)^])
let SINGLES = [for (i in 0 to 41, let p = polar(700 + i, 0.45, 0.95) ^ { null } where p != null) pad(700 + i, p.x, p.z, 0.0) ^ { null }]
fn wet_open(k) bool^ => (clear_of_start(k)^ and depth(k.x, k.z)^ > 0.12)
pub let PADS = [for (k in [*[for (c in 0 to 10, q in (cluster(c) ^ { [] })) q], *SINGLES] where k != null and (wet_open(k) ^ { false })) k]
// lotus stands in the shallows: each a seeded clump of round leaves, flowers and buds
pub let LOTUS = [for (i in 0 to 5, let p = polar(800 + i, 0.72, 0.92) ^ { null } where p != null and (wet_open(p) ^ { false })) {*: p, i: i}]

// underwater plants and creatures: seagrass clumps, tube sponges, starfish, urchins, pebbles
fn spot(i: int, lo: float, hi: float) any^ => (let p = polar(i, lo, hi)^, {*: p, y: floor_y(p.x, p.z)^, i: i})
pub let GRASS_CLUMPS = [for (i in 0 to 95, let p = spot(1000 + i, 0.3, 0.94) ^ { null } where p != null and p.y < -0.35) p]
pub let TUBES = [for (i in 0 to 23, let p = spot(1100 + i, 0.3, 0.85) ^ { null } where p != null and p.y < -0.6) p]
pub let STARS = [for (i in 0 to 23, let p = spot(1200 + i, 0.4, 0.95) ^ { null } where p != null and p.y < -0.25 and p.y > -1.4) p]
pub let URCHINS = [for (i in 0 to 13, let p = spot(1300 + i, 0.3, 0.9) ^ { null } where p != null and p.y < -0.5) p]
pub let PEBBLES = [for (i in 0 to 69, let p = spot(1400 + i, 0.2, 1.0) ^ { null } where p != null and p.y < -0.1) p]
// leafy rosettes, tall kelp in the deeps, branching sea fans and moss balls
pub let LETTUCE = [for (i in 0 to 55, let p = spot(1700 + i, 0.3, 0.9) ^ { null } where p != null and p.y < -0.4) p]
pub let KELP = [for (i in 0 to 39, let p = spot(1800 + i, 0.0, 0.7) ^ { null } where p != null and p.y < -1.2) p]
pub let FANS = [for (i in 0 to 19, let p = spot(1900 + i, 0.25, 0.85) ^ { null } where p != null and p.y < -0.8) p]
pub let MOSS_BALLS = [for (i in 0 to 49, let p = spot(2100 + i, 0.25, 0.95) ^ { null } where p != null and p.y < -0.3) p]
// broad-leaved sword plants, and the carpet of leafy sheets over most of the floor
pub let SWORDS = [for (i in 0 to 47, let p = spot(2200 + i, 0.2, 0.9) ^ { null } where p != null and p.y < -0.45) p]
pub let CARPET = [for (i in 0 to 899, let p = spot(2400 + i, 0.0, 0.93) ^ { null } where p != null and p.y < -0.42) p]
// reeds and cattails along the waterline, grass tufts on the banks
// a point on a ring around the shore at a seeded angle and radius fraction
fn rim(i: int, lo: float, hi: float) any^ => (let a = roll(i, 0)^ * u.TAU, let f = between(i, 1, lo, hi)^,
    let x = cos(a) * RX * f, let z = sin(a) * RZ * f, {x: x, z: z, y: floor_y(x, z)^, i: i})
pub let REEDS = [for (i in 0 to 41, let p = rim(1500 + i, 1.0, 1.07) ^ { null } where p != null and p.y > -0.25 and (clear_of_start(p) ^ { false })) p]
pub let TUFTS = [for (i in 0 to 89, let p = rim(1600 + i, 1.08, 1.45) ^ { null }
    where p != null and p.y > 0.1 and abs(p.x) < EXTENT[0] - 1.0 and abs(p.z) < EXTENT[1] - 1.0) p]

// ---- the boat's world: keep it on water deeper than its draft and off emergent rocks
pub let DRAFT = 0.32
pub fn navigable(x: float, z: float) bool^ => depth(x, z)^ > DRAFT and shore(x, z) < 0.985
