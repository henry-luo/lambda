// Mesh buffers for the land and roads beyond the city: terrain chunks over the near grid and a far
// mesh to the horizon, road ribbons and their verges, the gateway roundabout, the truss and the
// cable-stayed bridges, the tunnel with its portals and lamps, and the guardrails. Buffers follow
// mod_mesh: {positions, normals, uvs, indices[, colors]}, counter-clockwise seen from outside.
import u: .mod_util
import m: .mod_mesh
import t: lambda.scene3d.transform
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import region: .mod_region

// ---- terrain colour: grass with a soft wash of variation, crops on the farm parcels, a darker floor
// under the woods, rock where it is steep, sand by the sea and snow on the high peaks. The palette is
// written in display (sRGB) values and blended in linear light, the space vertex colours shade in.
fn lin(c) => [for (x in c) math.pow(x, 2.2)]
let GRASS = lin([0.36, 0.5, 0.21])
let MEADOW = lin([0.5, 0.58, 0.27])
let DRY = lin([0.64, 0.6, 0.38])
let WOOD_FLOOR = lin([0.22, 0.31, 0.14])
let ROCK = lin([0.5, 0.48, 0.45])
let SAND = lin([0.84, 0.77, 0.6])
let SEABED = lin([0.48, 0.44, 0.35])
let SNOW = lin([0.92, 0.94, 0.97])
let CROP_COLOR = {wheat: lin([0.78, 0.66, 0.34]), green: lin([0.42, 0.56, 0.2]), plowed: lin([0.42, 0.32, 0.22]), canola: lin([0.86, 0.78, 0.22]),
    meadow: lin([0.52, 0.62, 0.29]), hay: lin([0.7, 0.62, 0.4])}
// a colour channel triple mixed toward another by k (a number or an array)
fn mix(a, b, k) any^ => [land.mix(a[0], b[0], k), land.mix(a[1], b[1], k), land.mix(a[2], b[2], k)]
fn wash(x, z) any^ => 0.5 + 0.5 * sin(x * 0.021 + 1.3 * sin(z * 0.017)) * cos(z * 0.026 - 0.8 * sin(x * 0.013))
// the colour of crop index idx, summed from one-hot weights so an array of indices works too
let CROP_TABLE = [for (name in region.CROPS) CROP_COLOR[name]]
fn crop_color(idx) any^ => reduce([for (c, rgb in CROP_TABLE) (let one = land.clamp01(1.0 - abs(idx - c) * 4.0), [rgb[0] * one, rgb[1] * one, rgb[2] * one])],
    (acc, t) any^ => [acc[0] + t[0], acc[1] + t[1], acc[2] + t[2]])
// the terrain colour as [r, g, b], each a number or (for the whole grid) an array; it takes the
// coastline cz and beach weight sand above the point
pub fn terrain_color(x, z, h, ny, cz, sand) any^ {
    let w = wash(x, z)
    let base = mix(mix(GRASS, MEADOW, w), DRY, land.smooth(0.55, 0.95, wash(z * 1.7, x * 1.3)) * 0.45)
    let p = region.parcel(x, z)
    let edge = land.vmin(land.vmin(p.u, 1.0 - p.u) * region.FARM.w, land.vmin(p.v, 1.0 - p.v) * region.FARM.d)
    let crops = region.farm_weight(x, z, h) * land.step(ny - 0.9) * land.smooth(1.0, 4.0, edge)
    let ground = mix(mix(base, WOOD_FLOOR, 0.75 * region.wood_weight(x, z, h, cz)), crop_color(region.crop_index(p)), crops)
    let sandy = land.clamp01(sand * (1.0 - land.smooth(3.0, 7.0, h)) + (1.0 - land.smooth(0.2, 2.2, h)) * 0.6)
    let shore = mix(mix(ground, SAND, sandy), SEABED, 1.0 - land.smooth(land.SEA - 0.8, land.SEA + 0.4, h))
    let rocky = mix(shore, ROCK, 1.0 - land.smooth(0.62, 0.84, ny));
    mix(rocky, SNOW, land.smooth(290.0, 360.0, h) * land.smooth(0.55, 0.8, ny))
}
// interleave equal-length channel arrays into one flat buffer: [a0, b0, c0, a1, b1, c1, ...]
fn interleave(channels) any^ => flatten(transpose(channels))
// triangle indices for a grid of w columns and `rows` rows, two per cell, (a, c, b) and (b, c, d). A
// triangle with a `gone` corner, or with every corner `hidden` (both 0/1 per vertex), collapses onto
// one vertex and draws nothing, so the buffer comes from array arithmetic rather than a filtered loop
fn grid_indices(w, rows, gone, hidden) any^ {
    let n = w * (rows - 1) - 1
    let k = [for (q in 0 to (n - 1)) q]
    let edge = [for (q in 0 to (n - 1)) if (q % w == w - 1) 0 else 1]
    let g0 = slice(gone, 0, n), g1 = slice(gone, 1, n + 1), gw = slice(gone, w, n + w), gd = slice(gone, w + 1, n + w + 1)
    let h0 = slice(hidden, 0, n), h1 = slice(hidden, 1, n + 1), hw = slice(hidden, w, n + w), hd = slice(hidden, w + 1, n + w + 1)
    let keep0 = edge * (1 - g0) * (1 - gw) * (1 - g1) * (1 - h0 * hw * h1)
    let keep1 = edge * (1 - g1) * (1 - gw) * (1 - gd) * (1 - h1 * hw * hd);
    interleave([k, k + w * keep0, k + keep0, k + 1, k + 1 + (w - 1) * keep1, k + 1 + w * keep1])
}
// unit normals of a height grid (w columns, `step` apart) as [x, y, z] channel arrays, from central
// differences (one-sided at the edges)
fn grid_normals(hs, w, rows, step) any^ {
    let hx = [for (k, h in hs, let i = k % w) (hs[if (i < w - 1) k + 1 else k] - hs[if (i > 0) k - 1 else k]) / (if (i > 0 and i < w - 1) 2.0 * step else step)]
    let hz = [for (k, h in hs, let j = k div w) (hs[if (j < rows - 1) k + w else k] - hs[if (j > 0) k - w else k]) / (if (j > 0 and j < rows - 1) 2.0 * step else step)]
    let l = math.sqrt(1.0 + hx * hx + hz * hz);
    [0.0 - hx / l, 1.0 / l, 0.0 - hz / l]
}

// ---- the near grid, in chunks so the view and each shadow cascade cull what they cannot see (OR7)
let NX = land.NX
let NZ = land.NZ
let H = region.HEIGHT
// channel arrays get names of their own: indexing a nested array element by element is slow
let NORMALS = grid_normals(H, NX, NZ, land.STEP) ^ { [[], [], []] }
let N0 = NORMALS[0]
let N1 = NORMALS[1]
let N2 = NORMALS[2]
let COLORS = terrain_color(land.GX, land.GZ, H, N1, land.GCZ, land.GSAND) ^ { [[], [], []] }
let C0 = COLORS[0]
let C1 = COLORS[1]
let C2 = COLORS[2]
let GX = land.GX
let GZ = land.GZ
// ground the city, its barrier and the promenade already cover
fn covered(x, z) => abs(x) <= city.LIMIT + 1.0 and z >= 0.0 - city.LIMIT - 1.0 and z <= city.SHORE + 1.2
fn wet(h) => h < land.SEA - 0.6
fn hidden(k) any^ => (let x = land.X0 + (k % NX) * land.STEP, let z = land.Z0 + (k div NX) * land.STEP, covered(x, z) or wet(H[k]))
let HIDDEN = [for (k in 0 to (NX * NZ - 1)) if (hidden(k)) 1 else 0]
let GONE = [for (d in region.DROPPED) if (d) 1 else 0]
// one chunk's rows of a per-vertex array: columns i0..i1 of rows j0..j1, by native slices
fn rows(values, i0, i1, j0, j1) any^ => reduce([for (j in j0 to j1) slice(values, j * NX + i0, j * NX + i1 + 1)], (acc, r) any^ => concat(acc, r))
pub let CHUNK_X = 60
pub let CHUNK_Z = 50
pub let CHUNKS = [for (j0 in [for (k in 0 to ((NZ - 2) div CHUNK_Z)) k * CHUNK_Z], i0 in [for (k in 0 to ((NX - 2) div CHUNK_X)) k * CHUNK_X])
    [i0, min(NX - 1, i0 + CHUNK_X), j0, min(NZ - 1, j0 + CHUNK_Z)]]
// a triangle stays unless a road's verge replaces it or all of it is covered or under water
pub fn terrain_chunk(c) any^ {
    let i0 = c[0], i1 = c[1], j0 = c[2], j1 = c[3]
    let part = (values) any^ => rows(values, i0, i1, j0, j1)
    let x = part(GX), z = part(GZ);
    {positions: interleave([x, part(H), z]), normals: interleave([part(N0), part(N1), part(N2)]),
     colors: interleave([part(C0), part(C1), part(C2)]), uvs: interleave([x / 6.0, 0.0 - z / 6.0]),
     indices: grid_indices(i1 - i0 + 1, j1 - j0 + 1, part(GONE), part(HIDDEN))}
}

// ---- the far grid out to the horizon, with a skirt hanging from the near grid's rim to hide the seam
let FH = land.FAR
let FNX = land.FNX
let FNZ = land.FNZ
// far triangles go where the near grid covers them or the water does
let FAR_HIDDEN = [for (k, h in FH) if (land.FAR_INSIDE[k] or wet(h)) 1 else 0]
let FAR_GONE = [for (h in FH) 0]
pub fn far_terrain() any^ {
    let n = grid_normals(FH, FNX, FNZ, land.FAR_STEP);
    {positions: interleave([land.FGX, FH, land.FGZ]), normals: interleave(n),
     colors: interleave(terrain_color(land.FGX, land.FGZ, FH, n[1], land.FGCZ, land.FGSAND)),
     uvs: interleave([land.FGX / 6.0, 0.0 - land.FGZ / 6.0]), indices: grid_indices(FNX, FNZ, FAR_GONE, FAR_HIDDEN)}
}
// a curtain down from the near grid's rim
fn rim_points() => [*[for (i in 0 to (NX - 1)) i], *[for (j in 1 to (NZ - 1)) j * NX + NX - 1],
    *[for (i in 1 to (NX - 1)) (NZ - 1) * NX + NX - 1 - i], *[for (j in 1 to (NZ - 1)) (NZ - 1 - j) * NX]]
pub fn rim_skirt() any^ {
    let ks = rim_points()
    let n = len(ks);
    {positions: [for (k in ks, drop in [0.0, 14.0]) *[land.X0 + (k % NX) * land.STEP, H[k] - drop, land.Z0 + (k div NX) * land.STEP]],
     normals: [for (k in ks, drop in [0.0, 14.0]) *[N0[k], N1[k], N2[k]]],
     colors: [for (k in ks, drop in [0.0, 14.0]) *[C0[k], C1[k], C2[k]]],
     uvs: [for (q, k in ks, drop in [0.0, 14.0]) *[q * 1.5, drop / 6.0]],
     indices: [for (q in 0 to (n - 2), let a = 2 * q) *[a, a + 1, a + 2, a + 2, a + 1, a + 3, a, a + 2, a + 1, a + 2, a + 3, a + 1]]}
}

// ---- road ribbons: asphalt carries one texture across its width (u) repeating every ROAD_TILE metres (v)
pub let ROAD_TILE = 24.0
pub let ROAD_EDGE = 5.2             // asphalt half width including the shoulder past the edge line
fn up_normal(pts, k) any^ => (let a = pts[max(0, k - 1)], let b = pts[min(len(pts) - 1, k + 1)],
    let g = (b.y - a.y) / max(0.01, b.s - a.s), let f = u.forward(pts[k].heading), let l = sqrt(1.0 + g * g),
    [0.0 - g * f[0] / l, 1.0 / l, 0.0 - g * f[1] / l])
fn side(p, lat) => (let r = u.right(p.heading), [p.x + r[0] * lat, p.z + r[1] * lat])
// a strip across the samples [a, b] of a road, through the lateral offsets `lats`, each at height
// height(p, lat), with u from `us`; a coloured strip takes the terrain's colours in one array call,
// shaded by the cross slope to the next offset
fn strip(pts, a, b, lats, us, height, colored = false) any^ {
    let n = len(lats)
    let ys = [for (k in a to b, lat in lats) height(pts[k], lat)]
    let xs = [for (k in a to b, lat in lats) side(pts[k], lat)[0]]
    let zs = [for (k in a to b, lat in lats) side(pts[k], lat)[1]];
    {positions: interleave([xs, ys, zs]),
     normals: [for (k in a to b, lat in lats) *up_normal(pts, k)],
     uvs: [for (k in a to b, uu in us) *[uu, pts[k].s / ROAD_TILE]],
     *: (if (not colored) {} else {colors: interleave(terrain_color(xs, zs, ys,
        [for (i, y in ys, let c = i % n, let j = if (c + 1 < n) i + 1 else i - 1) 1.0 / sqrt(1.0 + u.sq((ys[j] - y) / (lats[j % n] - lats[c])))],
        [for (x in xs) land.coast_z(x)], [for (x in xs) land.beach(x)]))}),
     indices: [for (k in 0 to (b - a - 1), c in 0 to (n - 2), let r0 = k * n + c, let r1 = r0 + n) *[r0, r0 + 1, r1, r0 + 1, r1 + 1, r1]]}
}
pub fn asphalt(rd, a = 0, b = -1) any^ => strip(rd.samples, a, if (b < 0) len(rd.samples)^ - 1 else b, [0.0 - ROAD_EDGE, ROAD_EDGE], [0.0, 1.0], (p, lat) => p.y)
// the verge: terrain-coloured ground from under the asphalt's edge out past every dropped triangle,
// one strip per side and per stretch of open road (bridges and the bore have none). Around a tunnel
// portal the strip runs straight across, capping the hill over the bore's mouth.
let VERGE_LATS = [4.9, 6.4, 9.0, 12.5, 17.0, region.VERGE]
let APPROACH_LATS = [4.9, 6.4, 9.0, 12.5, 17.0, 23.5, 31.0]
let CAP_LATS = [for (k in 0 to 12) -30.0 + k * 5.0]
fn sample_kind(p) any^ => if (roads.within("truss", p.s, 5.0) or roads.within("cable", p.s, 5.0)) "span"
    else if (len([for (st in roads.STRUCTURES where st.kind == "tunnel" and (abs(p.s - st.a) < roads.PORTAL or abs(p.s - st.b) < roads.PORTAL)) st]) > 0)
        (if (roads.within("tunnel", p.s)) "cap" else "approach")
    else if (roads.within("tunnel", p.s)) "bore" else "open"
// runs of consecutive loop samples of one verge kind, as [first, last, kind]
fn kind_runs() any^ {
    let pts = roads.LOOP.samples
    let n = len(pts)^
    let kinds = [for (p in pts) sample_kind(p)]
    let starts = [for (k in 0 to (n - 1) where k == 0 or kinds[k] != kinds[k - 1]) k];
    [for (q, a in starts, let b = if (q + 1 < len(starts)) starts[q + 1] else n - 1) [a, b, kinds[a]]]
}
// the loop's samples as flat arrays: position, deck height, arc length, right-hand direction, deck normal
let LP = roads.LOOP.samples
let SX = [for (p in LP) p.x]
let SZ = [for (p in LP) p.z]
let SY = [for (p in LP) p.y]
let SS = [for (p in LP) p.s]
let RX = [for (p in LP) 0.0 - cos(p.heading)]
let RZ = [for (p in LP) sin(p.heading)]
let UP = [for (k, p in LP) up_normal(LP, k)]
let UP0 = [for (n in UP) n[0]]
let UP1 = [for (n in UP) n[1]]
let UP2 = [for (n in UP) n[2]]
// one verge strip in array form: the cut-and-fill land (or, over a bore's mouth, the hill's cover) at
// each offset, the deck's own edge under the asphalt, terrain colours shaded by the cross slope
fn verge_strip(a, b, lats, cap) any^ {
    let n = len(lats)
    let ks = [for (k in a to b, lat in lats) k]
    let ls = [for (k in a to b, lat in lats) lat]
    let xs = [for (q, k in ks) SX[k] + RX[k] * ls[q]]
    let zs = [for (q, k in ks) SZ[k] + RZ[k] * ls[q]]
    let deck = [for (k in ks) SY[k]]
    let nat = land.grid_many(land.NATURAL, xs, zs)
    let off = abs(ls)
    let land_y = if (cap) land.vmax(nat, deck + 10.5 - land.vmax(0.0, off - 8.0) * 0.25)
        else (let target = deck - 0.15, target + (nat - target) * land.smooth(region.FLAT_BAND, roads.REACH, off))
    let edge = if (cap) 0.0 * off else land.step(5.0 - off)
    let ys = edge * (deck - 0.04) + (1.0 - edge) * (land_y - 0.06)
    let ny = [for (i, y in ys, let c = i % n, let j = if (c + 1 < n) i + 1 else i - 1) 1.0 / sqrt(1.0 + u.sq((ys[j] - y) / (ls[j] - ls[i])))]
    let coast = land.columns_at(xs);
    {positions: interleave([xs, ys, zs]), normals: interleave([[for (k in ks) UP0[k]], [for (k in ks) UP1[k]], [for (k in ks) UP2[k]]]),
     uvs: interleave([ls / 6.0, [for (k in ks) SS[k] / ROAD_TILE]]), colors: interleave(terrain_color(xs, zs, ys, ny, coast[0], coast[1])),
     indices: [for (k in 0 to (b - a - 1), c in 0 to (n - 2), let r0 = k * n + c, let r1 = r0 + n) *[r0, r0 + 1, r1, r0 + 1, r1 + 1, r1]]}
}
pub fn verges() any^ {
    let runs = kind_runs()
    let sides = (lats) => [lats, [for (l in reverse(lats)) 0.0 - l]];
    m.merge([for (rn in runs, part in (if (rn[2] == "open") sides(VERGE_LATS) else if (rn[2] == "approach") sides(APPROACH_LATS)
            else if (rn[2] == "cap") [CAP_LATS] else []))
        verge_strip(rn[0], rn[1], part, rn[2] == "cap")^])
}

// ---- the gateway roundabout: an asphalt apron just under the arms, edge markings, the raised island
// with its monument, and a splitter island on each arm
fn disc(x, z, r, y, segments, tile) any^ => {positions: [x, y, z, *[for (k in 0 to (segments - 1), let a = 0.0 - k * u.TAU / segments) *[x + r * cos(a), y, z + r * sin(a)]]],
    normals: [for (k in 0 to segments) *[0.0, 1.0, 0.0]],
    uvs: [x / tile, 0.0 - z / tile, *[for (k in 0 to (segments - 1), let a = 0.0 - k * u.TAU / segments) *[(x + r * cos(a)) / tile, 0.0 - (z + r * sin(a)) / tile]]],
    indices: [for (k in 0 to (segments - 1)) *[0, 1 + k, 1 + (k + 1) % segments]]}
// a flat ring between radii r0 < r1, split into `segments`, optionally only every other one (dashes)
fn annulus(x, z, r0, r1, y, segments, dashed = false) any^ => m.merge([for (k in 0 to (segments - 1),
        let a0 = k * u.TAU / segments, let a1 = (k + (if (dashed) 0.55 else 1.0)) * u.TAU / segments where not dashed or k % 2 == 0)
    m.quad([x + r0 * cos(a0), y, z + r0 * sin(a0)], [x + r0 * cos(a1), y, z + r0 * sin(a1)], [x + r1 * cos(a1), y, z + r1 * sin(a1)], [x + r1 * cos(a0), y, z + r1 * sin(a0)])])
// a curb wall around a circle from y0 to y1, facing out
fn curb_ring(x, z, r, y0, y1, segments) any^ => m.merge([for (k in 0 to (segments - 1), let a0 = k * u.TAU / segments, let a1 = (k + 1) * u.TAU / segments)
    m.quad([x + r * cos(a1), y0, z + r * sin(a1)], [x + r * cos(a0), y0, z + r * sin(a0)], [x + r * cos(a0), y1, z + r * sin(a0)], [x + r * cos(a1), y1, z + r * sin(a1)])])
pub let ARMS = [roads.ARM_CITY, roads.ARM_LOOP_OUT, roads.ARM_LOOP_IN]
// a splitter island along an arm: a raised sliver from the ring's outer edge outward
fn splitter(angle) any^ {
    let c = roads.R1, f = [cos(angle), sin(angle)], r = [0.0 - sin(angle), cos(angle)]
    let at = (d, w) => [c.x + f[0] * d + r[0] * w, c.z + f[1] * d + r[1] * w]
    let pts = [at(c.ring + c.half + 1.0, 0.0), at(c.apron + 6.0, 1.4), at(c.apron + 8.0, 0.0), at(c.apron + 6.0, -1.4)];
    m.merge([m.quad([pts[0][0], 0.16, pts[0][1]], [pts[1][0], 0.16, pts[1][1]], [pts[2][0], 0.16, pts[2][1]], [pts[3][0], 0.16, pts[3][1]]),
        *[for (k in 0 to 3, let p = pts[k], let q = pts[(k + 1) % 4]) m.quad([p[0], 0.0, p[1]], [q[0], 0.0, q[1]], [q[0], 0.16, q[1]], [p[0], 0.16, p[1]])]])
}
// yield dashes across each arm's entry lane, just outside the ring
fn give_way(angle) any^ {
    let c = roads.R1, f = [cos(angle), sin(angle)], r = [0.0 - sin(angle), cos(angle)]
    let d = c.ring + c.half + 0.8;
    m.merge([for (k in 0 to 4, let w0 = 0.6 + k * 1.0, let w1 = w0 + 0.6)
        m.quad([c.x + f[0] * d + r[0] * w0, 0.012, c.z + f[1] * d + r[1] * w0], [c.x + f[0] * d + r[0] * w1, 0.012, c.z + f[1] * d + r[1] * w1],
            [c.x + f[0] * (d + 0.4) + r[0] * w1, 0.012, c.z + f[1] * (d + 0.4) + r[1] * w1], [c.x + f[0] * (d + 0.4) + r[0] * w0, 0.012, c.z + f[1] * (d + 0.4) + r[1] * w0])])
}
pub fn roundabout() any^ {
    let c = roads.R1;
    {apron: disc(c.x, c.z, c.apron, -0.01, 64, 8.0),
     lines: m.merge([annulus(c.x, c.z, c.ring + c.half - 0.6, c.ring + c.half - 0.45, 0.011, 72), annulus(c.x, c.z, c.island + 0.35, c.island + 0.5, 0.011, 48, true),
        *[for (a in ARMS) give_way(a)]]),
     island: disc(c.x, c.z, c.island, region.ISLAND_Y, 40, 6.0),
     curb: m.merge([curb_ring(c.x, c.z, c.island, -0.02, region.ISLAND_Y, 40), *[for (a in ARMS) splitter(a)]]),
     // the monument: a granite plinth and a tapering spire
     stone: m.merge([m.block(c.x, region.ISLAND_Y, c.z, 4.8, 1.2, 4.8), m.post(c.x, c.z, region.ISLAND_Y + 1.2, region.ISLAND_Y + 14.0, 1.25, 0.18, 4)])}
}

// ---- beams for the bridges: an oriented box from a to b, `w` wide and `h` deep. Its width runs along
// `across` (a horizontal [x, z] direction) when given, else horizontally square to the beam
let UNIT_BEAM = m.cuboid(-0.5, -0.5, 0.0, 0.5, 0.5, 1.0)
pub fn beam(a, b, w, h, across = null) any^ {
    let d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]]
    let l = max(sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 0.001)
    let tz = [d[0] / l, d[1] / l, d[2] / l]
    // side = up x t (or `across` with its t part removed), then up' = t x side: a right-handed frame
    let ax = if (across == null) [tz[2], 0.0, 0.0 - tz[0]] else (let k = across[0] * tz[0] + across[1] * tz[2],
        [across[0] - tz[0] * k, 0.0 - tz[1] * k, across[1] - tz[2] * k])
    let sl = sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2])
    let sx = if (sl < 0.001) [1.0, 0.0, 0.0] else [ax[0] / sl, ax[1] / sl, ax[2] / sl]
    let uy = [tz[1] * sx[2] - tz[2] * sx[1], tz[2] * sx[0] - tz[0] * sx[2], tz[0] * sx[1] - tz[1] * sx[0]];
    // one unit box, scaled into the beam's frame
    m.placed(UNIT_BEAM, [sx[0] * w, sx[1] * w, sx[2] * w, 0.0, uy[0] * h, uy[1] * h, uy[2] * h, 0.0, tz[0] * l, tz[1] * l, tz[2] * l, 0.0, a[0], a[1], a[2], 1.0])
}
// a point on a straight span: s metres along from its first sample, lat to the right, h above the deck
fn span_frame(st) any^ {
    let pts = roads.LOOP.samples
    let a = pts[st.start_k], b = pts[st.end_k]
    let l = sqrt(u.sq(b.x - a.x) + u.sq(b.z - a.z))
    let f = [(b.x - a.x) / l, (b.z - a.z) / l], r = [0.0 - f[1], f[0]];
    {a: a, b: b, length: l, f: f, r: r, y: a.y, at: (s, lat, h) => [a.x + f[0] * s + r[0] * lat, a.y + (b.y - a.y) * s / l + h, a.z + f[1] * s + r[1] * lat]}
}
// the river truss bridge: a deck slab on two abutments and a pier, carried by two Pratt trusses with
// lateral bracing overhead
pub fn truss_bridge(st) any^ {
    let sp = span_frame(st)^
    let at = sp.at, l = sp.length
    let n = max(4, int(round(l / 8.0)) ^ { 4 })
    let pl = l / n, top = 6.8, lo = 0.25;
    {deck: m.merge([beam(at(-2.0, 0.0, -0.58), at(l + 2.0, 0.0, -0.58), 14.2, 1.1),
        *[for (sd in [-1.0, 1.0]) beam(at(0.0, sd * 6.55, 0.1), at(l, sd * 6.55, 0.1), 0.35, 0.4)],
        *[for (s in [-1.5, l + 1.5]) beam(at(s, 0.0, -1.1), at(s, 0.0, -12.0), 15.0, 3.0, sp.r)],
        beam(at(l / 2.0, 0.0, -1.1), at(l / 2.0, 0.0, -1.1 - sp.y - 6.0), 9.0, 2.2, sp.r)]),
     steel: m.merge([for (sd in [-1.0, 1.0], let lat = sd * 6.95) *[
        beam(at(0.0, lat, lo), at(l, lat, lo), 0.45, 0.55), beam(at(pl, lat, top), at(l - pl, lat, top), 0.5, 0.5),
        beam(at(0.0, lat, lo), at(pl, lat, top), 0.5, 0.5), beam(at(l, lat, lo), at(l - pl, lat, top), 0.5, 0.5),
        *[for (i in 1 to (n - 1)) beam(at(i * pl, lat, lo), at(i * pl, lat, top), 0.32, 0.32, sp.r)],
        *[for (i in 1 to (n - 2)) if (i < n / 2) beam(at(i * pl, lat, top), at((i + 1) * pl, lat, lo), 0.26, 0.26)
            else beam(at(i * pl, lat, lo), at((i + 1) * pl, lat, top), 0.26, 0.26)],
        *(if (sd > 0.0) [for (i in 1 to (n - 1)) beam(at(i * pl, -6.95, top), at(i * pl, 6.95, top), 0.26, 0.3)] else []),
        *(if (sd > 0.0) [for (i in 1 to (n - 2)) beam(at(i * pl, -6.95, top), at((i + 1) * pl, 6.95, top), 0.16, 0.16)] else [])]])}
}
// the estuary cable-stayed bridge: a box-girder deck, an H pylon in the river mouth and two fans of
// stays, railings, and lamps along the deck
pub let STAYS = 9
pub fn cable_bridge(st) any^ {
    let sp = span_frame(st)^
    let at = sp.at, l = sp.length, mid = l / 2.0
    let foot = land.SEA - 5.0 - sp.y
    let lamps = [for (k in 0 to 6, let s = 12.0 + k * (l - 24.0) / 6.0) [s, if (k % 2 == 0) -7.2 else 7.2]];
    {deck: m.merge([beam(at(-2.0, 0.0, -0.9), at(l + 2.0, 0.0, -0.9), 15.4, 1.8),
        *[for (s in [-1.5, l + 1.5]) beam(at(s, 0.0, -1.8), at(s, 0.0, -10.0), 16.0, 3.0, sp.r)],
        *[for (sd in [-1.0, 1.0]) beam(at(0.0, sd * 6.7, 0.12), at(l, sd * 6.7, 0.12), 0.6, 0.24)]]),
     pylon: m.merge([*[for (sd in [-1.0, 1.0]) beam(at(mid, sd * 9.0, foot), at(mid, sd * 7.3, 54.0), 2.2, 3.0, sp.r)],
        beam(at(mid, -8.6, -2.6), at(mid, 8.6, -2.6), 2.0, 2.4), beam(at(mid, -7.6, 47.0), at(mid, 7.6, 47.0), 1.6, 2.2),
        beam(at(mid, -9.8, foot - 1.0), at(mid, 9.8, foot - 1.0), 6.0, 4.0)]),
     stays: m.merge([for (sd in [-1.0, 1.0], dir in [-1.0, 1.0], i in 0 to (STAYS - 1), let s = mid + dir * (12.0 + i * 9.4) where s > 2.0 and s < l - 2.0)
        beam(at(s, sd * 7.0, 0.5), at(mid, sd * 7.45, 28.0 + i * 2.4), 0.16, 0.16)]),
     rail: m.merge([*[for (sd in [-1.0, 1.0]) beam(at(0.0, sd * 7.05, 1.15), at(l, sd * 7.05, 1.15), 0.08, 0.12)],
        *[for (sd in [-1.0, 1.0], k in 0 to (int(l / 2.5) ^ { 0 })) beam(at(k * 2.5, sd * 7.05, 0.2), at(k * 2.5, sd * 7.05, 1.15), 0.06, 0.06, sp.r)],
        *[for (lp in lamps) beam(at(lp[0], lp[1], 0.2), at(lp[0], lp[1], 8.5), 0.18, 0.18, sp.r)],
        *[for (lp in lamps) beam(at(lp[0], lp[1], 8.4), at(lp[0], lp[1] * 0.72, 8.6), 0.12, 0.12)]]),
     bulbs: m.merge([for (lp in lamps) beam(at(lp[0] - 0.35, lp[1] * 0.72, 8.45), at(lp[0] + 0.35, lp[1] * 0.72, 8.45), 0.28, 0.1)])}
}
// a wing wall standing on the base line a to b ([x, y, z] at its foot), its top falling from h0 to h1
// above the foot, `t` thick toward side `out` (+1 to the base line's right)
fn wing(a, b, h0, h1, out) any^ {
    let dx = b[0] - a[0], dz = b[2] - a[2]
    let l = max(sqrt(dx * dx + dz * dz), 0.001)
    let n = [0.0 - dz / l * out * 0.8, dx / l * out * 0.8]
    let foot = [[a[0], a[2]], [b[0], b[2]], [b[0] + n[0], b[2] + n[1]], [a[0] + n[0], a[2] + n[1]]]
    let tops = [a[1] + h0, b[1] + h1, b[1] + h1, a[1] + h0]
    // hexa wants each ring counter-clockwise on the map (x right, z down the page)
    let area = sum([for (k in 0 to 3, let p = foot[k], let q = foot[(k + 1) % 4]) p[0] * (0.0 - q[1]) - q[0] * (0.0 - p[1])])
    let order = if (area > 0.0) [0, 1, 2, 3] else [3, 2, 1, 0];
    m.hexa([for (k in order) [foot[k][0], a[1], foot[k][1]]], [for (k in order) [foot[k][0], tops[k], foot[k][1]]])
}
// the ridge tunnel: a tiled bore under an arch, lamps along its crown, concrete headwalls with wing
// walls at both portals
let BORE = [[6.3, -0.1], [6.3, 4.4], [5.6, 5.8], [4.2, 6.9], [2.2, 7.5], [0.0, 7.7]]
let BORE_RING = [*BORE, *[for (k in 1 to (len(BORE) - 1), let p = BORE[len(BORE) - 1 - k]) [0.0 - p[0], p[1]]]]
pub fn tunnel(st) any^ {
    let sp = span_frame(st)^
    let at = sp.at, l = sp.length
    let n = max(2, int(ceil(l / 6.0)) ^ { 2 })
    let ring = len(BORE_RING)
    // the bore faces inward: its normals point at the arch's centre
    let inward = [for (p in BORE_RING, let dx = 0.0 - p[0], let dy = 3.6 - p[1], let dl = sqrt(dx * dx + dy * dy)) [dx / dl, dy / dl]]
    let headwall = (s, dir) any^ => m.merge([beam(at(s, 11.6, -1.0), at(s, 11.6, 11.5), 10.6, 1.4, sp.r), beam(at(s, -11.6, -1.0), at(s, -11.6, 11.5), 10.6, 1.4, sp.r),
        beam(at(s, -6.3, 9.6), at(s, 6.3, 9.6), 1.4, 3.8),
        *[for (k in 0 to (len(BORE_RING) - 2), let p = BORE_RING[k], let q = BORE_RING[k + 1] where p[1] > 4.0 or q[1] > 4.0)
            beam(at(s, (p[0] + q[0]) / 2.0, min(p[1], q[1])), at(s, (p[0] + q[0]) / 2.0, 7.9), abs(q[0] - p[0]) + 0.2, 1.4, sp.r)],
        beam(at(s + dir * 0.6, -17.5, 11.7), at(s + dir * 0.6, 17.5, 11.7), 0.9, 0.5),
        *[for (sd in [-1.0, 1.0]) wing(at(s, sd * 17.2, -2.0), at(s + dir * 15.0, sd * 13.5, -2.0), 11.4, 1.0, sd * dir)]]);
    {bore: {positions: [for (k in 0 to n, p in BORE_RING) *at(k * l / n, p[0], p[1])],
            normals: [for (k in 0 to n, q in inward) *[sp.r[0] * q[0], q[1], sp.r[1] * q[0]]],
            uvs: [for (k in 0 to n, c, p in BORE_RING) *[c / 2.0, k * l / n / 4.0]],
            indices: [for (k in 0 to (n - 1), c in 0 to (ring - 2), let a = k * ring + c, let b = a + ring) *[a, a + 1, b, a + 1, b + 1, b]]},
     lamps: m.merge([for (k in 0 to (int(l / 9.0) ^ { 0 }), sd in [-1.0, 1.0]) beam(at(4.0 + k * 9.0, sd * 1.7, 7.32), at(5.6 + k * 9.0, sd * 1.7, 7.32), 0.3, 0.12)]),
     // raised walkways close the floor between the asphalt and each wall
     walks: m.merge([for (sd in [-1.0, 1.0]) beam(at(-1.0, sd * 5.8, 0.05), at(l + 1.0, sd * 5.8, 0.05), 1.1, 0.3)]),
     portals: m.merge([headwall(0.0, -1.0), headwall(l, 1.0)])}
}

// ---- guardrails: a galvanised rail on posts along each railed run
pub fn guardrails() any^ {
    let pts = roads.LOOP.samples
    let at = (k, lat, h) any^ => (let p = pts[k], let q = side(p, lat), [q[0], p.y + h, q[1]]);
    m.merge([for (rl in region.RAILS, k in rl[0] to (rl[1] - 1), let lat = rl[2] * region.RAIL_LAT) *[
        beam(at(k, lat, 0.62), at(k + 1, lat, 0.62), 0.08, 0.32),
        *(if (k % 2 == 0) [beam(at(k, lat + rl[2] * 0.12, 0.0), at(k, lat + rl[2] * 0.12, 0.72), 0.12, 0.12, u.right(pts[k].heading))] else [])]])
}
// white delineator posts every 48 m on open road without a rail
pub fn delineators() any^ {
    let pts = roads.LOOP.samples
    let railed = [for (rl in region.RAILS, k in rl[0] to rl[1]) [k, rl[2]]];
    m.merge([for (k, p in pts, sd in [-1.0, 1.0] where k % 12 == 6 and sample_kind(p)^ == "open" and len([for (r in railed where r[0] == k and r[1] == sd) r]) == 0)
        (let q = side(p, sd * 5.9), m.block(q[0], p.y - 0.05, q[1], 0.14, 1.05, 0.14))])
}
